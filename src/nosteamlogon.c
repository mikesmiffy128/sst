/*
 * Copyright © Hayden K <imaciidz@gmail.com>
 * Copyright © Willian Henrique <wsimanbrazil@yahoo.com.br>
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

#include "chatmsg.h"
#include "chunklets/x86.h"
#include "con_.h"
#include "errmsg.h"
#include "feature.h"
#include "gamedata.h"
#include "gametype.h"
#include "hook.h"
#include "intdefs.h"
#include "langext.h"
#include "mem.h"
#include "os.h"
#include "sst.h"
#include "vcall.h"
#include "x86util.h"

FEATURE("No Steam Logon bugfix")
// TOOD(compat): theoretically we can support Portal 2 later, since this issue
// affects runners there too. L4D:S would also probably work but is untested
// (and who cares about L4D:S anyway?)
GAMESPECIFIC(L4D)
REQUIRE(chatmsg)
REQUIRE_GAMEDATA(vtidx_GetClientName)

static void *Steam3Server = 0;

// find CSteam3Server instance in hostname callback
static bool find_Steam3Server(const uchar *insns) {
	for (const uchar *p = insns; p - insns < 24;) {
		if (p[0] == X86_CALL) {
			p = p + 5 + mem_loads32(p + 1);
			if (p[0] == X86_MOVEAXI) {
				Steam3Server = mem_loadptr(p + 1);
				return true;
			}
			else {
				errmsg_errorx("couldn't find Steam3Server object");
				return false;
			}
		}
		NEXT_INSN(p, "Steam3Server");
	}
	return false;
}

struct CCallback {
	void *vtable;
	u8 flags;
	int id;
	void *thisptr;
	struct { void *ptr; int adj; } memberfunc;
};

struct Steam3;
struct IClient;
struct CBaseClient;

// TODO(opt): these two hook trampolines could be effectively unioned but we
// need a non-horrible way to do that in hook.h. currently not wasting real
// space anyway since all the hooks fit in one page but could be relevant later?

// CSteam3Server::OnGSClientDenyHelper (L4D1 + L4D2 2000-2105)
typedef void (*VCALLCONV ClientDenyHelper_func)(struct Steam3 *,
		struct CBaseClient *, int, char const *);
DEF_INLINE_HOOK(ClientDenyHelper_func, ClientDenyHelper)

// CSteam3Server::OnValidateAuthResponseTicketHelper (L4D2 2106+)
typedef void (*VCALLCONV OVARTH_func)(struct Steam3 *,
		struct CBaseClient *, int);
DEF_INLINE_HOOK(OVARTH_func, OVARTH)

DECL_VFUNC_DYN(struct IClient, char *, GetClientName)

static opt_small void dochat(struct CBaseClient *cl, const char *s, int len) {
	// max length: see strings in hooks below
	char buf[35 + 64 + 11 + 50 + 2], *p = buf;
	struct IClient *icl = mem_offset(cl, 4); // second base interface/vtable
	char *name = GetClientName(icl); int namelen = strlen(name);
	bool abbrev = namelen > 61;
	if (abbrev) namelen = 61;
	mem_copy_fixed(p, "SST: Preventing kicking of player \"", 35); p += 35;
	mem_copy(p, name, namelen); p += namelen;
	if (abbrev) {
		mem_storeu32(p + 61, '....'); // overwrite by one so it's a single mov
		p += 3;
	}
	mem_copy_fixed(p, "\" (reason: ", 11); p += 11;
	mem_copy(p, s, len); p += len;
	mem_copy_fixed(p, ")", 2); // include \0
	chatmsg_broadcast(buf);
}

static opt_small void VCALLCONV hookdest_ClientDenyHelper(struct Steam3 *this,
		struct CBaseClient *cl, int reason, char const *optionaltext) {
	switch (reason) {
		case  3: dochat(cl, "k_EDenyNotLoggedOn", 18); break;
		case 11: dochat(cl, "k_EDenySteamConnectionLost", 26); break;
		case 12: dochat(cl, "k_EDenySteamConnectionError", 27); break;
		default: orig_ClientDenyHelper(this, cl, reason, optionaltext);
	}
}

static opt_small void VCALLCONV hookdest_OVARTH(struct Steam3 *this,
		struct CBaseClient *cl, int reason) {
	// calling this unconditionally because it does some other stuff. we patch
	// a switch jump table (see below) to skip the unwanted cases. the only
	// reason this hook is still here is to display the chat message as well.
	orig_OVARTH(this, cl, reason);
	switch (reason) {
		case 1:
			dochat(cl, "k_EAuthSessionResponseUserNotConnectedToSteam", 45);
			break;
		case 6:
			dochat(cl, "k_EAuthSessionResponseAuthTicketCanceled", 40);
			break;
		case 7:
			dochat(cl, "k_EAuthSessionResponseAuthTicketInvalidAlreadyUsed", 50);
			break;
		case 8:
			dochat(cl, "k_EAuthSessionResponseAuthTicketInvalid", 39);
	}
}

static struct CCallback *find_Steam3Callback(void) {
	if (has_off_NoSteamLogonCallback) {
		struct CCallback *cb = mem_offset(Steam3Server, off_NoSteamLogonCallback);
		if_hot (cb->id == 203) return cb; // double-check in case it ever breaks
	}
	// in L4D2 this offset got changed all the time, so just search through a
	// few different offsets for what looks like the right pointer.
	for (int off = 100; off != 124; off += 4) {
		struct CCallback *cb = mem_offset(Steam3Server, off);
		// in L4D2 2.1.0.6 this got changed from 203 to 143. just accept both
		if (cb->id == 143 || cb->id == 203) return cb;
	}
	return 0;
}

static void *find_NoSteamLogon(void *memberfunc_ptr) {
	const uchar *insns = memberfunc_ptr;
	int callcnt = 0;
	for (const uchar *p = insns; p - insns < 80;) {
		if (p[0] == X86_CALL && ++callcnt == 2) {
			return (void *)(p + 5 + mem_loads32(p + 1));
		}
		NEXT_INSN(p, "NoSteamLogon function");
	}
	errmsg_errorx("couldn't find NoSteamLogon function");
	return 0;
}

static void **jumptable;
static void *orig_jumptable_addrs[4];
static void *retaddr;

// note: these are the same as the switch cases in the OVARTH hook, but off by 1
// in the actual jump table for whatever reason.
static const s8 patchcases[] = {0, 5, 6, 7};

// note: this can find the switch jump table in all versions, but currently we
// only actually need it in 2206+.
static bool find_jumptable(void *NoSteamLogon, int id) {
	uchar *insns = NoSteamLogon;
	for (uchar *p = insns; p - insns < 128;) {
		if (p[0] == X86_MISCMW && p[1] == 0x24 && p[2] == 0x85) {
			jumptable = mem_loadptr(p + 3);
			goto _1;
		}
		NEXT_INSN(p, "jump table");
	}
	return false;
_1:	// find the end of the no steam logon func
	if (id == 203) insns = jumptable[2]; else insns = jumptable[0];
	for (uchar *p = insns; p - insns < 32;) {
		if (p[0] == X86_2BYTE && p[1] == X86_2B_JZII) {
			retaddr = p + 6 + mem_loads32(p + 2);
			return true;
		}
		NEXT_INSN(p, "return address");
	}
	return false;
}

INIT {
	struct con_var *cvar_hostname = con_findvar("hostname");
	if_cold (!cvar_hostname) return FEAT_FAIL;
	// XXX: cb is usually SST-specific, but happens to have the same offset as
	// the engine's cvar callbacks *usually*; however this won't work in Portal
	// 2 where there's actually a *list* of callbacks. revist later...
	if_cold (!find_Steam3Server((const uchar *)cvar_hostname->cb)) {
		return FEAT_INCOMPAT;
	}
	struct CCallback *cb = find_Steam3Callback();
	if_cold (!cb) {
		errmsg_errorx("couldn't find Steam3 callback");
		return FEAT_INCOMPAT;
	}
	void *NoSteamLogon = find_NoSteamLogon(cb->memberfunc.ptr);
	if_cold (!NoSteamLogon) {
		errmsg_errorx("couldn't find No Steam Logon function");
		return FEAT_INCOMPAT;
	}
	if (cb->id == 203) { // L4D1, or L4D2 before 2106 - old func
		// simple case: we can just wrap the entire function, and skip the call
		// when one of the offending switch cases is hit.
		struct hook_featsetup_ret_ClientDenyHelper h =
				hook_featsetup_ClientDenyHelper(
					(ClientDenyHelper_func)NoSteamLogon);
		if_cold (h.err) return h.err;
		hook_commit_ClientDenyHelper(h.hookpos, &hookdest_ClientDenyHelper);
	}
	else { // after 2106 - new func
		// the function apparently does some other stuff besides the switch
		// case, so we hook the jump table itself. the function hook is only
		// used for sending the chat messages.
		if_cold (!find_jumptable(NoSteamLogon, cb->id)) {
			errmsg_errorx("couldn't find jump table in No Steam Logon function");
			return FEAT_INCOMPAT;
		}
		if (!os_mprot(jumptable, 12 * sizeof(void *), PAGE_EXECUTE_READWRITE)) {
			errmsg_errorsys("couldn't make jump table writable");
			return FEAT_FAIL;
		}
		struct hook_featsetup_ret_OVARTH h = hook_featsetup_OVARTH(
				(OVARTH_func)NoSteamLogon);
		if_cold (h.err) return h.err;
		hook_commit_OVARTH(h.hookpos, &hookdest_OVARTH);
		// replace the offending switch cases with the function return point
		for (int i = 0; i < ssizeof(patchcases); ++i) {
			orig_jumptable_addrs[i] = jumptable[patchcases[i]];
			jumptable[patchcases[i]] = retaddr;
		}
	}
	return FEAT_OK;
}

END {
	if_cold (sst_userunloaded) {
		// XXX: could avoid this silliness here if these were unioned.
		if (retaddr) {
			for (int i = 0; i < ssizeof(patchcases); ++i) {
				jumptable[patchcases[i]] = orig_jumptable_addrs[i];
			}
			unhook_OVARTH();
		}
		else {
			unhook_ClientDenyHelper();
		}
	}
}

// vi: sw=4 ts=4 noet tw=80 cc=80
