/*
 * Copyright © 2022 Matthew Wozniak <sirtomato999@gmail.com>
 * Copyright © 2022 Willian Henrique <wsimanbrazil@yahoo.com.br>
 * Copyright © 2024 Michael Smith <mikesmiffy128@gmail.com
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

#include <math.h>

#include "con_.h"
#include "engineapi.h"
#include "event.h"
#include "errmsg.h"
#include "gamedata.h"
#include "gametype.h"
#include "hexcolour.h"
#include "hook.h"
#include "hud.h"
#include "intdefs.h"
#include "feature.h"
#include "mem.h"
#include "vcall.h"
#include "x86.h"
#include "x86util.h"

FEATURE("button input HUD")
REQUIRE_GAMEDATA(vtidx_CreateMove)
REQUIRE_GAMEDATA(vtidx_DecodeUserCmdFromBuffer)
REQUIRE_GAMEDATA(vtidx_GetUserCmd)
REQUIRE_GAMEDATA(vtidx_VClient_DecodeUserCmdFromBuffer)
REQUIRE_GLOBAL(factory_client)
REQUIRE(hud)

DEF_CVAR(sst_inputhud, "Enable button input HUD", 0, CON_ARCHIVE | CON_HIDDEN)
DEF_CVAR(sst_inputhud_bgcolour_normal,
		"Input HUD default key background colour (RGBA hex)", "4040408C",
		CON_ARCHIVE | CON_HIDDEN)
DEF_CVAR(sst_inputhud_bgcolour_pressed,
		"Input HUD pressed key background colour (RGBA hex)", "202020C8",
		CON_ARCHIVE | CON_HIDDEN)
DEF_CVAR(sst_inputhud_fgcolour, "Input HUD text colour (RGBA hex)", "F0F0F0FF",
		CON_ARCHIVE | CON_HIDDEN)
DEF_CVAR_MINMAX(sst_inputhud_scale, "Input HUD size (multiple of minimum)",
		1.5, 1, 4, CON_ARCHIVE | CON_HIDDEN)
DEF_CVAR_MINMAX(sst_inputhud_x,
		"Input HUD x position (fraction between screen left and right)",
		0.02, 0, 1, CON_ARCHIVE | CON_HIDDEN)
DEF_CVAR_MINMAX(sst_inputhud_y,
		"Input HUD y position (fraction between screen top and bottom)",
		0.95, 0, 1, CON_ARCHIVE | CON_HIDDEN)

static void *input;
static int heldbuttons = 0, tappedbuttons = 0;

static struct rgba colours[3] = {
	{64, 64, 64, 140},
	{16, 16, 16, 200},
	{240, 240, 240, 255}
};

static void colourcb(struct con_var *v) {
	if (v == sst_inputhud_bgcolour_normal) {
		hexcolour_rgba(colours[0].bytes, con_getvarstr(v));
	}
	else if (v == sst_inputhud_bgcolour_pressed) {
		hexcolour_rgba(colours[1].bytes, con_getvarstr(v));
	}
	else /* v == sst_inputhud_fg */ {
		hexcolour_rgba(colours[2].bytes, con_getvarstr(v));
	}
}

struct CUserCmd {
	void **vtable;
	int cmd, tick;
	struct vec3f angles;
	float fmove, smove, umove;
	int buttons;
	char impulse;
	int weaponselect, weaponsubtype;
	int rngseed;
	short mousedx, mousedy;
	// client only:
	bool predicted;
	struct CUtlVector *entgroundcontact;
};

#define vtidx_GetUserCmd_l4dbased vtidx_GetUserCmd
DECL_VFUNC_DYN(struct CUserCmd *, GetUserCmd, int)
DECL_VFUNC_DYN(struct CUserCmd *, GetUserCmd_l4dbased, int, int)

typedef void (*VCALLCONV CreateMove_func)(void *, int, float, bool);
static CreateMove_func orig_CreateMove;
static void VCALLCONV hook_CreateMove(void *this, int seq, float ft,
		bool active) {
	orig_CreateMove(this, seq, ft, active);
	struct CUserCmd *cmd = GetUserCmd(this, seq);
	// trick: to ensure every input (including scroll wheel) is displayed for at
	// least a frame, even at sub-tickrate framerates, we accumulate tapped
	// buttons with bitwise or. once these are drawn, tappedbuttons is cleared,
	// but heldbuttons maintains its state, so stuff doesn't flicker constantly
	if (cmd) { heldbuttons = cmd->buttons; tappedbuttons |= cmd->buttons; }
}
// basically a dupe, but calling the other version of GetUserCmd
static void VCALLCONV hook_CreateMove_l4dbased(void *this, int seq, float ft,
		bool active) {
	orig_CreateMove(this, seq, ft, active);
	struct CUserCmd *cmd = GetUserCmd_l4dbased(this, -1, seq);
	if (cmd) { heldbuttons = cmd->buttons; tappedbuttons |= cmd->buttons; }
}

typedef void (*VCALLCONV DecodeUserCmdFromBuffer_func)(void *, void *, int);
typedef void (*VCALLCONV DecodeUserCmdFromBuffer_l4dbased_func)(void *, int,
		void *, int);
static union {
	DecodeUserCmdFromBuffer_func prel4d;
	DecodeUserCmdFromBuffer_l4dbased_func l4dbased;
} _orig_DecodeUserCmdFromBuffer;
#define orig_DecodeUserCmdFromBuffer _orig_DecodeUserCmdFromBuffer.prel4d
#define orig_DecodeUserCmdFromBuffer_l4dbased \
		_orig_DecodeUserCmdFromBuffer.l4dbased
static void VCALLCONV hook_DecodeUserCmdFromBuffer(void *this, void *reader,
		int seq) {
	orig_DecodeUserCmdFromBuffer(this, reader, seq);
	struct CUserCmd *cmd = GetUserCmd(this, seq);
	if (cmd) { heldbuttons = cmd->buttons; tappedbuttons |= cmd->buttons; }
}
static void VCALLCONV hook_DecodeUserCmdFromBuffer_l4dbased(void *this,
		int slot, void *reader, int seq) {
	orig_DecodeUserCmdFromBuffer_l4dbased(this, slot, reader, seq);
	struct CUserCmd *cmd = GetUserCmd_l4dbased(this, slot, seq);
	if (cmd) { heldbuttons = cmd->buttons; tappedbuttons |= cmd->buttons; }
}

static inline int bsf(uint x) {
	// this should generate xor <ret>, <ret>; bsfl <ret>, <x>.
	// doing a straight bsf (e.g. via BitScanForward or __builtin_ctz) creates
	// a false dependency on many CPUs, which compilers don't understand somehow
	int ret = 0;
#if defined(__GNUC__) || defined(__clang__)
	__asm__ volatile (
		"bsfl %1, %0\n"
		: "+r" (ret)
		: "r" (x)
	);
	return ret;
#else
#error need some sort of inline asm, or a non-broken(!) bitscan intrinsic
#endif
}

// IMPORTANT: these things must all match the button order in engineapi.h
static const struct {
	hud_wchar *s;
	int len;
} text[] = {
	/* IN_ATTACK */		{L"Pri", 3},
	/* IN_JUMP */		{L"Jump", 4},
	/* IN_DUCK */		{L"Duck", 4},
	/* IN_FORWARD */	{L"Fwd", 3},
	/* IN_BACK */		{L"Back", 4},
	/* IN_USE */		{L"Use", 3},
	/* IN_CANCEL */		{0},
	/* IN_LEFT */		{L"LTurn", 5},
	/* IN_RIGHT */		{L"RTurn", 5},
	/* IN_MOVELEFT */	{L"Left", 4},
	/* IN_MOVERIGHT */	{L"Right", 5},
	/* IN_ATTACK2 */	{L"Sec", 3},
	/* IN_RUN */		{0},
	/* IN_RELOAD */		{L"Rld", 3},
	/* IN_ALT1 */		{0},
	/* IN_ALT2 */		{0},
	/* IN_SCORE */		{0},
	/* IN_SPEED */		{L"Speed", 5},
	/* IN_WALK */		{L"Walk", 4},
	/* IN_ZOOM */		{L"Zoom", 4}
	// ignoring the rest
};

struct layout {
	int mask;
	schar w, h;
	struct { schar x, y; } pos[20]; // XXX: should make flexible???
};

// input layouts (since some games don't use all input bits) {{{

static const struct layout layout_hl2 = {
	IN_ATTACK | IN_JUMP | IN_DUCK | IN_FORWARD | IN_BACK | IN_USE | IN_LEFT |
	IN_RIGHT | IN_MOVELEFT | IN_MOVERIGHT | IN_ATTACK2 | IN_RELOAD | IN_SPEED |
	IN_WALK | IN_ZOOM,
	15, 6,
	{
		//    F      1 2
		//  L B R   U R Z
		// W S D J   l r
		/* IN_ATTACK */		{10, 0},	/* IN_JUMP */		{ 6, 4},
		/* IN_DUCK */		{ 4, 4},	/* IN_FORWARD */	{ 3, 0},
		/* IN_BACK */		{ 3, 2},	/* IN_USE */		{ 9, 2},
		/* IN_CANCEL */		{0},		/* IN_LEFT */		{10, 4},
		/* IN_RIGHT */		{12, 4},	/* IN_MOVELEFT */	{ 1, 2},
		/* IN_MOVERIGHT */	{ 5, 2},	/* IN_ATTACK2 */	{12, 0},
		/* IN_RUN */		{0},		/* IN_RELOAD */		{11, 2},
		/* IN_ALT1 */		{0},		/* IN_ALT2 */		{0},
		/* IN_SCORE */		{0},		/* IN_SPEED */		{ 2, 4},
		/* IN_WALK */		{ 0, 4},	/* IN_ZOOM */		{13, 2}
	}
};

static const struct layout layout_portal1 = {
	IN_ATTACK | IN_JUMP | IN_DUCK | IN_FORWARD | IN_BACK | IN_USE | IN_LEFT |
	IN_RIGHT | IN_MOVELEFT | IN_MOVERIGHT | IN_ATTACK2,
	11, 6,
	{
		//   F    1 2
		// L B R   U
		//  D J   l r
		/* IN_ATTACK */		{7, 0},		/* IN_JUMP */		{3, 4},
		/* IN_DUCK */		{1, 4},		/* IN_FORWARD */	{2, 0},
		/* IN_BACK */		{2, 2},		/* IN_USE */		{8, 2},
		/* IN_CANCEL */		{0},		/* IN_LEFT */		{7, 4},
		/* IN_RIGHT */		{9, 4},		/* IN_MOVELEFT */	{0, 2},
		/* IN_MOVERIGHT */	{4, 2},		/* IN_ATTACK2 */	{9, 0}
	}
};

// TODO(compat): add portal2 layout once there's hud gamedata for portal 2
//static const struct layout layout_portal2 = {
//	IN_ATTACK | IN_JUMP | IN_DUCK | IN_FORWARD | IN_BACK | IN_USE | IN_LEFT |
//	IN_RIGHT | IN_MOVELEFT | IN_MOVERIGHT | IN_ATTACK2 | IN_ZOOM,
//	11, 6,
//	{
//		//   F    1 2
//		// L B R  U Z
//		//  D J   l r
//		/* IN_ATTACK */		{7, 0},		/* IN_JUMP */		{3, 4},
//		/* IN_DUCK */		{1, 4},		/* IN_FORWARD */	{2, 0},
//		/* IN_BACK */		{2, 2},		/* IN_USE */		{7, 2},
//		/* IN_CANCEL */		{0},		/* IN_LEFT */		{7, 4},
//		/* IN_RIGHT */		{9, 4},		/* IN_MOVELEFT */	{0, 2},
//		/* IN_MOVERIGHT */	{4, 2},		/* IN_ATTACK2 */	{9, 0},
//		/* IN_RUN */		{0},		/* IN_RELOAD */		{0},
//		/* IN_ALT1 */		{0},		/* IN_ALT2 */		{0},
//		/* IN_SCORE */		{0},		/* IN_SPEED */		{0},
//		/* IN_WALK */		{0},		/* IN_ZOOM */		{9, 2}
//	}
//};

static const struct layout layout_l4d = {
	IN_ATTACK | IN_JUMP | IN_DUCK | IN_FORWARD | IN_BACK | IN_USE | IN_LEFT |
	IN_RIGHT | IN_MOVELEFT | IN_MOVERIGHT | IN_ATTACK2 | IN_SPEED | IN_ZOOM,
	11, 6,
	{
		//   F    1 2
		// L B R  U Z
		// W D J  l r
		/* IN_ATTACK */		{7, 0},		/* IN_JUMP */		{4, 4},
		/* IN_DUCK */		{2, 4},		/* IN_FORWARD */	{2, 0},
		/* IN_BACK */		{2, 2},		/* IN_USE */		{7, 2},
		/* IN_CANCEL */		{0},		/* IN_LEFT */		{7, 4},
		/* IN_RIGHT */		{9, 4},		/* IN_MOVELEFT */	{0, 2},
		/* IN_MOVERIGHT */	{4, 2},		/* IN_ATTACK2 */	{9, 0},
		/* IN_RUN */		{0},		/* IN_RELOAD */		{0},
		/* IN_ALT1 */		{0},		/* IN_ALT2 */		{0},
		/* IN_SCORE */		{0},		/* IN_SPEED */		{0, 4},
		/* IN_WALK */		{0},		/* IN_ZOOM */		{9, 2}
	}
};

// }}}

static const struct layout *layout = &layout_hl2;

static const char *const fontnames[] = {
	"DebugFixedSmall",
	"HudSelectionText",
	"CommentaryDefault",
	"DefaultVerySmall",
	"DefaultSmall",
	"Default"
};
static struct { ulong h; int sz; } fonts[countof(fontnames)];

HANDLE_EVENT(HudPaint, void) {
	if (!con_getvari(sst_inputhud)) return;
	int screenw, screenh;
	hud_screensize(&screenw, &screenh);
	int basesz = screenw > screenh ? screenw : screenh;
	int boxsz = ceilf(basesz * 0.025f);
	if (boxsz < 24) boxsz = 24;
	boxsz *= con_getvarf(sst_inputhud_scale);
	int idealfontsz = boxsz - 8; // NOTE: this is overall text width, see INIT
	ulong font = 0; int fontsz = 0;
	// get the biggest font that'll fit the box
	// XXX: can/should we avoid doing this every frame?
	for (int i = 0; i < countof(fonts); ++i) {
		// XXX: fonts aren't sorted... should we bother?
		if_cold (!fonts[i].h) continue;
		if (fonts[i].sz < fontsz) continue;
		if (fonts[i].sz <= idealfontsz) font = fonts[i].h;
		//else break; // not sorted
	}
	int gap = (boxsz | 32) >> 5; // minimum 1 pixel gap
	int w = (boxsz + gap) * layout->w / 2 - gap;
	int h = (boxsz + gap) * layout->h / 2 - gap;
	int basex = roundf(con_getvarf(sst_inputhud_x) * (screenw - w));
	int basey = roundf(con_getvarf(sst_inputhud_y) * (screenh - h));
	int buttons = heldbuttons | tappedbuttons;
	for (int mask = layout->mask, bitidx, bit; mask; mask ^= bit) {
		bitidx = bsf(mask); bit = 1 << bitidx;
		// divide sizes by 2 here to allow in-between positioning
		int x = basex + layout->pos[bitidx].x * (boxsz + gap) / 2;
		int y = basey + layout->pos[bitidx].y * (boxsz + gap) / 2;
		hud_drawrect(x, y, x + boxsz, y + boxsz,
				colours[!!(buttons & bit)], true);
		if_hot (font) {
			int tw, th;
			hud_textsize(font, text[bitidx].s, &tw, &th);
			hud_drawtext(font, x + (boxsz - tw) / 2, y + (boxsz - th) / 2,
					colours[2], text[bitidx].s, text[bitidx].len);
		}
	}
	tappedbuttons = 0;
}

// find the CInput "input" global
static inline bool find_input(void* vclient) {
#ifdef _WIN32
	// the only CHLClient::DecodeUserCmdFromBuffer() does is call a virtual
	// function, so find its thisptr being loaded into ECX
	void* decodeusercmd =
		(*(void***)vclient)[vtidx_VClient_DecodeUserCmdFromBuffer];
	for (uchar *p = (uchar *)decodeusercmd; p - (uchar *)decodeusercmd < 32;) {
		if (p[0] == X86_MOVRMW && p[1] == X86_MODRM(0, 1, 5)) {
			void **indirect = mem_loadptr(p + 2);
			input = *indirect;
			return true;
		}
		NEXT_INSN(p, "input object");
	}
#else
#warning TODO(linux): implement linux equivalent (see demorec.c)
#endif
	return false;
}

INIT {
	void *vclient;
	if (!(vclient = factory_client("VClient015", 0)) &&
			!(vclient = factory_client("VClient016", 0)) &&
			!(vclient = factory_client("VClient017", 0))) {
		errmsg_errorx("couldn't get client interface");
		return false;
	}
	if (!find_input(vclient)) {
		errmsg_errorx("couldn't find input global");
		return false;
	}
	for (int i = 0; i < countof(fontnames); ++i) {
		fonts[i].h = hud_getfont(fontnames[i], true);
		if (!fonts[i].h) {
			errmsg_warnx("couldn't get \"%s\" font", fontnames[i]);
		}
		else {
			int dummy;
			// use (roughly) the widest string as a reference for what will fit
			hud_textsize(fonts[i].h, L"Speed", &fonts[i].sz, &dummy);
		}
	}
	void **vtable = mem_loadptr(input);
	// just unprotect the first few pointers (GetUserCmd is 8)
	if (!os_mprot(vtable, sizeof(void *) * 8, PAGE_READWRITE)) {
		errmsg_errorsys("couldn't make virtual table writable");
		return false;
	}
	if (GAMETYPE_MATCHES(L4Dbased)) {
		orig_CreateMove = (CreateMove_func)hook_vtable(vtable, vtidx_CreateMove,
				(void *)&hook_CreateMove_l4dbased);
		orig_DecodeUserCmdFromBuffer = (DecodeUserCmdFromBuffer_func)hook_vtable(
				vtable, vtidx_DecodeUserCmdFromBuffer,
				(void *)&hook_DecodeUserCmdFromBuffer_l4dbased);
	}
	else {
		orig_CreateMove = (CreateMove_func)hook_vtable(vtable, vtidx_CreateMove,
				(void *)&hook_CreateMove);
		orig_DecodeUserCmdFromBuffer = (DecodeUserCmdFromBuffer_func)hook_vtable(
				vtable, vtidx_DecodeUserCmdFromBuffer,
				(void *)&hook_DecodeUserCmdFromBuffer);
	}

	if (GAMETYPE_MATCHES(Portal1)) layout = &layout_portal1;
	//else if (GAMETYPE_MATCHES(Portal2)) layout = &layout_portal2;
	else if (GAMETYPE_MATCHES(L4D)) layout = &layout_l4d;
	// TODO(compat): more game-specific layouts!

	sst_inputhud->base.flags &= ~CON_HIDDEN;
	sst_inputhud_scale->base.flags &= ~CON_HIDDEN;
	sst_inputhud_bgcolour_normal->base.flags &= ~CON_HIDDEN;
	sst_inputhud_bgcolour_normal->cb = &colourcb;
	sst_inputhud_bgcolour_pressed->base.flags &= ~CON_HIDDEN;
	sst_inputhud_bgcolour_pressed->cb = &colourcb;
	sst_inputhud_fgcolour->base.flags &= ~CON_HIDDEN;
	sst_inputhud_fgcolour->cb = &colourcb;
	sst_inputhud_x->base.flags &= ~CON_HIDDEN;
	sst_inputhud_y->base.flags &= ~CON_HIDDEN;

	// HACK: default HUD position would clash with L4D player health HUDs and
	// HL2 sprint HUD, so move it up. this currently has to be done in a super
	// crappy, nasty way to get the defaults to display right in the console...
	// TODO(opt): move PREINIT stuff to before cvar init and avoid this nonsense
	if (GAMETYPE_MATCHES(L4D)) {
		sst_inputhud_y->defaultval = "0.82";
		con_setvarstr(sst_inputhud_y, "0.82");
	}
	else if (GAMETYPE_MATCHES(HL2series)) {
		sst_inputhud_y->defaultval = "0.75";
		con_setvarstr(sst_inputhud_y, "0.75");
	}

	return true;
}

END {
	void **vtable = mem_loadptr(input);
	unhook_vtable(vtable, vtidx_CreateMove, (void *)orig_CreateMove);
	// N.B.: since the orig_ function is in a union, we don't have to worry
	// about which version we're unhooking
	unhook_vtable(vtable, vtidx_DecodeUserCmdFromBuffer,
			(void *)orig_DecodeUserCmdFromBuffer);
}

// vi: sw=4 ts=4 noet tw=80 cc=80 fdm=marker