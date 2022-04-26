/*
 * Copyright © 2022 Michael Smith <mikesmiffy128@gmail.com>
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

#ifndef INC_GAMETYPE_H
#define INC_GAMETYPE_H

#include "intdefs.h"

extern u32 _gametype_tag;

#define _gametype_tag_OE		1
// TODO(compat): detect in con_init, even if just to fail (VEngineServer broke)
// TODO(compat): buy dmomm in a steam sale to implement and test the above, lol
#define _gametype_tag_DMoMM		2
#define _gametype_tag_OrangeBox	4
#define _gametype_tag_L4D1		8
#define _gametype_tag_L4D2		16
#define _gametype_tag_L4DS		32
#define _gametype_tag_Portal1	64
#define _gametype_tag_Portal2	128
#define _gametype_tag_2013		256

#define _gametype_tag_L4D		(_gametype_tag_L4D1 | _gametype_tag_L4D2)
// XXX: *stupid* naming, refactor later (damn Survivors ruining everything)
#define _gametype_tag_L4D2x		(_gametype_tag_L4D2 | _gametype_tag_L4DS)
#define _gametype_tag_L4Dx		(_gametype_tag_L4D1 | _gametype_tag_L4D2x)
#define _gametype_tag_L4Dbased \
	(_gametype_tag_L4D1 | _gametype_tag_L4D2x | _gametype_tag_Portal2)
#define _gametype_tag_OrangeBoxbased \
	(_gametype_tag_OrangeBox | _gametype_tag_2013)

#define GAMETYPE_MATCHES(x) !!(_gametype_tag & (_gametype_tag_##x))

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
