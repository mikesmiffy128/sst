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

#include "con_.h"
#include "engineapi.h"
#include "ent.h"
#include "feature.h"
#include "gamedata.h"

FEATURE()
REQUIRE(ent)
REQUIRE_GAMEDATA(vtidx_ClientPrintf)
REQUIRE_GLOBAL(engserver)

DECL_VFUNC_DYN(struct VEngineServer, void, ClientPrintf, struct edict *,
		const char *)

void clientcon_msg(struct edict *e, const char *s) {
	ClientPrintf(engserver, e, s);
}

void clientcon_reply(const char *s) {
	struct edict *e = ent_getedict(con_cmdclient + 1);
	if (e) { clientcon_msg(e, s); return; }
}

INIT { return FEAT_OK; }

// vi: sw=4 ts=4 noet tw=80 cc=80
