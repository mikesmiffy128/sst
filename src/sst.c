/*
 * Copyright © 2021 Michael Smith <mikesmiffy128@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdbool.h>
#include <string.h>

#include "autojump.h"
#include "con_.h"
#include "demorec.h"
#include "factory.h"
#include "fixes.h"
#include "gamedata.h"
#include "gameinfo.h"
#include "gametype.h"
#include "hook.h"
#include "os.h"
#include "vcall.h"
#include "version.h"

#define RGBA(r, g, b, a) (&(struct con_colour){(r), (g), (b), (a)})

u32 _gametype_tag = 0; // spaghetti: no point making a .c file for 1 variable

static int plugin_ver;
// this is where we start dynamically adding virtual functions, see vtable[]
// array below
static const void **vtable_firstdiff;

// most plugin callbacks are unused - define dummy functions for each signature
static void VCALLCONV nop_v_v(void *this) {}
static void VCALLCONV nop_b_v(void *this, bool b) {}
static void VCALLCONV nop_p_v(void *this, void *p) {}
static void VCALLCONV nop_pp_v(void *this, void *p1, void *p2) {}
static void VCALLCONV nop_pii_v(void *this, void *p, int i1, int i2) {}
static int VCALLCONV nop_p_i(void *this, void *p) { return 0; }
static int VCALLCONV nop_pp_i(void *this, void *p1, void *p2) { return 0; }
static int VCALLCONV nop_5pi_i(void *this, void *p1, void *p2, void *p3,
		void *p4, void *p5, int i) { return 0; }
static void VCALLCONV nop_ipipp_v(void *this, int i1, void *p1, int i2,
		void *p2, void *p3) {}

#ifdef __linux__
// we need to keep this reference to dlclose() it later - see below
static void *clientlib = 0;
#endif

// more source spaghetti wow!
static void VCALLCONV SetCommandClient(void *this, int i) { con_cmdclient = i; }

ifacefactory factory_client = 0, factory_server = 0, factory_engine = 0;

// TODO(featgen): I wanted some nice fancy automatic feature system that
// figures out the dependencies at build time and generates all the init glue
// but we want to actually release the plugin this decade so for now I'm just
// plonking ~~some bools~~ one bool here and worrying about it later. :^)
static bool has_autojump = false;
static bool has_demorec = false;
static bool has_demorec_custom = false;

// HACK: later versions of L4D2 show an annoying dialog on every plugin_load.
// We can suppress this by catching the message string that's passed from
// engine.dll to gameui.dll through KeyValuesSystem in vstdlib.dll and just
// replacing it with some other arbitrary garbage string. This makes gameui fail
// to match the message and thus do nothing. :)
static void **kvsvt;
typedef const char *(*VCALLCONV GetStringForSymbol_func)(void *this, int s);
static GetStringForSymbol_func orig_GetStringForSymbol = 0;
static const char *VCALLCONV GetStringForSymbol_hook(void *this, int s) {
	const char *ret = orig_GetStringForSymbol(this, s);
	if (!strcmp(ret, "OnClientPluginWarning")) ret = "sstBlockedThisEvent";
	return ret;
}

static bool do_load(ifacefactory enginef, ifacefactory serverf) {
	factory_engine = enginef; factory_server = serverf;
#ifndef __linux__
	void *clientlib = 0;
#endif
	if (!gameinfo_init() || !con_init(enginef, plugin_ver)) return false;
	const void **p = vtable_firstdiff;
	if (GAMETYPE_MATCHES(Portal2)) *p++ = (void *)&nop_p_v; // ClientFullyConnect
	*p++ = (void *)&nop_p_v;		  // ClientDisconnect
	*p++ = (void *)&nop_pp_v;		  // ClientPutInServer
	*p++ = (void *)&SetCommandClient; // SetCommandClient
	*p++ = (void *)&nop_p_v;		  // ClientSettingsChanged
	*p++ = (void *)&nop_5pi_i;		  // ClientConnect
	*p++ = plugin_ver > 1 ? (void *)&nop_pp_i : (void *)&nop_p_i; // ClientCommand
	*p++ = (void *)&nop_pp_i;		  // NetworkIDValidated
	// remaining stuff here is backwards compatible, so added unconditionally
	*p++ = (void *)&nop_ipipp_v;	  // OnQueryCvarValueFinished (002+)
	*p++ = (void *)&nop_p_v;		  // OnEdictAllocated
	*p   = (void *)&nop_p_v;		  // OnEdictFreed

#ifdef _WIN32
	//if (gameinfo_serverlib) serverlib = GetModuleHandleW(gameinfo_serverlib);
	if (gameinfo_clientlib) clientlib = GetModuleHandleW(gameinfo_clientlib);
#else
	// Linux Source load order seems to be different to the point where if we
	// +plugin_load or use a vdf then RTLD_NOLOAD won't actually find these, so
	// we have to just dlopen them normally - and then remember to decrement the
	// refcount again later in do_unload() so nothing gets leaked
	//if (gameinfo_serverlib) serverlib = dlopen(gameinfo_serverlib, 0);
	if (gameinfo_clientlib) clientlib = dlopen(gameinfo_clientlib, 0);
#endif
	if (!clientlib) {
		con_warn("sst: warning: couldn't get the game's client library\n");
		goto nc;
	}
	factory_client = (ifacefactory)os_dlsym(clientlib, "CreateInterface");
	if (!factory_client) {
		con_warn("sst: warning: couldn't get client's CreateInterface\n");
	}

nc:	gamedata_init();
	has_autojump = autojump_init();
	has_demorec = demorec_init();
	if (has_demorec) has_demorec_custom = demorec_custom_init();
	fixes_apply();

	// NOTE: this is technically redundant for early versions but I CBA writing
	// a version check; it's easier to just do this unilaterally.
	if (GAMETYPE_MATCHES(L4D2)) {
#ifdef _WIN32
		// XXX: not sure if vstdlib should be done dynamically like this or just
		// another stub like tier0?
		void *vstdlib = GetModuleHandleW(L"vstdlib.dll");
		if (!vstdlib) {
			con_warn("sst: warning: couldn't get vstdlib, won't be able to "
					"prevent nag message\n");
			goto e;
		}
		void *(*KeyValuesSystem)(void) = (void *(*)(void))os_dlsym(vstdlib,
				"KeyValuesSystem");
		if (KeyValuesSystem) {
			void *kvs = KeyValuesSystem();
			kvsvt = *(void ***)kvs;
			if (!os_mprot(kvsvt + 4, sizeof(void *), PAGE_READWRITE)) {
				con_warn("sst: warning: couldn't unprotect KeyValuesSystem "
						"vtable; won't be able to prevent nag message\n");
				goto e;
			}
			orig_GetStringForSymbol = (GetStringForSymbol_func)hook_vtable(
					kvsvt, 4, (void *)GetStringForSymbol_hook);
		}
#else
#warning TODO(linux) suitably abstract this stuff to Linux!
#endif
	}

e:	con_colourmsg(RGBA(64, 255, 64, 255),
			LONGNAME " v" VERSION " successfully loaded");
	con_colourmsg(RGBA(255, 255, 255, 255), " for game ");
	con_colourmsg(RGBA(0, 255, 255, 255), "%s\n", gameinfo_title);
	return true;
}

static void do_unload(void) {
	if (has_autojump) autojump_end();
	if (has_demorec) demorec_end();

#ifdef __linux__
	//if (serverlib) dlclose(serverlib);
	if (clientlib) dlclose(clientlib);
#endif
	con_disconnect();

	if (orig_GetStringForSymbol) {
		unhook_vtable(kvsvt, 4, (void *)orig_GetStringForSymbol);
	}
}

// since this is static/global, it only becomes false again when the plugin SO
// is unloaded/reloaded
static bool already_loaded = false;
static bool skip_unload = false;

static bool VCALLCONV Load(void *this, ifacefactory enginef,
		ifacefactory serverf) {
	if (already_loaded) {
		con_warn("Already loaded! Doing nothing!\n");
		skip_unload = true;
		return false;
	}
	already_loaded = do_load(enginef, serverf);
	skip_unload = !already_loaded;
	return already_loaded;
}

static void Unload(void *this) {
	// the game tries to unload on a failed load, for some reason
	if (skip_unload) {
		skip_unload = false;
		return;
	}
	do_unload();
}

static void VCALLCONV Pause(void *this) {
	con_warn(NAME " doesn't support plugin_pause - ignoring\n");
}
static void VCALLCONV UnPause(void *this) {
	con_warn(NAME " doesn't support plugin_unpause - ignoring\n");
}

static const char *VCALLCONV GetPluginDescription(void *this) {
	return LONGNAME " v" VERSION;
}

DEF_CCMD_HERE(sst_printversion, "Display plugin version information", 0) {
	con_msg("v" VERSION "\n");
}

#define MAX_VTABLE_FUNCS 21
static const void *vtable[MAX_VTABLE_FUNCS] = {
	// start off with the members which (thankfully...) are totally stable
	// between interface versions - the *remaining* members get filled in just
	// in time by do_load() once we've figured out what engine branch we're on
	(void *)&Load,
	(void *)&Unload,
	(void *)&Pause,
	(void *)&UnPause,
	(void *)&GetPluginDescription,
	(void *)&nop_p_v,	// LevelInit
	(void *)&nop_pii_v, // ServerActivate
	(void *)&nop_b_v,	// GameFrame
	(void *)&nop_v_v,	// LevelShutdown
	(void *)&nop_p_v	// ClientActive
	// At this point, Alien Swarm and Portal 2 add ClientFullyConnect, so we
	// can't hardcode any more of the layout!
};
// end MUST point AFTER the last of the above entries
static const void **vtable_firstdiff = vtable + 10;
// this is equivalent to a class with no members!
static const void *const *const plugin_obj = vtable;

EXPORT const void *CreateInterface(const char *name, int *ret) {
	if (!strncmp(name, "ISERVERPLUGINCALLBACKS00", 24)) {
		if ((name[24] >= '1' || name[24] <= '3') && name[25] == '\0') {
			if (ret) *ret = 0;
			plugin_ver = name[24] - '0';
			return &plugin_obj;
		}
	}
	if (ret) *ret = 1;
	return 0;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
