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
#include <string.h>

#include "alias.h"
#include "con_.h"
#include "dbg.h"
#include "errmsg.h"
#include "extmalloc.h"
#include "mem.h"
#include "x86.h"

struct alias **_alias_head;

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

DEF_CCMD_HERE(sst_alias_remove, "Remove a command alias", 0) {
	if (cmd->argc != 2) {
		con_warn("usage: sst_alias_remove name");
		return;
	}
	if (strlen(cmd->argv[1]) > 31) {
		con_warn("invalid alias name (too long)");
		return;
	}
	alias_rm(cmd->argv[1]);
}

void alias_nuke(void) {
	for (struct alias *p = alias_head; p;) {
		struct alias *next = p->next;
		extfree(p->value); extfree(p);
		p = next;
	}
	alias_head = 0;
}

DEF_CCMD_HERE(sst_alias_clear, "Remove all command aliases", 0) {
	if (cmd->argc != 1) {
		con_warn("usage: sst_alias_clear");
		return;
	}
	alias_nuke();
}

// XXX: same as in demorec, might want some abstraction for this
#define NEXT_INSN(p, tgt) do { \
	int _len = x86_len(p); \
	if (_len == -1) { \
		errmsg_errorx("unknown or invalid instruction looking for %s", tgt); \
		return false; \
	} \
	(p) += _len; \
} while (0)

static bool find_alias_head(con_cmdcb alias_cb) {
#ifdef _WIN32
	for (uchar *p = (uchar *)alias_cb; p - (uchar *)alias_cb < 64;) {
		// alias command with no args calls ConMsg() then loads the head pointer
		// that asm looks like: call <reg>; mov <reg>, dword ptr [x]
		// (we don't care about the exact registers)
		if (p[0] == X86_MISCMW && (p[1] & 0xF0) == 0xD0 &&
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

bool alias_init(void) {
	struct con_cmd *cmd_alias = con_findcmd("alias");
	if (!cmd_alias) {
		errmsg_warnx("couldn't find \"alias\" command");
		return false;
	}
	if (!find_alias_head(con_getcmdcb(cmd_alias))) {
		errmsg_warnx("couldn't find alias list");
		return false;
	};
	return true;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
