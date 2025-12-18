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

#include "asm.h"

// On Windows, _con_colourmsg is subject to selfmod() in con_.c which overwrites
// it with another implementation on OE; put it in rwx. On Linux, .text is fine.
#ifdef _WIN32
.section ASM_RWX_SECTION, ASM_RWX_SECTION_FLAGS
#else
.section .text, "xr"
#endif
.globl ASM_MANGLE(_con_colourmsg)
.p2align 4
ASM_MANGLE(_con_colourmsg):
	// For some reason the assembler arbitrarily refuses to put instructions in
	// a section marked as containing uninitialised data. So, we have to list
	// out the instruction bytes manually here:
	// mov eax, [_con_iface]
	.byte 0xA1
	.long ASM_MANGLE(_con_iface)
	// mov [esp + 4], eax // put coniface in the empty stack slot
	.byte 0x89
	.byte 0x44
	.byte 0x24
	.byte 0x04
	// jmp dword ptr _con_colourmsgf // jump to the real function
	.byte 0xFF
	.byte 0x25
	.long ASM_MANGLE(_con_colourmsgf)

// vi: sw=4 ts=4 noet tw=80 cc=80
