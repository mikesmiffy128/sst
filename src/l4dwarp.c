/*
 * Copyright © Michael Smith <mikesmiffy128@gmail.com>
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

#define _USE_MATH_DEFINES // ... windows.
#include <math.h>

#include "accessor.h"
#include "chunklets/x86.h"
#include "clientcon.h"
#include "con_.h"
#include "engineapi.h"
#include "errmsg.h"
#include "ent.h"
#include "feature.h"
#include "gamedata.h"
#include "intdefs.h"
#include "langext.h"
#include "mem.h"
#include "trace.h"
#include "vcall.h"
#include "x86util.h"

FEATURE("Left 4 Dead warp testing")
GAMESPECIFIC(L4D)
REQUIRE(clientcon)
REQUIRE(ent)
REQUIRE(trace)
REQUIRE_GAMEDATA(off_entpos)
REQUIRE_GAMEDATA(off_eyeang)
REQUIRE_GAMEDATA(off_teamnum)
REQUIRE_GAMEDATA(vtidx_AddBoxOverlay2)
REQUIRE_GAMEDATA(vtidx_AddLineOverlay)
REQUIRE_GAMEDATA(vtidx_Teleport)

// XXX: could make these calls type safe in future? just tricky because the
// entity hierarchy is kind of crazy so it's not clear which type name to pick
DECL_VFUNC_DYN(void, void, Teleport, const struct vec3f */*pos*/,
		const struct vec3f */*ang*/, const struct vec3f */*vel*/)
DECL_VFUNC(void, const struct vec3f *, OBBMaxs, 2)

// IMPORTANT: padsz parameter is missing in L4D1, but since it's cdecl, we can
// still call it just the same (we always pass 0, so there's no difference).
typedef bool (*EntityPlacementTest_func)(void *ent, const struct vec3f *origin,
		struct vec3f *out, bool drop, uint mask, void *filt, float padsz);
static EntityPlacementTest_func EntityPlacementTest;

// Technically the warp uses a CTraceFilterSkipTeam, not a CTraceFilterSimple.
// That does, however, inherit from the simple filter and run some minor checks
// on top of it. I couldn't find a case where these checks actually mattered
// and, if needed, they could be easily reimplemented using the extra hit check
// (instead of hunting for the CTraceFilterSkipTeam vtable).
static struct CTraceFilterSimple {
	void **vtable;
	void *pass_ent;
	int collision_group;
	void * /* ShouldHitFunc_t */ extrahitcheck_func;
	//int teamnum; // player's team number. member of CTraceFilterSkipTeam
} filter;

typedef void (*VCALLCONV CTraceFilterSimple_ctor)(
		struct CTraceFilterSimple *this, void *pass_ent, int collisiongroup,
		void *extrahitcheck_func);

// Trace mask for non-bot survivors. Constant in all L4D versions
#define PLAYERMASK 0x0201420B

// debug overlay stuff, only used by sst_l4d_previewwarp
static struct IVDebugOverlay *dbgoverlay;
DECL_VFUNC_DYN(struct IVDebugOverlay, void, AddLineOverlay,
		const struct vec3f *, const struct vec3f *, int, int, int, bool, float)
DECL_VFUNC_DYN(struct IVDebugOverlay, void, AddBoxOverlay2,
		const struct vec3f *, const struct vec3f *, const struct vec3f *,
		const struct vec3f *, const struct rgba *, const struct rgba *, float)

// XXX: more type safety stuff here also
DEF_ACCESSORS(void, struct vec3f, entpos)
DEF_ACCESSORS(void, struct vec3f, eyeang)
DEF_ACCESSORS(void, uint, teamnum)
DEF_PTR_ACCESSOR(void, void, collision)

static struct vec3f warptarget(void *ent) {
	struct vec3f org = get_entpos(ent), ang = get_eyeang(ent);
	// L4D idle warps go up to 10 units behind yaw, lessening based on pitch.
	float pitch = ang.x * M_PI / 180, yaw = ang.y * M_PI / 180;
	float shift = -10 * cosf(pitch);
	return (struct vec3f){
		org.x + shift * cosf(yaw),
		org.y + shift * sinf(yaw),
		org.z
	};
}

DEF_FEAT_CCMD_HERE(sst_l4d_testwarp, "Simulate a bot warping to you "
		"(specify \"staystuck\" to skip take-control simulation)",
		CON_SERVERSIDE | CON_CHEAT) {
	bool staystuck = false;
	// TODO(autocomplete): suggest this argument
	if (argc == 2 && !strcmp(argv[1], "staystuck")) {
		staystuck = true;
	}
	else if (argc != 1) {
		clientcon_reply("usage: sst_l4d_testwarp [staystuck]\n");
		return;
	}
	struct edict *ed = ent_getedict(con_cmdclient + 1);
	if_cold (!ed || !ed->ent_unknown) {
		errmsg_errorx("couldn't access player entity");
		return;
	}
	void *e = ed->ent_unknown;
	if_cold (get_teamnum(e) != 2) {
		clientcon_msg(ed, "error: must be in the Survivor team\n");
		return;
	}
	filter.pass_ent = e;
	struct vec3f stuckpos = warptarget(e);
	struct vec3f finalpos;
	if (staystuck || !EntityPlacementTest(e, &stuckpos, &finalpos, false,
			PLAYERMASK, &filter, 0.0)) {
		finalpos = stuckpos;
	}
	Teleport(e, &finalpos, 0, &(struct vec3f){0, 0, 0});
}

static const struct rgba
	red_edge = {200, 0, 0, 100}, red_face = {220, 0, 0, 10},
	yellow_edge = {240, 200, 20, 100},
	green_edge = {20, 210, 50, 100}, green_face = {49, 220, 30, 10},
	clear_face = {0, 0, 0, 0},
	orange_line = {255, 100, 0, 255}, cyan_line = {0, 255, 255, 255};

static const struct vec3f zerovec = {0};

static bool draw_testpos(struct vec3f start, struct vec3f testpos,
		struct vec3f mins, struct vec3f maxs, bool needline) {
	struct CGameTrace t = trace_hull(testpos, testpos, mins, maxs, PLAYERMASK,
			&filter);
	if (t.base.frac != 1.0f || t.base.allsolid || t.base.startsolid) {
		AddBoxOverlay2(dbgoverlay, &testpos, &mins, &maxs, &zerovec,
				&clear_face, &red_edge, 1000.0);
		return needline;
	}
	AddBoxOverlay2(dbgoverlay, &testpos, &mins, &maxs, &zerovec,
			&clear_face, &yellow_edge, 1000.0);
	if (needline) {
		t = trace_line(start, testpos, PLAYERMASK, &filter);
		AddLineOverlay(dbgoverlay, &start, &t.base.endpos,
				orange_line.r, orange_line.g, orange_line.b, true, 1000.0);
		// current knowledge indicates that this should never happen, but it's
		// good to issue a warning if the code ever happens to be wrong
		if_cold (t.base.frac == 1.0 && !t.base.allsolid && !t.base.startsolid) {
			// XXX: should this be sent to client console? more effort...
			errmsg_warnx("false positive test position %.3f %.3f %.3f",
					testpos.x, testpos.y, testpos.z);
			return true;
		}
	}
	return false;
}

// note: UNREG because testwarp can still work without this
DEF_CCMD_HERE_UNREG(sst_l4d_previewwarp, "Visualise bot warp unstuck logic "
		"(use clear_debug_overlays to remove)", CON_SERVERSIDE | CON_CHEAT) {
	struct edict *ed = ent_getedict(con_cmdclient + 1);
	if_cold (!ed || !ed->ent_unknown) {
		errmsg_errorx("couldn't access player entity");
		return;
	}
	if (con_cmdclient != 0) {
		clientcon_msg(ed, "error: only the server host can see visualisations\n");
		return;
	}
	void *e = ed->ent_unknown;
	if_cold (get_teamnum(e) != 2) {
		clientcon_msg(ed, "error: must be in the Survivor team\n");
		return;
	}
	filter.pass_ent = e;
	struct vec3f stuckpos = warptarget(e);
	struct vec3f finalpos;
	// we use the real EntityPlacementTest and then work backwards to figure out
	// what to draw. that way there's very little room for missed edge cases
	bool success = EntityPlacementTest(e, &stuckpos, &finalpos, false,
			PLAYERMASK, &filter, 0.0);
	struct vec3f mins = {-16.0f, -16.0f, 0.0f};
	struct vec3f maxs = *OBBMaxs(getptr_collision(ed->ent_unknown));
	struct vec3f step = {maxs.x - mins.x, maxs.y - mins.y, maxs.z - mins.z};
	struct failranges { struct { int neg, pos; } x, y, z; } ranges;
	if (success) {
		AddBoxOverlay2(dbgoverlay, &finalpos, &mins, &maxs, &zerovec,
				&green_face, &green_edge, 1000.0);
		if (finalpos.x != stuckpos.x) {
			float iters = roundf((finalpos.x - stuckpos.x) / step.x);
			int isneg = iters < 0;
			iters = fabs(iters);
			ranges = (struct failranges){
				{-iters + isneg, iters - 1},
				{-iters + 1, iters - 1},
				{-iters + 1, iters - 1}
			};
		}
		else if (finalpos.y != stuckpos.y) {
			float iters = roundf((finalpos.y - stuckpos.y) / step.y);
			int isneg = iters < 0;
			iters = fabs(iters);
			ranges = (struct failranges){
				{-iters, iters},
				{-iters + isneg, iters - 1},
				{-iters + 1, iters - 1}
			};
		}
		else if (finalpos.z != stuckpos.z) {
			float iters = roundf((finalpos.z - stuckpos.z) / step.z);
			int isneg = iters > 0;
			iters = fabs(iters);
			ranges = (struct failranges){
				{-iters, iters},
				{-iters, iters},
				{-iters + isneg, iters - 1}
			};
		}
		else {
			// we were never actually stuck - no need to draw all the boxes
			return;
		}
		AddLineOverlay(dbgoverlay, &stuckpos, &finalpos,
				cyan_line.r, cyan_line.g, cyan_line.b, true, 1000.0);
	}
	else {
		finalpos = stuckpos;
		// searched the entire 15 iteration range, found nowhere to go
		ranges = (struct failranges){{-15, 15}, {-15, 15}, {-15, 15}};
	}
	AddBoxOverlay2(dbgoverlay, &stuckpos, &mins, &maxs, &zerovec,
			&red_face, &red_edge, 1000.0);
	bool needline = true;
	for (int i = ranges.x.neg; i <= ranges.x.pos; ++i) {
		if (i == 0) { needline = true; continue; }
		struct vec3f pos = {stuckpos.x + step.x * i, stuckpos.y, stuckpos.z};
		needline = draw_testpos(stuckpos, pos, mins, maxs, needline);
	}
	needline = true;
	for (int i = ranges.y.neg; i <= ranges.y.pos; ++i) {
		if (i == 0) { needline = true; continue; }
		struct vec3f pos = {stuckpos.x, stuckpos.y + step.y * i, stuckpos.z};
		needline = draw_testpos(stuckpos, pos, mins, maxs, needline);
	}
	needline = true;
	for (int i = ranges.z.neg; i <= ranges.z.pos; ++i) {
		if (i == 0) { needline = true; continue; }
		struct vec3f pos = {stuckpos.x, stuckpos.y, stuckpos.z + step.z * i};
		needline = draw_testpos(stuckpos, pos, mins, maxs, needline);
	}
}

static bool find_EntityPlacementTest(const uchar *insns) {
#ifdef _WIN32
	for (const uchar *p = insns; p - insns < 0x300;) {
		// Find 0, 0x200400B and 1 being pushed to the stack
		if (p[0] == X86_PUSHI8 && p[1] == 0 &&
				p[2] == X86_PUSHIW && mem_loadu32(p + 3) == 0x200400B &&
				p[7] == X86_PUSHI8 && p[8] == 1) {
			p += 9;
			// Next call is the one we are looking for
			while (p - insns < 0x300) {
				if (p[0] == X86_CALL) {
					EntityPlacementTest = (EntityPlacementTest_func)(
							p + 5 + mem_loads32(p + 1));
					return true;
				}
				NEXT_INSN(p, "EntityPlacementTest function");
			}
			return false;
		}
		NEXT_INSN(p, "EntityPlacementTest function");
	}
#else
#warning TODO(linux): usual asm search stuff
#endif
	return false;
}

static bool init_filter() {
	const uchar *insns = (const uchar *)EntityPlacementTest;
	for (const uchar *p = insns; p - insns < 0x60;) {
		if (p[0] == X86_CALL) {
			CTraceFilterSimple_ctor ctor = (CTraceFilterSimple_ctor)(
					p + 5 + mem_loads32(p + 1));
			// calling the constructor to fill the vtable and other members
			// with values used by the engine. pass_ent is filled in runtime
			ctor(&filter, 0, 8, 0);
			return true;
		}
		NEXT_INSN(p, "CTraceFilterSimple constructor");
	}
	return false;
}

INIT {
	struct con_cmd *z_add = con_findcmd("z_add");
	if (!z_add || !find_EntityPlacementTest(z_add->cb_insns)) {
		errmsg_errorx("couldn't find EntityPlacementTest function");
		return FEAT_INCOMPAT;
	}
	if (!init_filter()) {
		errmsg_errorx("couldn't find trace filter ctor for EntityPlacementTest");
		return FEAT_INCOMPAT;
	}
	if_cold (!has_off_collision) {
		errmsg_warnx("missing m_Collision gamedata - warp preview unavailable");
	}
	else if_cold (!(dbgoverlay = factory_engine("VDebugOverlay003", 0))) {
		errmsg_warnx("couldn't find debug overlay interface - "
				"warp preview unavailable");
	}
	else {
		con_regcmd(sst_l4d_previewwarp);
	}
	return FEAT_OK;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
