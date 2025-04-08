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

#ifdef _WIN32
#define fS "S"
#else
#define fS "s"
#endif

static cold noreturn die(int status, const char *s) {
	fprintf(stderr, "mkentprops: fatal: %s\n", s);
	exit(status);
}

// concatenated input file contents - string values are indices off this
static char *sbase = 0;

static const os_char *const *srcnames;
static cold noreturn dieparse(int file, int line, const char *s) {
	fprintf(stderr, "mkentprops: %" fS ":%d: %s\n", srcnames[file], line, s);
	exit(2);
}

#define MAXENTS 32768
static int tags[MAXENTS]; // varname/gametype
static int exprs[MAXENTS];
static uchar indents[MAXENTS]; // nesting level
static schar srcfiles[MAXENTS];
static int srclines[MAXENTS];
static int nents = 0;

static inline void handleentry(char *k, char *v, int indent,
		int file, int line) {
	int previndent = nents ? indents[nents - 1] : -1; // meh
	if_cold (indent > previndent + 1) {
		dieparse(file, line, "excessive indentation");
	}
	if_cold (indent == previndent && !exprs[nents - 1]) {
		dieparse(file, line - 1, "missing a value and/or conditional(s)");
	}
	if_cold (nents == MAXENTS) die(2, "out of array indices");
	tags[nents] = k - sbase;
	exprs[nents] = v - sbase; // will produce garbage for null v. this is fine!
	indents[nents] = indent;
	srcfiles[nents] = file;
	srclines[nents++] = line;
}

/*
 * -- Quick file format documentation! --
 *
 * We keep the gamedata format as simple as possible. Default values are
 * specified as direct key-value pairs:
 *
 *  <varname> <expr>
 *
 * Game- or engine-specific values are set using indented blocks:
 *
 *  <varname> <optional-default>
 *  	<gametype1> <expr>
 *  	<gametype2> <expr> # you can write EOL comments too!
 *			<some-other-nested-conditional-gametype> <expr>
 *
 * The most complicated it can get is if conditionals are nested, which
 * basically translates directly into nested ifs.
 *
 * Just be aware that whitespace is significant, and you have to use tabs.
 * Any and all future complaints about that decision SHOULD - and MUST - be
 * directed to the Python Software Foundation and the authors of the POSIX
 * Makefile specification. In that order.
 */

static void parse(int file, char *s, int len) {
	if_cold (s[len - 1] != '\n') {
		dieparse(file, 0, "invalid text file (missing EOL)");
	}
	enum { BOL = 0, KEY = 4, KWS = 8, VAL = 12, COM = 16, ERR = -1 };
	static const s8 statetrans[] = {
		// layout: any, space|tab, #, \n
		[BOL + 0] = KEY, [BOL + 1] = BOL, [BOL + 2] = COM, [BOL + 3] = BOL,
		[KEY + 0] = KEY, [KEY + 1] = KWS, [KEY + 2] = COM, [KEY + 3] = BOL,
		[KWS + 0] = VAL, [KWS + 1] = KWS, [KWS + 2] = COM, [KWS + 3] = BOL,
		[VAL + 0] = VAL, [VAL + 1] = VAL, [VAL + 2] = COM, [VAL + 3] = BOL,
		[COM + 0] = COM, [COM + 1] = COM, [COM + 2] = COM, [COM + 3] = BOL
	};
	char *key, *val = sbase; // 0 index by default (invalid value works as null)
	for (int state = BOL, i = 0, line = 1, indent = 0; i < len; ++i) {
		int transidx = state;
		char c = s[i];
		switch (c) {
			case '\0': dieparse(file, line, "unexpected null byte");
			case ' ':
				if_cold (state == BOL) {
					dieparse(file, line, "unexpected space at start of line");
				}
			case '\t':
				transidx += 1;
				break;
			case '#': transidx += 2; break;
			case '\n': transidx += 3;
		}
		int newstate = statetrans[transidx];
		switch_exhaust (newstate) {
			case KEY: if_cold (state != KEY) key = s + i; break;
			case KWS: if_cold (state != KWS) s[i] = '\0'; break;
			case VAL: if_cold (state == KWS) val = s + i; break;
			case BOL:
				indent += state == BOL;
				if_cold (indent > 255) { // this shouldn't happen if we're sober
					dieparse(file, line, "exceeded max nesting level (255)");
				}
			case COM:
				if_hot (state != BOL) {
					if (state != COM) { // blegh!
						int j = i;
						while (s[j - 1] == ' ' || s[j - 1] == '\t') --j;
						s[j] = '\0';
						handleentry(key, val, indent, file, line);
					}
					val = sbase; // reset this again
				}
		}
		if_cold (c == '\n') { // ugh, so much for state transitions.
			indent = 0;
			++line;
		}
		state = newstate;
	}
}

static cold noreturn diewrite() { die(100, "couldn't write to file"); }
#define _(x) if_cold (fprintf(out, "%s\n", x) < 0) diewrite();
#define _doindent() \
	for (int i = 0; i < indent; ++i) \
		if_cold (fputc('\t', out) == EOF) diewrite();
#define _i(x) _doindent(); _(x)
#define F(f, ...) if_cold (fprintf(out, f "\n", __VA_ARGS__) < 0) diewrite();
#define Fi(...) _doindent(); F(__VA_ARGS__)
#define H() \
_( "/* This file is autogenerated by src/build/mkgamedata.c. DO NOT EDIT! */") \
_( "")

static inline void knowngames(FILE *out) {
	// kind of tricky optimisation: if a gamedata entry has no default but
	// does have game-specific values which match a feature's GAMESPECIFIC()
	// macro, load-time conditional checks resulting from REQUIRE_GAMEDATA() can
	// be elided at compile-time.
	for (int i = 0, j; i < nents - 1; i = j) {
		while (exprs[i]) { // if there's a default value, we don't need this
			// skip to next unindented thing, return if there isn't one with at
			// least one indented thing under it.
			for (++i; indents[i] != 0; ++i) if (i == nents - 1) return;
		}
F( "#line %d \"%" fS "\"", srclines[i], srcnames[srcfiles[i]])
		if_cold (fprintf(out, "#define _GAMES_WITH_%s (", sbase + tags[i]) < 0) {
			diewrite();
		}
		const char *pipe = "";
		for (j = i + 1; j < nents && indents[j] != 0; ++j) {
			// don't attempt to optimise for nested conditionals because that's
			// way more complicated and also basically defeats the purpose.
			if (indents[j] != 1) continue;
			if_cold (fprintf(out, "%s \\\n\t _gametype_tag_%s", pipe,
					sbase + tags[j]) < 0) {
				diewrite();
			}
			pipe = " |";
		}
		fputs(" \\\n)\n", out);
	}
}

static inline void decls(FILE *out) {
	for (int i = 0; i < nents; ++i) {
		if (indents[i] != 0) continue;
F( "#line %d \"%" fS "\"", srclines[i], srcnames[srcfiles[i]])
		if (exprs[i]) { // default value is specified - entry always exists
			// *technically* this case is redundant - the other has_ macro would
			// still work. however, having a distinct case makes the generated
			// header a little easier to read at a glance.
F( "#define has_%s 1", sbase + tags[i])
		}
		else { // entry is missing unless a tag is matched
			// implementation detail: INT_MIN is reserved for missing gamedata!
			// XXX: for max robustness, should probably check for this in input?
F( "#define has_%s (%s != -2147483648)", sbase + tags[i], sbase + tags[i])
		}
F( "#line %d \"%" fS "\"", srclines[i], srcnames[srcfiles[i]])
		if_cold (i == nents - 1 || !indents[i + 1]) { // no tags - it's constant
F( "enum { %s = (%s) };", sbase + tags[i], sbase + exprs[i])
		}
		else { // global variable intialised by initgamedata() call
F( "extern int %s;", sbase + tags[i]);
		}
	}
}

static inline void defs(FILE *out) {
	for (int i = 0; i < nents; ++i) {
		if (indents[i] != 0) continue;
		if_hot (i < nents - 1 && indents[i + 1]) {
F( "#line %d \"%" fS "\"", srclines[i], srcnames[srcfiles[i]])
			if (exprs[i]) {
F( "int %s = (%s);", sbase + tags[i], sbase + exprs[i])
			}
			else {
F( "int %s = -2147483648;", sbase + tags[i])
			}
		}
	}
}

static inline void init(FILE *out) {
_( "static void initgamedata() {")
	int varidx;
	int indent = 0;
	for (int i = 0; i < nents; ++i) {
		if (indents[i] < indents[i - 1]) {
			for (; indent != indents[i]; --indent) {
_i("}")
			}
		}
		if (indents[i] == 0) {
			varidx = i;
			continue;
		}
F( "#line %d \"%" fS "\"", srclines[i], srcnames[srcfiles[i]])
		if (indents[i] > indents[i - 1]) {
Fi("	if (GAMETYPE_MATCHES(%s)) {", sbase + tags[i])
			++indent;
		}
		else {
_i("}")
F( "#line %d \"%" fS "\"", srclines[i], srcnames[srcfiles[i]])
Fi("else if (GAMETYPE_MATCHES(%s)) {", sbase + tags[i])
		}
		if (exprs[i]) {
F( "#line %d \"%" fS "\"", srclines[i], srcnames[srcfiles[i]])
Fi("	%s = (%s);", sbase + tags[varidx], sbase + exprs[i])
		}
	}
	for (; indent != 0; --indent) {
_i("}")
	}
_( "}")
}

int OS_MAIN(int argc, os_char *argv[]) {
	srcnames = (const os_char *const *)argv;
	int sbase_len = 0, sbase_max = 65536;
	sbase = malloc(sbase_max);
	if_cold (!sbase) die(100, "couldn't allocate memory");
	int n = 1;
	for (++argv; *argv; ++argv, ++n) {
		int f = os_open_read(*argv);
		if_cold (f == -1) die(100, "couldn't open file");
		vlong len = os_fsize(f);
		if_cold (sbase_len + len > 1u << 29) {
			die(2, "combined input files are far too large");
		}
		if_cold (sbase_len + len > sbase_max) {
			fprintf(stderr, "mkgamedata: warning: need to resize string. "
					"increase sbase_max to avoid this extra work!\n");
			sbase_max *= 4;
			sbase = realloc(sbase, sbase_max);
			if_cold (!sbase) die(100, "couldn't grow memory allocation");
		}
		char *s = sbase + sbase_len;
		if_cold (os_read(f, s, len) != len) die(100, "couldn't read file");
		os_close(f);
		parse(n, s, len);
		sbase_len += len;
	}

	FILE *out = fopen(".build/include/gamedata.gen.h", "wb");
	if_cold (!out) die(100, "couldn't open gamedata.gen.h");
	H();
	knowngames(out);
	decls(out);

	out = fopen(".build/include/gamedatainit.gen.h", "wb");
	if_cold (!out) die(100, "couldn't open gamedatainit.gen.h");
	H();
	defs(out);
	_("")
	init(out);
	return 0;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
