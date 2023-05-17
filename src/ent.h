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

#ifndef INC_ENT_H
#define INC_ENT_H

#include "engineapi.h"
#include "vcall.h"

/* Returns a server-side edict pointer, or null if not found. */
struct edict *ent_getedict(int idx);

/* Returns an opaque pointer to a server-side entity, or null if not found. */
void *ent_get(int idx);

struct CEntityFactory; // opaque for now, can move out of ent.c if needed later

/*
 * Returns the CEntityFactory for a given entity name, or null if not found or
 * unavailable. This provides a means to create and destroy entities of that
 * type on the server, as well as opportunities to dig through instructions to
 * find useful stuff.
 */
const struct CEntityFactory *ent_getfactory(const char *name);

/*
 * Attempts to find the virtual table of an entity class given only its factory.
 * Returns null if that couldn't be done. Can be used to hook things without
 * having an instance of the class up-front, or perform further snooping to get
 * even deeper into an entity's code.
 *
 * The classname parameter is used for error messages on failed instruction
 * searches and isn't used for the search itself.
 */
void **ent_findvtable(const struct CEntityFactory *factory,
		const char *classname);

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
