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

#include "../chunklets/clex.h"
#include "../intdefs.h"
#include "../langext.h"
#include "../os.h"
#include "cmeta.h"

static cold noreturn die(int status, const char *s) {
	fprintf(stderr, "cmeta: fatal: %s\n", s);
	exit(status);
}

struct cmeta cmeta_loadfile(const os_char *path) {
	int f = os_open_read(path);
	if_cold (f == -1) die(100, "couldn't open file");
	vlong len = os_fsize(f);
	if_cold (len == 0) die(2, "empty source file");
	// limit this a little lower so that clex_memreq doesn't overflow if (for
	// some reason!?) the host compiler target is 32-bit. no point worrying as
	// we should never have a 256MiB source file anyway!
	if_cold (len > 1u << 28 - 1) die(2, "input file is far too large");
	struct cmeta ret;
	usize lexmemreq = clex_memreq(len, os_strlen(path));
	// smallest possible item is END{} (5 chars), but it's nice to be able to go
	// >> 2, so pretend it's 4 chars. for each item we store 1 32-bit ints and
	// an 8-bit int, so the memory requirement ends up being clex_memreq() +
	// len + len >> 2. in total, including the file buffer, that should be 7.25x
	// the file size. which is pretty reasonable for normal files.
	usize memreq = lexmemreq + (len << 1) + (len >> 2);
	void *mem = malloc(memreq);
	ret.nitems = 0;
	ret.itemtoks = mem;
	// put the string and item bytes at the end so the cmeta stuff is aligned.
	ret.sbase = (char *)mem + memreq - len;
	ret.items = (struct cmeta_item *)mem + memreq - (len << 1);
	if_cold (!mem) die(100, "couldn't allocate memory");
	if_cold (os_read(f, ret.sbase, len) != len) die(100, "couldn't read file");
	os_close(f);
#ifdef _WIN32
	char *asciiname = malloc(wcslen(path) + 1);
	if_cold (!asciiname) die(100, "couldn't allocate memory");
	// XXX: being lazy about Unicode right now; a general purpose tool should
	// implement WTF8 or something. SST itself doesn't have any unicode paths
	// though, so we don't really care as much. this code still sucks though.
	*asciiname = *path;
	for (const ushort *p = path + 1; p[-1]; ++p) asciiname[p - path] = *p;
#else
	const char *asciiname = f;
#endif
	ret.lexer = clex(ret.sbase, len, (u32 *)mem + (len >> 2), asciiname);
	if (ret.lexer.err) die(2, ret.lexer.err);
	// everything is THING() or THING {}, and file also ends in an EOL, so once
	// there's less than 4 tokens left in the file, we can bail.
	for (u32 i = 0, end = ret.lexer.ntoks - 4; i < end; ++i) {
		if_hot (ret.lexer.toks[i] != CLEX_TOK_IDENT) continue;
		// technically we don't have to validate *every* token, but doing so
		// gives less confusing syntax errors.
		struct clex_ident_validate_ret val = clex_ident_validate(
				&ret.lexer, ret.sbase, i);
		if (val.err) {
			char buf[CLEX_IDENT_ERRSTR_MEMREQ(PATH_MAX)];
			clex_ident_errstr(buf, ret.sbase, val.err, val.err_off, asciiname);
			die(2, buf);
		}
		// everything we match has to be followed by either ( or {
		if (ret.lexer.toks[i + 1] != CLEX_TOK_OP1) { ++i; continue; }
		if (val.len > 24) continue; // longer than the longest string below
		char name[24];
		clex_ident(&ret.lexer, ret.sbase, i, name);
		int type;
		int flags = 0;
		char nextop = '(';
		// this is kind of dumb code. oh well, good enough probably.
		switch (val.len) {
			case 3:
				if (!memcmp(name, "END", 3)) {
					type = CMETA_ITEM_END;
					nextop = '{';
					break;
				}
				continue;
			case 4:
				if (!memcmp(name, "INIT", 4)) {
					type = CMETA_ITEM_INIT;
					nextop = '{';
					break;
				}
				continue;
			case 7:
				if (!memcmp(name, "FEATURE", 7)) {
					type = CMETA_ITEM_FEATURE;
					break;
				}
				if (!memcmp(name, "PREINIT", 7)) {
					type = CMETA_ITEM_PREINIT;
					nextop = '{';
					break;
				}
				if (!memcmp(name, "REQUIRE", 7)) {
					type = CMETA_ITEM_REQUIRE;
					break;
				}
				if (!memcmp(name, "REQUEST", 7)) {
					type = CMETA_ITEM_REQUIRE;
					flags = CMETA_REQUIRE_OPTIONAL;
					break;
				}
				continue;
			case 8:
				if (!memcmp(name, "DEF_CCMD", 8)) {
					type = CMETA_ITEM_DEF_CCMD;
					break;
				}
				if (!memcmp(name, "DEF_CVAR", 8)) {
					type = CMETA_ITEM_DEF_CVAR;
					break;
				}
				continue;
			case 9:
				if (!memcmp(name, "DEF_EVENT", 9)) {
					type = CMETA_ITEM_DEF_EVENT;
					break;
				}
				continue;
			case 12:
				if (!memcmp(name, "DEF_CVAR_MAX", 12) ||
						!memcmp(name, "DEF_CVAR_MIN", 12)) {
					type = CMETA_ITEM_DEF_CVAR;
					break;
				}
				if (!memcmp(name, "GAMESPECIFIC", 12)) {
					type = CMETA_ITEM_GAMESPECIFIC;
					break;
				}
				if (!memcmp(name, "HANDLE_EVENT", 12)) {
					type = CMETA_ITEM_HANDLE_EVENT;
					break;
				}
				continue;
			case 13:
				if (!memcmp(name, "DEF_CCMD_HERE", 13)) {
					type = CMETA_ITEM_DEF_CCMD;
					break;
				}
				if (!memcmp(name, "DEF_FEAT_CCMD", 13)) {
					type = CMETA_ITEM_DEF_CCMD;
					flags = CMETA_CVAR_FEAT;
					break;
				}
				if (!memcmp(name, "DEF_FEAT_CVAR", 13)) {
					type = CMETA_ITEM_DEF_CVAR;
					flags = CMETA_CVAR_FEAT;
					break;
				}
				if (!memcmp(name, "DEF_PREDICATE", 13)) {
					type = CMETA_ITEM_DEF_EVENT;
					flags = CMETA_EVENT_ISPREDICATE;
					break;
				}
				continue;
			case 14:
				if (!memcmp(name, "DEF_CCMD_UNREG", 14)) {
					type = CMETA_ITEM_DEF_CCMD;
					flags = CMETA_CCMD_UNREG;
					break;
				}
				if (!memcmp(name, "DEF_CVAR_UNREG", 14)) {
					type = CMETA_ITEM_DEF_CVAR;
					flags = CMETA_CVAR_UNREG;
					break;
				}
				if (!memcmp(name, "REQUIRE_GLOBAL", 14)) {
					type = CMETA_ITEM_REQUIRE;
					flags = CMETA_REQUIRE_GLOBAL;
					break;
				}
				continue;
			case 15:
				if (!memcmp(name, "DEF_CVAR_MINMAX", 15)) {
					type = CMETA_ITEM_DEF_CVAR;
					break;
				}
				continue;
			case 16:
				if (!memcmp(name, "REQUIRE_GAMEDATA", 16)) {
					type = CMETA_ITEM_REQUIRE;
					flags = CMETA_REQUIRE_GAMEDATA;
					break;
				}
				continue;
			case 17:
				if (!memcmp(name, "DEF_FEAT_CVAR_MAX", 17) ||
						!memcmp(name, "DEF_FEAT_CVAR_MIN", 17)) {
					type = CMETA_ITEM_DEF_CVAR;
					flags = CMETA_CVAR_FEAT;
					break;
				}
				continue;
			case 18:
				if (!memcmp(name, "DEF_CCMD_PLUSMINUS", 18)) {
					type = CMETA_ITEM_DEF_CCMD;
					flags = CMETA_CCMD_PLUSMINUS;
					break;
				}
				if (!memcmp(name, "DEF_CVAR_MAX_UNREG", 18) ||
						!memcmp(name, "DEF_CVAR_MIN_UNREG", 18)) {
					type = CMETA_ITEM_DEF_CVAR;
					flags = CMETA_CVAR_UNREG;
					break;
				}
				if (!memcmp(name, "DEF_FEAT_CCMD_HERE", 18)) {
					type = CMETA_ITEM_DEF_CCMD;
					flags = CMETA_CCMD_FEAT;
					break;
				}
				continue;
			case 19:
				if (!memcmp(name, "DEF_CCMD_HERE_UNREG", 19)) {
					type = CMETA_ITEM_DEF_CCMD;
					flags = CMETA_CCMD_UNREG;
					break;
				}
				continue;
			case 20:
				if (!memcmp(name, "DEF_FEAT_CVAR_MINMAX", 20)) {
					type = CMETA_ITEM_DEF_CVAR;
					flags = CMETA_CVAR_FEAT;
					break;
				}
				continue;
			case 21:
				if (!memcmp(name, "DEF_CVAR_MINMAX_UNREG", 21)) {
					type = CMETA_ITEM_DEF_CVAR;
					flags = CMETA_CVAR_UNREG;
					break;
				}
				continue;
			case 23:
				if (!memcmp(name, "DEF_FEAT_CCMD_PLUSMINUS", 23)) {
					type = CMETA_ITEM_DEF_CCMD;
					flags = CMETA_CCMD_FEAT | CMETA_CCMD_PLUSMINUS;
					break;
				}
				continue;
			case 24:
				if (!memcmp(name, "DEF_CCMD_PLUSMINUS_UNREG", 24)) {
					type = CMETA_ITEM_DEF_CCMD;
					flags = CMETA_CCMD_UNREG | CMETA_CCMD_PLUSMINUS;
					break;
				}
			default:
				continue;
		}
		if (ret.sbase[ret.lexer.tokoffs[i + 1]] != nextop) {
			// bump i a little further as we've already looked at at least 2
			// tokens. this is technically kind of inefficient; in most cases we
			// can skip more stuff, but we're always scanning for something
			// specific, so who cares actually, this is good enough.
			++i;
			continue;
		}
		ret.itemtoks[ret.nitems] = i;
		ret.items[ret.nitems] = (struct cmeta_item){type, flags};
		++ret.nitems;
		++i;
	}
	return ret;
}

int cmeta_nparams(const struct cmeta *cm, u32 item) {
	int argc = 1, nest = 0;
	int i = cm->itemtoks[item] + 2; // get past the first (
	// handle immediate ) - XXX: stupid special case, surely improvable?
	if (clex_isrparen(&cm->lexer, cm->sbase, i)) return 0;
	for (; i < cm->lexer.ntoks; ++i) {
		if (clex_islparen(&cm->lexer, cm->sbase, i)) { ++nest; continue; }
		if (!nest && clex_iscomma(&cm->lexer, cm->sbase, i)) ++argc;
		else if (clex_isrparen(&cm->lexer, cm->sbase, i) && !nest--) break;
	}
	if (nest != -1) return 0; // XXX: any need to do anything better here?
	return argc;
}

struct cmeta_param_iter cmeta_param_iter_init(const struct cmeta *cm, u32 i) {
	return (struct cmeta_param_iter){cm->itemtoks[i] + 2};
}

struct cmeta_slice cmeta_param_iter(const struct cmeta *cm,
		struct cmeta_param_iter *it) {
	int nest = 0;
	const char *start = cm->sbase + cm->lexer.tokoffs[it->i];
	for (; it->i < cm->lexer.ntoks; ++it->i) {
		if (clex_islparen(&cm->lexer, cm->sbase, it->i)) {
			++nest;
			continue;
		}
		if (!nest && clex_iscomma(&cm->lexer, cm->sbase, it->i)) {
			const char *end = cm->sbase + cm->lexer.tokoffs[it->i];
			// XXX: to avoid picking up random whitespace we should get the
			// previous token and ask clex for its extent, but I've not yet
			// implemented full parsing of all variable-length tokens (i.e.
			// numbers and string/char literals). not worrying about it too much
			// at the moment since it's not really a problem anyway in practice
			++it->i; // skip past comma for next time
			return (struct cmeta_slice){start, end - start};
		}
		else if (clex_isrparen(&cm->lexer, cm->sbase, it->i) && !nest--) {
			// annoying case: 0 args is different from ", )" (empty arg)
			if (clex_islparen(&cm->lexer, cm->sbase, it->i - 1)) break;
			const char *end = cm->sbase + cm->lexer.tokoffs[it->i];
			it->i = -1u; // force next call to return {0, 0}
			return (struct cmeta_slice){start, end - start};
		}
	}
	return (struct cmeta_slice){0, 0};
}

cold u32 cmeta_line(const struct cmeta *cm, u32 i) {
	u32 line = 1;
	for (u32 off = 0, end = cm->lexer.tokoffs[cm->itemtoks[i]];
			off != end; ++off) {
		line += cm->sbase[off] == '\n';
	}
	return line;
}

// vi: sw=4 ts=4 noet tw=80 cc=80 fdm=marker
