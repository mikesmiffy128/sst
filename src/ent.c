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

#include "engineapi.h"
#include "errmsg.h"
#include "gamedata.h"
#include "gametype.h"
#include "intdefs.h"
#include "mem.h"
#include "vcall.h"

DECL_VFUNC_DYN(void *, PEntityOfEntIndex, int)
static struct edict **edicts = 0;

void *ent_getedict(int idx) {
	if (edicts) {
		// globalvars->edicts seems to be null when disconnected
		if (!*edicts) return 0;
		return mem_offset(*edicts, sz_edict * idx);
	}
	else {
		return VCALL(engserver, PEntityOfEntIndex, idx);
	}
}

void *ent_get(int idx) {
	struct edict *e = ent_getedict(idx);
	if (!e) return 0;
	return e->ent_unknown;
}

bool ent_init(void) {
	// for PEntityOfEntIndex we don't really have to do any more init, we
	// can just call the function later.
	if (has_vtidx_PEntityOfEntIndex) return true;
	if (globalvars && has_off_edicts) {
		edicts = mem_offset(globalvars, off_edicts);
		return true;
	}
	errmsg_warnx("not implemented for this engine");
	return false;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
