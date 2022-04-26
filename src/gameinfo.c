/*
 * Copyright © 2022 Michael Smith <mikesmiffy128@gmail.com>
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

#include <stdbool.h>
#ifdef _WIN32
#include <shlwapi.h>
#endif

#include "con_.h"
#include "gametype.h"
#include "intdefs.h"
#include "kv.h"
#include "os.h"
#include "vcall.h"

#ifdef _WIN32
#define fS "S" // os string (wide string) to regular string
#define Fs L"S" // regular string to os string (wide string)
#define PATHSEP L"\\" // for joining. could just be / but \ is more consistent
#else
// everything is just a regular string already
#define fS "s"
#define Fs "s"
#define PATHSEP "/"
#endif

// TODO(opt): get rid of the rest of the snprintf and strcpy, some day

static os_char bindir[PATH_MAX] = {0};
#ifdef _WIN32
static os_char gamedir[PATH_MAX] = {0};
#endif
static os_char clientlib[PATH_MAX] = {0};
static os_char serverlib[PATH_MAX] = {0};
static char title[64] = {0};

const os_char *gameinfo_bindir = bindir;
const os_char *gameinfo_gamedir
#ifdef _WIN32
	= gamedir // on linux, the pointer gets directly set in gameinfo_init()
#endif
;
const os_char *gameinfo_clientlib = clientlib;
const os_char *gameinfo_serverlib = serverlib;
const char *gameinfo_title = title;

// case insensitive substring match, expects s2 to be lowercase already!
// note: in theory this shouldn't need to be case sensitive, but I've seen mods
// use both lowercase and TitleCase so this is just to be as lenient as possible
static bool matchtok(const char *s1, const char *s2, usize sz) {
	for (; sz; --sz, ++s1, ++s2) if (tolower(*s1) != *s2) return false;
	return true;
}

static void trygamelib(const os_char *path, os_char *outpath) {
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
static inline void dolibsearch(const char *p, uint len, bool isgamebin,
		const os_char *cwd) {
	// sanity check: don't do a bunch of work for no reason
	if (len >= PATH_MAX - 1 - (sizeof("client" OS_DLSUFFIX) - 1)) goto toobig;
	os_char bindir[PATH_MAX];
	os_char *outp = bindir;
	// this should really be an snprintf, meh whatever
	os_strcpy(bindir, cwd);
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
		trygamelib(bindir, clientlib);
	}
	if (!*gameinfo_serverlib) {
		os_strcpy(outp, OS_LIT("server" OS_DLSUFFIX));
		trygamelib(bindir, serverlib);
	}
}

// state for the callback below to keep it somewhat reentrant (except where
// it's not because I got lazy and wrote some spaghetti)
struct kv_parsestate {
	const os_char *cwd;
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
			// dumb hack: ignore Survivors title (they left it set to "Left 4
			// Dead 2" but it clearly isn't Left 4 Dead 2)
			if (ctxt->matchtype == mt_title && !GAMETYPE_MATCHES(L4DS)) {
				// title really shouldn't get this long, but truncate just to
				// avoid any trouble...
				// also note: leaving 1 byte of space for null termination (the
				// buffer is already zeroed initially)
				if (len > sizeof(title) - 1) len = sizeof(title) - 1;
				memcpy(title, p, len);
			}
			else if (ctxt->matchtype == mt_game ||
					ctxt->matchtype == mt_gamebin) {
				// if we already have everything, we can just stop!
				if (*gameinfo_clientlib && *gameinfo_serverlib) break;
				dolibsearch(p, len, ctxt->matchtype == mt_gamebin, ctxt->cwd);
			}
			ctxt->matchtype = mt_none;
			break;
		case KV_NEST_END:
			if (ctxt->dontcarelvl) --ctxt->dontcarelvl; else --ctxt->nestlvl;
			break;
		case KV_COND_PREFIX: case KV_COND_SUFFIX:
			con_warn("gameinfo: warning: just ignoring conditional \"%.*s\"",
					len, p);
	}
	#undef MATCH
}

bool gameinfo_init(void *(*ifacef)(const char *, int *)) {
	typedef char *(*VCALLCONV GetGameDirectory_func)(void *this);
	GetGameDirectory_func **engclient;
	int off;
	if (engclient = ifacef("VEngineClient015", 0)) { // portal 2 (post-release?)
		off = 35;
	}
	else if (engclient = ifacef("VEngineClient014", 0)) { // l4d2000-~2027, bms?
		if (!GAMETYPE_MATCHES(L4D2x)) goto unsup;
		off = 73; // YES, THIS IS SEVENTY THREE ALL OF A SUDDEN. I KNOW. CRAZY.
	}
	else if (engclient = ifacef("VEngineClient013", 0)) { // ...most things?
		if (GAMETYPE_MATCHES(L4Dx)) off = 36; // THEY CHANGED IT BACK LATER!?
		else off = 35;
	}
	else if (engclient = ifacef("VEngineClient012", 0)) { // dmomm, ep1, ...
		off = 37;
	}
	else {
unsup:	con_warn("gameinfo: unsupported VEngineClient interface\n");
		return false;
	}
	GetGameDirectory_func GetGameDirectory = (*engclient)[off];

	// engine always calls chdir() with its own base path on startup, so engine
	// base dir is just cwd
	os_char cwd[PATH_MAX];
	if (!os_getcwd(cwd, sizeof(cwd) / sizeof(*cwd))) {
		con_warn("gameinfo: couldn't get working directory: %s\n",
				strerror(errno));
		return false;
	}
	int len = os_strlen(cwd);
	if (len + sizeof("/bin") > sizeof(bindir) / sizeof(*bindir)) {
		con_warn("gameinfo: working directory path is too long!\n");
		return false;
	}
	memcpy(bindir, cwd, len * sizeof(*cwd));
	memcpy(bindir + len, PATHSEP OS_LIT("bin"), 5 * sizeof(os_char));

#ifdef _WIN32
	int gamedirlen = _snwprintf(gamedir, sizeof(gamedir) / sizeof(*gamedir),
				L"%S", GetGameDirectory(engclient));
	if (gamedirlen < 0) { // encoding error??? ugh...
		con_warn("gameinfo: invalid game directory path!\n");
		return false;
	}
#else
	// no need to munge charset, use the string pointer directly
	gameinfo_gamedir = GetGameDirectory(engclient);
	int gamedirlen = strlen(gameinfo_gamedir);
#endif
	if (gamedirlen + sizeof("/gameinfo.txt") > sizeof(gamedir) /
			sizeof(*gamedir)) {
		con_warn("gameinfo: game directory path is too long!\n");
		return false;
	}
	os_char gameinfopath[PATH_MAX];
	memcpy(gameinfopath, gameinfo_gamedir, gamedirlen *
			sizeof(*gameinfo_gamedir));
	memcpy(gameinfopath + gamedirlen, PATHSEP OS_LIT("gameinfo.txt"),
			14 * sizeof(os_char));
	int fd = os_open(gameinfopath, O_RDONLY);
	if (fd == -1) {
		con_warn("gameinfo: couldn't open gameinfo.txt: %s\n", strerror(errno));
		return false;
	}
	char buf[1024];
	struct kv_parser kvp = {0};
	struct kv_parsestate ctxt = {.cwd = cwd};
	int nread;
	while (nread = read(fd, buf, sizeof(buf))) {
		if (nread == -1) {
			con_warn("gameinfo: couldn't read gameinfo.txt: %s\n",
					strerror(errno));
			goto e;
		}
		if (!kv_parser_feed(&kvp, buf, nread, &kv_cb, &ctxt)) goto ep;
	}
	if (!kv_parser_done(&kvp)) goto ep;
	close(fd);

	// dumb hack pt2, see also kv callback above
	if (GAMETYPE_MATCHES(L4DS)) gameinfo_title = "Left 4 Dead: Survivors";
	return true;

ep:	con_warn("gameinfo: couldn't parse gameinfo.txt (%d:%d): %s\n",
			kvp.line, kvp.col, kvp.errmsg);
e:	close(fd);
	return false;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
