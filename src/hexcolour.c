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

#include <string.h>

#include "intdefs.h"

void hexcolour_rgb(uchar out[static 4], const char *s) {
	const char *p = s;
	for (uchar *q = out; q - out < 3; ++q) {
		if (*p >= '0' && *p <= '9') {
			*q = *p++ - '0' << 4;
		}
		else if ((*p | 32) >= 'a' && (*p | 32) <= 'f') {
			*q = 10 + (*p++ | 32) - 'a' << 4;
		}
		else {
			// screw it, just fall back on white, I guess.
			// note: this also handles *p == '\0' so we don't overrun the string
			memset(out, 255, 4); // should be a single mov
			return;
		}
		// repetitive unrolled nonsense
		if (*p >= '0' && *p <= '9') {
			*q |= *p++ - '0';
		}
		else if ((*p | 32) >= 'a' && (*p | 32) <= 'f') {
			*q |= 10 + (*p++ | 32) - 'a';
		}
		else {
			memset(out, 255, 4); // should be a single mov
			return;
		}
	}
	//out[3] = 255; // never changes!
}

void hexcolour_rgba(uchar out[static 4], const char *s) {
	const char *p = s;
	// same again but with 4 pairs of digits instead of 3!
	for (uchar *q = out; q - out < 4; ++q) {
		if (*p >= '0' && *p <= '9') {
			*q = *p++ - '0' << 4;
		}
		else if ((*p | 32) >= 'a' && (*p | 32) <= 'f') {
			*q = 10 + (*p++ | 32) - 'a' << 4;
		}
		else {
			if (q - out == 3 && !*p) {
				// ONLY if both the last alpha digits are missing: use provided
				// RGB at full opacity. otherwise same white fallback as before.
				out[3] = 255;
				return;
			}
			memset(out, 255, 4);
			return;
		}
		// even more repetitive unrolled nonsense
		if (*p >= '0' && *p <= '9') {
			*q |= *p++ - '0';
		}
		else if ((*p | 32) >= 'a' && (*p | 32) <= 'f') {
			*q |= 10 + (*p++ | 32) - 'a';
		}
		else {
			memset(out, 255, 4);
			return;
		}
	}
}

// vi: sw=4 ts=4 noet tw=80 cc=80
