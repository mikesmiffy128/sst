/*
 * Copyright © 2021 Willian Henrique <wsimanbrazil@yahoo.com.br>
 * Copyright © 2023 Michael Smith <mikesmiffy128@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED “AS IS” AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include <string.h>

#include "con_.h"
#include "engineapi.h"
#include "errmsg.h"
#include "event.h"
#include "feature.h"
#include "gamedata.h"
#include "gameinfo.h"
#include "hook.h"
#include "intdefs.h"
#include "mem.h"
#include "os.h"
#include "ppmagic.h"
#include "sst.h"
#include "vcall.h"
#include "x86.h"
#include "x86util.h"

FEATURE("improved demo recording")
REQUIRE_GAMEDATA(vtidx_StopRecording)

DEF_CVAR(sst_autorecord, "Continuously record demos even after reconnecting", 1,
		CON_ARCHIVE | CON_HIDDEN)

void *demorecorder;
static int *demonum;
static bool *recording;
const char *demorec_basename;
static bool wantstop = false;
bool demorec_forceauto = false;

#define SIGNONSTATE_NEW 3
#define SIGNONSTATE_SPAWN 5
#define SIGNONSTATE_FULL 6

DEF_PREDICATE(DemoControlAllowed, void)
DEF_EVENT(DemoRecordStarting, void)
DEF_EVENT(DemoRecordStopped, int)

typedef void (*VCALLCONV SetSignonState_func)(void *, int);
static SetSignonState_func orig_SetSignonState;
static void VCALLCONV hook_SetSignonState(void *this_, int state) {
	struct CDemoRecorder *this = this_;
	// NEW fires once every map or save load, but only bumps number if demo file
	// was left open (i.e. every transition). bump it unconditionally instead!
	if (state == SIGNONSTATE_NEW) {
		int oldnum = *demonum;
		orig_SetSignonState(this, state);
		*demonum = oldnum + 1;
		return;
	}
	// dumb hack: demo file gets opened on FULL. bumping the number on NEW would
	// make the first demo number 2 so we set the number to 0 in the record
	// command. however if we started recording already in-map we need to bodge
	// it back up to 1 right before the demo actually gets created
	if (state == SIGNONSTATE_FULL && *demonum == 0) *demonum = 1;
	orig_SetSignonState(this, state);
}

typedef void (*VCALLCONV StopRecording_func)(void *);
static StopRecording_func orig_StopRecording;
static void VCALLCONV hook_StopRecording(void *this) {
	bool wasrecording = *recording;
	int lastnum = *demonum;
	orig_StopRecording(this);
	// If the user didn't specifically request the stop, tell the engine to
	// start recording again as soon as it can.
	if (wasrecording && !wantstop && (demorec_forceauto ||
			con_getvari(sst_autorecord))) {
		*recording = true;
		*demonum = lastnum;
	}
	else {
		EMIT_DemoRecordStopped(lastnum);
	}
}

DECL_VFUNC_DYN(void, StartRecording)

static struct con_cmd *cmd_record, *cmd_stop;
static con_cmdcb orig_record_cb, orig_stop_cb;

static void hook_record_cb(const struct con_cmdargs *args) {
	if (!CHECK_DemoControlAllowed()) return;
	bool was = *recording;
	if (!was && args->argc == 2 || args->argc == 3) {
		// safety check: make sure a directory exists, otherwise recording
		// silently fails. this is necessarily TOCTOU, but in practice it's
		// way better than not doing it - just to have a sanity check.
		const char *arg = args->argv[1];
		const char *lastslash = 0;
		for (const char *p = arg; *p; ++p) {
#ifdef _WIN32
			if (*p == '/' || *p == '\\') lastslash = p;
#else
			if (*p == '/') lastslash = p;
#endif
		}
		if (lastslash) {
			int argdirlen = lastslash - arg;
			int gdlen = os_strlen(gameinfo_gamedir);
			if (gdlen + 1 + argdirlen < PATH_MAX) { // if not, too bad
				os_char dir[PATH_MAX], *q = dir;
				memcpy(q, gameinfo_gamedir, gdlen * sizeof(*gameinfo_gamedir));
				q += gdlen;
				*q++ = OS_LIT('/');
				// ascii->wtf16 (probably turns into memcpy() on linux)
				for (const char *p = arg; p - arg < argdirlen; ++p, ++q) {
					*q = (uchar)*p;
				}
				*q = OS_LIT('\0');
				// this is pretty ugly. the error cases would be way tidier if
				// we could use open(O_DIRECTORY), but that's not a thing on
				// windows, of course.
				struct os_stat s;
				static const char *const errpfx = "ERROR: can't record demo: ";
				if (os_stat(dir, &s) == -1) {
					if (errno == ENOENT) {
						con_warn("%ssubdirectory %.*s doesn't exist\n", errpfx,
								argdirlen, arg);
					}
					else {
						con_warn("%s%s\n", errpfx, strerror(errno));
					}
					return;
				}
				if (!S_ISDIR(s.st_mode)) {
					con_warn("%spath %.*s is not a directory\n", errpfx,
							argdirlen, arg);
					return;
				}
			}
		}
	}
	orig_record_cb(args);
	if (!was && *recording) {
		*demonum = 0; // see SetSignonState comment above
		// For UX, make it more obvious we're recording, in particular when not
		// already in a map as the "recording to x.dem" won't come up yet.
		// mike: I think this is questionably necessary but I'm outvoted :)
		con_msg("Demo recording started\n");
	}
	EMIT_DemoRecordStarting();
}

static void hook_stop_cb(const struct con_cmdargs *args) {
	if (!CHECK_DemoControlAllowed()) return;
	wantstop = true;
	orig_stop_cb(args);
	wantstop = false;
}

static inline bool find_demorecorder(void) {
#ifdef _WIN32
	const uchar *insns = (const uchar *)orig_stop_cb;
	// The stop command loads `demorecorder` into ECX to call IsRecording()
	for (const uchar *p = insns; p - insns < 32;) {
		if (p[0] == X86_MOVRMW && p[1] == X86_MODRM(0, 1, 5)) {
			void **indirect = mem_loadptr(p + 2);
			demorecorder = *indirect;
			return true;
		}
		NEXT_INSN(p, "global demorecorder object");
	}
#else
#warning TODO(linux): implement linux equivalent (cdecl!)
#endif
	return false;
}

static inline bool find_recmembers(void *StopRecording) {
#ifdef _WIN32
	const uchar *insns = (uchar *)StopRecording;
	for (const uchar *p = insns; p - insns < 128;) {
		// m_nDemoNumber = 0 -> mov dword ptr [<reg> + off], 0
		// XXX: might end up wanting constants for the MRM field masks?
		if (p[0] == X86_MOVMIW && (p[1] & 0xC0) == 0x80 &&
				mem_load32(p + 6) == 0) {
			demonum = mem_offset(demorecorder, mem_load32(p + 2));
		}
		// m_bRecording = false -> mov byte ptr [<reg> + off], 0
		else if (p[0] == X86_MOVMI8 && (p[1] & 0xC0) == 0x80 && p[6] == 0) {
			recording = mem_offset(demorecorder, mem_load32(p + 2));
		}
		if (recording && demonum) return true; // blegh
		NEXT_INSN(p, "recording state variables");
	}
#else // linux is probably different here idk
#warning TODO(linux): implement linux equivalent (???)
#endif
	return false;
}

static inline bool find_demoname(void *StartRecording) {
#ifdef _WIN32
	const uchar *insns = (uchar *)StartRecording;
	for (const uchar *p = insns; p - insns < 32;) {
		// the function immediately does a Q_strncpy() into a buffer offset from
		// `this` - look for a LEA some time *before* the first call instruction
		if (p[0] == X86_CALL) return false;
		if (p[0] == X86_LEA && (p[1] & 0xC0) == 0x80) {
			demorec_basename = mem_offset(demorecorder, mem_load32(p + 2));
			return true;
		}
		NEXT_INSN(p, "demo basename variable");
	}
#else
#warning TODO(linux): implement linux equivalent (???)
#endif
	return false;
}

bool demorec_start(const char *name) {
	bool was = *recording;
	if (was) return false;
	// dumb but easy way to do this: call the record command callback. note:
	// this args object is very incomplete by enough to make the command work
	struct con_cmdargs args = {.argc = 2, .argv = {0, name, 0}};
	orig_record_cb(&args);
	if (!was && *recording) *demonum = 0; // same logic as in the hook
	EMIT_DemoRecordStarting();
	return *recording;
}

int demorec_stop(void) {
	// note: our set-to-0-and-back hack actually has the nice side effect of
	// making this correct when recording and stopping in the menu lol
	int ret = *demonum;
	orig_StopRecording(demorecorder);
	EMIT_DemoRecordStopped(ret);
	return ret;
}

int demorec_demonum(void) {
	return *recording ? *demonum : -1;
}

INIT {
	cmd_record = con_findcmd("record");
	orig_record_cb = con_getcmdcb(cmd_record);
	cmd_stop = con_findcmd("stop");
	orig_stop_cb = con_getcmdcb(cmd_stop);
	if (!find_demorecorder()) {
		errmsg_errorx("couldn't find demo recorder instance");
		return false;
	}
	void **vtable = mem_loadptr(demorecorder);
	// XXX: 16 is totally arbitrary here! figure out proper bounds later
	if (!os_mprot(vtable, 16 * sizeof(void *), PAGE_READWRITE)) {
		errmsg_errorsys("couldn't make virtual table writable");
		return false;
	}
	if (!find_recmembers(vtable[vtidx_StopRecording])) {
		errmsg_errorx("couldn't find recording state variables");
		return false;
	}
	if (!find_demoname(vtable[vtidx_StartRecording])) {
		errmsg_errorx("couldn't find demo basename variable");
		return false;
	}

	orig_SetSignonState = (SetSignonState_func)hook_vtable(vtable,
			vtidx_SetSignonState, (void *)&hook_SetSignonState);
	orig_StopRecording = (StopRecording_func)hook_vtable(vtable,
			vtidx_StopRecording, (void *)&hook_StopRecording);

	cmd_record->cb = &hook_record_cb;
	cmd_stop->cb = &hook_stop_cb;

	sst_autorecord->base.flags &= ~CON_HIDDEN;
	return true;
}

END {
	if (!sst_userunloaded) return;
	// avoid dumb edge case if someone somehow records and immediately unloads
	if (*recording && *demonum == 0) *demonum = 1;
	void **vtable = *(void ***)demorecorder;
	unhook_vtable(vtable, vtidx_SetSignonState, (void *)orig_SetSignonState);
	unhook_vtable(vtable, vtidx_StopRecording, (void *)orig_StopRecording);
	cmd_record->cb = orig_record_cb;
	cmd_stop->cb = orig_stop_cb;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
