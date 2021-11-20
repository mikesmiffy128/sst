/*
 * Copyright © 2021 Michael Smith <mikesmiffy128@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
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
#define _gametype_tag_OrangeBox	2
#define _gametype_tag_L4D1		4
#define _gametype_tag_L4D2		8
#define _gametype_tag_Portal2	16
#define _gametype_tag_2013		32

#define _gametype_tag_L4D		(_gametype_tag_L4D1	| _gametype_tag_L4D2)
#define _gametype_tag_L4Dbased	(_gametype_tag_L4D	| _gametype_tag_Portal2)

#define GAMETYPE_MATCHES(x) !!(_gametype_tag & (_gametype_tag_##x))

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
