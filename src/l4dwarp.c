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

#define _USE_MATH_DEFINES // ... windows.
#include <math.h>

#include "con_.h"
#include "engineapi.h"
#include "errmsg.h"
#include "ent.h"
#include "feature.h"
#include "gamedata.h"
#include "gametype.h"
#include "intdefs.h"
#include "mem.h"
#include "vcall.h"

FEATURE("Left 4 Dead warp testing")
REQUIRE_GAMEDATA(off_entpos)
REQUIRE_GAMEDATA(off_eyeang)
REQUIRE_GAMEDATA(vtidx_Teleport)

DECL_VFUNC_DYN(void *, GetBaseEntity)
DECL_VFUNC_DYN(void, Teleport, const struct vec3f */*pos*/,
		const struct vec3f */*pos*/, const struct vec3f */*vel*/)

DEF_CCMD_HERE_UNREG(sst_l4d_testwarp, "Simulate a bot warping to you",
		CON_SERVERSIDE | CON_CHEAT) {
	struct edict *ed = ent_getedict(con_cmdclient + 1);
	if (!ed) { errmsg_errorx("couldn't access player entity"); return; }
	void *e = GetBaseEntity(ed->ent_unknown); // is this call required?
	struct vec3f *org = mem_offset(e, off_entpos);
	struct vec3f *ang = mem_offset(e, off_eyeang);
	// L4D idle warps go up to 10 units behind relative to whatever angle the
	// player is facing, lessening the distance based on pitch angle but never
	// displacing vertically
	float pitch = ang->x * M_PI / 180, yaw = ang->y * M_PI / 180;
	float shift = -10 * cos(pitch);
	Teleport(e, &(struct vec3f){org->x + shift * cos(yaw),
			org->y + shift * sin(yaw), org->z}, 0, &(struct vec3f){0, 0, 0});
}

PREINIT {
	return GAMETYPE_MATCHES(L4Dx);
}

INIT {
	con_reg(sst_l4d_testwarp);
	return true;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
