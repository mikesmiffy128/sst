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

#ifndef INC_X86UTIL_H
#define INC_X86UTIL_H

#include <stdbool.h>

#include "errmsg.h"
#include "x86.h"

// XXX: don't know where else to put this, or how else to design this, so this
// is very much a plonk-it-here-for-now scenario.

#define NEXT_INSN(p, tgt) do { \
	int _len = x86_len(p); \
	if (_len == -1) { \
		errmsg_errorx("unknown or invalid instruction looking for %s", tgt); \
		return false; \
	} \
	(p) += _len; \
} while (0)

#endif
