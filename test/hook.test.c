/* This file is dedicated to the public domain. */

{.desc = "inline function hooking"};

#ifdef _WIN32

#include "../src/chunklets/x86.c"
#include "../src/hook.c"
#include "../src/os.c"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// stub
void con_warn(const char *msg, ...) {
	va_list l;
	va_start(l, msg);
	vfprintf(stderr, msg, l);
	va_end(l);
}

typedef int (*testfunc)(int, int);

__attribute__((noinline)) static int func1(int a, int b) { return a + b; }
static int (*orig_func1)(int, int);
static int hook1(int a, int b) { return orig_func1(a, b) + 5; }

__attribute__((noinline)) static int func2(int a, int b) { return a - b; }
static int (*orig_func2)(int, int);
static int hook2(int a, int b) { return orig_func2(a, b) + 5; }

// basic reimplementation of old API to support existing test cases.
// XXX: we could probably have tests at the boundaries of the new API too,
// although the current tests are only testing for regressions in x86 jmp logic.
static inline void *test_hook_inline(void *func, void *target) {
	void *trampoline;
	struct hook_inline_prep_ret prep = hook_inline_prep(func, &trampoline);
	if (prep.err) return 0;
	if (!hook_inline_mprot(prep.prologue)) return 0;
	hook_inline_commit(prep.prologue, target);
	return trampoline;
}

TEST("Inline hooks should be able to wrap the original function") {
	if (!hook_init()) return false;
	orig_func1 = (testfunc)test_hook_inline((void *)&func1, (void *)&hook1);
	if (!orig_func1) return false;
	return func1(5, 5) == 15;
}

TEST("Inline hooks should be removable again") {
	if (!hook_init()) return false;
	orig_func1 = (testfunc)test_hook_inline((void *)&func1, (void *)&hook1);
	if (!orig_func1) return false;
	unhook_inline((void *)orig_func1);
	return func1(5, 5) == 10;
}

TEST("Multiple functions should be able to be inline-hooked at once") {
	if (!hook_init()) return false;
	orig_func1 = (testfunc)test_hook_inline((void *)&func1, (void *)&hook1);
	if (!orig_func1) return false;
	orig_func2 = (testfunc)test_hook_inline((void *)&func2, (void *)&hook2);
	if (!orig_func2) return false;
	return func2(5, 5) == 5;
}

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
