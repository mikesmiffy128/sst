/*
 * Copyright © 2023 Willian Henrique <wsimanbrazil@yahoo.com.br>
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

#ifndef INC_TRACE_H
#define INC_TRACE_H

#include "intdefs.h"
#include "engineapi.h"

struct CBaseTrace {
	struct vec3f startpos, endpos;
	struct {
		struct vec3f normal;
		float dist;
		u8 type, signbits;
		//u8 pad[2];
	} plane; // surface normal at impact
	float frac;
	int contents;
	ushort dispflags;
	bool allsolid, startsolid;
};

struct CGameTrace {
	struct CBaseTrace base;
	float fracleftsolid;
	struct {
		const char *name;
		short surfprops;
		ushort flags;
	} surf;
	int hitgroup;
	short physbone;
	ushort worldsurfidx; // not in every branch, but doesn't break ABI
	void *ent; // CBaseEntity (C_BaseEntity in client.dll)
	int hitbox;
};

struct CGameTrace trace_line(struct vec3f start, struct vec3f end, uint mask,
		void *filt);

struct CGameTrace trace_hull(struct vec3f start, struct vec3f end,
		struct vec3f mins, struct vec3f maxs, uint mask, void *filt);

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
