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

#include "chunklets/x86.h"
#include "con_.h"
#include "errmsg.h"
#include "feature.h"
#include "gamedata.h"
#include "intdefs.h"
#include "langext.h"
#include "mem.h"
#include "vcall.h"
#include "x86util.h"

FEATURE()
REQUIRE_GAMEDATA(vtidx_GetSpawnCount)

struct CGameServer;
DECL_VFUNC_DYN(struct CGameServer, int, GetSpawnCount)

static struct CGameServer *sv;

int gameserver_spawncount() { return GetSpawnCount(sv); }

static bool find_sv(const uchar *insns) {
#ifdef _WIN32
	// The last thing pause does is call BroadcastPrintf with 4 args including
	// `this`, all on the stack since it's varargs. 2 of the args are pushed
	// immediately before `this`, so we can just look for 3 back-to-back pushes
	// and a call.
	int pushes = 0;
	for (const uchar *p = insns; p - insns < 256;) {
		if (*p == X86_PUSHIW || *p >= X86_PUSHEAX && *p <= X86_PUSHEDI) {
			if (++pushes == 3) {
				if (*p != X86_PUSHIW || p[5] != X86_CALL) {
					// it'd be super weird to have this many pushes anywhere
					// else in the function, so give up here
					return false;
				}
				sv = mem_loadptr(p + 1);
				return true;
			}
		}
		else {
			pushes = 0;
		}
		NEXT_INSN(p, "load of sv pointer");
	}
#else
#warning TODO(linux): the usual x86 stuff
#endif
	return false;
}

INIT {
	struct con_cmd *pause = con_findcmd("pause");
	if_cold (!find_sv(pause->cb_insns)) {
		errmsg_errorx("couldn't find game server object");
		return FEAT_INCOMPAT;
	}
	return FEAT_OK;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
