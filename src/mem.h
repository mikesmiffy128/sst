/*
 * Copyright © 2021 Michael Smith <mikesmiffy128@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
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

/* retrieves a 32-bit integer from an unaligned pointer; avoids UB, probably */
static inline u32 mem_load32(const void *p) {
	const uchar *cp = p;
	return (u32)cp[0] | (u32)cp[1] << 8 | (u32)cp[2] << 16 | (u32)cp[3] << 24;
}

/* retrieves a 64-bit integer from an unaligned pointer; avoids UB, possibly */
static inline u64 mem_load64(const void *p) {
	return (u64)mem_load32(p) | (u64)mem_load32((uchar *)p + 4) << 32;
}

/* retrieves a pointer from an unaligned pointer-to-pointer; avoids UB, maybe */
static inline void *mem_loadptr(const void *p) {
#if defined(_WIN64) || defined(__x86_64__)
	return (void *)mem_load64(p);
#else
	return (void *)mem_load32(p);
#endif
}

/* retreives a signed offset from an unaligned pointer; avoids UB, hopefully */
static inline ssize mem_loadoffset(const void *p) {
	return (ssize)mem_loadptr(p);
}

/* stores a 32-bit integer to an unaligned pointer; avoids UB, most likely */
static inline void mem_store32(void *p, u32 val) {
	uchar *cp = p;
	cp[0] = val; cp[1] = val >> 8; cp[2] = val >> 16; cp[3] = val >> 24;
}

/* stores a 64-bit integer to an unaligned pointer; avoids UB, I'd assume */
static inline void mem_store64(void *p, u64 val) {
	mem_store32(p, val); mem_store32((uchar *)p + 4, val >> 32);
}

/* stores a pointer value to an unaligned pointer; avoids UB, I guess */
static inline void mem_storeptr(void *to, const void *val) {
#if defined(_WIN64) || defined(__x86_64__)
	mem_store64(to, (u64)val);
#else
	mem_store32(to, (u32)val);
#endif
}

/* adds a byte count to a pointer, and returns something that can be assigned
 * to any pointer type */
static inline void *mem_offset(void *p, int off) { return (char *)p + off; }

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
