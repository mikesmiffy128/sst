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

#include <stdbool.h>
#include <stdlib.h>

#include "con_.h"
#include "hook.h"
#include "engineapi.h"
#include "errmsg.h"
#include "intdefs.h"
#include "mem.h"
#include "os.h"
#include "ppmagic.h"

static bool lockdown = false;

#ifdef _WIN32

static void *gamewin, *inhookwin, *inhookthr;
static ulong inhooktid;

// UINT_PTR is a **stupid** typedef, but whatever.
static ssize __stdcall kproc(int code, UINT_PTR wp, ssize lp) {
	KBDLLHOOKSTRUCT *data = (KBDLLHOOKSTRUCT *)lp;
	if (lockdown && data->flags & LLKHF_INJECTED &&
			GetForegroundWindow() == gamewin) {
		return 1;
	}
	return CallNextHookEx(0, code, wp, lp);
}

static ssize __stdcall mproc(int code, UINT_PTR wp, ssize lp) {
	MSLLHOOKSTRUCT *data = (MSLLHOOKSTRUCT *)lp;
	if (lockdown && data->flags & LLMHF_INJECTED &&
			GetForegroundWindow() == gamewin) {
		return 1;
	}
	return CallNextHookEx(0, code, wp, lp);
}

// this is its own thread to meet the strict timing deadline, otherwise the
// hook gets silently removed. plus, we don't wanna incur latency anyway.
static ulong __stdcall inhookthrmain(void *unused) {
	if (!SetWindowsHookExW(WH_KEYBOARD_LL, &kproc, 0, 0) ||
			!SetWindowsHookExW(WH_MOUSE_LL, &mproc, 0, 0)) {
		// intentionally vague message
		con_warn("sst: RTA mode is unavailable due to an error\n");
		return -1;
	}
	MSG m; int ret;
	while ((ret = GetMessageW(&m, inhookwin, 0, 0)) > 0) DispatchMessage(&m);
	if (ret == -1) {
		// XXX: if this ever happens, it's a disaster! users might not notice
		// their run just dying all of a sudden. with any luck it won't matter
		// in practice but... this kind of sucks.
		con_warn("** sst: ERROR in message loop, abandoning RTA mode! **");
	}
	return ret;
}

static bool win32_init(void) {
	gamewin = FindWindowW(L"Valve001", 0);
	if (!gamewin) {
		errmsg_errorsys("failed to get game window handle");
		return false;
	}
	return true;
}

static void inhook_start(void) {
	inhookwin = CreateWindowW(L"sst-eventloop", L"sst-eventloop",
			WS_DISABLED, 0, 0, 0, 0, HWND_MESSAGE, 0, 0, 0);
	inhookthr = CreateThread(0, 0, &inhookthrmain, 0, 0, &inhooktid);
}

// TODO(rta): run this check every tick (or at least X amount of time)
static void inhook_check(void) {
	if (WaitForSingleObject(inhookthr, 0) == WAIT_OBJECT_0) {
		ulong status;
		GetExitCodeThread(inhookthr, &status);
		if (status != 0) {
			// TODO(rta): stop demos, and stuff.
			lockdown = false;
		}
	}
}

static void inhook_stop(void) {
	PostThreadMessageW(inhooktid, WM_QUIT, 0, 0);
	if (WaitForSingleObject(inhookthr, INFINITE) == WAIT_FAILED) {
		errmsg_warnsys("couldn't wait for thread, status unknown");
		// XXX: now what!?
	}
}

#else

// TODO(linux): do some stuff, I guess...

#endif

// TODO(rta): call these functions as part of the run lifecycle

static void startlockdown(void) {
	if (lockdown) return;
#ifdef _WIN32
	inhook_start();
#endif
	// FIXME: should really have some semaphore to make sure inhook thread got
	// going okay...
	lockdown = true;
	// TODO(rta): start demos, etc
}

static void endlockdown(void) {
	if (!lockdown) return;
#ifdef _WIN32
	inhook_stop();
#endif
	lockdown = false;
}

bool ac_init(void) {
#if defined(_WIN32)
	if (!win32_init()) return false;
#elif defined(__linux__)
	// TODO(linux): call init things
#endif
	return true;
}

void ac_end(void) {
	endlockdown();
}

// vi: sw=4 ts=4 noet tw=80 cc=80
