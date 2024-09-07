/* This file is dedicated to the public domain. */

#include <stdbool.h>
#include <stdio.h>

#include "../src/udis86.h"
#include "../src/udis86.c"
#include "../src/intdefs.h"
#include "../src/x86.h"
#include "../src/x86.c"
#include "../src/os.h"
#include "../src/os.c"

/*
 * Quick hacked-up test program to more exhaustively test x86.c. This is not run
 * as part of the build; it is just here for development and reference purposes.
 */

int main(void) {
	uchar buf[15];
	int bad = 0;
	for (int i = 0; i < 100000000 && bad < 30; ++i) {
		os_randombytes(buf, sizeof(buf));
		struct ud u;
		ud_init(&u);
		ud_set_mode(&u, 32);
		ud_set_input_buffer(&u, buf, sizeof(buf));
		ud_set_syntax(&u, UD_SYN_INTEL);
		int len = ud_disassemble(&u);
		if (len && ud_insn_mnemonic(&u) != UD_Iinvalid) {
			int mylen = x86_len(buf);
			if (mylen != -1 && mylen != len) {
				++bad;
				fprintf(stderr, "Uh oh! %s\nExp: %d\nGot: %d\nBytes:",
						ud_insn_asm(&u), len, mylen);
				for (int i = 0; i < len; ++i) fprintf(stderr, " %02X", buf[i]);
				fputs("\n\n", stderr);
			}
		}
	}
	fprintf(stderr, "%d bad cases\n", bad);
}

