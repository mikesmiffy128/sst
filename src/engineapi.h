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
extern struct VEngineClient *engclient;

struct VEngineServer {
	void **vtable;
	/* opaque fields */
};
extern struct VEngineServer *engserver;

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
	int stateflags;
	int netserial;
	void *ent_networkable;
	void *ent_unknown;
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

/// }}}

/*
 * Called on plugin init to attempt to initialise various core interfaces.
 * Doesn't return an error result, because the plugin can still load even if
 * this stuff is missing.
 *
 * Also performs additional gametype detection after con_init(), before
 * gamedata_init().
 */
void engineapi_init(void);

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80 fdm=marker
