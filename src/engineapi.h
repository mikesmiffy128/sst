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

#define SENDPROP_INT 0
#define SENDPROP_FLOAT 1
#define SENDPROP_VEC 2
#define SENDPROP_VECXY 3
#define SENDPROP_STR 4
#define SENDPROP_ARRAY 5
#define SENDPROP_DTABLE 6
#define SENDPROP_INT64 7

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
extern void *globalvars;

/*
 * Called on plugin init to attempt to initialise various core interfaces.
 * Doesn't return an error result, because the plugin can still load even if
 * this stuff is missing.
 *
 * Also performs additional gametype detection after con_init(), and calls
 * gamedata_init() to setup offsets and such.
 */
void engineapi_init(void);

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80 fdm=marker
