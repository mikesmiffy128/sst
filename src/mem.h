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

#ifndef INC_MEMUTIL_H
#define INC_MEMUTIL_H

#ifdef SST_DBG
#include <string.h>
#endif

#include "intdefs.h"
#include "langext.h"

/* Retrieves an unsigned 32-bit integer from an unaligned pointer. */
static inline u32 mem_loadu32(const void *p) {
	// XXX: Turns out the pedantically-safe approach below causes most compilers
	// to generate horribly braindead x86 output in at least some cases (and the
	// cases also differ by compiler). So, for now, use the simple pointer cast
	// instead, even though it's technically UB - it'll be fine probably...
	return *(u32 *)p;
	// For future reference, the pedantically-safe approach would be to do this:
	//const uchar *cp = p;
	//return (u32)cp[0] | (u32)cp[1] << 8 | (u32)cp[2] << 16 | (u32)cp[3] << 24;
}

/* Writes an unsigned 32-bit integer to an unaligned pointer. */
static inline void mem_storeu32(const void *p, u32 x) {
	// Same idea as above.
	*(u32 *)p = x;
}

/* Retrieves a signed 32-bit integer from an unaligned pointer. */
static inline s32 mem_loads32(const void *p) { return (s32)mem_loadu32(p); }

/* Writes a signed 32-bit integer to an unaligned pointer. */
static inline void mem_stores32(const void *p, s32 x) { mem_storeu32(p, x); }

/* Retrieves an unsigned 64-bit integer from an unaligned pointer. */
static inline u64 mem_loadu64(const void *p) {
	// this seems not to get butchered as badly in most cases?
	return (u64)mem_loadu32(p) | (u64)mem_loadu32((uchar *)p + 4) << 32;
}

/* Writes an unsigned 64-bit integer to an unaligned pointer. */
static inline void mem_storeu64(const void *p, u64 x) {
	mem_storeu32(p, x);
	mem_storeu32((u32 *)p + 1, x >> 32);
}

/* Retrieves a signed 64-bit integer from an unaligned pointer. */
static inline s64 mem_loads64(const void *p) { return (s64)mem_loadu64(p); }

/* Writes a signed 64-bit integer to an unaligned pointer. */
static inline void mem_stores64(const void *p, s64 x) { mem_storeu64(p, x); }

/* Retrieves a pointer from an unaligned pointer-to-pointer. */
static inline void *mem_loadptr(const void *p) {
	if (sizeof(void *) == 8) return (void *)mem_loadu64(p);
	return (void *)(usize)mem_loadu32(p); // extra cast to prevent warning
}

/* Writes a pointer to an unaligned pointer-to-pointer. */
static inline void mem_storeptr(const void *p, const void *x) {
	if (sizeof(void *) == 8) mem_storeu64(p, (u64)x);
	else mem_storeu32(p, (u32)(usize)x); // extra cast to prevent warning
}

/* Retrieves a signed size/offset value from an unaligned pointer. */
static inline ssize mem_loadssize(const void *p) {
	return (ssize)mem_loadptr(p);
}

/* Writes a signed size/offset value to an unaligned pointer. */
static inline void mem_storessize(const void *p, ssize x) {
	mem_storeptr(p, (void *)x);
}

/* Retrieves an unsigned size or raw address value from an unaligned pointer. */
static inline usize mem_loadusize(const void *p) {
	return (usize)mem_loadptr(p);
}

/* Writes an unsigned size or raw address value to an unaligned pointer. */
static inline void mem_storeusize(const void *p, usize x) {
	mem_storeptr(p, (void *)x);
}

/* Adds a byte count to a pointer and returns a freely-assignable pointer. */
static inline void *mem_offset(const void *p, int off) { return (char *)p + off; }

/* Returns the offset in bytes from one pointer to another (p - q). */
static inline ssize mem_diff(const void *p, const void *q) {
	return (char *)p - (char *)q;
}

// Note: the following functions have ifdefs with fallbacks for MVSC and other
// compilers just in case that code is ever useful somewhere else, but generally
// the SST codebase can only be built using Clang.

/*
 * Equivalent to memcpy(), but explicitly generates the most efficient inline
 * `rep movsb` instruction rather than calling out to a library function.
 *
 * Should always be used instead of explicit memcpy() calls. Furthermore, the
 * compiler is being instructed not to generate automatic memcpy() calls, so it
 * is the programmer's judgement when to use this. For very small arrays or
 * structs, simply assigning values is likely faster.
 *
 * In debug builds, this just wraps memcpy anyway, to get the CRT debug checks.
 */
static forceinline void *mem_copy(void *restrict x, const void *restrict y,
		unsigned int sz) {
#if defined(SST_DBG)
	return memcpy(x, y, sz);
#elif defined(__GNUC__) || defined(__clang__)
	void *r = x;
	__asm volatile (
		"rep movsb\n"
		: "+D" (x), "+S" (y), "+c" (sz)
		:
		: "memory"
	);
	return r;
#elif defined(_MSC_VER)
	void __movsb(uchar *, uchar *, usize);
	__movsb((uchar *)x, (uchar *)y, sz);
	return x;
#else
	char *restrict xb = x; const char *restrict yb = y;
	for (unsigned int i = 0; i < sz; ++i) xb[i] = yb[i];
	return x;
#endif
}

/*
 * Equivalent to memset(), but explicitly generates the most efficient inline
 * `rep stosb` instruction rather than calling out to a library function.
 *
 * Should always be used instead of explicit memset() calls. Furthermore, the
 * compiler is being instructed not to generate automatic memset() calls, so it
 * is the programmer's judgement whether to use this on structs or small arrays.
 *
 * In debug builds, this just wraps memset anyway, to get the CRT debug checks.
 */
static forceinline void *mem_set(void *x, int c, unsigned int sz) {
#if defined(SST_DBG)
	return memset(x, c, sz);
#elif defined(__GNUC__) || defined(__clang__)
    void *r = x;
    __asm volatile (
        "rep stosb\n"
        : "+D" (x), "+c" (sz)
        : "a"(c)
        : "memory"
    );
    return r;
#elif defined(_MSC_VER)
	void __stosb(uchar *, uchar, usize);
	__stosb((uchar *)x, c, sz);
	return x;
#else
	const unsigned char *xb = x;
	for (unsigned int i = 0; i < len; ++i) xb[i] = (unsigned char)c;
	return x;
#endif
}

/*
 * Equivalent to memcmp(), but explicitly generates the most efficient inline
 * `rep cmpsb` instruction rather than calling out to a library function.
 *
 * Should always be used instead of explicit memcmp() calls. Furthermore, the
 * compiler is being instructed not to generate automatic memcmp() calls, so it
 * is the programmer's judgement whether to use this on structs or small arrays.
 *
 * In debug builds, this just wraps memcmp anyway, to get the CRT debug checks.
 */
static forceinline int mem_cmp(const void *restrict x, const void *restrict y,
		unsigned int sz) {
#if defined(SST_DBG)
	return memcmp(x, y, sz);
#elif defined(__GNUC__) || defined(__clang__)
	int a, b;
	__asm volatile (
		"xor eax, eax\n"
		"repz cmpsb\n"
		: "+D" (x), "+S" (y), "+c" (sz), "=@cca"(a), "=@ccb"(b)
		:
		: "ax", "memory"
	);
	return b - a;
#else // no msvc intrinsic for this apparently
	const char *x = x_, *y = y_;
	for (unsigned int i = 0; i < sz; ++i) {
		if (x[i] > y[i]) return 1;
		if (x[i] < y[i]) return -1;
	}
	return 0;
#endif
}

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
