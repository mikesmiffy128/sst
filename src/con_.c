/* XXX: THIS FILE SHOULD BE CALLED `con.c` BUT WINDOWS IS STUPID */
/*
 * Copyright © 2021 Michael Smith <mikesmiffy128@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

#include "abi.h"
#include "con_.h"
#include "extmalloc.h"
#include "gameinfo.h"
#include "gametype.h"
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

// given to us by the engine to unregister cvars in bulk on plugin unload
static int dllid;

// external spaghetti variable, exists only because valve are bad
int con_cmdclient;

// these have to be extern because of varargs nonsense - they get wrapped in a
// macro for the actual api (con_colourmsg)
void *_con_iface;
void (*_con_colourmsgf)(void *this, const struct con_colour *c, const char *fmt,
		...) _CON_PRINTF(3, 4);

// XXX: the const and non-const entries might actually be flipped on windows,
// not 100% sure, but dunno if it's worth essentially duping most of these when
// the actual executed machine code is probably identical anyway.
DECL_VFUNC0(int, AllocateDLLIdentifier, 5)
DECL_VFUNC0(int, AllocateDLLIdentifier_p2, 8)
DECL_VFUNC(void, RegisterConCommand, 6, /*ConCommandBase*/ void *)
DECL_VFUNC(void, RegisterConCommand_p2, 9, /*ConCommandBase*/ void *)
DECL_VFUNC(void, UnregisterConCommands, 8, int)
DECL_VFUNC(void, UnregisterConCommands_p2, 11, int)
// DECL_VFUNC(void *, FindCommandBase, 10, const char *)
DECL_VFUNC(void *, FindCommandBase_p2, 13, const char *)
DECL_VFUNC(struct con_var *, FindVar, 12, const char *)
// DECL_VFUNC0(const struct con_var *, FindVar_const, 13, const char *)
DECL_VFUNC(struct con_var *, FindVar_p2, 15, const char *)
DECL_VFUNC(struct con_cmd *, FindCommand, 14, const char *)
DECL_VFUNC(struct con_cmd *, FindCommand_p2, 17, const char *)
DECL_VFUNC(void, CallGlobalChangeCallbacks, 20, struct con_var *, const char *,
		float)
DECL_VFUNC(void, CallGlobalChangeCallbacks_l4d, 18, struct con_var *,
		const char *, float)
DECL_VFUNC(void, CallGlobalChangeCallbacks_p2, 21, struct con_var *,
		const char *, float)
DECL_VFUNC_CDECL(void, ConsoleColorPrintf_004, 23, const struct con_colour *,
		const char *, ...)
DECL_VFUNC_CDECL(void, ConsoleColorPrintf_l4d, 21, const struct con_colour *,
		const char *, ...)
DECL_VFUNC_CDECL(void, ConsoleColorPrintf_p2, 24, const struct con_colour *,
		const char *, ...)

static inline void initval(struct con_var *v) {
	// v->strlen is set to defaultval len in _DEF_CVAR so we don't need to call
	// strlen() on each string :)
	v->strval = extmalloc(v->strlen);
	memcpy(v->strval, v->defaultval, v->strlen);
}

// generated by build/codegen.c, defines regcmds() and freevars()
#include <cmdinit.gen.h>

// to try and be like the engine even though it's probably not actually
// required, we call the Internal* virtual functions by actual virtual lookup.
// since the vtables are filled dynamically (below), we store this index; other
// indexes are just offset from this one since the 3-or-4 functions are all
// right next to each other.
static int vtidx_InternalSetValue;

// implementatiosn of virtual functions for our vars and commands below...

static void VCALLCONV dtor(void *_) {} // we don't use constructors/destructors

static bool VCALLCONV IsCommand_cmd(void *this) { return true; }
static bool VCALLCONV IsCommand_var(void *this) { return false; }

// flag stuff
static bool VCALLCONV IsFlagSet_cmd(struct con_cmd *this, int flags) {
	return !!(this->base.flags & flags);
}
static bool VCALLCONV IsFlagSet_var(struct con_var *this, int flags) {
	return !!(this->parent->base.flags & flags);
}
static void VCALLCONV AddFlags_cmd(struct con_cmd *this, int flags) {
	this->base.flags |= flags;
}
static void VCALLCONV AddFlags_var(struct con_var *this, int flags) {
	this->parent->base.flags |= flags;
}
static void VCALLCONV RemoveFlags_cmd(struct con_cmd *this, int flags) {
	this->base.flags &= ~flags;
}
static void VCALLCONV RemoveFlags_var(struct con_var *this, int flags) {
	this->parent->base.flags &= ~flags;
}
static int VCALLCONV GetFlags_cmd(struct con_cmd *this) {
	return this->base.flags;
}
static int VCALLCONV GetFlags_var(struct con_var *this) {
	return this->parent->base.flags;
}

// basic registration stuff
static const char *VCALLCONV GetName_cmd(struct con_cmd *this) {
	return this->base.name;
}
static const char *VCALLCONV GetName_var(struct con_var *this) {
	return this->parent->base.name;
}
static const char *VCALLCONV GetHelpText_cmd(struct con_cmd *this) {
	return this->base.help;
}
static const char *VCALLCONV GetHelpText_var(struct con_var *this) {
	return this->parent->base.help;
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
	if (this->hasmin && this->minval > *f) { *f = this->minval; return true; }
	if (this->hasmax && this->maxval < *f) { *f = this->maxval; return true; }
	return false;
}

// command-specific stuff
int VCALLCONV AutoCompleteSuggest(void *this, const char *partial,
		/* CUtlVector */ void *commands) {
	// TODO(autocomplete): implement this if needed later
	return 0;
}
bool VCALLCONV CanAutoComplete(void *this) {
	return false;
}
void VCALLCONV Dispatch(struct con_cmd *this, const struct con_cmdargs *args) {
	// only try cb, cbv1 and iface should never get used by us
	if (this->use_newcb && this->cb) this->cb(args);
}

// var-specific stuff
static void VCALLCONV ChangeStringValue(struct con_var *this, const char *s,
		float oldf) {
	char *old = alloca(this->strlen);
	memcpy(old, this->strval, this->strlen);
	int len = strlen(s) + 1;
	if (len > this->strlen) {
		this->strval = extrealloc(this->strval, len);
		this->strlen = len;
	}
	memcpy(this->strval, s, len);
	//if (cb) {...} // not bothering
	// also note: portal2 has a *list* of callbacks, although that part of ABI
	// doesn't matter as far as plugin compat goes, so still not bothering
	// we do however bother to call global callbacks, as is polite.
	if (GAMETYPE_MATCHES(Portal2)) {
		VCALL(_con_iface, CallGlobalChangeCallbacks_p2, this, old, oldf);
	}
	else if (GAMETYPE_MATCHES(L4D)) {
		VCALL(_con_iface, CallGlobalChangeCallbacks_l4d, this, old, oldf);
	}
	else {
		VCALL(_con_iface, CallGlobalChangeCallbacks, this, old, oldf);
	}
}

static void VCALLCONV InternalSetValue(struct con_var *this, const char *v) {
	float oldf = this->fval;
	float newf = atof(v);
	char tmp[32];
	// NOTE: calling our own ClampValue and ChangeString, not bothering with
	// vtable (it's internal anyway, so we're never calling into engine code)
	if (ClampValue(this, &newf)) {
		snprintf(tmp, sizeof(tmp), "%f", newf);
		v = tmp;
	}
	this->fval = newf;
	this->ival = (int)newf;
	if (!(this->base.flags & CON_NOPRINT)) ChangeStringValue(this, v, oldf);
}

static void VCALLCONV InternalSetFloatValue(struct con_var *this, float v) {
	if (v == this->fval) return;
	ClampValue(this, &v);
	float old = this->fval;
	this->fval = v; this->ival = (int)this->fval;
	if (!(this->base.flags & CON_NOPRINT)) {
		char tmp[32];
		snprintf(tmp, sizeof(tmp), "%f", this->fval);
		ChangeStringValue(this, tmp, old);
	}
}

static void VCALLCONV InternalSetIntValue(struct con_var *this, int v) {
	if (v == this->ival) return;
	float f = (float)v;
	if (ClampValue(this, &f)) v = (int)f;
	float old = this->fval;
	this->fval = f; this->ival = v;
	if (!(this->base.flags & CON_NOPRINT)) {
		char tmp[32];
		snprintf(tmp, sizeof(tmp), "%f", this->fval);
		ChangeStringValue(this, tmp, old);
	}
}

// Hack: IConVar things get this-adjusted pointers, we just reverse the offset
// to get the top pointer.
static void VCALLCONV SetValue_str_thunk(void *thisoff, const char *v) {
	struct con_var *this = mem_offset(thisoff,
			-offsetof(struct con_var, vtable_iconvar));
	((void (*VCALLCONV)(void *, const char *))(this->parent->base.vtable[
			vtidx_InternalSetValue]))(this, v);
}
static void VCALLCONV SetValue_f_thunk(void *thisoff, float v) {
	struct con_var *this = mem_offset(thisoff,
			-offsetof(struct con_var, vtable_iconvar));
	((void (*VCALLCONV)(void *, float))(this->parent->base.vtable[
			vtidx_InternalSetValue + 1]))(this, v);
}
static void VCALLCONV SetValue_i_thunk(void *thisoff, int v) {
	struct con_var *this = mem_offset(thisoff,
			-offsetof(struct con_var, vtable_iconvar));
	((void (*VCALLCONV)(void *, int))(this->parent->base.vtable[
			vtidx_InternalSetValue + 2]))(this, v);
}
static void VCALLCONV SetValue_colour_thunk(void *thisoff, struct con_colour v) {
	struct con_var *this = mem_offset(thisoff,
			-offsetof(struct con_var, vtable_iconvar));
	((void (*VCALLCONV)(void *, struct con_colour))(this->parent->base.vtable[
			vtidx_InternalSetValue + 3]))(this, v);
}

// more misc thunks, hopefully these just compile to a sub and a jmp
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
static int VCALLCONV GetSplitScreenPlayerSlot(void *thisoff) { return 0; }

// aand yet another Create nop
static void VCALLCONV Create_var(void *thisoff, const char *name,
		const char *defaultval, int flags, const char *helpstr, bool hasmin,
		float min, bool hasmax, float max, void *cb) {}

#ifdef _WIN32
#define NVDTOR 1
#else
#define NVDTOR 2 // Itanium ABI has 2 because I said so
#endif

// the first few members of ConCommandBase are the same between versions
void *_con_vtab_cmd[14 + NVDTOR] = {
	(void *)&dtor,
#ifndef _WIN32
	(void *)&dtor2,
#endif
	(void *)&IsCommand_cmd,
	(void *)&IsFlagSet_cmd,
	(void *)&AddFlags_cmd
};

// the engine does dynamic_casts on ConVar at some points so we have to fill out
// bare minimum rtti to prevent crashes. oh goody.
#ifdef _WIN32
DEF_MSVC_BASIC_RTTI(static, varrtti, _con_realvtab_var, "sst_ConVar")
#endif

void *_con_realvtab_var[20] = {
#ifdef _WIN32
	&varrtti,
#else
	// this, among many other things, will be totally different on linux
#endif
	(void *)&dtor,
#ifndef _WIN32
	(void *)&dtor2,
#endif
	(void *)&IsCommand_var,
	(void *)&IsFlagSet_var,
	(void *)&AddFlags_var
};

void *_con_vtab_iconvar[7] = {
#ifdef _WIN32
	0 // because of crazy overload vtable order we can't prefill *anything*
#else
	// colour is the last of the 4 on linux so we can at least prefill these 3
	(void *)&SetValue_str_thunk,
	(void *)&SetValue_f_thunk,
	(void *)&SetValue_i_thunk
#endif
};

static void fillvts(void) {
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
	vtidx_InternalSetValue = pv - _con_vtab_var;
	*pv++ = (void *)&InternalSetValue;
	*pv++ = (void *)&InternalSetFloatValue;
	*pv++ = (void *)&InternalSetIntValue;
	if (GAMETYPE_MATCHES(L4D2) || GAMETYPE_MATCHES(Portal2)) { // ugh, annoying
		// This is InternalSetColorValue, but that's basically the same thing,
		// when you think about it.
		*pv++ = (void *)&InternalSetIntValue;
	}
	*pv++ = (void *)&ClampValue;;
	*pv++ = (void *)&ChangeStringValue;
	*pv++ = (void *)&Create_var;
	if (GAMETYPE_MATCHES(L4D2) || GAMETYPE_MATCHES(Portal2)) {
		*pi++ = (void *)&SetValue_colour_thunk;
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

bool con_init(void *(*f)(const char *, int *), int plugin_ver) {
	int ifacever; // for error messages
	if (_con_iface = f("VEngineCvar007", 0)) {
		// GENIUS HACK (BUT STILL BAD): Portal 2 has everything in ICvar shifted
		// down 3 places due to the extra stuff in IAppSystem. This means that
		// if we look up the Portal 2-specific cvar using FindCommandBase, it
		// *actually* calls the const-overloaded FindVar on other branches,
		// which just happens to still work fine. From there, we can figure out
		// the actual ABI to use to avoid spectacular crashes.
		if (VCALL(_con_iface, FindCommandBase_p2, "portal2_square_portals")) {
			_con_colourmsgf = VFUNC(_con_iface, ConsoleColorPrintf_p2);
			dllid = VCALL0(_con_iface, AllocateDLLIdentifier_p2);
			_gametype_tag |= _gametype_tag_Portal2;
			fillvts();
			regcmds(VFUNC(_con_iface, RegisterConCommand_p2));
			return true;
		}
		if (VCALL(_con_iface, FindCommand, "l4d2_snd_adrenaline")) {
			_con_colourmsgf = VFUNC(_con_iface, ConsoleColorPrintf_l4d);
			dllid = VCALL0(_con_iface, AllocateDLLIdentifier);
			_gametype_tag |= _gametype_tag_L4D2;
			fillvts();
			regcmds(VFUNC(_con_iface, RegisterConCommand));
			return true;
		}
		if (VCALL(_con_iface, FindVar, "z_difficulty")) {
			_con_colourmsgf = VFUNC(_con_iface, ConsoleColorPrintf_l4d);
			dllid = VCALL0(_con_iface, AllocateDLLIdentifier);
			_gametype_tag |= _gametype_tag_L4D1;
			fillvts(); // XXX: is this all kinda dupey? maybe rearrange one day.
			regcmds(VFUNC(_con_iface, RegisterConCommand));
			return true;
		}
		con_warn("sst: error: game \"%s\" is unsupported (using "
					"VEngineCvar007)\n", gameinfo_title);
		ifacever = 7;
		goto e;
	}
	if (_con_iface = f("VEngineCvar004", 0)) {
		// TODO(compat): are there any cases where 004 is incompatible? could
		// this crash? find out!
		_con_colourmsgf = VFUNC(_con_iface, ConsoleColorPrintf_004);
		dllid = VCALL0(_con_iface, AllocateDLLIdentifier);
		// even more spaghetti! we need the plugin interface version to
		// accurately distinguish 2007/2013 branches
		if (plugin_ver == 3) _gametype_tag |= _gametype_tag_2013;
		else _gametype_tag |= _gametype_tag_OrangeBox;
		fillvts();
		regcmds(VFUNC(_con_iface, RegisterConCommand));
		return true;
	}
	if (f("VEngineCvar003", 0)) {
		ifacever = 3;
		goto warnoe;
	}
	if (f("VEngineCvar002", 0)) {
		// I don't suppose there's anything below 002 worth caring about? Shrug.
		ifacever = 2;
warnoe:	con_warn("sst: error: old engine console support is not implemented\n");
		goto e;
	}
	con_warn("sst: error: couldn't find a supported console interface\n");
	ifacever = -1; // meh
e:	con_msg("\n\n");
	con_msg("-- Please include ALL of the following if asking for help:\n");
	con_msg("--   plugin:     " LONGNAME " v" VERSION "\n");
	con_msg("--   game:       %s\n", gameinfo_title);
	con_msg("--   interfaces: %d/%d\n", plugin_ver, ifacever);
	con_msg("\n\n");
	return false;
}

void con_disconnect(void) {
	if (GAMETYPE_MATCHES(Portal2)) {
		VCALL(_con_iface, UnregisterConCommands_p2, dllid);
	}
	else {
		VCALL(_con_iface, UnregisterConCommands, dllid);
	}
	freevars();
}

struct con_var *con_findvar(const char *name) {
	if (GAMETYPE_MATCHES(Portal2)) {
		return VCALL(_con_iface, FindVar_p2, name);
	}
	else {
		return VCALL(_con_iface, FindVar, name);
	}
}

struct con_cmd *con_findcmd(const char *name) {
	if (GAMETYPE_MATCHES(Portal2)) {
		return VCALL(_con_iface, FindCommand_p2, name);
	}
	else {
		return VCALL(_con_iface, FindCommand, name);
	}
}

#define GETTER(T, N, M) \
	T N(const struct con_var *v) { return v->parent->M; }
GETTER(const char *, con_getvarstr, strval)
GETTER(float, con_getvarf, fval)
GETTER(int, con_getvari, ival)
#undef GETTER

#define SETTER(T, I, N) \
	void N(struct con_var *v, T x) { \
		((void (*VCALLCONV)(struct con_var *, T))(v->vtable_iconvar[I]))(v, x); \
	}
// vtable indexes for str/int/float are consistently at the start, hooray.
// unfortunately the windows overload ordering meme still applies...
#ifdef _WIN32
SETTER(const char *, 2, con_setvarstr)
SETTER(float, 1, con_setvarf)
SETTER(int, 0, con_setvari)
#else
SETTER(const char *, 0, con_setvarstr)
SETTER(float, 1, con_setvarf)
SETTER(int, 2, con_setvari)
#endif
#undef SETTER

con_cmdcb con_getcmdcb(const struct con_cmd *cmd) {
	if (cmd->use_newcmdiface || !cmd->use_newcb) return 0;
	return cmd->cb;
}

con_cmdcbv1 con_getcmdcbv1(const struct con_cmd *cmd) {
	if (cmd->use_newcmdiface || cmd->use_newcb) return 0;
	return cmd->cb_v1;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
