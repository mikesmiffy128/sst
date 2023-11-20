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

#include "con_.h"
#include "dictmaptree.h"
#include "engineapi.h"
#include "errmsg.h"
#include "feature.h"
#include "gamedata.h"
#include "gametype.h"
#include "intdefs.h"
#include "mem.h"
#include "vcall.h"
#include "x86.h"
#include "x86util.h"

FEATURE()

DECL_VFUNC_DYN(void *, PEntityOfEntIndex, int)
static struct edict **edicts = 0;

struct edict *ent_getedict(int idx) {
	if (edicts) {
		// globalvars->edicts seems to be null when disconnected
		if (!*edicts) return 0;
		return mem_offset(*edicts, sz_edict * idx);
	}
	else {
		return PEntityOfEntIndex(engserver, idx);
	}
}

void *ent_get(int idx) {
	struct edict *e = ent_getedict(idx);
	if (!e) return 0;
	return e->ent_unknown;
}

struct CEntityFactory {
	struct CEntityFactory_vtable {
		void /*IServerNetworkable*/ *(*VCALLCONV Create)(
				struct CEntityFactory *this, const char *name);
		void (*VCALLCONV Destroy)(struct CEntityFactory *this,
				void /*IServerNetworkable*/ *networkable);
		usize (*VCALLCONV GetEntitySize)(struct CEntityFactory *this);
	} *vtable;
};

struct CEntityFactoryDictionary {
	void **vtable;
	struct CUtlDict_p_ushort dict;
};

#ifdef _WIN32 // TODO(linux): this'll be different too, leaving out for now
static struct CEntityFactoryDictionary *entfactorydict = 0;
static inline bool find_entfactorydict(con_cmdcb dumpentityfactories_cb) {
	const uchar *insns = (const uchar *)dumpentityfactories_cb;
	for (const uchar *p = insns; p - insns < 64;) {
		// EntityFactoryDictionary() is inlined, and returns a static, which is
		// lazy-inited (trivia: this was old MSVC, so it's not thread-safe like
		// C++ requires nowadays). for some reason the init flag is set using
		// OR, and then the instance is put in ECX to call the ctor
		if (p[0] == X86_ORMRW && p[6] == X86_MOVECXI && p[11] == X86_CALL) {
			entfactorydict = mem_loadptr(p + 7);
			return true;
		}
		NEXT_INSN(p, "entity factory dictionary");
	}
	return false;
}
#endif

const struct CEntityFactory *ent_getfactory(const char *name) {
#ifdef _WIN32
	if (entfactorydict) {
		return CUtlDict_p_ushort_findval(&entfactorydict->dict, name);
	}
#endif
	return 0;
}

typedef void (*VCALLCONV ctor_func)(void *);
static inline ctor_func findctor(const struct CEntityFactory *factory,
		const char *classname) {
#ifdef _WIN32
	const uchar *insns = (const uchar *)factory->vtable->Create;
	// mostly every Create() method follows the same pattern. after calling what
	// is presumably operator new(), it copies the return value from a register
	// into ECX and then calls the constructor.
	//
	// there have also been some thunky-looking ones, which we attempt to
	// resolve by following the first call if we bump into a ret before anything
	// else. this is depth-limited to prevent things getting out of hand.
	const uchar *seencall = 0;
	int depth = 3;
	for (const uchar *p = insns; p - insns < 32;) {
		if (!seencall && p[0] == X86_CALL) {
			seencall = p;
		}
		else {
			if (p[0] == X86_MOVRMW && (p[1] & 0xF8) == 0xC8
					&& p[2] == X86_CALL) {
				return (ctor_func)(p + 7 + mem_loadoffset(p + 3));
			}
			if (p[0] == X86_RET || p[0] == X86_RETI16) {
				if (seencall && --depth) {
					p = seencall + 5 + mem_loadoffset(seencall + 1); insns = p;
					seencall = 0;
					continue;
				}
				return false;
			}
		}
		// duping NEXT_INSN macro here in the name of a nicer message
		int len = x86_len(p);
		if (len == -1) {
			errmsg_errorx("unknown or invalid instruction looking for %s "
					"constructor", classname);
			return 0;
		}
		p += len;
	}
#else
#warning TODO(linux): this will be different of course
#endif
	return 0;
}

void **ent_findvtable(const struct CEntityFactory *factory,
		const char *classname) {
#ifdef _WIN32
	ctor_func ctor = findctor(factory, classname);
	if (!ctor) return 0;
	const uchar *insns = (const uchar *)ctor;
	// the constructor itself should do *(void**)this = &vtable; almost right
	// away, so look for the first immediate load into indirect register
	for (const uchar *p = insns; p - insns < 32;) {
		if (p[0] == X86_MOVMIW && (p[1] & 0xF8) == 0) return mem_loadptr(p + 2);
		int len = x86_len(p);
		if (len == -1) {
			errmsg_errorx("unknown or invalid instruction looking for %s "
					"vtable pointer", classname);
			return 0;
		}
		p += len;
	}
#else
#warning TODO(linux): this will be different of course
#endif
	return 0;
}

INIT {
#ifdef _WIN32 // TODO(linux): above
	struct con_cmd *dumpentityfactories = con_findcmd("dumpentityfactories");
	if (!dumpentityfactories || !find_entfactorydict(dumpentityfactories->cb)) {
		errmsg_warnx("server entity factories unavailable");
	}
#endif

	// for PEntityOfEntIndex we don't really have to do any more init, we
	// can just call the function later.
	if (has_vtidx_PEntityOfEntIndex) return true;
	if (globalvars && has_off_edicts) {
		edicts = mem_offset(globalvars, off_edicts);
		return true;
	}
	errmsg_warnx("not implemented for this engine");
	return false;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
