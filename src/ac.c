/*
 * Copyright © Michael Smith <mikesmiffy128@gmail.com>
 * Copyright © Willian Henrique <wsimanbrazil@yahoo.com.br>
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

#ifdef _WIN32
#include <Windows.h>
#else
#include <sys/mman.h>
#endif

#include "alias.h"
#include "bind.h"
#include "chunklets/fastspin.h"
#include "chunklets/msg.h"
#include "con_.h"
#include "crypto.h"
#include "democustom.h"
#include "demorec.h"
#include "engineapi.h"
#include "errmsg.h"
#include "event.h"
#include "feature.h"
#include "gamedata.h"
#include "gametype.h"
#include "hook.h"
#include "intdefs.h"
#include "langext.h"
#include "mem.h"
#include "os.h"
#include "ppmagic.h"
#include "sst.h"
#include "vcall.h"
#include "x86.h"
#include "x86util.h"

FEATURE()
GAMESPECIFIC(L4D) // TODO(compat): wanna add support for more stuff, obviously!
REQUIRE(bind)
REQUIRE(democustom)
REQUIRE_GAMEDATA(vtidx_GetDesktopResolution)
REQUIRE_GAMEDATA(vtidx_DispatchAllStoredGameMessages)
REQUIRE_GLOBAL(pluginhandler)

static bool enabled = false;

// mild overkill: 1 page of memory that won't be coredumped or swapped to disk
static struct keybox {
	union { uchar prv[32], shr[32]; };
	uchar tmp[32], pub[32], lbpub[32]; // NOTE: these 3 must be kept contiguous!
	union { u64 nonce; uchar nonce_bytes[8]; };
	crypto_rng_ctx rng; // NOTE: keep this at the end, for wipesessionkeys()
} *keybox;

enum {
	LBPK_L4D
};
const uchar lbpubkeys[1][32] = {
	// L4D series (PLACEHOLDERS for now; this whole thing is unfinished!)
	0x4A, 0xF3, 0xE2, 0xFC, 0x9C, 0x4E, 0xCB, 0xF9,
	0xBD, 0xB8, 0xA9, 0xFC, 0x0E, 0xF7, 0x93, 0x9C,
	0xC3, 0x09, 0x43, 0xB2, 0x6E, 0x7B, 0x1F, 0x19,
	0x40, 0x05, 0xE9, 0x60, 0x43, 0xE8, 0xE2, 0x03
};

static void newsessionkeys() {
	crypto_rng_read(&keybox->rng, keybox->prv, sizeof(keybox->prv));
	crypto_x25519_public_key(keybox->pub, keybox->prv);
	crypto_x25519(keybox->tmp, keybox->prv, keybox->lbpub);
	// dumbest, safest possible key derivation, because I'm not a cryptographer.
	// future versions of the custom demo protocol COULD get something faster
	// (like something with hchacha20, if only I could find enough info on that)
	crypto_blake2b(keybox->shr, sizeof(keybox->tmp), keybox->tmp, 96);
	crypto_wipe(keybox->tmp, sizeof(keybox->tmp));
	keybox->nonce = 0;
}

static void wipesessionkeys() {
	crypto_wipe(keybox->prv, offsetof(struct keybox, rng));
}

HANDLE_EVENT(DemoRecordStarting) { if (enabled) newsessionkeys(); }
HANDLE_EVENT(DemoRecordStopped, int ndemos) { if (enabled) wipesessionkeys(); }

#ifdef _WIN32

static void *gamewin, *inhookwin, *inhookthr;
static ulong inhooktid;

static ssize __stdcall kproc(int code, usize wp, ssize lp) {
	KBDLLHOOKSTRUCT *data = (KBDLLHOOKSTRUCT *)lp;
	if_cold (enabled && data->flags & LLKHF_INJECTED) {
		// fast-path the next branch because alt-tabbed speed is irrelevant
		if_hot (GetForegroundWindow() == gamewin) {
			// maybe this input is reasonable, but log it for closer inspection
			// TODO(rta): figure out what to do with this stuff
			// something like the following, but with a proper abstraction...
			//uchar buf[28 + 16], *p = buf;
			//msg_putasz4(p, 2); p += 1;
			//	msg_putssz5(p, 8); memcpy(p + 1, "FakeKey", 7); p += 8;
			//	msg_putmsz4(p, 2); p += 1;
			//		msg_putssz5(p, 3); memcpy(p + 1, "vk", 2); p += 3;
			//			p += msg_putu32(p, data->vkCode);
			//		msg_putssz5(p, 3); memcpy(p + 1, "scan", 4); p += 5;
			//			p += msg_putu32(p, data->scanCode);
			//++keybox->nonce;
			//// append mac at end of message
			//crypto_aead_lock_djb(buf, p, keybox->shr, keybox->nonce_bytes, 0,
			//		0, buf, p - buf);
			//democustom_write(buf, p - buf + 16);
		}
	}
	return CallNextHookEx(0, code, wp, lp);
}

static ssize __stdcall mproc(int code, usize wp, ssize lp) {
	MSLLHOOKSTRUCT *data = (MSLLHOOKSTRUCT *)lp;
	if_cold (enabled && data->flags & LLMHF_INJECTED) {
		if_hot (GetForegroundWindow() == gamewin) {
			// no way this input would ever be reasonable. just discard it
			return 1;
		}
	}
	return CallNextHookEx(0, code, wp, lp);
}

// this is its own thread to meet the strict timing deadline, otherwise the
// hook gets silently removed. plus, we don't wanna incur latency anyway.
static ulong __stdcall inhookthrmain(void *param) {
	volatile int *sig = param;
	if_cold (!SetWindowsHookExW(WH_KEYBOARD_LL, (HOOKPROC)&kproc, 0, 0) ||
			!SetWindowsHookExW(WH_MOUSE_LL, (HOOKPROC)&mproc, 0, 0)) {
		fastspin_raise(sig, 2);
		return -1;
	}
	fastspin_raise(sig, 1);
	MSG m; int ret;
	while ((ret = GetMessageW(&m, inhookwin, 0, 0)) > 0) DispatchMessage(&m);
	return ret;
}

static ssize orig_wndproc;
static ssize __stdcall hook_wndproc(void *wnd, uint msg, usize wp, ssize lp) {
	if_cold (msg == WM_COPYDATA && enabled) {
		return DefWindowProcW(wnd, msg, wp, lp);
	}
	return CallWindowProcA((WNDPROC)orig_wndproc, wnd, msg, wp, lp);
}

static inline bool win32_init() {
	// note: using A instead of W to avoid some weirdness with handles...
	gamewin = FindWindowA("Valve001", 0);
	// note: error messages here are a bit cryptic on purpose, but easy to find
	// in the code. in other words, we're hiding in plain sight :-)
	if_cold (!gamewin) {
		errmsg_errorsys("failed to find window");
		return false;
	}
	orig_wndproc = SetWindowLongPtrA(gamewin, GWLP_WNDPROC,
			(ssize)&hook_wndproc);
	if_cold (!orig_wndproc) { // XXX: assuming 0 won't be legitimately returned
		errmsg_errorsys("failed to attach message handler");
		return false;
	}
	return true;
}

static inline void win32_end() {
	// no error handling here because we'd crash either way. good luck!
	SetWindowLongPtrA(gamewin, GWLP_WNDPROC, orig_wndproc);
}

static void inhook_start(volatile int *sig) {
	inhookwin = CreateWindowW(L"sst-eventloop", L"sst-eventloop", WS_DISABLED,
			0, 0, 0, 0, HWND_MESSAGE, 0, 0, 0);
	inhookthr = CreateThread(0, 0, &inhookthrmain, (u32 *)sig, 0, &inhooktid);
}

static void inhook_check() {
	if (WaitForSingleObject(inhookthr, 0) == WAIT_OBJECT_0) {
		ulong status;
		GetExitCodeThread(inhookthr, &status);
		if_cold (status) {
			// XXX: if this ever happens, it's a disaster! users might not
			// notice their run just dying all of a sudden. with any luck it
			// won't matter in practice but... this kind of sucks.
			con_warn("** sst: ERROR in message loop, abandoning RTA mode! **");
			// TODO(rta): stop demos, and stuff.
			enabled = false;
		}
	}
}

static void inhook_stop() {
	PostThreadMessageW(inhooktid, WM_QUIT, 0, 0);
	if (WaitForSingleObject(inhookthr, INFINITE) == WAIT_FAILED) {
		errmsg_warnsys("couldn't wait for thread, status unknown");
		// XXX: now what!?
	}
	else {
		// assume WAIT_OBJECT_0
		ulong status;
		GetExitCodeThread(inhookthr, &status);
		if_cold (status) {
			// not much else we can do now!
			errmsg_errorx("message loop didn't shut down cleanly\n");
		}
	}
	CloseHandle(inhookthr);
}

#else

// TODO(linux): do some stuff, I guess...

#endif

bool ac_enable() {
	if (!enabled) {
#ifdef _WIN32
		volatile int sig = 0;
		inhook_start(&sig);
		fastspin_wait(&sig);
		if_cold (sig == 2) { // else 1 for success
			con_warn("** sst: ERROR starting message loop, "
					"can't continue! **");
			CloseHandle(inhookthr);
			return false;
		}
#endif
	}
	enabled = true;
	return true;
}

HANDLE_EVENT(Tick, bool simulating) {
#ifdef _WIN32
	static uint fewticks = 0;
	// just check this every so often (roughly 0.1-0.3s depending on game)
	if (enabled && !(++fewticks & 7)) inhook_check();
#endif
}

void ac_disable() {
#ifdef _WIN32
	if (enabled) inhook_stop();
#endif
	enabled = false;
}

enum /* from InputEventType_t - terser names used here */ {
	BTNDOWN = 0, // data contains button code
	BTNUP, // "
	BTNDOUBLECLICK, // "
	ANALOGUEVALCHG, // data contains analogue code, data2 contains value
	SYSQUIT = 100,
	SYSCTRLHOTPLUG, // data contains controller ID
	SYSCTRLCOLDPLUG, // "
	// ranges for other things
	FIRSTVGUIEV = 1000,
	FIRSTAPPEV = 2000
};

struct inputevent {
	int type; // above enum
	int tick;
	int data, data2, data3;
};

struct IGameUIFuncs { void **vtable; };
struct IGame { void **vtable; };

DECL_VFUNC_DYN(struct IGameUIFuncs, void, GetDesktopResolution, int *, int *)
DECL_VFUNC_DYN(struct IGame, void, DispatchAllStoredGameMessages)

typedef void (*Key_Event_func)(struct inputevent *);
static Key_Event_func orig_Key_Event;
static void hook_Key_Event(struct inputevent *ev) {
	//const char *desc[] = {"DOWN", "UP", "DBL"};
	//const char desclen[] = {4, 2, 3};
	switch (ev->type) {
		CASES(BTNDOWN, BTNUP, BTNDOUBLECLICK):
			// TODO(rta): do something interesting with button data
			//uchar buf[28], *p = buf;
			//msg_putasz4(p, 2); p += 1;
			//	msg_putssz5(p, 8); memcpy(p + 1, "KeyInput", 8); p += 9;
			//	msg_putmsz4(p, 2); p += 1;
			//		msg_putssz5(p, 3); memcpy(p + 1, "key", 3); p += 4;
			//			p += msg_puts32(p, ev->data);
			//		msg_putssz5(p, 3); memcpy(p + 1, "btn", 3); p += 4;
			//			int idx = ev->type - BTNDOWN;
			//			msg_putssz5(p++, desclen[idx]);
			//			memcpy(p, desc[idx], desclen[idx]); p += desclen[idx];
	}
	orig_Key_Event(ev);
}

static bool find_Key_Event() {
#ifdef _WIN32
	// Crazy pointer-chasing path to get to DispatchInputEvent:
	// IGameUIFuncs interface
	// -> CGameUIFuncs::GetDesktopResolution vfunc
	//  -> IGame/CGame (first mov into ECX)
	//   -> CGame::DispatchAllStoredGameMessages vfunc
	//    -> First call instruction (either DispatchInputEvent or Key_Event)
	struct IGameUIFuncs *gameuifuncs = factory_engine(
			"VENGINE_GAMEUIFUNCS_VERSION005", 0);
	if_cold (!gameuifuncs) {
		errmsg_errorx("couldn't get engine game UI interface");
		return false;
	}
	struct IGame *cgame;
	const uchar *insns = gameuifuncs->vtable[vtidx_GetDesktopResolution];
	for (const uchar *p = insns; p - insns < 16;) {
		if (p[0] == X86_MOVRMW && p[1] == X86_MODRM(0, 1, 5)) {
			void **indirect = mem_loadptr(p + 2);
			cgame = *indirect;
			goto ok;
		}
		NEXT_INSN(p, "CGame instance pointer");
	}
	errmsg_errorx("couldn't find pointer to CGame instance");
	return false;

ok:	insns = cgame->vtable[vtidx_DispatchAllStoredGameMessages];
	for (const uchar *p = insns; p - insns < 128;) {
		if (p[0] == X86_CALL) {
			orig_Key_Event = (Key_Event_func)(p + 5 + mem_loads32(p + 1));
			goto ok2;
		}
		NEXT_INSN(p, "DispatchInputEvent/Key_Event function");
	}
	errmsg_errorx("couldn't find DispatchInputEvent/Key_Event function");
	return false;

ok2:insns = (const uchar *)orig_Key_Event;
	// Depending on compiler inlining decisions, the function we just found can
	// be either DispatchInputEvent or Key_Event. If another CALL is found at
	// the start of this function, that means that we actually found
	// DispatchInputEvent and this CALL points to Key_Event.
	for (const uchar *p = insns; p - insns < 32;) {
		if (p[0] == X86_CALL) {
			orig_Key_Event = (Key_Event_func)(p + 5 + mem_loads32(p + 1));
			break;
		}
		NEXT_INSN(p, "Key_Event function");
	}
	return true;
#else
#warning TODO(linux): more find-y stuff
#endif
	return false;
}

HANDLE_EVENT(AllowPluginLoading, bool loading) {
	if_cold (enabled) if_hot (demorec_demonum() != -1) {
		con_warn("sst: plugins cannot be %s while recording a run\n",
				loading ? "loaded" : "unloaded");
		return false;
	}
	return true;
}

HANDLE_EVENT(PluginLoaded) {
	// TODO(rta): do something with plugin list here
}
HANDLE_EVENT(PluginUnloaded) {
	// TODO(rta): do something with plugin list here
}

INIT {
	if_cold (!find_Key_Event()) return FEAT_INCOMPAT;
	struct hook_inline_featsetup_ret h = hook_inline_featsetup(
			(void *)orig_Key_Event, (void **)&orig_Key_Event, "Key_Event");
	if_cold (h.err) return h.err;

#ifdef _WIN32
	keybox = VirtualAlloc(0, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if_cold (!keybox) {
		errmsg_errorsys("couldn't allocate memory for session state");
		return FEAT_FAIL;
	}
	if_cold (!VirtualLock(keybox, 4096)) {
		errmsg_errorsys("couldn't secure session state");
		goto e;
	}
	if_cold (!win32_init()) goto e;
#else
	keybox = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
	if_cold (keybox == MAP_FAILED) {
		errmsg_errorstd("couldn't allocate memory for session state");
		return FEAT_FAIL;
	}
	// linux-specific madvise stuff (there are some equivalents in OpenBSD and
	// FreeBSD, if anyone's wondering, but we don't need to worry about those)
	if_cold (madvise(keybox, 4096, MADV_DONTFORK) == -1 ||
			madvise(keybox, 4096, MADV_DONTDUMP) == - 1 ||
			mlock(keybox, 4096) == -1) {
		errmsg_errorstd("couldn't secure session state");
		goto e;
	}
	// TODO(linux): call other init things
#endif

	uchar seed[32];
	os_randombytes(seed, sizeof(seed));
	crypto_rng_init(&keybox->rng, seed);
	if (GAMETYPE_MATCHES(L4D)) {
		// copy into the keybox so key derivation blake2 gets a nice contiguous
		// run of bytes
		memcpy(keybox->lbpub, lbpubkeys[LBPK_L4D], 32);
	}
	hook_inline_commit(h.prologue, (void *)hook_Key_Event);
	return FEAT_OK;

#ifdef _WIN32
e:	VirtualFree(keybox, 4096, MEM_RELEASE);
#else
e:	munmap(keybox, 4096);
#endif
	return FEAT_FAIL;
}

END {
	// TODO(opt): *maybe* do the skip-on-quit stuff here. feels a bit scary...
	ac_disable();
#if defined(_WIN32)
	VirtualFree(keybox, 4096, MEM_RELEASE);
	win32_end();
#else
	munmap(keybox, 4096);
	// TODO(linux): call other cleanup things
#endif
	unhook_inline((void *)orig_Key_Event);
}

// vi: sw=4 ts=4 noet tw=80 cc=80
