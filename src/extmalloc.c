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

#ifndef _WIN32
#include <stdlib.h>
#endif

#include "intdefs.h"
#include "vcall.h"

// XXX: not sure if "ext" is the best naming convention? use brain later

#ifdef _WIN32

__declspec(dllimport) void *g_pMemAlloc;

// this interface has changed a bit between versions but thankfully the basic
// functions we care about have always been at the start - nice and easy.
// note that since Microsoft are a bunch of crazies, overloads are grouped and
// reversed so the vtable order here is maybe not what you'd expect otherwise.
DECL_VFUNC(void *, Alloc, 1, usize)
DECL_VFUNC(void *, Realloc, 3, void *, usize)
DECL_VFUNC(void, Free, 5, void *)

void *extmalloc(usize sz) { return Alloc(g_pMemAlloc, sz); }
void *extrealloc(void *mem, usize sz) { return Realloc(g_pMemAlloc, mem, sz); }
void extfree(void *mem) { Free(g_pMemAlloc, mem); }

#else

void Error(const char *fmt, ...); // stub left out of con_.h (not that useful)

// Linux Source doesn't seem to bother with the custom allocator stuff at all.
// We still want to check for OoM though, because turning off overcommit is a
// right, not a privilege. Like func_vehicle.
void *extmalloc(usize sz) {
	void *ret = malloc(sz);
	if (!ret) Error("sst: out of memory");
	return ret;
}
void *extrealloc(void *mem, usize sz) {
	void *ret = realloc(mem, sz);
	if (!ret) Error("sst: out of memory");
	return ret;
}
// note: extfree is #defined to free in the header
#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
