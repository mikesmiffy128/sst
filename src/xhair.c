/*
 * Copyright © 2024 Michael Smith <mikesmiffy128@gmail.com>
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
#include "feature.h"
#include "gamedata.h"
#include "hexcolour.h"
#include "hud.h"
#include "intdefs.h"
#include "vcall.h"

FEATURE("crosshair drawing")
REQUIRE(hud)

DECL_VFUNC_DYN(bool, IsInGame)

DEF_CVAR(sst_xhair, "Enable custom crosshair", 0, CON_ARCHIVE | CON_HIDDEN)
DEF_CVAR(sst_xhair_colour, "Colour for alternative crosshair (RGBA hex)",
		"FFFFFF", CON_ARCHIVE | CON_HIDDEN)
DEF_CVAR_MIN(sst_xhair_thickness, "Thickness of custom crosshair in pixels", 2,
		1, CON_ARCHIVE | CON_HIDDEN)
DEF_CVAR_MIN(sst_xhair_size, "Length of lines in custom crosshair in pixels", 8,
		0, CON_ARCHIVE | CON_HIDDEN)
DEF_CVAR_MIN(sst_xhair_gap, "Gap between lines in custom crosshair in pixels",
		16, 0, CON_ARCHIVE | CON_HIDDEN)
DEF_CVAR(sst_xhair_dot, "Whether to draw dot in middle of custom crosshair",
		1, CON_ARCHIVE | CON_HIDDEN)
DEF_CVAR(sst_xhair_outline, "Whether to draw outline around custom crosshair",
		0, CON_ARCHIVE | CON_HIDDEN)

static struct rgba colour = {255, 255, 255, 255};

static void colourcb(struct con_var *v) {
	hexcolour_rgba(colour.bytes, con_getvarstr(v));
}

static inline void drawrect(int x0, int y0, int x1, int y1, struct rgba colour,
		bool outline) {
	hud_drawrect(x0, y0, x1, y1, colour, true);
	if (outline) hud_drawrect(x0, y0, x1, y1, (struct rgba){.a = 255}, false);
}

HANDLE_EVENT(HudPaint, void) {
	if (!con_getvari(sst_xhair)) return;
	if (has_vtidx_IsInGame && engclient && !IsInGame(engclient)) return;
	int w, h;
	hud_screensize(&w, &h);
	int thick = con_getvari(sst_xhair_thickness);
	int thick1 = (thick + 1) / 2, thick2 = thick - thick1;
	int sz = con_getvari(sst_xhair_size);
	int gap = con_getvari(sst_xhair_gap);
	int gap1 = (gap + 1) / 2, gap2 = gap - gap1;
	int x = w / 2, y = h / 2;
	bool ol = !!con_getvari(sst_xhair_outline);
	if (sz) {
		drawrect(x - thick1, y - sz - gap1, x + thick2, y - gap1, colour, ol);
		drawrect(x - thick1, y + gap2, x + thick2, y + sz + gap2, colour, ol);
		drawrect(x - sz - gap1, y - thick1, x - gap1, y + thick2, colour, ol);
		drawrect(x + gap2, y - thick1, x + sz + gap2, y + thick2, colour, ol);
	}
	if (con_getvari(sst_xhair_dot) && (gap >= thick || ol)) {
		drawrect(x - thick1, y - thick1, x + thick2, y + thick2, colour, ol);
	}
}

INIT {
	sst_xhair->base.flags &= ~CON_HIDDEN;
	sst_xhair_colour->base.flags &= ~CON_HIDDEN;
	sst_xhair_colour->cb = &colourcb;
	sst_xhair_thickness->base.flags &= ~CON_HIDDEN;
	sst_xhair_size->base.flags &= ~CON_HIDDEN;
	sst_xhair_gap->base.flags &= ~CON_HIDDEN;
	sst_xhair_dot->base.flags &= ~CON_HIDDEN;
	sst_xhair_outline->base.flags &= ~CON_HIDDEN;
	return true;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
