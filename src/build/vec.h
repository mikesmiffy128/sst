/* This file is dedicated to the public domain. */

#ifndef INC_VEC_H
#define INC_VEC_H

#include <errno.h>
#include <stdlib.h>

#include "../intdefs.h"

struct _vec {
	uint sz;
	uint max;
	void *data;
};

/*
 * A dynamic array with push, pop and concatenate operations.
 *
 * Usage: struct VEC(my_type) myvec = {0};
 * Or: struct myvec VEC(my_type);
 * Or: typedef struct VEC(my_type) myvec;
 */
#define VEC(type) { \
	uint sz; \
	uint max; \
	type *data; \
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((unused)) // heck off gcc
#endif
static bool _vec_ensure(struct _vec *v, uint tsize, uint newmax) {
	// FIXME: potential overflow at least on 32-bit hosts (if any!?).
	// should use reallocarray or something but didn't feel like porting right
	// now. consider doing later.
	void *new = realloc(v->data, tsize * newmax);
	if (new) { v->data = new; v->max = newmax; }
	return !!new;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((unused)) // heck off gcc 2
#endif
static bool _vec_make_room(struct _vec *v, uint tsize, uint addcnt) {
	// this overflow check is probably unnecessary, but just in case
	u64 chk = v->max + addcnt;
	if (chk > 1u << 30) { errno = ENOMEM; return false; }
	u32 x = chk;
	if (x < 16) {
		x = 16;
	}
	else {
		// round up to next 2*n
		--x;
		x |= x >> 1; x |= x >> 2; x |= x >> 4; x |= x >> 8; x |= x >> 16;
		x++;
	}
	return _vec_ensure(v, tsize, x);
}

// internal: for reuse by vec0
#define _vec_push(v, val, slack) ( \
	((v)->sz + (slack) < (v)->max || \
			_vec_make_room((struct _vec *)(v), sizeof(val), 1)) && \
	((v)->data[(v)->sz++ - slack] = (val), true) \
)

#define _vec_pushall(v, vals, n, slack) ( \
	((v)->sz + (n) + (slack) <= (v)->max || \
			_vec_make_room((struct _vec *)(v), sizeof(*(vals)), (n))) && \
	(memcpy((v)->data + (v)->sz - (slack), (vals), (n) * sizeof(*(vals))), \
			(v)->sz += (n), true) \
)

/*
 * Appends an item to the end of a vector. Gives true on success and false if
 * memory allocation fails.
 */
#define vec_push(v, val) _vec_push(v, val, 0)

/*
 * Appends n items from an array to the end of a vector. Gives true on success
 * and false if memory allocation fails.
 */
#define vec_pushall(v, vals, n) _vec_pushall(v, vals, n, 0)

/*
 * Removes an item from the end of a vector and gives that item.
 */
#define vec_pop(v) ((v)->data[--(v)->sz])

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
