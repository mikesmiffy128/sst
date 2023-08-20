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

#include <stdio.h>
#include <string.h>

#include "../intdefs.h"
#include "../os.h"
#include "cmeta.h"
#include "vec.h"

/*
 * This file does C metadata parsing/scraping for the build system. This
 * facilitates tasks ranging from determining header dependencies to searching
 * for certain magic macros (for example cvar/command declarations) to generate
 * other code.
 *
 * It's a bit of a mess since it's kind of just hacked together for use at build
 * time. Don't worry about it too much.
 */

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
// one more copypaste from preprocess.c for #include <filename> and then I'm
// done I promise
static char *join_tokens(const Token *tok, const Token *end) {
  int len = 1;
  for (const Token *t = tok; t != end && t->kind != TK_EOF; t = t->next) {
    if (t != tok && t->has_space)
      len++;
    len += t->len;
  }
  char *buf = calloc(1, len);
  int pos = 0;
  for (const Token *t = tok; t != end && t->kind != TK_EOF; t = t->next) {
    if (t != tok && t->has_space)
      buf[pos++] = ' ';
    strncpy(buf + pos, t->loc, t->len);
    pos += t->len;
  }
  buf[pos] = '\0';
  return buf;
}
// }}}

#ifdef _WIN32
#include "../3p/openbsd/asprintf.c" // missing from libc; plonked here for now
#endif

static void die(const char *s) {
	fprintf(stderr, "cmeta: fatal: %s\n", s);
	exit(100);
}

static char *readsource(const os_char *f) {
	int fd = os_open(f, O_RDONLY);
	if (fd == -1) return 0;
	uint bufsz = 8192;
	char *buf = malloc(bufsz);
	if (!buf) die("couldn't allocate memory");
	int nread;
	int off = 0;
	while ((nread = read(fd, buf + off, bufsz - off)) > 0) {
		off += nread;
		if (off == bufsz) {
			bufsz *= 2;
			// somewhat arbitrary cutoff
			if (bufsz == 1 << 30) die("input file is too large");
			buf = realloc(buf, bufsz);
			if (!buf) die("couldn't reallocate memory");
		}
	}
	if (nread == -1) die("couldn't read file");
	buf[off] = 0;
	close(fd);
	return buf;
}

// as per cmeta.h this is totally opaque; it's actually just a Token in disguise
struct cmeta;

const struct cmeta *cmeta_loadfile(const os_char *f) {
	char *buf = readsource(f);
	if (!buf) return 0;
#ifdef _WIN32
	char *realname = malloc(wcslen(f) + 1);
	if (!realname) die("couldn't allocate memory");
	// XXX: being lazy about Unicode right now; a general purpose tool should
	// implement WTF8 or something. SST itself doesn't have any unicode paths
	// though, so don't really care as much.
	*realname = *f;
	for (const ushort *p = f + 1; p[-1]; ++p) realname[p - f] = *p;
#else
	const char *realname = f;
#endif
	return (const struct cmeta *)tokenize_buf(realname, buf);
}

// NOTE: we don't care about conditional includes, nor do we expand macros. We
// just parse the minimum info to get what we need for SST. Also, there's not
// too much in the way of syntax checking; if an error gets ignored the compiler
// picks it up anyway, and gives far better diagnostics.
void cmeta_includes(const struct cmeta *cm,
		void (*cb)(const char *f, bool issys, void *ctxt), void *ctxt) {
	const Token *tp = (const Token *)cm;
	if (!tp || !tp->next || !tp->next->next) return; // #, include, "string"
	while (tp) {
		if (!tp->at_bol || !equal(tp, "#")) { tp = tp->next; continue; }
		if (!equal(tp->next, "include")) { tp = tp->next->next; continue; }
		tp = tp->next->next;
		if (!tp) break;
		if (tp->at_bol) tp = tp->next;
		if (!tp) break;
		if (tp->kind == TK_STR) {
			// include strings are a special case; they don't have \escapes.
			char *copy = malloc(tp->len - 1);
			if (!copy) die("couldn't allocate memory");
			memcpy(copy, tp->loc + 1, tp->len - 2);
			copy[tp->len - 2] = '\0';
			cb(copy, false, ctxt);
			//free(copy); // ??????
		}
		else if (equal(tp, "<")) {
			tp = tp->next;
			if (!tp) break;
			const Token *end = tp;
			while (!equal(end, ">")) {
				end = end->next;
				if (!end) return; // shouldn't happen in valid source obviously
				if (end->at_bol) break; // ??????
			}
			char *joined = join_tokens(tp, end); // just use func from chibicc
			cb(joined, true, ctxt);
			//free(joined); // ??????
		}
		// get to the next line (standard allows extra tokens because)
		while (!tp->at_bol) {
			tp = tp->next;
			if (!tp) return;
		}
	}
}

// AGAIN, NOTE: this doesn't *perfectly* match top level decls only in the event
// that someone writes something weird, but we just don't really care because
// we're not writing something weird. Don't write something weird!
void cmeta_conmacros(const struct cmeta *cm,
		void (*cb)(const char *, bool, bool)) {
	const Token *tp = (const Token *)cm;
	if (!tp || !tp->next || !tp->next->next) return; // DEF_xyz, (, name
	while (tp) {
		bool isplusminus = false, isvar = false;
		bool unreg = false;
		// this is like the worst thing ever, but oh well it's just build time
		// XXX: tidy this up some day, though, probably
		if (equal(tp, "DEF_CCMD_PLUSMINUS")) {
			isplusminus = true;
		}
		else if (equal(tp, "DEF_CCMD_PLUSMINUS_UNREG")) {
			isplusminus = true;
			unreg = true;
		}
		else if (equal(tp, "DEF_CVAR") || equal(tp, "DEF_CVAR_MIN") ||
				equal(tp, "DEF_CVAR_MAX") || equal(tp, "DEF_CVAR_MINMAX")) {
			isvar = true;
		}
		else if (equal(tp, "DEF_CVAR_UNREG") ||
				equal(tp, "DEF_CVAR_MIN_UNREG") ||
				equal(tp, "DEF_CVAR_MAX_UNREG") ||
				equal(tp, "DEF_CVAR_MINMAX_UNREG")) {
			isvar = true;
			unreg = true;
		}
		else if (equal(tp, "DEF_CCMD_UNREG") ||
				equal(tp, "DEF_CCMD_HERE_UNREG")) {
			unreg = true;
		}
		else if (!equal(tp, "DEF_CCMD") && !equal(tp, "DEF_CCMD_HERE")) {
			tp = tp->next; continue;
		}
		if (!equal(tp->next, "(")) { tp = tp->next->next; continue; }
		tp = tp->next->next;
		if (isplusminus) {
			// XXX: this is stupid but whatever
			char *plusname = malloc(sizeof("PLUS_") + tp->len);
			if (!plusname) die("couldn't allocate memory");
			memcpy(plusname, "PLUS_", 5);
			memcpy(plusname + sizeof("PLUS_") - 1, tp->loc, tp->len);
			plusname[sizeof("PLUS_") - 1 + tp->len] = '\0';
			cb(plusname, false, unreg);
			char *minusname = malloc(sizeof("MINUS_") + tp->len);
			if (!minusname) die("couldn't allocate memory");
			memcpy(minusname, "MINUS_", 5);
			memcpy(minusname + sizeof("MINUS_") - 1, tp->loc, tp->len);
			minusname[sizeof("MINUS_") - 1 + tp->len] = '\0';
			cb(minusname, false, unreg);
		}
		else {
			char *name = malloc(tp->len + 1);
			if (!name) die("couldn't allocate memory");
			memcpy(name, tp->loc, tp->len);
			name[tp->len] = '\0';
			cb(name, isvar, unreg);
		}
		tp = tp->next;
	}
}

const char *cmeta_findfeatmacro(const struct cmeta *cm) {
	const Token *tp = (const Token *)cm;
	if (!tp || !tp->next) return 0; // FEATURE, (
	while (tp) {
		if (equal(tp, "FEATURE") && equal(tp->next, "(")) {
			if (equal(tp->next->next, ")")) return ""; // no arg = no desc
			if (!tp->next->next || tp->next->next->kind != TK_STR) {
				return 0; // it's invalid, whatever, just return...
			}
			return tp->next->next->str;
		}
		tp = tp->next;
	}
	return 0;
}

void cmeta_featinfomacros(const struct cmeta *cm, void (*cb)(
		enum cmeta_featmacro type, const char *param, void *ctxt), void *ctxt) {
	const Token *tp = (const Token *)cm;
	if (!tp || !tp->next) return;
	while (tp) {
		int type = -1;
		if (equal(tp, "PREINIT")) {
			type = CMETA_FEAT_PREINIT;
		}
		else if (equal(tp, "INIT")) {
			type = CMETA_FEAT_INIT;
		}
		else if (equal(tp, "END")) {
			type = CMETA_FEAT_END;
		}
		if (type != - 1) {
			if (equal(tp->next, "{")) {
				cb(type, 0, ctxt);
				tp = tp->next;
			}
			tp = tp->next;
			continue;
		}
		if (equal(tp, "REQUIRE")) {
			type = CMETA_FEAT_REQUIRE;
		}
		else if (equal(tp, "REQUIRE_GAMEDATA")) {
			type = CMETA_FEAT_REQUIREGD;
		}
		else if (equal(tp, "REQUIRE_GLOBAL")) {
			type = CMETA_FEAT_REQUIREGLOBAL;
		}
		else if (equal(tp, "REQUEST")) {
			type = CMETA_FEAT_REQUEST;
		}
		if (type != -1) {
			if (equal(tp->next, "(") && tp->next->next) {
				tp = tp->next->next;
				char *param = malloc(tp->len + 1);
				if (!param) die("couldn't allocate memory");
				memcpy(param, tp->loc, tp->len);
				param[tp->len] = '\0';
				cb(type, param, ctxt);
				tp = tp->next;
			}
		}
		tp = tp->next;
	}
}

struct vec_str VEC(const char *);

static void pushmacroarg(const Token *last, const char *start,
		struct vec_str *list) {
	int len = last->loc - start + last->len;
	char *dup = malloc(len + 1);
	if (!dup) die("couldn't allocate memory");
	memcpy(dup, start, len);
	dup[len] = '\0';
	if (!vec_push(list, dup)) die("couldn't append to array");
}

// XXX: maybe this should be used for the other functions too. it'd be less ugly
// and handle closing parentheses better, but alloc for tokens we don't care
// about. probably a worthy tradeoff?
static const Token *macroargs(const Token *t, struct vec_str *list) {
	int paren = 1;
	const Token *last; // avoids copying extra ws/comments in
	for (const char *start = t->loc; t; last = t, t = t->next) {
		if (equal(t, "(")) {
			++paren;
		}
		else if (equal(t, ")")) {
			if (!--paren) {
				pushmacroarg(last, start, list);
				return t->next;
			}
		}
		else if (paren == 1 && equal(t, ",")) {
			pushmacroarg(last, start, list);
			t = t->next;
			if (t) start = t->loc; // slightly annoying...
		}
	}
	// I guess we handle this here.
	fprintf(stderr, "cmeta: fatal: unexpected EOF in %s\n", t->filename);
	exit(2);
}

void cmeta_evdefmacros(const struct cmeta *cm, void (*cb)(const char *name,
		const char *const *params, int nparams, bool predicate)) {
	const Token *tp = (const Token *)cm;
	if (!tp || !tp->next || !tp->next->next) return; // DEF_EVENT, (, name
	while (tp) {
		bool predicate = true;
		if (equal(tp, "DEF_EVENT") && equal(tp->next, "(")) {
			predicate = false;
		}
		else if (!equal(tp, "DEF_PREDICATE") || !equal(tp->next, "(")) {
			tp = tp->next;
			continue;
		}
		tp = tp->next->next;
		struct vec_str args = {0};
		tp = macroargs(tp, &args);
		if (args.sz == 0) {
			fprintf(stderr, "cmeta: fatal: missing event parameters in %s\n",
					tp->filename);
			exit(2);
		}
		cb(args.data[0], args.data + 1, args.sz - 1, predicate);
	}
}

void cmeta_evhandlermacros(const struct cmeta *cm, const char *modname,
		void (*cb_handler)(const char *evname, const char *modname)) {
	const Token *tp = (const Token *)cm;
	while (tp) {
		if (equal(tp, "HANDLE_EVENT") && equal(tp->next, "(")) {
			tp = tp->next->next;
			char *name = malloc(tp->len + 1);
			if (!name) die("couldn't allocate memory");
			memcpy(name, tp->loc, tp->len);
			name[tp->len] = '\0';
			cb_handler(name, modname);
		}
		tp = tp->next;
	}
}

// vi: sw=4 ts=4 noet tw=80 cc=80 fdm=marker
