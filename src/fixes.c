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

#include <stdlib.h>
#include <string.h>

#include "con_.h"
#include "gametype.h"

static void chflags(const char *name, int unset, int set) {
	struct con_var *v = con_findvar(name);
	if (v) v->parent->base.flags = v->parent->base.flags & ~unset | set;
}

static void unhide(const char *name) {
	chflags(name, CON_HIDDEN | CON_DEVONLY, 0);
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

	// handy console stuff
	unhide("con_filter_enable");
	unhide("con_filter_text");
	unhide("con_filter_text_out");

	// things that could conceivably cause issues with speedrun verification
	// and/or pedantic following of rules; throw on cheat flag. this could be
	// relaxed with the Eventual Fancy Demo Verification Stuff.
	chflags("director_afk_timeout", CON_HIDDEN | CON_DEVONLY, CON_CHEAT);
	chflags("mp_restartgame", CON_HIDDEN | CON_DEVONLY, CON_CHEAT);

	// also, ensure the initial state of sv_cheats goes into demos so you can't
	// start a demo with cheats already on and then do something subtle
	chflags("sv_cheats", 0, CON_DEMO);

	// also, let people use developer, it's pretty handy. ensure it goes in the
	// demo though. even though it's obvious looking at a video, maybe some day
	// a game will want to require demos only (probably not till demos are more
	// robust anyway... whatever)
	chflags("developer", CON_HIDDEN | CON_DEVONLY, CON_DEMO);

	// L4D2 doesn't let you set sv_cheats in lobbies, but turns out it skips all
	// the lobby checks if this random command is developer-only, presumably
	// because that flag is compiled out in debug builds and devs want to be
	// able to use cheats. Took literally hours of staring at Ghidra to find
	// this out. Good meme 8/10.
	unhide("sv_hosting_lobby");

	// Older versions of L4D2 reset mat_queue_mode to -1 (multicore rendering
	// off) all the time if gpu_level is 0 (low shader detail), causing lag that
	// can only be fixed by manually fixing the setting in video settings. Newer
	// versions work around this by marking it as ARCHIVE, *breaking* the code
	// that's supposed to link it to the other settings with a warning in the
	// console. This two-wrongs-make-a-right spaghetti hack fix is so stupid
	// that we can reimplement it ourselves for older versions even with our
	// limited intelligence. We also make it public for convenience, but
	// constrain it so we don't enable a configuration that isn't already
	// possible on these earlier versions (who knows if that breaks
	// something...).
	if (GAMETYPE_MATCHES(L4D2x)) {
		struct con_var *v = con_findvar("mat_queue_mode");
		if (v && (v->parent->base.flags & CON_ARCHIVE)) {
			v->parent->base.flags = v->parent->base.flags
					& ~(CON_DEVONLY | CON_HIDDEN) | CON_ARCHIVE;
			v->parent->hasmax = true; v->parent->maxval = 0;
		}
	}
}

// vi: sw=4 ts=4 noet tw=80 cc=80
