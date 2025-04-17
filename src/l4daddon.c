/*
 * Copyright © Hayden K <imaciidz@gmail.com>
 * Copyright © Willian Henrique <wsimanbrazil@yahoo.com.br>
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

#include <string.h>

#include "con_.h"
#include "engineapi.h"
#include "errmsg.h"
#include "feature.h"
#include "gamedata.h"
#include "hook.h"
#include "intdefs.h"
#include "langext.h"
#include "mem.h"
#include "os.h"
#include "ppmagic.h"
#include "sst.h"
#include "vcall.h"
#include "x86.h"
#include "x86util.h"

FEATURE("Left 4 Dead 2 addon bugfixes")
GAMESPECIFIC(L4D2_2125plus)
REQUIRE_GAMEDATA(vtidx_ManageAddonsForActiveSession)
REQUIRE_GLOBAL(engclient)

// Keeping this here since it will be useful for Future Addon Feature Plans™
/*struct SAddOnMetaData { // literal psychopath capitalisation
	char absolutePath;
	char name;
	int type; // (0 = content, 1 = mission, 2 = mode)
	int unknown;
};*/

// count of how many addon metadata entries there are - this is the m_Size
// member of s_vecAddonMetaData (which is a CUtlVector<SAddOnMetaData>)
static int *addonvecsz;
static char last_mission[128] = {0}, last_gamemode[128] = {0};
static int last_addonvecsz = 0;
static bool last_disallowaddons = false;

DECL_VFUNC_DYN(struct VEngineClient, void, ManageAddonsForActiveSession)

// Crazy full name: FileSystem_ManageAddonsForActiveSession. Hence the acronym.
// Note: the 4th parameter was first added in 2.2.0.4 (21 Oct 2020), but we
// don't have to worry about that since it's cdecl (and we don't use it
// ourselves, just pass it straight through).
typedef void (*FS_MAFAS_func)(bool, char *, char *, bool);
static FS_MAFAS_func orig_FS_MAFAS;
static void hook_FS_MAFAS(bool disallowaddons, char *mission, char *gamemode,
		bool ismutation) {
	// At the start of a map, particularly in 2204+ in campaigns with L4D1
	// commons, there can be a ton of hitches due to the game trying to
	// load uncached materials as models are drawn. This FS_MAFAS function is
	// the main cause of materials becoming uncached. When hosting a server,
	// there are 2 calls to this function per map load: first by the server
	// module calling CVEngineServer::ManageAddonsForActiveSession() and then by
	// the client module calling CEngineClient::ManageAddonsForActiveSession()
	// (non-host players only call call the latter). Omitting the second call
	// (for partially unknown reasons) fixes most hitches, but the work done by
	// FS_MAFAS can be omitted in a few more cases too.
	//
	// The function tries to evaluate which addon VPKs should be loaded in the
	// FS based on the current gamemode, campaign and addon restrictions, adding
	// and removing VPKs from the FS interface as necessary, and invalidating
	// many caches (material, model and audio) to ensure proper reloading of
	// assets when necessary. Given that enabled addons and addon restrictions
	// should not change mid-campaign and as such the parameters given to this
	// function should change very rarely, we can avoid unnecessary cache
	// invalidation by checking the parameter values along with whether addons
	// are enabled. Both disconnecting from a server and using the addon menu
	// call FS_MAFAS to allow every enabled VPK to be loaded, so we let those
	// calls go through. This fixes the vast majority of laggy cases without
	// breaking anything practice.
	//
	// As a bonus, doing all this also seems to speed up map loads by about 1s.
	//
	// TODO(opt): There's one unhandled edge case reconnecting the to the same
	// server we were just on, if the server hasn't changed maps. It's unclear
	// why hitches still occur in this case; further research is required.

	int curaddonvecsz = *addonvecsz;
	if (curaddonvecsz != last_addonvecsz) {
		// addons list has changed, meaning we're in the main menu. we will have
		// already been called with null mission and/or gamemode and reset the
		// last_ things, so update the count then call the original function.
		last_addonvecsz = curaddonvecsz;
		goto e;
	}

	// if we have zero addons loaded, we can skip doing anything else.
	if (!curaddonvecsz) return;

	// we have some addons, which may or may not have changed. based on the
	// above assumption that nothing will change *during* a campaign, cache
	// campaign and mode names try to early-exit if neither has changed. the
	// mission string can be empty if playing a gamemode not supported by the
	// current map (such as survival on c8m1), so always call the original in
	// that case since we can't know whether we changed campaigns or not.
	if (mission && gamemode && *mission) {
		int missionlen = strlen(mission + 1) + 1;
		int gamemodelen = strlen(gamemode);
		if (missionlen < sizeof(last_mission) &&
				gamemodelen < sizeof(last_gamemode)) {
			bool canskip = disallowaddons == last_disallowaddons &&
					!strncmp(mission, last_mission, missionlen + 1) &&
					!strncmp(gamemode, last_gamemode, gamemodelen + 1);
			if_hot (canskip) {
				disallowaddons = last_disallowaddons;
				memcpy(last_mission, mission, missionlen + 1);
				memcpy(last_gamemode, gamemode, gamemodelen + 1);
				return;
			}
		}
	}

	// If we get here, we don't know for sure whether something might have
	// changed, so we have to assume it did; we reset our cached values to avoid
	// any false negatives in future, and then call the original function.
	last_disallowaddons = false;
	last_mission[0] = '\0';
	last_gamemode[0] = '\0';

e:	orig_FS_MAFAS(disallowaddons, mission, gamemode, ismutation);
}

static inline bool find_FS_MAFAS() {
#ifdef _WIN32
	const uchar *insns = engclient->vtable[vtidx_ManageAddonsForActiveSession];
	// CEngineClient::ManageAddonsForActiveSession just calls FS_MAFAS
	for (const uchar *p = insns; p - insns < 32;) {
		if (p[0] == X86_CALL) {
			orig_FS_MAFAS = (FS_MAFAS_func)(p + 5 + mem_loads32(p + 1));
			return true;
		}
		NEXT_INSN(p, "FileSystem_ManageAddonsForActiveSession function");
	}
#else
#warning: TODO(linux): asm search stuff
#endif
	return false;
}

static inline bool find_addonvecsz(con_cmdcb show_addon_metadata_cb) {
#ifdef _WIN32
	const uchar *insns = (const uchar*)show_addon_metadata_cb;
	// show_addon_metadata immediately checks if s_vecAddonMetadata.m_Size is 0,
	// so we can just grab it from the CMP instruction
	for (const uchar *p = insns; p - insns < 32;) {
		if (p[0] == X86_ALUMI8S && p[1] == X86_MODRM(0, 7, 5) && p[6] == 0) {
			addonvecsz = mem_loadptr(p + 2);
			return true;
		}
		NEXT_INSN(p, "addonvecsz variable");
	}
#else
#warning: TODO(linux): asm search stuff
#endif
	return false;
}

static void *broken_addon_check = 0;
static uchar orig_broken_addon_check_bytes[13];

enum { SMALLNOP = 9, BIGNOP = 13 };
static inline bool nop_addon_check(int noplen) {
	// In versions prior to 2204 (21 Oct 2020), FS_MAFAS checks if any
	// addons are enabled before doing anything else. If no addons are enabled,
	// then the function just returns immediately. FS_MAFAS gets called by
	// update_addon_paths, which is run when you click 'Done' in the addons
	// menu. This means that turning off all addons breaks everything until the
	// game is restarted or another addon is loaded. To fix this, we just
	// replace the CMP and JZ instructions with NOPs. Depending on the version
	// of the code, we either have to replace 9 bytes (e.g. 2203) or 13 bytes
	// (e.g. 2147). So, we have a 9-byte NOP followed by a 4-byte NOP and can
	// just use the given length value.
	static const uchar nops[] =
		HEXBYTES(66, 0F, 1F, 84, 00, 00, 00, 00, 00, 0F, 1F, 40, 00);
	// NOTE: always using 13 for orig even though noplen can be 9 or 13; not
	// worth tracking that vs. just always putting 13 bytes back later.
	// Also passing 13 to mprot just in case the instructions straddle a page
	// boundary (unlikely, of course).
	if_hot (os_mprot(broken_addon_check, 13, PAGE_EXECUTE_READWRITE)) {
		memcpy(orig_broken_addon_check_bytes, broken_addon_check, 13);
		memcpy(broken_addon_check, nops, noplen);
		return true;
	}
	else {
		errmsg_warnsys("couldn't fix broken addon check: "
				"couldn't make make memory writable");
		return false;
	}
}

static inline void try_fix_broken_addon_check() {
	uchar *insns = (uchar *)orig_FS_MAFAS;
	for (uchar *p = insns; p - insns < 32;) {
		if (p[0] == X86_ALUMI8S && p[1] == X86_MODRM(0, 7, 5) &&
				mem_loadptr(p + 2) == addonvecsz) {
			if (nop_addon_check(p[7] == X86_2BYTE ? SMALLNOP : BIGNOP)) {
				broken_addon_check = p; // conditional so END doesn't crash!
			}
			return;
		}
		int len = x86_len(p);
		if_cold (len == -1) {
			errmsg_warnx("couldn't find broken addon check code: "
					"unknown or invalid instruction");
			return;
		}
		p += len;
	}
	return;
}

INIT {
	struct con_cmd *show_addon_metadata = con_findcmd("show_addon_metadata");
	if_cold (!show_addon_metadata) return FEAT_INCOMPAT; // shouldn't happen!
	if_cold (!find_addonvecsz(show_addon_metadata->cb)) {
		errmsg_errorx("couldn't find pointer to addon list");
		return FEAT_INCOMPAT;
	}
	if_cold (!find_FS_MAFAS()) {
		errmsg_errorx("couldn't find FileSystem_ManageAddonsForActiveSession");
		return FEAT_INCOMPAT;
	}
	try_fix_broken_addon_check();
	struct hook_inline_featsetup_ret h = hook_inline_featsetup(
			(void *)orig_FS_MAFAS, (void **)&orig_FS_MAFAS,
			"FileSystem_ManageAddonsForActiveSession");
	if_cold (h.err) return h.err;
	hook_inline_commit(h.prologue, (void *)&hook_FS_MAFAS);
	return FEAT_OK;
}

END {
	// TODO(opt): can this unhook be made conditional too? bill suggested it
	// before but I don't know. maybe Hayden knows - mike
	unhook_inline((void *)orig_FS_MAFAS);
	if_cold (sst_userunloaded) {
		if (broken_addon_check) {
			memcpy(broken_addon_check, orig_broken_addon_check_bytes, 13);
		}
	}
}

// vi: sw=4 ts=4 noet tw=80 cc=80
