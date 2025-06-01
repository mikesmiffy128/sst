/*
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
#include "engineapi.h"
#include "errmsg.h"
#include "feature.h"
#include "gamedata.h"
#include "gametype.h"
#include "hook.h"
#include "intdefs.h"
#include "langext.h"
#include "os.h"
#include "vcall.h"

FEATURE("autojump")
REQUIRE_GAMEDATA(off_mv)
REQUIRE_GAMEDATA(vtidx_CheckJumpButton)
REQUIRE_GLOBAL(factory_client) // note: server will never be null

struct CGameMovement { void **vtable; };
DEF_ACCESSORS(struct CGameMovement, struct CMoveData *, mv)

DEF_FEAT_CVAR(sst_autojump, "Jump upon hitting the ground while holding space",
		0, CON_REPLICATE | CON_DEMO)

#define NIDX 256 // *completely* arbitrary lol
static bool justjumped[NIDX] = {0};
static inline int handleidx(ulong h) { return h & (1 << 11) - 1; }

static struct CGameMovement *gmsv = 0, *gmcl = 0;
typedef bool (*VCALLCONV CheckJumpButton_func)(struct CGameMovement *);
static CheckJumpButton_func origsv, origcl;

static bool VCALLCONV hooksv(struct CGameMovement *this) {
	struct CMoveData *mv = get_mv(this);
	int idx = handleidx(mv->playerhandle);
	if (con_getvari(sst_autojump) && mv->firstrun && !justjumped[idx]) {
		mv->oldbuttons &= ~IN_JUMP;
	}
	bool ret = origsv(this);
	if (mv->firstrun) justjumped[idx] = ret;
	return ret;
}

static bool VCALLCONV hookcl(struct CGameMovement *this) {
	struct CMoveData *mv = get_mv(this);
	// FIXME: this will stutter in the rare case where justjumped is true.
	// currently doing clientside justjumped handling makes multiplayer
	// prediction in general wrong, so this'll need more work to do totally
	// properly.
	//if (con_getvari(sst_autojump) && !justjumped[0]) mv->oldbuttons &= ~IN_JUMP;
	if (con_getvari(sst_autojump)) mv->oldbuttons &= ~IN_JUMP;
	return justjumped[0] = origcl(this);
}

static bool unprot(struct CGameMovement *gm) {
	bool ret = os_mprot(gm->vtable + vtidx_CheckJumpButton, sizeof(void *),
			PAGE_READWRITE);
	if (!ret) errmsg_errorsys("couldn't make virtual table writable");
	return ret;
}

// reimplementing cheats check for dumb and bad reasons, see below
static struct con_var *sv_cheats;
static void cheatcb(struct con_var *this) {
	if (this->ival) if_cold (!con_getvari(sv_cheats)) {
		con_warn("Can't use cheat cvar sst_autojump, unless server has "
				"sv_cheats set to 1.\n");
		con_setvari(this, 0);
	}
}

INIT {
	gmsv = factory_server("GameMovement001", 0);
	if_cold (!gmsv) {
		errmsg_errorx("couldn't get server-side game movement interface");
		return FEAT_FAIL;
	}
	if_cold (!unprot(gmsv)) return FEAT_FAIL;
	gmcl = factory_client("GameMovement001", 0);
	if_cold (!gmcl) {
		errmsg_errorx("couldn't get client-side game movement interface");
		return FEAT_FAIL;
	}
	if_cold (!unprot(gmcl)) return FEAT_FAIL;
	origsv = (CheckJumpButton_func)hook_vtable(gmsv->vtable,
			vtidx_CheckJumpButton, (void *)&hooksv);
	origcl = (CheckJumpButton_func)hook_vtable(gmcl->vtable,
			vtidx_CheckJumpButton, (void *)&hookcl);

	if (GAMETYPE_MATCHES(Portal1)) {
		// this is a stupid, stupid policy that doesn't make any sense, but I've
		// tried arguing about it already and with how long it takes to convince
		// the Portal guys of anything I'd rather concede for now and maybe try
		// and revert this later if anyone eventually decides to be sensible.
		// and annoyingly, since cheats aren't even checked properly in portal,
		// it's also necessary to do this extremely stupid callback nonsense!
		sst_autojump->base.flags |= CON_CHEAT;
		sv_cheats = con_findvar("sv_cheats");
		sst_autojump->cb = cheatcb;
	}
	return FEAT_OK;
}

END {
	unhook_vtable(gmsv->vtable, vtidx_CheckJumpButton, (void *)origsv);
	unhook_vtable(gmcl->vtable, vtidx_CheckJumpButton, (void *)origcl);
}

// vi: sw=4 ts=4 noet tw=80 cc=80
