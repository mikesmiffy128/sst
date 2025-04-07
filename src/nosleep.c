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

#include "con_.h"
#include "engineapi.h"
#include "errmsg.h"
#include "feature.h"
#include "gamedata.h"
#include "hook.h"
#include "langext.h"
#include "mem.h"
#include "os.h"
#include "vcall.h"

FEATURE("inactive window sleep adjustment")
REQUIRE_GAMEDATA(vtidx_SleepUntilInput)
REQUIRE_GLOBAL(inputsystem)

DEF_CVAR_UNREG(engine_no_focus_sleep,
		"Delay while tabbed out (SST reimplementation)", 50,
		CON_ARCHIVE | CON_HIDDEN)

static void **vtable;

typedef void (*VCALLCONV SleepUntilInput_func)(void *this, int timeout);
static SleepUntilInput_func orig_SleepUntilInput;
static void VCALLCONV hook_SleepUntilInput(void *this, int timeout) {
	orig_SleepUntilInput(this, con_getvari(engine_no_focus_sleep));
}

PREINIT {
	if (con_findvar("engine_no_focus_sleep")) return false;
	con_regvar(engine_no_focus_sleep);
	return true;
}

INIT {
	vtable = mem_loadptr(inputsystem);
	if_cold (!os_mprot(vtable + vtidx_SleepUntilInput, sizeof(void *),
			PAGE_READWRITE)) {
		errmsg_errorx("couldn't make virtual table writable");
		return FEAT_FAIL;
	}
	orig_SleepUntilInput = (SleepUntilInput_func)hook_vtable(vtable,
			vtidx_SleepUntilInput, (void *)&hook_SleepUntilInput);
	engine_no_focus_sleep->base.flags &= ~CON_HIDDEN;
	return FEAT_OK;
}

END {
	unhook_vtable(vtable, vtidx_SleepUntilInput, (void *)orig_SleepUntilInput);
}

// vi: sw=4 ts=4 noet tw=80 cc=80
