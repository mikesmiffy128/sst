/*
 * Copyright © 2021 Michael Smith <mikesmiffy128@gmail.com>
 * Copyright © 2022 Willian Henrique <wsimanbrazil@yahoo.com.br>
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

#include "con_.h"
#include "intdefs.h"
#include "mem.h"
#include "os.h"
#include "udis86.h"

// Warning: half-arsed hacky implementation (because that's all we really need)
// Almost certainly breaks in some weird cases. Oh well! Most of the time,
// vtable hooking is more reliable, this is only for, uh, emergencies.

#if defined(_WIN32) && !defined(_WIN64)

__attribute__((aligned(4096)))
static uchar trampolines[4096];
static uchar *nexttrampoline = trampolines;
__attribute__((constructor))
static void setrwx(void) {
	// PE doesn't support rwx sections, not sure about ELF. Eh, just hack it in
	// a constructor instead. If this fails and we segfault later, too bad!
	os_mprot(trampolines, sizeof(trampolines), PAGE_EXECUTE_READWRITE);
}

#define RELJMP 0xE9 // the first byte of a 5-byte jmp

void *hook_inline(void *func_, void *target) {
	uchar *func = func_;
	if (!os_mprot(func, 5, PAGE_EXECUTE_READWRITE)) return false;
	struct ud udis;
	ud_init(&udis);
	ud_set_mode(&udis, 32);
	// max insn length is 15, we overwrite 5, so max to copy is 4 + 15 = 19
	ud_set_input_buffer(&udis, func, 19);
	int len = 0;
	while (ud_disassemble(&udis) && len < 5) {
		if (ud_insn_mnemonic(&udis) == UD_Ijmp ||
				ud_insn_mnemonic(&udis) == UD_Icall) {
			con_warn("hook_inline: jmp adjustment NYI\n");
			return 0;
		}
		len += ud_insn_len(&udis);
	}
	// for simplicity, just bump alloc the trampoline. no need to free anyway
	if (nexttrampoline - trampolines > sizeof(trampolines) - len - 6) goto nospc;
	uchar *trampoline = (uchar *)InterlockedExchangeAdd(
			(volatile long *)&nexttrampoline, len + 6);
	// avoid TOCTOU
	if (trampoline - trampolines > sizeof(trampolines) - len - 6) {
nospc:	con_warn("hook_inline: out of trampoline space\n");
		return 0;
	}
	*trampoline++ = len; // stick length in front for quicker unhooking
	memcpy(trampoline, func, len);
	trampoline[len] = RELJMP;
	uint diff = func - (trampoline + 5); // goto the continuation
	memcpy(trampoline + len + 1, &diff, 4);
	uchar jmp[8];
	jmp[0] = RELJMP;
	diff = (uchar *)target - (func + 5); // goto the hook target
	memcpy(jmp + 1, &diff, 4);
	// pad with original bytes so we can do an 8-byte atomic write
	memcpy(jmp + 5, func + 5, 3);
	*(volatile uvlong *)func = *(uvlong *)jmp; // (assuming function is aligned)
	FlushInstructionCache(GetCurrentProcess(), func, len);
	return trampoline;
}

void unhook_inline(void *orig) {
	uchar *p = (uchar *)orig;
	int len = p[-1];
	uint off = mem_load32(p + len + 1);
	uchar *q = p + off + 5;
	memcpy(q, p, 5); // XXX not atomic atm! (does any of it even need to be?)
	FlushInstructionCache(GetCurrentProcess(), q, 5);
}

#else

// TODO(linux): Implement for Linux and/or x86_64 when needed...

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
