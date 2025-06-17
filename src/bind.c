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

#include "con_.h"
#include "dbg.h"
#include "errmsg.h"
#include "feature.h"
#include "hook.h"
#include "intdefs.h"
#include "langext.h"
#include "mem.h"
#include "x86.h"
#include "x86util.h"

FEATURE()

struct keyinfo {
	char *binding;
	uchar keyuptgt : 3;
	uchar pressed : 1;
};
static struct keyinfo *keyinfo; // engine keybinds list (s_pKeyInfo[])

const char *bind_get(int keycode) { return keyinfo[keycode].binding; }

static bool find_keyinfo(const uchar *insns) {
#ifdef _WIN32
	for (const uchar *p = insns; p - insns < 64;) {
		// key_listboundkeys loops through each index, moving into a register:
		// mov <reg>, dword ptr [<reg> * 8 + s_pKeyInfo]
		if (p[0] == X86_MOVRMW && (p[1] & 0xC7) == 4 /* SIB + imm32 */ &&
				(p[2] & 0xC7) == 0xC5 /* [immediate + reg * 8] */) {
			keyinfo = mem_loadptr(p + 3);
			return true;
		}
		NEXT_INSN(p, "load from key binding list");
	}
#else
#warning TODO(linux): check whether linux is equivalent!
#endif
	return false;
}

INIT {
	struct con_cmd *cmd_key_listboundkeys = con_findcmd("key_listboundkeys");
	if_cold (!find_keyinfo(cmd_key_listboundkeys->cb_insns)) {
		errmsg_warnx("couldn't find key binding list");
		return FEAT_INCOMPAT;
	}
	return FEAT_OK;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
