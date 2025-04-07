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

#ifndef INC_BITBUF_H
#define INC_BITBUF_H

#include "intdefs.h"

// NOTE: This code is not big-endian-safe, because the game itself is little-
// endian. This could theoretically break tests in odd cross-compile scenarios,
// but no tests currently look at actual bit values so it's fine for now.

// handle one machine word at a time (SIMD is probably not worth it... yet?)
typedef usize bitbuf_cell;
static const int bitbuf_cell_bits = sizeof(bitbuf_cell) * 8;
static const int bitbuf_align = _Alignof(bitbuf_cell);

/* A bit buffer, ABI-compatible with bf_write defined in tier1/bitbuf.h */
struct bitbuf {
	union {
		char *buf; /* NOTE: the buffer SHOULD be aligned as bitbuf_cell! */
		bitbuf_cell *cells;
	};
	int sz, nbits;
	uint curbit; // made unsigned so divisions can become shifts (hopefully...)
	bool overflow, assert_on_overflow;
	const char *debugname;
};

// detail: need a cell internally, but API users shouldn't rely on 64-bit size
static inline void _bitbuf_append(struct bitbuf *bb, bitbuf_cell x, int nbits) {
	int idx = bb->curbit / bitbuf_cell_bits;
	int shift = bb->curbit % bitbuf_cell_bits;
	// OR into the existing cell (lower bits were already set!)
	bb->cells[idx] |= x << shift;
	// assign the next cell (that also clears the upper bits for the next OR)
	// if nbits fits in the first cell, this zeros the next cell, which is fine
	bb->cells[idx + 1] = x >> (bitbuf_cell_bits - shift);
	bb->curbit += nbits;
}

/* Appends a value to the bit buffer, with a specfied length in bits. */
static inline void bitbuf_appendbits(struct bitbuf *bb, uint x, int nbits) {
	_bitbuf_append(bb, x, nbits);
}

/* Appends a byte to the bit buffer. */
static inline void bitbuf_appendbyte(struct bitbuf *bb, uchar x) {
	_bitbuf_append(bb, x, 8);
}

/* Appends a sequence of bytes to the bit buffer, with length given in bytes. */
static inline void bitbuf_appendbuf(struct bitbuf *bb, const char *buf,
		uint len) {
	// NOTE! This function takes advantage of the fact that nothing unaligned
	// is page aligned, so accessing slightly outside the bounds of buf can't
	// segfault. This is absolutely definitely technically UB, but it's unit
	// tested and apparently works in practice. If something weird happens
	// further down the line, sorry!
	usize unalign = (usize)buf & (bitbuf_align - 1);
	if (unalign) {
		// round down the pointer
		bitbuf_cell *p = (bitbuf_cell *)((usize)buf - unalign);
		// shift the stored value (if it were big endian, the shift would have
		// to be the other way, or something)
		_bitbuf_append(bb, *p >> (unalign << 3), (bitbuf_align - unalign) << 3);
		buf += (int)sizeof(bitbuf_cell) - unalign;
		len -= unalign;
	}
	bitbuf_cell *aligned = (bitbuf_cell *)buf;
	for (; len >= (int)sizeof(bitbuf_cell); len -= (int)sizeof(bitbuf_cell),
			++aligned) {
		_bitbuf_append(bb, *aligned, bitbuf_cell_bits);
	}
	// unaligned end bytes
	_bitbuf_append(bb, *aligned, len << 3);
}

/* 0-pad the bit buffer up to the next whole byte boundary. */
static inline void bitbuf_roundup(struct bitbuf *bb) {
	bb->curbit += -(uint)bb->curbit & 7;
}

/* Clear the bit buffer to make it ready to append new data. */
static inline void bitbuf_reset(struct bitbuf *bb) {
	bb->buf[0] = 0; // we have to zero out the lowest cell since it gets ORed
	bb->curbit = 0;
}

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
