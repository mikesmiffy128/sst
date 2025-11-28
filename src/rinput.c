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

// NOTE: compiled on Windows only. All Linux Source releases are new enough to
// have raw input already.
// TODO(linux): actually, we DO want the scaling on Linux, so we need offsets
// for GetRawMouseAccumulators, etc.

#include <Windows.h>

#include "con_.h"
#include "engineapi.h"
#include "errmsg.h"
#include "feature.h"
#include "gamedata.h"
#include "hook.h"
#include "intdefs.h"
#include "langext.h"
#include "mem.h"
#include "sst.h"
#include "vcall.h"

FEATURE("scalable raw mouse input")

// We reimplement m_rawinput by hooking cursor functions in the same way as
// RInput (it's way easier than replacing all the mouse-handling internals of
// the actual engine). We also take the same window class it does in order to
// either block it from being loaded redundantly, or be blocked if it's already
// loaded. If m_rawinput already exists, we do nothing; people should use the
// game's native raw input instead in that case.
//
// As an *additional* feature, we also implement hardware input scaling, meaning
// that some number of counts are required from the mouse in order to move the
// cursor a single unit in game. This is useful for doing "minisnaps" (imprecise
// but accurate mouse movements at high sensitivity) on mice which don't allow
// their CPI to be lowered very far. It's implemented either in our own raw
// input functionality, or by hooking the game's, as required.

#define USAGEPAGE_MOUSE 1
#define USAGE_MOUSE 2

static int cx, cy, rx = 0, ry = 0; // cursor xy, remainder xy
static union { // space saving
	void *inwin;
	void **vtable_insys;
} U;
#define inwin U.inwin
#define vtable_insys U.vtable_insys

DEF_CVAR_UNREG(m_rawinput, "Use Raw Input for mouse input (SST reimplementation)",
		0, CON_ARCHIVE | CON_INIT_HIDDEN)

DEF_CVAR_MINMAX(sst_mouse_factor, "Number of hardware mouse counts per step",
		1, 1, 100, /*CON_ARCHIVE |*/ CON_INIT_HIDDEN)

static ssize __stdcall inproc(void *wnd, uint msg, usize wp, ssize lp) {
	switch (msg) {
		case WM_INPUT:
			char buf[ssizeof(RAWINPUTHEADER) + ssizeof(RAWMOUSE) /* = 40 */];
			uint sz = sizeof(buf);
			if_hot (GetRawInputData((void *)lp, RID_INPUT, buf, &sz,
					sizeof(RAWINPUTHEADER)) != -1) {
				RAWINPUT *ri = (RAWINPUT *)buf;
				if_hot (ri->header.dwType == RIM_TYPEMOUSE) {
					int d = con_getvari(sst_mouse_factor);
					int dx = rx + ri->data.mouse.lLastX;
					int dy = ry + ri->data.mouse.lLastY;
					cx += dx / d; cy += dy / d; rx = dx % d; ry = dy % d;
				}
			}
			return 0;
		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;
	}
	return DefWindowProc(wnd, msg, wp, lp);
}

typedef int (*__stdcall GetCursorPos_func)(POINT *p);
typedef uint (*VCALLCONV GetRawMouseAccumulators_func)(void *, int *, int *);
static union { // more cheeky space saving
	GetCursorPos_func orig_GetCursorPos;
	GetRawMouseAccumulators_func orig_GetRawMouseAccumulators;
} u2;
#define orig_GetCursorPos u2.orig_GetCursorPos
#define orig_GetRawMouseAccumulators u2.orig_GetRawMouseAccumulators

static int __stdcall hook_GetCursorPos(POINT *p) {
	if (!con_getvari(m_rawinput)) return orig_GetCursorPos(p);
	p->x = cx; p->y = cy;
	return 1;
}

typedef int (*__stdcall SetCursorPos_func)(int x, int y);
static SetCursorPos_func orig_SetCursorPos = 0;
static int __stdcall hook_SetCursorPos(int x, int y) {
	cx = x; cy = y;
	return orig_SetCursorPos(x, y);
}

static uint VCALLCONV hook_GetRawMouseAccumulators(void *this, int *x, int *y) {
	int dx, dy;
	uint ret = orig_GetRawMouseAccumulators(this, &dx, &dy);
	int d = con_getvari(sst_mouse_factor);
	dx += rx; dy += ry;
	*x = dx / d; *y = dy / d; rx = dx % d; ry = dy % d;
	// NOTE! This is usually void, but apparently returns a bool in the 2013
	// SDK, for reasons I didn't bother researching. In any case, we can just
	// unconditionally preserve EAX and it won't do any harm.
	return ret;
}

INIT {
	bool has_rawinput = !!con_findvar("m_rawinput");
	if (has_rawinput) {
		if (!has_vtidx_GetRawMouseAccumulators) return FEAT_INCOMPAT;
		if (!inputsystem) return FEAT_INCOMPAT;
		vtable_insys = mem_loadptr(inputsystem);
		// XXX: this is kind of duping nosleep, but that won't always init...
		if_cold (!os_mprot(vtable_insys + vtidx_GetRawMouseAccumulators,
				ssizeof(void *), PAGE_READWRITE)) {
			errmsg_errorx("couldn't make virtual table writable");
			return FEAT_FAIL;
		}
		orig_GetRawMouseAccumulators = (GetRawMouseAccumulators_func)hook_vtable(
				vtable_insys, vtidx_GetRawMouseAccumulators,
				(void *)&hook_GetRawMouseAccumulators);
	}
	else {
		// create cvar hidden so config is still preserved if we fail to init
		con_regvar(m_rawinput);
	}
	WNDCLASSEXW wc = {
		.cbSize = sizeof(wc),
		// cast because inproc is binary-compatible but doesn't use stupid
		// microsoft typedefs
		.lpfnWndProc = (WNDPROC)&inproc,
		.lpszClassName = L"RInput"
	};
	if_cold (!RegisterClassExW(&wc)) {
		struct rgba gold = {255, 210, 0, 255};
		struct rgba blue = {45, 190, 190, 255};
		struct rgba white = {200, 200, 200, 255};
		con_colourmsg(&gold, "SST PROTIP! ");
		con_colourmsg(&blue, "It appears you're using RInput.exe.\n"
				"Consider launching without that and using ");
		con_colourmsg(&gold, "m_rawinput 1");
		con_colourmsg(&blue, " instead!\n");
		if_cold (has_rawinput) { // slow path because this'd be kinda weird!
			con_colourmsg(&white, "This is built into this version of game, and"
					" will also get provided by SST in older versions. ");
		}
		else {
			con_colourmsg(&white, "This option carries over to newer game "
				"versions that have it built-in. ");
		}
		con_colourmsg(&white, "No need for external programs :)\n");
		con_colourmsg(&gold, "Additionally");
		con_colourmsg(&blue, ", you can scale down the sensor input with ");
		con_colourmsg(&gold, "sst_mouse_factor");
		con_colourmsg(&blue, "!\n");
		return FEAT_INCOMPAT;
	}
	if (has_rawinput) {
		// no real reason to keep this around receiving useless window messages
		UnregisterClassW(L"RInput", 0);
		goto ok;
	}

	int err;
	struct hook_inline_featsetup_ret h1 = hook_inline_featsetup(
			(void *)GetCursorPos, (void **)&orig_GetCursorPos, "GetCursorPos");
	if_cold (err = h1.err) goto e0;
	struct hook_inline_featsetup_ret h2 = hook_inline_featsetup(
			(void *)SetCursorPos, (void **)&orig_SetCursorPos, "SetCursorPos");
	if_cold (err = h2.err) goto e0;
	inwin = CreateWindowExW(0, L"RInput", L"RInput", 0, 0, 0, 0, 0, 0, 0, 0, 0);
	if_cold (!inwin) {
		errmsg_errorsys("couldn't create input window");
		goto e0;
	}
	RAWINPUTDEVICE rd = {
		.hwndTarget = inwin,
		.usUsagePage = USAGEPAGE_MOUSE,
		.usUsage = USAGE_MOUSE
	};
	if_cold (!RegisterRawInputDevices(&rd, 1, sizeof(rd))) {
		errmsg_errorsys("couldn't create raw mouse device");
		err = FEAT_FAIL;
		goto e1;
	}
	hook_inline_commit(h1.prologue, (void *)&hook_GetCursorPos);
	hook_inline_commit(h2.prologue, (void *)&hook_SetCursorPos);

ok:	// XXX: this is a little tricky and a little clunky. we have registered
	// m_rawinput above but sst_mouse_factor will get auto-registered after init
	// returns, so the flags are different.
	con_unhide(&m_rawinput->base);
	sst_mouse_factor->base.flags &= ~CON_INIT_HIDDEN;
	return FEAT_OK;

e1:	DestroyWindow(inwin);
e0:	UnregisterClassW(L"RInput", 0);
	return err;
}

END {
	if_hot (orig_SetCursorPos) { // we inited our own implementation
		RAWINPUTDEVICE rd = {
			.dwFlags = RIDEV_REMOVE,
			.hwndTarget = 0,
			.usUsagePage = USAGEPAGE_MOUSE,
			.usUsage = USAGE_MOUSE
		};
		RegisterRawInputDevices(&rd, 1, sizeof(rd));
		DestroyWindow(inwin);
		if_hot (!sst_userunloaded) return;
		UnregisterClassW(L"RInput", 0);
		unhook_inline((void *)orig_GetCursorPos);
		unhook_inline((void *)orig_SetCursorPos);
	}
	else if_cold (sst_userunloaded) {
		// we must have hooked the *existing* implementation
		unhook_vtable(vtable_insys, vtidx_GetRawMouseAccumulators,
				(void *)orig_GetRawMouseAccumulators);
	}
}

// vi: sw=4 ts=4 noet tw=80 cc=80
