/*
 * Copyright © Willian Henrique <wsimanbrazil@yahoo.com.br>
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

#include "chunklets/x86.h"
#include "con_.h"
#include "engineapi.h"
#include "errmsg.h"
#include "feature.h"
#include "gamedata.h"
#include "intdefs.h"
#include "langext.h"
#include "mem.h"
#include "x86util.h"

FEATURE("Portal \"ISG\" state reset (experimental)")
GAMESPECIFIC(Portal1)
REQUIRE_GAMEDATA(vtidx_CreateEnvironment)
REQUIRE_GAMEDATA(vtidx_CreatePolyObject)
REQUIRE_GAMEDATA(vtidx_RecheckCollisionFilter)

static bool *isg_flag;
static con_cmdcbv2 disconnect_cb;

DEF_FEAT_CCMD_HERE(sst_portal_resetisg,
		"Remove \"ISG\" state and disconnect from the server", 0) {
	// TODO(compat): OE? guess it might work by accident due to cdecl, find out
	disconnect_cb(&(struct con_cmdargs){0});
	*isg_flag = false;
}

static void **find_physenv_vtable(void *CreateEnvironment) {
	const uchar *insns = (uchar *)CreateEnvironment;
	for (const uchar *p = insns; p - insns < 16;) {
		if (*p == X86_CALL) { p = insns = p + 5 + mem_loads32(p + 1); goto _1; }
		NEXT_INSN(p, "call to CreateEnvironment");
	}
	return 0;
_1:	for (const uchar *p = insns; p - insns < 32;) {
		// tail call to the constructor
		if (*p == X86_JMPIW) { insns = p + 5 + mem_loads32(p + 1); goto _2; }
		NEXT_INSN(p, "call to CPhysicsEnvironment constructor");
	}
	return 0;
_2:	for (const uchar *p = insns; p - insns < 16;) {
		// the vtable is loaded pretty early on:
		// mov dword ptr [reg], <vtable address>
		if (*p == X86_MOVMIW && (p[1] & 0xF8) == 0) return mem_loadptr(p + 2);
		NEXT_INSN(p, "CPhysicsEnvironment vtable");
	}
	return 0;
}

static void **find_physobj_vtable(void *CreatePolyObject) {
	const uchar *insns = (uchar *)CreatePolyObject;
	for (const uchar *p = insns; p - insns < 64;) {
		// first thing in the method is a call (after pushing a million params)
		if (*p == X86_CALL) {
			insns = p + 5 + mem_loads32(p + 1);
			goto _1;
		}
		NEXT_INSN(p, "call to CreatePhysicsObject");
	}
	return 0;
_1:	for (const uchar *p = insns; p - insns < 768;) {
		// there's a call to "new CPhysicsObject" somewhere down the line.
		// the (always inlined) constructor calls memset on the obj to init it.
		// the obj's vtable being loaded in is interleaved with pushing args
		// for memset and the order for all the instructions varies between
		// versions. the consistent bit is that `push 72` always happens shortly
		// before the vtable is loaded.
		if (*p == X86_PUSHI8 && p[1] == 72) { insns = p + 2; goto _2; }
		NEXT_INSN(p, "push before CPhysicsObject vtable load");
	}
	return 0;
_2:	for (const uchar *p = insns; p - insns < 16;) {
		// mov dword ptr [reg], <vtable address>
		if (*p == X86_MOVMIW && (p[1] & 0xF8) == 0) return mem_loadptr(p + 2);
		NEXT_INSN(p, "CPhysicsObject vtable");
	}
	return 0;
}

static bool find_isg_flag(void *RecheckCollisionFilter) {
	const uchar *insns = (uchar *)RecheckCollisionFilter, *p = insns;
	while (p - insns < 32) {
		// besides some flag handling, the only thing this function does is
		// call m_pObject->recheck_collision_filter()
		if (*p == X86_CALL) {
			p = p + 5 + mem_loads32(p + 1);
			goto _1;
		}
		NEXT_INSN(p, "call to RecheckCollisionFilter");
	}
	return false;
_1: for (insns = p; p - insns < 32;) {
		// recheck_collision_filter pretty much just calls a function
		if (*p == X86_CALL) {
			p = p + 5 + mem_loads32(p + 1);
			goto _2;
		}
		NEXT_INSN(p, "call to recheck_ov_element");
	}
	return false;
_2:	for (insns = p; p - insns < 0x300;) {
		// mov byte ptr [g_fDeferDeleteMindist]
		if (*p == X86_MOVMI8 && p[1] == X86_MODRM(0, 0, 5) && p[6] == 1) {
			isg_flag = mem_loadptr(p + 2);
			return true;
		}
		NEXT_INSN(p, "g_fDeferDeleteMindist");
	}
	return false;
}

INIT {
	disconnect_cb = con_getcmdcbv2(con_findcmd("disconnect"));
	if_cold(!disconnect_cb) return FEAT_INCOMPAT;
	void *phys = factory_engine("VPhysics031", 0);
	if_cold (phys == 0) {
		errmsg_errorx("couldn't get IPhysics interface");
		return FEAT_INCOMPAT;
	}
	void **vtable = mem_loadptr(phys);
	vtable = find_physenv_vtable(vtable[vtidx_CreateEnvironment]);
	if_cold (!vtable) {
		errmsg_errorx("couldn't find CPhysicsEnvironment vtable");
		return FEAT_INCOMPAT;
	}
	vtable = find_physobj_vtable(vtable[vtidx_CreatePolyObject]);
	if_cold (!vtable) {
		errmsg_errorx("couldn't find CPhysicsObject vtable");
		return FEAT_INCOMPAT;
	}
	if_cold (!find_isg_flag(vtable[vtidx_RecheckCollisionFilter])) {
		errmsg_errorx("couldn't find ISG flag");
		return FEAT_INCOMPAT;
	}
	return FEAT_OK;
}
