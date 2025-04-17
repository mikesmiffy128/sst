/*
 * Copyright © Willian Henrique <wsimanbrazil@yahoo.com.br>
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

#include "engineapi.h"
#include "errmsg.h"
#include "feature.h"
#include "gametype.h"
#include "intdefs.h"
#include "trace.h"
#include "vcall.h"

FEATURE()
// TODO(compat): limiting to tested branches for now; support others as needed
GAMESPECIFIC(L4D)

struct ray {
	// these have type VectorAligned in the engine, which occupies 16 bytes
	struct vec3f _Alignas(16) start, delta, startoff, extents;
	// align to 16 since "extents" is supposed to occupy 16 bytes.
	// TODO(compat): this member isn't in every engine branch
	const float _Alignas(16) (*worldaxistransform)[3][4];
	bool isray, isswept;
};

static struct IEngineTraceServer *srvtrace;
DECL_VFUNC(struct IEngineTraceServer, void, TraceRay, 5,
		struct ray *, uint /*mask*/, void */*filter*/, struct CGameTrace *)

static inline bool nonzero(struct vec3f v) {
	union { struct vec3f v; struct { unsigned int x, y, z; }; } u = {v};
	return (u.x | u.y | u.z) << 1 != 0; // ignore sign bit
}

struct CGameTrace trace_line(struct vec3f start, struct vec3f end, uint mask,
		void *filt) {
	struct CGameTrace t;
	struct vec3f delta = {end.x - start.x, end.y - start.y, end.z - start.z};
	struct ray r = {
		.isray = true,
		.isswept = nonzero(delta),
		.start = start,
		.delta = delta
	};
	TraceRay(srvtrace, &r, mask, filt, &t);
	return t;
}

struct CGameTrace trace_hull(struct vec3f start, struct vec3f end,
		struct vec3f mins, struct vec3f maxs, uint mask, void *filt) {
	struct CGameTrace t;
	struct vec3f delta = {end.x - start.x, end.y - start.y, end.z - start.z};
	struct vec3f extents = {
		(maxs.x - mins.x) * 0.5f,
		(maxs.y - mins.y) * 0.5f,
		(maxs.z - mins.z) * 0.5f
	};
	struct ray r = {
		// NOTE: could maybe hardcode this to false, but we copy engine logic
		// just on the off chance we're tracing some insanely thin hull
		.isray = (extents.x * extents.x + r.extents.y * r.extents.y +
				extents.z * extents.z) < 1e-6,
		.isswept = nonzero(delta),
		.start = start,
		.delta = delta,
		.extents = extents,
		.startoff = {
			(mins.x + maxs.x) * -0.5f,
			(mins.y + maxs.y) * -0.5f,
			(mins.z + maxs.z) * -0.5f
		}
	};
	TraceRay(srvtrace, &r, mask, filt, &t);
	return t;
}

INIT {
	if (!(srvtrace = factory_engine("EngineTraceServer003", 0))) {
		errmsg_errorx("couldn't get server-side tracing interface");
		return FEAT_INCOMPAT;
	}
	return FEAT_OK;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
