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

#ifndef INC_BITBUF_H
#define INC_BITBUF_H

#include "intdefs.h"

// NOTE: This code assumes it's running on a little endian machine, because,
// well, the game runs on a little endian machine.
// *technically* this could break unit tests in a contrived cross-compile
// scenario? right now none of the tests care about actual bit values, and we
// don't cross compile, so this won't matter till later. :)

// handle 8 bytes at a time (COULD do 16 with SSE, but who cares this is fine)
typedef uvlong bitbuf_cell;
static const int bitbuf_cell_bits = sizeof(bitbuf_cell) * 8;
static const int bitbuf_align = _Alignof(bitbuf_cell);

/* A bit buffer, ABI-compatible with bf_write defined in tier1/bitbuf.h */
struct bitbuf {
	union {
		char *buf; /* NOTE: the buffer MUST be aligned as bitbuf_cell! */
		bitbuf_cell *buf_as_cells;
	};
	int sz, nbits, curbit;
	bool overflow, assert_on_overflow;
	const char *debugname;
};

/* Append a value to the bitbuffer, with a specfied length in bits. */
static inline void bitbuf_appendbits(struct bitbuf *bb, bitbuf_cell x,
		int nbits) {
	int idx = bb->curbit / bitbuf_cell_bits;
	int shift = bb->curbit % bitbuf_cell_bits;
	// OR into the existing cell (lower bits were already set!)
	bb->buf_as_cells[idx] |= x << shift;
	// assign the next cell (that also clears the upper bits for the next OR)
	// note: if nbits fits in the first cell, this just 0s the next cell, which
	// is absolutely fine
	bb->buf_as_cells[idx + 1] = x >> (bitbuf_cell_bits - shift);
	bb->curbit += nbits;
}

/* Append a byte to the bitbuffer - same as appendbits(8) but more convenient */
static inline void bitbuf_appendbyte(struct bitbuf *bb, uchar x) {
	bitbuf_appendbits(bb, x, 8);
}

/* Append a sequence of bytes to the bitbuffer, with length given in bytes */
static inline void bitbuf_appendbuf(struct bitbuf *bb, const char *buf,
		uint len) {
	// NOTE! This function takes advantage of the fact that nothing unaligned
	// is page aligned, so accessing slightly outside the bounds of buf can't
	// segfault. This is absolutely definitely technically UB, but it's unit
	// tested and apparently works in practice. If something weird happens
	// further down the line, sorry!
	usize unalign = (usize)buf % bitbuf_align;
	if (unalign) {
		// round down the pointer
		bitbuf_cell *p = (bitbuf_cell *)((usize)buf & ~(bitbuf_align - 1));
		// shift the stored value (if it were big endian, the shift would have
		// to be the other way, or something)
		bitbuf_appendbits(bb, *p >> (unalign * 8), (bitbuf_align - unalign) * 8);
		buf += sizeof(bitbuf_cell) - unalign;
		len -= unalign;
	}
	bitbuf_cell *aligned = (bitbuf_cell *)buf;
	for (; len > sizeof(bitbuf_cell); len -= sizeof(bitbuf_cell), ++aligned) {
		bitbuf_appendbits(bb, *aligned, bitbuf_cell_bits);
	}
	// unaligned end bytes
	bitbuf_appendbits(bb, *aligned, len * 8);
}

/* Clear the bitbuffer to make it ready to append new data */
static inline void bitbuf_reset(struct bitbuf *bb) {
	bb->buf[0] = 0; // we have to zero out the lowest cell since it gets ORed
	bb->curbit = 0;
}

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
