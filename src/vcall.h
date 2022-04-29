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

#ifndef INC_VCALL_H
#define INC_VCALL_H

#include "gamedata.h"

/*
 * Convenient facilities for calling simple (single-table) virtual functions on
 * possibly-opaque pointers to C++ objects.
 */

#ifdef _WIN32
#if defined(__GNUC__) || defined(__clang__)
#define VCALLCONV __thiscall
#else
// XXX: could support MSVC via __fastcall and dummy param, but is there a point?
#error C __thiscall support requires Clang or GCC
#endif
#else
#define VCALLCONV
#endif

#define _DECL_VFUNC_DYN(ret, conv, name, ...) \
	/* XXX: GCC extension, seems worthwhile vs having two macros for one thing.
	   Replace with __VA_OPT__(,) whenever that gets fully standardised. */ \
	typedef ret (*conv name##_func)(void *this, ##__VA_ARGS__);
#define _DECL_VFUNC(ret, conv, name, idx, ...) \
	enum { vtidx_##name = (idx) }; \
	_DECL_VFUNC_DYN(ret, conv, name, ##__VA_ARGS__)

/* Define a virtual function with a known index */
#define DECL_VFUNC(ret, name, idx, ...) \
	_DECL_VFUNC(ret, VCALLCONV, name, idx, ##__VA_ARGS__)

/* Define a virtual function with a known index, without thiscall convention */
#define DECL_VFUNC_CDECL(ret, name, idx, ...) \
	_DECL_VFUNC(ret, , name, idx, ##__VA_ARGS__)

/* Define a virtual function with an index defined elsewhere */
#define DECL_VFUNC_DYN(ret, name, ...) \
	_DECL_VFUNC_DYN(ret, VCALLCONV, name, ##__VA_ARGS__)

/* Define a virtual function with an index defined elsewhere, without thiscall */
#define DECL_VFUNC_CDECLDYN(ret, name, ...) \
	_DECL_VFUNC_DYN(ret, , name, ##__VA_ARGS__)

#define VFUNC(x, name) ((*(name##_func **)(x))[vtidx_##name])
#define VCALL(x, name, ...) VFUNC(x, name)(x, ##__VA_ARGS__)

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
