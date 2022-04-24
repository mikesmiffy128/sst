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

// NOTE: compiled on Windows only. All Linux Source releases are new enough to
// have raw input already.

#include <stdbool.h>
#include <Windows.h>

#include "con_.h"
#include "hook.h"
#include "intdefs.h"

// We reimplement m_rawinput by hooking cursor functions in the same way as
// RInput (it's way easier than replacing all the mouse-handling internals of
// the actual engine). We also take the same window class it does in order to
// either block it from being loaded redundantly, or be blocked if it's already
// loaded. If m_rawinput already exists, we do nothing; people should use the
// game's native raw input instead in that case.

#define ERR "sst: rinput: error: "

#define USAGEPAGE_MOUSE 1
#define USAGE_MOUSE 2

static long dx = 0, dy = 0;
static void *inwin;

DEF_CVAR_UNREG(m_rawinput, "Use Raw Input for mouse input (SST reimplementation)",
		0, CON_ARCHIVE | CON_HIDDEN)

static ssize __stdcall inproc(void *wnd, uint msg, ssize wp, ssize lp) {
	switch (msg) {
		case WM_INPUT:;
			char buf[sizeof(RAWINPUTHEADER) + sizeof(RAWMOUSE) /* = 40 */];
			uint sz = sizeof(buf);
			if (GetRawInputData((void *)lp, RID_INPUT, buf, &sz,
					sizeof(RAWINPUTHEADER)) != -1) {
				RAWINPUT *ri = (RAWINPUT *)buf;
				if (ri->header.dwType == RIM_TYPEMOUSE) {
					dx += ri->data.mouse.lLastX;
					dy += ri->data.mouse.lLastY;
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
static GetCursorPos_func orig_GetCursorPos;
static int __stdcall hook_GetCursorPos(POINT *p) {
	if (!con_getvari(m_rawinput)) return orig_GetCursorPos(p);
	p->x = dx; p->y = dy;
	return 0;
}
typedef int (*__stdcall SetCursorPos_func)(int x, int y);
static SetCursorPos_func orig_SetCursorPos;
static int __stdcall hook_SetCursorPos(int x, int y) {
	dx = x; dy = y;
	return orig_SetCursorPos(x, y);
}

bool rinput_init(void) {
	if (con_findvar("m_rawinput")) return false; // no need!
	// create cvar hidden so if we fail to init, setting can still be preserved
	con_reg(m_rawinput);

	WNDCLASSEXW wc = {
		.cbSize = sizeof(wc),
		// cast because inproc is binary-compatible but doesn't use stupid
		// microsoft typedefs
		.lpfnWndProc = (WNDPROC)&inproc,
		.lpszClassName = L"RInput"
	};
	if (!RegisterClassExW(&wc)) {
		struct con_colour gold = {255, 210, 0, 255};
		struct con_colour blue = {45, 190, 190, 255};
		struct con_colour white = {200, 200, 200, 255};
		con_colourmsg(&gold, "SST PROTIP! ");
		con_colourmsg(&blue, "It appears you're using RInput.exe.\n"
				"Consider launching without that and using ");
		con_colourmsg(&gold, "m_rawinput 1");
		con_colourmsg(&blue, " instead!\n");
		con_colourmsg(&white, "This option carries over to newer game versions "
				"that have it built-in. No need for external programs :)\n");
		return false;
	}

	orig_GetCursorPos = (GetCursorPos_func)hook_inline((void *)&GetCursorPos,
			(void *)&hook_GetCursorPos);
	if (!orig_GetCursorPos) {
		con_warn(ERR "couldn't hook GetCursorPos\n");
		goto e0;
	}
	orig_SetCursorPos = (SetCursorPos_func)hook_inline((void *)&SetCursorPos,
			(void *)&hook_SetCursorPos);
	if (!orig_SetCursorPos) {
		con_warn(ERR "couldn't hook SetCursorPos\n");
		goto e1;
	}
	inwin = CreateWindowExW(0, L"RInput", L"RInput", 0, 0, 0, 0, 0, 0, 0, 0, 0);
	if (!inwin) {
		con_warn(ERR " couldn't create input window\n");
		goto e2;
	}
	RAWINPUTDEVICE rd = {
		.hwndTarget = inwin,
		.usUsagePage = USAGEPAGE_MOUSE,
		.usUsage = USAGE_MOUSE
	};
	if (!RegisterRawInputDevices(&rd, 1, sizeof(rd))) {
		con_warn(ERR " couldn't create raw mouse device\n");
		goto e3;
	}

	m_rawinput->base.flags &= ~CON_HIDDEN;
	return true;

e3:	DestroyWindow(inwin);
e2:	unhook_inline((void *)orig_SetCursorPos);
e1:	unhook_inline((void *)orig_GetCursorPos);
e0:	UnregisterClassW(L"RInput", 0);
	return false;
}

void rinput_end(void) {
	RAWINPUTDEVICE rd = {
		.dwFlags = RIDEV_REMOVE,
		.hwndTarget = 0,
		.usUsagePage = USAGEPAGE_MOUSE,
		.usUsage = USAGE_MOUSE
	};
	RegisterRawInputDevices(&rd, 1, sizeof(rd));
	DestroyWindow(inwin);
	UnregisterClassW(L"RInput", 0);

	unhook_inline((void *)orig_GetCursorPos);
	unhook_inline((void *)orig_SetCursorPos);
}

// vi: sw=4 ts=4 noet tw=80 cc=80
