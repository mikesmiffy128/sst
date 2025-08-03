/*
 * Copyright © Hayden K <imaciidz@gmail.com>
 * Copyright © Willian Henrique <wsimanbrazil@yahoo.com.br>
 * Copyright © Michael Smith <mikesmiffy128@gmail.com>
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

#include "accessor.h"
#include "con_.h"
#include "errmsg.h"
#include "feature.h"
#include "hook.h"
#include "intdefs.h"
#include "mem.h"
#include "sst.h"
#include "vcall.h"
#include "x86.h"
#include "x86util.h"

FEATURE("Left 4 Dead 1 demo file backwards compatibility")
GAMESPECIFIC(L4D1_1022plus)

struct CDemoFile;
// NOTE: not bothering to put this in gamedata since it's actually a constant.
// We could optimise the gamedata system further to constant-fold things with no
// leaves beyond the GAMESPECIFIC cutoff or whatever. But that sounds annoying.
#define off_CDemoFile_protocol 272
DEF_ACCESSORS(struct CDemoFile, int, CDemoFile_protocol)

// L4D1 bumps the demo protocol version with every update to the game, which
// means whenever there is a security update, you cannot watch old demos. From
// minimal testing, it seems demos recorded on version 1022 and onwards are
// compatible with the latest version of the game, so this code lets us watch
// 1022+ demos on any later version of the game.

typedef int (*GetHostVersion_func)();
static GetHostVersion_func orig_GetHostVersion;

typedef void (*VCALLCONV ReadDemoHeader_func)(void *);
static ReadDemoHeader_func orig_ReadDemoHeader;

static inline bool find_ReadDemoHeader(const uchar *insns) {
	// Find the call to ReadDemoHeader in the listdemo callback
	for (const uchar *p = insns; p - insns < 192;) {
		if (p[0] == X86_LEA && p[1] == X86_MODRM(2, 1, 4) && p[2] == 0x24 &&
				p[7] == X86_CALL && p[12] == X86_LEA &&
				p[13] == X86_MODRM(2, 1, 4) && p[14] == 0x24) {
			orig_ReadDemoHeader =
					(ReadDemoHeader_func)(p + 12 + mem_loads32(p + 8));
			return true;
		}
		NEXT_INSN(p, "ReadDemoHeader");
	}
	return false;
}

static void *ReadDemoHeader_midpoint;

static inline bool find_midpoint() {
	uchar *insns = (uchar *)orig_ReadDemoHeader;
	for (uchar *p = insns; p - insns < 128;) {
		if (p[0] == X86_PUSHIW && p[5] == X86_PUSHEBX && p[6] == X86_CALL &&
				!memcmp(mem_loadptr(p + 1), "HL2DEMO", 7)) {
			ReadDemoHeader_midpoint = (p + 11);
			return true;
		}
		NEXT_INSN(p, "ReadDemoHeader hook midpoint");
	}
	return false;
}

static inline bool find_GetHostVersion() {
	uchar *insns = (uchar *)orig_ReadDemoHeader;
	int jzcnt = 0;
	for (uchar *p = insns; p - insns < 192;) {
		// GetHostVersion() is called right after the third JZ insn in
		// ReadDemoHeader()
		if (p[0] == X86_JZ && ++jzcnt == 3) {
			orig_GetHostVersion =
					(GetHostVersion_func)(p + 7 + mem_loads32(p + 3));
			return true;
		}
		NEXT_INSN(p, "GetHostVersion");
	}
	return false;
}

static int demoversion, gameversion;

static int hook_GetHostVersion() {
	// If the demo version is 1022 or later, and not newer than the version we
	// are currently using, then we spoof the game version to let the demo play.
	if (demoversion >= 1022 && demoversion <= gameversion) return demoversion;
	return gameversion;
}

static int *this_protocol;
static void VCALLCONV hook_ReadDemoHeader(struct CDemoFile *this) {
	// The mid-function hook needs to get the protocol from `this`, but by that
	// point we won't be able to rely on the ECX register and/or any particular
	// stack spill layout. So... offset the pointer and stick it in a global.
	this_protocol = getptr_CDemoFile_protocol(this);
	orig_ReadDemoHeader(this);
}

static asm_only int hook_midpoint() {
	__asm volatile (
		"push eax\n"
		"mov eax, %1\n"
		"mov eax, [eax]\n" // dereference this_protocol
		"mov %0, eax\n" // store in demoversion
		"pop eax\n"
		"jmp dword ptr %2\n"
		: "=m" (demoversion)
		: "m" (this_protocol), "m" (ReadDemoHeader_midpoint)
	);
}

INIT {
	struct con_cmd *cmd_listdemo = con_findcmd("listdemo");
	if_cold (!cmd_listdemo) return FEAT_INCOMPAT; // should never happen!
	if_cold (!find_ReadDemoHeader(cmd_listdemo->cb_insns)) {
		errmsg_errorx("couldn't find ReadDemoHeader function");
		return FEAT_INCOMPAT;
	}
	if_cold (!find_midpoint()) {
		errmsg_errorx("couldn't find mid-point for ReadDemoHeader hook");
		return FEAT_INCOMPAT;
	}
	if_cold (!find_GetHostVersion()) {
		errmsg_errorx("couldn't find GetHostVersion function");
		return FEAT_INCOMPAT;
	}
	gameversion = orig_GetHostVersion();
	struct hook_inline_featsetup_ret h1 = hook_inline_featsetup(
			(void *)orig_GetHostVersion, (void **)&orig_GetHostVersion,
			"GetHostVersion");
	if_cold (h1.err) return h1.err;
	struct hook_inline_featsetup_ret h2 = hook_inline_featsetup(
			(void *)orig_ReadDemoHeader, (void **)&orig_ReadDemoHeader,
			"ReadDemoHeader");
	if_cold (h2.err) return h2.err;
	struct hook_inline_featsetup_ret h3 = hook_inline_featsetup(
			ReadDemoHeader_midpoint, &ReadDemoHeader_midpoint,
			"ReadDemoHeader midpoint");
	if_cold (h3.err) return h3.err;
	hook_inline_commit(h1.prologue, (void *)&hook_GetHostVersion);
	hook_inline_commit(h2.prologue, (void *)&hook_ReadDemoHeader);
	hook_inline_commit(h3.prologue, (void *)&hook_midpoint);
	return FEAT_OK;
}

END {
	if_cold (sst_userunloaded) {
		unhook_inline((void *)ReadDemoHeader_midpoint);
		unhook_inline((void *)orig_ReadDemoHeader);
		unhook_inline((void *)orig_GetHostVersion);
	}
}

// vi: sw=4 ts=4 noet tw=80 cc=80
