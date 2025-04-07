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

/*
 * This header was named by mlugg. Please direct all filename-related
 * bikeshedding to <mlugg@mlugg.co.uk>.
 */

#ifndef INC_ENGINEAPI_H
#define INC_ENGINEAPI_H

#include "intdefs.h"
#include "vcall.h"

/*
 * Here, we define a bunch of random data types as well as interfaces that don't
 * have abstractions elsewhere but nonetheless need to be used in a few
 * different places.
 */

/* Access to game and engine factories obtained on plugin load */
typedef void *(*ifacefactory)(const char *name, int *ret);
extern ifacefactory factory_client, factory_server, factory_engine,
	   factory_inputsystem;

// various engine types {{{

struct VEngineClient {
	void **vtable;
	/* opaque fields */
};

struct VEngineServer {
	void **vtable;
	/* opaque fields */
};

struct CUtlMemory {
	void *mem;
	int alloccnt, growsz;
};
struct CUtlVector {
	struct CUtlMemory m;
	int sz;
	void *mem_again_for_some_reason;
};

struct edict {
	// CBaseEdict
	int stateflags;
	int netserial;
	void *ent_networkable;
	void *ent_unknown;
	// edict_t
	// NOTE! *REMOVED* in l4d-based branches. don't iterate over edict pointers!
	float freetime;
};

struct vec3f { float x, y, z; };

/* an RGBA colour, used for colour console messages as well as VGUI/HUD stuff */
struct rgba {
	union {
		struct { u8 r, g, b, a; };
		u32 val;
		uchar bytes[4];
	};
};

struct CMoveData {
	bool firstrun : 1, gamecodemoved : 1;
	ulong playerhandle;
	int impulse;
	struct vec3f viewangles, absviewangles;
	int buttons, oldbuttons;
	float mv_forward, mv_side, mv_up;
	float maxspeed, clmaxspeed;
	struct vec3f vel, angles, oldangles;
	float out_stepheight;
	struct vec3f out_wishvel, out_jumpvel;
	struct vec3f constraint_centre;
	float constraint_radius, constraint_width, constraint_speedfactor;
	struct vec3f origin;
};

// these have to be opaque because, naturally, they're unstable between
// branches - access stuff using gamedata offsets as usual
struct RecvProp;
struct SendProp;

// these two things seem to be stable enough for our purposes
struct SendTable {
	struct SendProp *props;
	int nprops;
	char *tablename;
	void *precalc;
	bool inited : 1;
	bool waswritten : 1;
	/* "has props encoded against current tick count" ??? */
	bool haspropsenccurtickcnt : 1;
};
struct ServerClass {
	char *name;
	struct SendTable *table;
	struct ServerClass *next;
	int id;
	int instbaselineidx;
};

/// }}}

extern struct VEngineClient *engclient;
extern struct VEngineServer *engserver;
extern void *srvdll;
extern void *globalvars;
extern void *inputsystem, *vgui;

// XXX: not exactly engine *API* but not curently clear where else to put this
struct CPlugin_common {
	bool paused;
	void *theplugin; // our own "this" pointer (or whichever other plugin it is)
	int ifacever;
	// should be the plugin library, but in old Source branches it's just null,
	// because CServerPlugin::Load() erroneously shadows this field with a local
	void *module;
};
struct CPlugin {
	char description[128];
	union {
		struct CPlugin_common v1;
		struct {
			char basename[128]; // WHY VALVE WHYYYYYYY!!!!
			struct CPlugin_common common;
		} v2;
	};
};
struct CServerPlugin /* : IServerPluginHelpers */ {
	void **vtable;
	struct CUtlVector plugins;
	/*IPluginHelpersCheck*/ void *pluginhlpchk;
};
extern struct CServerPlugin *pluginhandler;

// input button bits
#define IN_ATTACK (1 << 0)
#define IN_JUMP (1 << 1)
#define IN_DUCK (1 << 2)
#define IN_FORWARD (1 << 3)
#define IN_BACK (1 << 4)
#define IN_USE (1 << 5)
#define IN_CANCEL (1 << 6)
#define IN_LEFT (1 << 7)
#define IN_RIGHT (1 << 8)
#define IN_MOVELEFT (1 << 9)
#define IN_MOVERIGHT (1 << 10)
#define IN_ATTACK2 (1 << 11)
#define IN_RUN (1 << 12)
#define IN_RELOAD (1 << 13)
#define IN_ALT1 (1 << 14)
#define IN_ALT2 (1 << 15)
#define IN_SCORE (1 << 16)
#define IN_SPEED (1 << 17)
#define IN_WALK (1 << 18)
#define IN_ZOOM (1 << 19)
#define IN_WEAPON1 (1 << 20)
#define IN_WEAPON2 (1 << 21)
#define IN_BULLRUSH (1 << 22)
#define IN_GRENADE1 (1 << 23)
#define IN_GRENADE2 (1 << 24)

/*
 * Called on plugin init to attempt to initialise various core interfaces.
 * This includes console/cvar initialisation and populating gametype and
 * gamedata values.
 *
 * Returns true if there is enough stuff in place for the plugin to function -
 * there may still be stuff missing. Returns false if there's no way the plugin
 * can possibly work, e.g. if there's no cvar interface.
 */
bool engineapi_init(int pluginver);

/*
 * Called right before deferred feature initialisation to set up some additional
 * (nonessential) core stuff - currently this means entprops data.
 */
void engineapi_lateinit();

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80 fdm=marker
