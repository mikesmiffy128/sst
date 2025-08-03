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

#ifndef INC_ACCESSOR_H
#define INC_ACCESSOR_H

#include "intdefs.h"
#include "mem.h"

#if defined(__GNUC__) || defined(__clang__)
#define _ACCESSOR_UNUSED __attribute((unused))
#else
#define _ACCESSOR_UNUSED
#endif

/*
 * Defines a function to offset a pointer from a struct/class to a member based
 * on a corresponding offset value off_<field>. Such an offset would be
 * generally defined in gamedata. The function will be named getptr_<field>.
 * Essentially allows easy access to an opaque thing contained with another
 * opaque thing.
 */
#define DEF_PTR_ACCESSOR(class, type, member) \
	_ACCESSOR_UNUSED static inline typeof(type) *getptr_##member( \
			typeof(class) *obj) { \
		return mem_offset(obj, off_##member); \
	}

/*
 * Does the same as DEF_PTR_ACCESSOR, and also adds direct get/get functions.
 * Requires that the field type is complete - that is, either scalar or a fully
 * defined struct.
 */
#define DEF_ACCESSORS(class, type, member) \
	DEF_PTR_ACCESSOR(class, type, member) \
	_ACCESSOR_UNUSED static inline typeof(type) get_##member( \
			const typeof(class) *obj) { \
		return *getptr_##member((typeof(class) *)obj); \
	} \
	_ACCESSOR_UNUSED static inline void set_##member(typeof(class) *obj, \
			typeof(type) val) { \
		*getptr_##member(obj) = val; \
	}

/*
 * Defines an array indexing function arrayidx_<classname> which allows
 * offsetting an opaque pointer by sz_<classname> bytes. This size value would
 * generally be defined in gamedata. Allows iterating over structs/classes with
 * sizes that vary by game and are thus unknown at compile time.
 *
 * Note that idx is signed so this can also be used for relative pointer offsets
 * in either direction.
 *
 * Note on performance: obviously doing n * size requires a multiplication by a
 * global, which poses a bit of an optimisation challenge in loops. Based on
 * some testing though it seems like this *should* usually be optimised into a
 * single load of the global followed by repeated addition with no need for
 * multiplication, given that we use LTO, so... don't worry about it! It's fine!
 */
#define DEF_ARRAYIDX_ACCESSOR(type, classname) \
	_ACCESSOR_UNUSED static inline typeof(type) *arrayidx_##classname( \
			typeof(type) *array, ssize idx) { \
		return mem_offset(array, idx * sz_##classname); \
	}

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
