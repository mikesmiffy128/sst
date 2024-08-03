/*
 * Copyright © 2024 Michael Smith <mikesmiffy128@gmail.com>
 * Copyright © 2023 Hayden K <imaciidz@gmail.com>
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

#ifdef _WIN32
#include <d3d9.h>
#endif

#include "con_.h"
#include "gametype.h"
#include "langext.h"

static void chflags(const char *name, int unset, int set) {
	struct con_var *v = con_findvar(name);
	if (v) v->parent->base.flags = v->parent->base.flags & ~unset | set;
}

static void unhide(const char *name) {
	chflags(name, CON_HIDDEN | CON_DEVONLY, 0);
}

static void chcmdflags(const char *name, int unset, int set) {
	struct con_cmd *v = con_findcmd(name);
	if (v) v->base.flags = v->base.flags & ~unset | set;
}

static void generalfixes(void) {
	// Expose all the demo stuff, for games like L4D that hide it for some
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
	unhide("demo_interpolateview");
	unhide("cl_showdemooverlay");

	// some handy console stuff
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

	// fps_max policy varies a bit between speedgames and their communities!
	// in theory we might wanna remove CON_NOTCONN on Portal 1 in a future
	// release, but for now people haven't fully talked themselves into it.
	struct con_var *v = con_findvar("fps_max");
	if (GAMETYPE_MATCHES(L4Dx)) {
		// for L4D games, generally changing anything above normal limits is
		// disallowed, but externally capping FPS will always be possible so we
		// might as well allow lowering it ingame for convenience.
		if (v->parent->base.flags & (CON_HIDDEN | CON_DEVONLY)) {
			v->parent->base.flags &= ~(CON_HIDDEN | CON_DEVONLY);
			v->parent->hasmax = true; v->parent->maxval = 300;
		}
		else if (!v->parent->hasmax) {
			// in TLS, this was made changeable, but still limit to 1000 to
			// prevent breaking the engine
			v->parent->hasmax = true; v->parent->maxval = 1000;
		}
		// also show the lower limit in help, and prevent 0 (which is unlimited)
		v->parent->hasmin = true; v->parent->minval = 30;
		con_setvarf(v, con_getvarf(v)); // hack: reapply limit if we loaded late
	}
}

static void l4d2specific(void) {
	// L4D2 doesn't let you set sv_cheats in lobbies, but turns out it skips all
	// the lobby checks if this random command is developer-only, presumably
	// because that flag is compiled out in debug builds and devs want to be
	// able to use cheats. Took literally hours of staring at Ghidra to find
	// this out. Good meme 8/10.
	unhide("sv_hosting_lobby");

	// Older versions of L4D2 reset mat_queue_mode to 0 (multicore rendering
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
	struct con_var *v = con_findvar("mat_queue_mode");
	if_hot (v && !(v->parent->base.flags & CON_ARCHIVE)) { // not already fixed
		v->parent->base.flags = v->parent->base.flags &
				~(CON_HIDDEN | CON_DEVONLY) | CON_ARCHIVE;
		v->parent->hasmin = true; v->parent->minval = -1;
		v->parent->hasmax = true; v->parent->maxval = 0;
	}

#ifdef _WIN32
	// L4D2 has broken (dark) rendering on Intel iGPUs unless
	// mat_tonemapping_occlusion_use_stencil is enabled. Supposedly Valve used
	// to detect device IDs to enable it on, but new devices are still broken,
	// so just blanket enable it if the primary adapter is Intel, since it
	// doesn't seem to break anything else anyway.
	v = con_findvar("mat_tonemapping_occlusion_use_stencil");
	if_cold (!v || con_getvari(v)) goto e;
	// considered getting d3d9 object from actual game, but it's way easier
	// to just create another one
	IDirect3D9 *d3d9 = Direct3DCreate9(D3D_SDK_VERSION);
	if_cold (!d3d9) goto e;
	D3DADAPTER_IDENTIFIER9 ident;
	if_hot (IDirect3D9_GetAdapterIdentifier(d3d9, 0, 0, &ident) == D3D_OK) {
		if (ident.VendorId == 0x8086) con_setvari(v, 1); // neat vendor id, btw!
	}
	IDirect3D9_Release(d3d9);
e:;
#endif

	// There's a rare, inexplicable issue where the game will drop to an
	// unplayable framerate - a similar thing sometimes happens in CSGO,
	// incidentally. People used to fix it by recording a demo, but that can't
	// be done if *already* recording a demo. We've since also tried doing
	// `logaddress_add 1`, which works in CSGO, but that doesn't seem to work in
	// L4D games. So, another idea is `cl_fullupdate`, but it's cheat protected.
	// We're preemptively removing its cheat flag here, so if it turns out to be
	// absolutely necessary, people can use it. If it doesn't work, or some
	// other workaround is found, this might get reverted.
	chcmdflags("cl_fullupdate", CON_CHEAT, 0);
}

static void l4d1specific(void) {
	// For some reason, L4D1 hides mat_monitorgamma and doesn't archive it.
	// This means on every startup it's necessary to manually set non-default
	// values via the menu. This change here brings it in line with pretty much
	// all other Source games for convenience.
	chflags("mat_monitorgamma", CON_HIDDEN | CON_DEVONLY, CON_ARCHIVE);

	// Very early versions of L4D1 have a bunch of useless console spam. Setting
	// these hidden variables to 0 gets rid of it.
	struct con_var *v = con_findvar("ui_l4d_debug");
	if (v) con_setvari(v, 0);
	v = con_findvar("mm_l4d_debug");
	if (v) con_setvari(v, 0);

	// same thing as above, seemed easier to just dupe :)
	chcmdflags("cl_fullupdate", CON_CHEAT, 0);

	// These commands lack CLIENTCMD_CAN_EXECUTE, so enabling/disabling addons
	// doesn't work without manually running these in the console afterwards.
	chcmdflags("mission_reload", 0, CON_CCMDEXEC);
	chcmdflags("update_addon_paths", 0, CON_CCMDEXEC);
}

void fixes_apply(void) {
	generalfixes();
	if (GAMETYPE_MATCHES(L4D1)) l4d1specific();
	else if (GAMETYPE_MATCHES(L4D2x)) l4d2specific();
}

// vi: sw=4 ts=4 noet tw=80 cc=80
