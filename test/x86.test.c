/* This file is dedicated to the public domain. */

{.desc = "x86 opcode parsing"};

#include "../src/x86.c"
#include "../src/intdefs.h"

TEST("The \"crazy\" instructions should be given correct lengths\n") {
	const uchar test8[] = {
		0xF6, 0x05, 0x12, 0x34, 0x56, 0x78, 0x12
	};
	const uchar test16[] = {
		0x66, 0xF7, 0x05, 0x12, 0x34, 0x56, 0x78, 0x12, 0x34
	};
	const uchar test32[] = {
		0xF7, 0x05, 0x12, 0x34, 0x56, 0x78, 0x12, 0x34, 0x56, 0x78
	};
	const uchar not8[] = {
		0xF6, 0x15, 0x12, 0x34, 0x56, 0x78
	};
	const uchar not16[] = {
		0x66, 0xF7, 0x15, 0x12, 0x34, 0x56, 0x78
	};
	const uchar not32[] = {
		0xF7, 0x15, 0x12, 0x34, 0x56, 0x78
	};
	if (x86_len(test8) != 7) return false;
	if (x86_len(test16) != 9) return false;
	if (x86_len(test32) != 10) return false;
	if (x86_len(not8) != 6) return false;
	if (x86_len(not16) != 7) return false;
	if (x86_len(not32) != 6) return false;
	return true;
}

TEST("SIB bytes should be decoded correctly") {
	const uchar fstp[] = {0xD9, 0x1C, 0x24}; // old buggy case, for regressions
	return x86_len(fstp) == 3;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
