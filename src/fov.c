/*
 * Copyright © 2023 Michael Smith <mikesmiffy128@gmail.com>
 * Copyright © 2022 Willian Henrique <wsimanbrazil@yahoo.com.br>
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

// TODO(linux): theoretically, probably ifdef out the cvar-replacement stuff; we
// expect any game that's been ported to linux to already have fov_desired

#include "con_.h"
#include "engineapi.h"
#include "errmsg.h"
#include "ent.h"
#include "event.h"
#include "feature.h"
#include "gametype.h"
#include "hook.h"
#include "intdefs.h"
#include "mem.h"
#include "sst.h"
#include "vcall.h"
#include "x86.h"
#include "x86util.h"

FEATURE("extended FOV range")
REQUEST(ent)

DEF_CVAR_MINMAX_UNREG(fov_desired,
		"Set the base field of view (SST reimplementation)", 75, 75, 120,
		CON_HIDDEN | CON_ARCHIVE)
static struct con_var *real_fov_desired; // engine's if it has it, or ours

typedef void (*VCALLCONV SetDefaultFOV_func)(void *, int);
static SetDefaultFOV_func orig_SetDefaultFOV;
static void VCALLCONV hook_SetDefaultFOV(void *this, int fov) {
	// disregard server-side clamped value and force our own value instead
	orig_SetDefaultFOV(this, con_getvari(real_fov_desired));
}

static bool find_SetDefaultFOV(struct con_cmd *fov) {
	const uchar *insns = (const uchar *)fov->cb;
	int callcnt = 0;
	for (const uchar *p = insns; p - insns < 96;) {
		// The fov command calls 4 functions, one of them virtual. Of the 3
		// direct calls, SetDefaultFOV() is the third.
		if (p[0] == X86_CALL && ++callcnt == 3) {
			orig_SetDefaultFOV = (SetDefaultFOV_func)(p + 5 +
					mem_loadoffset(p + 1));
			return true;
		}
		NEXT_INSN(p, "SetDefaultFOV function");
	}
	return false;
}

// replacement cvar needs to actively set player fov if in a map
static void fovcb(struct con_var *v) {
	void *player = ent_get(1); // NOTE: singleplayer only!
	if (player) orig_SetDefaultFOV(player, con_getvari(v));
}

// ensure FOV is applied on load, if the engine wouldn't do that itself
HANDLE_EVENT(ClientActive, struct edict *e) {
	if (real_fov_desired == fov_desired) {
		orig_SetDefaultFOV(e->ent_unknown, con_getvari(fov_desired));
	}
}

static struct con_cmd *cmd_fov;

PREINIT {
	// could work for other games, but generally only portal 1 people want this
	// (the rest of us consider this cheating and a problem for runs...)
	return GAMETYPE_MATCHES(Portal1);
}

INIT {
	cmd_fov = con_findcmd("fov");
	if (!cmd_fov) return false; // shouldn't really happen but just in case!
	if (real_fov_desired = con_findvar("fov_desired")) {
		// latest steampipe already goes up to 120 fov
		if (real_fov_desired->parent->maxval == 120) return false;
		real_fov_desired->parent->maxval = 120;
	}
	else {
		if (!has_ent) return false;
		con_reg(fov_desired);
		real_fov_desired = fov_desired;
	}
	if (!find_SetDefaultFOV(cmd_fov)) {
		errmsg_errorx("couldn't find SetDefaultFOV function");
		return false;
	}
	orig_SetDefaultFOV = (SetDefaultFOV_func)hook_inline(
			(void *)orig_SetDefaultFOV, (void *)&hook_SetDefaultFOV);
	if (!orig_SetDefaultFOV) {
		errmsg_errorsys("couldn't hook SetDefaultFOV function");
		return false;
	}

	// we might not be using our cvar but simpler to do this unconditionally
	fov_desired->cb = &fovcb;
	fov_desired->parent->base.flags &= ~CON_HIDDEN;
	// hide the original fov command since we've effectively broken it anyway :)
	cmd_fov->base.flags |= CON_DEVONLY;
	return true;
}

END {
	if (real_fov_desired && real_fov_desired != fov_desired) {
		real_fov_desired->parent->maxval = 90;
		if (con_getvarf(real_fov_desired) > 90) {
			con_setvarf(real_fov_desired, 90); // blegh.
		}
	}
	else {
		void *player = ent_get(1); // also singleplayer only
		if (player) orig_SetDefaultFOV(player, 75);
	}
	unhook_inline((void *)orig_SetDefaultFOV);
	cmd_fov->base.flags &= ~CON_DEVONLY;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
