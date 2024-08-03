/* This file is dedicated to the public domain. */

{.desc = "inline function hooking"};

#ifdef _WIN32

#include "../src/x86.c"
#include "../src/hook.c"
#include "../src/os.c"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// stubs
void con_warn(const char *msg, ...) {
	va_list l;
	va_start(l, msg);
	vfprintf(stderr, msg, l);
	va_end(l);
}

__attribute__((noinline))
static int some_function(int a, int b) { return a + b; }
static int (*orig_some_function)(int, int);
static int some_hook(int a, int b) {
	return orig_some_function(a, b) + 5;
}
__attribute__((noinline))
static int other_function(int a, int b) { return a - b; }
static int (*orig_other_function)(int, int);
static int other_hook(int a, int b) {
	return orig_other_function(a, b) + 5;
}

TEST("Inline hooks should be able to wrap the original function") {
	if (!hook_init()) return false;
	orig_some_function = hook_inline(&some_function, &some_hook);
	if (!orig_some_function) return false;
	return some_function(5, 5) == 15;
}

TEST("Inline hooks should be removable again") {
	if (!hook_init()) return false;
	orig_some_function = hook_inline(&some_function, &some_hook);
	if (!orig_some_function) return false;
	unhook_inline(orig_some_function);
	return some_function(5, 5) == 10;
}

TEST("Multiple functions should be able to be inline hooked at once") {
	if (!hook_init()) return false;
	orig_some_function = hook_inline(&some_function, &some_hook);
	if (!orig_some_function) return false;

	orig_other_function = hook_inline(&other_function, &other_hook);
	if (!orig_other_function) return false;

	return other_function(5, 5) == 5;
}

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
