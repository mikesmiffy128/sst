/*
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

#include "con_.h"
#include "engineapi.h"
#include "errmsg.h"
#include "feature.h"
#include "gamedata.h"
#include "gametype.h"
#include "intdefs.h"
#include "hook.h"
#include "mem.h"
#include "os.h"
#include "vcall.h"

FEATURE("autojump")
REQUIRE_GAMEDATA(off_mv)
REQUIRE_GAMEDATA(vtidx_CheckJumpButton)
REQUIRE_GLOBAL(factory_client) // note: server will never be null

DEF_CVAR(sst_autojump, "Jump upon hitting the ground while holding space", 0,
		CON_REPLICATE | CON_DEMO | CON_HIDDEN)

#define IN_JUMP 2
#define NIDX 256 // *completely* arbitrary lol
static bool justjumped[NIDX] = {0};
static inline int handleidx(ulong h) { return h & (1 << 11) - 1; }

static void *gmsv = 0, *gmcl = 0;
typedef bool (*VCALLCONV CheckJumpButton_func)(void *);
static CheckJumpButton_func origsv, origcl;

static bool VCALLCONV hooksv(void *this) {
	struct CMoveData *mv = mem_loadptr(mem_offset(this, off_mv));
	int idx = handleidx(mv->playerhandle);
	if (con_getvari(sst_autojump) && mv->firstrun && !justjumped[idx]) {
		mv->oldbuttons &= ~IN_JUMP;
	}
	bool ret = origsv(this);
	if (mv->firstrun) justjumped[idx] = ret;
	return ret;
}

static bool VCALLCONV hookcl(void *this) {
	struct CMoveData *mv = mem_loadptr(mem_offset(this, off_mv));
	// FIXME: this will stutter in the rare case where justjumped is true.
	// currently doing clientside justjumped handling makes multiplayer
	// prediction in general wrong, so this'll need more work to do totally
	// properly.
	//if (con_getvari(sst_autojump) && !justjumped[0]) mv->oldbuttons &= ~IN_JUMP;
	if (con_getvari(sst_autojump)) mv->oldbuttons &= ~IN_JUMP;
	return justjumped[0] = origcl(this);
}

static bool unprot(void *gm) {
	void **vtable = *(void ***)gm;
	bool ret = os_mprot(vtable + vtidx_CheckJumpButton, sizeof(void *),
			PAGE_READWRITE);
	if (!ret) errmsg_errorsys("couldn't make virtual table writable");
	return ret;
}

INIT {
	gmsv = factory_server("GameMovement001", 0);
	if (!gmsv) {
		errmsg_errorx("couldn't get server-side game movement interface");
		return false;
	}
	if (!unprot(gmsv)) return false;
	gmcl = factory_client("GameMovement001", 0);
	if (!gmcl) {
		errmsg_errorx("couldn't get client-side game movement interface");
		return false;
	}
	if (!unprot(gmcl)) return false;
	origsv = (CheckJumpButton_func)hook_vtable(*(void ***)gmsv,
			vtidx_CheckJumpButton, (void *)&hooksv);
	origcl = (CheckJumpButton_func)hook_vtable(*(void ***)gmcl,
			vtidx_CheckJumpButton, (void *)&hookcl);

	sst_autojump->base.flags &= ~CON_HIDDEN;
	if (GAMETYPE_MATCHES(Portal1)) {
		// this is a stupid, stupid policy that doesn't make any sense, but I've
		// tried arguing about it already and with how long it takes to convince
		// the Portal guys of anything I'd rather concede for now and maybe try
		// and revert this later if anyone eventually decides to be sensible.
		// the alternative is nobody's allowed to use SST in runs - except of
		// course the couple of people who just roll the dice anyway, and
		// thusfar haven't actually been told to stop. yeah, whatever.
		sst_autojump->base.flags |= CON_CHEAT;
	}
	return true;
}

END {
	unhook_vtable(*(void ***)gmsv, vtidx_CheckJumpButton, (void *)origsv);
	unhook_vtable(*(void ***)gmcl, vtidx_CheckJumpButton, (void *)origcl);
}

// vi: sw=4 ts=4 noet tw=80 cc=80
