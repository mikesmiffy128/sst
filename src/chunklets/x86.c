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

// _Static_assert needs MSVC >= 2019, and this check is irrelevant on Windows
#ifndef _MSC_VER
_Static_assert((unsigned char)-1 == 255, "this code requires 8-bit chars");
#endif

#include "x86.h"

static int mrmsib(const unsigned char *p, int addrlen) {
	if (addrlen == 4 || *p & 0xC0) {
		int sib = addrlen == 4 && *p < 0xC0 && (*p & 7) == 4;
		switch (*p & 0xC0) {
			case 0x40: // disp8
				return 2 + sib;
			case 0: // disp16/32
				if ((*p & 7) != 5) {
					// disp8/32 via SIB
					if (sib && (p[1] & 7) == 5) return *p & 0x40 ? 3 : 6;
					return 1 + sib;
				}
			case 0x80:
				return 1 + addrlen + sib;
		}
	}
	if (addrlen == 2 && (*p & 0xC7) == 0x06) return 3;
	return 1; // note: include the mrm itself in the byte count
}

enum {
	CLASS_UNKNOWN,

	CLASS_PFX,
	CLASS_NO,
	CLASS_I8,
	CLASS_IW,
	CLASS_IWI,
	CLASS_I16,
	CLASS_MRM,
	CLASS_MRMI8,
	CLASS_MRMIW,
	CLASS_ENTER,
	CLASS_CRAZY8,
	CLASS_CRAZYW,
	CLASS_2BYTE,

	CLASS2B_NO = 1,
	CLASS2B_IW,
	CLASS2B_MRM,
	CLASS2B_MRMI8
};

// note: could theoretically shrink these tables down to 128B each although to
// do it at compile time without the use of _BitInt(4) would be kind of tricky.
static const unsigned char classtab[256] = {
#define TABLEENT(name, val) [val] = CLASS_PFX,
	X86_PREFIXES(TABLEENT)
#undef TABLEENT
#define TABLEENT(name, val) [val] = CLASS_NO,
	X86_OPS_1BYTE_NO(TABLEENT)
#undef TABLEENT
#define TABLEENT(name, val) [val] = CLASS_I8,
	X86_OPS_1BYTE_I8(TABLEENT)
#undef TABLEENT
#define TABLEENT(name, val) [val] = CLASS_IW,
	X86_OPS_1BYTE_IW(TABLEENT)
#undef TABLEENT
#define TABLEENT(name, val) [val] = CLASS_IWI,
	X86_OPS_1BYTE_IWI(TABLEENT)
#undef TABLEENT
#define TABLEENT(name, val) [val] = CLASS_I16,
	X86_OPS_1BYTE_I16(TABLEENT)
#undef TABLEENT
#define TABLEENT(name, val) [val] = CLASS_MRM,
	X86_OPS_1BYTE_MRM(TABLEENT)
#undef TABLEENT
#define TABLEENT(name, val) [val] = CLASS_MRMI8,
	X86_OPS_1BYTE_MRM_I8(TABLEENT)
#undef TABLEENT
#define TABLEENT(name, val) [val] = CLASS_MRMIW,
	X86_OPS_1BYTE_MRM_IW(TABLEENT)
#undef TABLEENT
	[X86_ENTER] = CLASS_ENTER,
	[X86_CRAZY8] = CLASS_CRAZY8,
	[X86_CRAZYW] = CLASS_CRAZYW,
	[X86_2BYTE] = CLASS_2BYTE
};

static const unsigned char classtab_2b[256] = {
	// we don't support any 3 byte ops for now; implement if ever needed...
	[X86_3BYTE1] = CLASS_UNKNOWN,
	[X86_3BYTE2] = CLASS_UNKNOWN,
	[X86_3DNOW] = CLASS_UNKNOWN,
#define TABLEENT(name, val) [val] = CLASS2B_NO,
	X86_OPS_2BYTE_NO(TABLEENT)
#undef TABLEENT
#define TABLEENT(name, val) [val] = CLASS2B_IW,
	X86_OPS_2BYTE_IW(TABLEENT)
#undef TABLEENT
#define TABLEENT(name, val) [val] = CLASS2B_MRM,
	X86_OPS_2BYTE_MRM(TABLEENT)
#undef TABLEENT
#define TABLEENT(name, val) [val] = CLASS2B_MRMI8,
	X86_OPS_2BYTE_MRM_I8(TABLEENT)
#undef TABLEENT
};

#ifndef X86_DISABLE_SIZE_OPT
#if defined(__clang__)
__attribute((minsize))
#elif defined(__GNUC__)
__attribute((optimize("Os")))
#endif
#endif
int x86_len(const unsigned char *insn) {
#define CASES(name, _) case name:
	int pfxlen = 0, addrlen = 4, operandlen = 4;

p:	switch (classtab[*insn]) {
		case CLASS_UNKNOWN: return -1;
		case CLASS_PFX:
			switch (*insn) {
				case X86_PFX_ADSZ: addrlen = 2; goto P; // bit dumb sorry
				case X86_PFX_OPSZ: operandlen = 2;
P:				X86_SEG_PREFIXES(CASES)
				case X86_PFX_LOCK: case X86_PFX_REPN: case X86_PFX_REP:
					// instruction can only be 15 bytes. this could go over, oh
					// well, just don't want to loop for 8 million years
					if (++pfxlen == 14) return -1;
					++insn;
					goto p;
		}
		case CLASS_NO: return pfxlen + 1;
		case CLASS_I8: operandlen = 1;
		case CLASS_IW: return pfxlen + 1 + operandlen;
		case CLASS_IWI: return pfxlen + 1 + addrlen;
		case CLASS_I16: return pfxlen + 3;
		case CLASS_MRM: return pfxlen + 1 + mrmsib(insn + 1, addrlen);
		case CLASS_MRMI8: operandlen = 1;
		case CLASS_MRMIW:
			return pfxlen + 1 + operandlen + mrmsib(insn + 1, addrlen);
		case CLASS_ENTER: return pfxlen + 4;
		case CLASS_CRAZY8: operandlen = 1;
		case CLASS_CRAZYW:
			if ((insn[1] & 0x38) >= 0x10) operandlen = 0;
			return pfxlen + 1 + operandlen + mrmsib(insn + 1, addrlen);
		case CLASS_2BYTE: ++insn; goto b2;
	}
#if defined(__GNUC__) || defined(__clang__)
	__builtin_unreachable();
#elif defined(_MSC_VER)
	__assume(0);
#else
	return -1;
#endif

b2:	switch (classtab_2b[*insn]) {
		case CLASS_UNKNOWN: return -1;
		case CLASS2B_NO: return pfxlen + 2;
		case CLASS2B_IW: return pfxlen + 2 + operandlen;
		case CLASS2B_MRM: return pfxlen + 2 + mrmsib(insn + 1, addrlen);
		case CLASS2B_MRMI8: operandlen = 1;
			return pfxlen + 2 + operandlen + mrmsib(insn + 1, addrlen);
	}
#if defined(__GNUC__) || defined(__clang__)
	__builtin_unreachable();
#elif defined(_MSC_VER)
	__assume(0);
#else
	return -1;
#endif
#undef CASES
}

// vi: sw=4 ts=4 noet tw=80 cc=80
