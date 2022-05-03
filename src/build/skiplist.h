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

#ifndef INC_SKIPLIST_H
#define INC_SKIPLIST_H

#include <stdlib.h>

#include "../intdefs.h"
#include "../os.h"

#ifdef _WIN32
static inline int _skiplist_ffs(uint x) {
	uint ret;
	// on Windows, sizeof(ulong) == sizeof(uint)
	if (_BitScanForward((ulong *)&ret, x)) return ret + 1; else return 0;
}
#else
#include <strings.h>
#define _skiplist_ffs ffs
#endif

// WARNING: this is a really hacked-up version of the skiplist.h from cbits in
// order to support windows. It probably isn't a good idea to plop straight into
// your own use case.

#if defined(__GNUC__) || defined(__clang__)
#define _skiplist_unused __attribute__((unused)) // heck off gcc
#else
#define _skiplist_unused
#endif

// NOTE: using xoroshiro128++, a comparatively bad (i.e. non-cryptographic) prng
// for the sake of simplicity; original cbits skiplist.h relies on libcpoly to
// get arc4random() everywhere but since we're only using this at build time
// that seemed like a silly dependency to bother with.
//#define _skiplist_rng arc4random

// ALSO NOTE: the PRNG code here is *decidedly not* thread safe. again, this
// isn't a problem for our use case. just keep it in mind if reusing this header
// for something else. or ideally, don't reuse this header for something else...
static inline uvlong _skiplist_rotl(const uvlong x, int k) {
	return (x << k) | (x >> (64 - k));
}
_skiplist_unused static uvlong _skiplist_rng(void) {
	static uvlong s[2];
	static bool init = false;
	if (!init) { os_randombytes(s, sizeof(s)); init = true; }
	uvlong s0 = s[0], s1 = s[1];
	uvlong ret = _skiplist_rotl(s0 * 5, 7) * 9;
	s1 ^= s0;
	s[0] = _skiplist_rotl(s0, 24) ^ s1 ^ (s1 << 16);
	s[1] = _skiplist_rotl(s1, 37);
	return ret;
}

/*
 * Declares the skiplist header struct skiplist_hdr##name, but none of the
 * associated functions. Use when the structure needs to be passed around in
 * some way but actual operations on the list are a private implementation
 * detail. Otherwise, see DECL_SKIPLIST below.
 */
#define DECL_SKIPLIST_TYPE(name, dtype, ktype, levels) \
typedef dtype _skiplist_dt_##name; \
typedef ktype _skiplist_kt_##name; \
enum { skiplist_lvls_##name = (levels) }; \
struct skiplist_hdr_##name { dtype *x[levels]; };

/*
 * Declares the skiplist header struct skiplist_hdr_##name, with functions
 * skiplist_{get,del,pop,insert}_##name for operating on the list. A single
 * occurrence of DEF_SKIPLIST is required to actually implement the
 * functions.
 *
 * This macro implies DECL_SKIPLIST_TYPE (both should not be used).
 *
 * mod should be either static or extern.
 *
 * dtype should be the struct type that the skiplist header will be embedded in,
 * forming the linked structure.
 *
 * ktype should be the type of the struct member used for comparisons, for
 * example int or char *.
 *
 * levels should be the number of levels in each node. 4 is probably a
 * reasonable number, depending on the size of the structure and how many
 * entries need to be stored and looked up.
 *
 * The resulting get, del, pop and insert functions are hopefully self-
 * explanatory - get and del return the relevant node or a null pointer if
 * no such node is found.
 */
#define DECL_SKIPLIST(mod, name, dtype, ktype, levels) \
DECL_SKIPLIST_TYPE(name, dtype, ktype, levels) \
\
_skiplist_unused mod dtype *skiplist_get_##name(struct skiplist_hdr_##name *l, \
		ktype k); \
_skiplist_unused mod dtype *skiplist_del_##name(struct skiplist_hdr_##name *l, \
		ktype k); \
_skiplist_unused mod dtype *skiplist_pop_##name(struct skiplist_hdr_##name *l); \
_skiplist_unused mod void skiplist_insert_##name(struct skiplist_hdr_##name *l, \
		ktype k, dtype *node);

/*
 * Implements the functions corresponding to a skiplist - must come after
 * DECL_SKIPLIST with the same modifier and name.
 *
 * compfunc should be a function declared as follows (or an equivalent macro):
 * int cf(dtype *x, ktype y);
 *
 * hdrfunc should be a function declared as follows (or an equivalent macro):
 * struct skiplist_hdr_##name *hf(dtype *l);
 */
#define DEF_SKIPLIST(mod, name, compfunc, hdrfunc) \
static inline int _skiplist_lvl_##name(void) { \
	int i; \
	/* for 2 levels we get 1 50% of the time, 2 25% of the time, 0 25% of the
	   time. loop if 0 to distribute this evenly (this gets less likely the more
	   levels there are: at 4 levels, only loops 6% of the time) */ \
	while (!(i = _skiplist_ffs(_skiplist_rng() & \
			((1 << skiplist_lvls_##name) - 1)))); \
	/* ffs gives bit positions as 1-N but we actually want an array index */ \
	return i - 1; \
} \
\
_skiplist_unused \
mod _skiplist_dt_##name *skiplist_get_##name(struct skiplist_hdr_##name *l, \
		_skiplist_kt_##name k) { \
	for (int cmp, lvl = skiplist_lvls_##name - 1; lvl > -1; --lvl) { \
		while (l->x[lvl] && (cmp = compfunc(l->x[lvl], k)) < 0) { \
			l = hdrfunc(l->x[lvl]); \
		} \
		/* NOTE: cmp can be uninitialised here, but only if the list is
		   completely empty, in which case we'd return 0 anyway - so it doesn't
		   actually matter! */ \
		if (cmp == 0) return l->x[lvl]; \
	} \
	/* reached the end, no match */ \
	return 0; \
} \
\
_skiplist_unused \
_skiplist_dt_##name *skiplist_del_##name(struct skiplist_hdr_##name *l, \
		_skiplist_kt_##name k) { \
	_skiplist_dt_##name *ret = 0; \
	/* ALSO NOTE: in *this* case, cmp DOES need to be initialised to prevent a
	   possible null-deref via hdrfunc(l->x[lvl])->x */ \
	for (int cmp = 1, lvl = skiplist_lvls_##name - 1; lvl > -1; --lvl) { \
		while (l->x[lvl] && (cmp = compfunc(l->x[lvl], k)) < 0) { \
			l = hdrfunc(l->x[lvl]); \
		} \
		if (cmp == 0) { \
			ret = l->x[lvl]; \
			/* just shift each link by 1 */ \
			l->x[lvl] = hdrfunc(l->x[lvl])->x[0]; \
			/* ... and update every level of links via loop */ \
		} \
	} \
	/* reached the end, return whatever was found */ \
	return ret; \
} \
\
_skiplist_unused \
mod _skiplist_dt_##name *skiplist_pop_##name(struct skiplist_hdr_##name *l) { \
	_skiplist_dt_##name *cur = l->x[0]; \
	if (!cur) return 0; \
	l->x[0] = hdrfunc(cur)->x[0]; \
	for (int lvl = 1; lvl < skiplist_lvls_##name; ++lvl) { \
		if (l->x[lvl]) l->x[lvl] = hdrfunc(l->x[lvl])->x[lvl]; \
	} \
	return cur; \
} \
\
_skiplist_unused \
mod void skiplist_insert_##name(struct skiplist_hdr_##name *l, \
		_skiplist_kt_##name k, _skiplist_dt_##name *node) { \
	/* note: higher levels are unset but also skipped in other searches */ \
	int inslvl = _skiplist_lvl_##name(); \
	for (int lvl = skiplist_lvls_##name - 1; lvl > -1; --lvl) { \
		while (l->x[lvl] && compfunc(l->x[lvl], k) < 0) { \
			l = hdrfunc(l->x[lvl]); \
		} \
		if (lvl <= inslvl) { \
			hdrfunc(node)->x[lvl] = l->x[lvl]; \
			l->x[lvl] = node; \
		} \
	} \
} \

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
