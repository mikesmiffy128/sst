/*
 * Copyright © 2021 Michael Smith <mikesmiffy128@gmail.com>
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

#ifndef INC_EXTMALLOC_H
#define INC_EXTMALLOC_H

#include "intdefs.h"

/*
 * These functions are just like malloc/realloc/free, but they call into
 * Valve's memory allocator wrapper, which ensures that allocations crossing
 * plugin/engine boundaries won't cause any weird issues.
 *
 * On Linux, there is no allocation wrapper, but these still do their own OoM
 * checking, so there's no need to think about that.
 */
void *extmalloc(usize sz);
void *extrealloc(void *mem, usize sz);
#ifdef _WIN32
void extfree(void *mem);
#else
void free(void *);
#define extfree free
#endif

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
