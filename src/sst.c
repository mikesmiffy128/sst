/*
 * Copyright © 2023 Michael Smith <mikesmiffy128@gmail.com>
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

#ifndef _WIN32
#include <stdlib.h> // unsetenv
#endif
#include <string.h>

#ifdef _WIN32
#include <shlwapi.h>
#endif

#include "con_.h"
#include "engineapi.h"
#include "errmsg.h"
#include "event.h"
#include "fixes.h"
#include "gamedata.h"
#include "gameinfo.h"
#include "gametype.h"
#include "hook.h"
#include "os.h"
#include "sst.h"
#include "vcall.h"
#include "version.h"

#ifdef _WIN32
#define fS "S"
#else
#define fS "s"
#endif

static int ifacever;

// XXX: exposing this clumsily to portalcolours. we should have a better way of
// exposing lib handles in general, probably.
void *clientlib = 0;

bool sst_earlyloaded = false; // see deferinit() below
bool sst_userunloaded = false; // see hook_plugin_unload_cb() below

#define VDFBASENAME "SourceSpeedrunTools"

#ifdef _WIN32
extern long __ImageBase; // this is actually the PE header struct but don't care
static inline void *ownhandle(void) { return &__ImageBase; }
#else
// sigh, _GNU_SOURCE crap. define here instead >:(
typedef struct {
	const char *dli_fname;
	void *dli_fbase;
	const char *dli_sname;
	void *dli_saddr;
} Dl_info;
int dladdr1(const void *addr, Dl_info *info, void **extra_info, int flags);
static void *ownhandle(void) {
	static void *cached = 0;
	Dl_info dontcare;
	if (!cached) {
		dladdr1((void *)&ownhandle, &dontcare, &cached, /*RTLD_DL_LINKMAP*/ 2);
	}
	return cached;
}

struct gnu_link_map *_os_lmbase = 0; // XXX: stupid place to put this, oh well
#endif

#ifdef _WIN32
// not a proper check, just a short-circuit check to avoid doing more work.
static inline bool checksamedrive(const ushort *restrict path1,
		const ushort *restrict path2) {
	bool ret = (path1[0] | 32) == (path2[0] | 32);
	if (!ret) errmsg_errorx("game and plugin must be on the same drive\n");
	return ret;
}
#endif

DEF_CCMD_HERE(sst_autoload_enable, "Register SST to load on game startup", 0) {
	os_char path[PATH_MAX];
	if (os_dlfile(ownhandle(), path, sizeof(path) / sizeof(*path)) == -1) {
		// hopefully by this point this won't happen, but, like, never know
		errmsg_errordl("failed to get path to plugin");
		return;
	}
	os_char _startdir[PATH_MAX];
	const os_char *startdir;
	if (ifacever == 2) {
		startdir = _startdir;
		os_getcwd(_startdir, PATH_MAX); // if this fails, OS devs are all fired.
#ifdef _WIN32
		// note: strictly speaking we *could* allow this with an absolute path
		// since old builds allow absolute plugin_load paths but since it's less
		// reliable if e.g. a disk is removed, and also doesn't work for all
		// games, just rule it out entirely to keep things simple.
		if (!checksamedrive(path, startdir)) return;
#endif
		int len = os_strlen(startdir);
		if (len + sizeof("/bin") >= PATH_MAX) {
			errmsg_errorx("path to game is too long");
			return;
		}
		memcpy(_startdir + len,
#ifdef _WIN32
				L"\\bin", // PathRelativePathToW actually NEEDS a backslash, UGH
#else
				"/bin",
#endif
				5 * sizeof(os_char));
	}
	else /* ifacever == 3 */ {
		// newer games load from the mod dir instead of engine bin, and search
		// in inherited search paths too, although we don't bother with those as
		// the actual VDF is only read from the mod itself so it's always enough
		// to make the path relative to that (and that makes the actual plugin
		// search fast too as it should find it in the first place it looks).
		// we *still* refuse to autoload across different drives even if some
		// obscure gameinfo.txt arrangement could technically allow that to work
		startdir = gameinfo_gamedir;
#ifdef _WIN32
		if (!checksamedrive(path, startdir)) return;
#endif
	}
	os_char relpath[PATH_MAX];
#ifdef _WIN32
	// note: dll isn't actually in gamedir if it's in a base mod directory
	// note: gamedir doesn't account for if the dll is in a base mod's
	// directory, although it will yield a valid/working relative path anyway.
	if (!PathRelativePathToW(relpath, startdir, FILE_ATTRIBUTE_DIRECTORY,
			path, 0)) {
		errmsg_errorsys("couldn't compute a relative path");
		return;
	}
	// arbitrary aesthetic judgement
	for (ushort *p = relpath; *p; ++p) if (*p == L'\\') *p = L'/';
#else
	const char *p = path, *q = startdir;
	int slash = 0;
	int i = 1;
	for (;; ++i) {
		if (p[i] == '/' && (q[i] == '/' || q[i] == '\0')) slash = i;
		if (p[i] != q[i]) break;
	}
	int rellen = strlen(p + slash + 1) + 1; // include \0
	char *r = relpath;
	if (q[i]) {
		if (r - relpath >= PATH_MAX - 3 - rellen) {
			errmsg_errorx("path to game is too long"); // eh...
			return;
		}
		for (;;) {
			r[0] = '.'; r[1] = '.'; r[2] = '/';
			r += 3;
			for (;;) {
				if (q[++i] == '/') break;
				if (!q[i]) goto c;
			}
		}
	}
c:	memcpy(r, p + slash + 1, rellen);
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
	// XXX: oh crap, we're clobbering unicode again. welp, let's continue
	// relying on the theory that the engine would fail to deal with it anyway.
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

// most plugin callbacks are unused - define dummy functions for each signature
static void VCALLCONV nop_v_v(void *this) {}
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

static bool already_loaded = false, skip_unload = false;

// auto-update message. see below in do_featureinit()
static const char *updatenotes = "\
* Cleaned up even more code, because it's hard to be perfect all the time\n\
";

#include <featureinit.gen.h> // generated by build/codegen.c

static void do_featureinit(void) {
	// load libs that might not be there early (...at least on Linux???)
	clientlib = os_dlhandle(OS_LIT("client") OS_LIT(OS_DLSUFFIX));
	if (!clientlib) {
		errmsg_warndl("couldn't get the game's client library");
	}
	else if (!(factory_client = (ifacefactory)os_dlsym(clientlib,
			"CreateInterface"))) {
		errmsg_warndl("couldn't get client's CreateInterface");
	}
	void *inputsystemlib = os_dlhandle(OS_LIT("bin/") OS_LIT("inputsystem")
			OS_LIT(OS_DLSUFFIX));
	if (!inputsystemlib) {
		errmsg_warndl("couldn't get the input system library");
	}
	else if (!(factory_inputsystem = (ifacefactory)os_dlsym(inputsystemlib,
			"CreateInterface"))) {
		errmsg_warndl("couldn't get input system's CreateInterface");
	}
	else if (!(inputsystem = factory_inputsystem("InputSystemVersion001", 0))) {
		errmsg_warnx("missing input system interface");
	}
	// ... and now for the real magic!
	initfeatures();

	// if we're autoloaded and the external autoupdate script downloaded a new
	// version, let the user know about the cool new stuff!
	if (getenv("SST_UPDATED")) {
		// avoid displaying again if we're unloaded and reloaded in one session
#ifdef _WIN32
		SetEnvironmentVariableA("SST_UPDATED", 0);
#else
		unsetenv("SST_UPDATED");
#endif
		struct rgba gold = {255, 210, 0, 255};
		struct rgba white = {255, 255, 255, 255};
		con_colourmsg(&white, "\n" NAME " was just ");
		con_colourmsg(&gold, "UPDATED");
		con_colourmsg(&white, " to version ");
		con_colourmsg(&gold, "%s", VERSION);
		con_colourmsg(&white, "!\n\nNew in this version:\n%s\n", updatenotes);
	}
}

typedef void (*VCALLCONV VGuiConnect_func)(void *this);
static VGuiConnect_func orig_VGuiConnect;
static void VCALLCONV hook_VGuiConnect(void *this) {
	orig_VGuiConnect(this);
	do_featureinit();
	fixes_apply();
	unhook_vtable(*(void ***)vgui, vtidx_VGuiConnect, (void *)orig_VGuiConnect);
}

DECL_VFUNC_DYN(bool, VGuiIsInitialized)

// --- Magical deferred load order hack nonsense! ---
// VDF plugins load right after server.dll, but long before most other stuff. We
// want to be able to load via VDF so archived cvars in config.cfg can get set,
// but don't want to be so early that most of the game's interfaces haven't been
// brought up yet. Hook CEngineVGui::Connect(), which is called very late in
// startup, in order to init the features properly.
//
// Route credit to bill for helping figure a lot of this out - mike
static bool deferinit(void) {
	if (!vgui) {
		errmsg_warnx("can't use VEngineVGui for deferred feature setup");
		goto e;
	}
	// Arbitrary check to infer whether we've been early- or late-loaded.
	// We used to just see whether gameui.dll/libgameui.so was loaded, but
	// Portal 2 does away with the separate gameui library, so now we just call
	// CEngineVGui::IsInitialized() which works everywhere.
	if (VGuiIsInitialized(vgui)) return false;
	sst_earlyloaded = true; // let other code know
	if (!os_mprot(*(void ***)vgui + vtidx_VGuiConnect, sizeof(void *),
			PAGE_READWRITE)) {
		errmsg_warnsys("couldn't make CEngineVGui vtable writable for deferred "
				"feature setup");
		goto e;
	}
	orig_VGuiConnect = (VGuiConnect_func)hook_vtable(*(void ***)vgui,
			vtidx_VGuiConnect, (void *)&hook_VGuiConnect);
	return true;

e:	con_warn("!!! SOME FEATURES MAY BE BROKEN !!!\n");
	// Lesser of two evils: just init features now. Unlikely to happen anyway.
	return false;
}

DEF_PREDICATE(AllowPluginLoading, bool)
DEF_EVENT(PluginLoaded, void)
DEF_EVENT(PluginUnloaded, void)

static struct con_cmd *cmd_plugin_load, *cmd_plugin_unload;
static con_cmdcb orig_plugin_load_cb, orig_plugin_unload_cb;

static int ownidx; // XXX: super hacky way of getting this to do_unload()

static void hook_plugin_load_cb(const struct con_cmdargs *args) {
	if (args->argc == 1) return;
	if (!CHECK_AllowPluginLoading(true)) return;
	orig_plugin_load_cb(args);
	EMIT_PluginLoaded();
}
static void hook_plugin_unload_cb(const struct con_cmdargs *args) {
	if (args->argc == 1) return;
	if (!CHECK_AllowPluginLoading(false)) return;
	int idx = atoi(args->argv[1]);
	struct CPlugin **plugins = pluginhandler->plugins.m.mem;
	if (idx >= 0 && idx < pluginhandler->plugins.sz &&
			plugins[idx]->theplugin == &plugin_obj) {
		sst_userunloaded = true;
		ownidx = idx;
#ifdef __clang__
		// thanks clang for forcing use of return here and THEN warning about it
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpedantic"
		__attribute__((musttail)) return orig_plugin_unload_cb(args);
#pragma clang diagnostic pop
#else
#error We are tied to clang without an assembly solution for this!
#endif
	}
	// if it's some other plugin being unloaded, we can keep doing stuff after
	orig_plugin_unload_cb(args);
	EMIT_PluginUnloaded();
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
	if (!deferinit()) { do_featureinit(); fixes_apply(); }
	if (pluginhandler) {
		cmd_plugin_load = con_findcmd("plugin_load");
		orig_plugin_load_cb = cmd_plugin_load->cb;
		cmd_plugin_load->cb = &hook_plugin_load_cb;
		cmd_plugin_unload = con_findcmd("plugin_unload");
		orig_plugin_unload_cb = cmd_plugin_unload->cb;
		cmd_plugin_unload->cb = &hook_plugin_unload_cb;
	}
	return true;
}

static void do_unload(void) {
#ifdef _WIN32 // this is only relevant in builds that predate linux support
	if (pluginhandler) { // if not, oh well too bad :^)
		cmd_plugin_load->cb = orig_plugin_load_cb;
		cmd_plugin_unload->cb = orig_plugin_unload_cb;
		if (sst_userunloaded) {
			struct CPlugin **plugins = pluginhandler->plugins.m.mem;
			// see comment in CPlugin above. setting this to the real handle
			// right before the engine tries to unload us allows it to actually
			// do so. in newer branches this is redundant but doesn't do any
			// harm so it's just unconditional. NOTE: old engines ALSO just leak
			// the handle and never call Unload() if Load() fails; can't really
			// do anything about that.
			plugins[ownidx]->module = ownhandle();
		}
	}
#endif
	endfeatures();
	con_disconnect();
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

DEF_EVENT(ClientActive, struct edict */*player*/)
DEF_EVENT(Tick, bool /*simulating*/)

// Quick and easy server tick event. Eventually, we might want a deeper hook
// for anything timing-sensitive, but this will do for our current needs.
static void VCALLCONV GameFrame(void *this, bool simulating) {
	EMIT_Tick(simulating);
}

static void VCALLCONV ClientActive(void *this, struct edict *player) {
	EMIT_ClientActive(player);
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
	(void *)&GameFrame,
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

// no better place to put this lol
#include <evglue.gen.h> // generated by src/build/codegen.c

// vi: sw=4 ts=4 noet tw=80 cc=80
