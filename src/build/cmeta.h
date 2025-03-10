/*
 * Copyright © 2025 Michael Smith <mikesmiffy128@gmail.com>
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

#ifndef INC_CMETA_H
#define INC_CMETA_H

#include "../intdefs.h"
#include "../os.h"

// XXX: leaking chibicc internals. won't matter after we do away with that
typedef struct Token Token;

enum cmeta_item {
	CMETA_ITEM_DEF_CVAR, // includes all min/max/unreg variants
	CMETA_ITEM_DEF_CCMD, // includes plusminus/unreg variants
	CMETA_ITEM_DEF_EVENT, // includes predicates
	CMETA_ITEM_HANDLE_EVENT,
	CMETA_ITEM_FEATURE,
	CMETA_ITEM_REQUIRE, // includes all REQUIRE_*/REQUEST variants
	CMETA_ITEM_GAMESPECIFIC,
	CMETA_ITEM_PREINIT,
	CMETA_ITEM_INIT,
	CMETA_ITEM_END
};

struct cmeta {
	char *sbase;
	u32 nitems; // number of interesting macros
	//u32 *itemoffs; // file offsets of interesting macros (ONE DAY!)
	Token **itemtoks; // crappy linked token structures, for the time being
	u8 *itemtypes; // CMETA_ITEM_* enum values
};

enum cmeta_flag_cvar {
	CMETA_CVAR_UNREG = 1,
	CMETA_CVAR_FEAT = 2,
};
enum cmeta_flag_ccmd {
	CMETA_CCMD_UNREG = 1,
	CMETA_CCMD_FEAT = 2,
	CMETA_CCMD_PLUSMINUS = 4
};
enum cmeta_flag_event {
	CMETA_EVENT_ISPREDICATE = 1
};
enum cmeta_flag_require {
	CMETA_REQUIRE_OPTIONAL = 1, // i.e. REQUEST() macro, could be extended
	CMETA_REQUIRE_GAMEDATA = 2,
	CMETA_REQUIRE_GLOBAL = 4
};

struct cmeta_slice { const char *s; int len; };

struct cmeta cmeta_loadfile(const os_char *path);
int cmeta_flags_cvar(const struct cmeta *cm, u32 i);
int cmeta_flags_ccmd(const struct cmeta *cm, u32 i);
int cmeta_flags_event(const struct cmeta *cm, u32 i);
int cmeta_flags_require(const struct cmeta *cm, u32 i);

int cmeta_nparams(const struct cmeta *cm, u32 i);
struct cmeta_param_iter { Token *cur; };
struct cmeta_param_iter cmeta_param_iter_init(const struct cmeta *cm, u32 i);
struct cmeta_slice cmeta_param_iter(struct cmeta_param_iter *it);

#define cmeta_param_foreach(varname, cm, u32) \
	switch (0) for (struct cmeta_slice varname; 0;) default: \
		for (struct cmeta_param_iter _it = cmeta_param_iter_init(cm, i); \
				varname = cmeta_param_iter(&_it), varname.s;) \
			/* {...} */

u32 cmeta_line(const struct cmeta *cm, u32 i);

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
