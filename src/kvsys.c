/*
 * Copyright © 2023 Michael Smith <mikesmiffy128@gmail.com>
 * Copyright © 2024 Willian Henrique <wsimanbrazil@yahoo.com.br>
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
#include "extmalloc.h"
#include "errmsg.h"
#include "feature.h"
#include "gametype.h"
#include "hook.h"
#include "kvsys.h"
#include "mem.h"
#include "os.h"
#include "vcall.h"
#include "x86.h"

FEATURE()

IMPORT void *KeyValuesSystem(void); // vstlib symbol
static void *kvs;
static int vtidx_GetSymbolForString = 3, vtidx_GetStringForSymbol = 4;
static bool iskvv2 = false;
DECL_VFUNC_DYN(int, GetSymbolForString, const char *, bool)
DECL_VFUNC_DYN(const char *, GetStringForSymbol, int)

const char *kvsys_symtostr(int sym) { return GetStringForSymbol(kvs, sym); }
int kvsys_strtosym(const char *s) { return GetSymbolForString(kvs, s, true); }

struct KeyValues *kvsys_getsubkey(struct KeyValues *kv, int sym) {
	for (kv = iskvv2 ? kv->v2.child : kv->v1.child; kv;
			kv = iskvv2 ? kv->v2.next : kv->v1.next) {
		if (kv->keyname == sym) return kv;
	}
	return 0;
}

// this is trivial for now, but may need expansion later; see header comment
const char *kvsys_getstrval(const struct KeyValues *kv) { return kv->strval; }

void kvsys_free(struct KeyValues *kv) {
	while (kv) {
		kvsys_free(iskvv2 ? kv->v2.child : kv->v1.child);
		struct KeyValues *next = iskvv2 ? kv->v2.next : kv->v1.next;
		// NOTE! could (should?) call the free function in IKeyValuesSystem but
		// we instead assume pooling is compiled out in favour of the IMemAlloc
		// stuff, and thus call the latter directly for less overhead
		extfree(kv->strval); extfree(kv->wstrval);
		extfree(kv);
		kv = next;
	}
}

// HACK: later versions of L4D2 show an annoying dialog on every plugin_load.
// We can suppress this by catching the message string that's passed from
// engine.dll to gameui.dll through KeyValuesSystem in vstdlib.dll and just
// replacing it with some other arbitrary string that gameui won't match.
static GetStringForSymbol_func orig_GetStringForSymbol = 0;
static const char *VCALLCONV hook_GetStringForSymbol(void *this, int s) {
	const char *ret = orig_GetStringForSymbol(this, s);
	if (!strcmp(ret, "OnClientPluginWarning")) ret = "sstBlockedThisEvent";
	return ret;
}

// XXX: 5th instance of this in the codebase, should REALLY tidy up soon
#ifdef _WIN32
#define NVDTOR 1
#else
#define NVDTOR 2
#endif

static void detectabichange(void **kvsvt) {
	// When no virtual destructor is present, the 6th function in the KVS vtable
	// is AddKeyValuesToMemoryLeakList, which is a nop in release builds.
	// L4D2 2.2.3.8 (14th May 2024) adds a virtual destructor which pushes
	// everything down, so the 6th function is not a nop any more. This
	// coincides with changes to the KeyValues struct layout (see above).
	uchar *insns = kvsvt[5];
	// should be RETI16 on Windows (thiscall, callee cleanup) and RET on Linux
	// (cdecl, caller cleanup) but let's just check both to be thorough
	if (insns[0] != X86_RETI16 && insns[0] != X86_RET) {
		iskvv2 = true;
		vtidx_GetSymbolForString += NVDTOR;
		vtidx_GetStringForSymbol += NVDTOR;
	}
}

INIT {
	kvs = KeyValuesSystem();
	// NOTE: this hook is technically redundant for early versions but I CBA
	// writing a version check; it's easier to just do this unilaterally. The
	// kvs ABI check is probably relevant for other games, but none that we
	// currently actively support
	if (GAMETYPE_MATCHES(L4D2x)) {
		void **kvsvt = mem_loadptr(kvs);
		detectabichange(kvsvt);
		if (!os_mprot(kvsvt + vtidx_GetStringForSymbol, sizeof(void *),
				PAGE_READWRITE)) {
			errmsg_warnx("couldn't make KeyValuesSystem vtable writable");
			errmsg_note("won't be able to prevent any nag messages");
		}
		else {
			orig_GetStringForSymbol = (GetStringForSymbol_func)hook_vtable(
					kvsvt, vtidx_GetStringForSymbol,
					(void *)hook_GetStringForSymbol);
		}
	}
	return true;
}

END {
	if (orig_GetStringForSymbol) {
		unhook_vtable(*(void ***)kvs, vtidx_GetStringForSymbol,
				(void *)orig_GetStringForSymbol);
	}
}

// vi: sw=4 ts=4 noet tw=80 cc=80
