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

#include <stdio.h>
#include <stdlib.h>

#include "../intdefs.h"
#include "../langext.h"
#include "../os.h"
#include "cmeta.h"

// lazy inlined 3rd party stuff {{{
// too lazy to write a C tokenizer at the moment, or indeed probably ever, so
// let's just yoink some code from a hacked-up copy of chibicc, a nice minimal C
// compiler with code that's pretty easy to work with. it does leak memory by
// design, but build stuff is all one-shot so that's fine.
#include "../3p/chibicc/chibicc.h"
#include "../3p/chibicc/unicode.c"
// type sentinels from type.c (don't bring in the rest of type.c because it
// circularly depends on other stuff and we really only want tokenize here)
Type *ty_void = &(Type){TY_VOID, 1, 1};
Type *ty_bool = &(Type){TY_BOOL, 1, 1};
Type *ty_char = &(Type){TY_CHAR, 1, 1};
Type *ty_short = &(Type){TY_SHORT, 2, 2};
Type *ty_int = &(Type){TY_INT, 4, 4};
Type *ty_long = &(Type){TY_LONG, 8, 8};
Type *ty_uchar = &(Type){TY_CHAR, 1, 1, true};
Type *ty_ushort = &(Type){TY_SHORT, 2, 2, true};
Type *ty_uint = &(Type){TY_INT, 4, 4, true};
Type *ty_ulong = &(Type){TY_LONG, 8, 8, true};
Type *ty_float = &(Type){TY_FLOAT, 4, 4};
Type *ty_double = &(Type){TY_DOUBLE, 8, 8};
Type *ty_ldouble = &(Type){TY_LDOUBLE, 16, 16};
// inline just a couple more things, super lazy, but whatever
static Type *new_type(TypeKind kind, int size, int align) {
	Type *ty = calloc(1, sizeof(Type));
	ty->kind = kind;
	ty->size = size;
	ty->align = align;
	return ty;
}
Type *array_of(Type *base, int len) {
	Type *ty = new_type(TY_ARRAY, base->size * len, base->align);
	ty->base = base;
	ty->array_len = len;
	return ty;
}
#include "../3p/chibicc/hashmap.c"
#include "../3p/chibicc/strings.c"
#include "../3p/chibicc/tokenize.c"
// }}}

#ifdef _WIN32
#include "../3p/openbsd/asprintf.c" // missing from libc; plonked here for now
#endif

static cold noreturn die(int status, const char *s) {
	fprintf(stderr, "cmeta: fatal: %s\n", s);
	exit(status);
}

struct cmeta cmeta_loadfile(const os_char *path) {
	int f = os_open_read(path);
	if_cold (f == -1) die(100, "couldn't open file");
	vlong len = os_fsize(f);
	if_cold (len > 1u << 30 - 1) die(2, "input file is far too large");
	struct cmeta ret;
	ret.sbase = malloc(len + 1);
	ret.sbase[len] = '\0'; // chibicc needs a null terminator
	if_cold (!ret.sbase) die(100, "couldn't allocate memory");
	if_cold (os_read(f, ret.sbase, len) != len) die(100, "couldn't read file");
	int maxitems = len / 4; // shortest word is "END"
	ret.nitems = 0;
	// eventual overall memory requirement: file size * 6. seems fine to me.
	// current memory requirement: file size * 10, + all the chibicc linked list
	// crap. not as good but we'll continue tolerating it... probably for years!
	//ret.itemoffs = malloc(maxitems * sizeof(*ret.itemoffs));
	//if (!ret.itemoffs) die(100, "couldn't allocate memory");
	ret.itemtoks = malloc(maxitems * sizeof(*ret.itemtoks));
	if_cold (!ret.itemtoks) die(100, "couldn't allocate memory");
	ret.itemtypes = malloc(maxitems * sizeof(*ret.itemtypes));
	if_cold (!ret.itemtypes) die(100, "couldn't allocate memory");
	os_close(f);
#ifdef _WIN32
	char *realname = malloc(wcslen(path) + 1);
	if_cold (!realname) die(100, "couldn't allocate memory");
	// XXX: being lazy about Unicode right now; a general purpose tool should
	// implement WTF8 or something. SST itself doesn't have any unicode paths
	// though, so we don't really care as much. this code still sucks though.
	*realname = *path;
	for (const ushort *p = path + 1; p[-1]; ++p) realname[p - path] = *p;
#else
	const char *realname = f;
#endif
	struct Token *t = tokenize_buf(realname, ret.sbase);
	// everything is THING() or THING {} so we need at least 3 tokens ahead - if
	// we have fewer tokens left in the file we can bail
	if (t && t->next) while (t->next->next) {
		if (!t->at_bol) {
			t = t->next;
			continue;
		}
		int type;
		if ((equal(t, "DEF_CVAR") || equal(t, "DEF_CVAR_MIN") ||
				equal(t, "DEF_CVAR_MAX") || equal(t, "DEF_CVAR_MINMAX") ||
				equal(t, "DEF_CVAR_UNREG") || equal(t, "DEF_CVAR_MIN_UNREG") ||
				equal(t, "DEF_CVAR_MAX_UNREG") ||
				equal(t, "DEF_CVAR_MINMAX_UNREG") ||
				equal(t, "DEF_FEAT_CVAR") || equal(t, "DEF_FEAT_CVAR_MIN") ||
				equal(t, "DEF_FEAT_CVAR_MAX") ||
				equal(t, "DEF_FEAT_CVAR_MINMAX")) && equal(t->next, "(")) {
			type = CMETA_ITEM_DEF_CVAR;
		}
		else if ((equal(t, "DEF_CCMD") || equal(t, "DEF_CCMD_HERE") ||
				equal(t, "DEF_CCMD_UNREG") || equal(t, "DEF_CCMD_HERE_UNREG") ||
				equal(t, "DEF_CCMD_PLUSMINUS") ||
				equal(t, "DEF_CCMD_PLUSMINUS_UNREG") ||
				equal(t, "DEF_FEAT_CCMD") || equal(t, "DEF_FEAT_CCMD_HERE") ||
				equal(t, "DEF_FEAT_CCMD_PLUSMINUS")) && equal(t->next, "(")) {
			type = CMETA_ITEM_DEF_CCMD;
		}
		else if ((equal(t, "DEF_EVENT") || equal(t, "DEF_PREDICATE")) &&
				equal(t->next, "(")) {
			type = CMETA_ITEM_DEF_EVENT;
		}
		else if (equal(t, "HANDLE_EVENT") && equal(t->next, "(")) {
			type = CMETA_ITEM_HANDLE_EVENT;
		}
		else if (equal(t, "FEATURE") && equal(t->next, "(")) {
			type = CMETA_ITEM_FEATURE;
		}
		else if ((equal(t, "REQUIRE") || equal(t, "REQUIRE_GAMEDATA") ||
				equal(t, "REQUIRE_GLOBAL") || equal(t, "REQUEST")) &&
				equal(t->next, "(")) {
			type = CMETA_ITEM_REQUIRE;
		}
		else if (equal(t, "GAMESPECIFIC") && equal(t->next, "(")) {
			type = CMETA_ITEM_GAMESPECIFIC;
		}
		else if (equal(t, "PREINIT") && equal(t->next, "{")) {
			type = CMETA_ITEM_PREINIT;
		}
		else if (equal(t, "INIT") && equal(t->next, "{")) {
			type = CMETA_ITEM_INIT;
		}
		else if (equal(t, "END") && equal(t->next, "{")) {
			type = CMETA_ITEM_END;
		}
		else {
			t = t->next;
			continue;
		}
		ret.itemtoks[ret.nitems] = t;
		ret.itemtypes[ret.nitems] = type;
		++ret.nitems;
		// this is kind of inefficient; in most cases we can skip more stuff,
		// but then also, we're always scanning for something specific, so who
		// cares actually, this will do for now.
		t = t->next->next;
	}
	return ret;
}

int cmeta_flags_cvar(const struct cmeta *cm, u32 i) {
	struct Token *t = cm->itemtoks[i];
	switch_exhaust (t->len) {
		// It JUST so happens all of the possible tokens here have a unique
		// length. I swear this wasn't planned. But it IS convenient!
		case 8: case 12: case 15: return 0;
		case 14: case 18: case 21: return CMETA_CVAR_UNREG;
		case 13: case 17: case 20: return CMETA_CVAR_FEAT;
	}
}

int cmeta_flags_ccmd(const struct cmeta *cm, u32 i) {
	struct Token *t = cm->itemtoks[i];
	switch_exhaust (t->len) {
		case 13: if (t->loc[4] == 'F') return CMETA_CCMD_FEAT;
		case 8: return 0;
		case 18: if (t->loc[4] == 'F') return CMETA_CCMD_FEAT;
			return CMETA_CCMD_PLUSMINUS;
		case 14: case 19: return CMETA_CCMD_UNREG;
		case 23: return CMETA_CCMD_FEAT | CMETA_CCMD_PLUSMINUS;
		case 24: return CMETA_CCMD_UNREG | CMETA_CCMD_PLUSMINUS;
	}
}

int cmeta_flags_event(const struct cmeta *cm, u32 i) {
	// assuming CMETA_EVENT_ISPREDICATE remains 1, the ternary should
	// optimise out
	return cm->itemtoks[i]->len == 13 ? CMETA_EVENT_ISPREDICATE : 0;
}

int cmeta_flags_require(const struct cmeta *cm, u32 i) {
	struct Token *t = cm->itemtoks[i];
	// NOTE: this is somewhat more flexible to enable REQUEST_GAMEDATA or
	// something in future, although that's kind of useless currently
	int optflag = t->loc[4] == 'E'; // REQU[E]ST
	switch_exhaust (t->len) {
		case 7: return optflag;
		case 16: return optflag | CMETA_REQUIRE_GAMEDATA;
		case 14: return optflag | CMETA_REQUIRE_GLOBAL;
	};
}

int cmeta_nparams(const struct cmeta *cm, u32 i) {
	int argc = 1, nest = 0;
	struct Token *t = cm->itemtoks[i]->next->next;
	if (equal(t, ")")) return 0; // XXX: stupid special case, surely improvable?
	for (; t; t = t->next) {
		if (equal(t, "(")) { ++nest; continue; }
		if (!nest && equal(t, ",")) ++argc;
		else if (equal(t, ")") && !nest--) break;
	}
	if (nest != -1) return 0; // XXX: any need to do anything better here?
	return argc;
}

struct cmeta_param_iter cmeta_param_iter_init(const struct cmeta *cm, u32 i) {
	return (struct cmeta_param_iter){cm->itemtoks[i]->next->next};
}

struct cmeta_slice cmeta_param_iter(struct cmeta_param_iter *it) {
	int nest = 0;
	const char *start = it->cur->loc;
	for (struct Token *last = 0; it->cur;
			last = it->cur, it->cur = it->cur->next) {
		if (equal(it->cur, "(")) { ++nest; continue; }
		if (!nest && equal(it->cur, ",")) {
			if (!last) { // , immediately after (, for some reason. treat as ""
				return (struct cmeta_slice){start, 0};
			}
			it->cur = it->cur->next;
		}
		else if (equal(it->cur, ")") && !nest--) {
			if (!last) break;
		}
		else {
			continue;
		}
		return (struct cmeta_slice){start, last->loc - start + last->len};
	}
	return (struct cmeta_slice){0, 0};
}

u32 cmeta_line(const struct cmeta *cm, u32 i) {
	return cm->itemtoks[i]->line_no;
}

// vi: sw=4 ts=4 noet tw=80 cc=80 fdm=marker
