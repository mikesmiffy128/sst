/*
 * Copyright © 2023 Matthew Wozniak <sirtomato999@gmail.com>
 * Copyright © 2025 Michael Smith <mikesmiffy128@gmail.com>
 * Copyright © 2023 Willian Henrique <wsimanbrazil@yahoo.com.br>
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

#include <stdlib.h>

#include "con_.h"
#include "engineapi.h"
#include "errmsg.h"
#include "gamedata.h"
#include "gametype.h"
#include "feature.h"
#include "hook.h"
#include "intdefs.h"
#include "langext.h"
#include "mem.h"
#include "os.h"
#include "ppmagic.h"
#include "sst.h"
#include "x86.h"
#include "x86util.h"

FEATURE()
// currently only used for l4d quick reset stuff, and conflicts with SPT's
// tas_pause hook. so, disable for non-L4D games for now, to be polite.
// TODO(compat): come up with a real solution for this if/when required
GAMESPECIFIC(L4Dbased)
REQUIRE_GAMEDATA(vtidx_RunFrame)
REQUIRE_GAMEDATA(vtidx_Frame)
REQUIRE_GAMEDATA(vtidx_GetRealTime)
REQUIRE_GAMEDATA(vtidx_HostFrameTime)

typedef void (*Host_AccumulateTime_func)(float dt);
static Host_AccumulateTime_func orig_Host_AccumulateTime;
static float *realtime, *host_frametime;

static float skiptime = 0.0, skiprate;
static void hook_Host_AccumulateTime(float dt) {
	float skipinc = skiprate * dt;
	if_hot (!skiptime) {
		orig_Host_AccumulateTime(dt);
		return;
	}
	if_random (skiptime <= skipinc) skipinc = skiptime; // should become fcmovbe
	skiptime -= skipinc;
	*realtime += skipinc;
	*host_frametime = skipinc;
}

void fastfwd(float seconds, float timescale) {
	skiptime = seconds;
	skiprate = timescale;
}

void fastfwd_add(float seconds, float timescale) {
	skiptime += seconds;
	skiprate = timescale;
}

static inline void *find_eng(void *runframe) {
#ifdef _WIN32
	const uchar *insns = (const uchar *)runframe;
	// RunFrame() first calls a virtual function on `eng`, the CEngine global.
	// Look for the load of `this` into ECX.
	for (const uchar *p = insns; p - insns < 32;) {
		// mov ecx, dword ptr [this]
		if (p[0] == X86_MOVRMW && p[1] == X86_MODRM(0, 1, 5)) {
			void **indirect = mem_loadptr(p + 2);
			return *indirect;
		}
		NEXT_INSN(p, "eng global object");
	}
#else
#warning TODO(linux): yet another assembly thing
#endif
	return 0;
}

static inline void *find_HostState_Frame(void *Frame) {
#ifdef _WIN32
	// Frame() calls HostState_Frame in a small switch which the compiler just
	// turns into a conditional branch. Find a cmp with a call after it.
	const uchar *insns = (const uchar *)Frame;
	for (const uchar *p = insns; p - insns < 640;) {
		if (p[0] == X86_ALUMI8S && (p[1] & 0x38) == X86_MODRM(0, 7, 0) &&
				p[2] == 2) {
			NEXT_INSN(p, "HostState_Frame");
			while (p - insns < 640) {
				if (p[0] == X86_CALL) {
					return (uchar *)p + 5 + mem_loads32(p + 1);
				}
				NEXT_INSN(p, "HostState_Frame");
			}
			return 0;
		}
		NEXT_INSN(p, "HostState_Frame");
	}
#else
#warning TODO(linux): yet another assembly thing
#endif
	return 0;
}

static inline void *find_FrameUpdate(void *HostState_Frame) {
#ifdef _WIN32
	// HostState_Frame() calls another non-virtual member function (FrameUpdate)
	const uchar *insns = (const uchar *)HostState_Frame;
	for (const uchar *p = insns; p - insns < 384;) {
		if (p[0] == X86_CALL) return (uchar *)p + 5 + mem_loads32(p + 1);
		NEXT_INSN(p, "CHostState::FrameUpdate");
	}
#else
#warning TODO(linux): yet another assembly thing
#endif
	return 0;
}

static inline bool find_Host_AccumulateTime(void *_Host_RunFrame) {
#ifdef _WIN32
	const uchar *insns = (const uchar *)_Host_RunFrame;
	for (const uchar *p = insns; p - insns < 384;) {
		if (p[0] == X86_FLTBLK2 && p[1] == X86_MODRM(1, 0, 5) && p[2] == 8) {
			NEXT_INSN(p, "Host_AccumulateTime");
			while (p - insns < 384) {
				if (p[0] == X86_CALL) {
					orig_Host_AccumulateTime = (Host_AccumulateTime_func)(
							p + 5 + mem_loads32(p + 1));
					return true;
				}
				NEXT_INSN(p, "Host_AccumulateTime");
			}
			return false;
		}
		NEXT_INSN(p, "Host_AccumulateTime");
	}
	return false;
#else
#warning TODO(linux): yet another assembly thing
#endif
}

// we can find some float globals with functions that return them immediately
// FLD dword ptr [floatvar]
static inline float *find_float(void *func) {
	// TODO(linux): this one might be okay - but still check!
	const uchar *insn = (const uchar *)func;
	if (insn[0] != X86_FLTBLK2 || insn[1] != 0x05) return 0;
	else return mem_loadptr(insn + 2);
}

// a few layers of the call stack have the target function take a float arg,
// so we can look for a particular number of FLD instructions followed by a CALL
// and then grab the function from that
static void *find_floatcall(void *func, int fldcnt, const char *name) {
	// TODO(linux): likewise this has a chance of working, but needs testing
	const uchar *insns = (const uchar *)func;
	for (const uchar *p = insns; p - insns < 384;) {
		if (p[0] == X86_FLTBLK2 && (p[1] & 0x38) == 0) {
			NEXT_INSN(p, name);
			while (p - insns < 384) {
				if (p[0] == X86_CALL) {
					if (!--fldcnt) return (uchar *)p + 5 + mem_loads32(p + 1);
					goto next;
				}
				NEXT_INSN(p, name);
			}
			return 0;
		}
next:	NEXT_INSN(p, name);
	}
	return 0;
}

INIT {
	void *hldsapi = factory_engine("VENGINE_HLDS_API_VERSION002", 0);
	if_cold (!hldsapi) {
		errmsg_errorx("couldn't find HLDS API interface");
		return FEAT_INCOMPAT;
	}
	void *enginetool = factory_engine("VENGINETOOL003", 0);
	if_cold (!enginetool) {
		errmsg_errorx("missing engine tool interface");
		return FEAT_INCOMPAT;
	}
	// behold: the greatest pointer chase of all time
	realtime = find_float((*(void ***)enginetool)[vtidx_GetRealTime]);
	if_cold (!realtime) {
		errmsg_errorx("couldn't find realtime variable");
		return FEAT_INCOMPAT;
	}
	host_frametime = find_float((*(void ***)enginetool)[vtidx_HostFrameTime]);
	if_cold (!host_frametime) {
		errmsg_errorx("couldn't find host_frametime variable");
		return FEAT_INCOMPAT;
	}
	void *eng = find_eng((*(void ***)hldsapi)[vtidx_RunFrame]);
	if_cold (!eng) {
		errmsg_errorx("couldn't find eng global object");
		return FEAT_INCOMPAT;
	}
	void *func;
	if_cold (!(func = find_HostState_Frame((*(void ***)eng)[vtidx_Frame]))) {
		errmsg_errorx("couldn't find HostState_Frame function");
		return FEAT_INCOMPAT;
	}
	if_cold (!(func = find_FrameUpdate(func))) {
		errmsg_errorx("couldn't find FrameUpdate function");
		return FEAT_INCOMPAT;
	}
	if_cold (!(func = find_floatcall(func, GAMETYPE_MATCHES(L4D2_2147plus) ?
			2 : 1, "CHostState::State_Run"))) {
		errmsg_errorx("couldn't find State_Run function");
		return FEAT_INCOMPAT;
	}
	if_cold (!(func = find_floatcall(func, 1, "Host_RunFrame"))) {
		errmsg_errorx("couldn't find Host_RunFrame function");
		return FEAT_INCOMPAT;
	}
	if_cold (!(func = find_floatcall(func, 1, "_Host_RunFrame"))) {
		errmsg_errorx("couldn't find _Host_RunFrame");
		return FEAT_INCOMPAT;
	}
	if_cold (!find_Host_AccumulateTime(func)) {
		errmsg_errorx("couldn't find Host_AccumulateTime");
		return FEAT_INCOMPAT;
	}
	orig_Host_AccumulateTime = (Host_AccumulateTime_func)hook_inline(
			(void *)orig_Host_AccumulateTime, (void *)hook_Host_AccumulateTime);
	if_cold (!orig_Host_AccumulateTime) {
		errmsg_errorsys("couldn't hook Host_AccumulateTime function");
		return FEAT_FAIL;
	}
	return FEAT_OK;
}

END {
	if_hot (!sst_userunloaded) return;
	unhook_inline((void *)orig_Host_AccumulateTime);
}

// vi: sw=4 ts=4 noet tw=80 cc=80
