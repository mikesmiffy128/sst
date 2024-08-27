/*
 * Copyright © 2024 Michael Smith <mikesmiffy128@gmail.com>
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
#include <string.h>
#include <sys/stat.h>

#include "../intdefs.h"
#include "../langext.h"
#include "../os.h"

#ifdef _WIN32
#define fS "S"
#else
#define fS "s"
#endif

static noreturn die(int status, const char *s) {
	fprintf(stderr, "mkentprops: %s\n", s);
	exit(status);
}
static noreturn dieparse(const os_char *file, int line, const char *s) {
	fprintf(stderr, "mkentprops: %" fS ":%d: %s\n", file, line, s);
	exit(2);
}

static char *sbase; // input file contents - string values are indices off this

// custom data structure of the day: zero-alloc half-SoA adaptive radix trees(!)

#define ART_MAXNODES 16384
static uchar art_firstbytes[ART_MAXNODES];
static struct art_core {
	int soff; // string offset to entire key chunk (including firstbyte)
	u16 slen; // number of bytes in key chunk (including firstbyte, >=1)
	u16 next; // sibling node (same prefix, different firstbyte)
} art_cores[ART_MAXNODES];
// if node doesn't end in \0: child node (i.e. key appends more bytes)
// if node DOES end in \0: offset into art_leaves (below)
static u16 art_children[ART_MAXNODES];
static int art_nnodes = 0;

#define ART_NULL ((u16)-1)

static inline int art_newnode(void) {
	if (art_nnodes == ART_MAXNODES) die(2, "out of tree nodes");
	return art_nnodes++;
}

#define ART_MAXLEAVES 8192
static struct art_leaf {
	int varstr; // offset of string (generated variable), if any, or -1 if none
	u16 subtree; // art index of subtree (nested SendTable), or -1 if none
	u16 nsubs; // number of subtrees (used to short-circuit the runtime search)
} art_leaves[ART_MAXLEAVES];
static int art_nleaves = 0;
static u16 art_root = ART_NULL; // ServerClasses (which point at SendTables)
static u16 nclasses = 0; // similar short circuit for ServerClasses

#define VAR_NONE -1

// for quick iteration over var names to generate decls header without ART faff
#define MAXDECLS 4096
static int decls[MAXDECLS];
static int ndecls = 0;

static inline int art_newleaf(void) {
	if (art_nleaves == ART_MAXLEAVES) die(2, "out of leaf nodes");
	return art_nleaves++;
}

static struct art_lookup_ret {
	bool isnew; // true if node created, false if found
	u16 leafidx; // index of value (leaf) node
} art_lookup(u16 *art, int soff, int len) {
	assume(len > 0); // everything must be null-terminated!
	for (;;) {
		const uchar *p = (const uchar *)sbase + soff;
		u16 cur = *art;
		if (cur == ART_NULL) { // append
			int new = art_newnode();
			*art = new;
			art_firstbytes[new] = *p;
			art_cores[new].soff = soff;
			art_cores[new].slen = len;
			art_cores[new].next = ART_NULL;
			int leaf = art_newleaf(); // N.B. firstbyte is already 0 here
			art_children[new] = leaf;
			return (struct art_lookup_ret){true, leaf};
		}
		while (art_firstbytes[cur] == *p) {
			int nodelen = art_cores[cur].slen;
			int matchlen = 1;
			const uchar *q = (const uchar *)sbase + art_cores[cur].soff;
			for (; matchlen < nodelen; ++matchlen) {
				if (p[matchlen] != q[matchlen]) {
					// node and key diverge: split into child nodes
					// (left is new part of key string, right is existing tail)
					art_cores[cur].slen = matchlen;
					int l = art_newnode(), r = art_newnode();
					art_firstbytes[l] = p[matchlen];
					art_cores[l].soff = soff + matchlen;
					art_cores[l].slen = len - matchlen;
					art_cores[l].next = r;
					art_firstbytes[r] = q[matchlen];
					art_cores[r].soff = art_cores[cur].soff + matchlen;
					art_cores[r].slen = nodelen - matchlen;
					art_cores[r].next = ART_NULL;
					art_children[r] = art_children[cur];
					art_children[cur] = l;
					int leaf = art_newleaf();
					art_children[l] = leaf;
					return (struct art_lookup_ret){true, leaf};
				}
			}
			if (matchlen == len) {
				// node matches entire key: we have matched an existing entry
				return (struct art_lookup_ret){false, art_children[cur]};
			}
			// node is substring of key: descend into child nodes
			soff += matchlen;
			len -= matchlen;
			cur = art_children[cur]; // note: this can't be ART_NULL (thus loop)
			p = (const uchar *)sbase + soff; // XXX: kinda silly dupe
		}
		// if we didn't match this node, try the next sibling node.
		// if sibling is null, we'll hit the append case above.
		art = &art_cores[cur].next;
	}
}

static struct art_leaf *helpgetleaf(u16 *art, const char *s, int len,
		const os_char *parsefile, int parseline, u16 *countvar) {
	struct art_lookup_ret leaf = art_lookup(art, s - sbase, len);
	if (leaf.isnew) {
		art_leaves[leaf.leafidx].varstr = VAR_NONE;
		art_leaves[leaf.leafidx].subtree = ART_NULL;
		++*countvar;
	}
	// if parsefile is null then we don't care about dupes (looking at subtable)
	else if (parsefile && art_leaves[leaf.leafidx].varstr != VAR_NONE) {
		dieparse(parsefile, parseline, "duplicate property name");
	}
	return art_leaves + leaf.leafidx;
}

static inline void handleentry(char *k, char *v, int vlen,
		const os_char *file, int line) {
	if (ndecls == MAXDECLS) die(2, "out of declaration entries");
	decls[ndecls++] = k - sbase;
	char *propname = memchr(v, '/', vlen);
	if (!propname) {
		dieparse(file, line, "network name not in class/property format");
	}
	*propname++ = '\0';
	int sublen = propname - v;
	if (sublen > 65535) {
		dieparse(file, line, "network class name is far too long");
	}
	vlen -= sublen;
	struct art_leaf *leaf = helpgetleaf(&art_root, v, sublen, 0, 0, &nclasses);
	u16 *subtree = &leaf->subtree;
	for (;;) {
		if (vlen > 65535) {
			dieparse(file, line, "property (SendTable) name is far too long");
		}
		char *nextpart = memchr(propname, '/', vlen);
		if (!nextpart) {
			leaf = helpgetleaf(subtree, propname, vlen, file, line,
					&leaf->nsubs);
			leaf->varstr = k - sbase;
			break;
		}
		*nextpart++ = '\0';
		sublen = nextpart - propname;
		leaf = helpgetleaf(subtree, propname, sublen, 0, 0, &leaf->nsubs);
		subtree = &leaf->subtree;
		vlen -= sublen;
		propname = nextpart;
	}
}

static void parse(const os_char *file, int len) {
	char *s = sbase; // for convenience
	if (s[len - 1] != '\n') dieparse(file, 0, "invalid text file (missing EOL)");
	enum { BOL = 0, KEY = 4, KWS = 8, VAL = 12, COM = 16, ERR = -1 };
	static const s8 statetrans[] = {
		// layout: any, space|tab, #, \n
		[BOL + 0] = KEY, [BOL + 1] = ERR, [BOL + 2] = COM, [BOL + 3] = BOL,
		[KEY + 0] = KEY, [KEY + 1] = KWS, [KEY + 2] = ERR, [KEY + 3] = ERR,
		[KWS + 0] = VAL, [KWS + 1] = KWS, [KWS + 2] = ERR, [KWS + 3] = ERR,
		[VAL + 0] = VAL, [VAL + 1] = VAL, [VAL + 2] = COM, [VAL + 3] = BOL,
		[COM + 0] = COM, [COM + 1] = COM, [COM + 2] = COM, [COM + 3] = BOL
	};
	char *key, *val;
	for (int state = BOL, i = 0, line = 1; i < len; ++i) {
		int transidx = state;
		switch (s[i]) {
			case '\0': dieparse(file, line, "unexpected null byte");
			case ' ': case '\t': transidx += 1; break;
			case '#': transidx += 2; break;
			case '\n': transidx += 3;
		}
		int newstate = statetrans[transidx];
		if_cold (newstate == ERR) {
			if (state == BOL) dieparse(file, line, "unexpected indentation");
			if (s[i] == '\n') dieparse(file, line, "unexpected end of line");
			dieparse(file, line, "unexpected comment");
		}
		switch_exhaust (newstate) {
			case KEY: if_cold (state != KEY) key = s + i; break;
			case KWS: if_cold (state != KWS) s[i] = '\0'; break;
			case VAL: if_cold (state == KWS) val = s + i; break;
			case COM: case BOL:
				if (state == VAL) {
					int j = i;
					while (s[j - 1] == ' ' || s[j - 1] == '\t') --j;
					s[j] = '\0';
					int vallen = j - (val - s) + 1;
					handleentry(key, val, vallen, file, line);
				}
		}
		line += state == BOL;
		state = newstate;
	}
}

static inline noreturn diewrite(void) { die(100, "couldn't write to file"); }
#define _(x) if (fprintf(out, "%s\n", x) < 0) diewrite();
#define _i(x) for (int i = 0; i < indent; ++i) fputc('\t', out); _(x)
#define F(f, ...) if (fprintf(out, f "\n", __VA_ARGS__) < 0) diewrite();
#define Fi(...) for (int i = 0; i < indent; ++i) fputc('\t', out); F(__VA_ARGS__)
#define H() \
_( "/* This file is autogenerated by src/build/mkentprops.c. DO NOT EDIT! */") \
_( "")

static void dosendtables(FILE *out, u16 art, int indent) {
_i("switch (*p) {")
	while (art != ART_NULL) {
		// stupid hack: figure out char literal in case of null byte
		char charlit[3] = {art_firstbytes[art], '0'};
		if_hot (charlit[0]) charlit[1] = '\0'; else charlit[0] = '\\';
		if (art_cores[art].slen != 1) {
			const char *tail = sbase + art_cores[art].soff + 1;
			int len = art_cores[art].slen - 1;
Fi("	case '%s': if (!strncmp(p + 1, \"%.*s\", %d)) {",
charlit, len, tail, len)
		}
		else {
Fi("	case '%s': {", charlit)
		}
		int idx = art_children[art];
		// XXX: kind of a dumb and bad way to distinguish these. okay for now...
		if (sbase[art_cores[art].soff + art_cores[art].slen - 1] != '\0') {
Fi("		p += %d;", art_cores[art].slen)
			dosendtables(out, idx, indent + 2);
		}
		else {
			// XXX: do we actually want to prefetch this before the for loop?
_i("		int off = baseoff + mem_loads32(mem_offset(sp, off_SP_offset));")
			if (art_leaves[idx].varstr != VAR_NONE) {
Fi("		%s = off;", sbase + art_leaves[idx].varstr);
			}
			if (art_leaves[idx].subtree != ART_NULL) {
_i("		if (mem_loads32(mem_offset(sp, off_SP_type)) == DPT_DataTable) {")
_i("			int baseoff = off;")
_i("			const struct SendTable *st = mem_loadptr(mem_offset(sp, off_SP_subtable));")
_i("			// BEGIN SUBTABLE")
Fi("			for (int i = 0, need = %d; i < st->nprops && need; ++i) {",
art_leaves[idx].nsubs + (art_leaves[idx].varstr != -1))
_i("				const struct SendProp *sp = mem_offset(st->props, sz_SendProp * i);")
_i("				const char *p = mem_loadptr(mem_offset(sp, off_SP_varname));")
				dosendtables(out, art_leaves[idx].subtree, indent + 4);
_i("			}")
_i("			// END SUBTABLE")
_i("		}")
			}
Fi("		--need;")
		}
_i("	} break;")
		art = art_cores[art].next;
	}
_i("}")
}

static void doclasses(FILE *out, u16 art, int indent) {
_i("switch (*p) {")
	for (; art != ART_NULL; art = art_cores[art].next) {
		// stupid hack 2: exact dupe boogaloo
		char charlit[3] = {art_firstbytes[art], '0'};
		if_hot (charlit[0]) charlit[1] = '\0'; else charlit[0] = '\\';
		if (art_cores[art].slen != 1) {
			const char *tail = sbase + art_cores[art].soff + 1;
			int len = art_cores[art].slen - 1;
Fi("	case '%s': if (!strncmp(p + 1, \"%.*s\", %d)) {",
charlit, len, tail, len)
		}
		else {
Fi("	case '%s': {", charlit)
		}
		int idx = art_children[art];
		// XXX: same dumb-and-bad-ness as above. there must be a better way!
		if (sbase[art_cores[art].soff + art_cores[art].slen - 1] != '\0') {
Fi("		p += %d;", art_cores[art].slen)
			doclasses(out, art_children[art], indent + 2);
		}
		else {
			assume(art_leaves[idx].varstr == VAR_NONE);
			assume(art_leaves[idx].subtree != ART_NULL);
_i("		const struct SendTable *st = class->table;")
Fi("		for (int i = 0, need = %d; i < st->nprops && need; ++i) {",
art_leaves[idx].nsubs + (art_leaves[idx].varstr != -1))
				// note: annoyingly long line here, but the generated code gets
				// super nested anyway, so there's no point in caring really
				// XXX: basically a dupe of dosendtables() - fold into above?
_i("			const struct SendProp *sp = mem_offset(st->props, sz_SendProp * i);")
_i("			const char *p = mem_loadptr(mem_offset(sp, off_SP_varname));")
			dosendtables(out, art_leaves[idx].subtree, indent + 3);
_i("		}")
Fi("		--need;")
		}
_i("	} break;")
	}
_i("}")
}

static void dodecls(FILE *out) {
	for (int i = 0; i < ndecls; ++i) {
		const char *s = sbase + decls[i];
F( "extern int %s;", s);
F( "#define has_%s (!!%s)", s, s); // offsets will NEVER be 0, due to vtable!
	}
}

static void doinit(FILE *out) {
	for (int i = 0; i < ndecls; ++i) {
		const char *s = sbase + decls[i];
F( "int %s = 0;", s);
	}
_( "")
_( "static inline void initentprops(const struct ServerClass *class) {")
_( "	enum { baseoff = 0 };") // can be shadowed for subtables.
F( "	for (int need = %d; need && class; class = class->next) {", nclasses)
_( "		const char *p = class->name;")
	doclasses(out, art_root, 2);
_( "	}")
_( "}")
}

int OS_MAIN(int argc, os_char *argv[]) {
	if (argc != 2) die(1, "wrong number of arguments");
	int f = os_open_read(argv[1]);
	if (f == -1) die(100, "couldn't open file");
	vlong len = os_fsize(f);
	if (len > 1u << 30 - 1) die(2, "input file is far too large");
	sbase = malloc(len);
	if (!sbase) die(100, "couldn't allocate memory");
	if (os_read(f, sbase, len) != len) die(100, "couldn't read file");
	os_close(f);
	parse(argv[1], len);

	FILE *out = fopen(".build/include/entprops.gen.h", "wb");
	if (!out) die(100, "couldn't open entprops.gen.h");
	H();
	dodecls(out);

	out = fopen(".build/include/entpropsinit.gen.h", "wb");
	if (!out) die(100, "couldn't open entpropsinit.gen.h");
	H();
	doinit(out);
	return 0;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
