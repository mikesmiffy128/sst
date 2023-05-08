/*
 * Copyright © 2023 Willian Henrique <wsimanbrazil@yahoo.com.br>
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

#include "con_.h"
#include "engineapi.h"
#include "ent.h"
#include "errmsg.h"
#include "feature.h"
#include "gamedata.h"
#include "gametype.h"
#include "intdefs.h"
#include "mem.h"
#include "x86.h"
#include "x86util.h"

FEATURE("Left 4 Dead 2 vote cooldown resetting")
REQUIRE_GAMEDATA(vtidx_Spawn)

static void **votecontroller = 0;
static int off_callerrecords = 0;

// Note: the vote callers vector contains these as elements. We don't currently
// do anything with the structure, but keeping it here for reference.
/*struct CallerRecord {
	u32 steamid_trunc;
	float last_time;
	int votes_passed;
	int votes_failed;
	int last_issueidx;
	bool last_passed;
};*/

DEF_CCMD_HERE_UNREG(sst_l4d2_vote_cooldown_reset,
		"Reset vote cooldown for all players", CON_CHEAT) {
	if (!*votecontroller) {
		con_warn("vote controller not initialised\n");
		return;
	}
	// Basically equivalent to CUtlVector::RemoveAll. The elements don't need
	// to be destructed. This state is equivalent to when no one has voted yet
	struct CUtlVector *recordvector = mem_offset(*votecontroller,
			off_callerrecords);
	recordvector->sz = 0;
}

PREINIT {
	// note: L4D1 has sv_vote_creation_timer but it doesn't actually do anything
	return GAMETYPE_MATCHES(L4D2) && !!con_findvar("sv_vote_creation_timer");
}

static inline bool find_votecontroller(con_cmdcbv1 listissues_cb) {
	const uchar *insns = (const uchar *)listissues_cb;
#ifdef _WIN32
	// The "listissues" command calls CVoteController::ListIssues, loading
	// g_voteController into ECX
	for (const uchar *p = insns; p - insns < 32;) {
		if (p[0] == X86_MOVRMW && p[1] == X86_MODRM(0, 1, 5)) {
			votecontroller = mem_loadptr(p + 2);
			return true;
		}
		NEXT_INSN(p, "g_voteController variable");
	}
#else
#warning TODO(linux): this will be different
#endif
	return false;
}

// This finds the caller record vector using a pointer to the
// CVoteController::Spawn function
static inline bool find_votecallers(void *votectrlspawn) {
	const uchar *insns = (const uchar *)votectrlspawn;
	for (const uchar *p = insns; p - insns < 64;) {
		// Unsure what the member on this offset actually is (the game seems to
		// want it to be set to 0 to allow votes to happen), but the vector we
		// want seems to consistently be 8 bytes after whatever this is
		// "mov dword ptr [<reg> + off], 0", mod == 0b11
		if (p[0] == X86_MOVMIW && (p[1] & 0xC0) == 0x80 &&
				mem_load32(p + 6) == 0) {
			off_callerrecords = mem_load32(p + 2) + 8;
			return true;
		}
		NEXT_INSN(p, "vote caller record vector");
	}
	return false;
}

INIT {
	struct con_cmd *cmd_listissues = con_findcmd("listissues");
	if (!cmd_listissues) {
		errmsg_errorx("couldn't find \"listissues\" command");
		return false;
	}
	con_cmdcbv1 listissues_cb = con_getcmdcbv1(cmd_listissues);
	if (!find_votecontroller(listissues_cb)) {
		errmsg_errorx("couldn't find vote controller instance");
		return false;
	}

	// g_voteController may have not been initialized yet so we get the vtable
	// from the ent factory
	const struct CEntityFactory *factory = ent_getfactory("vote_controller");
	if (!factory) {
		errmsg_errorx("couldn't find vote controller entity factory");
		return false;
	}
	void **vtable = ent_findvtable(factory, "CVoteController");
	if (!vtable) {
		errmsg_errorx("couldn't find CVoteController vtable");
		return false;
	}
	if (!find_votecallers(vtable[vtidx_Spawn])) {
		errmsg_errorx("couldn't find vote callers vector offset");
		return false;
	}

	con_reg(sst_l4d2_vote_cooldown_reset);
	return true;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
