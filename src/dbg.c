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

#ifdef _WIN32
#include <Windows.h>
#endif

#include "accessor.h"
#include "con_.h"
#include "engineapi.h"
#include "errmsg.h"
#include "gamedata.h"
#include "intdefs.h"
#include "ppmagic.h"
#include "udis86.h"
#include "vcall.h"

#include <gamedatadbg.gen.h>
#include <entpropsdbg.gen.h>

#ifdef _WIN32
usize dbg_toghidra(const void *addr) {
	const char *mod;
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
			GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (ushort *)addr,
			(HMODULE *)&mod)) {
		con_warn("dbg_toghidra: couldn't get base address\n");
		return 0;
	}
	return (const char *)addr - mod + 0x10000000;
}
#endif

void dbg_hexdump(const char *name, const void *p, int len) {
	struct rgba nice_colour = {160, 64, 200, 255}; // a nice purple colour
#ifdef _WIN32
	con_colourmsg(&nice_colour, "Hex dump \"%s\" (%p | %p):\n", name, p,
			(void *)dbg_toghidra(p));
#else
	con_colourmsg(&nice_colour, "Hex dump \"%s\" (%p):\n", name, p);
#endif
	for (const uchar *cp = p; cp - (uchar *)p < len; ++cp) {
		// group into words and wrap every 8 words
		switch ((cp - (uchar *)p) & 31) {
			case 0: con_colourmsg(&nice_colour, "\n"); break;
			CASES(4, 8, 12, 16, 20, 24, 28): con_colourmsg(&nice_colour, " ");
		}
		con_colourmsg(&nice_colour, "%02X ", *cp);
	}
	con_msg("\n");
}

void dbg_asmdump(const char *name, const void *p, int len) {
	struct rgba nice_colour = {40, 160, 140, 255}; // a nice teal colour
	struct ud udis;
	ud_init(&udis);
	ud_set_mode(&udis, 32);
	ud_set_input_buffer(&udis, p, len);
	ud_set_syntax(&udis, UD_SYN_INTEL);
#ifdef _WIN32
	con_colourmsg(&nice_colour, "Disassembly \"%s\" (%p | %p):\n", name, p,
			(void *)dbg_toghidra(p));
#else
	con_colourmsg(&nice_colour, "Disassembly \"%s\" (%p):\n", name, p);
#endif
	while (ud_disassemble(&udis)) {
		con_colourmsg(&nice_colour, "  %s\n", ud_insn_asm(&udis));
	}
}

DEF_CCMD_HERE(sst_dbg_getcmdcb, "Get the address of a command callback", 0) {
	if (cmd->argc != 2) {
		con_warn("usage: sst_dbg_getcmdcb command\n");
		return;
	}
	struct con_cmd *thecmd = con_findcmd(cmd->argv[1]);
	if (!thecmd) {
		errmsg_errorstd("couldn't find command %s\n", cmd->argv[1]);
		return;
	}
#ifdef _WIN32
	con_msg("addr: %p\nghidra: %p\n", (void *)thecmd->cb,
			(void *)dbg_toghidra((void *)thecmd->cb)); // ugh
#else
	con_msg("addr: %p\n", (void *)thecmd->cb);
#endif
}

DECL_VFUNC_DYN(struct IServerGameDLL, struct ServerClass *, GetAllServerClasses)
DEF_ARRAYIDX_ACCESSOR(struct SendProp, SendProp)
DEF_ACCESSORS(struct SendProp, const char *, SP_varname)
DEF_ACCESSORS(struct SendProp, int, SP_type)
DEF_ACCESSORS(struct SendProp, int, SP_offset)
DEF_ACCESSORS(struct SendProp, struct SendTable *, SP_subtable)

static void dumptable(struct SendTable *st, int indent) {
	for (int i = 0; i < st->nprops; ++i) {
		for (int i = 0; i < indent; i++) con_msg("  ");
		struct SendProp *p = arrayidx_SendProp(st->props, i);
		const char *name = get_SP_varname(p);
		if (get_SP_type(p) == DPT_DataTable) {
			struct SendTable *st = get_SP_subtable(p);
			if (!strcmp(name, "baseclass")) {
				con_msg("baseclass -> table %s (skipped)\n", st->tablename);
			}
			else {
				con_msg("%s -> subtable %s\n", name, st->tablename);
				dumptable(st, indent + 1);
			}
		}
		else {
			con_msg("%s -> offset %d\n", name, get_SP_offset(p));
		}
	}
}
DEF_CCMD_HERE(sst_dbg_sendtables, "Dump ServerClass/SendTable hierarchy", 0) {
	for (struct ServerClass *class = GetAllServerClasses(srvdll); class;
			class = class->next) {
		struct SendTable *st = class->table;
		con_msg("class %s (table %s)\n", class->name, st->tablename);
		dumptable(st, 1);
	}
}

DEF_CCMD_HERE(sst_dbg_gamedata, "Dump current gamedata values", 0) {
	dumpgamedata();
	dumpentprops();
}

// vi: sw=4 ts=4 noet tw=80 cc=80
