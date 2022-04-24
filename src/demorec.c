/*
 * Copyright © 2021 Willian Henrique <wsimanbrazil@yahoo.com.br>
 * Copyright © 2022 Michael Smith <mikesmiffy128@gmail.com>
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

#include <stdbool.h>
#include <string.h>

#include "bitbuf.h"
#include "con_.h"
#include "demorec.h"
#include "hook.h"
#include "factory.h"
#include "gamedata.h"
#include "intdefs.h"
#include "mem.h"
#include "os.h"
#include "ppmagic.h"
#include "vcall.h"
#include "x86.h"

#define SIGNONSTATE_SPAWN 5 // ready to receive entity packets
#define SIGNONSTATE_FULL 6 // fully connected, first non-delta packet received

typedef void (*VCALLCONV f_StopRecording)(void *);
typedef void (*VCALLCONV f_SetSignonState)(void *, int);

static void *demorecorder;
static struct con_cmd *cmd_stop;
static bool *recording;
static int *demonum;
static f_SetSignonState orig_SetSignonState;
static f_StopRecording orig_StopRecording;
static con_cmdcb orig_stop_callback;
static int nbits_msgtype;
static int nbits_datalen;

static int auto_demonum = 1;
static bool auto_recording = false;

DEF_CVAR(sst_autorecord, "Continue recording demos through map changes", 1,
		CON_ARCHIVE | CON_HIDDEN)

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

// The engine allows usermessages up to 255 bytes, we add 2 bytes of overhead,
// and then there's the leading bits before that too (see create_message)
static char bb_buf[DEMOREC_CUSTOM_MSG_MAX + 4];
static struct bitbuf bb = {
	bb_buf, sizeof(bb_buf), sizeof(bb_buf) * 8, 0, false, false, "SST"
};

static void create_message(struct bitbuf *msg, const void *buf, int len) {
	// The way we pack our custom demo data is via a user message packet with
	// type "HudText" - this causes the client to do a text lookup which will
	// simply silently fail on invalid keys. By making the first byte null
	// (creating an empty string), we get the rest of the packet to stick in
	// whatever other data we want.
	//
	// Notes from Uncrafted:
	// > But yeah the data you want to append is as follows:
	// > - 6 bits (5 bits in older versions) for the message type - should be 23
	// >   for user message
	bitbuf_appendbits(msg, 23, nbits_msgtype);
	// > - 1 byte for the user message type - should be 2 for HudText
	bitbuf_appendbyte(msg, 2);
	// > - ~~an int~~ 11 or 12 bits for the length of your data in bits,
	// NOTE: this assumes len <= 254
	bitbuf_appendbits(msg, len * 8, nbits_datalen);
	// > - your data
	// [first the aforementioned null byte, plus an arbitrary marker byte to
	// avoid confusion when parsing the demo later...
	bitbuf_appendbyte(msg, 0);
	bitbuf_appendbyte(msg, 0xAC);
	// ... and then just the data itself]
	bitbuf_appendbuf(msg, buf, len);
	// Thanks Uncrafted, very cool!
}

typedef void (*VCALLCONV WriteMessages_func)(void *this, struct bitbuf *msg);
static WriteMessages_func WriteMessages = 0;

void demorec_writecustom(void *buf, int len) {
	create_message(&bb, buf, len);
	WriteMessages(demorecorder, &bb);
	bitbuf_reset(&bb);
}

// XXX: probably want some general foreach-instruction macro once we start doing
// this kind of hackery in multiple different places
#define NEXT_INSN(p) do { \
	int _len = x86_len(p); \
	if (_len == -1) { \
		con_warn("demorec: %s: unknown or invalid instruction\n", __func__); \
		return false; \
	} \
	(p) += _len; \
} while (0)

// This finds the "demorecorder" global variable (the engine-wide CDemoRecorder
// instance).
static inline bool find_demorecorder(struct con_cmd *cmd_stop) {
#ifdef _WIN32
	uchar *stopcb = (uchar *)con_getcmdcb(cmd_stop);
	// The "stop" command calls the virtual function demorecorder.IsRecording(),
	// so just look for the load of the "this" pointer into ECX
	for (uchar *p = stopcb; p - stopcb < 32;) {
		if (p[0] == X86_MOVRMW && p[1] == X86_MODRM(0, 1, 5)) {
			void **indirect = mem_loadptr(p + 2);
			demorecorder = *indirect;
			return true;
		}
		NEXT_INSN(p);
	}
#else
#warning TODO(linux): implement linux equivalent (cdecl!)
#endif
	return false;
}

// This finds "m_bRecording" and "m_nDemoNumber" using the pointer to the
// original "StopRecording" demorecorder function.
static inline bool find_recmembers(void *stoprecording) {
#ifdef _WIN32
	for (uchar *p = (uchar *)stoprecording; p - (uchar *)stoprecording < 128;) {
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
		NEXT_INSN(p);
	}
#else // linux is probably different here idk
#warning TODO(linux): implement linux equivalent (???)
#endif
	return false;
}

// This finds the CDemoRecorder::WriteMessages() function, which takes a raw
// network packet, wraps it up in the appropriate demo framing format and writes
// it out to the demo file being recorded.
static bool find_WriteMessages(void) {
	// TODO(compat): probably rewrite this to just scan for a call instruction!
	const uchar *insns = (*(uchar ***)demorecorder)[gamedata_vtidx_RecordPacket];
	// RecordPacket calls WriteMessages pretty much right away:
	// 56           push  esi
	// 57           push  edi
	// 8B F1        mov   esi,ecx
	// 8D BE        lea   edi,[esi + 0x68c]
	// 8C 06 00 00
	// 57           push  edi
	// E8           call  CDemoRecorder_WriteMessages
	// B0 EF FF FF
	// So we just double check the byte pattern...
	static const uchar bytes[] =
#ifdef _WIN32
		HEXBYTES(56, 57, 8B, F1, 8D, BE, 8C, 06, 00, 00, 57, E8);
#else
#warning This is possibly different on Linux too, have a look!
		{-1, -1, -1, -1, -1, -1};
#endif
	if (!memcmp(insns, bytes, sizeof(bytes))) {
		ssize off = mem_loadoffset(insns + sizeof(bytes));
		// ... and then offset is relative to the address of whatever is _after_
		// the call instruction... because x86.
		WriteMessages = (WriteMessages_func)(insns + sizeof(bytes) + 4 + off);
		return true;
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
	if (!find_demorecorder(cmd_stop)) {
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

	if (!find_recmembers(vtable[7])) { // XXX: stop hardcoding this!?
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

// make custom data a separate feature so we don't lose autorecording if we
// can't find the WriteMessage stuff
bool demorec_custom_init(void) { 
	if (!gamedata_has_vtidx_GetEngineBuildNumber ||
			!gamedata_has_vtidx_RecordPacket) {
		con_warn("demorec: custom: missing gamedata entries for this engine\n");
		return false;
	}

	// More UncraftedkNowledge:
	// > yeah okay so [the usermessage length is] 11 bits if the demo protocol
	// > is 11 or if the game is l4d2 and the network protocol is 2042.
	// > otherwise it's 12 bits
	// > there might be some other l4d2 versions where it's 11 but idk
	// So here we have to figure out the network protocol version!
	void *clientiface;
	uint buildnum;
	// TODO(compat): probably expose VEngineClient/VEngineServer some other way
	// if it's useful elsewhere later!?
	if (clientiface = factory_engine("VEngineClient013", 0)) {
		typedef uint (*VCALLCONV GetEngineBuildNumber_func)(void *this);
		buildnum = (*(GetEngineBuildNumber_func **)clientiface)[
				gamedata_vtidx_GetEngineBuildNumber](clientiface);
	}
	// add support for other interfaces here:
	// else if (clientiface = factory_engine("VEngineClient0XX", 0)) {
	//     ...
	// }
	else {
		return false;
	}
	// condition is redundant until other GetEngineBuildNumber offsets are added
	// if (GAMETYPE_MATCHES(L4D2)) {
		nbits_msgtype = 6;
		// based on Some Code I Read, buildnum *should* be the protocol version,
		// however L4D2 returns the actual game version instead, because sure
		// why not. The only practical difference though is that the network
		// protocol froze after 2042, so we just have to do a >=. No big deal
		// really.
		if (buildnum >= 2042) nbits_datalen = 11; else nbits_datalen = 12;
	// }
	
	return find_WriteMessages();
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
