/* This file is dedicated to the public domain. */

{.desc = "inline function hooking"};

#ifdef _WIN32

#include "../src/udis86.c"
#include "../src/hook.c"

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

TEST("Inline hooks should be able to wrap the original function", 0) {
	orig_some_function = hook_inline(&some_function, &some_hook);
	if (!orig_some_function) return false;
	return some_function(5, 5) == 15;
}

TEST("Inline hooks should be removable again", 0) {
	orig_some_function = hook_inline(&some_function, &some_hook);
	if (!orig_some_function) return false;
	unhook_inline(orig_some_function);
	return some_function(5, 5) == 10;
}

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
