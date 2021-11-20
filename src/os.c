/*
 * Copyright © 2021 Michael Smith <mikesmiffy128@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdbool.h>

#include "intdefs.h"
#include "os.h"

#ifdef _WIN32
// SystemFunction036 is the *real* name of "RtlGenRandom," and is also
// incorrectly defined in system headers. Yay, Windows.
int __stdcall SystemFunction036(void *buf, ulong sz);
#endif

bool os_mprot(void *addr, int len, int fl) {
#ifdef _WIN32
	ulong old;
	return !!VirtualProtect(addr, len, fl, &old);
#else
	// round down address and round up size
	addr = (void *)((ulong)addr & ~(4095));
	len = len + 4095 & ~(4095);
	return mprotect(addr, len, fl) != -1;
#endif
}

#ifdef _WIN32
void *os_dlsym(void *m, const char *s) {
	return (void *)GetProcAddress(m, s);
}
#endif

void os_randombytes(void *buf, int sz) {
	// if these calls ever fail, the system is fundamentally broken with no
	// recourse, so just loop until success. hopefully nothing will go wrong.
#ifdef _WIN32
	while (!SystemFunction036(buf, sz));
#else
	while (getentropy(buf, sz) == -1);
#endif
}

// vi: sw=4 ts=4 noet tw=80 cc=80
