/*
 * Copyright © 2025 Michael Smith <mikesmiffy128@gmail.com>
 * Copyright © 2023 Willian Henrique <wsimanbrazil@yahoo.com.br>
 * Copyright © 2025 Hayden K <imaciidz@gmail.com>
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

/* general engine branches used in a bunch of stuff */
#define _gametype_tag_OE		1
#define _gametype_tag_OrangeBox	(1 << 1)
#define _gametype_tag_2013		(1 << 2)

/* specific games with dedicated branches / engine changes */
// TODO(compat): detect dmomm, even if only just to fail (VEngineServer broke)
// TODO(compat): buy dmomm in a steam sale to implement and test the above, lol
#define _gametype_tag_DMoMM		(1 << 3)
#define _gametype_tag_L4D1		(1 << 4)
#define _gametype_tag_L4D2		(1 << 5)
#define _gametype_tag_L4DS		(1 << 6) /* Survivors (weird arcade port) */
#define _gametype_tag_Portal2	(1 << 7)

/* games needing game-specific stuff, but not tied to a singular branch */
#define _gametype_tag_Portal1	(1 << 8)
#define _gametype_tag_HL2series	(1 << 9) /* HL2, episodes, and mods */

/* VEngineClient versions */
#define _gametype_tag_Client015 (1 << 10)
#define _gametype_tag_Client014 (1 << 11)
#define _gametype_tag_Client013 (1 << 12)
#define _gametype_tag_Client012 (1 << 13)
#define _gametype_tag_Server021 (1 << 14)

/* ServerGameDLL versions */
#define _gametype_tag_SrvDLL009	(1 << 15) // 2013-ish
#define _gametype_tag_SrvDLL005	(1 << 16) // mostly everything else, it seems

/* games needing version-specific stuff */
#define _gametype_tag_Portal1_3420	(1 << 17)
#define _gametype_tag_L4D1_1015plus	(1 << 18) // Crash Course update
#define _gametype_tag_L4D1_1022plus	(1 << 19) // Mac update, with code shuffling
#define _gametype_tag_L4D2_2125plus	(1 << 20)
#define _gametype_tag_TheLastStand	(1 << 21) /* The JAiZ update */

/* Matches for any multiple possible tags */
#define _gametype_tag_L4D		(_gametype_tag_L4D1 | _gametype_tag_L4D2)
// XXX: *stupid* naming, refactor one day (damn Survivors ruining everything)
#define _gametype_tag_L4D2x		(_gametype_tag_L4D2 | _gametype_tag_L4DS)
#define _gametype_tag_L4Dx		(_gametype_tag_L4D1 | _gametype_tag_L4D2x)
#define _gametype_tag_L4Dbased	(_gametype_tag_L4Dx | _gametype_tag_Portal2)
#define _gametype_tag_OrangeBoxbased \
	(_gametype_tag_OrangeBox | _gametype_tag_2013)
#define _gametype_tag_Portal (_gametype_tag_Portal1 | _gametype_tag_Portal2)

#define GAMETYPE_MATCHES(x) !!(_gametype_tag & (_gametype_tag_##x))

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
