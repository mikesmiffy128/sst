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

#include <stdbool.h> // used in generated code
#include <string.h> // "

#include "engineapi.h"
#include "gamedata.h"
#include "gametype.h"
#include "intdefs.h"
#include "mem.h" // "
#include "vcall.h"

u64 _gametype_tag = 0; // declared in gametype.h but seems sensible enough here

ifacefactory factory_client = 0, factory_server = 0, factory_engine = 0,
		factory_inputsystem = 0;

struct VEngineClient *engclient;
struct VEngineServer *engserver;

// this seems to be very stable, thank goodness
DECL_VFUNC(void *, GetGlobalVars, 1)
void *globalvars;

DECL_VFUNC_DYN(void *, GetAllServerClasses)
DECL_VFUNC_DYN(int, GetEngineBuildNumber)

#include <entpropsinit.gen.h>

void engineapi_init(void) {
	if (engclient = factory_engine("VEngineClient015", 0)) {
		_gametype_tag |= _gametype_tag_Client015;
	}
	else if (engclient = factory_engine("VEngineClient014", 0)) {
		_gametype_tag |= _gametype_tag_Client014;
	}
	else if (engclient = factory_engine("VEngineClient013", 0)) {
		_gametype_tag |= _gametype_tag_Client013;
	}
	else if (engclient = factory_engine("VEngineClient012", 0)) {
		_gametype_tag |= _gametype_tag_Client012;
	}

	if (engserver = factory_engine("VEngineServer021", 0)) {
		_gametype_tag |= _gametype_tag_Server021;
	}
	// else if (engserver = others as needed...) {
	// }

	void *pim = factory_server("PlayerInfoManager002", 0);
	if (pim) globalvars = VCALL(pim, GetGlobalVars);

	void *srvdll;
	// TODO(compat): add this back when there's gamedata for 009 (no point atm)
	/*if (srvdll = factory_engine("ServerGameDLL009", 0)) {
		_gametype_tag |= _gametype_tag_SrvDLL009;
	}*/
	if (srvdll = factory_server("ServerGameDLL005", 0)) {
		_gametype_tag |= _gametype_tag_SrvDLL005;
	}

	// need to do this now; ServerClass network table iteration requires
	// SendProp offsets
	gamedata_init();

	// TODO(compat): we need this terrible hack for now because TLS somehow
	// changed the entity vtable layout and I've yet to think of a way to make
	// gamedata more flexible to handle that properly. I blame JAiZ.
	if (engclient && has_vtidx_GetEngineBuildNumber &&
			VCALL(engclient, GetEngineBuildNumber) >= 2200) {
		++vtidx_Teleport;
	}

	if (has_vtidx_GetAllServerClasses && has_sz_SendProp &&
			has_off_SP_varname && has_off_SP_offset) {
		initentprops(VCALL(srvdll, GetAllServerClasses));
	}
}

// vi: sw=4 ts=4 noet tw=80 cc=80
