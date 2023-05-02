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

#ifdef _WIN32
#include <Windows.h>
#endif

#include "con_.h"
#include "engineapi.h"
#include "intdefs.h"
#include "ppmagic.h"
#include "udis86.h"

void dbg_hexdump(char *name, const void *p, int len) {
	struct rgba nice_colour = {160, 64, 200, 255}; // a nice purple colour
	con_colourmsg(&nice_colour, "Hex dump \"%s\" (%p):", name, p);
	for (const uchar *cp = p; cp - (uchar *)p < len; ++cp) {
		// group into words and wrap every 8 words
		switch ((cp - (uchar *)p) & 31) {
			case 0: con_msg("\n"); break;
			CASES(4, 8, 12, 16, 20, 24, 28): con_msg(" ");
		}
		con_colourmsg(&nice_colour, "%02X ", *cp);
	}
	con_msg("\n");
}

void dbg_asmdump(char *name, const void *p, int len) {
	struct rgba nice_colour = {40, 160, 140, 255}; // a nice teal colour
	struct ud udis;
	ud_init(&udis);
	ud_set_mode(&udis, 32);
	ud_set_input_buffer(&udis, p, len);
	ud_set_syntax(&udis, UD_SYN_INTEL);
	con_colourmsg(&nice_colour, "Disassembly \"%s\" (%p)\n", name, p);
	while (ud_disassemble(&udis)) {
		con_colourmsg(&nice_colour, "  %s\n", ud_insn_asm(&udis));
	}
}

#ifdef _WIN32
usize dbg_toghidra(void *addr) {
	void *mod;
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
			GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (ushort *)addr,
			(HMODULE *)&mod/*please leave me alone*/)) {
		con_warn("dbg_toghidra: couldn't get base address\n");
		return 0;
	}
	return (char *)addr - (char *)mod + 0x10000000;
}
#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
