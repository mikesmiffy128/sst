/*
 * Copyright © Michael Smith <mikesmiffy128@gmail.com>
 * Copyright © Willian Henrique <wsimanbrazil@yahoo.com.br>
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

#include <string.h>

#include "hook.h"
#include "intdefs.h"
#include "langext.h"
#include "mem.h"
#include "os.h"
#include "x86.h"

// Warning: half-arsed hacky implementation (because that's all we really need)
// Almost certainly breaks in some weird cases. Oh well! Most of the time,
// vtable hooking is more reliable, this is only for, uh, emergencies.

static _Alignas(4096) uchar trampolines[4096];
static uchar *curtrampoline = trampolines;

bool hook_init() {
	// PE doesn't support rwx sections, not sure about ELF. Meh, just set it
	// here instead.
	return os_mprot(trampolines, 4096, PAGE_EXECUTE_READWRITE);
}

struct hook_inline_prep_ret hook_inline_prep(void *func, void **trampoline) {
	uchar *p = func;
	// dumb hack: if we hit some thunk that immediately jumps elsewhere (which
	// seems common for win32 API functions), hook the underlying thing instead.
	// later: that dumb hack has now ended up having implications in the
	// redesign of the entire API. :-)
	while (*p == X86_JMPIW) p += mem_loads32(p + 1) + 5;
	void *prologue = p;
	int len = 0;
	for (;;) {
		if_cold (p[len] == X86_CALL) {
			return (struct hook_inline_prep_ret){
				0, "can't trampoline call instructions"
			};
		}
		int ilen = x86_len(p + len);
		if_cold (ilen == -1) {
			return (struct hook_inline_prep_ret){
				0, "unknown or invalid instruction"
			};
		}
		len += ilen;
		if (len >= 5) {
			// we should have statically made trampoline buffer size big enough
			assume(curtrampoline - trampolines < sizeof(trampolines) - len - 6);
			*curtrampoline = len; // stuff length in there for quick unhooking
			uchar *newtrampoline = curtrampoline + 1;
			curtrampoline += len + 6;
			memcpy(newtrampoline, p, len);
			newtrampoline[len] = X86_JMPIW;
			u32 diff = p - (newtrampoline + 5); // goto the continuation
			memcpy(newtrampoline + len + 1, &diff, 4);
			*trampoline = newtrampoline;
			return (struct hook_inline_prep_ret){prologue, 0};
		}
		if_cold (p[len] == X86_JMPIW) {
			return (struct hook_inline_prep_ret){
				0, "can't trampoline jump instructions"
			};
		}
	}
}

bool hook_inline_mprot(void *prologue) {
	return os_mprot(prologue, 5, PAGE_EXECUTE_READWRITE);
}

void hook_inline_commit(void *restrict prologue, void *restrict target) {
	uchar *p = prologue;
	u32 diff = (uchar *)target - (p + 5); // goto the hook target
	p[0] = X86_JMPIW;
	memcpy(p + 1, &diff, 4);
}

void unhook_inline(void *orig) {
	uchar *p = orig;
	int len = p[-1];
	int off = mem_loads32(p + len + 1);
	uchar *q = p + off + 5;
	memcpy(q, p, 5); // XXX: not atomic atm! (does any of it even need to be?)
}

// vi: sw=4 ts=4 noet tw=80 cc=80
