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
#include <string.h>

#ifdef _WIN32
#include <shlwapi.h>
#endif

#include "autojump.h"
#include "con_.h"
#include "demorec.h"
#include "factory.h"
#include "fixes.h"
#include "gamedata.h"
#include "gameinfo.h"
#include "gametype.h"
#include "hook.h"
#include "nosleep.h"
#include "os.h"
#include "rinput.h"
#include "vcall.h"
#include "version.h"

#ifdef _WIN32
#define fS "S"
#else
#define fS "s"
#endif

#define RGBA(r, g, b, a) (&(struct con_colour){(r), (g), (b), (a)})

u32 _gametype_tag = 0; // spaghetti: no point making a .c file for 1 variable

static int ifacever;
// this is where we start dynamically adding virtual functions, see vtable[]
// array below
static const void **vtable_firstdiff;
static const void *const *const plugin_obj;

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

ifacefactory factory_client = 0, factory_server = 0, factory_engine = 0,
		factory_inputsystem = 0;

// TODO(featgen): I wanted some nice fancy automatic feature system that
// figures out the dependencies at build time and generates all the init glue
// but we want to actually release the plugin this decade so for now I'm just
// plonking some bools here and worrying about it later. :^)
static bool has_autojump = false;
static bool has_demorec = false;
static bool has_demorec_custom = false;
static bool has_nosleep = false;
#ifdef _WIN32
static bool has_rinput = false;
#endif

// since this is static/global, it only becomes false again when the plugin SO
// is unloaded/reloaded
static bool already_loaded = false;
static bool skip_unload = false;

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

// vstdlib symbol, only currently used in l4d2 but exists everywhere so oh well
IMPORT void *KeyValuesSystem(void);

// XXX: not sure if all this stuff should, like, go somewhere?

struct CUtlMemory {
	void *mem;
	int alloccnt;
	int growsz;
};

struct CUtlVector {
	struct CUtlMemory m;
	int sz;
	void *mem_again_for_some_reason;
};

struct CServerPlugin /* : IServerPluginHelpers */ {
	void **vtable;
	struct CUtlVector plugins;
	/*IPluginHelpersCheck*/ void *pluginhlpchk;
};

struct CPlugin {
	char description[128];
	bool paused;
	void *theplugin; // our own "this" pointer (or whichever other plugin it is)
	int ifacever;
	// should be the plugin library, but in old Source branches it's just null,
	// because CServerPlugin::Load() erroneously shadows this field with a local
	void *module;
};

#ifdef _WIN32
extern long __ImageBase; // this is actually the PE header struct but don't care
#define ownhandle() ((void *)&__ImageBase)
#else
// sigh, _GNU_SOURCE crap. define here instead >:(
typedef struct {
	const char *dli_fname;
	void *dli_fbase;
	const char *dli_sname;
	void *dli_saddr;
} Dl_info;
int dladdr1(const void *addr, Dl_info *info, void **extra_info, int flags);
static inline void *ownhandle(void) {
	Dl_info dontcare;
	void *dl;
	dladdr1((void *)&ownhandle, &dontcare, &dl, /*RTLD_DL_LINKMAP*/ 2);
	return dl;
}
#endif

#define VDFBASENAME "SourceSpeedrunTools"

DEF_CCMD_HERE(sst_autoload_enable, "Register SST to load on game startup", 0) {
	// note: gamedir doesn't account for if the dll is in a base mod's
	// directory, although it will yield a valid/working relative path anyway.
	const os_char *searchdir = ifacever == 3 ?
			gameinfo_gamedir : gameinfo_bindir;
	os_char path[PATH_MAX];
	if (!os_dlfile(ownhandle(), path, sizeof(path) / sizeof(*path))) {
		// hopefully by this point this won't happen, but, like, never know
		con_warn("error: failed to get path to plugin\n");
		return;
	}
	os_char relpath[PATH_MAX];
#ifdef _WIN32
	if (!PathRelativePathToW(relpath, searchdir, FILE_ATTRIBUTE_DIRECTORY,
			path, 0)) {
		con_warn("error: couldn't compute a relative path for some reason\n");
		return;
	}
	// arbitrary aesthetic judgement
	for (os_char *p = relpath; *p; ++p) if (*p == L'\\') *p = L'/'; 
#else
#error TODO(linux): implement this, it's late right now and I can't be bothered
#endif
	int len = os_strlen(gameinfo_gamedir);
	if (len + sizeof("/addons/" VDFBASENAME ".vdf") >
			sizeof(path) / sizeof(*path)) {
		con_warn("error: path to VDF is too long\n");
		return;
	}
	memcpy(path, gameinfo_gamedir, len * sizeof(*gameinfo_gamedir));
	memcpy(path + len, OS_LIT("/addons"), 8 * sizeof(os_char));
	if (os_mkdir(path) == -1 && errno != EEXIST) {
		con_warn("error: couldn't create %" fS ": %s\n", path, strerror(errno));
		return;
	}
	memcpy(path + len + sizeof("/addons") - 1,
			OS_LIT("/") OS_LIT(VDFBASENAME) OS_LIT(".vdf"),
			sizeof("/" VDFBASENAME ".vdf") * sizeof(os_char));
	FILE *f = os_fopen(path, OS_LIT("wb"));
	if (!f) {
		con_warn("error: couldn't open %" fS ": %s", path, strerror(errno));
		return;
	}
	// XXX: oh, crap, we're clobbering unicode again. welp, let's hope the
	// theory that the engine is just as bad if not worse is true so that it
	// doesn't matter.
	if (fprintf(f, "Plugin { file \"%" fS "\" }\n", relpath) < 0 ||
			fflush(f) == -1) {
		con_warn("error: couldn't write to %" fS ": %s", path, strerror(errno));
	}
	fclose(f);
}

DEF_CCMD_HERE(sst_autoload_disable, "Stop loading SST on game startup", 0) {
	os_char path[PATH_MAX];
	int len = os_strlen(gameinfo_gamedir);
	if (len + sizeof("/addons/" VDFBASENAME ".vdf") >
			sizeof(path) / sizeof(*path)) {
		con_warn("error: path to VDF is too long\n");
		return;
	}
	memcpy(path, gameinfo_gamedir, len * sizeof(*gameinfo_gamedir));
	memcpy(path + len, OS_LIT("/addons/") OS_LIT(VDFBASENAME) OS_LIT(".vdf"),
			sizeof("/addons/" VDFBASENAME ".vdf") * sizeof(os_char));
	if (os_unlink(path) == -1 && errno != ENOENT) {
		con_warn("warning: couldn't delete %" fS ":%s\n", path, strerror(errno));
	}
}

static bool do_load(ifacefactory enginef, ifacefactory serverf) {
	factory_engine = enginef; factory_server = serverf;
	if (!con_init(enginef, ifacever)) return false;
	if (!gameinfo_init(enginef)) { con_disconnect(); return false; }
	const void **p = vtable_firstdiff;
	if (GAMETYPE_MATCHES(Portal2)) *p++ = (void *)&nop_p_v; // ClientFullyConnect
	*p++ = (void *)&nop_p_v;		  // ClientDisconnect
	*p++ = (void *)&nop_pp_v;		  // ClientPutInServer
	*p++ = (void *)&SetCommandClient; // SetCommandClient
	*p++ = (void *)&nop_p_v;		  // ClientSettingsChanged
	*p++ = (void *)&nop_5pi_i;		  // ClientConnect
	*p++ = ifacever > 1 ? (void *)&nop_pp_i : (void *)&nop_p_i; // ClientCommand
	// remaining stuff here is backwards compatible, so added unconditionally
	*p++ = (void *)&nop_pp_i;		  // NetworkIDValidated
	*p++ = (void *)&nop_ipipp_v;	  // OnQueryCvarValueFinished (002+)
	*p++ = (void *)&nop_p_v;		  // OnEdictAllocated
	*p   = (void *)&nop_p_v;		  // OnEdictFreed

#ifdef _WIN32
	//serverlib = GetModuleHandleW(gameinfo_serverlib);
	void *clientlib = GetModuleHandleW(gameinfo_clientlib);
#else
	// Linux Source load order seems to be different to the point where if we
	// +plugin_load or use a vdf then RTLD_NOLOAD won't actually find these, so
	// we have to just dlopen them normally - and then remember to decrement the
	// refcount again later in do_unload() so nothing gets leaked
	//serverlib = dlopen(gameinfo_serverlib, RTLD_NOW);
	clientlib = dlopen(gameinfo_clientlib, RTLD_NOW);
#endif
	if (!clientlib) {
		con_warn("sst: warning: couldn't get the game's client library\n");
	}
	else if (!(factory_client = (ifacefactory)os_dlsym(clientlib,
			"CreateInterface"))) {
		con_warn("sst: warning: couldn't get client's CreateInterface\n");
	}
#ifdef _WIN32
	void *inputsystemlib = GetModuleHandleW(L"inputsystem.dll");
#else
	// TODO(linux): assuming the above doesn't apply to this; check if it does!
	void *inputsystemlib = dlopen("bin/libinputsystem.so",
			RTLD_NOW | RLTD_NOLOAD);
	if (inputsystemlib) dlclose(inputsystemlib); // blegh
#endif
	if (!inputsystemlib) {
		con_warn("sst: warning: couldn't get the input system library\n");
	}
	else if (!(factory_inputsystem = (ifacefactory)os_dlsym(inputsystemlib,
			"CreateInterface"))) {
		con_warn("sst: warning: couldn't get input system's CreateInterface\n");
	}

	gamedata_init();
	has_autojump = autojump_init();
	has_demorec = demorec_init();
	has_nosleep = nosleep_init();
#ifdef _WIN32
	has_rinput = rinput_init();
#endif
	if (has_demorec) has_demorec_custom = demorec_custom_init();
	fixes_apply();

	// NOTE: this is technically redundant for early versions but I CBA writing
	// a version check; it's easier to just do this unilaterally.
	if (GAMETYPE_MATCHES(L4D2x)) {
		void *kvs = KeyValuesSystem();
		kvsvt = *(void ***)kvs;
		if (!os_mprot(kvsvt + 4, sizeof(void *), PAGE_READWRITE)) {
			con_warn("sst: warning: couldn't unprotect KeyValuesSystem "
					"vtable; won't be able to prevent nag message\n");
			goto e;
		}
		orig_GetStringForSymbol = (GetStringForSymbol_func)hook_vtable(kvsvt,
				4, (void *)GetStringForSymbol_hook);
	}

e:	con_colourmsg(RGBA(64, 255, 64, 255),
			LONGNAME " v" VERSION " successfully loaded");
	con_colourmsg(RGBA(255, 255, 255, 255), " for game ");
	con_colourmsg(RGBA(0, 255, 255, 255), "%s\n", gameinfo_title);
	return true;
}

static void do_unload(void) {
	struct CServerPlugin *pluginhandler =
			factory_engine("ISERVERPLUGINHELPERS001", 0);
	if (pluginhandler) { // if not, oh well too bad we tried :^)
		struct CPlugin **plugins = pluginhandler->plugins.m.mem;
		int n = pluginhandler->plugins.sz;
		for (struct CPlugin **pp = plugins; pp - plugins < n; ++pp) {
			if ((*pp)->theplugin == (void *)&plugin_obj) {
				// see comment in CPlugin above. setting this to the real handle
				// right before the engine tries to unload us allows it to
				// actually unload us instead of just doing nothing.
				// in newer branches that don't have this bug, this is still
				// correct anyway so no need to bother checking.
				(*pp)->module = ownhandle();
				break;
			}
		}
	}

	if (has_autojump) autojump_end();
	if (has_demorec) demorec_end();
	if (has_nosleep) nosleep_end();
#ifdef _WIN32
	if (has_rinput) rinput_end();
#endif

#ifdef __linux__
	//if (serverlib) dlclose(serverlib);
	if (clientlib) dlclose(clientlib);
#endif
	con_disconnect();

	if (orig_GetStringForSymbol) {
		unhook_vtable(kvsvt, 4, (void *)orig_GetStringForSymbol);
	}
}

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

static void VCALLCONV Unload(void *this) {
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
			ifacever = name[24] - '0';
			return &plugin_obj;
		}
	}
	if (ret) *ret = 1;
	return 0;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
