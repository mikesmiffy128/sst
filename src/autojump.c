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
#include "gamedata.h"
#include "intdefs.h"
#include "hook.h"
#include "mem.h"
#include "os.h"
#include "vcall.h"

DEF_CVAR(sst_autojump, "Jump upon hitting the ground while holding space", 0,
		CON_REPLICATE | CON_DEMO | CON_HIDDEN)

#define IN_JUMP 2
#define NIDX 256 // *completely* arbitrary lol
static bool justjumped[NIDX] = {0};
static inline int handleidx(ulong h) { return h & (1 << 11) - 1; }

static void *gmsv = 0, *gmcl = 0;
typedef bool (*VCALLCONV CheckJumpButton_func)(void *);
static CheckJumpButton_func origsv, origcl;

static bool VCALLCONV hook(void *this) {
	struct CMoveData **mvp = mem_offset(this, off_mv), *mv = *mvp;
	// use 0 idx for client side, as server indices start at 1
	// FIXME: does this account for splitscreen???
	int i = this == gmsv ? handleidx(mv->playerhandle) : 0;
	if (con_getvari(sst_autojump) && !justjumped[i]) mv->oldbuttons &= ~IN_JUMP;
	return justjumped[i] = (this == gmsv ? origsv : origcl)(this);
}

static bool unprot(void *gm) {
	void **vtable = *(void ***)gm;
	bool ret = os_mprot(vtable + vtidx_CheckJumpButton, sizeof(void *),
			PAGE_EXECUTE_READWRITE);
	if (!ret) con_warn("autojump: couldn't make memory writable\n");
	return ret;
}

bool autojump_init(void) {
	// TODO(featgen): auto-check these factories
	if (!factory_client || !factory_server) {
		con_warn("autojump: missing required factories\n");
		return false;
	}
	if (!has_vtidx_CheckJumpButton || !has_off_mv) {
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
	origsv = (CheckJumpButton_func)hook_vtable(*(void ***)gmsv,
			vtidx_CheckJumpButton, (void *)&hook);
	origcl = (CheckJumpButton_func)hook_vtable(*(void ***)gmcl,
			vtidx_CheckJumpButton, (void *)&hook);

	sst_autojump->base.flags &= ~CON_HIDDEN;
	return true;
}

void autojump_end(void) {
	unhook_vtable(*(void ***)gmsv, vtidx_CheckJumpButton, (void *)origsv);
	unhook_vtable(*(void ***)gmcl, vtidx_CheckJumpButton, (void *)origcl);
}

// vi: sw=4 ts=4 noet tw=80 cc=80
