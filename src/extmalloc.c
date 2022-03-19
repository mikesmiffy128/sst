/*
 * Copyright © 2021 Michael Smith <mikesmiffy128@gmail.com>
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

#include "intdefs.h"
#include "vcall.h"

// FIXME: this is duped from os.h because I don't want to pull in Windows.h,
// consider splitting out the IMPORT/EXPORT defs to some other thing?
#ifdef _WIN32
#define IMPORT __declspec(dllimport) // only needed for variables
#else
#define IMPORT
#endif

// XXX: not sure if "ext" is the best naming convention? use brain later

IMPORT void *g_pMemAlloc;

// this interface has changed a bit between versions but thankfully the basic
// functions we care about have always been at the start - nice and easy.
// unfortunately though, because the debug and non-debug versions are overloads
// and Microsoft are a bunch of crazies who decided vtable order should be
// affected by naming (overloads are grouped, and *reversed* inside of a
// group!?), we get this amusing ABI difference between platforms:
#ifdef _WIN32
DECL_VFUNC(void *, Alloc, 1, usize sz)
DECL_VFUNC(void *, Realloc, 3, void *mem, usize sz)
DECL_VFUNC(void, Free, 5, void *mem)
#else
DECL_VFUNC(void *, Alloc, 0, usize sz)
DECL_VFUNC(void *, Realloc, 1, void *mem, usize sz)
DECL_VFUNC(void, Free, 2, void *mem)
#endif

void *extmalloc(usize sz) {
	return VCALL(g_pMemAlloc, Alloc, sz);
}

void *extrealloc(void *mem, usize sz) {
	return VCALL(g_pMemAlloc, Realloc, mem, sz);
}

void extfree(void *mem) {
	VCALL(g_pMemAlloc, Free, mem);
}

// vi: sw=4 ts=4 noet tw=80 cc=80
