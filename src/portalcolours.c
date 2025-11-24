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

#include <string.h>

#include "con_.h"
#include "engineapi.h"
#include "errmsg.h"
#include "feature.h"
#include "gametype.h"
#include "hexcolour.h"
#include "hook.h"
#include "intdefs.h"
#include "langext.h"
#include "mem.h"
#include "ppmagic.h"
#include "sst.h"
#include "vcall.h"

FEATURE("portal gun colour customisation")
GAMESPECIFIC(Portal1)
REQUIRE_GLOBAL(clientlib)

// It's like the thing Portal Tools does, but at runtime!

DEF_FEAT_CVAR(sst_portal_colour0, "Crosshair colour for gravity beam (RGB hex)",
		"F2CAA7", CON_ARCHIVE)
DEF_FEAT_CVAR(sst_portal_colour1, "Crosshair colour for left portal (RGB hex)",
		"40A0FF", CON_ARCHIVE)
DEF_FEAT_CVAR(sst_portal_colour2, "Crosshair colour for right portal (RGB hex)",
		"FFA020", CON_ARCHIVE)
static struct rgba colours[3] = {
		{242, 202, 167, 255}, {64, 160, 255, 255}, {255, 160, 32, 255}};

static void colourcb(struct con_var *v) {
	// this is stupid and ugly and has no friends, too bad!
	if (v == sst_portal_colour0) {
		hexcolour_rgb(colours[0].bytes, con_getvarstr(v));
	}
	else if (v == sst_portal_colour1) {
		hexcolour_rgb(colours[1].bytes, con_getvarstr(v));
	}
	else /* sst_portal_colour2 */ {
		hexcolour_rgb(colours[2].bytes, con_getvarstr(v));
	}
}

// Original sig is the following but we wanna avoid calling convention weirdness
//typedef struct rgba (*UTIL_Portal_Color_func)(int);
typedef void (*UTIL_Portal_Color_func)(struct rgba *out, int portal);
static UTIL_Portal_Color_func orig_UTIL_Portal_Color;
static void hook_UTIL_Portal_Color(struct rgba *out, int portal) {
	if (portal < 0 || portal > 2) *out = (struct rgba){255, 255, 255, 255};
	else *out = colours[portal];
}

// TODO(compat): would like to do the usual pointer-chasing business instead of
// using hardcoded offsets, but that's pretty hard here. Would probably have to
// do the entprops stuff for ClientClass, get at the portalgun factory, get a
// vtable, find ViewModelDrawn or something, chase through another 4 or 5 call
// offsets to find something that calls UTIL_Portal_Color... that or dig through
// vgui/hud entries, find the crosshair drawing...
//
// For now we do this!

static bool find_UTIL_Portal_Color(void *base) {
	static const uchar x[] = HEXBYTES(8B, 44, 24, 08, 83, E8, 00, 74, 37, 83,
			E8, 01, B1, FF, 74, 1E, 83, E8, 01, 8B, 44, 24, 04, 88);
	// 5135
	orig_UTIL_Portal_Color = (UTIL_Portal_Color_func)mem_offset(base, 0x1BF090);
	if (!memcmp((void *)orig_UTIL_Portal_Color, x, sizeof(x))) return true;
	// 3420
	orig_UTIL_Portal_Color = (UTIL_Portal_Color_func)mem_offset(base, 0x1AA810);
	if (!memcmp((void *)orig_UTIL_Portal_Color, x, sizeof(x))) return true;
	// SteamPipe (7197370) - almost sure to break in a later update!
	// TODO(compat): this has indeed been broken for ages.
	// TODO(compat): we also still don't have 4104. really need to do this
	// properly some time soon, it seems.
	static const uchar y[] = HEXBYTES(55, 8B, EC, 8B, 45, 0C, 83, E8, 00, 74,
			24, 48, 74, 16, 48, 8B, 45, 08, 74, 08, C7, 00, FF, FF);
	orig_UTIL_Portal_Color = (UTIL_Portal_Color_func)mem_offset(base, 0x234C00);
	if (!memcmp((void *)orig_UTIL_Portal_Color, y, sizeof(y))) return true;
	return false;
}

INIT {
#ifdef _WIN32
	if_cold (!find_UTIL_Portal_Color(clientlib)) {
		errmsg_errorx("couldn't find UTIL_Portal_Color");
		return FEAT_INCOMPAT;
	}
	struct hook_inline_featsetup_ret h = hook_inline_featsetup(
			(void *)orig_UTIL_Portal_Color, (void **)&orig_UTIL_Portal_Color,
			"UTIL_Portal_Color");
	if_cold (h.err) return h.err;
	hook_inline_commit(h.prologue, (void *)&hook_UTIL_Portal_Color);
	sst_portal_colour0->cb = &colourcb;
	sst_portal_colour1->cb = &colourcb;
	sst_portal_colour2->cb = &colourcb;
	return FEAT_OK;
#else
#warning TODO(linux): yet more stuff!
	return FEAT_INCOMPAT;
#endif
}

END {
	if_hot (!sst_userunloaded) return;
	unhook_inline((void *)orig_UTIL_Portal_Color);
}

// vi: sw=4 ts=4 noet tw=80 cc=80
