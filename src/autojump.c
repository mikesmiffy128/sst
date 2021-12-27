/*
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
#include "factory.h"
#include "gamedata.h"
#include "intdefs.h"
#include "hook.h"
#include "mem.h"
#include "os.h"
#include "vcall.h"

DEF_CVAR(sst_autojump, "Jump upon hitting the group while holding space", "0",
		CON_REPLICATE | CON_DEMO | CON_HIDDEN)

struct vec3f { float x, y, z; };
struct CMoveData {
	bool firstrun : 1;
	bool gamecodemoved : 1;
	ulong playerhandle;
	int impulse;
	struct vec3f viewangles;
	struct vec3f absviewangles;
	int buttons;
	int oldbuttons;
	float mv_forward;
	float mv_side;
	float mv_up;
	float maxspeed;
	float clientmaxspeed;
	struct vec3f vel;
	struct vec3f angles;
	struct vec3f oldangles;
	float out_stepheight;
	struct vec3f out_wishvel;
	struct vec3f out_jumpvel;
	struct vec3f constraint_center;
	float constraint_radius;
	float constraint_width;
	float constraint_speedfactor;
	struct vec3f origin;
};

#define IN_JUMP 2
#define NIDX 256 // *completely* arbitrary lol
static bool justjumped[NIDX] = {0};
static inline int handleidx(ulong h) { return h & (1 << 11) - 1; }

static void *gmsv = 0, *gmcl = 0;
typedef bool (VCALLCONV *CheckJumpButton_f)(void *);
static CheckJumpButton_f origsv, origcl;

static bool VCALLCONV hook(void *this) {
	struct CMoveData **mvp = mem_offset(this, gamedata_off_mv), *mv = *mvp;
	// use 0 idx for client side, as server indices start at 1
	// FIXME: does this account for splitscreen???
	int i = this == gmsv ? handleidx(mv->playerhandle) : 0;
	if (con_getvari(sst_autojump) && !justjumped[i]) mv->oldbuttons &= ~IN_JUMP;
	return justjumped[i] = (this == gmsv ? origsv : origcl)(this);
}

static bool unprot(void *gm) {
	void **vtable = *(void ***)gm;
	bool ret = os_mprot(vtable + gamedata_vtidx_CheckJumpButton,
			sizeof(void *), PAGE_EXECUTE_READWRITE);
	if (!ret) con_warn("autojump: couldn't make memory writeable\n");
	return ret;
}

bool autojump_init(void) {
	// TODO(featgen): auto-check these factories
	if (!factory_client || !factory_server) {
		con_warn("autojump: missing required interfaces\n");
		return false;
	}
	if (!gamedata_has_vtidx_CheckJumpButton || !gamedata_has_off_mv) {
		con_warn("autojump: missing gamedata entries for this engine\n");
		return false;
	}

	gmsv = factory_server("GameMovement001", 0);
	if (!gmsv) {
		con_warn("autojump: couldn't get server-side game movement interface\n");
		return false;
	}
	if (!unprot(gmsv)) return false;
	gmcl = factory_client("GameMovement001", 0);
	if (!gmcl) {
		con_warn("autojump: couldn't get client-side game movement interface\n");
		return false;
	}
	if (!unprot(gmcl)) return false;
	origsv = (CheckJumpButton_f)hook_vtable(*(void ***)gmsv,
			gamedata_vtidx_CheckJumpButton, (void *)&hook);
	origcl = (CheckJumpButton_f)hook_vtable(*(void ***)gmcl,
			gamedata_vtidx_CheckJumpButton, (void *)&hook);

	sst_autojump->base.flags &= ~CON_HIDDEN;
	return true;
}

void autojump_end(void) {
	unhook_vtable(*(void ***)gmsv, gamedata_vtidx_CheckJumpButton,
			(void *)origsv);
	unhook_vtable(*(void ***)gmcl, gamedata_vtidx_CheckJumpButton,
			(void *)origcl);
}

// vi: sw=4 ts=4 noet tw=80 cc=80
