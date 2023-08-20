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

#ifndef INC_HOOK_H
#define INC_HOOK_H

#include "intdefs.h"

bool hook_init(void);

/*
 * Replaces a vtable entry with a target function and returns the original
 * function.
 */
static inline void *hook_vtable(void **vtable, usize off, void *target) {
	void *orig = vtable[off];
	vtable[off] = target;
	return orig;
}

/*
 * Puts an original function back after hooking.
 */
static inline void unhook_vtable(void **vtable, usize off, void *orig) {
	vtable[off] = orig;
}

/*
 * Returns a trampoline pointer, or null if hooking failed. Unlike hook_vtable,
 * handles memory protection for you.
 *
 * This function is not remotely thread-safe, and should never be called from
 * any thread besides the main one nor be used to hook anything that gets called
 * from other threads.
 */
void *hook_inline(void *func, void *target);

/*
 * Reverts the function to its original unhooked state. Takes the pointer to the
 * callable "original" function, i.e. the trampoline, NOT the initial function
 * pointer from before hooking.
 */
void unhook_inline(void *orig);

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
