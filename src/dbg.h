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

#ifndef INC_DBG_H
#define INC_DBG_H

#include "intdefs.h"

/*
 * These functions can all be used for development and debugging but aren't
 * available to release builds; this header shouldn't even be #included in real
 * code that's committed to a repo.
 */

/* Prints out a basic hexadecimal listing of a byte range. */
void dbg_hexdump(const char *name, const void *p, int len);

/* Prints out a disassembly of some instructions in memory. */
void dbg_asmdump(const char *name, const void *p, int len);

#ifdef _WIN32 // at least for now
/* Returns a function's Ghidra address, assuming default project offsets. */
usize dbg_toghidra(const void *addr);
#endif

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
