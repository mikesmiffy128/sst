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

#ifndef INC_PPMAGIC_H
#define INC_PPMAGIC_H

/* random preprocessor shenanigans */

#define _PPMAGIC_DO02(m, sep, x, y) m(x) sep m(y)
#define _PPMAGIC_DO03(m, sep, x, y, z) m(x) sep m(y) sep m(z)
#define _PPMAGIC_DO04(m, sep, w, x, y, z) m(w) sep m(x) sep m(y) sep m(z)
#define _PPMAGIC_DO05(m, sep, x, ...) m(x) sep _PPMAGIC_DO04(m, sep, __VA_ARGS__)
// repetitive nonsense {{{
#define _PPMAGIC_DO06(m, sep, x, ...) m(x) sep _PPMAGIC_DO05(m, sep, __VA_ARGS__)
#define _PPMAGIC_DO07(m, sep, x, ...) m(x) sep _PPMAGIC_DO06(m, sep, __VA_ARGS__)
#define _PPMAGIC_DO08(m, sep, x, ...) m(x) sep _PPMAGIC_DO07(m, sep, __VA_ARGS__)
#define _PPMAGIC_DO09(m, sep, x, ...) m(x) sep _PPMAGIC_DO08(m, sep, __VA_ARGS__)
#define _PPMAGIC_DO10(m, sep, x, ...) m(x) sep _PPMAGIC_DO09(m, sep, __VA_ARGS__)
#define _PPMAGIC_DO11(m, sep, x, ...) m(x) sep _PPMAGIC_DO10(m, sep, __VA_ARGS__)
#define _PPMAGIC_DO12(m, sep, x, ...) m(x) sep _PPMAGIC_DO11(m, sep, __VA_ARGS__)
#define _PPMAGIC_DO13(m, sep, x, ...) m(x) sep _PPMAGIC_DO12(m, sep, __VA_ARGS__)
#define _PPMAGIC_DO14(m, sep, x, ...) m(x) sep _PPMAGIC_DO13(m, sep, __VA_ARGS__)
#define _PPMAGIC_DO15(m, sep, x, ...) m(x) sep _PPMAGIC_DO14(m, sep, __VA_ARGS__)
#define _PPMAGIC_DO16(m, sep, x, ...) m(x) sep _PPMAGIC_DO15(m, sep, __VA_ARGS__)
#define _PPMAGIC_DO17(m, sep, x, ...) m(x) sep _PPMAGIC_DO16(m, sep, __VA_ARGS__)
#define _PPMAGIC_DO18(m, sep, x, ...) m(x) sep _PPMAGIC_DO17(m, sep, __VA_ARGS__)
#define _PPMAGIC_DO19(m, sep, x, ...) m(x) sep _PPMAGIC_DO18(m, sep, __VA_ARGS__)
#define _PPMAGIC_DO20(m, sep, x, ...) m(x) sep _PPMAGIC_DO19(m, sep, __VA_ARGS__)
#define _PPMAGIC_DO21(m, sep, x, ...) m(x) sep _PPMAGIC_DO20(m, sep, __VA_ARGS__)
#define _PPMAGIC_DO22(m, sep, x, ...) m(x) sep _PPMAGIC_DO21(m, sep, __VA_ARGS__)
#define _PPMAGIC_DO23(m, sep, x, ...) m(x) sep _PPMAGIC_DO22(m, sep, __VA_ARGS__)
#define _PPMAGIC_DO24(m, sep, x, ...) m(x) sep _PPMAGIC_DO23(m, sep, __VA_ARGS__)
#define _PPMAGIC_DO25(m, sep, x, ...) m(x) sep _PPMAGIC_DO24(m, sep, __VA_ARGS__)
#define _PPMAGIC_DO26(m, sep, x, ...) m(x) sep _PPMAGIC_DO25(m, sep, __VA_ARGS__)
#define _PPMAGIC_DO27(m, sep, x, ...) m(x) sep _PPMAGIC_DO26(m, sep, __VA_ARGS__)
#define _PPMAGIC_DO28(m, sep, x, ...) m(x) sep _PPMAGIC_DO27(m, sep, __VA_ARGS__)
#define _PPMAGIC_DO29(m, sep, x, ...) m(x) sep _PPMAGIC_DO28(m, sep, __VA_ARGS__)
#define _PPMAGIC_DO30(m, sep, x, ...) m(x) sep _PPMAGIC_DO29(m, sep, __VA_ARGS__)
#define _PPMAGIC_DO31(m, sep, x, ...) m(x) sep _PPMAGIC_DO30(m, sep, __VA_ARGS__)
#define _PPMAGIC_DO32(m, sep, x, ...) m(x) sep _PPMAGIC_DO31(m, sep, __VA_ARGS__)
// }}}

#define _PPMAGIC_DO_N( \
x01, x02, x03, x04, x05, x06, x07, x08, x09, x10, x11, x12, x13, x14, x15, x16, \
x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, x29, x30, x31, x32, \
		N, ...) \
	_PPMAGIC_DO##N

/*
 * applies the given single-argument macro m to each of a list of up to 32
 * parameters, with the optional token sep inserted in between.
 */
#define PPMAGIC_MAP(m, sep, ...) \
	_PPMAGIC_DO_N(__VA_ARGS__, \
		32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, \
		16, 15, 14, 13, 12, 11, 10, 09, 08, 07, 06, 05, 04, 03, 02, 01) \
	(m, sep, __VA_ARGS__)

/* expands to up to 32 case labels at once, for matching multiple values */
#define CASES(...) PPMAGIC_MAP(case, :, __VA_ARGS__)

#define _PPMAGIC_0x(n) 0x##n,
/* expands to a byte array with each digit prefixed with 0x */
#define HEXBYTES(...) {PPMAGIC_MAP(_PPMAGIC_0x, , __VA_ARGS__)}

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80 fdm=marker
