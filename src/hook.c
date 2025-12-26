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

#include "chunklets/x86.h"
#include "hook.h"
#include "intdefs.h"
#include "langext.h"
#include "mem.h"
#include "os.h"

// Warning: half-arsed hacky implementation (because that's all we really need)
// Almost certainly breaks in some weird cases. Oh well! Most of the time,
// vtable hooking is more reliable, this is only for, uh, emergencies.

uchar *_hook_getpos(uchar *func) {
	// if we are hooking some thunk that immediately jumps elsewhere (which
	// seems common for win32 API functions), hook the underlying thing instead.
	// we return this back as the thing that actually ends up getting hooked
	// after memory protections are changed.
	while (*func == X86_JMPIW) func += mem_loads32(func + 1) + 5;
	return func;
}

struct _hook_prep_ret _hook_prep(uchar *func, uchar *trampoline) {
	func = _hook_getpos(func);
	const uchar *p = func;
	int len = 0;
	for (;;) {
		if_cold (p[len] == X86_CALL) {
			return (struct _hook_prep_ret){
				0, 0, "can't trampoline call instructions"
			};
		}
		int ilen = x86_len(p + len);
		if_cold (ilen == -1) {
			return (struct _hook_prep_ret){
				0, 0, "unknown or invalid instruction"
			};
		}
		len += ilen;
		if (len >= 5) {
			memcpy(trampoline, p, len);
			trampoline[len] = X86_JMPIW;
			s32 diff = p - (trampoline + 5); // goto the continuation
			memcpy(trampoline + len + 1, &diff, 4);
			return (struct _hook_prep_ret){func, len, 0};
		}
		if_cold (p[len] == X86_JMPIW) {
			return (struct _hook_prep_ret){
				0, 0, "can't trampoline jump instructions"
			};
		}
	}
}

bool hook_inline_mprot(void *hookpos) {
	return os_mprot(hookpos, 5, PAGE_EXECUTE_READWRITE);
}

void _hook_inline_commit(uchar *restrict hookpos, const uchar *restrict target) {
	s32 diff = (uchar *)target - (hookpos + 5); // goto the hook target
	hookpos[0] = X86_JMPIW;
	memcpy(hookpos + 1, &diff, 4);
}

void _unhook_inline(uchar *trampoline, int len) {
	s32 off = mem_loads32(trampoline + len + 1);
	uchar *orig = trampoline + off + 5;
	memcpy(orig, trampoline, 5);
}

// vi: sw=4 ts=4 noet tw=80 cc=80
