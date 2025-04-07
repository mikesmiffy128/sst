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

#ifndef INC_HUD_H
#define INC_HUD_H

#include "event.h"
#include "engineapi.h"
#include "intdefs.h"

// ugh!
#ifdef _WIN32
typedef ushort hud_wchar;
#else
typedef int hud_wchar;
#endif

/*
 * Emitted when the game HUD is being drawn. Allows features to draw their own
 * additional overlays atop the game's standard HUD.
 */
DECL_EVENT(HudPaint, int /*width*/, int /*height*/)

/* Font style flags */
#define HUD_FONT_ITALIC 1
#define HUD_FONT_UNDERLINE 2
#define HUD_FONT_STRIKE 4
#define HUD_FONT_SYMBOL 8
#define HUD_FONT_AA 16
#define HUD_FONT_GAUSSBLUR 32
#define HUD_FONT_ROTARY 64
#define HUD_FONT_DROPSHADOW 128
#define HUD_FONT_ADDITIVE 256
#define HUD_FONT_OUTLINE 512
#define HUD_FONT_CUSTOM 1024
#define HUD_FONT_BITMAP 2048

/* Gets a font handle by its name in sourcescheme.res. */
ulong hud_getfont(const char *name, bool proportional);

/* Sets the drawing pen colour for subsequent HUD drawing calls (below). */
void hud_setcolour(struct rgba colour);

/* Draws a rectangle on top of the HUD. */
void hud_drawrect(int x0, int y0, int x1, int y1, struct rgba colour, bool fill);

/* Draws a line on top of the HUD. */
void hud_drawline(int x0, int y0, int x1, int y1, struct rgba colour);

/* Draws an arbitrary series of lines between an array of points. */
void hud_drawpolyline(int *xs, int *ys, int npoints, struct rgba colour);

/* Draws text using a given font handle. */
void hud_drawtext(ulong font, int x, int y, struct rgba colour, hud_wchar *str,
		int len);

/* Gets the width and height of the game window in pixels. */
void hud_screensize(int *width, int *height);

/* Returns the height of a font, in pixels. */
int hud_fontheight(ulong font);

/* Returns the width of a font character, in pixels. */
int hud_charwidth(ulong font, hud_wchar ch);

/* Gets the width and height of string s, in pixels, using the given font. */
void hud_textsize(ulong font, const ushort *s, int *width, int *height);

#endif
