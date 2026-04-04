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

#include <errno.h>
#include <string.h>
#include <time.h>

#include "con_.h"
#include "demorec.h"
#include "errmsg.h"
#include "feature.h"
#include "gameinfo.h"
#include "intdefs.h"
#include "mem.h"
#include "os.h"
#include "sst.h"

#ifdef _WIN32
#define fS "S"
#define localtime_r localtime_s
#else
#define fS "s"
#endif

FEATURE("speedrun recording helper")
REQUIRE(demorec)

DEF_FEAT_CVAR(sst_speedrun_dir, "Directory for speedrun demos (\"\" for none)",
		"speedruns", CON_ARCHIVE)

DEF_FEAT_CVAR(sst_speedrun_usesubdirs,
		"Use timestamped subdirectories for each run", 1, CON_ARCHIVE)

static bool recording = false;

static inline struct datetime {
	s16 Y; s8 M, D;
	s8 h, m, s;
} datetime() {
	// TODO(opt): doing this without the libc dep would be nice, but bleh long
	// division figure out later
	struct tm tm;
	vlong epoch = time(0);
	localtime_r(&tm, &epoch);
	return (struct datetime){
		tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec
	};
}
static void fmttime(os_char out[static 14], const struct datetime *dt) {
	uint Y = dt->Y, M = dt->M, D = dt->D, h = dt->h, m = dt->m, s = dt->s;
	uint div10 = Y * 3435973837ull >> 35;
	uint div100 = Y * 1374389535ull >> 37; // increase ILP
	uint mod10 = Y - div10 * 10, mod100div10 = div10 - div100 * 10;
	Y = div100;
	out[2] = OS_LIT('0') + mod100div10; out[3] = OS_LIT('0') + mod10;
	div10 = Y * 3435973837ull >> 35; mod10 = Y - div10 * 10;
	// assuming a 4-digit year here; will be true for the next ~8k years!
	out[0] = OS_LIT('0') + div10; out[1] = OS_LIT('0') + mod10;
	// for the month, we don't need to divide (or rather multiply-shift) at all;
	// the tens column is always either zero or one. */
	int tens = M >= 10;
	out[4] = OS_LIT('0') + tens; out[5] = OS_LIT('0') + M - 10 * tens;
	// the rest of these are always 2-digit, which makes things quite easy.
	div10 = D * 3435973837ull >> 35; mod10 = D - div10 * 10;
	out[6] = OS_LIT('0') + div10; out[7] = OS_LIT('0') + mod10;
	div10 = h * 3435973837ull >> 35; mod10 = h - div10 * 10;
	out[8] = OS_LIT('0') + div10; out[9] = OS_LIT('0') + mod10;
	div10 = m * 3435973837ull >> 35; mod10 = m - div10 * 10;
	out[10] = OS_LIT('0') + div10; out[11] = OS_LIT('0') + mod10;
	div10 = s * 3435973837ull >> 35; mod10 = s - div10 * 10;
	out[12] = OS_LIT('0') + div10; out[13] = OS_LIT('0') + mod10;
}

HANDLE_EVENT(DemoControlAllowed) {
	if (recording) {
		con_warn("%s during speedrun recording. %s\n",
				"Manual demo control disabled",
				"Use sst_speedrun_stop to finish a run.");
		return false;
	}
	return true;
}

HANDLE_EVENT(AllowPluginLoading) {
	if (recording) {
		con_warn("%s during speedrun recording. %s\n",
				"Cannot load or unload plugins",
				"Use sst_speedrun_stop to finish a run.");
		return false;
	}
	return true;
}

DEF_CCMD_HERE(sst_speedrun_start, "Start recording demos for a speedrun", 0) {
	if (recording) {
		con_warn("Already recording a speedrun. %s\n",
				"Use sst_speedrun_stop to finish a run.");
		return;
	}
	if (demorec_demonum() != -1) {
		con_warn("%sUse the stop command to finish the recording first.\n",
				"Currently manually recording a demo. ");
		return;
	}
	const char *basedir = con_getvarstr(sst_speedrun_dir);
#ifdef _WIN32
	if (strpbrk(basedir, "/\\")) {
#else
	if (strchr(basedir, '/')) {
#endif
		errmsg_errorx("sst_speedrun_dir must not contain a slash");
		return;
	}
	if (basedir[0] == '.' && (basedir[1] == '\0' ||
			basedir[1] == '.' && basedir[2] == '\0')) {
		errmsg_errorx("sst_speedrun_dir must not point outside the game");
		return;
	}
	// basedir/yyyymmddhhmmssNN/yymmdd
	int baselen = strlen(basedir);
	int maxlen = baselen + !!baselen; // slash, unless empty (no subdir)
	bool usesubdirs = con_getvari(sst_speedrun_usesubdirs);
	if (usesubdirs) maxlen += sizeof("yyyymmddhhmmss/") - 1;
	// for this length calc we assume there'll never been 100 *million* demos
	// (the max possible int would of course be 2 billion, but the game would
	// likely crash way before then)
	maxlen += sizeof("yymmddhhmmss_99999999.dem"); // include \0
	if (maxlen > PATH_MAX) {
		errmsg_errorx("demo filename is too long");
		return;
	}
	if (os_strlen(gameinfo_gamedir) + 1 + maxlen > PATH_MAX) {
		errmsg_errorx("path to game directory is too long for demo path");
		return;
	}
	os_char path[PATH_MAX];
	// TODO(opt): maybe gameinfo should have globals for the lengths?
	int pathsz = os_strlen(gameinfo_gamedir);
	os_spancopy(path, gameinfo_gamedir, pathsz);
	path[pathsz++] = OS_LIT('/');
	int prefixsz = pathsz;
	if (basedir[0]) {
#ifdef _WIN32
		for (int i = 0; i < baselen; ++i) path[pathsz + i] = basedir[i];
#else
		mem_copy(path + pathsz, basedir, baselen);
#endif
		pathsz += baselen;
		path[pathsz] = OS_LIT('\0');
		if (!os_mkdir(path) && os_lasterror() != OS_EEXIST) {
			errmsg_errorsys("couldn't create directory %" fS, path);
			return;
		}
		path[pathsz++] = OS_LIT('/');
	}
	struct datetime dt = datetime();
	if (usesubdirs) {
		fmttime(path + pathsz, &dt);
		path[pathsz + 14] = OS_LIT('\0');
		if (!os_mkdir(path) && os_lasterror() != OS_EEXIST) {
			errmsg_errorsys("couldn't create directory %" fS, path);
			return;
		}
		path[pathsz + 14] = OS_LIT('/');
		__builtin_memcpy_inline(path + pathsz + 15, path + pathsz,
				14 * sizeof(*path));
		__builtin_memcpy_inline(path + pathsz + 29, OS_LIT(".dem"),
				sizeof(OS_LIT(".dem")));
		pathsz += 33;
	}
	else {
		fmttime(path + pathsz, &dt);
		__builtin_memcpy_inline(path + pathsz + 14, OS_LIT(".dem"),
				sizeof(OS_LIT(".dem")));
		pathsz += 18;
	}
	struct os_stat st;
	if (os_stat(path, &st) != -1) {
		errmsg_errorx("demo with this timestamp already exists; "
				"try again in ~1s");
		return;
	}
	else if (errno != ENOENT) {
		errmsg_errorstd("couldn't get info for file %" fS, path);
		return;
	}
#ifdef _WIN32
	char recordpath[PATH_MAX];
	for (int i = 0; i < pathsz - prefixsz - 4; ++i) { // leave off .dem
		recordpath[i] = path[i + prefixsz]; // back to ascii
	}
#else
	char *recordpath = path + prefixsz;
#endif
	recordpath[pathsz - prefixsz - 4] = OS_LIT('\0');
	con_msg("Speedrun recording started\n");
	recording = true;
	demorec_forceauto = true;
	demorec_start(recordpath);
}

DEF_CCMD_HERE(sst_speedrun_stop, "Finish recording speedrun demos", 0) {
	if (!recording) {
		if (demorec_demonum() != -1) {
			con_warn("%sUse the stop command to end the recording.\n",
					"Currently manually recording a demo. ");
		}
		// seems best to be silent if not recording at all, for keybinds etc.
		return;
	}
	recording = false;
	demorec_forceauto = false;
	int n = demorec_stop();
	if (n) {
		con_msg("Finished speedrun; recorded %d demos.\n", n);
	}
	else {
		con_msg("Cancelled speedrun; no demos were recorded.\n");
		// XXX: do we want to delete the now-empty dirs in this case?
	}
}

// TODO(rta): add some sort of abort/abandon command too, which deletes demos
// rather than leaving them behind.

INIT { return FEAT_OK; }

// vi: sw=4 ts=4 noet tw=80 cc=80
