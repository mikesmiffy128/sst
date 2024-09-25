/*
 * Copyright © 2023 Willian Henrique <wsimanbrazil@yahoo.com.br>
 * Copyright © 2024 Michael Smith <mikesmiffy128@gmail.com>
 * Copyright © 2024 Hayden K <imaciidz@gmail.com>
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

#include "abi.h"
#include "accessor.h"
#include "con_.h"
#include "engineapi.h"
#include "ent.h"
#include "errmsg.h"
#include "event.h"
#include "fastfwd.h"
#include "feature.h"
#include "gamedata.h"
#include "gametype.h"
#include "gameserver.h"
#include "hook.h"
#include "intdefs.h"
#include "langext.h"
#include "l4dmm.h"
#include "mem.h"
#include "sst.h"
#include "vcall.h"
#include "x86.h"
#include "x86util.h"

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

FEATURE("Left 4 Dead quick resetting")
REQUIRE(ent)
REQUIRE(fastfwd)
REQUIRE(gameserver)
REQUIRE(l4dmm)
REQUIRE_GLOBAL(srvdll)
REQUIRE_GAMEDATA(vtidx_GameFrame) // note: for L4D1 only, always defined anyway
REQUIRE_GAMEDATA(vtidx_GameShutdown)
REQUIRE_GAMEDATA(vtidx_OnGameplayStart)

static void **votecontroller;
static int off_callerrecords = -1;
static int off_voteissues;

static void *director; // "TheDirector" server global

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

struct CVoteIssue;
DECL_VFUNC(const char *, SetIssueDetails, 1 + NVDTOR, const char *)
DECL_VFUNC(const char *, GetDisplayString, 8 + NVDTOR)
DECL_VFUNC(const char *, ExecuteCommand, 9 + NVDTOR)

DEF_PTR_ACCESSOR(struct CUtlVector, voteissues)
DEF_PTR_ACCESSOR(struct CUtlVector, callerrecords)

static struct CVoteIssue *getissue(const char *textkey) {
	struct CVoteIssue **issues = getptr_voteissues(*votecontroller)->m.mem;
	for (int i = 0; /*i < issuevec->sz*/; ++i) { // key MUST be valid!
		if (!strcmp(GetDisplayString(issues[i]), textkey)) return issues[i];
	}
}

static inline void reset(void) {
	// reset the vote cooldowns if possible (will skip L4D1). only necessary on
	// versions >2045 and on map 1, but it's easiest to do unconditionally.
	// This is equivalent to CUtlVector::RemoveAll() as there's no
	// destructors to call. The result as is if nobody had ever voted.
	// the way this is written will *hopefully* produce a nice neat lea+cmovne.
	if_random (off_callerrecords != -1) {
		getptr_callerrecords(*votecontroller)->sz = 0;
	}
	ExecuteCommand(getissue("#L4D_vote_restart_game"));
}

static inline void change(const char *missionid) {
	struct CVoteIssue *issue = getissue("#L4D_vote_mission_change");
	SetIssueDetails(issue, missionid); // will just nop if invalid
	ExecuteCommand(issue);
}

// Encoding: skip segment[, another[, another...]]. The last one is negative.
// Cutscenes of same length can share an entry to save a few bytes :)
static const schar ffsegs[] = {
	- 9, // No Mercy; Ravenholm (short cutscene); Dam It Complete; Day Break;
		 // Hard Rain: Downpour; Dead Before Dawn Too
	  4, // Swamp Fever
	  3, // - seen first propane
	-12, // - second propane. Also: Death Toll; Dead Air (L4D1); Dark Carnival;
		 // Cold Front; Riptide
	-15, // Blood Harvest (L4D1); The Sacrifice (L4D1)
	- 8, // Crash Course; Hard Rain; Left 4 Mario; Alley War
	-13, // Dead Center; The Parish; Dead Air (L4D2); Dark Blood 2; Grey Scale;
		 //	Tour of Terror; Carried Off; 2evileyes; Urban Flight; Detour Ahead;
		 // Energy Crisis; Deadly Dispatch; Blood Proof; Centro
	-10, // The Passing; Ceda Fever
	 11, // The Sacrifice (L4D2)
	- 4, // - view of biles
	-16, // Blood Harvest (L4D2), The Last Stand
	-18, // Cold Stream, Warcelona
	- 7, // One 4 Nine
	-14, // Roadkill; Haunted Forest; Suicide Blitz 2
	-19, // Journey to Splash Mountain
	-23, // Dark Woods (Extended)
	-25, // Dark Carnival Remix
	-28, // Diescraper Redux
	-31, // Chernobyl: Chapter One, The Curse of Lazar Castle
	-33, // RedemptionII
	-36, // Deathcraft II
	- 2, // Outrun
	-11, // Arena of the Dead
};
#define FFIDX_NOMERCY 0
#define FFIDX_RAVENHOLM 0
#define FFIDX_DAMIT 0
#define FFIDX_DAYBREAK 0
#define FFIDX_HARDRAINREMIX 0
#define FFIDX_DEADBEFOREDAWNTOO 0
#define FFIDX_SWAMP 1
#define FFIDX_DEATHTOLL 3
#define FFIDX_DEADAIR_L4D1 3
#define FFIDX_CARNIVAL 3
#define FFIDX_COLDFRONT 3
#define FFIDX_RIPTIDE 3
#define FFIDX_HARVEST_L4D1 4
#define FFIDX_SACRIFICE_L4D1 4
#define FFIDX_CRASHCOURSE 5
#define FFIDX_HARDRAIN 5
#define FFIDX_MARIO 5
#define FFIDX_ALLEYWAR 5
#define FFIDX_CENTER 6
#define FFIDX_PARISH 6
#define FFIDX_DEADAIR_L4D2 6
#define FFIDX_DARKBLOOD2 6
#define FFIDX_GREYSCALE 6
#define FFIDX_TOUROFTERROR 6
#define FFIDX_CARRIEDOFF 6
#define FFIDX_2EVILEYES 6
#define FFIDX_URBANFLIGHT 6
#define FFIDX_CITY17 6
#define FFIDX_DETOURAHEAD 6
#define FFIDX_ENERGYCRISIS 6
#define FFIDX_DEADLYDISPATCH 6
#define FFIDX_BLOODPROOF 6
#define FFIDX_CENTRO 6
#define FFIDX_PASSING 7
#define FFIDX_CEDAFEVER 7
#define FFIDX_SACRIFICE_L4D2 8
#define FFIDX_HARVEST_L4D2 10
#define FFIDX_TLS 10
#define FFIDX_STREAM 11
#define FFIDX_WARCELONA 11
#define FFIDX_ONE4NINE 12
#define FFIDX_ROADKILL 13
#define FFIDX_HAUNTEDFOREST 13
#define FFIDX_SUICIDEBLITZ2 13
#define FFIDX_SPLASHMTN 14
#define FFIDX_BLOODTRACKS 14
#define FFIDX_DARKWOODS 15
#define FFIDX_CARNIVALREMIX 16
#define FFIDX_DIESCRAPER 17
#define FFIDX_CHERNOBYL 18
#define FFIDX_LAZARCASTLE 18
#define FFIDX_REDEMPTIONII 19
#define FFIDX_DEATHCRAFT 20
#define FFIDX_OUTRUN 21
#define FFIDX_ARENAOFTHEDEAD 22

static schar ffidx;
static short ffdelay = 0;
static float ffadj = 0;
static int nextmapnum = 0;

DEF_CVAR_MINMAX_UNREG(sst_l4d_quickreset_peektime,
		"Number of seconds to show each relevant item spot during fast-forward",
		1.5, 0, 3, CON_ARCHIVE | CON_HIDDEN)

DEF_CCMD_HERE_UNREG(sst_l4d_quickreset_continue,
		"Get to the end of the current cutscene without further slowdowns", 0) {
	if (!ffdelay) {
		con_warn("not currently fast-forwarding a cutscene\n");
		return;
	}
	float remainder = ffdelay / 30.0f; // XXX: fixed tickrate... fine for now.
	while (ffsegs[ffidx] > 0) remainder += ffsegs[ffidx++];
	remainder -= ffsegs[ffidx];
	ffdelay = 0;
	fastfwd_add(remainder, 30);
}

HANDLE_EVENT(Tick, bool simulating) {
	if (!nextmapnum && simulating && ffdelay && !--ffdelay) {
		schar seg = ffsegs[ffidx];
		float halfwin = con_getvarf(sst_l4d_quickreset_peektime) / 2.0f;
		float t;
		if (seg > 0) { // there's more after this one!
			// if first seg, just take half window. otherwise half + adj = full
			t = seg - ffadj - halfwin;
			ffadj = halfwin;
			++ffidx;
			ffdelay = seg * 30; // XXX: fixed tickrate as above... fine for now.
		}
		else { // last one
			t = -seg - ffadj;
			ffadj = 0; // don't adjust the *next* fastforward's first seg
		}
		fastfwd(t, 30);
	}
}

typedef void (*VCALLCONV OnGameplayStart_func)(void *this);
static OnGameplayStart_func orig_OnGameplayStart;
static void VCALLCONV hook_OnGameplayStart(void *this) {
	orig_OnGameplayStart(this);
	if (nextmapnum) {
		// if we changed map more than 1 time, cancel the reset. this'll happen
		// if someone prematurely disconnects and then starts a new session.
		if (nextmapnum != gameserver_spawncount()) ffdelay = 0;
		else reset(); // prevent bots walking around. note ffdelay is stil 45
	}
	nextmapnum = 0; // resume countdown if there is one! otherwise do nothing.
}
// Simply reuse the above for L4D1, since the calling ABI is the exact same!
#define UnfreezeTeam_func OnGameplayStart_func
#define UnfreezeTeam OnGameplayStart
#define orig_UnfreezeTeam orig_OnGameplayStart
#define hook_UnfreezeTeam hook_OnGameplayStart

static int getffidx(const char *campaign) {
	if (GAMETYPE_MATCHES(L4D1)) {
		if (!strcmp(campaign, "Hospital")) return FFIDX_NOMERCY;
		if (!strcmp(campaign, "SmallTown")) return FFIDX_DEATHTOLL;
		if (!strcmp(campaign, "Airport")) return FFIDX_DEADAIR_L4D1;
		if (!strcmp(campaign, "Farm")) return FFIDX_HARVEST_L4D1;
		if (!strcmp(campaign, "River")) return FFIDX_SACRIFICE_L4D1;
		if (!strcmp(campaign, "Garage")) return FFIDX_CRASHCOURSE;
	}
	else /* L4D2 */ {
		if (!strncmp(campaign, "L4D2C", 5)) {
			int ret;
			switch (campaign[5]) {
				case '1':
					switch (campaign[6]) {
						case '\0': return FFIDX_CENTER;
						case '0': ret = FFIDX_DEATHTOLL; break;
						case '1': ret = FFIDX_DEADAIR_L4D2; break;
						case '2': ret = FFIDX_HARVEST_L4D2; break;
						case '3': ret = FFIDX_STREAM; break;
						case '4': ret = FFIDX_TLS; break;
						default: return -1;
					}
					if (campaign[7]) return -1;
					return ret;
				case '2': ret = FFIDX_CARNIVAL; break;
				case '3': ret = FFIDX_SWAMP; break;
				case '4': ret = FFIDX_HARDRAIN; break;
				case '5': ret = FFIDX_PARISH; break;
				case '6': ret = FFIDX_PASSING; break;
				case '7': ret = FFIDX_SACRIFICE_L4D2; break;
				case '8': ret = FFIDX_NOMERCY; break;
				case '9': ret = FFIDX_CRASHCOURSE; break;
				default: return -1;
			}
			if (campaign[6]) return -1;
			return ret;
		}
		// slow but uncommon case: this giant list of custom campaigns, for fun.
		// TODO(opt): come up with something smart to do here on some rainy day?
		if (!strcmp(campaign, "darkblood2")) return FFIDX_DARKBLOOD2;
		if (!strcmp(campaign, "Greyscale")) return FFIDX_GREYSCALE;
		if (!strcmp(campaign, "yomimario2")) return FFIDX_MARIO;
		// TODO(compat): ravenholm cutscene length is actually random; this is
		// the short one only. we'll want to solve that some day, somehow...
		if (!strcmp(campaign, "ravenholmwar2")) return FFIDX_RAVENHOLM;
		if (!strcmp(campaign, "warcelona")) return FFIDX_WARCELONA;
		if (!strcmp(campaign, "tot")) return FFIDX_TOUROFTERROR;
		if (!strcmp(campaign, "damitcomplete")) return FFIDX_DAMIT;
		if (!strcmp(campaign, "CarriedOff")) return FFIDX_CARRIEDOFF;
		if (!strcmp(campaign, "JourneyToSplashMountain")) return FFIDX_SPLASHMTN;
		if (!strcmp(campaign, "rkls")) return FFIDX_ROADKILL;
		if (!strcmp(campaign, "CedaFever")) return FFIDX_CEDAFEVER;
		if (!strcmp(campaign, "coldfront")) return FFIDX_COLDFRONT;
		if (!strcmp(campaign, "DarkCarnivalRemix")) return FFIDX_CARNIVALREMIX;
		if (!strncmp(campaign, "DayBreak", 8)) {
			if (campaign[8] == '\0' || !strcmp(campaign + 8, "v3")) {
				return FFIDX_DAYBREAK;
			}
			return -1;
		}
		if (!strcmp(campaign, "Downpour")) return FFIDX_HARDRAINREMIX;
		if (!strcmp(campaign, "red")) return FFIDX_REDEMPTIONII;
		if (!strcmp(campaign, "Diescraper362")) return FFIDX_DIESCRAPER;
		if (!strcmp(campaign, "dbd2")) return FFIDX_DEADBEFOREDAWNTOO;
		if (!strcmp(campaign, "2evileyes")) return FFIDX_2EVILEYES;
		if (!strcmp(campaign, "Chernobyl")) return FFIDX_CHERNOBYL;
		if (!strcmp(campaign, "darkwood")) return FFIDX_DARKWOODS;
		if (!strcmp(campaign, "TheCurseofLazarCastle")) return FFIDX_LAZARCASTLE;
		if (!strcmp(campaign, "urbanflight")) return FFIDX_URBANFLIGHT;
		if (!strcmp(campaign, "BloodTracks")) return FFIDX_BLOODTRACKS;
		if (!strcmp(campaign, "City17l4d2")) return FFIDX_CITY17;
		if (!strcmp(campaign, "Deathcraft")) return FFIDX_DEATHCRAFT;
		if (!strcmp(campaign, "detourahead")) return FFIDX_DETOURAHEAD;
		if (!strcmp(campaign, "hauntedforest")) return FFIDX_HAUNTEDFOREST;
		if (!strcmp(campaign, "One4Nine")) return FFIDX_ONE4NINE;
		if (!strcmp(campaign, "suicideblitz2")) return FFIDX_SUICIDEBLITZ2;
		if (!strcmp(campaign, "energycrisis")) return FFIDX_ENERGYCRISIS;
		if (!strcmp(campaign, "DDCW")) return FFIDX_DEADLYDISPATCH;
		if (!strcmp(campaign, "Outrun")) return FFIDX_OUTRUN;
		if (!strcmp(campaign, "BloodProof")) return FFIDX_BLOODPROOF;
		if (!strcmp(campaign, "centro")) return FFIDX_CENTRO;
		if (!strcmp(campaign, "riptide")) return FFIDX_RIPTIDE;
		if (!strcmp(campaign, "jsarena2")) return FFIDX_ARENAOFTHEDEAD;
		if (!strcmp(campaign, "AlleyWar")) return FFIDX_ALLEYWAR;
	}
	return -1; // if unknown, just don't fast-forward, I guess.
}

DEF_CVAR_UNREG(sst_l4d_quickreset_fastfwd,
		"Fast-forward through cutscenes when quick-resetting", 1,
		CON_ARCHIVE | CON_HIDDEN)

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
	const char *campaign = l4dmm_curcampaign();
	if (cmd->argc == 2 && (!campaign || strcasecmp(campaign, cmd->argv[1]))) {
		change(cmd->argv[1]);
		campaign = cmd->argv[1];
		nextmapnum = gameserver_spawncount() + 1; // immediate next changelevel
	}
	else {
		reset();
		if (l4dmm_firstmap()) {
			fastfwd(0.8, 10); // same-map reset is delayed by about a second
			nextmapnum = 0; // reset this just in case it got stuck... somehow?
		}
		else {
			nextmapnum = gameserver_spawncount() + 1; // same as above
		}
	}
	if (campaign && con_getvari(sst_l4d_quickreset_fastfwd) &&
			(ffidx = getffidx(campaign)) != -1) {
		ffdelay = 45; // 1.5s
	}
}

PREINIT {
	if (!GAMETYPE_MATCHES(L4D)) return false;
	con_reg(sst_l4d_quickreset_fastfwd);
	con_reg(sst_l4d_quickreset_peektime);
	return true;
}

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
			insns = p + 5 + mem_loads32(p + 1);
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
			int off = mem_loads32(p + 2);
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
				mem_loads32(p + 6) == 0) {
			off_callerrecords = mem_loads32(p + 2) + 8;
			return true;
		}
		NEXT_INSN(p, "offset to vote caller record vector");
	}
#else
#warning TODO(linux): this too
#endif
	return false;
}

#ifdef _WIN32
static inline bool find_TheDirector(void *GameShutdown) {
	// in 2045, literally the first instruction of this function is loading
	// TheDirector into ECX. although, do the usual search in case moves a bit.
	const uchar *insns = (const uchar *)GameShutdown;
	for (const uchar *p = insns; p - insns < 24;) {
		if (p[0] == X86_MOVRMW && p[1] == X86_MODRM(0, 1, 5)) {
			void **indirect = mem_loadptr(p + 2);
			director = *indirect;
			return true;
		}
		NEXT_INSN(p, "load of TheDirector");
	}
	return false;
}
#endif

static inline bool find_UnfreezeTeam(void *GameFrame) { // note: L4D1 only
	// CServerGameDLL::GameFrame() loads TheDirector into ECX and then calls
	// Director::Update()
	const uchar *insns = (const uchar *)GameFrame, *p = insns;
	while (p - insns < 192) {
		if (p[0] == X86_MOVRMW && p[1] == X86_MODRM(0, 1, 5) &&
				mem_loadptr(mem_loadptr(p + 2)) == director &&
				p[6] == X86_CALL) {
			p += 11 + mem_loads32(p + 7);
			insns = p;
			goto ok;
		}
		NEXT_INSN(p, "Director::Update call");
	}
	return false;
ok: // Director::Update calls UnfreezeTeam after the first jmp instruction
	while (p - insns < 96) {
		// jz XXX; mov ecx, <reg>; call Director::UnfreezeTeam
		if (p[0] == X86_JZ && p[2] == X86_MOVRMW && (p[3] & 0xF8) == 0xC8 &&
				p[4] == X86_CALL) {
			p += 9 + mem_loads32(p + 5);
			orig_UnfreezeTeam = (UnfreezeTeam_func)p;
			return true;
		}
		NEXT_INSN(p, "Director::UnfreezeTeam call");
	}
	return false;
 }

INIT {
	struct con_cmd *cmd_listissues = con_findcmd("listissues");
	if_cold (!cmd_listissues) {
		errmsg_errorx("couldn't find \"listissues\" command");
		return false;
	}
	con_cmdcbv1 listissues_cb = con_getcmdcbv1(cmd_listissues);
	const uchar *nextinsns = find_votecontroller(listissues_cb);
	if_cold (!nextinsns) {
		errmsg_errorx("couldn't find vote controller variable");
		return false;
	}
	if_cold (!find_voteissues(nextinsns)) {
		errmsg_errorx("couldn't find vote issues list offset\n");
		return false;
	}
	void **vtable;
#ifdef _WIN32
	void *GameShutdown = (*(void ***)srvdll)[vtidx_GameShutdown];
	if_cold (!find_TheDirector(GameShutdown)) {
		errmsg_errorx("couldn't find TheDirector variable");
		return false;
	}
#else
#warning TODO(linux): should be able to just dlsym(server, "TheDirector")
	return false;
#endif
#ifdef _WIN32 // L4D1 has no Linux build, no need to check whether L4D2
	if (GAMETYPE_MATCHES(L4D2)) {
#endif
		vtable = mem_loadptr(director);
		if_cold (!os_mprot(vtable + vtidx_OnGameplayStart, sizeof(*vtable),
				PAGE_READWRITE)) {
			errmsg_errorsys("couldn't make virtual table writable");
			return false;
		}
		orig_OnGameplayStart = (OnGameplayStart_func)hook_vtable(vtable,
				vtidx_OnGameplayStart, (void *)&hook_OnGameplayStart);
#ifdef _WIN32 // L4D1 has no Linux build!
	}
	else /* L4D1 */ {
		void *GameFrame = (*(void ***)srvdll)[vtidx_GameFrame];
		if_cold (!find_UnfreezeTeam(GameFrame)) {
			errmsg_errorx("couldn't find UnfreezeTeam function");
			return false;
		}
		orig_UnfreezeTeam = (UnfreezeTeam_func)hook_inline(
				(void *)orig_UnfreezeTeam, (void *)&hook_UnfreezeTeam);
	}
#endif
	// Only try cooldown stuff for L4D2, since L4D1 always had unlimited votes.
	if (GAMETYPE_MATCHES(L4D2)) {
		// g_voteController is invalid if not running a server so get the
		// vtable by inspecting the ent factory code instead
		const struct CEntityFactory *factory = ent_getfactory("vote_controller");
		if_cold (!factory) {
			errmsg_errorx("couldn't find vote controller entity factory");
			goto nocd;
		}
		vtable = ent_findvtable(factory, "CVoteController");
		if_cold (!vtable) {
			errmsg_errorx("couldn't find CVoteController vtable");
			goto nocd;
		}
		if_cold (!find_votecallers(vtable[vtidx_Spawn])) {
			errmsg_errorx("couldn't find vote callers list offset");
nocd:		errmsg_note("resetting a first map will not clear vote cooldowns");
		}
	}
	con_reg(sst_l4d_quickreset);
	con_reg(sst_l4d_quickreset_continue);
	sst_l4d_quickreset_fastfwd->base.flags &= ~CON_HIDDEN;
	sst_l4d_quickreset_peektime->base.flags &= ~CON_HIDDEN;
	return true;
}

END {
	if (GAMETYPE_MATCHES(L4D2)) {
		unhook_vtable(mem_loadptr(director), vtidx_OnGameplayStart,
				(void *)orig_OnGameplayStart);
	}
	else {
		unhook_inline((void *)orig_UnfreezeTeam);
	}
}

// vi: sw=4 ts=4 noet tw=80 cc=80
