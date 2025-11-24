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

#include <string.h>

#include "alias.h"
#include "chunklets/x86.h"
#include "con_.h"
#include "errmsg.h"
#include "extmalloc.h"
#include "feature.h"
#include "gametype.h"
#include "mem.h"
#include "x86util.h"

FEATURE("alias management")

struct alias **_alias_head;

void alias_nuke() {
	for (struct alias *p = alias_head; p;) {
		struct alias *next = p->next;
		extfree(p->value); extfree(p);
		p = next;
	}
	alias_head = 0;
}

void alias_rm(const char *name) {
	for (struct alias **p = _alias_head; *p; p = &(*p)->next) {
		if (!strcmp((*p)->name, name)) {
			struct alias *next = (*p)->next;
			extfree((*p)->value); extfree(*p);
			*p = next;
			return;
		}
	}
}

DEF_FEAT_CCMD_HERE(sst_alias_clear, "Remove all command aliases", 0) {
	if (argc != 1) {
		con_warn("usage: sst_alias_clear\n");
		return;
	}
	alias_nuke();
}

DEF_FEAT_CCMD_HERE(sst_alias_remove, "Remove a command alias", 0) {
	if (argc != 2) {
		con_warn("usage: sst_alias_remove name\n");
		return;
	}
	if (strlen(argv[1]) > 31) {
		con_warn("invalid alias name (too long)\n");
		return;
	}
	alias_rm(argv[1]);
}

static bool find_alias_head(const uchar *insns) {
#ifdef _WIN32
	for (const uchar *p = insns; p - insns < 64;) {
		// alias command with no args calls ConMsg() then loads the head pointer
		// that asm looks like: call <reg>; mov <reg>, dword ptr [x]
		if (p[0] == X86_MISCMW && (p[1] & 0xF8) == 0xD0 &&
				p[2] == X86_MOVRMW && (p[3] & 0xC7) == 0x05) {
			_alias_head = mem_loadptr(p + 4);
			return true;
		}
		NEXT_INSN(p, "load of alias list");
	}
#else
#warning TODO(linux): check whether linux is equivalent!
#endif
	return false;
}

INIT {
	// TODO(compat): no idea why sst_alias_clear crashes in p2, figure out later
	if (GAMETYPE_MATCHES(Portal2)) return FEAT_INCOMPAT;

	struct con_cmd *cmd_alias = con_findcmd("alias");
	if_cold (!find_alias_head(cmd_alias->cb_insns)) {
		errmsg_warnx("couldn't find alias list");
		return FEAT_INCOMPAT;
	}
	return FEAT_OK;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
