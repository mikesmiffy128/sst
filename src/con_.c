/* THIS FILE SHOULD BE CALLED `con.c` BUT WINDOWS IS STUPID */
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

#include <stddef.h> // should be implied by stdlib but glibc is dumb (offsetof)
#include <stdlib.h>
#include <stdio.h>

#include "abi.h"
#include "con_.h"
#include "engineapi.h" // for factories and rgba - XXX: is this a bit circular?
#include "extmalloc.h"
#include "gamedata.h"
#include "gametype.h"
#include "langext.h"
#include "mem.h"
#include "os.h"
#include "vcall.h"
#include "version.h"

/******************************************************************************\
 * Have you ever noticed that when someone comments "here be dragons" there's *
 * no actual dragons? Turns out, that's because the dragons all migrated over *
 * here, so that they could build multiple inheritance vtables in C, by hand. *
 *                                                                            *
 * Don't get set on fire.                                                     *
\******************************************************************************/

static int dllid; // from AllocateDLLIdentifier(), lets us unregister in bulk
int con_cmdclient;

DECL_VFUNC(struct ICvar, void *, FindCommandBase_p2, 13, const char *)
DECL_VFUNC(struct ICvar, void *, FindCommand_nonp2, 14, const char *)
DECL_VFUNC(struct ICvar, void *, FindVar_nonp2, 12, const char *)

DECL_VFUNC_DYN(struct ICvar, int, AllocateDLLIdentifier)
DECL_VFUNC_DYN(struct ICvar, void, RegisterConCommand, /*ConCommandBase*/ void *)
DECL_VFUNC_DYN(struct ICvar, void, UnregisterConCommands, int)
DECL_VFUNC_DYN(struct ICvar, struct con_var *, FindVar, const char *)
//DECL_VFUNC(struct ICvar, const struct con_var *, FindVar_const, 13, const char *)
DECL_VFUNC_DYN(struct ICvar, struct con_cmd *, FindCommand, const char *)
DECL_VFUNC_DYN(struct ICvar, void, CallGlobalChangeCallbacks, struct con_var *,
		const char *, float)

static struct ICvar *coniface;
static void *colourmsgf;

asm_only void _con_colourmsg(void *dummy, const struct rgba *c, const char *fmt, ...) {
	// NE: ConsoleColorPrintf is virtual, so the dummy param is a carve-out for
	// `this` (which is coniface).
	// OE: it's a global function, with no this param, so we'd simply fix up the
	// stack (pop return address, put it in the dummy slot).
	// TODO(compat): actually implementing the OE case might be a bit tricky. it
	// seems the most efficient thing would be self-modifying code (i.e. replace
	// this function's asm with different asm). that'll be interesting...
	__asm volatile (
		"mov eax, %0\n"
		"mov [esp + 4], eax\n" // put coniface in the empty stack slot
		"jmp dword ptr %1\n" // jump to the real function
		:
		: "m" (coniface), "m" (colourmsgf)
		: "eax", "edi", "memory"
	);
}

static inline void initval(struct con_var *v) {
	v->v2.strval = extmalloc(v->v2.strlen); // note: _DEF_CVAR() sets strlen
	memcpy(v->v2.strval, v->v2.defaultval, v->v2.strlen);
}

static void VCALLCONV dtor(void *_) {} // we don't use constructors/destructors

static bool VCALLCONV IsCommand_cmd(void *this) { return true; }
static bool VCALLCONV IsCommand_var(void *this) { return false; }

static bool VCALLCONV IsFlagSet_cmd(struct con_cmd *this, int flags) {
	return !!(this->base.flags & flags);
}
static bool VCALLCONV IsFlagSet_var(struct con_var *this, int flags) {
	return !!(this->base.flags & flags);
}
static void VCALLCONV AddFlags_cmd(struct con_cmd *this, int flags) {
	this->base.flags |= flags;
}
static void VCALLCONV AddFlags_var(struct con_var *this, int flags) {
	this->base.flags |= flags;
}
static void VCALLCONV RemoveFlags_cmd(struct con_cmd *this, int flags) {
	this->base.flags &= ~flags;
}
static void VCALLCONV RemoveFlags_var(struct con_var *this, int flags) {
	this->base.flags &= ~flags;
}
static int VCALLCONV GetFlags_cmd(struct con_cmd *this) {
	return this->base.flags;
}
static int VCALLCONV GetFlags_var(struct con_var *this) {
	return this->base.flags;
}

static const char *VCALLCONV GetName_cmd(struct con_cmd *this) {
	return this->base.name;
}
static const char *VCALLCONV GetName_var(struct con_var *this) {
	return this->base.name;
}
static const char *VCALLCONV GetHelpText_cmd(struct con_cmd *this) {
	return this->base.help;
}
static const char *VCALLCONV GetHelpText_var(struct con_var *this) {
	return this->base.help;
}
static bool VCALLCONV IsRegistered(struct con_cmdbase *this) {
	return this->registered;
}
static int VCALLCONV GetDLLIdentifier(struct con_cmdbase *this) {
	return dllid;
}
static void VCALLCONV Create_base(struct con_cmdbase *this, const char *name,
		const char *help, int flags) {} // nop, we static init already
static void VCALLCONV Init(struct con_cmdbase *this) {} // ""

static bool VCALLCONV ClampValue(struct con_var *this, float *f) {
	if (this->v2.hasmin && this->v2.minval > *f) {
		*f = this->v2.minval;
		return true;
	}
	if (this->v2.hasmax && this->v2.maxval < *f) {
		*f = this->v2.maxval;
		return true;
	}
	return false;
}

int VCALLCONV AutoCompleteSuggest(struct con_cmd *this, const char *partial,
		/*CUtlVector*/ void *commands) {
	// TODO(autocomplete): implement this if needed later
	return 0;
}
bool VCALLCONV CanAutoComplete(struct con_cmd *this) {
	return false;
}
void VCALLCONV Dispatch(struct con_cmd *this, const struct con_cmdargs *args) {
	this->cb(args->argc, args->argv);
}

static void VCALLCONV ChangeStringValue(struct con_var *this, const char *s,
		float oldf) {
	char *old = alloca(this->v2.strlen);
	memcpy(old, this->v2.strval, this->v2.strlen);
	int len = strlen(s) + 1;
	if (len > this->v2.strlen) {
		this->v2.strval = extrealloc(this->v2.strval, len);
		this->v2.strlen = len;
	}
	memcpy(this->v2.strval, s, len);
	// callbacks don't matter as far as ABI compat goes (and thank goodness
	// because e.g. portal2 randomly adds a *list* of callbacks!?). however we
	// do need callbacks for at least one feature, so do our own minimal thing
	if (this->cb) this->cb(this);
	// also call global callbacks, as is polite.
	CallGlobalChangeCallbacks(coniface, this, old, oldf);
}

// NOTE: these Internal* functions are virtual in the engine, but nowadays we
// just call them directly since they're private to us. We still put them in the
// vtable just in case (see below), though arguably nothing in the engine
// *should* be calling these internal things anyway.

static void VCALLCONV InternalSetValue(struct con_var *this, const char *v) {
	float oldf = this->v2.fval;
	float newf = atof(v);
	char tmp[32];
	if (ClampValue(this, &newf)) {
		snprintf(tmp, sizeof(tmp), "%f", newf);
		v = tmp;
	}
	this->v2.fval = newf;
	this->v2.ival = (int)newf;
	if (!(this->base.flags & CON_NOPRINT)) ChangeStringValue(this, v, oldf);
}

static void VCALLCONV InternalSetFloatValue(struct con_var *this, float v) {
	if (v == this->v2.fval) return;
	ClampValue(this, &v);
	float old = this->v2.fval;
	this->v2.fval = v; this->v2.ival = (int)this->v2.fval;
	if (!(this->base.flags & CON_NOPRINT)) {
		char tmp[32];
		snprintf(tmp, sizeof(tmp), "%f", this->v2.fval);
		ChangeStringValue(this, tmp, old);
	}
}

static void VCALLCONV InternalSetIntValue(struct con_var *this, int v) {
	if (v == this->v2.ival) return;
	float f = (float)v;
	if (ClampValue(this, &f)) v = (int)f;
	float old = this->v2.fval;
	this->v2.fval = f; this->v2.ival = v;
	if (!(this->base.flags & CON_NOPRINT)) {
		char tmp[32];
		snprintf(tmp, sizeof(tmp), "%f", this->v2.fval);
		ChangeStringValue(this, tmp, old);
	}
}

// IConVar calls get this-adjusted pointers, so just subtract the offset
static void VCALLCONV SetValue_str_thunk(void *thisoff, const char *v) {
	struct con_var *this = mem_offset(thisoff,
			-offsetof(struct con_var, vtable_iconvar));
	InternalSetValue(this, v);
}
static void VCALLCONV SetValue_f_thunk(void *thisoff, float v) {
	struct con_var *this = mem_offset(thisoff,
			-offsetof(struct con_var, vtable_iconvar));
	InternalSetFloatValue(this, v);
}
static void VCALLCONV SetValue_i_thunk(void *thisoff, int v) {
	struct con_var *this = mem_offset(thisoff,
			-offsetof(struct con_var, vtable_iconvar));
	InternalSetIntValue(this, v);
}
static void VCALLCONV SetValue_colour_thunk(void *thisoff, struct rgba v) {
	struct con_var *this = mem_offset(thisoff,
			-offsetof(struct con_var, vtable_iconvar));
	InternalSetIntValue(this, v.val);
}

// more misc thunks, hopefully these just compile to a lea and a jmp
static const char *VCALLCONV GetName_thunk(void *thisoff) {
	struct con_var *this = mem_offset(thisoff,
			-offsetof(struct con_var, vtable_iconvar));
	return GetName_var(this);
}
static bool VCALLCONV IsFlagSet_thunk(void *thisoff, int flags) {
	struct con_var *this = mem_offset(thisoff,
			-offsetof(struct con_var, vtable_iconvar));
	return IsFlagSet_var(this, flags);
}

// dunno what this is actually for...
static int VCALLCONV GetSplitScreenPlayerSlot(struct con_var *thisoff) {
	return 0;
}

// aand yet another Create nop
static void VCALLCONV Create_var(void *thisoff, const char *name,
		const char *defaultval, int flags, const char *helpstr, bool hasmin,
		float min, bool hasmax, float max, void *cb) {}

// the first few members of ConCommandBase are the same between versions
void *_con_vtab_cmd[14 + NVDTOR] = {
	(void *)&dtor,
#ifndef _WIN32
	(void *)&dtor,
#endif
	(void *)&IsCommand_cmd,
	(void *)&IsFlagSet_cmd,
	(void *)&AddFlags_cmd
};

// the engine does dynamic_casts on ConVar at some points so we have to fill out
// bare minimum rtti to prevent crashes. oh goody.
#ifdef _WIN32
DEF_MSVC_BASIC_RTTI(static, varrtti, _con_vtab_var, "sst_ConVar")
#else
DEF_ITANIUM_BASIC_RTTI(static, varrtti, "sst_ConVar")
#endif

struct _con_vtab_var_wrap _con_vtab_var_wrap = {
#ifndef _WIN32
	0, // this *is* the top, no offset needed :)
#endif
	&varrtti,
	(void *)&dtor,
#ifndef _WIN32
	(void *)&dtor,
#endif
	(void *)&IsCommand_var,
	(void *)&IsFlagSet_var,
	(void *)&AddFlags_var
};

struct _con_vtab_iconvar_wrap _con_vtab_iconvar_wrap = {
#ifdef _WIN32
	0 // because of crazy overload vtable order we can't prefill *anything*
#else
	// RTTI members first on linux:
	-offsetof(struct con_var, vtable_iconvar),
	&varrtti,
	// colour is the last of the 4 on linux so we can at least prefill these 3
	(void *)&SetValue_str_thunk,
	(void *)&SetValue_f_thunk,
	(void *)&SetValue_i_thunk
#endif
};

void con_regvar(struct con_var *v) {
	initval(v);
	RegisterConCommand(coniface, v);
}

void con_regcmd(struct con_cmd *c) {
	RegisterConCommand(coniface, c);
}

// XXX: these should use vcall/gamedata stuff as they're only used for the
// setter API after everything is brought up. however that will require some
// kind of windows/linux conditionals in the gamedata system! this solution is
// just hacked in for now to get things working because it was broken before...
#ifdef _WIN32
static int vtidx_SetValue_str = 2, vtidx_SetValue_f = 1, vtidx_SetValue_i = 0;
#else
enum { vtidx_SetValue_str = 0, vtidx_SetValue_f = 1, vtidx_SetValue_i = 2 };
#endif

void con_init() {
	colourmsgf = coniface->vtable[vtidx_ConsoleColorPrintf];
	dllid = AllocateDLLIdentifier(coniface);

	void **pc = _con_vtab_cmd + 3 + NVDTOR, **pv = _con_vtab_var + 3 + NVDTOR,
			**pi = _con_vtab_iconvar
#ifndef _WIN32
				+ 3
#endif
			;
	if (GAMETYPE_MATCHES(L4Dbased)) { // 007 base
		*pc++ = (void *)&RemoveFlags_cmd;
		*pc++ = (void *)&GetFlags_cmd;
		*pv++ = (void *)&RemoveFlags_var;
		*pv++ = (void *)&GetFlags_var;
	}
	// base stuff in cmd
	*pc++ = (void *)&GetName_cmd;
	*pc++ = (void *)&GetHelpText_cmd;
	*pc++ = (void *)&IsRegistered;
	*pc++ = (void *)&GetDLLIdentifier;
	*pc++ = (void *)&Create_base;
	*pc++ = (void *)&Init;
	// cmd-specific
	*pc++ = (void *)&AutoCompleteSuggest;
	*pc++ = (void *)&CanAutoComplete;
	*pc++ = (void *)&Dispatch;
	// base stuff in var
	*pv++ = (void *)&GetName_var;
	*pv++ = (void *)&GetHelpText_var;
	*pv++ = (void *)&IsRegistered;
	*pv++ = (void *)&GetDLLIdentifier;
	*pv++ = (void *)&Create_base;
	*pv++ = (void *)&Init;
	// var-specific
	*pv++ = (void *)&InternalSetValue;
	*pv++ = (void *)&InternalSetFloatValue;
	*pv++ = (void *)&InternalSetIntValue;
	if (GAMETYPE_MATCHES(L4D2x) || GAMETYPE_MATCHES(Portal2)) { // ugh, annoying
		// InternalSetColorValue, literally the same machine instructions as int
		*pv++ = (void *)&InternalSetIntValue;
	}
	*pv++ = (void *)&ClampValue;;
	*pv++ = (void *)&ChangeStringValue;
	*pv++ = (void *)&Create_var;
	if (GAMETYPE_MATCHES(L4D2x) || GAMETYPE_MATCHES(Portal2)) {
		*pi++ = (void *)&SetValue_colour_thunk;
#ifdef _WIN32
		// stupid hack for above mentioned crazy overload ordering
		++vtidx_SetValue_str;
		++vtidx_SetValue_i;
		++vtidx_SetValue_f;
#endif
	}
#ifdef _WIN32
	// see above: these aren't prefilled due the the reverse order
	*pi++ = (void *)&SetValue_i_thunk;
	*pi++ = (void *)&SetValue_f_thunk;
	*pi++ = (void *)&SetValue_str_thunk;
#endif
	*pi++ = (void *)&GetName_thunk;
	// GetBaseName (we just return actual name in all cases)
	if (GAMETYPE_MATCHES(L4Dbased)) *pi++ = (void *)&GetName_thunk;
	*pi++ = (void *)&IsFlagSet_thunk;
	// last one: not in 004, but doesn't matter. one less branch!
	*pi++ = (void *)&GetSplitScreenPlayerSlot;
}

static void helpuserhelpus(int pluginver, char ifaceverchar) {
	con_msg("\n");
	con_msg("-- Please include ALL of the following if asking for help:\n");
	con_msg("--   plugin:     " LONGNAME " v" VERSION "\n");
	con_msg("--   interfaces: %d/%c\n", pluginver, ifaceverchar);
	con_msg("\n");
}

// note: for now at least, not using errmsg_*() macros here because it doesn't
// really make sense for these messages to be coming from "con"

static void warnoe() {
	con_warn("sst: error: this engine version is not yet supported\n");
}

bool con_detect(int pluginver) {
	if (coniface = factory_engine("VEngineCvar007", 0)) {
		// GENIUS HACK (BUT STILL BAD): Portal 2 has everything in ICvar shifted
		// down 3 places due to the extra stuff in IAppSystem. This means that
		// if we look up the Portal 2-specific cvar using FindCommandBase, it
		// *actually* calls the const-overloaded FindVar on other branches,
		// which just happens to still work fine. From there, we can figure out
		// the actual ABI to use to avoid spectacular crashes.
		if (FindCommandBase_p2(coniface, "portal2_square_portals")) {
			_gametype_tag |= _gametype_tag_Portal2;
			return true;
		}
		if (FindCommand_nonp2(coniface, "l4d2_snd_adrenaline")) {
			// while we're here, also distinguish Survivors, the stupid Japanese
			// arcade game a few people seem to care about for some reason
			// (which for some other reason also has some vtable changes)
			if (FindVar_nonp2(coniface, "avatarbasemodel")) {
				_gametype_tag |= _gametype_tag_L4DS;
			}
			else {
				_gametype_tag |= _gametype_tag_L4D2;
			}
			return true;
		}
		if (FindVar_nonp2(coniface, "z_difficulty")) {
			_gametype_tag |= _gametype_tag_L4D1;
			return true;
		}
		con_warn("sst: error: game is unsupported (using VEngineCvar007)\n");
		helpuserhelpus(pluginver, '7');
		return false;
	}
	if (coniface = factory_engine("VEngineCvar004", 0)) {
		// TODO(compat): are there any cases where 004 is incompatible? could
		// this crash? find out!
		if (pluginver == 3) _gametype_tag |= _gametype_tag_2013;
		else _gametype_tag |= _gametype_tag_OrangeBox;
		return true;
	}
	if (factory_engine("VEngineCvar003", 0)) {
		warnoe();
		helpuserhelpus(pluginver, '3');
		return false;
	}
	// I don't suppose there's anything below 002 worth caring about? Shrug.
	if (factory_engine("VEngineCvar002", 0)) {
		warnoe();
		helpuserhelpus(pluginver, '2');
		return false;
	}
	con_warn("sst: error: couldn't find a supported console interface\n");
	helpuserhelpus(pluginver, '?');
	return false;
}

void con_disconnect() {
	UnregisterConCommands(coniface, dllid);
}

struct con_var *con_findvar(const char *name) {
	return FindVar(coniface, name);
}

struct con_cmd *con_findcmd(const char *name) {
	return FindCommand(coniface, name);
}

// NOTE: getters here still go through the parent pointer although we stopped
// doing that internally, just in case we run into parented cvars in the actual
// engine. a little less efficient, but safest and simplest for now.
#define GETTER(T, N, M) \
	T N(const struct con_var *v) { \
		return v->v2.parent->v2.M; \
	}
GETTER(const char *, con_getvarstr, strval)
GETTER(float, con_getvarf, fval)
GETTER(int, con_getvari, ival)
#undef GETTER

// XXX: move this to vcall/gamedata (will require win/linux conditionals first!)
// see also above comment on the vtidx definitions
#define SETTER(T, I, N) \
	void N(struct con_var *v, T x) { \
		((void (*VCALLCONV)(void *, T))(v->vtable_iconvar[I]))( \
				&v->vtable_iconvar, x); \
	}
SETTER(const char *, vtidx_SetValue_str, con_setvarstr)
SETTER(float, vtidx_SetValue_f, con_setvarf)
SETTER(int, vtidx_SetValue_i, con_setvari)
#undef SETTER

con_cmdcbv2 con_getcmdcbv2(const struct con_cmd *cmd) {
	return !cmd->use_newcmdiface && cmd->use_newcb ? cmd->cb_v2 : 0;
}

con_cmdcbv1 con_getcmdcbv1(const struct con_cmd *cmd) {
	return !cmd->use_newcmdiface && !cmd->use_newcb ? cmd->cb_v1 : 0;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
