/*
 * Copyright © Michael Smith <mikesmiffy128@gmail.com>
 * Copyright © Hayden K <imaciidz@gmail.com>
 * Copyright © Willian Henrique <wsimanbrazil@yahoo.com.br>
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
#include "engineapi.h"
#include "ent.h"
#include "feature.h"
#include "langext.h"
#include "x86util.h"

FEATURE()
REQUIRE(ent)
REQUIRE_GLOBAL(globalvars)

struct IRecipientFilter {
	const struct IRecipientFilter_vtable {
		void *dtor;
#ifndef _WIN32
		void *dtor2;
#endif
		bool (*VCALLCONV IsReliable)(struct IRecipientFilter *this);
		bool (*VCALLCONV IsInitMessage)(struct IRecipientFilter *this);
		int (*VCALLCONV GetRecipientCount)(struct IRecipientFilter *this);
		int (*VCALLCONV GetRecipientIndex)(struct IRecipientFilter *this,
				int slot);
	} *vtable;
};

// this function serves as two identical functions!
static bool VCALLCONV IsReliable_IsInitMessage(struct IRecipientFilter *this) {
	return false;
}

static int VCALLCONV GetRecipientCount(struct IRecipientFilter *this) {
	int count = 0;
	for (int i = 1; i <= globalvars->maxclients; ++i) {
		// if there's an entity in a player slot, just assume there's a player.
		// this is simpler than what the engine's own CBroadcastRecipientFilter
		// seems to do, but holds up in practice
		struct edict *e = ent_getedict(i);
		if (e && e->ent_unknown) ++count;
	}
	return count;
}

static int VCALLCONV GetRecipientIndex(struct IRecipientFilter *this, int slot) {
	if_cold (slot < 0 || slot > globalvars->maxclients) return -1;
	// this isn't _particularly_ efficient (is it technically quadratic?)
	// but maxplayers is never a particularly big number, so it's not really a
	// big deal to do this just to send the occasional chat broadcast
	for (int i = 1; i <= globalvars->maxclients; ++i) {
		struct edict *e = ent_getedict(i);
		if (e && e->ent_unknown && !slot--) return i;
	}
	return -1;
}

static const struct IRecipientFilter_vtable recipientfilter_vtable = {
	0, // should never get called
#ifndef _WIN32
	0, // ditto
#endif
	&IsReliable_IsInitMessage,
	&IsReliable_IsInitMessage,
	&GetRecipientCount,
	&GetRecipientIndex
};

typedef void (*UTIL_ClientPrintFilter_func)(struct IRecipientFilter *, int,
		const char *, const char *, const char *, const char *, const char *);
static UTIL_ClientPrintFilter_func UTIL_ClientPrintFilter;

static bool find_UTIL_ClientPrintFilter(const uchar *insns) {
#ifdef _WIN32
	for (const uchar *p = insns; p - insns < 128;) {
		if (p[0] == X86_PUSHI8 && p[1] == 0x02 && p[3] == X86_CALL) {
			p = p + 8 + mem_loads32(p + 4);
			insns = p;
			for (p = insns; p - insns < 96;) {
				if (p[0] == X86_PUSHEAX && p[1] == X86_CALL) {
					UTIL_ClientPrintFilter = (UTIL_ClientPrintFilter_func)(
							p + 6 + mem_loads32(p + 2));
					return true;
				}
				NEXT_INSN(p, "UTIL_ClientPrintFilter");
			}
			return false;
		}
		NEXT_INSN(p, "UTIL_ClientPrintFilter");
	}
#endif
	// TODO(linux): the usual code search stuff
	return false;
}

void chatmsg_broadcast(const char *msg) {
	struct IRecipientFilter filter = {.vtable = &recipientfilter_vtable};
	UTIL_ClientPrintFilter(&filter, /*HUD_PRINTTALK*/ 3, msg, 0, 0, 0, 0);
	return;
}

INIT {
	struct con_cmd *cmd_ent_dump = con_findcmd("ent_dump");
	if_cold (!cmd_ent_dump) return FEAT_INCOMPAT;
	if_cold (!find_UTIL_ClientPrintFilter(cmd_ent_dump->cb_insns)) {
		errmsg_errorx("couldn't find UTIL_ClientPrintFilter");
		return FEAT_INCOMPAT;
	}
	return FEAT_OK;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
