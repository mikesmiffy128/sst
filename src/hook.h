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

#ifndef INC_HOOK_H
#define INC_HOOK_H

#include "intdefs.h"
#include "errmsg.h"
#include "feature.h"
#include "langext.h"

bool hook_init();

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
 * Finds the correct function prologue location to install an inline hook, and
 * tries to initialise a trampoline with sufficient instructions and a jump back
 * to enable calling the original function.
 *
 * This is a low-level API and in most cases, if doing hooking from inside a
 * plugin feature, the hook_inline_featsetup() function should be used instead.
 * It automatically performs conventional error logging for both this step and
 * the hook_inline_mprot() call below, and returns error codes that are
 * convenient for use in a feature INIT function.
 *
 * When this function succeeds, the returned struct will have the prologue
 * member set to the prologue or starting point of the hooked function (which is
 * not always the same as the original function pointer). The trampoline
 * parameter, being a pointer-to-pointer, is an output parameter to which a
 * trampoline pointer will be written. The trampoline is a small run of
 * instructions from the original function, followed by a jump back to it,
 * allowing the original to be seamlessly called from a hook.
 *
 * In practically rare cases, this function will fail due to unsupported
 * instructions in the function prologue. In such instances, the returned struct
 * will have a null prologue, and the second member err, will point to a
 * null-terminated string for error logging. In this case, the trampoline
 * pointer will remain untouched.
 */
struct hook_inline_prep_ret {
	void *prologue;
	const char *err;
} hook_inline_prep(void *func, void **trampoline);

/*
 * This is a small helper function to make the memory page containing a
 * function's prologue writable, allowing an inline hook to be inserted with
 * hook_inline_commit().
 *
 * This is a low-level API and in most cases, if doing hooking from inside a
 * plugin feature, the hook_inline_featsetup() function should be used instead.
 * It automatically performs conventional error logging for both this step and
 * the prior hook_inline_prep() call documented above, and returns error codes
 * that are convenient for use in a feature INIT function.
 *
 * After using hook_inline_prep() to obtain the prologue and an appropriate
 * trampoline, call this to unlock the prologue, and then use
 * hook_inline_commit() to finalise the hook. In the event that multiple
 * functions need to be hooked at once, the commit calls can be batched up at
 * the end, removing the need for rollbacks since commitment is guaranteed to
 * succeed after all setup is complete.
 *
 * This function returns true on success, or false if a failure occurs at the
 * level of the OS memory protection API. os_lasterror() or errmsg_*sys() can be
 * used to report such an error.
 */
bool hook_inline_mprot(void *func);

/*
 * Finalises an inline hook set up using the hook_inline_prep() and
 * hook_inline_mprot() functions above (or the hook_inline_featsetup() helper
 * function below). prologue must be the prologue obtained via the
 * aforementioned functons and target must be the function that will be jumped
 * to in place of the original. It is very important that these functions are
 * ABI-compatible lest obvious bad things happen.
 *
 * The resulting hook can be removed later by calling unhook_inline().
 */
void hook_inline_commit(void *restrict prologue, void *restrict target);

/*
 * This is a helper specifically for use in feature INIT code. It doesn't make
 * much sense to call it elsewhere.
 *
 * Combines the functionality of the hook_inline_prep() and hook_inline_mprot()
 * functions above, logs to the console on error automatically in a conventional
 * format, and returns an error status that can be propagated straight from a
 * feature INIT function.
 *
 * func must point to the original function to be hooked, orig must point to
 * your trampoline pointer (which can in turn be used to call the original
 * function indirectly from within your hook or elsewhere), and fname should be
 * the name of the function for error logging purposes.
 *
 * If the err member of the returned struct is nonzero, simply return it as-is.
 * Otherwise, the prologue member will contain the prologue pointer to pass to
 * hook_inline_commit() to finalise the hook.
 */
static inline struct hook_inline_featsetup_ret {
	void *prologue;
	int err;
} hook_inline_featsetup(void *func, void **orig, const char *fname) {
	void *trampoline;
	struct hook_inline_prep_ret prep = hook_inline_prep(func, &trampoline);
	if_cold (prep.err) {
		errmsg_warnx("couldn't hook %s function: %s", fname, prep.err);
		return (struct hook_inline_featsetup_ret){0, FEAT_INCOMPAT};
	}
	if_cold (!hook_inline_mprot(prep.prologue)) {
		errmsg_errorsys("couldn't hook %s function: %s", fname,
				"couldn't make prologue writable");
		return (struct hook_inline_featsetup_ret){0, FEAT_FAIL};
	}
	*orig = trampoline;
	return (struct hook_inline_featsetup_ret){prep.prologue, 0};
}

/*
 * Reverts a function to its original unhooked state. Takes the pointer to the
 * callable "original" function, i.e. the trampoline, NOT the initial function
 * pointer from before hooking.
 */
void unhook_inline(void *orig);

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
