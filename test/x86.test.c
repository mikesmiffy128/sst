/* This file is dedicated to the public domain. */

{.desc = "x86 opcode parsing"};

#include "../src/x86.c"
#include "../src/intdefs.h"

#include "../src/ppmagic.h"

TEST("The \"crazy\" instructions should be given correct lengths\n") {
	const uchar test8[] = HEXBYTES(F6, 05, 12, 34, 56, 78, 12);
	const uchar test16[] = HEXBYTES(66, F7, 05, 12, 34, 56, 78, 12, 34);
	const uchar test32[] = HEXBYTES(F7, 05, 12, 34, 56, 78, 12, 34, 56, 78);
	const uchar not8[] = HEXBYTES(F6, 15, 12, 34, 56, 78);
	const uchar not16[] = HEXBYTES(66, F7, 15, 12, 34, 56, 78);
	const uchar not32[] = HEXBYTES(F7, 15, 12, 34, 56, 78);
	if (x86_len(test8) != 7) return false;
	if (x86_len(test16) != 9) return false;
	if (x86_len(test32) != 10) return false;
	if (x86_len(not8) != 6) return false;
	if (x86_len(not16) != 7) return false;
	if (x86_len(not32) != 6) return false;
	return true;
}

TEST("SIB bytes should be decoded correctly") {
	const uchar fstp[] = HEXBYTES(D9, 1C, 24); // old buggy case for regressions
	return x86_len(fstp) == 3;
}

TEST("mov AL, moff8 instructions should be decoded correctly") {
	// more fixed buggy cases for regressions
	const uchar mov_moff8_al[] = HEXBYTES(A2, DA, 78, B4, 0D);
	const uchar mov_al_moff8[] = HEXBYTES(A0, 28, DF, 5C, 66);
	if (x86_len(mov_moff8_al) != 5) return false;
	if (x86_len(mov_al_moff8) != 5) return false;
	return true;
}

TEST("16-bit MRM instructions should be decoded correctly") {
	const uchar fiadd_off16[] = HEXBYTES(67, DA, 06, DF, 11);
	const uchar fld_tword[] = HEXBYTES(67, DB, 2E, 99, C4);
	const uchar add_off16_bl[] = HEXBYTES(67, 00, 1E, F5, BB);
	if (x86_len(fiadd_off16) != 5) return false;
	if (x86_len(fld_tword) != 5) return false;
	if (x86_len(add_off16_bl) != 5) return false;
	return true;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
