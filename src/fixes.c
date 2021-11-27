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

#include <stdlib.h>
#include <string.h>

#include "con_.h"

static void unhide(const char *name) {
	struct con_var *v = con_findvar(name);
	if (v) v->parent->base.flags &= ~(CON_DEVONLY | CON_HIDDEN);
}

void fixes_apply(void) {
	// expose all the demo stuff, for games like L4D that hide it for some
	// reason.
	unhide("demo_debug");
	unhide("demo_fastforwardfinalspeed");
	unhide("demo_fastforwardramptime");
	unhide("demo_fastforwardstartspeed");
	unhide("demo_gototick");
	unhide("demo_interplimit");
	unhide("demo_legacy_rollback");
	unhide("demo_pauseatservertick");
	unhide("demo_quitafterplayback");

	unhide("director_afk_timeout");
	unhide("mp_restartgame");

	// handy console stuff
	unhide("con_filter_enable");
	unhide("con_filter_text");
	unhide("con_filter_text_out");

	// also, let people just do this lol. why not
	unhide("developer");

	// L4D2 doesn't let you set sv_cheats in lobbies, but turns out it skips all
	// the lobby checks if this random command is developer-only, presumably
	// because that flag is compiled out in debug builds and devs want to be
	// able to use cheats. Took literally hours of staring at Ghidra to find
	// this out. Good meme 8/10.
	unhide("sv_hosting_lobby");
}

// vi: sw=4 ts=4 noet tw=80 cc=80
