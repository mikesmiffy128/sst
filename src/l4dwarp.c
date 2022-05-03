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
#include <stdbool.h>

#include "con_.h"
#include "engineapi.h"
#include "ent.h"
#include "gamedata.h"
#include "gametype.h"
#include "intdefs.h"
#include "mem.h"
#include "vcall.h"

DECL_VFUNC_DYN(void *, GetBaseEntity)
DECL_VFUNC_DYN(void, Teleport, const struct vec3f *pos, const struct vec3f *ang,
		const struct vec3f *vel)

DEF_CCMD_HERE_UNREG(sst_l4d_testwarp, "Simulate a bot warping to you",
		CON_SERVERSIDE) {
	struct edict *ed = ent_getedict(con_cmdclient + 1);
	if (!ed) { con_warn("error: couldn't access player entity\n"); return; }
	void *e = VCALL(ed->ent_unknown, GetBaseEntity); // is this call required?
	struct vec3f *org = mem_offset(e, off_entpos);
	struct vec3f *ang = mem_offset(e, off_eyeang);
	// L4D idle warps go up to 10 units behind relative to whatever angle the
	// player is facing, lessening the distance based on pitch angle but never
	// displacing vertically
	float pitch = ang->x * M_PI / 180, yaw = ang->y * M_PI / 180;
	float shift = -10 * cos(pitch);
	VCALL(e, Teleport, &(struct vec3f){org->x + shift * cos(yaw),
			org->y + shift * sin(yaw), org->z}, 0, &(struct vec3f){0, 0, 0});
}

bool l4dwarp_init(void) {
	if (!GAMETYPE_MATCHES(L4Dx)) return false;
	if (!has_off_entpos || !has_off_eyeang || !has_vtidx_Teleport) {
		con_warn("l4dwarp: missing gamedata entries for this engine\n");
		return false;
	}
	con_reg(sst_l4d_testwarp);
	return true;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
