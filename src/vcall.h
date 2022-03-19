/*
 * Copyright © 2021 Michael Smith <mikesmiffy128@gmail.com>
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

#define DECL_VFUNC0(ret, name, idx) \
	enum { _VTIDX_##name = (idx) }; \
	typedef ret (*VCALLCONV _VFUNC_##name)(void *this);

#define DECL_VFUNC(ret, name, idx, ...) \
	enum { _VTIDX_##name = (idx) }; \
	typedef ret (*VCALLCONV _VFUNC_##name)(void *this, __VA_ARGS__);

// not bothering to provide a zero-argument version because the main use of
// this is vararg functions, which error if __thiscall
#define DECL_VFUNC_CDECL(ret, name, idx, ...) \
	enum { _VTIDX_##name = (idx) }; \
	typedef ret (*_VFUNC_##name)(void *this, __VA_ARGS__);

#define VFUNC(x, name) ((*(_VFUNC_##name **)(x))[_VTIDX_##name])

#define VCALL0(x, name) (VFUNC(x, name)(x))
#define VCALL(x, name, ...) VFUNC(x, name)(x, __VA_ARGS__)

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
