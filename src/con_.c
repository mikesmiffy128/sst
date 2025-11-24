/* THIS FILE SHOULD BE CALLED `con.c` BUT WINDOWS IS STUPID */
/*
 * Copyright © Michael Smith <mikesmiffy128@gmail.com>
 * Copyright © Hayden K <imaciidz@gmail.com>
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
#include <string.h>

#include "abi.h"
#include "chunklets/x86.h"
#include "con_.h"
#include "engineapi.h" // for factories and rgba - XXX: is this a bit circular?
#include "errmsg.h"
#include "extmalloc.h"
#include "gamedata.h"
#include "gametype.h"
#include "langext.h"
#include "mem.h"
#include "os.h"
#include "vcall.h"
#include "version.h"
#include "x86util.h"

/******************************************************************************\
 * Have you ever noticed that when someone comments "here be dragons" there's *
 * no actual dragons? Turns out, that's because the dragons all migrated over *
 * here, so that they could build multiple inheritance vtables in C, by hand. *
 *                                                                            *
 * Also there's self-modifying code now.                                      *
 *                                                                            *
 * Don't get set on fire.                                                     *
\******************************************************************************/

static int dllid; // from AllocateDLLIdentifier(), lets us unregister in bulk
int con_cmdclient;

DECL_VFUNC_DYN(struct ICvar, int, AllocateDLLIdentifier)
DECL_VFUNC_DYN(struct ICvar, void, RegisterConCommand, /*ConCommandBase*/ void *)
DECL_VFUNC_DYN(struct ICvar, void, UnregisterConCommands, int)
DECL_VFUNC_DYN(struct ICvar, struct con_var *, FindVar, const char *)
//DECL_VFUNC(struct ICvar, const struct con_var *, FindVar_const, 13, const char *)
DECL_VFUNC_DYN(struct ICvar, struct con_cmd *, FindCommand, const char *)
DECL_VFUNC_DYN(struct ICvar, void, CallGlobalChangeCallbacks, struct con_var *,
		const char *, float)

#ifdef _WIN32
DECL_VFUNC_DYN(struct ICvar, void, CallGlobalChangeCallbacks_OE,
		struct con_var *, const char *)

// other OE stuff. TODO(compat): should this be in gamedata? fine for now?
DECL_VFUNC(struct ICvar, struct con_cmdbase *, GetCommands_OE, 9)
DECL_VFUNC(struct VEngineClient, void *, Cmd_Argv, 32)
#endif

// bootstrap things for con_detect(), not used after that
DECL_VFUNC(struct ICvar, void *, FindCommandBase_p2, 13, const char *)
DECL_VFUNC(struct ICvar, void *, FindCommand_nonp2, 14, const char *)
DECL_VFUNC(struct ICvar, void *, FindVar_nonp2, 12, const char *)
#ifdef _WIN32
DECL_VFUNC(struct ICvar, void *, FindVar_OE, 7, const char *)
#endif

static struct ICvar *coniface;
static void *colourmsgf;

#ifdef _WIN32
#pragma section("selfmod", execute)
__attribute((used, section("selfmod"), noinline))
#endif
asm_only void _con_colourmsg(void *dummy, const struct rgba *c,
		const char *fmt, ...) {
	// NE: ConsoleColorPrintf is virtual, so the dummy param is a carve-out for
	// `this` (which is coniface).
	__asm volatile (
		"mov eax, %0\n"
		"mov [esp + 4], eax\n" // put coniface in the empty stack slot
		"jmp dword ptr %1\n" // jump to the real function
		:
		: "m" (coniface), "m" (colourmsgf)
		: "eax", "memory"
	);
}

#ifdef _WIN32
// this function is defined as data because we'll be using it to self-modify the
// main _con_colourmsg function!
__attribute((used, section("rdata")))
asm_only static void _con_colourmsg_OE(void *dummy, const struct rgba *c,
		const char *fmt, ...) {
	// OE: it's a global function, with no this param, so we have to fix up the
	// stack a bit. This will be less efficient than NE, but that seems like a
	// reasonable tradeoff considering most games are NE. We could in theory
	// self-modify every single call site to avoid the fixups but haha are you
	// out of your mind we're not doing that.
	__asm volatile (
		"pop ebx\n" // pop return address, store in callee-save (*see header!*)
		"add esp, 4\n" // pop the dummy stack slot, it's only useful for NE
		"call dword ptr %1\n" // jump to the real function
		"sub esp, 4\n" // pad the stack back out for the caller
		"jmp ebx\n" // return to saved address
		:
		: "m" (coniface), "m" (colourmsgf)
		: "eax", "ebx", "memory"
	);
}
#define SELFMOD_LEN 15 // above instructions assemble to this many bytes!

static bool selfmod() {
	if (!os_mprot((void *)_con_colourmsg, SELFMOD_LEN, PAGE_EXECUTE_READWRITE)) {
		errmsg_errorsys("couldn't make memory writable");
		return false;
	}
	memcpy((void *)&_con_colourmsg, (void *)&_con_colourmsg_OE, SELFMOD_LEN);
	if (!os_mprot((void *)_con_colourmsg, SELFMOD_LEN, PAGE_EXECUTE_READ)) {
		errmsg_warnsys("couldn't restore self-modified page to read-only");
	}
	return true;
}
#endif

static void VCALLCONV dtor(void *_) {} // we don't use constructors/destructors

static bool VCALLCONV IsCommand_cmd(void *this) { return true; }
static bool VCALLCONV IsCommand_var(void *this) { return false; }

static bool VCALLCONV IsFlagSet(struct con_cmdbase *this, int flags) {
	return !!(this->flags & flags);
}
static void VCALLCONV AddFlags(struct con_cmdbase *this, int flags) {
	this->flags |= flags;
}
static void VCALLCONV RemoveFlags(struct con_cmdbase *this, int flags) {
	this->flags &= ~flags;
}
static int VCALLCONV GetFlags(struct con_cmdbase *this) {
	return this->flags;
}
static const char *VCALLCONV GetName(struct con_cmdbase *this) {
	return this->name;
}
static const char *VCALLCONV GetHelpText(struct con_cmdbase *this) {
	if_cold (this->flags & (CON_INIT_HIDDEN | _CON_NE_HIDDEN)) {
		return this->help - 18; // see _DEF_* macros in con_.h
	}
	return this->help;
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

static bool ClampValue_common(struct con_var_common *this, float *f) {
	if (this->hasmin && this->minval > *f) { *f = this->minval; return true; }
	if (this->hasmax && this->maxval < *f) { *f = this->maxval; return true; }
	return false;
}
static bool VCALLCONV ClampValue(struct con_var *this, float *f) {
	return ClampValue_common(&this->v2, f);
}
#ifdef _WIN32
static bool VCALLCONV ClampValue_OE(struct con_var *this, float *f) {
	return ClampValue_common(&this->v1, f);
}
#endif

// global argc/argv. also OE only. extern for use in sst.c plugin_unload hook
// as well as in DEF_CCMD_COMPAT_HOOK
int *_con_argc;
const char *(*_con_argv)[80];

static bool find_argcargv() {
	const uchar *insns = (const uchar *)VFUNC(engclient, Cmd_Argv);
	for (const uchar *p = insns; p - insns < 32;) {
		if (p[0] == X86_CALL) { insns = p + 5 + mem_loads32(p + 1); goto _1; }
		NEXT_INSN(p, "global Cmd_Argv function");
	}
	return false;
_1:	for (const uchar *p = insns; p - insns < 32;) {
		if (p[0] == X86_CMPRMW && p[1] == X86_MODRM(0, 0, 5)) {
			_con_argc = mem_loadptr(p + 2);
		}
		else if (p[0] == X86_MOVRMW && p[1] == X86_MODRM(0, 0, 4) &&
				p[2] == X86_TESTMRW) {
			_con_argv = mem_loadptr(p + 3);
		}
		if (_con_argc && _con_argv) return true;
		NEXT_INSN(p, "global argc and argv variables");
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
#ifdef _WIN32
void VCALLCONV Dispatch_OE(struct con_cmd *this) {
	this->cb(*_con_argc, *_con_argv);
}
#endif

static void ChangeStringValue_common(struct con_var *this,
		struct con_var_common *common, char *old, const char *s) {
	memcpy(old, common->strval, common->strlen);
	int len = strlen(s) + 1;
	if (len > common->strlen) {
		common->strval = extrealloc(common->strval, len);
		common->strlen = len;
	}
	memcpy(common->strval, s, len);
	// callbacks don't matter as far as ABI compat goes (and thank goodness
	// because e.g. portal2 randomly adds a *list* of callbacks!?). however we
	// do need callbacks for at least one feature, so do our own minimal thing
	if (this->cb) this->cb(this);
}
static void VCALLCONV ChangeStringValue(struct con_var *this, const char *s,
		float oldf) {
	char *old = alloca(this->v2.strlen);
	ChangeStringValue_common(this, &this->v2, old, s);
	CallGlobalChangeCallbacks(coniface, this, old, oldf);
}
#ifdef _WIN32
static void VCALLCONV ChangeStringValue_OE(struct con_var *this, const char *s) {
	char *old = alloca(this->v1.strlen);
	ChangeStringValue_common(this, &this->v1, old, s);
	CallGlobalChangeCallbacks_OE(coniface, this, old);
}
#endif

// NOTE: these Internal* functions are virtual in the engine, but nowadays we
// just call them directly since they're private to us. We still put them in the
// vtable just in case (see below), though arguably nothing in the engine
// *should* be calling these internal things anyway.

static void InternalSetValue_common(struct con_var *this,
		struct con_var_common *common, const char *v) {
	float newf = atof(v);
	char tmp[32];
	if (ClampValue_common(common, &newf)) {
		snprintf(tmp, sizeof(tmp), "%f", newf);
		v = tmp;
	}
	common->fval = newf;
	common->ival = (int)newf;
}
static void VCALLCONV InternalSetValue(struct con_var *this, const char *v) {
	float oldf = this->v2.fval;
	InternalSetValue_common(this, &this->v2, v);
	if (!(this->base.flags & CON_NOPRINT)) ChangeStringValue(this, v, oldf);
}
#ifdef _WIN32
static void VCALLCONV InternalSetValue_OE(struct con_var *this, const char *v) {
	InternalSetValue_common(this, &this->v1, v);
	if (!(this->base.flags & CON_NOPRINT)) ChangeStringValue_OE(this, v);
}
#endif

static void VCALLCONV InternalSetFloatValue(struct con_var *this, float v) {
	if (v == this->v2.fval) return;
	float old = this->v2.fval;
	ClampValue_common(&this->v2, &v);
	this->v2.fval = v; this->v2.ival = (int)v;
	if (!(this->base.flags & CON_NOPRINT)) {
		char tmp[32];
		snprintf(tmp, sizeof(tmp), "%f", this->v2.fval);
		ChangeStringValue(this, tmp, old);
	}
}
#ifdef _WIN32
static void VCALLCONV InternalSetFloatValue_OE(struct con_var *this, float v) {
	if (v == this->v1.fval) return;
	ClampValue_common(&this->v1, &v);
	this->v1.fval = v; this->v1.ival = (int)v;
	if (!(this->base.flags & CON_NOPRINT)) {
		char tmp[32];
		snprintf(tmp, sizeof(tmp), "%f", this->v1.fval);
		ChangeStringValue_OE(this, tmp);
	}
}
#endif

static void InternalSetIntValue_impl(struct con_var *this,
		struct con_var_common *common, int v) {
	float f = (float)v;
	if (ClampValue_common(common, &f)) v = (int)f;
	common->fval = f; common->ival = v;
}
static void VCALLCONV InternalSetIntValue(struct con_var *this, int v) {
	if (v == this->v2.ival) return;
	float old = this->v2.fval;
	InternalSetIntValue_impl(this, &this->v2, v);
	if (!(this->base.flags & CON_NOPRINT)) {
		char tmp[32];
		snprintf(tmp, sizeof(tmp), "%f", this->v2.fval);
		ChangeStringValue(this, tmp, old);
	}
}
#ifdef _WIN32
static void VCALLCONV InternalSetIntValue_OE(struct con_var *this, int v) {
	if (v == this->v1.ival) return;
	InternalSetIntValue_impl(this, &this->v1, v);
	if (!(this->base.flags & CON_NOPRINT)) {
		char tmp[32];
		snprintf(tmp, sizeof(tmp), "%f", this->v1.fval);
		ChangeStringValue_OE(this, tmp);
	}
}
#endif

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

// more misc thunks for IConVar, hopefully these just compile to a lea and a jmp
static const char *VCALLCONV GetName_thunk(void *thisoff) {
	struct con_var *this = mem_offset(thisoff,
			-offsetof(struct con_var, vtable_iconvar));
	return GetName(&this->base);
}
static bool VCALLCONV IsFlagSet_thunk(void *thisoff, int flags) {
	struct con_var *this = mem_offset(thisoff,
			-offsetof(struct con_var, vtable_iconvar));
	return IsFlagSet(&this->base, flags);
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
	(void *)&IsFlagSet,
	(void *)&AddFlags
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
	(void *)&IsFlagSet,
	(void *)&AddFlags
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

#ifdef _WIN32
static int off_cvar_common = offsetof(struct con_var, v2);
#else
enum { off_cvar_common = offsetof(struct con_var, v2) };
#endif

struct con_var_common *con_getvarcommon(const struct con_var *v) {
	return mem_offset(v, off_cvar_common);
}

static inline void fudgeflags(struct con_cmdbase *b) {
	if_hot (!GAMETYPE_MATCHES(OE)) if (b->flags & CON_INIT_HIDDEN) {
		b->flags = (b->flags & ~CON_INIT_HIDDEN) | _CON_NE_HIDDEN;
	}
}

void con_regvar(struct con_var *v) {
	fudgeflags(&v->base);
	struct con_var_common *c = con_getvarcommon(v);
	c->strval = extmalloc(c->strlen); // note: _DEF_CVAR() sets strlen member
	memcpy(c->strval, c->defaultval, c->strlen);
	RegisterConCommand(coniface, v);
}

void con_regcmd(struct con_cmd *c) {
	fudgeflags(&c->base);
	if_hot (!GAMETYPE_MATCHES(OE)) if (c->base.flags & CON_INIT_HIDDEN) {
		c->base.flags = (c->base.flags & ~CON_INIT_HIDDEN) | _CON_NE_HIDDEN;
	}
	RegisterConCommand(coniface, c);
}

void con_hide(struct con_cmdbase *b) {
	if_hot (!GAMETYPE_MATCHES(OE)) b->flags |= _CON_NE_HIDDEN;
}
void con_unhide(struct con_cmdbase *b) {
	if_hot (!GAMETYPE_MATCHES(OE)) b->flags &= ~_CON_NE_HIDDEN;
}

// XXX: these should use vcall/gamedata stuff as they're only used for the
// setter API after everything is brought up. however that will require some
// kind of windows/linux conditionals in the gamedata system! this solution is
// just hacked in for now to get things working because it was broken before...
#ifdef _WIN32
static int vtidx_SetValue_str = 2, vtidx_SetValue_f = 1, vtidx_SetValue_i = 0;
static int off_setter_vtable = offsetof(struct con_var, vtable_iconvar);
#else
enum { vtidx_SetValue_str = 0, vtidx_SetValue_f = 1, vtidx_SetValue_i = 2 };
#endif

#ifdef _WIN32
struct con_cmdbase **linkedlist = 0; // indirect command list, OE only!

static bool find_linkedlist(const uchar *insns) {
	// note: it's a jmp in the disasm I've seen but a call seems plausible too
	if (insns[0] != X86_JMPIW && *insns != X86_CALL) return false;
	insns += 5 + mem_loads32(insns + 1); // follow the call
	if (insns[0] != X86_MOVEAXII || insns[5] != X86_RET) return false;
	linkedlist = mem_loadptr(insns + 1);
	return true;
}

static bool find_Con_ColorPrintf() {
	typedef void *(*GetSpewOutputFunc_func)();
	void *tier0 = os_dlhandle(L"tier0.dll");
	if_cold (!tier0) {
		errmsg_errorsys("couldn't get tier0.dll handle");
		return false;
	}
	GetSpewOutputFunc_func GetSpewOutputFunc = (GetSpewOutputFunc_func)os_dlsym(
			tier0, "GetSpewOutputFunc");
	if_cold (!GetSpewOutputFunc) {
		errmsg_errorx("couldn't find GetSpewOutputFunc symbol");
		return false;
	}
	uchar *insns = (uchar *)GetSpewOutputFunc();
	for (uchar *p = insns; p - insns < 320;) {
		if (p[0] == X86_PUSHECX && p[1] == X86_PUSHIW && p[6] == X86_CALL &&
				p[11] == X86_ALUMI8S && p[12] == X86_MODRM(3, 0, 4)) {
			colourmsgf = p + 11 + mem_loads32(p + 7);
			return true;
		}
		NEXT_INSN(p, "Con_ColorPrintf function");
	}
	return false;
}
#endif

static void helpuserhelpus(int pluginver, char ifaceverchar) {
	con_msg("\n");
	con_msg("-- Please include ALL of the following if asking for help:\n");
	con_msg("--   plugin:     " LONGNAME " v" VERSION "\n");
	con_msg("--   interfaces: %d/%c\n", pluginver, ifaceverchar);
	con_msg("\n");
}

// note: for now at least, not using errmsg_*() macros here because it doesn't
// really make sense for these messages to be coming from "con"

static void badver() {
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
			if (FindVar_nonp2(coniface, "sv_zombie_touch_trigger_delay")) {
				_gametype_tag |= _gametype_tag_L4D2_2125plus;
			}
			if (FindVar_nonp2(coniface, "director_cs_weapon_spawn_chance")) {
				_gametype_tag |= _gametype_tag_TheLastStand;
			}
			return true;
		}
		if (FindVar_nonp2(coniface, "z_difficulty")) {
			_gametype_tag |= _gametype_tag_L4D1;
			// Crash Course update
			if (FindCommand_nonp2(coniface, "director_log_scavenge_items")) {
				_gametype_tag |= _gametype_tag_L4D1_1015plus;
				// seems there was some code shuffling in the Mac update (1022).
				// this update came out like 2-3 weeks after The Sacrifice
				if (con_findvar("tank_stasis_time_suicide")) {
					_gametype_tag |= _gametype_tag_L4D1_1022plus;
				}
			}
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
		// detect Portal 1 versions while we're here...
		if (FindCommand_nonp2(coniface, "upgrade_portalgun")) {
			_gametype_tag |= _gametype_tag_Portal1;
			if (!FindVar_nonp2(coniface, "tf_escort_score_rate")) {
				_gametype_tag |= _gametype_tag_Portal1_3420;
			}
		}
		else if (FindCommand_nonp2(coniface, "phys_swap")) {
			_gametype_tag |= _gametype_tag_HL2series;
		}
		return true;
	}
	if (coniface = factory_engine("VEngineCvar003", 0)) {
#ifdef _WIN32 // there's no OE on linux!
		_gametype_tag |= _gametype_tag_OE;
		// for deletion/unlinking on unload, we need an indirect linked list
		// pointer. calling GetCommands gives us a direct pointer. so we have to
		// actually pull out the indirect pointer from the actual asm lol.
		if (!find_linkedlist((uchar *)VFUNC(coniface, GetCommands_OE))) {
			errmsg_errorx("couldn't find command list pointer");
			return false;
		}
		if (!find_argcargv()) return false;
		if (!find_Con_ColorPrintf()) return false;
		if (!selfmod()) return false;
		// NOTE: the default static struct layout is for NE; immediately after
		// engineapi init finishes, the generated glue code will shunt
		// everything along for OE if required, in shuntvars(). since all the
		// gluegen code is currently hooked up in sst.c this is a little bit
		// annoyingly removed from here. not sure how to do it better, sorry.
		off_cvar_common = offsetof(struct con_var, v1);
		if (FindVar_OE(coniface, "mm_ai_facehugger_enablehugeattack")) {
			_gametype_tag |= _gametype_tag_DMoMM;
		}
		return true;
#else
		badver();
		helpuserhelpus(pluginver, '2');
#endif
	}
	// I don't suppose there's anything below 002 worth caring about? Shrug.
	if (factory_engine("VEngineCvar002", 0)) {
		badver();
		helpuserhelpus(pluginver, '2');
		return false;
	}
	con_warn("sst: error: couldn't find a supported console interface\n");
	helpuserhelpus(pluginver, '?');
	return false;
}

static int *find_host_initialized() {
	const uchar *insns = colourmsgf;
	for (const uchar *p = insns; p - insns < 32;) {
		// cmp byte ptr [<pointer>], <value>
		if (p[0] == X86_ALUMI8 && p[1] == X86_MODRM(0, 7, 5)) {
			return mem_loadptr(p + 2);
		}
		NEXT_INSN(p, "host_initialized variable");
	}
	return 0;
}

void con_init() {
	if (GAMETYPE_MATCHES(OE)) {
		// if we're autoloaded, we have to set host_initialized early or colour
		// log output (including error output!) won't be visible, for some inane
		// reason. *as far as we know* this doesn't have any bad side effects.
		// note: if this fails, too bad. not like we can log a warning.
		int *host_initialized = find_host_initialized();
		if (host_initialized && *host_initialized == 0) *host_initialized = 1;
	}
	else {
		colourmsgf = coniface->vtable[vtidx_ConsoleColorPrintf];
		dllid = AllocateDLLIdentifier(coniface);
	}

	void **pc = _con_vtab_cmd + 3 + NVDTOR, **pv = _con_vtab_var + 3 + NVDTOR,
#ifdef _WIN32
			**pi = _con_vtab_iconvar;
#else
			**pi = _con_vtab_iconvar + 3;
#endif
	if (GAMETYPE_MATCHES(L4Dbased)) { // 007 base
		*pc++ = (void *)&RemoveFlags;
		*pc++ = (void *)&GetFlags;
		*pv++ = (void *)&RemoveFlags;
		*pv++ = (void *)&GetFlags;
	}
	// base stuff in cmd
	*pc++ = (void *)&GetName;
	*pc++ = (void *)&GetHelpText;
	*pc++ = (void *)&IsRegistered;
	if (!GAMETYPE_MATCHES(OE)) *pc++ = (void *)&GetDLLIdentifier;
	*pc++ = (void *)&Create_base;
	*pc++ = (void *)&Init;
	// cmd-specific
	*pc++ = (void *)&AutoCompleteSuggest;
	*pc++ = (void *)&CanAutoComplete;
	if (GAMETYPE_MATCHES(OE)) {
#ifdef _WIN32 // function only defined in windows
		*pc++ = (void *)&Dispatch_OE;
#endif
	}
	else {
		*pc++ = (void *)&Dispatch;
	}
	// base stuff in var
	*pv++ = (void *)&GetName;
	*pv++ = (void *)&GetHelpText;
	*pv++ = (void *)&IsRegistered;
	if (!GAMETYPE_MATCHES(OE)) *pv++ = (void *)&GetDLLIdentifier;
	*pv++ = (void *)&Create_base;
	*pv++ = (void *)&Init;
	// var-specific
	if (GAMETYPE_MATCHES(OE)) {
#ifdef _WIN32
		// these there are for the SetValue overloads but we effectively inline
		// them by putting in pointers to call the Internal ones directly. this
		// specifically works now that we've opted not to bother with the parent
		// pointer stuff, otherwise we'd still need wrappers here.
		vtidx_SetValue_i = pv - _con_vtab_var;
		*pv++ = (void *)&InternalSetIntValue_OE;
		vtidx_SetValue_f = pv - _con_vtab_var;
		*pv++ = (void *)&InternalSetFloatValue_OE;
		vtidx_SetValue_str = pv - _con_vtab_var;
		*pv++ = (void *)&InternalSetValue_OE;
		off_setter_vtable = 0; // setters should use the single vtable (below)
		*pv++ = (void *)&InternalSetValue_OE;
		*pv++ = (void *)&InternalSetFloatValue_OE;
		*pv++ = (void *)&InternalSetIntValue_OE;
		*pv++ = (void *)&ClampValue_OE;
		*pv++ = (void *)&ChangeStringValue_OE;
#endif
	}
	else {
		*pv++ = (void *)&InternalSetValue;
		*pv++ = (void *)&InternalSetFloatValue;
		*pv++ = (void *)&InternalSetIntValue;
		if (GAMETYPE_MATCHES(L4D2x) || GAMETYPE_MATCHES(Portal2)) { // ugh.
			// InternalSetColorValue, exact same machine instructions as for int
			*pv++ = (void *)&InternalSetIntValue;
		}
		*pv++ = (void *)&ClampValue;
		*pv++ = (void *)&ChangeStringValue;
	}
	*pv++ = (void *)&Create_var;
	if (GAMETYPE_MATCHES(OE)) return; // we can just skip the rest on OE!
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
	// see above: these aren't prefilled due to the reverse order
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

void con_disconnect() {
#ifdef _WIN32
	if (linkedlist) {
		// there's no DLL identifier system in OE so we have to manually unlink
		// our commands and variables from the global list.
		for (struct con_cmdbase **pp = linkedlist; *pp; ) {
			struct con_cmdbase **next = &(*pp)->next;
			// HACK: easiest way to do this is by vtable. dumb, but whatever!
			const struct con_cmdbase *p = *pp;
			if (p->vtable == _con_vtab_cmd || p->vtable == _con_vtab_var) {
				*pp = *next;
			}
			else {
				pp = next;
			}
		}
		return;
	}
#endif
	UnregisterConCommands(coniface, dllid);
}

struct con_var *con_findvar(const char *name) {
	return FindVar(coniface, name);
}

struct con_cmd *con_findcmd(const char *name) {
#ifdef _WIN32
	if (linkedlist) {
		// OE has a FindVar but no FindCommand. interesting oversight...
		for (struct con_cmdbase *p = *linkedlist; p; p = p->next) {
			if (!_stricmp(name, p->name)) {
				// FIXME: this'll get variables too! make the appropriate vcall!
				return (struct con_cmd *)p;
			}
		}
		return 0;
	}
#endif
	return FindCommand(coniface, name);
}

// NOTE: getters here still go through the parent pointer although we stopped
// doing that internally, just in case we run into parented cvars in the actual
// engine. a little less efficient, but safest and simplest for now.
#define GETTER(T, N, M) \
	T N(const struct con_var *v) { \
		return con_getvarcommon(con_getvarcommon(v)->parent)->M; \
	}
GETTER(const char *, con_getvarstr, strval)
GETTER(float, con_getvarf, fval)
GETTER(int, con_getvari, ival)
#undef GETTER

#define SETTER(T, I, N) \
	void N(struct con_var *v, T x) { \
		void (***VCALLCONV vtp)(void *, T) = mem_offset(v, off_setter_vtable); \
		(*vtp)[I](vtp, x); \
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
