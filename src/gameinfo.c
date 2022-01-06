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

#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#ifdef _WIN32
#include <shlwapi.h>
#endif

#include "con_.h"
#include "intdefs.h"
#include "kv.h"
#include "os.h"

// Formatting for os_char * -> char * (or vice versa) - needed for con_warn()s
// with file paths, etc
#ifdef _WIN32
#define fS "S" // os string (wide string) to regular string
#define Fs L"S" // regular string to os string (wide string)
#else
// everything is just a regular string already
#define fS "s"
#define Fs "s"
#endif

static os_char exedir[PATH_MAX];
static os_char gamedir[PATH_MAX];
static char _gameinfo_title[64] = {0};
const char *gameinfo_title = _gameinfo_title;
static os_char _gameinfo_clientlib[PATH_MAX] = {0};
const os_char *gameinfo_clientlib = _gameinfo_clientlib;
static os_char _gameinfo_serverlib[PATH_MAX] = {0};
const os_char *gameinfo_serverlib = _gameinfo_serverlib;

// magical argc/argv grabber so we don't have to go through procfs
#ifdef __linux__
static const char *const *prog_argv;
static int storeargs(int argc, char *argv[]) {
	prog_argv = (const char *const *)argv;
	return 0;
}
__attribute__((used, section(".init_array")))
static void *pstoreargs = &storeargs;
#endif

// case insensitive substring match, expects s2 to be lowercase already!
// note: in theory this shouldn't need to be case sensitive, but I've seen mods
// use both lowercase and TitleCase so this is just to be as lenient as possible
static bool matchtok(const char *s1, const char *s2, usize sz) {
	for (; sz; --sz, ++s1, ++s2) if (tolower(*s1) != *s2) return false;
	return true;
}

static void try_gamelib(const os_char *path, os_char *outpath) {
	// _technically_ this is toctou, but I don't think that matters here
	if (os_access(path, F_OK) != -1) {
		os_strcpy(outpath, path);
	}
	else if (errno != ENOENT) {
		con_warn("gameinfo: failed to access %" fS ": %s\n", path,
				strerror(errno));
	}
}

// note: p and len are a non-null-terminated string
static inline void do_gamelib_search(const char *p, uint len, bool isgamebin) {
	// sanity check: don't do a bunch of work for no reason
	if (len >= PATH_MAX - 1 - (sizeof("client" OS_DLSUFFIX) - 1)) goto toobig;
	os_char bindir[PATH_MAX];
	os_char *outp = bindir;
	// this should really be an snprintf, meh whatever
	os_strcpy(bindir, exedir);
	outp = bindir + os_strlen(bindir);
	// quick note about windows encoding conversion: this MIGHT clobber the
	// encoding of non-ascii mod names, but it's unclear if/how source handles
	// that anyway, so we just have to assume there *are no* non-ascii mod
	// names, since they'd also be clobbered, probably. if I'm wrong this can
	// just change later to an explicit charset conversion, so... it's kinda
	// whatever, I guess
	const os_char *fmt = isgamebin ?
		OS_LIT("/%.*") Fs OS_LIT("/") :
		OS_LIT("/%.*") Fs OS_LIT("/bin/");
	int spaceleft = PATH_MAX;
	if (len >= 25 && matchtok(p, "|all_source_engine_paths|", 25)) {
		// this special path doesn't seem any different to normal,
		// why is this a thing?
		p += 25; len -= 25;
	}
	else if (len >= 15 && matchtok(p, "|gameinfo_path|", 15)) {
		// search in the actual mod/game directory
		p += 15; len -= 15;
		int ret = os_snprintf(bindir, PATH_MAX, OS_LIT("%s"), gamedir);
		outp = bindir + ret;
		spaceleft -= ret;
	}
	else {
#ifdef _WIN32
		// sigh
		char api_needs_null_term[PATH_MAX];
		memcpy(api_needs_null_term, p, len * sizeof(*p));
		api_needs_null_term[len] = L'\0';
		if (!PathIsRelativeA(api_needs_null_term))
#else
		if (*p == '/') // so much easier :')
#endif
	{
		// the mod path is absolute, so we're not sticking anything else in
		// front of it, so skip the leading slash in fmt and point the pointer
		// at the start of the buffer
		++fmt;
		outp = bindir;
	}}

	// leave room for server/client.dll/so (note: server and client happen to
	// conveniently have the same number of letters)
	int fmtspace = spaceleft - (sizeof("client" OS_DLSUFFIX) - 1);
	int ret = os_snprintf(outp, fmtspace, fmt, len, p);
	if (ret >= fmtspace) {
toobig: con_warn("gameinfo: skipping an overly long search path\n");
		return;
	}
	outp += ret;
	if (!*gameinfo_clientlib) {
		os_strcpy(outp, OS_LIT("client" OS_DLSUFFIX));
		try_gamelib(bindir, _gameinfo_clientlib);
	}
	if (!*gameinfo_serverlib) {
		os_strcpy(outp, OS_LIT("server" OS_DLSUFFIX));
		try_gamelib(bindir, _gameinfo_serverlib);
	}
}

// state for the callback below to keep it somewhat reentrant-ish (except where
// it isn't because I got lazy and wrote some spaghetti)
struct kv_parsestate {
	// after parsing a key we *don't* care about, how many nested subkeys have
	// we come across?
	short dontcarelvl;
	// after parsing a key we *do* care about, which key in the matchkeys[]
	// array below are we looking for next?
	schar nestlvl;
	// what kind of key did we just match?
	schar matchtype;
};

// this is a sprawling mess. Too Bad!
static void kv_cb(enum kv_token type, const char *p, uint len, void *_ctxt) {
	struct kv_parsestate *ctxt = _ctxt;

	static const struct {
		const char *s;
		uint len;
	} matchkeys[] = {
		{"gameinfo", 8},
		{"filesystem", 10},
		{"searchpaths", 11}
	};

	// values for ctxt->matchtype
	enum {
		mt_none,
		mt_title,
		mt_nest,
		mt_game,
		mt_gamebin
	};

	#define MATCH(s) (len == sizeof(s) - 1 && matchtok(p, s, sizeof(s) - 1))
	switch (type) {
		case KV_IDENT: case KV_IDENT_QUOTED:
			if (ctxt->nestlvl == 1 && MATCH("game")) {
				ctxt->matchtype = mt_title;
			}
			else if (ctxt->nestlvl == 3) {
				// for some reason there's a million different ways of
				// specifying the same type of path
				if (MATCH("mod+game") || MATCH("game+mod") || MATCH("game") ||
						MATCH("mod")) {
					ctxt->matchtype = mt_game;
				}
				else if (MATCH("gamebin")) {
					ctxt->matchtype = mt_gamebin;
				}
			}
			else if (len == matchkeys[ctxt->nestlvl].len &&
					matchtok(p, matchkeys[ctxt->nestlvl].s, len)) {
				ctxt->matchtype = mt_nest;
			}
			break;
		case KV_NEST_START:
			if (ctxt->matchtype == mt_nest) ++ctxt->nestlvl;
			else ++ctxt->dontcarelvl;
			ctxt->matchtype = mt_none;
			break;
		case KV_VAL: case KV_VAL_QUOTED:
			if (ctxt->dontcarelvl) break;
			if (ctxt->matchtype == mt_title) {
				// title really shouldn't get this long, but truncate just to
				// avoid any trouble...
				// also note: leaving 1 byte of space for null termination (the
				// buffer is already zeroed initially)
				if (len > sizeof(_gameinfo_title) - 1) {
					len = sizeof(_gameinfo_title) - 1;
				}
				memcpy(_gameinfo_title, p, len);
			}
			else if (ctxt->matchtype == mt_game ||
					ctxt->matchtype == mt_gamebin) {
				// if we already have everything, we can just stop!
				if (*gameinfo_clientlib && *gameinfo_serverlib) break;
				do_gamelib_search(p, len, ctxt->matchtype == mt_gamebin);
			}
			ctxt->matchtype = mt_none;
			break;
		case KV_NEST_END:
			if (ctxt->dontcarelvl) --ctxt->dontcarelvl; else --ctxt->nestlvl;
	}
	#undef MATCH
}

bool gameinfo_init(void) {
	const os_char *modname = OS_LIT("hl2");
#ifdef _WIN32
	int len = GetModuleFileNameW(0, exedir, PATH_MAX);
	if (!len) {
		char err[128];
		OS_WINDOWS_ERROR(err);
		con_warn("gameinfo: couldn't get EXE path: %s\n", err);
		return false;
	}
	// if the buffer is full and has no null, it's truncated
	if (len == PATH_MAX && exedir[len - 1] != L'\0') {
		con_warn("gameinfo: EXE path is too long!\n");
		return false;
	}
#else
	int len = readlink("/proc/self/exe", exedir, PATH_MAX);
	if (len == -1) {
		con_warn("gameinfo: couldn't get program path: %s\n", strerror(errno));
		return false;
	}
	// if the buffer is full at all, it's truncated (readlink never writes \0)
	if (len == PATH_MAX) {
		con_warn("gameinfo: program path is too long!\n");
		return false;
	}
	else {
		exedir[len] = '\0';
	}
#endif
	// find the last slash
	os_char *p;
	for (p = exedir + len - 1; *p != OS_LIT('/')
#ifdef _WIN32
			&& *p != L'\\'
#endif
	;		--p);
	// ... and split on it
	*p = 0;
	const os_char *exename = p + 1;
#ifdef _WIN32
	// try and infer the default mod name (when -game isn't given) from the exe
	// name for a few known games
	if (!_wcsicmp(exename, L"left4dead2.exe")) modname = L"left4dead2";
	else if (!_wcsicmp(exename, L"left4dead.exe")) modname = L"left4dead";
	else if (!_wcsicmp(exename, L"portal2.exe")) modname = L"portal2";

	const ushort *args = GetCommandLineW();
	const ushort *argp = args;
	ushort modbuf[PATH_MAX];
	// have to take the _last_ occurence of -game because sourcemods get the
	// flag twice, for some reason
	while (argp = wcsstr(argp, L" -game ")) {
		argp += 7;
		while (*argp == L' ') ++argp;
		ushort sep = L' ';
		// WARNING: not handling escaped quotes and such nonsense, since you
		// can't have quotes in filepaths anyway outside of UNC and I'm just
		// assuming there's no way Source could even be started with such an
		// insanely named mod. We'll see how this assumption holds up!
		if (*argp == L'"') {
			++argp;
			sep = L'"';
		}
		ushort *bufp = modbuf;
		for (; *argp != L'\0' && *argp != sep; ++argp, ++bufp) {
			if (bufp - modbuf == PATH_MAX - 1) {
				con_warn("gameinfo: mod name parameter is too long\n");
				return false;
			}
			*bufp = *argp;
		}
		*bufp = L'\0';
		modname = modbuf;
	}
	bool isrelative = PathIsRelativeW(modname);
#else
	// also do the executable name check just for portal2_linux
	if (!strcmp(exename, "portal2_linux")) modname = "portal2";
	// ah, the sane, straightforward world of unix command line arguments :)
	for (const char *const *pp = prog_argv + 1; *pp; ++pp) {
		if (!strcmp(*pp, "-game")) {
			if (!*++pp) break;
			modname = *pp;
		}
	}
	// ah, the sane, straightforward world of unix paths :)
	bool isrelative = modname[0] != '/';
#endif

	int ret = isrelative ?
		os_snprintf(gamedir, PATH_MAX, OS_LIT("%s/%s"), exedir, modname) :
		// mod name might actually be an absolute (if installed in steam
		// sourcemods for example)
		os_snprintf(gamedir, PATH_MAX, OS_LIT("%s"), modname);
	if (ret >= PATH_MAX) {
		con_warn("gameinfo: game directory path is too long!\n");
		return false;
	}
	os_char gameinfopath[PATH_MAX];
	if (os_snprintf(gameinfopath, PATH_MAX, OS_LIT("%s/gameinfo.txt"),
			gamedir, modname) >= PATH_MAX) {
		con_warn("gameinfo: gameinfo.text path is too long!\n");
		return false;
	}

	int fd = os_open(gameinfopath, O_RDONLY);
	if (fd == -1) {
		con_warn("gameinfo: couldn't open gameinfo.txt: %s\n", strerror(errno));
		return false;
	}
	char buf[1024];
	struct kv_parser kvp = {0};
	struct kv_parsestate ctxt = {0};
	int nread;
	while (nread = read(fd, buf, sizeof(buf))) {
		if (nread == -1) {
			con_warn("gameinfo: couldn't read gameinfo.txt: %s\n",
					strerror(errno));
			goto e;
		}
		kv_parser_feed(&kvp, buf, nread, &kv_cb, &ctxt);
		if (kvp.state == KV_PARSER_ERROR) goto ep;
	}
	kv_parser_done(&kvp);
	if (kvp.state == KV_PARSER_ERROR) goto ep;

	close(fd);
	return true;

ep:	con_warn("gameinfo: couldn't parse gameinfo.txt (%d:%d): %s\n",
			kvp.line, kvp.col, kvp.errmsg);
e:	close(fd);
	return false;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
