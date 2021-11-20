/*
 * Copyright © 2021 Willian Henrique <wsimanbrazil@yahoo.com.br>
 * Copyright © 2021 Michael Smith <mikesmiffy128@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdbool.h>

#include "con_.h"
#include "hook.h"
#include "gamedata.h"
#include "intdefs.h"
#include "mem.h"
#include "os.h"
#include "udis86.h"
#include "vcall.h"

#define SIGNONSTATE_SPAWN 5 // ready to receive entity packets
#define SIGNONSTATE_FULL 6 // fully connected, first non-delta packet receieved

typedef void (*VCALLCONV f_StopRecording)(void *);
typedef void (*VCALLCONV f_SetSignonState)(void *, int);

static void *demorecorder;
static struct con_cmd *cmd_stop;
static bool *recording;
static int *demonum;
static f_SetSignonState orig_SetSignonState;
static f_StopRecording orig_StopRecording;
static con_cmdcb orig_stop_callback;

static int auto_demonum = 1;
static bool auto_recording = false;

DEF_CVAR(sst_autorecord, "Continue recording demos through map changes",
		"0", CON_ARCHIVE | CON_HIDDEN)

static void VCALLCONV hook_StopRecording(void *this) {
	// This hook will get called twice per loaded save (in most games/versions,
	// at least, according to SAR people): first with m_bLoadgame set to false
	// and then with it set to true. This will set m_nDemoNumber to 0 and
	// m_bRecording to false
	orig_StopRecording(this);

	if (auto_recording && con_getvari(sst_autorecord)) {
		*demonum = auto_demonum;
		*recording = true;
	}
	else {
		auto_demonum = 1;
		auto_recording = false;
	}
}

static void VCALLCONV hook_SetSignonState(void *this, int state) {
	// SIGNONSTATE_FULL *may* happen twice per load, depending on the game, so
	// use SIGNONSTATE_SPAWN for demo number increase
	if (state == SIGNONSTATE_SPAWN && auto_recording) auto_demonum++;
	// Starting a demo recording will call this function with SIGNONSTATE_FULL
	// After a load, the engine's demo recorder will only start recording when
	// it reaches this state, so this is a good time to set the flag if needed
	else if (state == SIGNONSTATE_FULL) {
		// Changing sessions may unset the recording flag (or so says NeKzor),
		// so if we want to be recording, we want to tell the engine to record.
		// But also, if the engine is already recording, we want our state to
		// reflect *that*. IOW, if either thing is set, also set the other one.
		auto_recording |= *recording; *recording = auto_recording;

		// FIXME: this will override demonum incorrectly if the plugin is
		// loaded while demos are already being recorded
		if (auto_recording) *demonum = auto_demonum;
	}
	orig_SetSignonState(this, state);
}

static void hook_stop_callback(const struct con_cmdargs *args) {
	auto_recording = false;
	orig_stop_callback(args);
}

// This finds the "demorecorder" global variable (the engine-wide CDemoRecorder
// instance).
static inline void *find_demorecorder(struct con_cmd *cmd_stop) {
	// The "stop" command calls the virtual function demorecorder.IsRecording(),
	// so just look for the load of the "this" pointer
	struct ud udis;
	ud_init(&udis);
	ud_set_mode(&udis, 32);
	ud_set_input_buffer(&udis, (uchar *)con_getcmdcb(cmd_stop), 32);
	while (ud_disassemble(&udis)) {
#ifdef _WIN32
		if (ud_insn_mnemonic(&udis) == UD_Imov) {
			const struct ud_operand *dest = ud_insn_opr(&udis, 0);
			const struct ud_operand *src = ud_insn_opr(&udis, 1);
			// looking for a mov from an address into ECX, as per thiscall
			if (dest->type == UD_OP_REG && dest->base == UD_R_ECX &&
					src->type == UD_OP_MEM) {
				return *(void **)src->lval.udword;
			}
		}
#else
#error TODO(linux): implement linux equivalent (cdecl!)
#endif
	}
	return 0;
}

// This finds "m_bRecording" and "m_nDemoNumber" using the pointer to the
// original "StopRecording" demorecorder function
static inline bool find_recmembers(void *stop_recording_func, void *demorec) {
	struct ud udis;
	ud_init(&udis);
	ud_set_mode(&udis, 32);
	// TODO(opt): consider the below: is it really needed? does it matter?
	// way overshooting the size of the function in bytes to make sure it
	// accomodates for possible differences in different games. we make sure
	// to stop early if we find a RET so should be fine
	ud_set_input_buffer(&udis, (uchar *)stop_recording_func, 200);
	while (ud_disassemble(&udis)) {
#ifdef _WIN32
		enum ud_mnemonic_code code = ud_insn_mnemonic(&udis);
		if (code == UD_Imov) {
			const struct ud_operand *dest = ud_insn_opr(&udis, 0);
			const struct ud_operand *src = ud_insn_opr(&udis, 1);
			// m_nDemoNumber and m_bRecording are both set to 0
			// looking for movs with immediates equal to 0
			// the byte immediate refers to m_bRecording
			if (src->type == UD_OP_IMM && src->lval.ubyte == 0) {
				if (src->size == 8) {
					recording = (bool *)mem_offset(demorec, dest->lval.udword);
				}
				else {
					demonum = (int *)mem_offset(demorec, dest->lval.udword);
				}
				if (recording && demonum) return true; // blegh
			}
		}
		else if (code == UD_Iret) {
			return false;
		}
#else // linux is probably different here idk
#error TODO(linux): implement linux equivalent
#endif
	}
	return false;
}

bool demorec_init(void) {
	if (!gamedata_has_vtidx_SetSignonState ||
			!gamedata_has_vtidx_StopRecording) {
		con_warn("demorec: missing gamedata entries for this engine\n");
		return false;
	}

	cmd_stop = con_findcmd("stop");
	if (!cmd_stop) { // can *this* even happen? I hope not!
		con_warn("demorec: couldn't find \"stop\" command\n");
		return false;
	}

	demorecorder = find_demorecorder(cmd_stop);
	if (!demorecorder) {
		con_warn("demorec: couldn't find demo recorder instance\n");
		return false;
	}

	void **vtable = *(void ***)demorecorder;

	// XXX: 16 is totally arbitrary here! figure out proper bounds later
	if (!os_mprot(vtable, 16 * sizeof(void *), PAGE_EXECUTE_READWRITE)) {
#ifdef _WIN32
		char err[128];
		OS_WINDOWS_ERROR(err);
#else
		const char *err = strerror(errno);
#endif
		con_warn("demorec: couldn't unprotect CDemoRecorder vtable: %s\n", err);
		return false;
	}

	if (!find_recmembers(vtable[7], demorecorder)) {
		con_warn("demorec: couldn't find m_bRecording and m_nDemoNumber\n");
		return false;
	}

	orig_SetSignonState = (f_SetSignonState)hook_vtable(vtable,
			gamedata_vtidx_SetSignonState, (void *)&hook_SetSignonState);
	orig_StopRecording = (f_StopRecording)hook_vtable(vtable,
			gamedata_vtidx_StopRecording, (void *)&hook_StopRecording);

	orig_stop_callback = cmd_stop->cb;
	cmd_stop->cb = &hook_stop_callback;

	sst_autorecord->base.flags &= ~CON_HIDDEN;
	return true;
}

void demorec_end(void) {
	void **vtable = *(void ***)demorecorder;
	unhook_vtable(vtable, gamedata_vtidx_SetSignonState,
			(void *)orig_SetSignonState);
	unhook_vtable(vtable, gamedata_vtidx_StopRecording,
			(void *)orig_StopRecording);
	cmd_stop->cb = orig_stop_callback;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
