/*
 * Copyright © 2023 Willian Henrique <wsimanbrazil@yahoo.com.br>
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
#include "ent.h"
#include "errmsg.h"
#include "feature.h"
#include "gamedata.h"
#include "gametype.h"
#include "intdefs.h"
#include "l4dmm.h"
#include "mem.h"
#include "vcall.h"
#include "x86.h"
#include "x86util.h"

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

FEATURE("Left 4 Dead quick resetting")
REQUIRE(ent)
REQUIRE(l4dmm)

static void **votecontroller;
static int off_callerrecords = -1;
static int off_voteissues;

// Note: the vote callers vector contains these as elements. We don't currently
// do anything with the structure, but we're keeping it here for reference.
/*struct CallerRecord {
	u32 steamid_trunc;
	float last_time;
	int votes_passed;
	int votes_failed;
	int last_issueidx;
	bool last_passed;
};*/

// XXX: duping this again here... what makes sense to tidy this up?
#ifdef _WIN32
#define NVDTOR 1
#else
#define NVDTOR 2
#endif

struct CVoteIssue;
DECL_VFUNC(const char *, SetIssueDetails, 1 + NVDTOR, const char *)
DECL_VFUNC(const char *, GetDisplayString, 8 + NVDTOR)
DECL_VFUNC(const char *, ExecuteCommand, 9 + NVDTOR)

static struct CVoteIssue *getissue(const char *textkey) {
	struct CUtlVector *issuevec = mem_offset(*votecontroller, off_voteissues);
	struct CVoteIssue **issues = issuevec->m.mem;
	for (int i = 0; /*i < issuevec->sz*/; ++i) { // key MUST be valid!
		if (!strcmp(GetDisplayString(issues[i]), textkey)) return issues[i];
	}
}

static void reset(void) {
	// reset the vote cooldowns if possible (will skip L4D1). only necessary on
	// versions >2045 and on map 1, but it's easiest to do unconditionally
	if (off_callerrecords != -1) {
		// This is equivalent to CUtlVector::RemoveAll() as there's no
		// destructors to call. The result as is if nobody had ever voted.
		struct CUtlVector *recordvector = mem_offset(*votecontroller,
				off_callerrecords);
		recordvector->sz = 0;
	}
	struct CVoteIssue *issue = getissue("#L4D_vote_restart_game");
	ExecuteCommand(issue);
}

static void change(const char *missionid) {
	struct CVoteIssue *issue = getissue("#L4D_vote_mission_change");
	SetIssueDetails(issue, missionid); // will just nop if invalid
	ExecuteCommand(issue);
}

DEF_CCMD_HERE_UNREG(sst_l4d_quickreset,
		"Reset (or switch) campaign and clear all vote cooldowns", 0) {
	if (cmd->argc > 2) {
		con_warn("usage: sst_l4d_quickreset [campaignid]\n");
		return;
	}
	if (!*votecontroller) {
		con_warn("not hosting a server\n");
		return;
	}
	if (cmd->argc == 2) {
		const char *cur = l4dmm_curcampaign();
		if (!cur || strcasecmp(cur, cmd->argv[1])) {
			change(cmd->argv[1]);
			return;
		}
	}
	reset();
}

PREINIT { return GAMETYPE_MATCHES(L4D); }

// Note: this returns a pointer to subsequent bytes for find_voteissues() below
static inline const uchar *find_votecontroller(con_cmdcbv1 listissues_cb) {
	const uchar *insns = (const uchar *)listissues_cb;
#ifdef _WIN32
	// The "listissues" command calls CVoteController::ListIssues, loading
	// g_voteController into ECX
	for (const uchar *p = insns; p - insns < 32;) {
		if (p[0] == X86_MOVRMW && p[1] == X86_MODRM(0, 1, 5)) {
			votecontroller = mem_loadptr(p + 2);
			return p;
		}
		NEXT_INSN(p, "g_voteController variable");
	}
#else
#warning TODO(linux): this will be different
#endif
	return 0;
}

static inline bool find_voteissues(const uchar *insns) {
#ifdef _WIN32
	for (const uchar *p = insns; p - insns < 16;) {
		// Look for the last call before the ret - that has to be ListIssues()
		if (p[0] == X86_CALL && p[5] == X86_RET) {
			insns = p + 5 + mem_loadoffset(p + 1);
			goto ok;
		}
		NEXT_INSN(p, "ListIssues call");
	}
	return false;
ok:	for (const uchar *p = insns; p - insns < 96;) {
		// The loop in ListIssues() calls a member function on each CVoteIssue.
		// Each pointer is loaded from a CUtlVector at an offset from `this`, so
		// we can find that offset from the mov into ECX.
		if (p[0] == X86_MOVRMW && (p[1] & 0xF8) == 0x88) {
			int off = mem_loadoffset(p + 2);
			if (off > 800) { // sanity check: offset is always fairly high
				off_voteissues = off;
				return true;
			}
		}
		// Complication: at least 2045 has a short jmp over some garbage bytes.
		// Follow that jmp since there's nothing interesting before the target.
		if (p[0] == X86_JMPI8) {
			p += 2 + ((s8 *)p)[1];
			continue;
		}
		NEXT_INSN(p, "offset to vote issue vector");
	}
#else
#warning TODO(linux): and also this
#endif
	return false;
}

static inline bool find_votecallers(void *votectrlspawn) {
#ifdef _WIN32
	const uchar *insns = (const uchar *)votectrlspawn;
	for (const uchar *p = insns; p - insns < 64;) {
		// Unsure what this offset points at (it seems to have to be 0 for votes
		// to happen), but the vector of interest always comes 8 bytes later.
		// "mov dword ptr [<reg> + off], 0", mod == 0b11
		if (p[0] == X86_MOVMIW && (p[1] & 0xC0) == 0x80 &&
				mem_load32(p + 6) == 0) {
			off_callerrecords = mem_load32(p + 2) + 8;
			return true;
		}
		NEXT_INSN(p, "offset to vote caller record vector");
	}
#else
#warning TODO(linux): this too
#endif
	return false;
}

INIT {
	struct con_cmd *cmd_listissues = con_findcmd("listissues");
	if (!cmd_listissues) {
		errmsg_errorx("couldn't find \"listissues\" command");
		return false;
	}
	con_cmdcbv1 listissues_cb = con_getcmdcbv1(cmd_listissues);
	const uchar *nextinsns = find_votecontroller(listissues_cb);
	if (!nextinsns) {
		errmsg_errorx("couldn't find vote controller variable");
		return false;
	}
	if (!find_voteissues(nextinsns)) {
		errmsg_errorx("couldn't find vote issues list offset\n");
		return false;
	}
	// Only try cooldown stuff for L4D2, since L4D1 always had unlimited votes.
	// NOTE: assuming L4D2 always has Spawn in gamedata (why wouldn't it?)
	if (GAMETYPE_MATCHES(L4D2)) {
		// g_voteController is invalid if not running a server so get the
		// vtable by inspecting the ent factory code instead
		const struct CEntityFactory *factory = ent_getfactory("vote_controller");
		if (!factory) {
			errmsg_errorx("couldn't find vote controller entity factory");
			goto nocd;
		}
		void **vtable = ent_findvtable(factory, "CVoteController");
		if (!vtable) {
			errmsg_errorx("couldn't find CVoteController vtable");
			goto nocd;
		}
		if (!find_votecallers(vtable[vtidx_Spawn])) {
			errmsg_errorx("couldn't find vote callers list offset");
nocd:		errmsg_note("resetting a first map will not clear vote cooldowns");
		}
	}
	con_reg(sst_l4d_quickreset);
	return true;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
