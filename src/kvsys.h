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

#ifndef INC_KVSYS_H
#define INC_KVSYS_H

#include "intdefs.h"

struct KeyValues {
	int keyname;
	char *strval;
	ushort *wstrval;
	union {
		int ival;
		float fval;
		void *pval;
	};
	char datatype;
	bool hasescapes;
	bool evalcond;
	//char unused;
	union {
		struct {
			struct KeyValues *next, *child, *chain;
		} v1;
		struct {
			void *kvsys;
			bool haskvsys; // wasting 3 bytes for no reason, brilliant
			struct KeyValues *next, *child, *chain;
		} v2;
	};
	// this was supposedly added here at some point but we don't use it:
	// typedef bool (*GetSymbolProc_t)(const char *pKey);
	// GetSymbolProc_t m_pExpressionGetSymbolProc;
};

/* Wraps the engine IKeyValuesSystem::GetStringForSymbol() call. */
const char *kvsys_symtostr(int sym);

/* Wraps the engine IKeyValuesSystem::GetSymbolForString() call. */
int kvsys_strtosym(const char *s);

/* Finds a subkey based on its interned name (via kvsys_strtosym() above) */
struct KeyValues *kvsys_getsubkey(struct KeyValues *kv, int sym);

/*
 * Gets the string value of the KV object, or null if it doesn't have one.
 * IMPORTANT: currently does not automatically coerce types like the engine
 * does. This can be added later if actually required.
 */
const char *kvsys_getstrval(const struct KeyValues *kv);

/* Free a KV object and all its subkeys. */
void kvsys_free(struct KeyValues *kv);

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
