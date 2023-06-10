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

#ifndef INC_MEMUTIL_H
#define INC_MEMUTIL_H

#include "intdefs.h"

/* Retrieves a 32-bit integer from an unaligned pointer. */
static inline u32 mem_load32(const void *p) {
	// XXX: Turns out the pedantically-safe approach below causes most compilers
	// to generate horribly braindead x86 output in at least some cases (and the
	// cases also differ by compiler). So, for now, use the simple pointer cast
	// instead, even though it's technically UB - it'll be fine probably...
	return *(u32 *)p;
	// For future reference, the pedantically-safe approach would be to do this:
	//const uchar *cp = p;
	//return (u32)cp[0] | (u32)cp[1] << 8 | (u32)cp[2] << 16 | (u32)cp[3] << 24;
}

/* Retrieves a 64-bit integer from an unaligned pointer. */
static inline u64 mem_load64(const void *p) {
	// this seems not to get butchered as badly in most cases?
	return (u64)mem_load32(p) | (u64)mem_load32((uchar *)p + 4) << 32;
}

/* Retrieves a pointer from an unaligned pointer-to-pointer. */
static inline void *mem_loadptr(const void *p) {
#if defined(_WIN64) || defined(__x86_64__)
	return (void *)mem_load64(p);
#else
	return (void *)mem_load32(p);
#endif
}

/* Retreives a signed offset from an unaligned pointer. */
static inline ssize mem_loadoffset(const void *p) {
	return (ssize)mem_loadptr(p);
}

/* Adds a byte count to a pointer and returns a freely-assignable pointer. */
static inline void *mem_offset(void *p, int off) { return (char *)p + off; }

/* Returns the offset in bytes from one pointer to another (p - q). */
static inline ssize mem_diff(const void *p, const void *q) {
	return (char *)p - (char *)q;
}

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
