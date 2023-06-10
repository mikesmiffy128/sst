/*
 * Copyright © 2023 Michael Smith <mikesmiffy128@gmail.com>
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

#include <string.h>

#include "engineapi.h"
#include "errmsg.h"
#include "feature.h"
#include "gamedata.h"
#include "gametype.h"
#include "kvsys.h"
#include "mem.h"
#include "os.h"
#include "vcall.h"

FEATURE()
REQUIRE(kvsys)
REQUIRE_GAMEDATA(vtidx_GetMatchNetworkMsgController)
REQUIRE_GAMEDATA(vtidx_GetActiveGameServerDetails)

DECL_VFUNC_DYN(void *, GetMatchNetworkMsgController)
DECL_VFUNC_DYN(struct KeyValues *, GetActiveGameServerDetails,
		struct KeyValues *)

// Old L4D1 uses a heavily modified version of the CMatchmaking in Source 2007.
// None of it is publicly documented or well-understood but I was able to figure
// out that this random function does something *close enough* to what we want.
struct contextval {
	const char *name;
	int _unknown[8];
	const char *val;
	/* other stuff unknown */
};
DECL_VFUNC(struct contextval *, unknown_contextlookup, 67, const char *)

static void *matchfwk;
static union { // space saving
	struct { int sym_game, sym_campaign; }; // "game/campaign" KV lookup
	void *oldmmiface; // old L4D1 interface
} U;
#define oldmmiface U.oldmmiface
#define sym_game U.sym_game
#define sym_campaign U.sym_campaign
static char campaignbuf[32];

const char *l4dmm_curcampaign(void) {
#ifdef _WIN32
	if (!matchfwk) { // we must have oldmmiface, then
		struct contextval *ctxt = unknown_contextlookup(oldmmiface,
				"CONTEXT_L4D_CAMPAIGN");
		if (ctxt) {
			// HACK: since this context symbol stuff was the best that was found
			// for this old MM interface, just map things back to their names
			// manually. bit stupid, but it gets the (rather difficult) job done
			if (strncmp(ctxt->val, "CONTEXT_L4D_CAMPAIGN_", 21)) return 0;
			if (!strcmp(ctxt->val + 21, "APARTMENTS")) return "Hospital";
			if (!strcmp(ctxt->val + 21, "CAVES")) return "SmallTown";
			if (!strcmp(ctxt->val + 21, "GREENHOUSE")) return "Airport";
			if (!strcmp(ctxt->val + 21, "HILLTOP")) return "Farm";
		}
		return 0;
	}
#endif
	void *ctrlr = GetMatchNetworkMsgController(matchfwk);
	struct KeyValues *kv = GetActiveGameServerDetails(ctrlr, 0);
	if (!kv) return 0; // not in server, probably
	const char *ret = 0;
	struct KeyValues *subkey = kvsys_getsubkey(kv, sym_game);
	if (subkey) subkey = kvsys_getsubkey(subkey, sym_campaign);
	if (subkey) ret = kvsys_getstrval(subkey);
	if (ret) {
		// ugh, we have to free all the memory allocated by the engine, so copy
		// this glorified global state to a buffer so the caller doesn't have to
		// deal with freeing. this necessitates a length cap but it's hopefully
		// reasonable...
		int len = strlen(ret);
		if (len > sizeof(campaignbuf) - 1) ret = 0;
		else ret = memcpy(campaignbuf, ret, len + 1);
	}
	kvsys_free(kv);
	return ret;
}

INIT {
	// XXX: ugh, we NEED to centralise library stuff at some point, this sucks
#ifdef _WIN32
	void *mmlib = GetModuleHandleW(L"matchmaking.dll");
#else
	void *mmlib = dlopen("matchmaking.so", RTLD_NOW | RTLD_NOLOAD);
	if (mmlib) dlclose(mmlib);
#endif
	if (mmlib) {
		ifacefactory factory = (ifacefactory)os_dlsym(mmlib, "CreateInterface");
		if (!factory) {
			errmsg_errordl("couldn't get matchmaking interface factory");
			return false;
		}
		matchfwk = factory("MATCHFRAMEWORK_001", 0);
		if (!matchfwk) {
			errmsg_errorx("couldn't get IMatchFramework interface");
			return false;
		}
		sym_game = kvsys_strtosym("game");
		sym_campaign = kvsys_strtosym("campaign");
	}
	else {
#ifdef _WIN32
		oldmmiface = factory_engine("VENGINE_MATCHMAKING_VERSION001", 0);
		if (!oldmmiface) {
			errmsg_errorx("couldn't get IMatchmaking interface");
			return false;
		}
#else // Linux L4D1 has always used the separate matchmaking library
		errmsg_errordl("couldn't get matchmaking library");
		return false;
#endif
	}
	return true;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
