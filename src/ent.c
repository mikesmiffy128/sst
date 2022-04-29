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
#include "gametype.h"
#include "intdefs.h"
#include "vcall.h"

DECL_VFUNC_DYN(void *, PEntityOfEntIndex, int)

void *ent_get(int idx) {
	// TODO(compat): Based on previous attempts at this, for L4D2, we need
	// factory_server("PlayerInfoManager002")->GetGlobalVars()->edicts
	// (offset 22 or so). Then get edicts from that. For now, we only need this
	// for Portal FOV stuff, so just doing this eiface stuff.

	struct edict *e = VCALL(engserver, PEntityOfEntIndex, idx);
	if (!e) return 0;
	return e->ent_unknown;
}

bool ent_init(void) {
	if (!has_vtidx_PEntityOfEntIndex) {
		con_warn("ent: missing gamedata entries for this engine\n");
		return false;
	}
	// for PEntityOfEntIndex we don't really have to do any more init, we can
	// just call the function later.
	return true;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
