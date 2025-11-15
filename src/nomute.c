/*
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

// TODO(linux): this is currently only built on Windows, but a linux
// implementation would be also useful for some games e.g. L4D2

// NOTE: all three of these headers must be in this order. annoyingly.
#include <Windows.h>
#include <mmeapi.h>
#include <dsound.h>

#include "con_.h"
#include "errmsg.h"
#include "feature.h"
#include "langext.h"
#include "os.h"
#include "sst.h"

FEATURE("inactive window audio control")

DEF_CVAR_UNREG(snd_mute_losefocus,
		"Keep playing audio while tabbed out (SST reimplementation)", 1,
		CON_ARCHIVE | CON_INIT_HIDDEN)

static IDirectSoundVtbl *ds_vt = 0;
static typeof(ds_vt->CreateSoundBuffer) orig_CreateSoundBuffer;
static con_cmdcbv1 snd_restart_cb = 0;

// early init via VDF happens before config is loaded and audio is set up after
// that, so we don't want to run snd_restart the first time the cvar is set,
// unless we were loaded later with plugin_load in which case we actually do.
static bool skiprestart;
static void losefocuscb(struct con_var *v) {
	if_hot (!skiprestart) snd_restart_cb();
	skiprestart = false;
}

static long __stdcall hook_CreateSoundBuffer(IDirectSound *this,
		const DSBUFFERDESC *desc, IDirectSoundBuffer **buff, IUnknown *unk) {
	if (!con_getvari(snd_mute_losefocus)) {
		((DSBUFFERDESC *)desc)->dwFlags |= DSBCAPS_GLOBALFOCUS;
	}
	return orig_CreateSoundBuffer(this, desc, buff, unk);
}

PREINIT {
	if (con_findvar("snd_mute_losefocus")) return FEAT_SKIP;
	con_regvar(snd_mute_losefocus);
	return FEAT_OK;
}

INIT {
	skiprestart = sst_earlyloaded; // see above
	IDirectSound *ds_obj = 0;
	if_cold (DirectSoundCreate(0, &ds_obj, 0) != DS_OK) {
		// XXX: can this error be usefully stringified?
		errmsg_errorx("couldn't create IDirectSound instance");
		return FEAT_OK;
	}
	ds_vt = ds_obj->lpVtbl;
	ds_obj->lpVtbl->Release(ds_obj);
	if_cold (!os_mprot(&ds_vt->CreateSoundBuffer, sizeof(void *),
			PAGE_READWRITE)) {
		errmsg_errorsys("couldn't make virtual table writable");
		return FEAT_OK;
	}
	orig_CreateSoundBuffer = ds_vt->CreateSoundBuffer;
	ds_vt->CreateSoundBuffer = &hook_CreateSoundBuffer;

	con_unhide(&snd_mute_losefocus->base);
	struct con_cmd *snd_restart = con_findcmd("snd_restart");
	if_hot (snd_restart) {
		snd_restart_cb = con_getcmdcbv1(snd_restart);
		snd_mute_losefocus->cb = &losefocuscb;
	}
	else {
		errmsg_warnx("couldn't find snd_restart");
		errmsg_note("changing snd_mute_losefocus will require changing other "
				"audio settings or restarting the game with SST autoloaded in "
				"order to have an effect");
	}
	return FEAT_OK;
}

END {
	ds_vt->CreateSoundBuffer = orig_CreateSoundBuffer;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
