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
	char unalign[3] = {'X', 'X', 'X'};
	char _buf[32 + sizeof(bitbuf_cell)];
	char *buf = _buf;
	if (bitbuf_align <= 1) {
		// *shouldn't* happen
		fputs("what's going on with the alignment???\n", stderr);
		return false;
	}
	// make sure the pointer is definitely misaligned
	while (!((usize)buf % bitbuf_align)) ++buf;

	memcpy(buf, "Misaligned test buffer contents!", 32);
	bitbuf_appendbuf(&bb, buf, 32);
	return !memcmp(bb.buf, buf, 32);
}

// vi: sw=4 ts=4 noet tw=80 cc=80
