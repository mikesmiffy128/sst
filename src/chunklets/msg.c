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

#ifdef __cplusplus
#error This file should not be compiled as C++. It relies on C-specific union \
behaviour which is undefined in C++.
#endif

// _Static_assert needs MSVC >= 2019, and this check is irrelevant on Windows
#ifndef _MSC_VER
_Static_assert(
	(unsigned char)-1 == 255 &&
	sizeof(short) == 2 &&
	sizeof(int) == 4 &&
	sizeof(long long) == 8 &&
	sizeof(float) == 4 &&
	sizeof(double) == 8,
	"this code is only designed for relatively sane environments, plus Windows"
);
#endif

#include "msg.h"

// -- A note on performance hackery --
//
// Clang won't emit byte-swapping instructions in place of bytewise array writes
// unless nothing else is written to the same array. MSVC won't do it at all.
// For these compilers on little-endian platforms that can also do unaligned
// writes efficiently, we do so explicitly and handle the byte-swapping
// manually, which then tends to get optimised pretty well.
//
// GCC, somewhat surprisingly, seems to be much better at optimising the naïve
// version of the code, so we don't try to do anything clever there. Also, for
// unknown, untested compilers and/or platforms, we stick to the safe approach.
#if defined(_MSC_VER) || defined(__clang__) && (defined(__x86_64__) || \
		defined(__i386__) || defined(__aarch64__) || defined(__arm__))
#define USE_BSWAP_NONSENSE
#endif

#ifdef USE_BSWAP_NONSENSE
#if defined(_MSC_VER) && !defined(__clang__)
// MSVC prior to 2022 won't even optimise shift/mask swaps into a bswap
// instruction. Screw it, just use the intrinsics.
unsigned long _byteswap_ulong(unsigned long);
unsigned long long _byteswap_uint64(unsigned long long);
#define swap32 _byteswap_ulong
#define swap64 _byteswap_uint64
#else
static inline unsigned int swap32(unsigned int x) {
    return x >> 24 | x << 24 | x >> 8 & 0xFF00 | x << 8 & 0xFF0000;
}
static inline unsigned long long swap64(unsigned long long x) {
	return	x >> 56              | x << 56                    |
			x >> 40 &     0xFF00 | x << 40 & 0xFF000000000000 |
			x >> 24 &   0xFF0000 | x << 24 &   0xFF0000000000 |
			x >>  8 & 0xFF000000 | x <<  8 &     0xFF00000000;
}
#endif
#endif

static inline void doput16(unsigned char *out, unsigned char tag,
		unsigned short val) {
	out[0] = tag;
#ifdef USE_BSWAP_NONSENSE
	// Use swap32() here because x86 and ARM don't have instructions for 16-bit
	// swaps, and Clang doesn't realise it could just use the 32-bit one anyway.
	*(unsigned short *)(out + 1) = swap32(val) >> 16;
#else
	out[1] = val >> 8; out[2] = val;
#endif
}

static inline void doput32(unsigned char *out, unsigned char tag,
		unsigned int val) {
	out[0] = tag;
#ifdef USE_BSWAP_NONSENSE
	*(unsigned int *)(out + 1) = swap32(val);
#else
	out[1] = val >> 24; out[2] = val >> 16; out[3] = val >> 8; out[4] = val;
#endif
}

static inline void doput64(unsigned char *out, unsigned char tag,
		unsigned int val) {
	out[0] = tag;
#ifdef USE_BSWAP_NONSENSE
	// Clang is smart enough to make this into two bswaps and a word swap in
	// 32-bit builds. MSVC seems to be fine too when using the above intrinsics.
	*(unsigned long long *)(out + 1) = swap64(val);
#else
	out[1] = val >> 56; out[2] = val >> 48;
	out[3] = val >> 40; out[4] = val >> 32;
	out[5] = val >> 24; out[6] = val >> 16;
	out[7] = val >>  8; out[8] = val;
#endif
}

int msg_puts8(unsigned char *out, signed char val) {
	out[0] = 0xD0;
	int off = val < -32; // out of -ve fixnum range?
	out[off] = val;
	return off + 1;
}

int msg_rputs8(unsigned char *end, signed char val) {
	int off = val < -32;
	end[-1 - off] = 0xD0;
	end[-1] = val;
	return off + 1;
}

int msg_putu8(unsigned char *out, unsigned char val) {
	out[0] = 0xCC;
	int off = val > 127; // out of +ve fixnum range?
	out[off] = val;
	return off + 1;
}

int msg_rputu8(unsigned char *end, unsigned char val) {
	int off = val > 127;
	end[-1 - off] = 0xCC;
	end[-1] = val;
	return off + 1;
}

int msg_puts16(unsigned char *out, short val) {
	if (val >= -128 && val <= 127) return msg_puts8(out, val);
	doput16(out, 0xD1, val);
	return 3;
}

int msg_rputs16(unsigned char *end, short val) {
	if (val >= -128 && val <= 127) return msg_rputs8(end, val);
	doput16(end - 3, 0xD1, val);
	return 3;
}

int msg_putu16(unsigned char *out, unsigned short val) {
	if (val <= 255) return msg_putu8(out, val);
	doput16(out, 0xCD, val);
	return 3;
}

int msg_rputu16(unsigned char *end, unsigned short val) {
	if (val <= 255) return msg_rputu8(end, val);
	doput16(end - 3, 0xCD, val);
	return 3;
}

int msg_puts32(unsigned char *out, int val) {
	if (val >= -32768 && val <= 32767) return msg_puts16(out, val);
	doput32(out, 0xD2, val);
	return 5;
}

int msg_rputs32(unsigned char *end, int val) {
	if (val >= -32768 && val <= 32767) return msg_rputs16(end, val);
	doput32(end - 5, 0xD2, val);
	return 5;
}

int msg_putu32(unsigned char *out, unsigned int val) {
	if (val <= 65536) return msg_putu16(out, val);
	doput32(out, 0xCE, val);
	return 5;
}

int msg_rputu32(unsigned char *end, unsigned int val) {
	if (val <= 65536) return msg_rputu16(end, val);
	doput32(end - 5, 0xCE, val);
	return 5;
}

int msg_puts(unsigned char *out, long long val) {
	if (val >= -2147483648 && val <= 2147483647) return msg_puts32(out, val);
	doput64(out, 0xD3, val);
	return 9;
}

int msg_rputs(unsigned char *end, long long val) {
	if (val >= -2147483648 && val <= 2147483647) return msg_rputs32(end, val);
	doput64(end - 9, 0xD3, val);
	return 9;
}

int msg_putu(unsigned char *out, unsigned long long val) {
	if (val <= 4294967295) return msg_putu32(out, val);
	doput64(out, 0xCF, val);
	return 9;
}

int msg_rputu(unsigned char *end, unsigned long long val) {
	if (val <= 4294967295) return msg_rputu32(end, val);
	doput64(end - 9, 0xCF, val);
	return 9;
}

static inline unsigned int floatbits(float f) {
	return (union { float f; unsigned int i; }){f}.i;
}

static inline unsigned long long doublebits(double d) {
	return (union { double d; unsigned long long i; }){d}.i;
}

void msg_putf(unsigned char *out, float val) {
	doput32(out, 0xCA, floatbits(val));
}

int msg_putd(unsigned char *out, double val) {
	// XXX: is this really the most efficient way to check this?
	float f = val;
	if ((double)f == val) { msg_putf(out, f); return 5; }
	doput64(out, 0xCA, doublebits(val)); return 9;
}

int msg_rputd(unsigned char *end, double val) {
	float f = val;
	if ((double)f == val) { msg_rputf(end, f); return 5; }
	doput64(end - 9, 0xCA, doublebits(val)); return 9;
}

int msg_putssz8(unsigned char *out, int sz) {
	if (sz < 32) { msg_putssz5(out, sz); return 1; }
	out[0] = 0xD9; out[1] = sz; return 2;
}

int msg_rputssz8(unsigned char *end, int sz) {
	if (sz < 32) { msg_putssz5(end - 1, sz); return 1; }
	end[-2] = 0xD9; end[-1] = sz; return 2;
}

int msg_putssz16(unsigned char *out, int val) {
	if (val <= 255) return msg_putssz8(out, val);
	doput16(out, 0xDA, val);
	return 3;
}

int msg_rputssz16(unsigned char *end, int val) {
	if (val <= 255) return msg_rputssz8(end, val);
	doput16(end - 3, 0xDA, val);
	return 3;
}

int msg_putssz(unsigned char *out, unsigned int val) {
	if (val <= 65535) return msg_putssz16(out, val);
	doput32(out, 0xDB, val);
	return (32) / 8 + 1;
}
int msg_rputssz(unsigned char *end, unsigned int val) {
	if (val <= 65535) return msg_rputssz16(end, val);
	doput32(end - (32) / 8 - 1, 0xDB, val);
	return (32) / 8 + 1;
}

int msg_putbsz16(unsigned char *out, int val) {
	if (val <= 255) { msg_putbsz8(out, val); return 2; }
	doput16(out, 0xC5, val);
	return 3;
}

int msg_rputbsz16(unsigned char *end, int val) {
	if (val <= 255) { msg_rputbsz8(end, val); return 2; }
	doput16(end - 3, 0xC5, val);
	return 3;
}

int msg_putbsz(unsigned char *out, unsigned int val) {
	if (val <= 65535) return msg_putbsz16(out, val);
	doput32(out, 0xC6, val);
	return 5;
}

int msg_rputbsz(unsigned char *end, unsigned int val) {
	if (val <= 65535) return msg_rputbsz16(end, val);
	doput32(end - 5, 0xC6, val);
	return 5;
}

int msg_putasz16(unsigned char *out, int val) {
	if (val <= 15) { msg_putasz4(out, val); return 1; }
	doput16(out, 0xDC, val);
	return 3;
}

int msg_rputasz16(unsigned char *end, int val) {
	if (val <= 15) { msg_rputasz4(end, val); return 1; }
	doput16(end - 3, 0xDC, val);
	return 3;
}

int msg_putasz(unsigned char *out, unsigned int val) {
	if (val <= 65535) return msg_putasz16(out, val);
	doput32(out, 0xDD, val);
	return 5;
}

int msg_rputasz(unsigned char *end, unsigned int val) {
	if (val <= 65535) return msg_rputasz16(end, val);
	doput32(end - 5, 0xDD, val);
	return 5;
}

int msg_putmsz16(unsigned char *out, int val) {
	if (val <= 15) { msg_putmsz4(out, val); return 1; }
	doput16(out, 0xDE, val);
	return 3;
}

int msg_rputmsz16(unsigned char *end, int val) {
	if (val <= 15) { msg_rputmsz4(end, val); return 1; }
	doput16(end - 3, 0xDE, val);
	return 3;
}

int msg_putmsz(unsigned char *out, unsigned int val) {
	if (val <= 65535) return msg_putmsz16(out, val);
	doput32(out, 0xDF, val);
	return 5;
}

int msg_rputmsz(unsigned char *end, unsigned int val) {
	if (val <= 65535) return msg_rputmsz16(end, val);
	doput32(end - 5, 0xDF, val);
	return 5;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
