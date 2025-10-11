/*
 * Copyright © Michael Smith <mikesmiffy128@gmail.com>
 * Copyright © Willian Henrique <wsimanbrazil@yahoo.com.br>
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

#ifndef INC_GAMETYPE_H
#define INC_GAMETYPE_H

#include "intdefs.h"

extern u32 _gametype_tag;

#define GAMETYPE_BASETAGS(ALL, WINDOWSONLY) \
	/* general engine branches used in a bunch of stuff */ \
	WINDOWSONLY(OE) \
	ALL(OrangeBox) \
	ALL(2013) \
\
	/* specific games with dedicated branches / engine changes */ \
	/* TODO(compat): dmomm seems to fail currently (VEngineServer broke?) */ \
	WINDOWSONLY(DMoMM) \
	WINDOWSONLY(L4D1) \
	ALL(L4D2) \
	WINDOWSONLY(L4DS) /* Survivors (weird arcade port) */ \
	ALL(Portal2) \
\
	/* games needing game-specific stuff, but not tied to a singular branch */ \
	ALL(Portal1) \
	ALL(HL2series) /* HL2, episodes, mods */ \
\
	/* VEngineClient versions */ \
	ALL(Client015) \
	ALL(Client014) \
	ALL(Client013) \
	ALL(Client012) \
\
	/* VEngineServer versions */ \
	ALL(Server021) \
\
	/* ServerGameDLL versions */ \
	ALL(SrvDLL009) /* 2013-ish */ \
	ALL(SrvDLL005) /* mostly everything else, it seems */ \
\
	/* games needing version-specific stuff */ \
	WINDOWSONLY(Portal1_3420) \
	WINDOWSONLY(L4D1_1015plus) /* Crash Course update */ \
	WINDOWSONLY(L4D1_1022plus) /* Mac update, bunch of code reshuffling */ \
	ALL(L4D2_2125plus) \
	ALL(TheLastStand) /* The JAiZ update */ \

enum {
	// here we define the enum values in such a way that on linux, the windows-
	// only tags are still defined as zero. that way we can use GAMETYPE_MATCHES
	// checks in some cases without needing #ifdef _WIN32 and the optimiser can
	// throw it out.
#define _GAMETYPE_ENUMBIT(x) _gametype_tagbit_##x,
#define _GAMETYPE_ENUMVAL(x) _gametype_tag_##x = 1 << _gametype_tagbit_##x,
#define _GAMETYPE_DISCARD(x)
#define _GAMETYPE_ZERO(x) _gametype_tag_##x = 0,
#ifdef _WIN32
GAMETYPE_BASETAGS(_GAMETYPE_ENUMBIT, _GAMETYPE_ENUMBIT)
GAMETYPE_BASETAGS(_GAMETYPE_ENUMVAL, _GAMETYPE_ENUMVAL)
#else
GAMETYPE_BASETAGS(_GAMETYPE_ENUMBIT, _GAMETYPE_DISCARD)
GAMETYPE_BASETAGS(_GAMETYPE_ENUMVAL, _GAMETYPE_DISCARD)
GAMETYPE_BASETAGS(_GAMETYPE_DISCARD, _GAMETYPE_ZERO)
#endif
#define _GAMETYPE_ENUMVAL(x) _gametype_tag_##x = 1 << _gametype_tagbit_##x,
#undef _GAMETYPE_ZERO
#undef _GAMETYPE_DISCARD
#undef _GAMETYPE_ENUMVAL
#undef _GAMETYPE_ENUMBIT
};

/* Matches for any of multiple possible tags */
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
