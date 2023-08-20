/* THIS FILE SHOULD BE CALLED `con.h` BUT WINDOWS IS STUPID */
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

#ifndef INC_CON_H
#define INC_CON_H

#include "intdefs.h"

#if defined(__GNUC__) || defined(__clang__)
#define _CON_PRINTF(x, y) __attribute__((format(printf, (x), (y))))
#else
#define _CON_PRINTF(x, y)
#endif

#define CON_CMD_MAX_ARGC 64
#define CON_CMD_MAX_LENGTH 512

/* arguments to a console command, parsed by the engine (SDK name: CCommand) */
struct con_cmdargs {
	int argc;
	int argv0size;
	char argsbuf[CON_CMD_MAX_LENGTH];
	char argvbuf[CON_CMD_MAX_LENGTH];
	const char *argv[CON_CMD_MAX_ARGC];
};

#define CON_CMD_MAXCOMPLETE 64
#define CON_CMD_MAXCOMPLLEN 64

/* ConVar/ConCommand flag bits - undocumented ones are probably not useful... */
enum {
	CON_UNREG = 1,
	CON_DEVONLY = 1 << 1,		/* hide unless developer 1 is set */
	CON_SERVERSIDE = 1 << 2,	/* set con_cmdclient and run on server side */
	CON_CLIENTDLL = 1 << 3,
	CON_HIDDEN = 1 << 4,		/* hide completely, often useful to remove! */
	CON_PROTECTED = 1 << 5,		/* don't send to clients (example: password) */
	CON_SPONLY = 1 << 6,
	CON_ARCHIVE = 1 << 7,		/* save in config - plugin would need a VDF! */
	CON_NOTIFY = 1 << 8,		/* announce changes in game chat */
	CON_USERINFO = 1 << 9,
	CON_PRINTABLE = 1 << 10,	/* do not allow non-printable values */
	CON_UNLOGGED = 1 << 11,
	CON_NOPRINT = 1 << 12,		/* do not attempt to print, contains junk! */
	CON_REPLICATE = 1 << 13,	/* client will use server's value */
	CON_CHEAT = 1 << 14,		/* require sv_cheats 1 to change from default */
	CON_DEMO = 1 << 16,			/* record value at the start of a demo */
	CON_NORECORD = 1 << 17,		/* don't record the command to a demo, ever */
	CON_NOTCONN = 1 << 22,		/* cannot be changed while in-game */
	CON_SRVEXEC = 1 << 28,		/* server can make clients run the command */
	CON_NOSRVQUERY = 1 << 29,	/* server cannot query the clientside value */
	CON_CCMDEXEC = 1 << 30		/* ClientCmd() function may run the command */
};

/* A callback function invoked to execute a command. */
typedef void (*con_cmdcb)(const struct con_cmdargs *cmd);

/* Obsolete callback; not used by SST, but might still exist in the engine. */
typedef void (*con_cmdcbv1)(void);

/*
 * This is an autocompletion callback for suggesting arguments to a command.
 * XXX: Autocompletion isn't really totally figured out or implemented yet.
 */
typedef int (*con_complcb)(const char *part,
		char cmds[CON_CMD_MAXCOMPLETE][CON_CMD_MAXCOMPLLEN]);

/* These are called on plugin load/unload. They should not be used elsewhere. */
bool con_detect(int pluginver);
void con_init(void);
void con_disconnect(void);

/*
 * These types *pretty much* match those in the engine. Their fields can be
 * accessed and played with if you know what you're doing!
 */

struct con_cmdbase { // ConCommandBase in engine
	void **vtable;
	struct con_cmdbase *next;
	bool registered;
	const char *name;
	const char *help;
	uint flags;
};

struct con_cmd { // ConCommand in engine
	struct con_cmdbase base;
	union {
		con_cmdcbv1 cb_v1;
		con_cmdcb cb;
		/*ICommandCallback*/ void *cb_iface; // does source even use this?
	};
	union {
		con_complcb complcb;
		/*ICommandCompletionCallback*/ void *complcb_iface; // ...or this?
	};
	bool has_complcb : 1, use_newcb : 1, use_newcmdiface : 1;
};

struct con_var { // ConVar in engine
	struct con_cmdbase base;
	void **vtable_iconvar; // IConVar in engine (pure virtual)
	struct con_var *parent;
	const char *defaultval;
	char *strval;
	uint strlen;
	float fval;
	int ival;
	bool hasmin;
	// bool hasmax; // better packing here, would break engine ABI
	float minval;
	bool hasmax; // just sticking to sdk position
	float maxval;
	/*
	 * Our quickly-chucked-in optional callback - doesn't match the engine!!
	 * Also has to be manually set in code, although that's probably fine anyway
	 * as it's common to only want a cvar to do something if the feature
	 * succesfully init-ed.
	 */
	void (*cb)(struct con_var *this);
};

/* The change callback used in most branches of Source. Takes an IConVar :) */
typedef void (*con_varcb)(void *v, const char *, float);

/*
 * These functions get and set the values of console variables in a
 * neatly-abstracted manner. Note: cvar values are always strings internally -
 * numerical values are just interpretations of the underlying value.
 */
struct con_var *con_findvar(const char *name);
struct con_cmd *con_findcmd(const char *name);
const char *con_getvarstr(const struct con_var *v);
float con_getvarf(const struct con_var *v);
int con_getvari(const struct con_var *v);
void con_setvarstr(struct con_var *v, const char *s);
void con_setvarf(struct con_var *v, float f);
void con_setvari(struct con_var *v, int i);

/*
 * These functions grab the callback function from an existing command, allowing
 * it to be called directly or further dug into for convenient research.
 *
 * They perform sanity checks to ensure that the command implements the type of
 * callback being requested. If this is already known, consider just grabbing
 * the member directly to avoid the small amount of unnecessary work.
 */
con_cmdcb con_getcmdcb(const struct con_cmd *cmd);
con_cmdcbv1 con_getcmdcbv1(const struct con_cmd *cmd);

/*
 * These functions provide basic console output, in white and red colours
 * respectively. They are aliases to direct tier0 calls, so they work early on
 * even before anything else is initialised.
 */
#if defined(__GNUC__) || defined(__clang__)
#ifdef _WIN32
#define __asm__(x) __asm__("_" x) // stupid mangling meme, only on windows!
#endif
void con_msg(const char *fmt, ...) _CON_PRINTF(1, 2) __asm__("Msg");
void con_warn(const char *fmt, ...) _CON_PRINTF(1, 2) __asm__("Warning");
#undef __asm__
#else
#error Need an equivalent of asm names for your compiler!
#endif

struct rgba; // in engineapi.h - forward declare here to avoid warnings

extern void *_con_iface;
extern void (*_con_colourmsgf)(void *this, const struct rgba *c,
		const char *fmt, ...) _CON_PRINTF(3, 4);
/*
 * This provides the same functionality as ConColorMsg which was removed from
 * tier0 in the L4D engine branch - specifically, it allows printing a message
 * with an arbitrary RGBA colour. It must only be used after a successful
 * con_init() call.
 */
#define con_colourmsg(c, ...) _con_colourmsgf(_con_iface, c, __VA_ARGS__)

/*
 * The index of the client responsible for the currently executing command,
 * or -1 if serverside. This is a global variable because of Source spaghetti,
 * rather than unforced plugin spaghetti.
 */
extern int con_cmdclient;

// internal detail, used by DEF_* macros below
extern void *_con_vtab_cmd[];
// rtti pointers are offset negatively from the vtable pointer. to make this
// a constant expression we have to use a macro.
extern struct _con_vtab_var_wrap {
#ifdef _WIN32
	const struct msvc_rtti_locator *rtti;
#else
	// itanium ABI also has the top offset/"whole object" offset in libstdc++
	ssize topoffset;
	const struct itanium_vmi_type_info *rtti;
#endif
	void *vtable[19];
} _con_vtab_var_wrap;
#define _con_vtab_var (_con_vtab_var_wrap.vtable)
extern struct _con_vtab_iconvar_wrap {
#ifndef _WIN32
	ssize topoffset;
	const struct itanium_vmi_type_info *rtti;
#endif
	void *vtable[7];
} _con_vtab_iconvar_wrap;
#define _con_vtab_iconvar _con_vtab_iconvar_wrap.vtable

#define _DEF_CVAR(name_, desc, value, hasmin_, min, hasmax_, max, flags_) \
	static struct con_var _cvar_##name_ = { \
		.base = { \
			.vtable = _con_vtab_var, \
			.name = "" #name_, .help = "" desc, .flags = (flags_) \
		}, \
		.vtable_iconvar = _con_vtab_iconvar, \
		.parent = &_cvar_##name_, /* bizarre, but how the engine does it */ \
		.defaultval = _Generic(value, char *: value, int: #value, \
				double: #value), \
		/* N.B. the NOLINT comment below isn't for you, the reader, it's for the
		   computer, because clangd decided the only way to turn off a bogus
		   warning is to write a bogus comment. Also note, this comment you're
		   reading now isn't very useful either, I'm just angry. */ \
		.strlen = _Generic(value, char *: sizeof(value), /*NOLINT*/ \
				default: sizeof(#value)), \
		.fval = _Generic(value, char *: 0, int: value, double: value), \
		.ival = _Generic(value, char *: 0, int: value, double: (int)value), \
		.hasmin = hasmin_, .minval = (min), .hasmax = hasmax_, .maxval = (max) \
	}; \
	struct con_var *name_ = &_cvar_##name_;

/* Defines a console variable with no min/max values. */
#define DEF_CVAR(name, desc, value, flags) \
	_DEF_CVAR(name, desc, value, false, 0, false, 0, flags)

/* Defines a console variable with a given mininum numeric value. */
#define DEF_CVAR_MIN(name, desc, value, min, flags) \
	_DEF_CVAR(name, desc, value, true, min, false, 0, flags)

/* Defines a console variable with a given maximum numeric value. */
#define DEF_CVAR_MAX(name, desc, value, max, flags) \
	_DEF_CVAR(name, desc, value, false, 0, true, max, flags)

/* Defines a console variable in the given numeric value range. */
#define DEF_CVAR_MINMAX(name, desc, value, min, max, flags) \
	_DEF_CVAR(name, desc, value, true, min, true, max, flags)

#define _DEF_CCMD(varname, name_, desc, func, flags_) \
	static struct con_cmd _ccmd_##varname = { \
		.base = { \
			.vtable = _con_vtab_cmd, \
			.name = "" #name_, .help = "" desc, .flags = (flags_) \
		}, \
		.cb = &func, \
		.use_newcb = true \
	}; \
	struct con_cmd *varname = (struct con_cmd *)&_ccmd_##varname;

/* Defines a command with a given function as its handler. */
#define DEF_CCMD(name, desc, func, flags) \
	_DEF_CCMD(name, name, desc, func, flags)

/*
 * Defines two complementary +- commands, with PLUS_ and MINUS_ prefixes on
 * their C names.
 */
#define DEF_CCMD_PLUSMINUS(name, descplus, fplus, descminus, fminus, flags) \
	_DEF_CCMD(PLUS_##name, "+" name, descplus, fplus, flags) \
	_DEF_CCMD(MINUS_##name, "-" name, descminus, fminus, flags)

/*
 * Defines a console command with the handler function body immediately
 * following the macro (like in Source itself).
 */
#define DEF_CCMD_HERE(name, desc, flags) \
	static void _cmdf_##name(const struct con_cmdargs *cmd); \
	_DEF_CCMD(name, name, desc, _cmdf_##name, flags) \
	static void _cmdf_##name(const struct con_cmdargs *cmd) \
	/* { body here } */

/*
 * These are exactly the same as the above macros, but they don't cause the
 * commands or variables to be registered on plugin load.
 */
#define DEF_CVAR_UNREG DEF_CVAR
#define DEF_CVAR_MIN_UNREG DEF_CVAR_MIN
#define DEF_CVAR_MAX_UNREG DEF_CVAR_MAX
#define DEF_CVAR_MINMAX_UNREG DEF_CVAR_MINMAX
#define DEF_CCMD_UNREG DEF_CCMD
#define DEF_CCMD_HERE_UNREG DEF_CCMD_HERE
#define DEF_CCMD_PLUSMINUS_UNREG DEF_CCMD_PLUSMINUS

/*
 * Registers a command or variable defined with the _UNREG variants of the above
 * macros. Can be used to conditionally register things.
 */
void con_reg(void *cmd_or_var);

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
