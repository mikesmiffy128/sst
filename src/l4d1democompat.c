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
#include "asm.h"
#include "chunklets/x86.h"
#include "con_.h"
#include "errmsg.h"
#include "feature.h"
#include "gametype.h"
#include "hook.h"
#include "intdefs.h"
#include "mem.h"
#include "sst.h"
#include "vcall.h"
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
DEF_INLINE_HOOK_NOTRAMPOLINE(GetHostVersion_func, GetHostVersion)

typedef void (*VCALLCONV ReadDemoHeader_func)(struct CDemoFile *);
DEF_INLINE_HOOK(ReadDemoHeader_func, ReadDemoHeader)

int _l4d1democompat_demover; // extern for asm
static int gameversion;

static int hookdest_GetHostVersion() {
	// If the demo version is 1022 or later, and not newer than the version we
	// are currently using, then we spoof the game version to let the demo play.
	if (_l4d1democompat_demover >= 1022 &&
			_l4d1democompat_demover <= gameversion) {
		return _l4d1democompat_demover;
	}
	return gameversion;
}

int *_l4d1democompat_thisproto; // extern for asm
static void VCALLCONV hookdest_ReadDemoHeader(struct CDemoFile *this) {
	// The mid-function hook needs to get the protocol from `this`, but by that
	// point we won't be able to rely on the ECX register and/or any particular
	// stack spill layout. So... offset the pointer and stick it in a global.
	_l4d1democompat_thisproto = getptr_CDemoFile_protocol(this);
	orig_ReadDemoHeader(this);
}

// should this be in its own .S file? meh, leaving it here for now.
__asm (
	".pushsection " ASM_RWX_SECTION_STR ", \"" ASM_RWX_SECTION_FLAGS "\"\n"
	".globl " ASM_MANGLE_STR("_l4d1democompat_midpoint_hook") "\n"
	ASM_MANGLE_STR("_l4d1democompat_midpoint_hook") ":\n"
	// like in con_.S, we have to assemble stuff manually here, annoyingly.
	// note: ebx is safe to clobber here because we're hooking immediately after
	// a call, ebx is caller-save, and also ebx is explicitly pushed before the
	// call which means we know the code we're hooking obeys the calling conv.
	// see also find_midpoint() below.
	// mov ebx, [_l4d1democompat_thisproto]
	".byte 0x8B\n"
	".byte 0x1D\n"
	".long " ASM_MANGLE_STR("_l4d1democompat_thisproto") "\n"
	// mov ebx, [ebx]
	".byte 0x8B\n"
	".byte 0x1B\n"
	// mov [_l4d1democompat_demover], ebx
	".byte 0x89\n"
	".byte 0x1D\n"
	".long " ASM_MANGLE_STR("_l4d1democompat_demover") "\n"
	// and then we leave trampoline space immediately after; we can jump
	// straight back from here to the rest of the function!
	".globl " ASM_MANGLE_STR("_l4d1democompat_midpoint_trampoline") "\n"
	ASM_MANGLE_STR("_l4d1democompat_midpoint_trampoline") ":\n"
	".space 24\n"
	".popsection\n"
);
// XXX: dummy function prototype. not to be called directly, just to pass into
// the inline hooking machinery.
// maybe one day we'll come up with a marginally cleaner way of doing these
// midpoint hooks, although there's not many uses currently so no big deal.
int _l4d1democompat_midpoint_hook();
int _l4d1democompat_midpoint_trampoline();
DEF_INLINE_HOOK_WITHTRAMPOLINE(int (*)(), ReadDemoHeader_midpoint,
		_l4d1democompat_midpoint_trampoline)

static inline ReadDemoHeader_func find_ReadDemoHeader(const uchar *insns) {
	// Find the call to ReadDemoHeader in the listdemo callback
	for (const uchar *p = insns; p - insns < 192;) {
		if (p[0] == X86_LEA && p[1] == X86_MODRM(2, 1, 4) && p[2] == 0x24 &&
				p[7] == X86_CALL && p[12] == X86_LEA &&
				p[13] == X86_MODRM(2, 1, 4) && p[14] == 0x24) {
			return (ReadDemoHeader_func)(p + 12 + mem_loads32(p + 8));
		}
		NEXT_INSN(p, "ReadDemoHeader");
	}
	return 0;
}

static inline void *find_midpoint(ReadDemoHeader_func ReadDemoHeader) {
	uchar *insns = (uchar *)ReadDemoHeader;
	for (uchar *p = insns; p - insns < 128;) {
		if (p[0] == X86_PUSHIW && p[5] == X86_PUSHEBX && p[6] == X86_CALL &&
				!memcmp(mem_loadptr(p + 1), "HL2DEMO", 7)) {
			return p + 11;
		}
		NEXT_INSN(p, "ReadDemoHeader hook midpoint");
	}
	return 0;
}

static inline GetHostVersion_func find_GetHostVersion(
		ReadDemoHeader_func ReadDemoHeader) {
	uchar *insns = (uchar *)ReadDemoHeader;
	int jzcnt = 0;
	for (uchar *p = insns; p - insns < 192;) {
		// GetHostVersion() is called right after the third JZ insn in
		// ReadDemoHeader()
		if (p[0] == X86_JZ && ++jzcnt == 3) {
			return (GetHostVersion_func)(p + 7 + mem_loads32(p + 3));
		}
		NEXT_INSN(p, "GetHostVersion");
	}
	return 0;
}

INIT {
	struct con_cmd *cmd_listdemo = con_findcmd("listdemo");
	if_cold (!cmd_listdemo) return FEAT_INCOMPAT; // should never happen!
	ReadDemoHeader_func ReadDemoHeader = find_ReadDemoHeader(
			cmd_listdemo->cb_insns);
	if_cold (!ReadDemoHeader) {
		errmsg_errorx("couldn't find ReadDemoHeader function");
		return FEAT_INCOMPAT;
	}
	void *midpoint = find_midpoint(ReadDemoHeader);
	if_cold (!midpoint) {
		errmsg_errorx("couldn't find mid-point for ReadDemoHeader hook");
		return FEAT_INCOMPAT;
	}
	GetHostVersion_func GetHostVersion = find_GetHostVersion(ReadDemoHeader);
	if_cold (!GetHostVersion) {
		errmsg_errorx("couldn't find GetHostVersion function");
		return FEAT_INCOMPAT;
	}
	gameversion = GetHostVersion();
	int err = hook_featsetup_GetHostVersion(GetHostVersion);
	if_cold (err) return err;
	struct hook_featsetup_ret_ReadDemoHeader h2 =
			hook_featsetup_ReadDemoHeader(ReadDemoHeader);
	if_cold (h2.err) return h2.err;
	struct hook_featsetup_ret_ReadDemoHeader_midpoint h3 =
			hook_featsetup_ReadDemoHeader_midpoint((int (*)())midpoint);
	if_cold (h3.err) return h3.err;
	hook_commit_GetHostVersion(&hookdest_GetHostVersion);
	hook_commit_ReadDemoHeader(h2.hookpos, &hookdest_ReadDemoHeader);
	hook_commit_ReadDemoHeader_midpoint(h3.hookpos,
			&_l4d1democompat_midpoint_hook);
	return FEAT_OK;
}

END {
	if_cold (sst_userunloaded) {
		unhook_ReadDemoHeader_midpoint();
		unhook_ReadDemoHeader();
		unhook_GetHostVersion();
	}
}

// vi: sw=4 ts=4 noet tw=80 cc=80
