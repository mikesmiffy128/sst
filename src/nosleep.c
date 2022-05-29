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

#include "con_.h"
#include "engineapi.h"
#include "gamedata.h"
#include "hook.h"
#include "os.h"
#include "vcall.h"

DEF_CVAR_UNREG(engine_no_focus_sleep,
		"Delay while tabbed out (SST reimplementation)", 50,
		CON_ARCHIVE | CON_HIDDEN)

static void **vtable;

typedef void (*VCALLCONV SleepUntilInput_func)(void *this, int timeout);
static SleepUntilInput_func orig_SleepUntilInput;
static void VCALLCONV hook_SleepUntilInput(void *this, int timeout) {
	orig_SleepUntilInput(this, con_getvari(engine_no_focus_sleep));
}

bool nosleep_init(void) {
	struct con_var *v = con_findvar("engine_no_focus_sleep");
	if (v) return false; // no need!
	con_reg(engine_no_focus_sleep);
	// TODO(featgen): auto-check these factories
	if (!factory_inputsystem) {
		con_warn("nosleep: missing required factories\n");
		return false;
	}
	if (!has_vtidx_SleepUntilInput) {
		con_warn("nosleep: missing gamedata entries for this engine\n");
		return false;
	}
	void *insys = factory_inputsystem("InputSystemVersion001", 0);
	if (!insys) {
		con_warn("nosleep: couldn't get input system interface\n");
		return false;
	}
	vtable = *(void ***)insys;
	if (!os_mprot(vtable + vtidx_SleepUntilInput, sizeof(void *),
			PAGE_READWRITE)) {
		con_warn("nosleep: couldn't make memory writable\n");
		return false;
	}
	orig_SleepUntilInput = (SleepUntilInput_func)hook_vtable(vtable,
			vtidx_SleepUntilInput, (void *)&hook_SleepUntilInput);
	engine_no_focus_sleep->base.flags &= ~CON_HIDDEN;
	return true;
}

void nosleep_end(void) {
	unhook_vtable(vtable, vtidx_SleepUntilInput, (void *)orig_SleepUntilInput);
}

// vi: sw=4 ts=4 noet tw=80 cc=80
