/*
 * Copyright © 2023 Michael Smith <mikesmiffy128@gmail.com>
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

#include "intdefs.h"
#include "x86.h"

static int mrmsib(const uchar *p, int addrlen) {
	// I won't lie: I thought I almost understood this, but after Bill walked me
	// through correcting a bunch of wrong cases I now realise that I don't
	// really understand it at all. If it helps, I used this as a reference:
	// https://github.com/Nomade040/length-disassembler/blob/e8b34546/ldisasm.cpp#L14
	// But it's confusingly-written enough that the code I wrote before didn't
	// work, so with any luck nobody will need to refer to it again and this is
	// actually correct now. Fingers crossed.
	if (addrlen == 4 || *p & 0xC0) {
		int sib = addrlen == 4 && *p < 0xC0 && (*p & 7) == 4;
		switch (*p & 0xC0) {
			// disp8
			case 0x40: return 2 + sib;
			// disp16/32
			case 0:
				if ((*p & 7) != 5) {
					// disp8/32 via SIB
					if (sib && (p[1] & 7) == 5) return *p & 0x40 ? 3 : 6;
					return 1 + sib;
				}
			case 0x80: return 1 + addrlen + sib;
		}
	}
	if (addrlen == 2 && *p == 0x26) return 3;
	return 1; // note: include the mrm itself in the byte count
}

int x86_len(const void *insn_) {
#define CASES(name, _) case name:
	const uchar *insn = insn_;
	int pfxlen = 0, addrlen = 4, operandlen = 4;

p:	switch (*insn) {
		case X86_PFX_ADSZ: addrlen = 2; goto P; // bit dumb sorry
		case X86_PFX_OPSZ: operandlen = 2;
P:		X86_SEG_PREFIXES(CASES)
		case X86_PFX_LOCK: case X86_PFX_REPN: case X86_PFX_REP:
			// instruction can only be 15 bytes. this could go over, oh well,
			// just don't want to loop for 8 million years
			if (++pfxlen == 14) return -1;
			++insn;
			goto p;
	}

	switch (*insn) {
		X86_OPS_1BYTE_NO(CASES) return pfxlen + 1;
		X86_OPS_1BYTE_I8(CASES) operandlen = 1;
		X86_OPS_1BYTE_IW(CASES) return pfxlen + 1 + operandlen;
		X86_OPS_1BYTE_I16(CASES) return pfxlen + 3;
		X86_OPS_1BYTE_MRM(CASES) return pfxlen + 1 + mrmsib(insn + 1, addrlen);
		X86_OPS_1BYTE_MRM_I8(CASES) operandlen = 1;
		X86_OPS_1BYTE_MRM_IW(CASES)
			return pfxlen + 1 + operandlen + mrmsib(insn + 1, addrlen);
		case X86_ENTER: return pfxlen + 4;
		case X86_CRAZY8: operandlen = 1;
		case X86_CRAZYW:
			if ((insn[1] & 0x38) >= 0x10) operandlen = 0;
			return pfxlen + 1 + operandlen + mrmsib(insn + 1, addrlen);
		case X86_2BYTE: ++insn; goto b2;
	}
	return -1;

b2:	switch (*insn) {
		// we don't support any 3 byte ops for now, implement if ever needed...
		case X86_3BYTE1: case X86_3BYTE2: case X86_3DNOW: return -1;
		X86_OPS_2BYTE_NO(CASES) return pfxlen + 2;
		X86_OPS_2BYTE_IW(CASES) return pfxlen + 2 + operandlen;
		X86_OPS_2BYTE_MRM(CASES) return pfxlen + 2 + mrmsib(insn + 1, addrlen);
		X86_OPS_2BYTE_MRM_I8(CASES) operandlen = 1;
			return pfxlen + 2 + operandlen + mrmsib(insn + 1, addrlen);
	}

	return -1;
#undef CASES
}

// vi: sw=4 ts=4 noet tw=80 cc=80
