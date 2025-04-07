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
#include <string.h>

#include "../intdefs.h"
#include "../langext.h"
#include "../os.h"
#include "cmeta.h"

#ifdef _WIN32
#define fS "S"
#else
#define fS "s"
#endif

static inline noreturn die(int status, const char *s) {
	fprintf(stderr, "gluegen: fatal: %s\n", s);
	exit(status);
}

static inline noreturn diefile(int status, const os_char *f, int line,
		const char *s) {
	if (line) {
		fprintf(stderr, "gluegen: fatal: %" fS ":%d: %s\n", f, line, s);
	}
	else {
		fprintf(stderr, "gluegen: fatal: %" fS ": %s\n", f, s);
	}
	exit(status);
}

#define ARENASZ (1024 * 1024)
static _Alignas(64) char _arena[ARENASZ] = {0}, *const arena = _arena - 64;
static int arena_last = 0;
static int arena_used = 64; // using 0 indices as null; reserve and stay aligned

static inline void _arena_align() {
	enum { ALIGN = ssizeof(void *) };
	if (arena_used & ALIGN - 1) arena_used = (arena_used + ALIGN) & ~(ALIGN - 1);
}

static inline int arena_bump(int amt) {
	if_cold (arena_used + amt >= ARENASZ) die(2, "out of arena memory");
	int ret = arena_used;
	arena_used += amt;
	return ret;
}

static int arena_new(int sz) {
	_arena_align();
	arena_last = arena_used;
	int off = arena_bump(sz);
	return off;
}

#define LIST_MINALLOC 64
#define LIST_MINSPACE (LIST_MINALLOC - ssizeof(struct list_chunkhdr))

struct list_chunkhdr { int next, sz; };
struct list_chunk { struct list_chunkhdr hdr; char space[LIST_MINSPACE]; };

static struct list_grow_ret {
	struct list_chunkhdr *tail;
	void *data;
} list_grow(struct list_chunkhdr *tail, int amt) {
	if_hot ((char *)tail == arena + arena_last) {
		arena_bump(amt);
	}
	else if (tail->sz + amt > LIST_MINSPACE) {
		int allocsz = ssizeof(struct list_chunkhdr) + amt;
		int new = arena_new(allocsz > LIST_MINALLOC ? allocsz : LIST_MINALLOC);
		struct list_chunkhdr *newptr = (struct list_chunkhdr *)(arena + new);
		newptr->next = 0; newptr->sz = 0;
		tail->next = new;
		return (struct list_grow_ret){newptr, (char *)(newptr + 1)};
	}
	struct list_grow_ret r = {tail, (char *)(tail + 1) + tail->sz};
	tail->sz += amt;
	return r;
}

static inline void *list_grow_p(struct list_chunkhdr **tailp, int amt) {
	struct list_grow_ret r = list_grow(*tailp, amt);
	*tailp = r.tail;
	return r.data;
}

#define list_append_p(T, tailp) ((T *)list_grow_p((tailp), ssizeof(T)))
#define list_append(tailp, x) ((void)(*list_append_p(typeof(x), (tailp)) = (x)))

#define _list_foreach(T, varname, firstchunkp, extra) \
	for (struct list_chunkhdr *_h = &(firstchunkp)->hdr; _h; \
			_h = _h->next ? (struct list_chunkhdr *)(arena + _h->next) : 0) \
		for (typeof(T) *varname = (typeof(T) *)(_h + 1); \
				(char *)varname < (char *)(_h + 1) + _h->sz && (extra); \
				++varname) \
			/* ... */

#define list_foreach_p(T, varname, firstchunkp) \
	_list_foreach(T, varname, firstchunkp, 1)
		/* {...} */

#define list_foreach(T, varname, firstchunkp) \
	/* N.B. this crazy looking construct avoids unused warnings on varname
	   in case we just want to count up the entries or something */ \
	switch (0) for (typeof(T) varname; 0;) if ((void)varname, 0) case 0: \
		_list_foreach(T, _foreachp, firstchunkp, (varname = *_foreachp, 1)) \
			/* ... */

#define DEF_NEW(type, func, nvar, maxvar, desc) \
	static inline type func() { \
		if_cold (nvar == maxvar) { \
			die(2, "out of " desc " - increase " #maxvar " in gluegen.c!"); \
		} \
		return nvar++; \
	}

// trickery to enable 1-based indexing (and thus 0-as-null) for various arrays
#define SHUNT(T, x) typeof(T) _array_##x[], *const x = _array_##x - 1, _array_##x

#define MAX_MODULES 512
// note that by 1-indexing these, we enable the radix stuff above to use the
// default initialisation as null and we *also* allow argc to be used as a
// direct index into each array when looping through files passed to main()!
// XXX: padded 16-byte structs. could eventually switch to u32 indices. for now,
// locality is favoured over packing. no clue whether that's the right call!
static SHUNT(struct cmeta_slice, mod_names)[MAX_MODULES] = {0};
static SHUNT(struct cmeta_slice, mod_featdescs)[MAX_MODULES] = {0};
static SHUNT(struct cmeta_slice, mod_gamespecific)[MAX_MODULES] = {0};
enum {
	HAS_INIT = 1, // really, this means this *is a feature*!
	HAS_PREINIT = 2,
	HAS_END = 4,
	HAS_EVENTS = 8,
	HAS_OPTDEPS = 16, // something else depends on *us* with REQUEST()
	DFS_SEEING = 64, // for REQUIRE() cycle detection
	DFS_SEEN = 128
};
static u8 mod_flags[MAX_MODULES] = {0};
static SHUNT(struct list_chunk, mod_needs)[MAX_MODULES] = {0};
static SHUNT(struct list_chunk, mod_wants)[MAX_MODULES] = {0};
static SHUNT(struct list_chunk, mod_gamedata)[MAX_MODULES] = {0};
static SHUNT(struct list_chunk, mod_globals)[MAX_MODULES] = {0};
static int nmods = 1;

static s16 feat_initorder[MAX_MODULES];
static int nfeatures = 0;

#define MAX_CVARS 8192
#define MAX_CCMDS MAX_CVARS
static SHUNT(struct cmeta_slice, cvar_names)[MAX_CVARS];
static SHUNT(struct cmeta_slice, ccmd_names)[MAX_CCMDS];
static SHUNT(s16, cvar_feats)[MAX_CVARS];
static SHUNT(s16, ccmd_feats)[MAX_CVARS];
static SHUNT(u8, cvar_flags)[MAX_CVARS];
static SHUNT(u8, ccmd_flags)[MAX_CVARS];
static int ncvars = 1, nccmds = 1;
DEF_NEW(s16, cvar_new, ncvars, MAX_CVARS, "cvar entries")
DEF_NEW(s16, ccmd_new, nccmds, MAX_CCMDS, "ccmd entries")

#define MAX_EVENTS 512
static SHUNT(struct cmeta_slice, event_names)[MAX_EVENTS];
static SHUNT(s16, event_owners)[MAX_EVENTS];
static SHUNT(struct list_chunk, event_handlers)[MAX_EVENTS] = {0};
static SHUNT(struct list_chunkhdr *, event_handlers_tails)[MAX_EVENTS] = {0};
static SHUNT(struct list_chunk, event_params)[MAX_EVENTS] = {0};
// XXX: would simply things a little if we could segregate the regular event and
// predicate arrays, but because of how the event.h API is currently set up,
// HANDLE_EVENT doesn't give context for what kind of event something is, so we
// can't do that in a single pass (right now we create placeholder event entries
// and check at the end to make sure we didn't miss one). fixing this is tricky!
static SHUNT(bool, event_predicateflags)[MAX_EVENTS] = {0};
static int nevents = 1;
DEF_NEW(s16, event_new, nevents, MAX_EVENTS, "event entries")

// a "crit-nybble tree" (see also: djb crit-bit trees)
struct radix { s16 children[16], critpos; };
static SHUNT(struct radix, radices)[MAX_MODULES * 2 + MAX_EVENTS];
static int nradices = 1; // also reserve a null value

// NOTE: this will never fail, as node count is bounded by modules * 2 + events
static inline s16 radix_new() { return nradices++; }

static int matchlen(const char *s1, const char *s2, int len, bool ignorecase) {
	uchar c1, c2;
	for (int i = 0; i < len; ++i) {
		c1 = s1[i]; c2 = s2[i];
		if (ignorecase) {
			c1 |= (c1 >= 'A' && c1 <= 'Z') << 5; // if A-Z, |= 32 -> a-z
			c2 |= (c2 >= 'A' && c2 <= 'Z') << 5; // "
		}
		int diff = c1 ^ c2;
		if (diff) return (i << 1) | (diff < 16);
	}
	return (len << 1);
}
static inline int radixbranch(const char *s, int i, bool ignorecase) {
	uchar c = s[i >> 1];
	if (ignorecase) c |= (c >= 'A' && c <= 'Z') << 5;
	return c >> ((~i & 1) << 2) & 0xF;
}

/*
 * Tries to insert a string index into a radix/crit-nybble/whatever trie.
 * Does not actually put the index in; that way an item can be allocated only
 * after determining that it doesn't already exist. Callers have to check
 * ret.isnew and act accordingly.
 */
static struct radix_insert_ret {
	bool isnew;
	union {
		s16 *strp; /* If isnew, caller must do `*ret.strp = -stridx;`. */
		s16 stridx; /* If !isnew, `stridx` points at the existing string. */
	};
} radix_insert(s16 *nodep, const struct cmeta_slice *strlist,
		const char *s, int len, bool ignorecase) {
	// special case: an empty tree is just null; replace it with a leaf node
	if (!*nodep) return (struct radix_insert_ret){true, nodep};
	s16 *insp = nodep;
	for (s16 cur;;) {
		cur = *nodep;
		assume(cur);
		if (cur < 0) {
			// once we find an existing leaf node, we have to compare our string
			// against it to find the critical position (i.e. the point where
			// the strings differ) and then insert a new node at that point.
			const struct cmeta_slice existing = strlist[-cur];
			int ml;
			if (existing.len == len) {
				ml = matchlen(existing.s, s, len, ignorecase);
				if (ml == len << 1) {
					return (struct radix_insert_ret){false, .stridx = -cur};
				}
			}
			else {
				// ugh this is a little inelegant, can we think of a better way?
				ml = matchlen(existing.s, s,
						len < existing.len ? len : existing.len, ignorecase);
			}
			int oldbranch = radixbranch(existing.s, ml, ignorecase);
			int newbranch = radixbranch(s, ml, ignorecase);
			assume(oldbranch != newbranch);
			for (;;) {
				// splice in front of an existing string *or* an empty slot.
				if (*insp <= 0) break;
				struct radix *r = radices + *insp;
				if (r->critpos > ml) break;
				insp = r->children + radixbranch(s, r->critpos, ignorecase);
			}
			s16 new = radix_new();
			radices[new].critpos = ml;
			radices[new].children[oldbranch] = *insp;
			s16 *strp = radices[new].children + newbranch;
			*insp = new;
			return (struct radix_insert_ret){true, strp};
		}
		// try to take the exact path to match common prefixes, but otherwise
		// just pick any path that lets us find a string to compare with.
		// we always have to compare against an existing string to determine the
		// exact correct point at which to insert a new node.
		int branch = 0;
		if_hot (radices[cur].critpos <= len << 1) {
			branch = radixbranch(s, radices[cur].critpos, ignorecase);
		}
		while (!radices[cur].children[branch]) branch = (branch + 1) & 15;
		nodep = radices[cur].children + branch;
	}
}

/*
 * Inserts an entry that's already in a string list. Can be used for
 * dupe-checking after appending a new entry. Returns false on duplicate
 * entries.
 */
static bool radix_insertidx(s16 *nodep, const struct cmeta_slice *strlist,
		s16 stridx, bool ignorecase) {
	const char *s = strlist[stridx].s; int len = strlist[stridx].len;
	struct radix_insert_ret r = radix_insert(nodep, strlist, s, len, ignorecase);
	if (r.isnew) *r.strp = -stridx;
	return r.isnew;
}

/*
 * Returns the string index of an existing entry matching `s` and `len`.
 * Returns 0 (the reserved null index) if an entry does not exist.
 */
static s16 radix_lookup(s16 node, const struct cmeta_slice *strlist,
		const char *s, int len, bool ignorecase) {
	while (node) {
		if (node < 0) {
			const struct cmeta_slice matched = strlist[-node];
			if (matched.len != len) return 0;
			if (matchlen(matched.s, s, len, ignorecase) == len << 1) {
				return -node;
			}
			return 0;
		}
		if (radices[node].critpos >= len << 1) return 0;
		int branch = radixbranch(s, radices[node].critpos, ignorecase);
		node = radices[node].children[branch];
	}
	return 0;
}

static inline void handle(s16 mod, s16 mods, s16 *featdescs, s16 *events,
		const os_char *file, const struct cmeta *cm) {
	bool isfeat = false;
	const char *needfeat = 0;
	bool canpreinit = true, haspreinit = false, hasinit = false;
	struct list_chunkhdr *tail_needs = &mod_needs[mod].hdr;
	struct list_chunkhdr *tail_wants = &mod_wants[mod].hdr;
	struct list_chunkhdr *tail_gamedata = &mod_gamedata[mod].hdr;
	struct list_chunkhdr *tail_globals = &mod_globals[mod].hdr;
	for (u32 i = 0; i != cm->nitems; ++i) {
		switch_exhaust_enum(cmeta_item, cm->itemtypes[i]) {
			case CMETA_ITEM_DEF_CVAR:
				if (!cmeta_nparams(cm, i)) {
					diefile(2, file, cmeta_line(cm, i),
							"cvar macro missing required parameter");
				}
				int flags = cmeta_flags_cvar(cm, i);
				s16 idx = cvar_new();
				cvar_flags[idx] = flags;
				// NOTE: always hooking cvar/ccmd up to a feat entry for
				// GAMESPECIFIC checks, even if it's not a DEF_FEAT_*.
				// If a module doesn't declare FEATURE, its cvars/ccmds will
				// end up getting initialised/registered unconditionally.
				cvar_feats[idx] = mod;
				if (flags & CMETA_CVAR_FEAT) needfeat = "feature cvar defined";
				cmeta_param_foreach (name, cm, i) {
					cvar_names[idx] = name;
					break; // ignore subsequent args
				}
				break;
			case CMETA_ITEM_DEF_CCMD:
				if (!cmeta_nparams(cm, i)) {
					diefile(2, file, cmeta_line(cm, i),
							"ccmd macro missing required parameter");
				}
				flags = cmeta_flags_ccmd(cm, i);
				if (flags & CMETA_CCMD_FEAT) {
					needfeat = "feature ccmd defined";
				}
				if (flags & CMETA_CCMD_PLUSMINUS) {
					// split PLUSMINUS entries in two; makes stuff easier later.
					flags &= ~CMETA_CCMD_PLUSMINUS;
					idx = ccmd_new();
					ccmd_flags[idx] = flags;
					ccmd_feats[idx] = mod;
					cmeta_param_foreach (name, cm, i) {
						char *s = arena + arena_new(5 + name.len);
						memcpy(s, "PLUS_", 5);
						memcpy(s + 5, name.s, name.len);
						ccmd_names[idx] = (struct cmeta_slice){s, 5 + name.len};
						break;
					}
					idx = ccmd_new();
					ccmd_flags[idx] = flags;
					ccmd_feats[idx] = mod;
					cmeta_param_foreach (name, cm, i) {
						char *s = arena + arena_new(6 + name.len);
						memcpy(s, "MINUS_", 6);
						memcpy(s + 6, name.s, name.len);
						ccmd_names[idx] = (struct cmeta_slice){s, 6 + name.len};
						break;
					}
				}
				else {
					idx = ccmd_new();
					ccmd_flags[idx] = flags;
					ccmd_feats[idx] = mod;
					cmeta_param_foreach (name, cm, i) {
						ccmd_names[idx] = name;
						break;
					}
				}
				break;
			case CMETA_ITEM_DEF_EVENT:
				if (!cmeta_nparams(cm, i)) {
					diefile(2, file, cmeta_line(cm, i),
							"event macro missing required parameter");
				}
				flags = cmeta_flags_event(cm, i);
				struct cmeta_param_iter it = cmeta_param_iter_init(cm, i);
				struct cmeta_slice evname = cmeta_param_iter(&it);
				struct radix_insert_ret r = radix_insert(events, event_names,
						evname.s, evname.len, false);
				int e;
				if (r.isnew) {
					e = event_new();
					*r.strp = -e;
					event_names[e] = evname;
					event_handlers_tails[e] = &event_handlers[e].hdr;
				}
				else {
					e = r.stridx;
					if (event_owners[e]) {
						diefile(2, file, cmeta_line(cm, i),
							"conflicting event definition");
					}
				}
				event_owners[e] = mod;
				event_predicateflags[e] = !!(flags & CMETA_EVENT_ISPREDICATE);
				struct list_chunkhdr *tail = &event_params[e].hdr;
				for (struct cmeta_slice param; param = cmeta_param_iter(&it),
						param.s;) {
					list_append(&tail, param);
				}
				break;
			case CMETA_ITEM_HANDLE_EVENT:
				int nparams = cmeta_nparams(cm, i);
				if (!nparams) {
					diefile(2, file, cmeta_line(cm, i),
							"event handler macro missing required parameter");
				}
				it = cmeta_param_iter_init(cm, i);
				evname = cmeta_param_iter(&it);
				r = radix_insert(events, event_names, evname.s, evname.len,
						false);
				if (r.isnew) {
					e = event_new();
					*r.strp = -e;
					event_names[e] = evname;
					event_handlers_tails[e] = &event_handlers[e].hdr;
				}
				else {
					e = r.stridx;
				}
				list_append(event_handlers_tails + e, mod);
				mod_flags[mod] |= HAS_EVENTS;
				break;
			case CMETA_ITEM_FEATURE:
				isfeat = true;
				// note: if no param given, featdesc will still be null
				cmeta_param_foreach (param, cm, i) {
					mod_featdescs[mod] = param;
					if (!radix_insertidx(featdescs, mod_featdescs, mod, true)) {
						diefile(2, file, cmeta_line(cm, i),
								"duplicate feature description text");
					}
					break;
				}
				break;
			case CMETA_ITEM_REQUIRE:
				if (!cmeta_nparams(cm, i)) {
					diefile(2, file, cmeta_line(cm, i),
							"dependency macro missing required parameter");
				}
				flags = cmeta_flags_require(cm, i);
				struct list_chunkhdr **tailp;
				switch_exhaust(flags) {
					case 0: tailp = &tail_needs; break;
					case CMETA_REQUIRE_OPTIONAL: tailp = &tail_wants; break;
					case CMETA_REQUIRE_GAMEDATA: tailp = &tail_gamedata; break;
					case CMETA_REQUIRE_GLOBAL: tailp = &tail_globals;
				}
				cmeta_param_foreach(param, cm, i) {
					int modflags = 0;
					switch_exhaust(flags) {
						case CMETA_REQUIRE_OPTIONAL:
							modflags = HAS_OPTDEPS;
						case 0:
							canpreinit = false;
							s16 depmod = radix_lookup(mods, mod_names, param.s,
									param.len, false);
							if (!depmod) {
								fprintf(stderr, "cmeta_fatal: %" fS ":%d: "
										"feature `%.*s` does not exist\n",
										file, cmeta_line(cm, i),
										param.len, param.s);
								exit(2);
							}
							mod_flags[depmod] |= modflags;
							list_append(tailp, depmod);
							break;
						case CMETA_REQUIRE_GAMEDATA: case CMETA_REQUIRE_GLOBAL:
							list_append(tailp, param);
					}
				}
				break;
			case CMETA_ITEM_GAMESPECIFIC:
				canpreinit = false;
				if_cold (!cmeta_nparams(cm, i)) {
					diefile(2, file, cmeta_line(cm, i),
							"GAMESPECIFIC macro missing required parameter");
				}
				needfeat = "GAMESPECIFIC specified";
				if_cold (mod_gamespecific[mod].s) {
					diefile(2, file, cmeta_line(cm, i),
							"conflicting GAMESPECIFIC macros");
				}
				cmeta_param_foreach(param, cm, i) {
					mod_gamespecific[mod] = param;
					break;
				}
				break;
			case CMETA_ITEM_INIT:
				if (hasinit) {
					diefile(2, file, cmeta_line(cm, i), "multiple INIT blocks");
				}
				hasinit = true;
				mod_flags[mod] |= HAS_INIT;
				needfeat = "INIT block defined";
				break;
			case CMETA_ITEM_PREINIT:
				if (haspreinit) {
					diefile(2, file, cmeta_line(cm, i),
							"multiple PREINIT blocks");
				}
				haspreinit = true;
				mod_flags[mod] |= HAS_PREINIT;
				needfeat = "PREINIT block defined";
				break;
			case CMETA_ITEM_END:
				if (mod_flags[mod] & HAS_END) {
					diefile(2, file, cmeta_line(cm, i), "multiple END blocks");
				}
				mod_flags[mod] |= HAS_END;
				needfeat = "END block defined";
		}
	}
	if (needfeat && !isfeat) {
		fprintf(stderr, "gluegen: fatal: %" fS ": %s without FEATURE()", file,
				needfeat);
		exit(2);
	}
	if (isfeat && !hasinit) {
		diefile(2, file, 0, "feature is missing INIT block");
	}
	if (!canpreinit && haspreinit) {
		diefile(2, file, 0, "cannot use dependencies along with PREINIT");
	}
}

static int dfs(s16 mod, bool first);
static int dfs_inner(s16 mod, s16 dep, bool first) {
	if (!(mod_flags[dep] & HAS_INIT)) {
		fprintf(stderr, "gluegen: fatal: feature `%.*s` tried to depend on "
				"non-feature module `%.*s`\n",
				mod_names[mod].len, mod_names[mod].s,
				mod_names[dep].len, mod_names[dep].s);
		exit(2);
	}
	switch (dfs(dep, false)) {
		// unwind the call stack by printing each node in the dependency cycle.
		// ASCII arrows are kind of ugly here but windows' CRT handles unicode
		// *so* horrendously and I'm not in the mood to replace stdio for this
		// (maybe another time!)
		case 1:
			fprintf(stderr, ".-> %.*s\n",
					mod_names[mod].len, mod_names[mod].s);
			return 2;
		case 2:
			fprintf(stderr, first ? "'-- %.*s\n" : "|   %.*s\n",
					mod_names[mod].len, mod_names[mod].s);
			return 2;
	}
	return 0;
}
static int dfs(s16 mod, bool first) {
	if (mod_flags[mod] & DFS_SEEN) return 0;
	if (mod_flags[mod] & DFS_SEEING) {
		fprintf(stderr, "gluegen: fatal: feature dependency cycle:\n");
		return 1;
	}
	mod_flags[mod] |= DFS_SEEING;
	list_foreach(s16, dep, mod_needs + mod) {
		if (dfs_inner(mod, dep, first)) return 2;
	}
	list_foreach(s16, dep, mod_wants + mod) {
		if (dfs_inner(mod, dep, first)) return 2;
	}
	feat_initorder[nfeatures++] = mod;
	mod_flags[mod] |= DFS_SEEN; // note: no need to bother unsetting SEEING.
	return 0;
}

static inline void sortfeatures() {
	for (int i = 1; i < nmods; ++i) {
		if ((mod_flags[i] & HAS_INIT) && dfs(i, true)) exit(2);
	}
}

static inline noreturn diewrite() { die(100, "couldn't write to file"); }
#define _(x) \
	if (fprintf(out, "%s\n", x) < 0) diewrite();
#define F(f, ...) \
	if (fprintf(out, f "\n", __VA_ARGS__) < 0) diewrite();
#define H_() \
	_("/* This file is autogenerated by src/build/gluegen.c. DO NOT EDIT! */")
#define H() H_() _("")

static void recursefeatdescs(FILE *out, s16 node) {
	if (node < 0) {
		if (mod_featdescs[-node].s) {
F( "	if (status_%.*s != FEAT_SKIP) {",
			mod_names[-node].len, mod_names[-node].s)
F( "		con_colourmsg(status_%.*s == FEAT_OK ? &green : &red,",
			mod_names[-node].len, mod_names[-node].s)
F( "				featmsgs[status_%.*s], %.*s);",
			mod_names[-node].len, mod_names[-node].s,
			mod_featdescs[-node].len, mod_featdescs[-node].s)
_( "	}")
		}
	}
	else if (node > 0) {
		for (int i = 0; i < 16; ++i) {
			recursefeatdescs(out, radices[node].children[i]);
		}
	}
}

static int evargs(FILE *out, s16 i, const char *suffix) {
	int j = 1;
	if (fprintf(out, "(") < 0) diewrite();
	list_foreach (struct cmeta_slice, param, event_params + i) {
		if (param.len == 4 && !memcmp(param.s, "void", 4)) {
			// UGH, crappy special case for (void). with C23 this could be
			// redundant, but I think we still want to avoid blowing up gluegen
			// if someone (me?) does it the old way out of habit.
			// also if someone does void, int something_else this will create
			// nonsense, but that's just garbage-in-garbage-out I guess.
			break;
		}
		else if (fprintf(out, "%stypeof(%.*s) a%d", j == 1 ? "" : ", ",
				param.len, param.s, j) < 0) {
			diewrite();
		}
		++j;
	}
	if (fputs(suffix, out) < 0) diewrite();
	return j;
}

static int evargs_notype(FILE *out, s16 i, const char *suffix) {
	int j = 1;
	if (fprintf(out, "(") < 0) diewrite();
	list_foreach(struct cmeta_slice, param, event_params + i) {
		if (param.len == 4 && !memcmp(param.s, "void", 4)) {
			break;
		}
		if (fprintf(out, "%sa%d", j == 1 ? "" : ", ", j) < 0) {
			diewrite();
		}
		++j;
	}
	if (fputs(suffix, out) < 0) diewrite();
	return j;
}

static inline void gencode(FILE *out, s16 featdescs) {
	for (int i = 1; i < nmods; ++i) {
		if (mod_flags[i] & HAS_INIT) {
F( "extern int _feat_init_%.*s();", mod_names[i].len, mod_names[i].s)
		}
		if (mod_flags[i] & HAS_PREINIT) {
F( "extern int _feat_preinit_%.*s();", mod_names[i].len, mod_names[i].s)
		}
		if (mod_flags[i] & HAS_END) {
F( "extern void _feat_end_%.*s();", mod_names[i].len, mod_names[i].s)
		}
	}
_( "")
_( "static struct {")
	for (int i = 1; i < nmods; ++i) {
		if (mod_flags[i] & HAS_OPTDEPS) continue;
		if ((mod_flags[i] & HAS_INIT) &&
				(mod_flags[i] & (HAS_END | HAS_EVENTS))) {
F( "	bool _has_%.*s : 1;", mod_names[i].len, mod_names[i].s)
		}
	}
	for (int i = 1; i < nmods; ++i) {
		if (mod_flags[i] & HAS_PREINIT) {
F( "	int preinit_%.*s : 2;", mod_names[i].len, mod_names[i].s)
		}
	}
_( "} feats = {0};")
_( "")
	for (int i = 1; i < nmods; ++i) {
		if (!(mod_flags[i] & HAS_INIT)) continue;
		if (mod_flags[i] & HAS_OPTDEPS) {
			// If something REQUESTS a feature, it needs to be able to query
			// has_*, so make it extern. XXX: this could be bitpacked as well
			// potentially however the whole struct would then have to be
			// included in all the places that use feature macros, which would
			// probably screw up the potential for fast incremental builds...
F( "bool has_%.*s = false;", mod_names[i].len, mod_names[i].s)
		}
		else if (mod_flags[i] & (HAS_END | HAS_EVENTS)) {
			// mildly stupid, but easier really. paper over the difference so we
			// can generate the same has_ checks elsewhere :^)
F( "#define has_%.*s (feats._has_%.*s)",
		mod_names[i].len, mod_names[i].s, mod_names[i].len, mod_names[i].s)
		}
	}
_( "")
	for (int i = 1; i < ncvars; ++i) {
F( "extern struct con_var *%.*s;", cvar_names[i].len, cvar_names[i].s);
	}
	for (int i = 1; i < nccmds; ++i) {
F( "extern struct con_cmd *%.*s;", ccmd_names[i].len, ccmd_names[i].s);
	}
_( "")
_( "static inline void preinitfeatures() {")
	for (int i = 1; i < nmods; ++i) {
		if (mod_flags[i] & HAS_PREINIT) {
F( "	feats.preinit_%.*s = _feat_preinit_%.*s();",
		mod_names[i].len, mod_names[i].s, mod_names[i].len, mod_names[i].s)
		}
	}
_( "}")
_( "")
_( "static inline void initfeatures() {")
	for (int i = 0; i < nfeatures; ++i) { // N.B.: this *should* be 0-indexed!
		const char *else_ = "";
		s16 mod = feat_initorder[i];
		if (mod_flags[mod] & HAS_PREINIT) {
F( "	s8 status_%.*s = feats.preinit_%.*s;",
		mod_names[mod].len, mod_names[mod].s,
		mod_names[mod].len, mod_names[mod].s)
		}
		else {
F( "	s8 status_%.*s;", mod_names[mod].len, mod_names[mod].s)
		}
		if (mod_gamespecific[mod].s) {
F( "	%sif (!GAMETYPE_MATCHES(%.*s)) status_%.*s = FEAT_SKIP;", else_,
		mod_gamespecific[mod].len, mod_gamespecific[mod].s,
		mod_names[mod].len, mod_names[mod].s)
			else_ = "else ";
		}
		list_foreach (struct cmeta_slice, gamedata, mod_gamedata + mod) {
			// this is not a *totally* ideal way of doing this, but it's easy.
			// if we had some info about what gamedata was doing, we could avoid
			// having to ifdef these cases and could just directly generate the
			// right thing. but that'd be quite a bit of work, so... we don't!
			if (mod_gamespecific[mod].s) {
F( "#ifdef _GAMES_WITH_%.*s", gamedata.len, gamedata.s)
F( "	%sif (!(_gametype_tag_%.*s & _GAMES_WITH_%.*s) && !has_%.*s) {", else_,
				mod_gamespecific[mod].len, mod_gamespecific[mod].s,
				gamedata.len, gamedata.s, gamedata.len, gamedata.s)
F( "		status_%.*s = NOGD;", mod_names[mod].len, mod_names[mod].s)
_( "	}")
_( "#else")
			}
F( "	%sif (!has_%.*s) status_%.*s = NOGD;", else_,
			gamedata.len, gamedata.s, mod_names[mod].len, mod_names[mod].s)
			if (mod_gamespecific[mod].s) {
_( "#endif")
			}
			else_ = "else ";
		}
		list_foreach (struct cmeta_slice, global, mod_globals + mod) {
F( "	%sif (!(%.*s)) status_%.*s = NOGLOBAL;", else_,
				global.len, global.s, mod_names[mod].len, mod_names[mod].s)
			else_ = "else ";
		}
		list_foreach (s16, dep, mod_needs + mod) {
F( "	%sif (status_%.*s != FEAT_OK) status_%.*s = REQFAIL;", else_,
				mod_names[dep].len, mod_names[dep].s,
				mod_names[mod].len, mod_names[mod].s)
			else_ = "else ";
		}
		if (mod_flags[mod] & (HAS_END | HAS_EVENTS | HAS_OPTDEPS)) {
F( "	%sif ((status_%.*s = _feat_init_%.*s()) == FEAT_OK) has_%.*s = true;",
			else_,
			mod_names[mod].len, mod_names[mod].s,
			mod_names[mod].len, mod_names[mod].s,
			mod_names[mod].len, mod_names[mod].s)
		}
		else {
F( "	%sstatus_%.*s = _feat_init_%.*s();", else_,
			mod_names[mod].len, mod_names[mod].s,
			mod_names[mod].len, mod_names[mod].s)
		}
	}
_( "")
	for (int i = 1; i < ncvars; ++i) {
		if (!(cvar_flags[i] & CMETA_CVAR_UNREG)) {
			if (cvar_flags[i] & CMETA_CVAR_FEAT) {
				struct cmeta_slice modname = mod_names[cvar_feats[i]];
F( "	if (status_%.*s != FEAT_SKIP) con_regvar(%.*s);",
		modname.len, modname.s, cvar_names[i].len, cvar_names[i].s)
F( "	else if (status_%.*s != FEAT_OK) %.*s->base.flags |= CON_HIDDEN;",
		modname.len, modname.s, cvar_names[i].len, cvar_names[i].s)
			}
			else {
F( "	con_regvar(%.*s);", cvar_names[i].len, cvar_names[i].s)
			}
		}
	}
	for (int i = 1; i < nccmds; ++i) {
		if (!(ccmd_flags[i] & CMETA_CCMD_UNREG)) {
			if (ccmd_flags[i] & CMETA_CCMD_FEAT) {
				struct cmeta_slice modname = mod_names[ccmd_feats[i]];
F( "	if (status_%.*s == FEAT_OK) con_regcmd(%.*s);",
		modname.len, modname.s, ccmd_names[i].len, ccmd_names[i].s)
			}
			else {
F( "	con_regcmd(%.*s);", ccmd_names[i].len, ccmd_names[i].s)
			}
		}
	}
_( "")
_( "	successbanner();")
_( "	struct rgba white = {255, 255, 255, 255};")
_( "	struct rgba green = {128, 255, 128, 255};")
_( "	struct rgba red   = {255, 128, 128, 255};")
_( "	con_colourmsg(&white, \"---- List of plugin features ---\\n\");");
	recursefeatdescs(out, featdescs);
_( "}")
_( "")
_( "static inline void endfeatures() {")
	for (int i = nfeatures - 1; i >= 0; --i) {
		s16 mod = feat_initorder[i];
		if (mod_flags[mod] & HAS_END) {
F( "	if (has_%.*s) _feat_end_%.*s();",
		mod_names[mod].len, mod_names[mod].s,
		mod_names[mod].len, mod_names[mod].s)
		}
	}
_( "}")
_( "")
_( "static inline void freevars() {")
	for (int i = 1; i < ncvars; ++i) {
F( "	extfree(%.*s->strval);", cvar_names[i].len, cvar_names[i].s)
	}
_( "}")
	for (int i = 1; i < nevents; ++i) {
		const char *prefix = event_predicateflags[i] ?
				"bool CHECK_" : "void EMIT_";
		if (fprintf(out, "\n%s%.*s", prefix,
				event_names[i].len, event_names[i].s) < 0) {
			diewrite();
		}
		evargs(out, i, ") {\n");
		list_foreach(s16, mod, event_handlers + i) {
			const char *type = event_predicateflags[i] ? "bool" : "void";
			if (fprintf(out, "\t%s _evhandler_%.*s_%.*s", type,
					mod_names[mod].len, mod_names[mod].s,
					event_names[i].len, event_names[i].s) < 0) {
				diewrite();
			}
			evargs(out, i, ");\n");
			if (event_predicateflags[i]) {
				if (mod_flags[mod] & HAS_INIT) {
					if (fprintf(out, "\tif (has_%.*s && !",
							mod_names[mod].len, mod_names[mod].s) < 0) {
						diewrite();
					}
				}
				else if (fputs("\tif (!", out) < 0) {
					diewrite();
				}
				if (fprintf(out, "_evhandler_%.*s_%.*s",
						mod_names[mod].len, mod_names[mod].s,
						event_names[i].len, event_names[i].s) < 0) {
					diewrite();
				}
				evargs_notype(out, i, ")) return false;\n");
			}
			else {
				if (fputc('\t', out) < 0) diewrite();
				if ((mod_flags[mod] & HAS_INIT) && fprintf(out, "if (has_%.*s) ",
						mod_names[mod].len, mod_names[mod].s) < 0) {
					diewrite();
				}
				if (fprintf(out, "_evhandler_%.*s_%.*s",
					mod_names[mod].len, mod_names[mod].s,
					event_names[i].len, event_names[i].s) < 0) {
					diewrite();
				}
				evargs_notype(out, i, ");\n");
			}
		}
		if (event_predicateflags[i]) {
_( "	return true;")
		}
_( "}")
	}
}

int OS_MAIN(int argc, os_char *argv[]) {
	s16 modlookup = 0, featdesclookup = 0, eventlookup = 0;
	if (argc > MAX_MODULES) {
		die(2, "too many files passed - increase MAX_MODULES in gluegen.c!");
	}
	nmods = argc;
	for (int i = 1; i < nmods; ++i) {
		const os_char *f = argv[i];
		int len = 0;
		int lastpart = 0;
		for (; f[len]; ++len) {
#ifdef _WIN32
			if (f[len] == '/' || f[len] == '\\') lastpart = len + 1;
#else
			if (f[len] == '/') lastpart = len + 1;
#endif
		}
		if_cold (!len) die(1, "empty file path given");
		if_cold (len < lastpart) diefile(1, f, 0, "invalid file path");
		if_cold (len < 3 || memcmp(f + len - 2, OS_LIT(".c"),
				2 * ssizeof(os_char))) {
			diefile(1, f, 0, "not a C source file (.c)");
		}
		struct cmeta_slice modname;
		// ugh. same dumb hack from compile scripts
		if_cold (len - lastpart == 6 && !memcmp(f + lastpart, OS_LIT("con_.c"),
				6 * ssizeof(os_char))) {
			modname.s = "con"; modname.len = 3;
		}
		else {
			char *p = arena + arena_new(len - lastpart - 2);
			// XXX: Unicode isn't real, it can't hurt you.
			for (int i = lastpart, j = 0; i < len - 2; ++i, ++j) p[j] = f[i];
			modname.s = p; modname.len = len - lastpart - 2;
		}
		mod_names[i] = modname;
		if (!radix_insertidx(&modlookup, mod_names, i, false)) {
			// XXX: might have to fix this some day to handle subdirs and such.
			// for now we rely on it happening not to be a problem basically lol
			diefile(2, f, 0, "duplicate module name");
		}
	}
	for (int i = 1; i < nmods; ++i) {
		struct cmeta cm = cmeta_loadfile(argv[i]);
		handle(i, modlookup, &featdesclookup, &eventlookup, argv[i], &cm);
	}
	// double check that events are defined. the compiler would also catch this,
	// but we can do it faster and with arguably more helpful error information.
	for (int i = 1; i < nevents; ++i) {
		if (!event_owners[i]) {
			fprintf(stderr, "gluegen: fatal: undefined event %.*s\n",
					event_names[i].len, event_names[i].s);
			exit(2);
		}
	}
	sortfeatures();

	FILE *out = fopen(".build/include/glue.gen.h", "wb");
	if (!out) die(100, "couldn't open .build/include/glue.gen.h");
	H()
	gencode(out, featdesclookup);
	if (fflush(out)) die(100, "couldn't finish writing output");
	return 0;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
