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

#ifndef INC_CHUNKLETS_FASTSPIN_H
#define INC_CHUNKLETS_FASTSPIN_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Raises an event through p to 0 or more callers of fastspin_wait().
 * val must be positive, and can be used to signal a specific condition.
 */
void fastspin_raise(volatile int *p, int val);

/*
 * Waits for an event to be raised by fastspin_raise(). Allows this and possibly
 * some other threads to wait for one other thread to signal its status.
 *
 * Returns the positive value that was passed to fastspin_raise().
 */
int fastspin_wait(volatile int *p);

/*
 * Takes a mutual exclusion, i.e. a lock. *p must be initialised to 0 before
 * anything starts using it as a lock.
 */
void fastspin_lock(volatile int *p);

/*
 * Releases a lock such that other threads may claim it. Immediately as a lock
 * is released, its value will be 0, as though it had just been initialised.
 */
void fastspin_unlock(volatile int *p);

#ifdef __cplusplus
}

/* An attempt to throw C++ users a bone. Should be self-explanatory. */
struct fastspin_lock_guard {
	fastspin_lock_guard(volatile int &i): _p(&i) { fastspin_lock(_p); }
	fastspin_lock_guard() = delete;
	~fastspin_lock_guard() { fastspin_unlock(_p); }
	volatile int *_p;
};

#endif

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
