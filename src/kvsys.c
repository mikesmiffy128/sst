/*
 * Copyright © 2023 Michael Smith <mikesmiffy128@gmail.com>
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

FEATURE()

IMPORT void *KeyValuesSystem(void); // vstlib symbol
static void *kvs;
DECL_VFUNC(int, GetSymbolForString, 3, const char *, bool)
DECL_VFUNC(const char *, GetStringForSymbol, 4, int)

const char *kvsys_symtostr(int sym) { return GetStringForSymbol(kvs, sym); }
int kvsys_strtosym(const char *s) { return GetSymbolForString(kvs, s, true); }

struct KeyValues *kvsys_getsubkey(struct KeyValues *kv, int sym) {
	for (kv = kv->child; kv; kv = kv->next) if (kv->keyname == sym) return kv;
	return 0;
}

// this is trivial for now, but may need expansion later; see header comment
const char *kvsys_getstrval(struct KeyValues *kv) { return kv->strval; }

void kvsys_free(struct KeyValues *kv) {
	while (kv) {
		kvsys_free(kv->child);
		struct KeyValues *next = kv->next;
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

INIT {
	kvs = KeyValuesSystem();
	// NOTE: this is technically redundant for early versions but I CBA writing
	// a version check; it's easier to just do this unilaterally.
	if (GAMETYPE_MATCHES(L4D2x)) {
		void **kvsvt = mem_loadptr(kvs);
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
		unhook_vtable(*(void ***)kvs, 4, (void *)orig_GetStringForSymbol);
	}
}

// vi: sw=4 ts=4 noet tw=80 cc=80
