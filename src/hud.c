/*
 * Copyright © Matthew Wozniak <sirtomato999@gmail.com>
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

#include "engineapi.h"
#include "errmsg.h"
#include "event.h"
#include "feature.h"
#include "gamedata.h"
#include "hook.h"
#include "hud.h"
#include "intdefs.h"
#include "langext.h"
#include "mem.h"
#include "os.h"
#include "sst.h"
#include "vcall.h"
#include "x86.h"
#include "x86util.h"

FEATURE()
REQUIRE_GLOBAL(factory_engine)
REQUIRE_GLOBAL(vgui)
// ISurface
REQUIRE_GAMEDATA(vtidx_DrawSetColor)
REQUIRE_GAMEDATA(vtidx_DrawFilledRect)
REQUIRE_GAMEDATA(vtidx_DrawOutlinedRect)
REQUIRE_GAMEDATA(vtidx_DrawLine)
REQUIRE_GAMEDATA(vtidx_DrawPolyLine)
REQUIRE_GAMEDATA(vtidx_DrawSetTextFont)
REQUIRE_GAMEDATA(vtidx_DrawSetTextColor)
REQUIRE_GAMEDATA(vtidx_DrawSetTextPos)
REQUIRE_GAMEDATA(vtidx_DrawPrintText)
REQUIRE_GAMEDATA(vtidx_GetScreenSize)
REQUIRE_GAMEDATA(vtidx_GetFontTall)
REQUIRE_GAMEDATA(vtidx_GetCharacterWidth)
// CEngineVGui
REQUIRE_GAMEDATA(vtidx_GetPanel)
// vgui::Panel
REQUIRE_GAMEDATA(vtidx_SetPaintEnabled)
REQUIRE_GAMEDATA(vtidx_Paint)
// ISchemeManager
REQUIRE_GAMEDATA(vtidx_GetIScheme)
// IScheme
REQUIRE_GAMEDATA(vtidx_GetFont)

DEF_EVENT(HudPaint, int /*width*/, int /*height*/)

// we just use ulongs for API, but keep a struct for vcalls to ensure we get the
// right calling convention (x86 Windows/MSVC is funny about passing structs...)
struct handlewrap { ulong x; };

struct CEngineVGui;
DECL_VFUNC_DYN(struct CEngineVGui, unsigned int, GetPanel, int)

struct ISchemeManager;
DECL_VFUNC_DYN(struct ISchemeManager, void *, GetIScheme, struct handlewrap)
struct IScheme;
DECL_VFUNC_DYN(struct IScheme, struct handlewrap, GetFont, const char *, bool)

struct ISurface;
DECL_VFUNC_DYN(struct ISurface, void, DrawSetColor, struct rgba)
DECL_VFUNC_DYN(struct ISurface, void, DrawFilledRect, int, int, int, int)
DECL_VFUNC_DYN(struct ISurface, void, DrawOutlinedRect, int, int, int, int)
DECL_VFUNC_DYN(struct ISurface, void, DrawLine, int, int, int, int)
DECL_VFUNC_DYN(struct ISurface, void, DrawPolyLine, int *, int *, int)
DECL_VFUNC_DYN(struct ISurface, void, DrawSetTextFont, struct handlewrap)
DECL_VFUNC_DYN(struct ISurface, void, DrawSetTextColor, struct rgba)
DECL_VFUNC_DYN(struct ISurface, void, DrawSetTextPos, int, int)
DECL_VFUNC_DYN(struct ISurface, void, DrawPrintText, hud_wchar *, int, int)
DECL_VFUNC_DYN(struct ISurface, void, GetScreenSize, int *, int *)
DECL_VFUNC_DYN(struct ISurface, int, GetFontTall, struct handlewrap)
DECL_VFUNC_DYN(struct ISurface, int, GetCharacterWidth, struct handlewrap, int)
DECL_VFUNC_DYN(struct ISurface, int, GetTextSize, struct handlewrap,
		const hud_wchar *, int *, int *)

struct IPanel { void **vtable; };
DECL_VFUNC_DYN(struct IPanel, void, SetPaintEnabled, bool)

static struct ISurface *matsurf;
static struct IPanel *toolspanel;
static struct IScheme *scheme;

typedef void (*VCALLCONV Paint_func)(struct IPanel *);
static Paint_func orig_Paint;
void VCALLCONV hook_Paint(struct IPanel *this) {
	if (this == toolspanel) {
		int width, height;
		hud_screensize(&width, &height);
		EMIT_HudPaint(width, height);
	}
	orig_Paint(this);
}

ulong hud_getfont(const char *name, bool proportional) {
	return GetFont(scheme, name, proportional).x;
}

void hud_drawrect(int x0, int y0, int x1, int y1, struct rgba colour,
		bool fill) {
	DrawSetColor(matsurf, colour);
	if (fill) DrawFilledRect(matsurf, x0, y0, x1, y1);
	else DrawOutlinedRect(matsurf, x0, y0, x1, y1);
}

void hud_drawline(int x0, int y0, int x1, int y1, struct rgba colour) {
	DrawSetColor(matsurf, colour);
	DrawLine(matsurf, x0, y0, x1, y1);
}

void hud_drawpolyline(int *x, int *y, int npoints, struct rgba colour) {
	DrawSetColor(matsurf, colour);
	DrawPolyLine(matsurf, x, y, npoints);
}

void hud_drawtext(ulong font, int x, int y, struct rgba colour, hud_wchar *str,
		int len) {
	DrawSetTextFont(matsurf, (struct handlewrap){font});
	DrawSetTextPos(matsurf, x, y);
	DrawSetTextColor(matsurf, colour);
	DrawPrintText(matsurf, str, len, /*FONT_DRAW_DEFAULT*/ 0);
}

void hud_screensize(int *width, int *height) {
	GetScreenSize(matsurf, width, height);
}

int hud_fontheight(ulong font) {
	return GetFontTall(matsurf, (struct handlewrap){font});
}

int hud_charwidth(ulong font, hud_wchar ch) {
	return GetCharacterWidth(matsurf, (struct handlewrap){font}, ch);
}

void hud_textsize(ulong font, const ushort *s, int *width, int *height) {
	GetTextSize(matsurf, (struct handlewrap){font}, s, width, height);
}

static bool find_toolspanel(struct CEngineVGui *enginevgui) {
	const uchar *insns = enginevgui->vtable[vtidx_GetPanel];
	for (const uchar *p = insns; p - insns < 16;) {
		// first CALL instruction in GetPanel calls GetRootPanel, which gives a
		// pointer to the specified panel
		if (p[0] == X86_CALL) {
			typedef void *(*VCALLCONV GetRootPanel_func)(void *this, int);
			int off = mem_loads32(p + 1);
			GetRootPanel_func GetRootPanel = (GetRootPanel_func)(p + 5 + off);
			toolspanel = GetRootPanel(enginevgui, /*PANEL_TOOLS*/ 3);
			return true;
		}
		NEXT_INSN(p, "GetRootPanel function");
	}
	return false;
}

INIT {
	if (!(matsurf = factory_engine("MatSystemSurface006", 0)) &&
			!(matsurf = factory_engine("MatSystemSurface008", 0))) {
		errmsg_errorx("couldn't get MatSystemSurface interface");
		return FEAT_INCOMPAT;
	}
	struct ISchemeManager *schememgr = factory_engine("VGUI_Scheme010", 0);
	if_cold (!schememgr) {
		errmsg_errorx("couldn't get VGUI_Scheme010 interface");
		return FEAT_INCOMPAT;
	}
	if_cold (!find_toolspanel(vgui)) {
		errmsg_errorx("couldn't find engine tools panel");
		return FEAT_INCOMPAT;
	}
	if_cold (!os_mprot(toolspanel->vtable + vtidx_Paint, sizeof(void *),
			PAGE_READWRITE)) {
		errmsg_errorsys("couldn't make virtual table writable");
		return FEAT_FAIL;
	}
	orig_Paint = (Paint_func)hook_vtable(toolspanel->vtable, vtidx_Paint,
			(void *)&hook_Paint);
	SetPaintEnabled(toolspanel, true);
	// 1 is the default, first loaded scheme. should always be sourcescheme.res
	scheme = GetIScheme(schememgr, (struct handlewrap){1});
	return FEAT_OK;
}

END {
	// don't unhook toolspanel if exiting: it's already long gone!
	if_cold (sst_userunloaded) {
		unhook_vtable(toolspanel->vtable, vtidx_Paint, (void *)orig_Paint);
		SetPaintEnabled(toolspanel, false);
	}
}

// vi: sw=4 ts=4 noet tw=80 cc=80
