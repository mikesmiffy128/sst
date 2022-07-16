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

#include "ac.h"
#include "bind.h"
#include "alias.h"
#include "autojump.h"
#include "con_.h"
#include "demorec.h"
#include "engineapi.h"
#include "errmsg.h"
#include "ent.h"
#include "fov.h"
#include "fixes.h"
#include "gameinfo.h"
#include "gametype.h"
#include "hook.h"
#include "l4dwarp.h"
#include "nosleep.h"
#include "portalcolours.h"
#include "os.h"
#include "rinput.h"
#include "vcall.h"
#include "version.h"

#ifdef _WIN32
#define fS "S"
#else
#define fS "s"
#endif

static int ifacever;

// we need to keep this reference to dlclose() it later - see below
static void *clientlib = 0;

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
		errmsg_errordl("failed to get path to plugin");
		return;
	}
	os_char relpath[PATH_MAX];
#ifdef _WIN32
	if (!PathRelativePathToW(relpath, searchdir, FILE_ATTRIBUTE_DIRECTORY,
			path, 0)) {
		errmsg_errorsys("couldn't compute a relative path for some reason");
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
		errmsg_errorx("path to VDF is too long");
		return;
	}
	memcpy(path, gameinfo_gamedir, len * sizeof(*gameinfo_gamedir));
	memcpy(path + len, OS_LIT("/addons"), 8 * sizeof(os_char));
	if (os_mkdir(path) == -1 && errno != EEXIST) {
		errmsg_errorstd("couldn't create %" fS, path);
		return;
	}
	memcpy(path + len + sizeof("/addons") - 1,
			OS_LIT("/") OS_LIT(VDFBASENAME) OS_LIT(".vdf"),
			sizeof("/" VDFBASENAME ".vdf") * sizeof(os_char));
	FILE *f = os_fopen(path, OS_LIT("wb"));
	if (!f) {
		errmsg_errorstd("couldn't open %" fS, path);
		return;
	}
	// XXX: oh, crap, we're clobbering unicode again. welp, let's hope the
	// theory that the engine is just as bad if not worse is true so that it
	// doesn't matter.
	if (fprintf(f, "Plugin { file \"%" fS "\" }\n", relpath) < 0 ||
			fflush(f) == -1) {
		errmsg_errorstd("couldn't write to %" fS, path);
	}
	fclose(f);
}

DEF_CCMD_HERE(sst_autoload_disable, "Stop loading SST on game startup", 0) {
	os_char path[PATH_MAX];
	int len = os_strlen(gameinfo_gamedir);
	if (len + sizeof("/addons/" VDFBASENAME ".vdf") >
			sizeof(path) / sizeof(*path)) {
		errmsg_errorx("path to VDF is too long");
		return;
	}
	memcpy(path, gameinfo_gamedir, len * sizeof(*gameinfo_gamedir));
	memcpy(path + len, OS_LIT("/addons/") OS_LIT(VDFBASENAME) OS_LIT(".vdf"),
			sizeof("/addons/" VDFBASENAME ".vdf") * sizeof(os_char));
	if (os_unlink(path) == -1 && errno != ENOENT) {
		errmsg_warnstd("couldn't delete %" fS, path);
	}
}

DEF_CCMD_HERE(sst_printversion, "Display plugin version information", 0) {
	con_msg("v" VERSION "\n");
}

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

// more source spaghetti wow!
static void VCALLCONV SetCommandClient(void *this, int i) { con_cmdclient = i; }

// this is where we start dynamically adding virtual functions, see vtable[]
// array below
static const void **vtable_firstdiff;
static const void *const *const plugin_obj;

// TODO(featgen): I wanted some nice fancy automatic feature system that
// figures out the dependencies at build time and generates all the init glue
// but we want to actually release the plugin this decade so for now I'm just
// plonking some bools here and worrying about it later. :^)
static bool has_ac = false, has_autojump = false, has_demorec = false,
		has_fov = false, has_nosleep = false, has_portalcolours = false;
#ifdef _WIN32
static bool has_rinput = false;
#endif

static bool already_loaded = false, skip_unload = false;

#define RGBA(r, g, b, a) (&(struct con_colour){(r), (g), (b), (a)})

// auto-update message. see below in do_featureinit()
static const char *updatenotes = "\
* various internal cleanup\n\
";

static void do_featureinit(void) {
	bool has_bind = bind_init();
	if (has_bind) has_ac = ac_init();
	bool has_alias = alias_init();
	has_autojump = autojump_init();
	has_demorec = demorec_init();
	// not enabling demorec_custom yet - kind of incomplete and currently unused
	//if (has_demorec) demorec_custom_init();
	bool has_ent = ent_init();
	has_fov = fov_init(has_ent);
	if (has_ent) l4dwarp_init();
	has_nosleep = nosleep_init();
	if (clientlib) has_portalcolours = portalcolours_init(clientlib);
#ifdef _WIN32
	has_rinput = rinput_init();
#endif
	fixes_apply();

	con_colourmsg(RGBA(64, 255, 64, 255),
			LONGNAME " v" VERSION " successfully loaded");
	con_colourmsg(RGBA(255, 255, 255, 255), " for game ");
	con_colourmsg(RGBA(0, 255, 255, 255), "%s\n", gameinfo_title);

	// if we're autoloaded and the external autoupdate script downloaded a new
	// version, let the user know about the cool new stuff!
	if (getenv("SST_UPDATED")) {
		// avoid displaying again if we're unloaded and reloaded in one session
#ifdef _WIN32
		SetEnvironmentVariableA("SST_UPDATED", 0);
#else
		unsetenv("SST_UPDATED");
#endif
		struct con_colour gold = {255, 210, 0, 255};
		struct con_colour white = {255, 255, 255, 255};
		con_colourmsg(&white, "\n" NAME " was just ");
		con_colourmsg(&gold, "UPDATED");
		con_colourmsg(&white, " to version ");
		con_colourmsg(&gold, "%s", VERSION);
		con_colourmsg(&white, "!\n\nNew in this version:\n%s\n", updatenotes);
	}
}

static void *vgui;
typedef void (*VCALLCONV VGuiConnect_func)(void);
static VGuiConnect_func orig_VGuiConnect;
static void VCALLCONV hook_VGuiConnect(void) {
	orig_VGuiConnect();
	do_featureinit();
	unhook_vtable(*(void ***)vgui, vtidx_VGuiConnect, (void *)orig_VGuiConnect);
}

// --- Magical deferred load order hack nonsense! ---
// The engine loads VDF plugins basically right after server.dll, but long
// before most other stuff, which makes hooking certain other stuff a pain. We
// still want to be able to load via VDF as it's the only reasonable way to get
// in before config.cfg, which is needed for any kind of configuration to work
// correctly.
//
// So here, we hook CEngineVGui::Connect() which is pretty much the last thing
// that gets called on init, and defer feature init till afterwards. That allows
// us to touch pretty much any engine stuff without worrying about load order
// nonsense.
//
// In do_load() below, we check to see whether we're loading early by checking
// whether gameui.dll is loaded yet; this is one of several possible arbitrary
// checks. If it's loaded already, we assume we're getting loaded late via the
// console and just init everything immediately.
//
// Route credit to Bill for helping figure a lot of this out - mike
static void deferinit(void) {
	vgui = factory_engine("VEngineVGui001", 0);
	if (!vgui) {
		errmsg_warnx("couldn't get VEngineVGui for deferred feature setup");
		goto e;
	}
	if (!os_mprot(*(void ***)vgui + vtidx_VGuiConnect, sizeof(void *),
			PAGE_READWRITE)) {
		errmsg_warnsys("couldn't make CEngineVGui vtable writable for deferred "
				"feature setup");
		goto e;
	}
	orig_VGuiConnect = (VGuiConnect_func)hook_vtable(*(void ***)vgui,
			vtidx_VGuiConnect, (void *)&hook_VGuiConnect);
	return;

e:	con_warn("!!! SOME FEATURES MAY BE BROKEN !!!\n");
	// I think this is the lesser of two evils! Unlikely to happen anyway.
	do_featureinit();
}

static bool do_load(ifacefactory enginef, ifacefactory serverf) {
	if (!hook_init()) {
		errmsg_warnsys("couldn't set up memory for function hooking");
		return false;
	}

	factory_engine = enginef; factory_server = serverf;
	if (!engineapi_init(ifacever)) return false;

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
	clientlib = GetModuleHandleW(gameinfo_clientlib);
#else
	// Apparently on Linux, the client library isn't actually loaded yet here,
	// so RTLD_NOLOAD won't actually find it. We have to just dlopen it
	// normally - and then remember to decrement the refcount again later in
	// do_unload() so nothing gets leaked!
	clientlib = dlopen(gameinfo_clientlib, RTLD_NOW);
#endif
	if (!clientlib) {
		errmsg_warndl("couldn't get the game's client library");
	}
	else if (!(factory_client = (ifacefactory)os_dlsym(clientlib,
			"CreateInterface"))) {
		errmsg_warndl("couldn't get client's CreateInterface");
	}
#ifdef _WIN32
	void *inputsystemlib = GetModuleHandleW(L"inputsystem.dll");
#else
	// TODO(linux): assuming the above doesn't apply to this; check if it does!
	// ... actually, there's a good chance this assumption is now wrong!
	void *inputsystemlib = dlopen("bin/libinputsystem.so",
			RTLD_NOW | RLTD_NOLOAD);
	if (inputsystemlib) dlclose(inputsystemlib); // blegh
#endif
	if (!inputsystemlib) {
		errmsg_warndl("couldn't get the input system library");
	}
	else if (!(factory_inputsystem = (ifacefactory)os_dlsym(inputsystemlib,
			"CreateInterface"))) {
		errmsg_warndl("couldn't get input system's CreateInterface");
	}

	// NOTE: this is technically redundant for early versions but I CBA writing
	// a version check; it's easier to just do this unilaterally.
	if (GAMETYPE_MATCHES(L4D2x)) {
		void *kvs = KeyValuesSystem();
		kvsvt = *(void ***)kvs;
		if (!os_mprot(kvsvt + 4, sizeof(void *), PAGE_READWRITE)) {
			errmsg_warnx("couldn't make KeyValuesSystem vtable writable");
			errmsg_note("won't be able to prevent any nag messages");
		}
		else {
			orig_GetStringForSymbol = (GetStringForSymbol_func)hook_vtable(
					kvsvt, 4, (void *)GetStringForSymbol_hook);
		}
	}

#ifdef _WIN32
	bool isvdf = !GetModuleHandleW(L"gameui.dll");
#else
	void *gameuilib = dlopen("bin/libgameui.so", RTLD_NOW | RLTD_NOLOAD);
	bool isvdf = !gameuilib;
	if (gameuilib) dlclose(gameuilib);
#endif
	if (isvdf) deferinit(); else do_featureinit();
	return true;
}

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

static void do_unload(void) {
#ifdef _WIN32 // this is only relevant in builds that predate linux support
	struct CServerPlugin *pluginhandler =
			factory_engine("ISERVERPLUGINHELPERS001", 0);
	if (pluginhandler) { // if not, oh well too bad we tried :^)
		struct CPlugin **plugins = pluginhandler->plugins.m.mem;
		int n = pluginhandler->plugins.sz;
		for (struct CPlugin **pp = plugins; pp - plugins < n; ++pp) {
			if ((*pp)->theplugin == (void *)&plugin_obj) {
				// see comment in CPlugin above. setting this to the real handle
				// right before the engine tries to unload us allows it to
				// actually do so. in newer branches this is redundant but
				// doesn't do any harm so it's just unconditional.
				// NOTE: old engines ALSO just leak the handle and never call
				// Unload() if Load() fails; can't really do anything about that
				(*pp)->module = ownhandle();
				break;
			}
		}
	}
#endif

	if (has_ac) ac_end();
	if (has_autojump) autojump_end();
	if (has_demorec) demorec_end();
	if (has_fov) fov_end(); // dep on ent
	if (has_nosleep) nosleep_end();
	if (has_portalcolours) portalcolours_end();
#ifdef _WIN32
	if (has_rinput) rinput_end();
#endif

#ifdef __linux__
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
	if (skip_unload) { skip_unload = false; return; }
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

DECL_VFUNC_DYN(void, ServerCommand, const char *)

// XXX: quick hack requested by Portal people for some timeboxed IL grind
// challenge they want to do. I think this is a terribly sad way to do this but
// at the same time it's easy and low-impact so put it in as hidden for now
// until we come up with something better later.
DEF_CVAR(_sst_onload_echo, "EXPERIMENTAL! Don't rely on this existing!", "",
		CON_HIDDEN)

static void VCALLCONV ClientActive(void *this, struct edict *player) {
	// XXX: it's kind of dumb that we get handed the edict here then go look it
	// up again in fov.c but I can't be bothered refactoring any further now
	// that this finally works, do something later lol
	if (has_fov) fov_onload();

	// continuing dumb portal hack. didn't even seem worth adding a feature for
	if (has_vtidx_ServerCommand && con_getvarstr(_sst_onload_echo)[0]) {
		char *s = malloc(8 + _sst_onload_echo->strlen); // dumb lol
		if (s) { // if not, game probably exploded already
			memcpy(s, "echo \"", 6);
			// note: assume there's no quotes in the variable, because there
			// should be no way to do this at least via console
			memcpy(s + 6, con_getvarstr(_sst_onload_echo),
					_sst_onload_echo->strlen);
			memcpy(s + 6 + _sst_onload_echo->strlen - 1, "\"\n", 3);
			VCALL(engserver, ServerCommand, s);
			free(s);
		}
	}
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
	(void *)&nop_pii_v,	// ServerActivate
	(void *)&nop_b_v,	// GameFrame
	(void *)&nop_v_v,	// LevelShutdown
	(void *)&ClientActive
	// At this point, Alien Swarm and Portal 2 add ClientFullyConnect, so we
	// can't hardcode any more of the layout!
};
// end MUST point AFTER the last of the above entries
static const void **vtable_firstdiff = vtable + 10;
// this is equivalent to a class with no members!
static const void *const *const plugin_obj = vtable;

EXPORT const void *CreateInterface(const char *name, int *ret) {
	if (!strncmp(name, "ISERVERPLUGINCALLBACKS00", 24)) {
		if (name[24] >= '1' && name[24] <= '3' && name[25] == '\0') {
			if (ret) *ret = 0;
			ifacever = name[24] - '0';
			return &plugin_obj;
		}
	}
	if (ret) *ret = 1;
	return 0;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
