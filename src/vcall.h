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

// black magic argument list maker thingmy, similar to the PPMAGIC_MAP thing,
// but slightly different, so we get to have all this nonsense twice. not sorry

// note: arg numbering is counting down instead of up because it's easier to
// treat the vararg macros like a stack, and doesn't actually matter otherwise
// also note: I just did 16. that should be enough
#define _VCALL_ARG00()
#define _VCALL_ARG01(t) typeof(t) a1
#define _VCALL_ARG02(t1, t2) typeof(t1) a2, typeof(t2) a1
#define _VCALL_ARG03(t1, t2, t3) typeof(t1) a3, typeof(t2) a2, typeof(t3) a1
#define _VCALL_ARG04(t1, t2, t3, t4) \
	typeof(t1) a4, typeof(t2) a3, typeof(t3) a2, typeof(t4) a1
#define _VCALL_ARG05(t, ...) typeof(t) a5, _VCALL_ARG04(__VA_ARGS__)
#define _VCALL_ARG06(t, ...) typeof(t) a6, _VCALL_ARG05(__VA_ARGS__)
#define _VCALL_ARG07(t, ...) typeof(t) a7, _VCALL_ARG06(__VA_ARGS__)
#define _VCALL_ARG08(t, ...) typeof(t) a8, _VCALL_ARG07(__VA_ARGS__)
#define _VCALL_ARG09(t, ...) typeof(t) a9, _VCALL_ARG08(__VA_ARGS__)
#define _VCALL_ARG10(t, ...) typeof(t) a10, _VCALL_ARG09(__VA_ARGS__)
#define _VCALL_ARG11(t, ...) typeof(t) a11, _VCALL_ARG10(__VA_ARGS__)
#define _VCALL_ARG12(t, ...) typeof(t) a12, _VCALL_ARG11(__VA_ARGS__)
#define _VCALL_ARG13(t, ...) typeof(t) a13, _VCALL_ARG12(__VA_ARGS__)
#define _VCALL_ARG14(t, ...) typeof(t) a14, _VCALL_ARG14(__VA_ARGS__)
#define _VCALL_ARG15(t, ...) typeof(t) a15, _VCALL_ARG15(__VA_ARGS__)
#define _VCALL_ARG16(t, ...) typeof(t) a16, _VCALL_ARG16(__VA_ARGS__)

#define _VCALL_ARG_N(x01, x02, x03, x04, x05, x06, x07, x08, x09, x10, \
		x11, x12, x13, x14, x15, x16, N, ...) \
	_VCALL_ARG##N

#define _VCALL_ARGLIST(...) \
	_VCALL_ARG_N(__VA_ARGS__ __VA_OPT__(,) \
		16, 15, 14, 13, 12, 11, 10, 09, 08, 07, 06, 05, 04, 03, 02, 01, 00) \
	(__VA_ARGS__)

// aannd we need these as well...
#define _VCALL_PASS00()
#define _VCALL_PASS01() a1
#define _VCALL_PASS02() a2, a1
#define _VCALL_PASS03() a3, a2, a1
#define _VCALL_PASS04() a4, a3, a2, a1
#define _VCALL_PASS05() a5, _VCALL_PASS04()
#define _VCALL_PASS06() a6, _VCALL_PASS05()
#define _VCALL_PASS07() a7, _VCALL_PASS06()
#define _VCALL_PASS08() a8, _VCALL_PASS07()
#define _VCALL_PASS09() a9, _VCALL_PASS08()
#define _VCALL_PASS10() a10, _VCALL_PASS09()
#define _VCALL_PASS11() a11, _VCALL_PASS10()
#define _VCALL_PASS12() a12, _VCALL_PASS11()
#define _VCALL_PASS13() a13, _VCALL_PASS12()
#define _VCALL_PASS14() a14, _VCALL_PASS13()
#define _VCALL_PASS15() a15, _VCALL_PASS14()
#define _VCALL_PASS16() a16, _VCALL_PASS15()
#define _VCALL_PASS_N(x01, x02, x03, x04, x05, x06, x07, x08, x09, x10, x11, \
		x12, x13, x14, x15, x16, N, ...) \
	_VCALL_PASS##N

#define _VCALL_PASSARGS(...) \
	_VCALL_PASS_N(__VA_ARGS__ __VA_OPT__(,) 16, 15, 14, 13, 12, 11, 10, 09, \
			08, 07, 06, 05, 04, 03, 02, 01, 00)()

#define VFUNC(x, name) ((*(name##_func **)(x))[vtidx_##name])
#define VCALL(x, name, ...) VFUNC(x, name)(x, ##__VA_ARGS__)

// even more magic: return keyword only if not void
#define _VCALL_RETKW_(x, n, ...) n
#define _VCALL_RETKW(...) _VCALL_RETKW_(__VA_ARGS__, return,)
#define _VCALL_RET_void() x, ,
#define _VCALL_RET(t) _VCALL_RETKW(_VCALL_RET_##t())

// I thought static inline was supposed to prevent unused warnings???
#if defined(__GNUC__) || defined(__clang__)
#define _VCALL_UNUSED __attribute((unused))
#else
#define _VCALL_UNUSED
#endif

#define _DECL_VFUNC_DYN(class, ret, conv, name, ...) \
	typedef typeof(ret) (*conv name##_func)(typeof(class) * __VA_OPT__(,) \
			__VA_ARGS__); \
	static inline _VCALL_UNUSED typeof(ret) name( \
			typeof(class) *this __VA_OPT__(,) _VCALL_ARGLIST(__VA_ARGS__)) { \
		_VCALL_RET(ret) VCALL(this, name __VA_OPT__(,) \
				_VCALL_PASSARGS(__VA_ARGS__)); \
	}
#define _DECL_VFUNC(class, ret, conv, name, idx, ...) \
	enum { vtidx_##name = (idx) }; \
	_DECL_VFUNC_DYN(class, ret, conv, name __VA_OPT__(,) __VA_ARGS__)

/* Define a virtual function with a known index. */
#define DECL_VFUNC(class, ret, name, idx, ...) \
	_DECL_VFUNC(class, ret, VCALLCONV, name, idx __VA_OPT__(,) __VA_ARGS__)

/* Define a virtual function with a known index, without thiscall convention */
#define DECL_VFUNC_CDECL(class, ret, name, idx, ...) \
	_DECL_VFUNC(class, ret, , name, idx __VA_OPT__(,) __VA_ARGS__)

/* Define a virtual function with an index defined elsewhere (e.g. gamedata) */
#define DECL_VFUNC_DYN(class, ret, name, ...) \
	_DECL_VFUNC_DYN(class, ret, VCALLCONV, name __VA_OPT__(,) __VA_ARGS__)

/* Define a virtual function with an index defined elsewhere, without thiscall */
#define DECL_VFUNC_CDECLDYN(class, ret, name, ...) \
	_DECL_VFUNC_DYN(class, void, ret, , name __VA_OPT__(,) __VA_ARGS__)

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
