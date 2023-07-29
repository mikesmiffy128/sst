/* This file is dedicated to the public domain. */

{.desc = "the bit buffer implementation"};

#include "../src/bitbuf.h"
#include "../src/intdefs.h"

#include <stdio.h>
#include <string.h>

static union {
	char buf[512];
	bitbuf_cell buf_align[512 / sizeof(bitbuf_cell)];
} bb_buf;
static struct bitbuf bb = {bb_buf.buf, 512, 512 * 8, 0, false, false, "test"};

TEST("The possible UB in bitbuf_appendbuf shouldn't trigger horrible bugs") {
	if (bitbuf_align <= 1) { // *shouldn't* happen
		fputs("what's going on with the alignment???\n", stderr);
		return false;
	}
	char _buf[32 + _Alignof(bitbuf_cell)], *buf = _buf;
	while (!((usize)buf % bitbuf_align)) ++buf;

	memcpy(buf, "Misaligned test buffer contents!", 32);
	bitbuf_appendbuf(&bb, buf, 32);
	return !memcmp(bb.buf, buf, 32);
}

TEST("Aligning to the next byte should work as intended") {
	for (int i = 0; i < 65535; i += 8) {
		bb.curbit = i;
		bitbuf_roundup(&bb);
		if (bb.curbit != i) return false; // don't round if already rounded
		for (int j = i + 1; j < i + 8; ++j) {
			bb.curbit = j;
			bitbuf_roundup(&bb);
			if (bb.curbit != i + 8) return false;
		}
	}
	return true;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
