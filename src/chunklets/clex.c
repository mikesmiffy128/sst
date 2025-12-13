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

/* WARNING: this library is incomplete and still subject to change! */

// NYI:
// - numeric literal parsing
// - string/char literal parsing
// - more performance measurements
//     - if needed: more optimisation (target: >500MB/s give or take)
// - security/robustness tests (e.g. fuzzing)
// - UTF-8 aware column numbers (assuming this is what most tools do/expect?)

#ifdef __cplusplus
#error This file should not be compiled as C++. It relies on numerous C-only \
features.
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#if !defined(_MSVC_TRADITIONAL)
#error This version of MSVC is too old: upgrade to 2019 or newer and use \
`-std:c17`, or better yet use Clang.
#elif _MSVC_TRADITIONAL
#error This file must be compiled using `-std:c17` when using MSVC.
#endif
#endif

#ifndef _WIN32
_Static_assert(
	(unsigned char)-1 == 255 &&
	sizeof(short) == 2 &&
	sizeof(int) == 4 &&
	sizeof(long long) == 8 &&
	sizeof(void *) == 4 || sizeof(void *) == 8 &&
	sizeof(long) == sizeof(void *),
	"this code is only designed for relatively sane environments, plus Windows"
);
#endif

#ifndef __clang__ // see `#define copy` further down
#include <string.h>
#endif

#include "clex.h"

#if defined(__GNUC__) || defined(__clang__)
#define if_cold(x) if (__builtin_expect(!!(x), 0))
#else
#define if_cold(x) if (x)
#endif

#if defined(__GNUC__) || defined(__clang__)
#define cold __attribute__((cold, noinline))
#elif defined(_MSC_VER)
#define cold __declspec(noinline)
#else
#define cold
#endif

// duping this from clex.h because it's undef'd there to pollute a little less
#if defined(__GNUC__) || defined(__clang__)
#define unreachable __builtin_unreachable()
#elif defined(_MSC_VER)
#define unreachable __assume(0)
#else
static inline _Noreturn void _clex_invoke_ub(void) {}
#define unreachable (_clex_invoke_ub())
#endif

// note: a couple of these have been moved from seemingly more neat/logical
// positions in order to make statetoks below more compact. classes and state
// transitions use designated initialisers so this stuff can be moved around
// without ruining the state transitions. the visual grouping here shows how
// statetoks[] will be indexed, using halved state values.
#define STATES(S, s) \
	S(BOL) \
	s(OP1) s(WS) \
	s(OP2) s(WS_BS) \
	S(LITPFX) \
	S(uPFX) \
	S(WORD) \
	S(NUM) \
	S(MIGHTEQ) \
	S(MINUS) \
	S(PLUS) \
	S(AND) \
	S(OR) \
	S(HASH) \
	S(COLON) \
	S(FS) \
	S(LT) \
	S(GT) \
	S(DOT) \
	S(SHIFT) \
	S(LNCOMM) \
	s(BLKCOMM) s(BLKSTAR) \
	s(OPEQ) s(BLKSTAR_BS) \
	s(SHIFTEQ) s(STRLIT_BS_WS) /* < STUPID case, needed for gcc/clang compat! */ \
	s(STRLIT) s(STRLIT_BS) \
	s(CHRLIT) s(CHRLIT_BS) \
	s(ERROR) /* special case for bad syntax */ \
	s(CHRLIT_BS_WS) /* also stupid case */

#define ENUMSTATE(name) STATE_##name,
#define ENUMSTATEBS(name) STATE_##name, STATE_##name##_BS,
#define ENUMTRANS(name) STATE_TRANS_##name = STATE_##name << 1,
#define ENUMTRANSBS(name) \
		ENUMTRANS(name) STATE_TRANS_##name##_BS = STATE_##name##_BS << 1,
enum {
	STATES(ENUMSTATEBS, ENUMSTATE)
	NSTATES,
	STATES(ENUMTRANSBS, ENUMTRANS)
};
#undef SHIFTENUMBS
#undef SHIFTENUM
#undef ENUMSTATEBS
#undef ENUMSTATE

#define CLASSES(X) \
	X(OTHER) /* includes (most) alpha, underscores, anything unicode */ \
	X(LU) /* L and U (lit prefixes) */ \
	X(u) /* u prefix (could become u8) */ \
	X(DIGIT) /* excluding 8 */ \
	X(8) \
	/* ^^ those must be before symbols */ \
	X(OP1) /* 1 char ops/symbols */ \
	X(MIGHTEQ) /* chars that could be followed by = */ \
	X(MINUS) \
	X(PLUS) \
	X(EQ) \
	X(LT) \
	X(GT) \
	X(AND) \
	X(OR) \
	X(DOT) \
	X(STAR) \
	X(HASH) \
	X(COLON) \
	X(FS) \
	X(BS) \
	X(SQ) \
	X(DQ) \
	X(SP) \
	X(NL)

// premultiply class values for transition table.
#define CLASSBASEVALUE(name) CLASSBASEVAL_##name,
enum { CLASSES(CLASSBASEVALUE) NCLASSES };
#undef CLASSBASEVALUE
#define CLASSREALVALUE(name) CLASS_##name = CLASSBASEVAL_##name * NSTATES,
enum { CLASSES(CLASSREALVALUE) };
#undef CLASSREALVALUE

#define CLS(cls, c) [c] = CLASS_##cls,
#define CLS2(cls, c1, c2) CLS(cls, c1) CLS(cls, c2)
#define CLS3(cls, c1, ...) CLS(cls, c1) CLS2(cls, __VA_ARGS__)
#define CLS4(cls, c1, ...) CLS(cls, c1) CLS3(cls, __VA_ARGS__)
#define CLS5(cls, c1, ...) CLS(cls, c1) CLS4(cls, __VA_ARGS__)
#define CLS6(cls, c1, ...) CLS(cls, c1) CLS5(cls, __VA_ARGS__)
#define CLS7(cls, c1, ...) CLS(cls, c1) CLS6(cls, __VA_ARGS__)
#define CLS8(cls, c1, ...) CLS(cls, c1) CLS7(cls, __VA_ARGS__)
#define CLS9(cls, c1, ...) CLS(cls, c1) CLS8(cls, __VA_ARGS__)
#define CLS10(cls, c1, ...) CLS(cls, c1) CLS9(cls, __VA_ARGS__)

static const short classes[256] = {
	/*CLSX(OTHER, everything, else)*/
	CLS2(LU, 'L', 'U')
	CLS(u, 'u')
	CLS9(DIGIT, '0', '1', '2', '3', '4', '5', '6', '7', '9')
	CLS(8, '8')
	CLS10(OP1, '(', ')', '[', ']', '{', '}', ',', '?', ';', '~')
	CLS3(MIGHTEQ, '!', '^', '%')
	CLS(PLUS, '+')
	CLS(MINUS, '-')
	CLS(EQ, '=')
	CLS(LT, '<')
	CLS(GT, '>')
	CLS(AND, '&')
	CLS(OR, '|')
	CLS(DOT, '.')
	CLS(STAR, '*')
	CLS(HASH, '#')
	CLS(COLON, ':')
	CLS(FS, '/')
	CLS(BS, '\\')
	CLS(SQ, '\'')
	CLS(DQ, '"')
	CLS3(SP, ' ', '\t', '\r')
	CLS(NL, '\n')
};

#define FWD 1 // advance token index (i.e. create new token)
#define FIXPOS 128 // in \u cases, token starts before the u. fixup file offset

// abstracting transition table definition to allow fiddling with the layout.
#define TRANS(from, class, to) \
	[CLASS_##class + STATE_##from] = STATE_TRANS_##to,
#define ST(s, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11, s12, \
		s13, s14, s15, s16, s17, s18, s19, s20, s21, s22, s23, s24) \
	TRANS(s, OTHER, s1) \
	TRANS(s, LU, s2) \
	TRANS(s, u, s3) \
	TRANS(s, DIGIT, s4) \
	TRANS(s, 8, s5) \
	TRANS(s, OP1, s6) \
	TRANS(s, MIGHTEQ, s7) \
	TRANS(s, PLUS, s8) \
	TRANS(s, MINUS, s9) \
	TRANS(s, EQ, s10) \
	TRANS(s, LT, s11) \
	TRANS(s, GT, s12) \
	TRANS(s, AND, s13) \
	TRANS(s, OR, s14) \
	TRANS(s, DOT, s15) \
	TRANS(s, STAR, s16) \
	TRANS(s, HASH, s17) \
	TRANS(s, COLON, s18) \
	TRANS(s, FS, s19) \
	TRANS(s, BS, s20) \
	TRANS(s, SQ, s21) \
	TRANS(s, DQ, s22) \
	TRANS(s, SP, s23) \
	TRANS(s, NL, s24)

#define ST_BS(s) \
	ST(s##_BS, \
		/* OTHER	=> */ ERROR | FWD, \
		/* LU		=> */ WORD | FWD | FIXPOS, /* *could* be UCN (unlikely) */ \
		/* u		=> */ WORD | FWD | FIXPOS, /* same */ \
		/* DIGIT	=> */ ERROR | FWD, \
		/* 8		=> */ ERROR | FWD, \
		/* OP1		=> */ ERROR | FWD, \
		/* MIGHTEQ	=> */ ERROR | FWD, \
		/* PLUS		=> */ ERROR | FWD, \
		/* MINUS	=> */ ERROR | FWD, \
		/* EQ		=> */ ERROR | FWD, \
		/* LT		=> */ ERROR | FWD, \
		/* GT		=> */ ERROR | FWD, \
		/* AND		=> */ ERROR | FWD, \
		/* OR		=> */ ERROR | FWD, \
		/* DOT		=> */ ERROR | FWD, \
		/* STAR		=> */ ERROR | FWD, \
		/* HASH		=> */ ERROR | FWD, \
		/* COLON	=> */ ERROR | FWD, \
		/* FS		=> */ ERROR | FWD, \
		/* BS		=> */ ERROR | FWD, \
		/* SQ		=> */ ERROR | FWD, \
		/* DQ		=> */ ERROR | FWD, \
		/* SP		=> */ s##_BS, /* nonstandard crap done by GCC and Clang */ \
		/* NL		=> */ s \
	)

#define ST_TERM(s) /* "terminal" state (i.e. not mid-token) */ \
	ST(s, \
		/* OTHER	=> */ WORD | FWD, \
		/* LU		=> */ LITPFX | FWD, \
		/* u		=> */ uPFX | FWD, \
		/* DIGIT	=> */ NUM | FWD, \
		/* 8		=> */ NUM | FWD, \
		/* OP1		=> */ OP1 | FWD, \
		/* MIGHTEQ	=> */ MIGHTEQ | FWD, \
		/* PLUS		=> */ PLUS | FWD, \
		/* MINUS	=> */ MINUS | FWD, \
		/* EQ		=> */ MIGHTEQ | FWD, \
		/* LT		=> */ LT | FWD, \
		/* GT		=> */ GT | FWD, \
		/* AND		=> */ AND | FWD, \
		/* OR		=> */ OR | FWD, \
		/* DOT		=> */ DOT | FWD, \
		/* STAR		=> */ MIGHTEQ | FWD, \
		/* HASH		=> */ HASH | FWD, \
		/* COLON	=> */ COLON | FWD, \
		/* FS		=> */ FS | FWD, \
		/* BS		=> */ WS_BS, \
		/* SQ		=> */ CHRLIT | FWD, \
		/* DQ		=> */ STRLIT | FWD, \
		/* SP		=> */ s, \
		/* NL		=> */ BOL | FWD \
	)

// VERY long table {{{
static const unsigned char trans[NSTATES * NCLASSES] = {
	ST(BOL, // note: same as ST_TERM but without FWD so we don't repeat NLs
		/* OTHER	=> */ WORD | FWD,
		/* LU		=> */ LITPFX | FWD,
		/* u		=> */ uPFX | FWD,
		/* DIGIT	=> */ NUM | FWD,
		/* 8		=> */ NUM | FWD,
		/* OP1		=> */ OP1 | FWD,
		/* MIGHTEQ	=> */ MIGHTEQ | FWD,
		/* PLUS		=> */ PLUS | FWD,
		/* MINUS	=> */ MINUS | FWD,
		/* EQ		=> */ MIGHTEQ | FWD,
		/* LT		=> */ LT | FWD,
		/* GT		=> */ GT | FWD,
		/* AND		=> */ AND | FWD,
		/* OR		=> */ OR | FWD,
		/* DOT		=> */ DOT | FWD,
		/* STAR		=> */ MIGHTEQ | FWD,
		/* HASH		=> */ HASH | FWD,
		/* COLON	=> */ COLON | FWD,
		/* FS		=> */ FS | FWD,
		/* BS		=> */ BOL_BS,
		/* SQ		=> */ CHRLIT | FWD,
		/* DQ		=> */ STRLIT | FWD,
		/* SP		=> */ BOL,
		/* NL		=> */ BOL
	)
	ST(BOL_BS,
		/* OTHER	=> */ ERROR | FWD,
		/* LU		=> */ WORD | FWD | FIXPOS, // maybe UCN
		/* u		=> */ WORD | FWD | FIXPOS, // "
		/* DIGIT	=> */ ERROR | FWD,
		/* 8		=> */ ERROR | FWD,
		/* OP1		=> */ ERROR | FWD,
		/* MIGHTEQ	=> */ ERROR | FWD,
		/* PLUS		=> */ ERROR | FWD,
		/* MINUS	=> */ ERROR | FWD,
		/* EQ		=> */ ERROR | FWD,
		/* LT		=> */ ERROR | FWD,
		/* GT		=> */ ERROR | FWD,
		/* AND		=> */ ERROR | FWD,
		/* OR		=> */ ERROR | FWD,
		/* DOT		=> */ ERROR | FWD,
		/* STAR		=> */ ERROR | FWD,
		/* HASH		=> */ ERROR | FWD,
		/* COLON	=> */ ERROR | FWD,
		/* FS		=> */ ERROR | FWD,
		/* BS		=> */ WORD_BS | FWD | FIXPOS, // maybe UCN split over lines
		/* SQ		=> */ ERROR | FWD,
		/* DQ		=> */ ERROR | FWD,
		/* SP		=> */ BOL_BS,
		/* NL		=> */ BOL
	)
	ST_TERM(WS)
	ST(WS_BS,
		/* OTHER	=> */ ERROR | FWD,
		/* LU		=> */ WORD | FWD | FIXPOS, // maybe UCN
		/* u		=> */ WORD | FWD | FIXPOS, // "
		/* DIGIT	=> */ ERROR | FWD,
		/* 8		=> */ ERROR | FWD,
		/* OP1		=> */ ERROR | FWD,
		/* MIGHTEQ	=> */ ERROR | FWD,
		/* PLUS		=> */ ERROR | FWD,
		/* MINUS	=> */ ERROR | FWD,
		/* EQ		=> */ ERROR | FWD,
		/* LT		=> */ ERROR | FWD,
		/* GT		=> */ ERROR | FWD,
		/* AND		=> */ ERROR | FWD,
		/* OR		=> */ ERROR | FWD,
		/* DOT		=> */ ERROR | FWD,
		/* STAR		=> */ ERROR | FWD,
		/* HASH		=> */ ERROR | FWD,
		/* COLON	=> */ ERROR | FWD,
		/* FS		=> */ ERROR | FWD,
		/* BS		=> */ WORD_BS | FWD | FIXPOS, // maybe UCN split over liens
		/* SQ		=> */ ERROR | FWD,
		/* DQ		=> */ ERROR | FWD,
		/* SP		=> */ WS_BS,
		/* NL		=> */ WS
	)
	ST(uPFX,
		/* OTHER	=> */ WORD,
		/* LU		=> */ WORD,
		/* u		=> */ WORD,
		/* DIGIT	=> */ WORD,
		/* 8		=> */ LITPFX,
		/* OP1		=> */ OP1 | FWD,
		/* MIGHTEQ	=> */ MIGHTEQ | FWD,
		/* PLUS		=> */ PLUS | FWD,
		/* MINUS	=> */ MINUS | FWD,
		/* EQ		=> */ MIGHTEQ | FWD,
		/* LT		=> */ LT | FWD,
		/* GT		=> */ GT | FWD,
		/* AND		=> */ AND | FWD,
		/* OR		=> */ OR | FWD,
		/* DOT		=> */ DOT | FWD,
		/* STAR		=> */ MIGHTEQ | FWD,
		/* HASH		=> */ HASH | FWD,
		/* COLON	=> */ COLON | FWD,
		/* FS		=> */ FS | FWD,
		/* BS		=> */ uPFX_BS,
		/* SQ		=> */ CHRLIT,
		/* DQ		=> */ STRLIT,
		/* SP		=> */ WS,
		/* NL		=> */ BOL | FWD
	)
	ST(uPFX_BS,
		/* OTHER	=> */ ERROR | FWD,
		/* LU		=> */ WORD, // maybe UCN
		/* u		=> */ WORD, // "
		/* DIGIT	=> */ ERROR | FWD,
		/* 8		=> */ ERROR | FWD,
		/* OP1		=> */ ERROR | FWD,
		/* MIGHTEQ	=> */ ERROR | FWD,
		/* PLUS		=> */ ERROR | FWD,
		/* MINUS	=> */ ERROR | FWD,
		/* EQ		=> */ ERROR | FWD,
		/* LT		=> */ ERROR | FWD,
		/* GT		=> */ ERROR | FWD,
		/* AND		=> */ ERROR | FWD,
		/* OR		=> */ ERROR | FWD,
		/* DOT		=> */ ERROR | FWD,
		/* STAR		=> */ ERROR | FWD,
		/* HASH		=> */ ERROR | FWD,
		/* COLON	=> */ ERROR | FWD,
		/* FS		=> */ ERROR | FWD,
		/* BS		=> */ WORD_BS, // maybe UCN split over lines
		/* SQ		=> */ ERROR | FWD,
		/* DQ		=> */ ERROR | FWD,
		/* SP		=> */ uPFX_BS,
		/* NL		=> */ uPFX
	)
	ST(LITPFX,
		/* OTHER	=> */ WORD,
		/* LU		=> */ WORD,
		/* u		=> */ WORD,
		/* DIGIT	=> */ WORD,
		/* 8		=> */ WORD,
		/* OP1		=> */ OP1 | FWD,
		/* MIGHTEQ	=> */ MIGHTEQ | FWD,
		/* PLUS		=> */ PLUS | FWD,
		/* MINUS	=> */ MINUS | FWD,
		/* EQ		=> */ MIGHTEQ | FWD,
		/* LT		=> */ LT | FWD,
		/* GT		=> */ GT | FWD,
		/* AND		=> */ AND | FWD,
		/* OR		=> */ OR | FWD,
		/* DOT		=> */ DOT | FWD,
		/* STAR		=> */ MIGHTEQ | FWD,
		/* HASH		=> */ HASH | FWD,
		/* COLON	=> */ COLON | FWD,
		/* FS		=> */ FS | FWD,
		/* BS		=> */ LITPFX_BS,
		/* SQ		=> */ CHRLIT,
		/* DQ		=> */ STRLIT,
		/* SP		=> */ WS,
		/* NL		=> */ BOL | FWD
	)
	ST(LITPFX_BS,
		/* OTHER	=> */ ERROR | FWD,
		/* LU		=> */ WORD, // maybe UCN
		/* u		=> */ WORD, // "
		/* DIGIT	=> */ ERROR | FWD,
		/* 8		=> */ ERROR | FWD,
		/* OP1		=> */ ERROR | FWD,
		/* MIGHTEQ	=> */ ERROR | FWD,
		/* PLUS		=> */ ERROR | FWD,
		/* MINUS	=> */ ERROR | FWD,
		/* EQ		=> */ ERROR | FWD,
		/* LT		=> */ ERROR | FWD,
		/* GT		=> */ ERROR | FWD,
		/* AND		=> */ ERROR | FWD,
		/* OR		=> */ ERROR | FWD,
		/* DOT		=> */ ERROR | FWD,
		/* STAR		=> */ ERROR | FWD,
		/* HASH		=> */ ERROR | FWD,
		/* COLON	=> */ ERROR | FWD,
		/* FS		=> */ ERROR | FWD,
		/* BS		=> */ WORD_BS, // maybe UCN split over lines
		/* SQ		=> */ ERROR | FWD,
		/* DQ		=> */ ERROR | FWD,
		/* SP		=> */ LITPFX_BS,
		/* NL		=> */ LITPFX
	)
	ST(WORD,
		/* OTHER	=> */ WORD,
		/* LU		=> */ WORD,
		/* u		=> */ WORD,
		/* DIGIT	=> */ WORD,
		/* 8		=> */ WORD,
		/* OP1		=> */ OP1 | FWD,
		/* MIGHTEQ	=> */ MIGHTEQ | FWD,
		/* PLUS		=> */ PLUS | FWD,
		/* MINUS	=> */ MINUS | FWD,
		/* EQ		=> */ MIGHTEQ | FWD,
		/* LT		=> */ LT | FWD,
		/* GT		=> */ GT | FWD,
		/* AND		=> */ AND | FWD,
		/* OR		=> */ OR | FWD,
		/* DOT		=> */ DOT | FWD,
		/* STAR		=> */ MIGHTEQ | FWD,
		/* HASH		=> */ HASH | FWD,
		/* COLON	=> */ COLON | FWD,
		/* FS		=> */ FS | FWD,
		/* BS		=> */ WORD_BS,
		/* SQ		=> */ CHRLIT | FWD,
		/* DQ		=> */ STRLIT | FWD,
		/* SP		=> */ WS,
		/* NL		=> */ BOL | FWD
	)
	ST(WORD_BS,
		/* OTHER	=> */ ERROR | FWD,
		/* LU		=> */ WORD, // maybe UCN
		/* u		=> */ WORD, // "
		/* DIGIT	=> */ ERROR | FWD,
		/* 8		=> */ ERROR | FWD,
		/* OP1		=> */ ERROR | FWD,
		/* MIGHTEQ	=> */ ERROR | FWD,
		/* PLUS		=> */ ERROR | FWD,
		/* MINUS	=> */ ERROR | FWD,
		/* EQ		=> */ ERROR | FWD,
		/* LT		=> */ ERROR | FWD,
		/* GT		=> */ ERROR | FWD,
		/* AND		=> */ ERROR | FWD,
		/* OR		=> */ ERROR | FWD,
		/* DOT		=> */ ERROR | FWD,
		/* STAR		=> */ ERROR | FWD,
		/* HASH		=> */ ERROR | FWD,
		/* COLON	=> */ ERROR | FWD,
		/* FS		=> */ ERROR | FWD,
		/* BS		=> */ WORD_BS, // maybe UCN split over lines
		/* SQ		=> */ ERROR | FWD,
		/* DQ		=> */ ERROR | FWD,
		/* SP		=> */ WORD_BS,
		/* NL		=> */ WORD
	)
	ST(NUM,
		/* OTHER	=> */ NUM,
		/* LU		=> */ NUM,
		/* u		=> */ NUM,
		/* DIGIT	=> */ NUM,
		/* 8		=> */ NUM,
		/* OP1		=> */ OP1 | FWD,
		/* MIGHTEQ	=> */ MIGHTEQ | FWD,
		/* PLUS		=> */ PLUS | FWD,
		/* MINUS	=> */ MINUS | FWD,
		/* EQ		=> */ MIGHTEQ | FWD,
		/* LT		=> */ LT | FWD,
		/* GT		=> */ GT | FWD,
		/* AND		=> */ AND | FWD,
		/* OR		=> */ OR | FWD,
		/* DOT		=> */ NUM,
		/* STAR		=> */ MIGHTEQ | FWD,
		/* HASH		=> */ HASH | FWD,
		/* COLON	=> */ COLON | FWD,
		/* FS		=> */ FS | FWD,
		/* BS		=> */ NUM_BS,
		/* SQ		=> */ NUM, // C23
		/* DQ		=> */ STRLIT | FWD,
		/* SP		=> */ WS,
		/* NL		=> */ BOL | FWD
	)
	ST(NUM_BS,
		/* OTHER	=> */ ERROR | FWD,
		/* LU		=> */ ERROR | FWD, // UCN mid-number makes no sense
		/* u		=> */ ERROR | FWD,
		/* DIGIT	=> */ ERROR | FWD,
		/* 8		=> */ ERROR | FWD,
		/* OP1		=> */ ERROR | FWD,
		/* MIGHTEQ	=> */ ERROR | FWD,
		/* PLUS		=> */ ERROR | FWD,
		/* MINUS	=> */ ERROR | FWD,
		/* EQ		=> */ ERROR | FWD,
		/* LT		=> */ ERROR | FWD,
		/* GT		=> */ ERROR | FWD,
		/* AND		=> */ ERROR | FWD,
		/* OR		=> */ ERROR | FWD,
		/* DOT		=> */ ERROR | FWD,
		/* STAR		=> */ ERROR | FWD,
		/* HASH		=> */ ERROR | FWD,
		/* COLON	=> */ ERROR | FWD,
		/* FS		=> */ ERROR | FWD,
		/* BS		=> */ ERROR | FWD,
		/* SQ		=> */ ERROR | FWD,
		/* DQ		=> */ ERROR | FWD,
		/* SP		=> */ NUM_BS,
		/* NL		=> */ NUM
	)
	ST(MIGHTEQ,
		/* OTHER	=> */ WORD | FWD,
		/* LU		=> */ LITPFX | FWD,
		/* u		=> */ uPFX | FWD,
		/* DIGIT	=> */ NUM | FWD,
		/* 8		=> */ NUM | FWD,
		/* OP1		=> */ OP1 | FWD,
		/* MIGHTEQ	=> */ MIGHTEQ | FWD,
		/* PLUS		=> */ PLUS | FWD,
		/* MINUS	=> */ MINUS | FWD,
		/* EQ		=> */ OPEQ,
		/* LT		=> */ LT | FWD,
		/* GT		=> */ GT | FWD,
		/* AND		=> */ AND | FWD,
		/* OR		=> */ OR | FWD,
		/* DOT		=> */ DOT | FWD,
		/* STAR		=> */ MIGHTEQ | FWD,
		/* HASH		=> */ HASH | FWD,
		/* COLON	=> */ COLON | FWD,
		/* FS		=> */ FS | FWD,
		/* BS		=> */ MIGHTEQ_BS,
		/* SQ		=> */ CHRLIT | FWD,
		/* DQ		=> */ STRLIT | FWD,
		/* SP		=> */ WS,
		/* NL		=> */ BOL | FWD
	)
	ST_BS(MIGHTEQ)
	ST(MINUS,
		/* OTHER	=> */ WORD | FWD,
		/* LU		=> */ LITPFX | FWD,
		/* u		=> */ uPFX | FWD,
		/* DIGIT	=> */ NUM | FWD,
		/* 8		=> */ NUM | FWD,
		/* OP1		=> */ OP1 | FWD,
		/* MIGHTEQ	=> */ MIGHTEQ | FWD,
		/* PLUS		=> */ PLUS | FWD,
		/* MINUS	=> */ OP2,
		/* EQ		=> */ OPEQ,
		/* LT		=> */ LT | FWD,
		/* GT		=> */ OP2,
		/* AND		=> */ AND | FWD,
		/* OR		=> */ OR | FWD,
		/* DOT		=> */ DOT | FWD,
		/* STAR		=> */ MIGHTEQ | FWD,
		/* HASH		=> */ HASH | FWD,
		/* COLON	=> */ COLON | FWD,
		/* FS		=> */ FS | FWD,
		/* BS		=> */ MINUS_BS,
		/* SQ		=> */ CHRLIT | FWD,
		/* DQ		=> */ STRLIT | FWD,
		/* SP		=> */ WS,
		/* NL		=> */ BOL | FWD
	)
	ST_BS(MINUS)
	ST(PLUS,
		/* OTHER	=> */ WORD | FWD,
		/* LU		=> */ LITPFX | FWD,
		/* u		=> */ uPFX | FWD,
		/* DIGIT	=> */ NUM | FWD,
		/* 8		=> */ NUM | FWD,
		/* OP1		=> */ DOT | FWD,
		/* MIGHTEQ	=> */ MIGHTEQ | FWD,
		/* PLUS		=> */ OP2,
		/* MINUS	=> */ MINUS | FWD,
		/* EQ		=> */ OPEQ,
		/* LT		=> */ LT | FWD,
		/* GT		=> */ GT | FWD,
		/* AND		=> */ MIGHTEQ | FWD,
		/* OR		=> */ OR | FWD,
		/* DOT		=> */ DOT | FWD,
		/* STAR		=> */ MIGHTEQ | FWD,
		/* HASH		=> */ HASH | FWD,
		/* COLON	=> */ COLON | FWD,
		/* FS		=> */ FS | FWD,
		/* BS		=> */ PLUS_BS,
		/* SQ		=> */ CHRLIT | FWD,
		/* DQ		=> */ STRLIT | FWD,
		/* SP		=> */ WS,
		/* NL		=> */ BOL | FWD
	)
	ST_BS(PLUS)
	ST(AND,
		/* OTHER	=> */ WORD | FWD,
		/* LU		=> */ LITPFX | FWD,
		/* u		=> */ uPFX | FWD,
		/* DIGIT	=> */ NUM | FWD,
		/* 8		=> */ NUM | FWD,
		/* OP1		=> */ OP1 | FWD,
		/* MIGHTEQ	=> */ MIGHTEQ | FWD,
		/* PLUS		=> */ PLUS | FWD,
		/* MINUS	=> */ MINUS | FWD,
		/* EQ		=> */ OPEQ,
		/* LT		=> */ LT | FWD,
		/* GT		=> */ GT | FWD,
		/* AND		=> */ OP2,
		/* OR		=> */ OR | FWD,
		/* DOT		=> */ DOT | FWD,
		/* STAR		=> */ MIGHTEQ | FWD,
		/* HASH		=> */ HASH | FWD,
		/* COLON	=> */ COLON | FWD,
		/* FS		=> */ FS | FWD,
		/* BS		=> */ AND_BS,
		/* SQ		=> */ CHRLIT | FWD,
		/* DQ		=> */ STRLIT | FWD,
		/* SP		=> */ WS,
		/* NL		=> */ BOL | FWD
	)
	ST_BS(AND)
	ST(OR,
		/* OTHER	=> */ WORD | FWD,
		/* LU		=> */ LITPFX | FWD,
		/* u		=> */ uPFX | FWD,
		/* DIGIT	=> */ NUM | FWD,
		/* 8		=> */ NUM | FWD,
		/* OP1		=> */ OP1 | FWD,
		/* MIGHTEQ	=> */ MIGHTEQ | FWD,
		/* PLUS		=> */ PLUS | FWD,
		/* MINUS	=> */ MINUS | FWD,
		/* EQ		=> */ OPEQ,
		/* LT		=> */ LT | FWD,
		/* GT		=> */ GT | FWD,
		/* AND		=> */ AND | FWD,
		/* OR		=> */ OP2,
		/* DOT		=> */ DOT | FWD,
		/* STAR		=> */ MIGHTEQ | FWD,
		/* HASH		=> */ HASH | FWD,
		/* COLON	=> */ COLON | FWD,
		/* FS		=> */ FS | FWD,
		/* BS		=> */ OR_BS,
		/* SQ		=> */ CHRLIT | FWD,
		/* DQ		=> */ STRLIT | FWD,
		/* SP		=> */ WS,
		/* NL		=> */ BOL | FWD
	)
	ST_BS(OR)
	ST(HASH,
		/* OTHER	=> */ WORD | FWD,
		/* LU		=> */ LITPFX | FWD,
		/* u		=> */ uPFX | FWD,
		/* DIGIT	=> */ NUM | FWD,
		/* 8		=> */ NUM | FWD,
		/* OP1		=> */ OP1 | FWD,
		/* MIGHTEQ	=> */ MIGHTEQ | FWD,
		/* PLUS		=> */ PLUS | FWD,
		/* MINUS	=> */ MINUS | FWD,
		/* EQ		=> */ MIGHTEQ | FWD,
		/* LT		=> */ LT | FWD,
		/* GT		=> */ GT | FWD,
		/* AND		=> */ AND | FWD,
		/* OR		=> */ OR | FWD,
		/* DOT		=> */ DOT | FWD,
		/* STAR		=> */ MIGHTEQ | FWD,
		/* HASH		=> */ OP2,
		/* COLON	=> */ COLON | FWD,
		/* FS		=> */ FS | FWD,
		/* BS		=> */ HASH_BS,
		/* SQ		=> */ CHRLIT | FWD,
		/* DQ		=> */ STRLIT | FWD,
		/* SP		=> */ WS,
		/* NL		=> */ BOL | FWD
	)
	ST_BS(HASH)
	ST(COLON,
		/* OTHER	=> */ WORD | FWD,
		/* LU		=> */ LITPFX | FWD,
		/* u		=> */ uPFX | FWD,
		/* DIGIT	=> */ NUM | FWD,
		/* 8		=> */ NUM | FWD,
		/* OP1		=> */ OP1 | FWD,
		/* MIGHTEQ	=> */ MIGHTEQ | FWD,
		/* PLUS		=> */ PLUS | FWD,
		/* MINUS	=> */ MINUS | FWD,
		/* EQ		=> */ MIGHTEQ | FWD,
		/* LT		=> */ LT | FWD,
		/* GT		=> */ GT | FWD,
		/* AND		=> */ AND | FWD,
		/* OR		=> */ OR | FWD,
		/* DOT		=> */ DOT | FWD,
		/* STAR		=> */ MIGHTEQ | FWD,
		/* HASH		=> */ HASH | FWD,
		/* COLON	=> */ OP2,
		/* FS		=> */ FS | FWD,
		/* BS		=> */ COLON_BS,
		/* SQ		=> */ CHRLIT | FWD,
		/* DQ		=> */ STRLIT | FWD,
		/* SP		=> */ WS,
		/* NL		=> */ BOL | FWD
	)
	ST_BS(COLON)
	ST(FS,
		/* OTHER	=> */ WORD | FWD,
		/* LU		=> */ LITPFX | FWD,
		/* u		=> */ uPFX | FWD,
		/* DIGIT	=> */ NUM | FWD,
		/* 8		=> */ NUM | FWD,
		/* OP1		=> */ OP1 | FWD,
		/* MIGHTEQ	=> */ MIGHTEQ | FWD,
		/* PLUS		=> */ PLUS | FWD,
		/* MINUS	=> */ MINUS | FWD,
		/* EQ		=> */ MIGHTEQ | FWD,
		/* LT		=> */ LT | FWD,
		/* GT		=> */ GT | FWD,
		/* AND		=> */ AND | FWD,
		/* OR		=> */ OR | FWD,
		/* DOT		=> */ DOT | FWD,
		/* STAR		=> */ BLKCOMM,
		/* HASH		=> */ HASH | FWD,
		/* COLON	=> */ COLON | FWD,
		/* FS		=> */ LNCOMM,
		/* BS		=> */ FS_BS,
		/* SQ		=> */ CHRLIT | FWD,
		/* DQ		=> */ STRLIT | FWD,
		/* SP		=> */ WS,
		/* NL		=> */ BOL | FWD
	)
	ST_BS(FS)
	ST(LT,
		/* OTHER	=> */ WORD | FWD,
		/* LU		=> */ LITPFX | FWD,
		/* u		=> */ uPFX | FWD,
		/* DIGIT	=> */ NUM | FWD,
		/* 8		=> */ NUM | FWD,
		/* OP1		=> */ OP1 | FWD,
		/* MIGHTEQ	=> */ MIGHTEQ | FWD,
		/* PLUS		=> */ PLUS | FWD,
		/* MINUS	=> */ MINUS | FWD,
		/* EQ		=> */ OPEQ,
		/* LT		=> */ SHIFT,
		/* GT		=> */ GT | FWD,
		/* AND		=> */ AND | FWD,
		/* OR		=> */ OR | FWD,
		/* DOT		=> */ DOT | FWD,
		/* STAR		=> */ MIGHTEQ | FWD,
		/* HASH		=> */ HASH | FWD,
		/* COLON	=> */ COLON | FWD,
		/* FS		=> */ FS | FWD,
		/* BS		=> */ LT_BS,
		/* SQ		=> */ CHRLIT | FWD,
		/* DQ		=> */ STRLIT | FWD,
		/* SP		=> */ WS,
		/* NL		=> */ BOL | FWD
	)
	ST_BS(LT)
	ST(GT,
		/* OTHER	=> */ WORD | FWD,
		/* LU		=> */ LITPFX | FWD,
		/* u		=> */ uPFX | FWD,
		/* DIGIT	=> */ NUM | FWD,
		/* 8		=> */ NUM | FWD,
		/* OP1		=> */ OP1 | FWD,
		/* MIGHTEQ	=> */ MIGHTEQ | FWD,
		/* PLUS		=> */ PLUS | FWD,
		/* MINUS	=> */ MINUS | FWD,
		/* EQ		=> */ OPEQ,
		/* LT		=> */ LT | FWD,
		/* GT		=> */ SHIFT | FWD,
		/* AND		=> */ AND | FWD,
		/* OR		=> */ OR | FWD,
		/* DOT		=> */ DOT | FWD,
		/* STAR		=> */ MIGHTEQ | FWD,
		/* HASH		=> */ HASH | FWD,
		/* COLON	=> */ COLON | FWD,
		/* FS		=> */ FS | FWD,
		/* BS		=> */ GT_BS,
		/* SQ		=> */ CHRLIT | FWD,
		/* DQ		=> */ STRLIT | FWD,
		/* SP		=> */ WS,
		/* NL		=> */ BOL | FWD
	)
	ST_BS(GT)
	ST(DOT,
		/* OTHER	=> */ WORD | FWD,
		/* LU		=> */ LITPFX | FWD,
		/* u		=> */ uPFX | FWD,
		/* DIGIT	=> */ NUM,
		/* 8		=> */ NUM,
		/* OP1		=> */ OP1 | FWD,
		/* MIGHTEQ	=> */ MIGHTEQ | FWD,
		/* PLUS		=> */ PLUS | FWD,
		/* MINUS	=> */ MINUS | FWD,
		/* EQ		=> */ MIGHTEQ | FWD,
		/* LT		=> */ LT | FWD,
		/* GT		=> */ GT | FWD,
		/* AND		=> */ AND | FWD,
		/* OR		=> */ OR | FWD,
		/* DOT		=> */ DOT | FWD,
		/* STAR		=> */ MIGHTEQ | FWD,
		/* HASH		=> */ HASH | FWD,
		/* COLON	=> */ COLON | FWD,
		/* FS		=> */ FS | FWD,
		/* BS		=> */ DOT_BS,
		/* SQ		=> */ CHRLIT | FWD,
		/* DQ		=> */ STRLIT | FWD,
		/* SP		=> */ WS,
		/* NL		=> */ BOL | FWD
	)
	ST_BS(DOT)
	ST(SHIFT,
		/* OTHER	=> */ WORD | FWD,
		/* LU		=> */ LITPFX | FWD,
		/* u		=> */ uPFX | FWD,
		/* DIGIT	=> */ NUM | FWD,
		/* 8		=> */ NUM | FWD,
		/* OP1		=> */ OP1 | FWD,
		/* MIGHTEQ	=> */ MIGHTEQ | FWD,
		/* PLUS		=> */ PLUS | FWD,
		/* MINUS	=> */ MINUS | FWD,
		/* EQ		=> */ SHIFTEQ,
		/* LT		=> */ LT | FWD,
		/* GT		=> */ GT | FWD,
		/* AND		=> */ AND | FWD,
		/* OR		=> */ OR | FWD,
		/* DOT		=> */ DOT | FWD,
		/* STAR		=> */ MIGHTEQ | FWD,
		/* HASH		=> */ HASH | FWD,
		/* COLON	=> */ COLON | FWD,
		/* FS		=> */ FS | FWD,
		/* BS		=> */ SHIFT_BS,
		/* SQ		=> */ CHRLIT | FWD,
		/* DQ		=> */ STRLIT | FWD,
		/* SP		=> */ WS,
		/* NL		=> */ BOL | FWD
	)
	ST_BS(SHIFT)
	ST(LNCOMM,
		/* OTHER	=> */ LNCOMM,
		/* LU		=> */ LNCOMM,
		/* u		=> */ LNCOMM,
		/* DIGIT	=> */ LNCOMM,
		/* 8		=> */ LNCOMM,
		/* OP1		=> */ LNCOMM,
		/* MIGHTEQ	=> */ LNCOMM,
		/* PLUS		=> */ LNCOMM,
		/* MINUS	=> */ LNCOMM,
		/* EQ		=> */ LNCOMM,
		/* LT		=> */ LNCOMM,
		/* GT		=> */ LNCOMM,
		/* AND		=> */ LNCOMM,
		/* OR		=> */ LNCOMM,
		/* DOT		=> */ LNCOMM,
		/* STAR		=> */ LNCOMM,
		/* HASH		=> */ LNCOMM,
		/* COLON	=> */ LNCOMM,
		/* FS		=> */ LNCOMM,
		/* BS		=> */ LNCOMM_BS,
		/* SQ		=> */ LNCOMM,
		/* DQ		=> */ LNCOMM,
		/* SP		=> */ LNCOMM,
		/* NL		=> */ BOL | FWD
	)
	ST(LNCOMM_BS,
		/* OTHER	=> */ LNCOMM,
		/* LU		=> */ LNCOMM,
		/* u		=> */ LNCOMM,
		/* DIGIT	=> */ LNCOMM,
		/* 8		=> */ LNCOMM,
		/* OP1		=> */ LNCOMM,
		/* MIGHTEQ	=> */ LNCOMM,
		/* PLUS		=> */ LNCOMM,
		/* MINUS	=> */ LNCOMM,
		/* EQ		=> */ LNCOMM,
		/* LT		=> */ LNCOMM,
		/* GT		=> */ LNCOMM,
		/* AND		=> */ LNCOMM,
		/* OR		=> */ LNCOMM,
		/* DOT		=> */ LNCOMM,
		/* STAR		=> */ LNCOMM,
		/* HASH		=> */ LNCOMM,
		/* COLON	=> */ LNCOMM,
		/* FS		=> */ LNCOMM,
		/* BS		=> */ LNCOMM_BS,
		/* SQ		=> */ LNCOMM,
		/* DQ		=> */ LNCOMM,
		/* SP		=> */ LNCOMM_BS,
		/* NL		=> */ LNCOMM
	)
	ST(BLKCOMM,
		/* OTHER	=> */ BLKCOMM,
		/* LU		=> */ BLKCOMM,
		/* u		=> */ BLKCOMM,
		/* DIGIT	=> */ BLKCOMM,
		/* 8		=> */ BLKCOMM,
		/* OP1		=> */ BLKCOMM,
		/* MIGHTEQ	=> */ BLKCOMM,
		/* PLUS		=> */ BLKCOMM,
		/* MINUS	=> */ BLKCOMM,
		/* EQ		=> */ BLKCOMM,
		/* LT		=> */ BLKCOMM,
		/* GT		=> */ BLKCOMM,
		/* AND		=> */ BLKCOMM,
		/* OR		=> */ BLKCOMM,
		/* DOT		=> */ BLKCOMM,
		/* STAR		=> */ BLKSTAR,
		/* HASH		=> */ BLKCOMM,
		/* COLON	=> */ BLKCOMM,
		/* FS		=> */ BLKCOMM,
		/* BS		=> */ BLKCOMM,
		/* SQ		=> */ BLKCOMM,
		/* DQ		=> */ BLKCOMM,
		/* SP		=> */ BLKCOMM,
		/* NL		=> */ BLKCOMM
	)
	ST(BLKSTAR,
		/* OTHER	=> */ BLKCOMM,
		/* LU		=> */ BLKCOMM,
		/* u		=> */ BLKCOMM,
		/* DIGIT	=> */ BLKCOMM,
		/* 8		=> */ BLKCOMM,
		/* OP1		=> */ BLKCOMM,
		/* MIGHTEQ	=> */ BLKCOMM,
		/* PLUS		=> */ BLKCOMM,
		/* MINUS	=> */ BLKCOMM,
		/* EQ		=> */ BLKCOMM,
		/* LT		=> */ BLKCOMM,
		/* GT		=> */ BLKCOMM,
		/* AND		=> */ BLKCOMM,
		/* OR		=> */ BLKCOMM,
		/* DOT		=> */ BLKCOMM,
		/* STAR		=> */ BLKSTAR,
		/* HASH		=> */ BLKCOMM,
		/* COLON	=> */ BLKCOMM,
		/* FS		=> */ WS,
		/* BS		=> */ BLKSTAR_BS,
		/* SQ		=> */ BLKCOMM,
		/* DQ		=> */ BLKCOMM,
		/* SP		=> */ BLKCOMM,
		/* NL		=> */ BLKCOMM
	)
	ST(BLKSTAR_BS,
		/* OTHER	=> */ BLKCOMM,
		/* LU		=> */ BLKCOMM,
		/* u		=> */ BLKCOMM,
		/* DIGIT	=> */ BLKCOMM,
		/* 8		=> */ BLKCOMM,
		/* OP1		=> */ BLKCOMM,
		/* MIGHTEQ	=> */ BLKCOMM,
		/* PLUS		=> */ BLKCOMM,
		/* MINUS	=> */ BLKCOMM,
		/* EQ		=> */ BLKCOMM,
		/* LT		=> */ BLKCOMM,
		/* GT		=> */ BLKCOMM,
		/* AND		=> */ BLKCOMM,
		/* OR		=> */ BLKCOMM,
		/* DOT		=> */ BLKCOMM,
		/* STAR		=> */ BLKSTAR,
		/* HASH		=> */ BLKCOMM,
		/* COLON	=> */ BLKCOMM,
		/* FS		=> */ WS,
		/* BS		=> */ BLKCOMM,
		/* SQ		=> */ BLKCOMM,
		/* DQ		=> */ BLKCOMM,
		/* SP		=> */ BLKSTAR_BS,
		/* NL		=> */ BLKSTAR
	)
	ST_TERM(OP1)
	ST_TERM(OP2)
	ST_TERM(OPEQ)
	ST_TERM(SHIFTEQ)
	ST(STRLIT,
		/* OTHER	=> */ STRLIT,
		/* LU		=> */ STRLIT,
		/* u		=> */ STRLIT,
		/* DIGIT	=> */ STRLIT,
		/* 8		=> */ STRLIT,
		/* OP1		=> */ STRLIT,
		/* MIGHTEQ	=> */ STRLIT,
		/* PLUS		=> */ STRLIT,
		/* MINUS	=> */ STRLIT,
		/* EQ		=> */ STRLIT,
		/* LT		=> */ STRLIT,
		/* GT		=> */ STRLIT,
		/* AND		=> */ STRLIT,
		/* OR		=> */ STRLIT,
		/* DOT		=> */ STRLIT,
		/* STAR		=> */ STRLIT,
		/* HASH		=> */ STRLIT,
		/* COLON	=> */ STRLIT,
		/* FS		=> */ STRLIT,
		/* BS		=> */ STRLIT_BS,
		/* SQ		=> */ STRLIT,
		/* DQ		=> */ WS,
		/* SP		=> */ STRLIT,
		/* NL		=> */ ERROR | FWD
	)
	ST(STRLIT_BS,
		/* OTHER	=> */ STRLIT,
		/* LU		=> */ STRLIT,
		/* u		=> */ STRLIT,
		/* DIGIT	=> */ STRLIT,
		/* 8		=> */ STRLIT,
		/* OP1		=> */ STRLIT,
		/* MIGHTEQ	=> */ STRLIT,
		/* PLUS		=> */ STRLIT,
		/* MINUS	=> */ STRLIT,
		/* EQ		=> */ STRLIT,
		/* LT		=> */ STRLIT,
		/* GT		=> */ STRLIT,
		/* AND		=> */ STRLIT,
		/* OR		=> */ STRLIT,
		/* DOT		=> */ STRLIT,
		/* STAR		=> */ STRLIT,
		/* HASH		=> */ STRLIT,
		/* COLON	=> */ STRLIT,
		/* FS		=> */ STRLIT,
		/* BS		=> */ STRLIT,
		/* SQ		=> */ STRLIT,
		/* DQ		=> */ STRLIT,
		/* SP		=> */ STRLIT_BS_WS,
		/* NL		=> */ STRLIT
	)
	ST(STRLIT_BS_WS, // ugh this really does suck
		/* OTHER	=> */ ERROR | FWD,
		/* LU		=> */ ERROR | FWD,
		/* u		=> */ ERROR | FWD,
		/* DIGIT	=> */ ERROR | FWD,
		/* 8		=> */ ERROR | FWD,
		/* OP1		=> */ ERROR | FWD,
		/* MIGHTEQ	=> */ ERROR | FWD,
		/* PLUS		=> */ ERROR | FWD,
		/* MINUS	=> */ ERROR | FWD,
		/* EQ		=> */ ERROR | FWD,
		/* LT		=> */ ERROR | FWD,
		/* GT		=> */ ERROR | FWD,
		/* AND		=> */ ERROR | FWD,
		/* OR		=> */ ERROR | FWD,
		/* DOT		=> */ ERROR | FWD,
		/* STAR		=> */ ERROR | FWD,
		/* HASH		=> */ ERROR | FWD,
		/* COLON	=> */ ERROR | FWD,
		/* FS		=> */ ERROR | FWD,
		/* BS		=> */ ERROR | FWD,
		/* SQ		=> */ ERROR | FWD,
		/* DQ		=> */ ERROR | FWD,
		/* SP		=> */ STRLIT_BS_WS,
		/* NL		=> */ STRLIT
	)
	ST(CHRLIT,
		/* OTHER	=> */ CHRLIT,
		/* LU		=> */ CHRLIT,
		/* u		=> */ CHRLIT,
		/* DIGIT	=> */ CHRLIT,
		/* 8		=> */ CHRLIT,
		/* OP1		=> */ CHRLIT,
		/* MIGHTEQ	=> */ CHRLIT,
		/* PLUS		=> */ CHRLIT,
		/* MINUS	=> */ CHRLIT,
		/* EQ		=> */ CHRLIT,
		/* LT		=> */ CHRLIT,
		/* GT		=> */ CHRLIT,
		/* AND		=> */ CHRLIT,
		/* OR		=> */ CHRLIT,
		/* DOT		=> */ CHRLIT,
		/* STAR		=> */ CHRLIT,
		/* HASH		=> */ CHRLIT,
		/* COLON	=> */ CHRLIT,
		/* FS		=> */ CHRLIT,
		/* BS		=> */ CHRLIT_BS,
		/* SQ		=> */ WS,
		/* DQ		=> */ CHRLIT,
		/* SP		=> */ CHRLIT,
		/* NL		=> */ ERROR | FWD
	)
	ST(CHRLIT_BS,
		/* OTHER	=> */ CHRLIT,
		/* LU		=> */ CHRLIT,
		/* u		=> */ CHRLIT,
		/* DIGIT	=> */ CHRLIT,
		/* 8		=> */ CHRLIT,
		/* OP1		=> */ CHRLIT,
		/* MIGHTEQ	=> */ CHRLIT,
		/* PLUS		=> */ CHRLIT,
		/* MINUS	=> */ CHRLIT,
		/* EQ		=> */ CHRLIT,
		/* LT		=> */ CHRLIT,
		/* GT		=> */ CHRLIT,
		/* AND		=> */ CHRLIT,
		/* OR		=> */ CHRLIT,
		/* DOT		=> */ CHRLIT,
		/* STAR		=> */ CHRLIT,
		/* HASH		=> */ CHRLIT,
		/* COLON	=> */ CHRLIT,
		/* FS		=> */ CHRLIT,
		/* BS		=> */ CHRLIT,
		/* SQ		=> */ CHRLIT,
		/* DQ		=> */ CHRLIT,
		/* SP		=> */ CHRLIT_BS_WS,
		/* NL		=> */ CHRLIT
	)
	ST(CHRLIT_BS_WS, // ugh this really does also suck
		/* OTHER	=> */ ERROR | FWD,
		/* LU		=> */ ERROR | FWD,
		/* u		=> */ ERROR | FWD,
		/* DIGIT	=> */ ERROR | FWD,
		/* 8		=> */ ERROR | FWD,
		/* OP1		=> */ ERROR | FWD,
		/* MIGHTEQ	=> */ ERROR | FWD,
		/* PLUS		=> */ ERROR | FWD,
		/* MINUS	=> */ ERROR | FWD,
		/* EQ		=> */ ERROR | FWD,
		/* LT		=> */ ERROR | FWD,
		/* GT		=> */ ERROR | FWD,
		/* AND		=> */ ERROR | FWD,
		/* OR		=> */ ERROR | FWD,
		/* DOT		=> */ ERROR | FWD,
		/* STAR		=> */ ERROR | FWD,
		/* HASH		=> */ ERROR | FWD,
		/* COLON	=> */ ERROR | FWD,
		/* FS		=> */ ERROR | FWD,
		/* BS		=> */ ERROR | FWD,
		/* SQ		=> */ ERROR | FWD,
		/* DQ		=> */ ERROR | FWD,
		/* SP		=> */ CHRLIT_BS_WS,
		/* NL		=> */ CHRLIT
	)
	ST(ERROR, // dead end, caught outside the branchless main loop
		/* OTHER	=> */ ERROR,
		/* LU		=> */ ERROR,
		/* u		=> */ ERROR,
		/* DIGIT	=> */ ERROR,
		/* 8		=> */ ERROR,
		/* OP1		=> */ ERROR,
		/* MIGHTEQ	=> */ ERROR,
		/* PLUS		=> */ ERROR,
		/* MINUS	=> */ ERROR,
		/* EQ		=> */ ERROR,
		/* LT		=> */ ERROR,
		/* GT		=> */ ERROR,
		/* AND		=> */ ERROR,
		/* OR		=> */ ERROR,
		/* DOT		=> */ ERROR,
		/* STAR		=> */ ERROR,
		/* HASH		=> */ ERROR,
		/* COLON	=> */ ERROR,
		/* FS		=> */ ERROR,
		/* BS		=> */ ERROR,
		/* SQ		=> */ ERROR,
		/* DQ		=> */ ERROR,
		/* SP		=> */ ERROR,
		/* NL		=> */ ERROR
	)
};
// }}}

static const unsigned char statetoks[] = {
	// comments here cover corresponding tokens. stuff in parentheses doesn't
	// matter (could have any value)
	CLEX_TOK_EOL, // BOL, (BOL_BS)
	CLEX_TOK_OP1, // OP1, (WS)
	CLEX_TOK_OP2, // OP2, (WS_BS)
	CLEX_TOK_IDENT, // LITPFX, (LITPFX_BS)
	CLEX_TOK_IDENT, // uPFX, (uPFX_BS)
	CLEX_TOK_IDENT, // WORD, (WORD_BS)
	CLEX_TOK_NUM, // NUM, (NUM_BS)
	CLEX_TOK_OP1, // MIGHTEQ, (MIGHTEQ_BS)
	CLEX_TOK_OP1, // MINUS, (MINUS_BS)
	CLEX_TOK_OP1, // PLUS, (PLUS_BS)
	CLEX_TOK_OP1, // AND, (AND_BS)
	CLEX_TOK_OP1, // OR, (OR_BS)
	CLEX_TOK_OP1, // HASH, (HASH_BS)
	CLEX_TOK_OP1, // COLON, (COLON_BS)
	CLEX_TOK_OP1, // FS, (FS_BS)
	CLEX_TOK_OP1, // LT, (LT_BS)
	CLEX_TOK_OP1, // GT, (GT_BS)
	CLEX_TOK_OP1, // DOT, (DOT_BS)
	CLEX_TOK_SHIFT, // SHIFT, (SHIFT_BS)
	CLEX_TOK_LNCOMM, // LNCOMM, (LNCOMM_BS)
	CLEX_TOK_BLKCOMM, // BLKCOMM, (BLKSTAR)
	CLEX_TOK_OPEQ, // OPEQ, (BLKSTAR_BS)
	CLEX_TOK_SHIFTEQ, // SHIFTEQ, (STRLIT_BS_WS)
	CLEX_TOK_STR, // STRLIT, (STRLIT_BS)
	CLEX_TOK_CHAR, // CHRLIT, (CHRLIT_BS)
	200 // (ERROR), (CHRLIT_BS_WS) // completely arbitrary value!
};

// Uncomment and LSP-inspect this for an idea of how much space is used:
//enum { TABLESPACE = sizeof(classes) + sizeof(trans) + sizeof(statetoks) };
// => 1786 bytes.

// somewhat inefficient number formatter - size matters more than speed here.
// also assumes there's extra space in the buffer, because there always is.
static cold int err_fmtnum(char *out, unsigned int n) {
	int i = 0, j = 10;
	do {
		// explicitly optimised division by 10 to make sure we NEVER have a
		// divide instruction because integer division is evil.
		unsigned int div10 = n * 3435973837ull >> 35;
		// apparently doing * 10 here emits a more efficient lea-based shift-add
		// thingy than attempting to shift-add by hand, at least with clang.
		unsigned int remainder = n - div10 * 10;
		out[--j] = '0' + remainder;
		n = div10;
	} while (n);
	do out[i++] = out[j++]; while (j < 10);
	return i;
}

static cold struct linecol {
	unsigned int ln, col;
} getlinecol(const char *buf, unsigned int off) {
	// we don't store line and col for every token as it wastes a lot of memory.
	// instead we can simply re-scan and count the newlines.
	// FIXME: grapheme widths for columns? horrendous, but technically correct!
	// if not that, then probably at least count utf8 codepoints.
	unsigned int ln = 1, col = 0;
	for (unsigned int i = 0;; ++i, ++col) {
		if (buf[i] == '\n') { ++ln; col = 0; }
		if (i == off) break;
	}
	return (struct linecol) {ln, col};
}

static cold char *err_putprefix(char *restrict out, const char *f,
		const char *restrict buf, unsigned int off) {
	struct linecol lc = getlinecol(buf, off);
	// "filename:line:col: "
	while (*f) *out++ = *f++;
	*out++ = ':';
	out += err_fmtnum(out, lc.ln);
	*out++ = ':';
	out += err_fmtnum(out, lc.col);
	*out++ = ':';
	*out++ = ' ';
	return out;
}

// if we have clang we can avoid even doing a library call here. otherwise we
// still only depend on memcpy which is pretty reasonable. we could also in
// theory do some manual word-wise copying of the strings below, but eww.
#ifdef __clang__
#define copy __builtin_memcpy_inline
#else
#define copy memcpy
#endif

static cold void err(struct clex *c, const char *p, unsigned int off, int state,
		int prevtok, const char *f) {
	if (state == STATE_ERROR) {
		// all transitions to ERROR have FWD, so we get a dummy token at the
		// point where the error was detected. if this token points at a
		// newline, then we have an unterminated literal. if it does *not* point
		// at a newline, we have a non-newline after a backslash. in the latter
		// case, backtrack to the backslash to report it as unexpected. in the
		// former case, move back one character to point to the end of the line.
		if (p[off--] != '\n') while (p[off] != '\\') --off;
	}
	// format the message as f:ln:col: msg. pretty verbose due to not using
	// stdio or any other convenient formatting library, but also pretty simple
	// IMPORTANT: clex_memreq() must be recalculated after changing any of this!
	char *msg = (char *)c->tokoffs, *msgp = msg;
	c->err = msg;
	msgp = err_putprefix(msgp, f, p, off);
	static const char strblock[78] =
		"unterminated "		// + 0, len 13
		"string"			// +13, len 6
		"character"			// +19, len 9
		" literal"			// +28, len 8
		"block comment"		// +36, len 13
		"expected"			// +49, len 8
		" line after"		// +57, len 11
		" backslash";		// +68, len 10
	// minor code size trick: write a little too much first, then replace parts
	copy(msgp, strblock, 19); // "unterminated string"
	if (state == STATE_ERROR) {
		// note: ERROR can only follow another token-producing state. prevtok
		// could otherwise contain garbage from one of the dummy slots (see
		// dotok() below) but in this scenario that will never be the case.
		if (prevtok == CLEX_TOK_CHAR) {
			// s/string /character literal/
			copy(msgp + 13, strblock + 19, 9 + 8);
			msgp += 13 + 9 + 8;
		}
		else if (prevtok == CLEX_TOK_STR) {
			// append "literal" -> "
			copy(msgp + 19, strblock + 29, 8);
			msgp += 13 + 6 + 8;
		}
		else /* prevtok == 200 (backslash case above) */ {
			// s/terminated/expected/ -> unexpected
			copy(msgp + 2, strblock + 49, 8);
			// append " backslash" -> "unexpected backslash"
			copy(msgp + 10, strblock + 68, 10);
			msgp += 10 + 10;
		}
	}
	else if (state == STATE_BLKCOMM) {
		// s/string/block comment/ -> "unterminated block comment"
		copy(msgp + 13, strblock + 36, 13);
		msgp += 13 + 13;
	}
	else /* state == STATE_BS */ {
		// s/unterminated string/expected line after backslash/
		copy(msgp, strblock + 49, 8 + 11 + 10);
		msgp += 8 + 11 + 10;
	}
	*msgp = '\0';
	c->errlen = msgp - msg;
}

static inline void dotok(struct clex *c, const unsigned char *restrict p,
		unsigned int sz, const char *restrict f) {
	unsigned char *toks = c->toks;
	unsigned int *tokoffs = c->tokoffs;
	int state = STATE_BOL;
	unsigned int off = 0;
	unsigned int ntoks = 1; // skip first slot, see below
	for (; off != sz; ++off) {
		unsigned char state_trans = trans[classes[p[off]] + state];
		unsigned int lower7 = state_trans & 127;
		unsigned int off_adj = off - (state_trans >> 7); // see FIXPOS
		unsigned int fwdbit = state_trans & 1; // see FWD
		// bit 2 (low state bit): set -> 1; unset -> -1.
		// => only even-indexed states (which are terminal) update the type
		// note: this means we have to waste the bottom two slots. oh well!
		unsigned int idxmask = (state_trans | -3u) + 2;
		// set the starting offset for the *next* token, that way if we're not
		// starting a new one we don't clobber the existing position.
		tokoffs[ntoks + 1] = off_adj;
		// if FWD bit is set then we have a new token; at this point we increase
		// ntoks meaning the first token will be at position 2. hence the waste.
		ntoks += fwdbit; // advance if we have a new token
		state = lower7 >> 1; // middle 6 bits are our new state
		toks[ntoks & idxmask] = statetoks[lower7 >> 2];
	}
	// hide the wasted slots from the caller
	c->ntoks = ntoks - 2; c->toks += 2; c->tokoffs += 2;
	if_cold (state != STATE_BOL) {
		if (f) {
			err(c, (char *)p, tokoffs[ntoks], state, toks[ntoks - 1], f);
		}
		else {
			c->err = "syntax error";
			c->errlen = 12;
		}
	}
}

struct clex clex(const char *restrict buf, unsigned int sz,
		void *restrict outmem, const char *restrict filename) {
	struct clex c = {
		.tokoffs = outmem,
		.toks = (unsigned char *)(c.tokoffs + sz)
	};
	if_cold (sz != 0 && buf[sz - 1] != '\n') {
		c.err = "input is not a valid text file (must end with newline)";
		return c;
	}
	dotok(&c, (unsigned char *)buf, sz, filename);
	return c;
}

static inline bool ishex(char c) {
	char lower = c | 32;
	return c >= '0' & c <= '9' | lower >= 'a' & lower <= 'f';
}
static inline int hexval(char c) {
	unsigned char u = c;
	return (u & 15) + (u >> 6) * 9; // assumes ishex(c)
}

static inline int utf8len_ucs2(int codepoint) { // assumes codepoint <= 0xFFFF
	if (codepoint > 0x7FF) return 3;
	// would be weird to use \u for ascii, so do this branch last
	if (codepoint <= 0x7F) return 1;
	return 2;
}
static inline int utf8len(int codepoint) { // assumes codepoint <= 0x10FFFF
    if (codepoint <= 0xFFFF) { // most likely use for \U over \u
		if (codepoint <= 0x7FF) {
			if (codepoint <= 0x7F) return 1; // should still be least likely
			return 2;
		}
		return 3;
	}
	return 4;
}

static inline bool isbadcodepoint_ucs2(int codepoint) { // assumes <= 0xFFFF
	return codepoint >= 0xD800 && codepoint <= 0xDFFF || // surrogates
			codepoint >= 0xFDD0 && codepoint <= 0xFDEF || // non-characters
			(codepoint & 0x0FFE) == 0x0FFE; // more non-characters
}
static inline bool isbadcodepoint(int codepoint) {
	return isbadcodepoint_ucs2(codepoint) ||
			codepoint >= 0x110000 && codepoint <= 0x1FFFFF || // more non-chars
			codepoint > 0x10FFFF; // max valid codepoint
}

static struct bsiter_ret { char c; const char *next; } bsiter(const char *p) {
	char c;
	// skip line continuations, but don't ignore backslashes mid-line. may need
	// some backtracking/re-scanning - oh well. this is the slow path.
	while ((c = *p) == '\\') {
		const char *q = p;
		while (classes[(unsigned char)*++q] == CLASS_SP);
		if (*q != '\n') break; // backslash is significant, return it
		p = q + 1; // skip newline and keep scanning forward
	}
	return (struct bsiter_ret){c, p + 1};
}

struct clex_ident_validate_ret clex_ident_validate(const struct clex *c,
		const char *restrict buf, unsigned int idx) {
	const char *p = buf + c->tokoffs[idx], *start = p;
	// N.B. file always ends in an EOL, so no need for bounds check on this loop
	struct clex_ident_validate_ret ret;
	for (int namelen = 0;;) {
		struct bsiter_ret iter = bsiter(p); char c = iter.c; p = iter.next;
		// first 5 entries in CLASSES() are alphanumeric; if it's a symbol or
		// space, we're past this token...
		if (classes[(unsigned char)c] > 4 * NSTATES) {
			if (c == '\\') { // ... unless it's a UCN, or bad syntax
				const char *pbs = p;
				iter = bsiter(p); c = iter.c; p = iter.next;
				if (c == 'u') {
					char hex[4];
					iter = bsiter(p); hex[0] = iter.c;
					const char *p0 = iter.next;
					for (int i = 0;;) {
						if_cold (!ishex(hex[i])) {
							ret.err = CLEX_IDENT_BADUCNHEX4;
							ret.err_off = p - buf;
							return ret;
						}
						p = iter.next;
						if (++i == 4) break;
						iter = bsiter(p); hex[i] = iter.c;
					}
					int codepoint = hexval(hex[0]) << 12 | hexval(hex[1]) << 8 |
							hexval(hex[2]) << 4 | hexval(hex[3]);
					if_cold (isbadcodepoint_ucs2(codepoint)) {
						ret.err = CLEX_IDENT_BADCODEPOINT;
						ret.err_off = p0 - buf;
						return ret;
					}
					namelen += utf8len_ucs2(codepoint);
				}
				else if (c == 'U') {
					char hex[6];
					iter = bsiter(p); hex[0] = iter.c;
					const char *p0 = iter.next;
					for (int i = 0;;) {
						if_cold (!ishex(hex[i])) {
							ret.err = CLEX_IDENT_BADUCNHEX6;
							ret.err_off = p - buf;
							return ret;
						}
						p = iter.next;
						if (++i == 6) break;
						iter = bsiter(p); hex[i] = iter.c;
					}
					int codepoint = hexval(hex[0]) << 20 | hexval(hex[1]) << 16 |
							hexval(hex[2]) << 12 | hexval(hex[3] << 8) |
							hexval(hex[4]) << 4 | hexval(hex[5]);
					if_cold (isbadcodepoint(codepoint)) {
						ret.err = CLEX_IDENT_BADCODEPOINT;
						ret.err_off = p0 - buf;
						return ret;
					}
					namelen += utf8len(codepoint);
				}
				else { // we skipped line conts already; must be bad syntax here
					ret.err = CLEX_IDENT_UNEXPBACKSLASH;
					ret.err_off = pbs - buf;
					return ret;
				}
			}
			else {
				ret.err = 0; ret.ext = p - start; ret.len = namelen;
				return ret;
			}
		}
		else {
			++namelen;
		}
	}
}

cold int clex_ident_errstr(char *restrict out, const char *restrict buf,
		enum clex_ident_validate_err err, unsigned int err_off,
		const char *restrict filename) {
	char *p = out;
	static const char strblock[81] =
		"illegal character in 3-digit UCN" // (becomes 4 or 6, see below)
		"Unicode codepoint"
		"identifier"
		"unexpected end of line";
	if (buf[err_off] == '\n') {
		p = err_putprefix(p, filename, buf, err_off - 1);
		copy(p, strblock + 59, 22);
		p[22] = '\0';
		return p + 23 - out;
	}
	switch (err) {
		case CLEX_IDENT_BADUCNHEX4:
		case CLEX_IDENT_BADUCNHEX6:
			copy(p, strblock, 32);
			p[21] += err; // 3 -> 4 or 6 (N.B. relies on enum value!!!)
			p += 32;
			break;
		case CLEX_IDENT_BADCODEPOINT:
			copy(p, strblock, 8); // "illegal "
			copy(p + 8, strblock + 32, 17); // "Unicode codepoint"
			p += 25;
			break;
		case CLEX_IDENT_UNEXPBACKSLASH:
			copy(p, strblock, 18); // "illegal character in "
			copy(p + 18, strblock + 49, 10); // "identifier"
			p += 28;
			break;
		default:
			unreachable;
	}
	*p = '\0';
	return p - out;
}

static int utf8put_ucs2(char *out, int codepoint) { // assumes valid UCS2
	if (codepoint > 0x7FF) {
		out[0] = 0xE0 | codepoint >> 12;
		out[1] = 0x80 | codepoint >> 6 & 0x3F;
		out[2] = 0x80 | codepoint & 0x3F;
		return 3;
	}
    if (codepoint <= 0x7F) {
		out[0] = codepoint;
		return 1;
	}
	out[0] = 0xC0 | codepoint >> 6;
	out[1] = 0x80 | codepoint & 0x3F;
	return 2;
}
static int utf8put(char *out, int codepoint) { // assumes valid Unicode
	if (codepoint <= 0xFFFF) return utf8put_ucs2(out, codepoint);
	out[0] = 0xF0 | codepoint >> 18;
	out[1] = 0x80 | codepoint >> 12 & 0x3F;
	out[2] = 0x80 | codepoint >> 6 & 0x3F;
	out[3] = 0x80 | codepoint & 0x3F;
	return 4;
}

void clex_ident(const struct clex *c, const char *restrict buf,
		unsigned int idx, char *restrict out) {
	const char *p = buf + c->tokoffs[idx];
	struct bsiter_ret iter = bsiter(p); char ch = iter.c; p = iter.next;
	for (;;) {
		if (classes[(unsigned char)ch] > 4 * NSTATES) {
			if (ch == '\\') {
				iter = bsiter(p); ch = iter.c; p = iter.next;
				if (ch == 'u') {
					iter = bsiter(p); char c0 = iter.c; p = iter.next;
					iter = bsiter(p); char c1 = iter.c; p = iter.next;
					iter = bsiter(p); char c2 = iter.c; p = iter.next;
					iter = bsiter(p); char c3 = iter.c; p = iter.next;
					int codepoint = hexval(c0) << 12 | hexval(c1) << 8 |
							hexval(c2) << 4 | hexval(c3);
					out += utf8put_ucs2(out, codepoint);
				}
				else /* ch == 'U' */ {
					iter = bsiter(p); char c0 = iter.c; p = iter.next;
					iter = bsiter(p); char c1 = iter.c; p = iter.next;
					iter = bsiter(p); char c2 = iter.c; p = iter.next;
					iter = bsiter(p); char c3 = iter.c; p = iter.next;
					iter = bsiter(p); char c4 = iter.c; p = iter.next;
					iter = bsiter(p); char c5 = iter.c; p = iter.next;
					int codepoint = hexval(c0) << 20 | hexval(c1) << 16 |
							hexval(c2) << 12 | (hexval(c3) << 8) |
							hexval(c4) << 4 | hexval(c5);
					out += utf8put(out, codepoint);
				}
			}
			else {
				return;
			}
		}
		else {
			*out++ = ch;
			iter = bsiter(p); ch = iter.c; p = iter.next;
		}
	}
}

// vi: sw=4 ts=4 noet tw=80 cc=80 fdm=marker
