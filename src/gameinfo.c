/*
 * Copyright © 2023 Michael Smith <mikesmiffy128@gmail.com>
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

#include "engineapi.h"
#include "errmsg.h"
#include "gamedata.h"
#include "gametype.h"
#include "os.h"
#include "vcall.h"

#ifdef _WIN32
static os_char gamedir[PATH_MAX] = {0};
#endif
static char title[64] = {0};

const os_char *gameinfo_gamedir
#ifdef _WIN32
	= gamedir // on linux, the pointer gets directly set in gameinfo_init()
#endif
;
const char *gameinfo_title = title;

DECL_VFUNC_DYN(const char *, GetGameDirectory)

bool gameinfo_init(void) {
	if (!has_vtidx_GetGameDirectory) {
		errmsg_errorx("unsupported VEngineClient interface");
		return false;
	}

#ifdef _WIN32
	// Although the engine itself uses Unicode-incompatible stuff everywhere so
	// supporting arbitrary paths is basically a no-go, turns out we still have
	// to respect the system legacy code page setting, otherwise some users
	// using e.g. Cyrillic folder names and successfully loading their
	// speedgames won't be able to load SST. Thanks Windows!
	const char *lcpgamedir = GetGameDirectory(engclient);
	if (!MultiByteToWideChar(CP_ACP, 0, lcpgamedir, strlen(lcpgamedir), gamedir,
			sizeof(gamedir) / sizeof(*gamedir))) {
		errmsg_errorsys("couldn't convert game directory path character set");
		return false;
	}
#else
	// no need to munge charset, use the string pointer directly
	gameinfo_gamedir = GetGameDirectory(engclient);
#endif

	// dumb hack: ignore Survivors title (they left it set to "Left 4 Dead 2"
	// but that game clearly isn't Left 4 Dead 2)
	if (GAMETYPE_MATCHES(L4DS)) {
		gameinfo_title = "Left 4 Dead: Survivors";
	}
	else {
#ifdef _WIN32
		// XXX: this same FindWindow call happens in ac.c - maybe factor out?
		void *gamewin = FindWindowW(L"Valve001", 0);
		// assuming: all games/mods use narrow chars only; this won't fail.
		int len = GetWindowTextA(gamewin, title, sizeof(title));
		// argh, why did they start doing this, it's so pointless!
		// hopefully nobody included these suffixes in their mod names, lol
		if (len > 13 && !memcmp(title + len - 13, " - Direct3D 9", 13)) {
			title[len - 13] = '\0';
		}
		else if (len > 9 && !memcmp(title + len - 9, " - Vulkan", 9)) {
			title[len - 9] = '\0';
		}
#else
//#error TODO(linux): grab window handle and title from SDL (a bit involved...)
		gameinfo_title = "Linux Game With As Yet Unkown Title";
#endif

		// SUPER crude algorithm to force uppercase titles like HALF-LIFE 2 or
		// PORTAL 2 to (almost-)titlecase. will refine later, as needed
		bool hasupper = false, haslower = false;
		for (char *p = title; *p && (!hasupper || !haslower); ++p) {
			haslower |= *p >= 'a' && *p <= 'z';
			hasupper |= *p >= 'A' && *p <= 'Z';
		}
		if (hasupper && !haslower) {
			int casebit = 0;
			for (char *p = title; *p; ++p) {
				if (*p >= 'A' && *p <= 'Z') *p |= casebit;
				casebit = (*p == ' ' || *p == '-') << 5; // ? 32 : 0
			}
		}
	}
	return true;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
