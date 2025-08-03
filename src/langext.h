/* This file is dedicated to the public domain. */

#ifndef INC_LANGEXT_H
#define INC_LANGEXT_H

#include "intdefs.h"

#define ssizeof(x) ((ssize)sizeof(x))
#define countof(x) (ssizeof(x) / ssizeof(*x))

#undef unreachable // C23 stddef.h; prefer the non-function-like look of ours.

#if defined(__GNUC__) || defined(__clang__)
#define if_hot(x) if (__builtin_expect(!!(x), 1))
#define if_cold(x)  if (__builtin_expect(!!(x), 0))
#define if_random(x) if (__builtin_expect_with_probability(!!(x), 1, 0.5))
#define unreachable __builtin_unreachable()
#define assume(x) ((void)(!!(x) || (unreachable, 0)))
#define cold __attribute((__cold__, __noinline__))
#define asm_only __attribute((__naked__)) // N.B.: may not actually work in GCC?
#else
#define if_hot(x) if (x)
#define if_cold(x) if (x)
#define if_random(x) if (x)
#ifdef _MSC_VER
#define unreachable __assume(0)
#define assume(x) ((void)(__assume(x), 0))
#define cold __declspec(noinline)
#define asm_only __declspec(naked)
#else
static inline _Noreturn void _invoke_ub() {}
#define unreachable (_invoke_ub())
#define assume(x) ((void)(!!(x) || (_invoke_ub(), 0)))
#define cold
//#define asm_only // Can't use this without Clang/GCC/MSVC. Too bad.
#endif
#endif

#define switch_exhaust(x) switch (x) if (0) default: unreachable; else
#if defined(__GNUC__) || defined(__clang__)
#define switch_exhaust_enum(E, x) \
	_Pragma("GCC diagnostic push") \
	_Pragma("GCC diagnostic error \"-Wswitch-enum\"") \
	switch_exhaust ((enum E)(x)) \
	_Pragma("GCC diagnostic pop")
#else
// NOTE: pragma trick doesn't work in MSVC (the pop seems to happen before the
// switch is evaluated, so nothing happens) but you can still get errors using
// -we4061. This doesn't matter for sst but might come in handy elsewhere...
#define switch_exhaust_enum(E, x) switch_exhaust ((enum E)(x))
#endif

// could do [[noreturn]] in future, _Noreturn probably supports more compilers.
#define noreturn _Noreturn void

#ifdef _WIN32
#define import __declspec(dllimport) // only needed for variables
#define export __declspec(dllexport)
#else
#define import
#ifdef __GNUC__
// N.B. we assume -fvisibility=hidden
#define export __attribute((visibility("default"))
#else
#define export int exp[-!!"compiler needs a way to export symbols!"];
#endif
#endif

#ifdef __clang__
#define tailcall \
	/* Clang forces us to use void return and THEN warns about it ._. */ \
	_Pragma("clang diagnostic push") \
	_Pragma("clang diagnostic ignored \"-Wpedantic\"") \
	__attribute((musttail)) return \
	_Pragma("clang diagnostic pop")
#else
//#define tailcall // Can't use this without Clang.
#endif

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
