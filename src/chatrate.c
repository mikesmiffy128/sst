/*
 * Copyright © Hayden K <imaciidz@gmail.com>
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

#include "con_.h"
#include "errmsg.h"
#include "feature.h"
#include "intdefs.h"
#include "langext.h"
#include "os.h"
#include "x86.h"
#include "x86util.h"

FEATURE("chat rate limit removal")
GAMESPECIFIC(L4Dbased) // Tested/known to work in L4D1/2, L4D:S, and Portal 2.

static uchar *patchedbyte;

// Same method as SAR here. Basically, the game only lets you chat every 0.66s.
// So, instead of adding 0.66 to the current time, we subtract it, and that
// means we can always chat immediately.

static inline bool find_ratelimit_insn(con_cmdcb say_cb) {
	// Find the add instruction
	uchar *insns = (uchar *)say_cb;
	for (uchar *p = insns; p - insns < 128;) {
		// find FADD
		if (p[0] == X86_FLTBLK5 && p[1] == X86_MODRM(0, 0, 5)) {
			patchedbyte = p + 1;
			return true;
		}
		// Portal 2, L4D2 2125-2134, L4D:S all use SSE2, so try finding ADDSD
		if (p[0] == X86_PFX_REPN && p[1] == X86_2BYTE & p[2] == X86_2B_ADD) {
			patchedbyte = p + 2;
			return true;
		}
		NEXT_INSN(p, "chat rate limit");
	}
	return false;
}

static inline bool patch_ratelimit_insn() {
	// if FADD replace with FSUB; otherwise it is ADDSD, replace that with SUBSD
	if_cold (!os_mprot(patchedbyte, 1, PAGE_EXECUTE_READWRITE)) {
		errmsg_errorsys("failed to patch chat rate limit: "
				"couldn't make memory writable");
		return false;
	}
	if (*patchedbyte == X86_MODRM(0, 0, 5)) *patchedbyte = X86_MODRM(0, 4, 5);
	else *patchedbyte = X86_2B_SUB;
	return true;
}

static inline void unpatch_ratelimit_insn() {
	// same logic as above but in reverse
	if (*patchedbyte == X86_MODRM(0, 4, 5)) *patchedbyte = X86_MODRM(0, 0, 5);
	else *patchedbyte = X86_2B_ADD;
}

INIT {
	struct con_cmd *cmd_say = con_findcmd("say");
	if_cold (!cmd_say) return FEAT_INCOMPAT; // should never happen!
	if (!find_ratelimit_insn(cmd_say->cb)) {
		errmsg_errorx("couldn't find chat rate limit instruction");
		return FEAT_INCOMPAT;
	}
	if (!patch_ratelimit_insn()) {
		errmsg_errorx("couldn't patch chat rate limit");
		return FEAT_FAIL;
	}
	return FEAT_OK;
}

END {
	unpatch_ratelimit_insn();
}
