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

#ifndef INC_CHUNKLETS_CLEX_H
#define INC_CHUNKLETS_CLEX_H

#ifdef __cplusplus
#define _clex_bool bool
#define _clex_restrict __restrict // XXX: is this portable enough?
#else
#define _clex_bool _Bool
#define _clex_restrict restrict
#endif

#ifdef _WIN32
#ifdef __cplusplus
typedef wchar_t clex_os_char; // ugh
#else
typedef unsigned short clex_os_char;
#endif
#else
typedef char clex_os_char;
#endif

#ifdef _WIN64
typedef long long clex_size;
#else
typedef long clex_size;
#endif

#if defined(__GNUC__) || defined(__clang__)
#define _clex_unreachable __builtin_unreachable()
#elif defined(_MSC_VER)
#define _clex_unreachable __assume(0)
#else
#ifdef __cplusplus
[[noreturn]] static inline void _clex_invoke_ub(void) {}
#else
static inline _Noreturn void _clex_invoke_ub(void) {}
#endif
#define _clex_unreachable (_clex_invoke_ub())
#endif

/*
 * Determines the amount of memory required to call clex() on a given file.
 * That memory must be provided as a contiguous block, with at least 4 byte
 * alignment.
 *
 * filesz is the number of bytes in the file, and namelen is the length of the
 * name. The reason namelen is required is because the memory is reused to
 * produce an error string when lexing fails.
 *
 * filesz is assumed to be non-zero, since empty C source files are not allowed,
 * and common memory allocators do not accept zero sizes either. To gracefully
 * handle an empty file if desired, check for a zero size and avoid attempting
 * tokenisation altogether.
 *
 * If a null filename will be passed to clex(), which skips error message
 * formatting, namelen can be set to 0.
 *
 * NOTE: This assumes that the file is reasonably-sized. On 64-bit architectures,
 * the file size must be not exceed 4GiB *minus 2 bytes*, due to an internal
 * implementation detail. On 32-bit architectures, the file has to be even
 * smaller in order to fit the token data in memory; it is recommended to stay
 * below ~256MiB.
 */
static inline clex_size clex_memreq(unsigned int filesz, unsigned int namelen) {
	clex_size sz = ((clex_size)filesz + 2) * 5 + 5;
	if (sz < namelen + 64) return namelen + 64; // make space for error messages
	return sz;
}

/* The main structure returned by a call to clex() - see below. */
struct clex {
	/*
	 * Check this first. It is a null pointer if lexing succeeded, or points to
	 * a null-terminated error string on failure. See also clex() for a
	 * description of how the error string is created.
	 */
	const char *err;
	union {
		/*
		 * If err is not null, contains the length of the string, excluding the
		 * null terminator.
		 */
		int errlen;
		/*
		 * If lexing was successful (err is null), contains the number of tokens
		 * lexed. Use this to loop over toks and tokoffs.
		 */
		unsigned int ntoks;
	};
	//char pad[4];
	/*
	 * If lexing was successful (err is null), points to an array of token
	 * offsets within the lexed file. On failure, the value is undefined.
	 */
	unsigned int *tokoffs;
	/*
	 * If lexing was successful (err is null), points to an array of basic token
	 * types corresponding to each position in tokoffs. On failure, the value is
	 * undefined.
	 */
	unsigned char *toks;
};

/*
 * Lexes a C file into a list of tokens. See above comments on struct clex for
 * return type information.
 *
 * buf and sz point to the contents of the file, which has to be fully loaded
 * into memory prior to lexing.
 *
 * outmem is an opaque block of memory of at sufficient size calculated by
 * clex_memreq(), and at least 4-byte alignment. It can be allocated by any
 * means desired.
 *
 * filename is used purely for error reporting, though typically it would be the
 * name of the file being lexed, of course. Normally the error string output
 * will include the filename, row and column, and a brief description of the
 * error. If this information is not needed, filename can be a null pointer
 * instead, which disables this functionality.
 */
struct clex clex(const char *_clex_restrict buf, unsigned int sz,
		void *_clex_restrict outmem, const char *_clex_restrict filename);

/*
 * These are the basic token types recognised by the lexer in its main pass.
 * All the operators are grouped in such a way that they can be distinguished by
 * looking at a single character.
 *
 * Identifiers and literals might not necessarily be totally valid; this is
 * checked in more detail when parsing out specific values.
 */
enum clex_tok_type {
	CLEX_TOK_IDENT, /* An identifier or keyword. */
	CLEX_TOK_NUM, /* A numeric literal, which may or may not be totally valid */
	CLEX_TOK_OP1, /* One of: + - * / = < > ! & | ^ ~ . , ( ) [ ] { } : ; ? # */
	CLEX_TOK_OP2, /* One of: ++ -- -> && || ## :: */
	CLEX_TOK_SHIFT, /* One of: >> << */
	CLEX_TOK_OPEQ, /* One of: += -= *= /= &= |= ^= <= >= != == */
	CLEX_TOK_SHIFTEQ, /* One of: >>= <<= */
	CLEX_TOK_CHAR, /* A character literal. */
	CLEX_TOK_STR, /* A string literal. */
	CLEX_TOK_LNCOMM, /* A C++/C99-style one-line comment. */
	CLEX_TOK_BLKCOMM, /* A classic C-style multi-line comment. */
	CLEX_TOK_EOL /* A run of end-of-line characters, for parsing #directives. */
};

/* These are all the C operators, hopefully self-explanatory. */
enum clex_op {
	// -- op1 --
	CLEX_OP_PLUS,		/* + */
	CLEX_OP_MINUS,		/* - */
	CLEX_OP_MULT,		/* * (could also be a deref) */ // XXX: bad name maybe?
	CLEX_OP_DIV,		/* / */
	CLEX_OP_ASSIGN,		/* = */
	CLEX_OP_LT,			/* < */
	CLEX_OP_GT,			/* > */
	CLEX_OP_NOT,		/* ! */
	CLEX_OP_BITAND,		/* & (could also be an address-of) */ // XXX: bad name?
	CLEX_OP_BITOR,		/* | */
	CLEX_OP_XOR,		/* ^ */
	CLEX_OP_BITNOT,		/* ~ */
	CLEX_OP_DOT,		/* . */
	CLEX_OP_COMMA,		/* , */
	CLEX_OP_LPAREN,		/* ( */
	CLEX_OP_RPAREN,		/* ) */
	CLEX_OP_LSQ,		/* [ */
	CLEX_OP_RSQ,		/* ] */
	CLEX_OP_LCURL,		/* { */
	CLEX_OP_RCURL,		/* } */
	CLEX_OP_COLON,		/* : */
	CLEX_OP_SEMICOL,	/* ; */
	CLEX_OP_QUESTION,	/* ? */
	CLEX_OP_HASH,		/* # */
	// -- op2 --
	CLEX_OP_INC,		/* ++ */
	CLEX_OP_DEC,		/* -- */
	CLEX_OP_ARROW,		/* -> */
	CLEX_OP_AND,		/* && */
	CLEX_OP_OR,			/* || */
	CLEX_OP_PASTE,		/* ## */
	CLEX_OP_DCOLON,		/* :: C23 attribute vendors/namespaces */
	// -- shift --
	CLEX_OP_LSH,		/* << */
	CLEX_OP_RSH,		/* >> */
	// -- opeq --
	CLEX_OP_PLUSEQ,		/* += */
	CLEX_OP_MINUSEQ,	/* -= */
	CLEX_OP_MULTEQ,		/* *= */
	CLEX_OP_DIVEQ,		/* /= */
	CLEX_OP_ANDEQ,		/* &= */
	CLEX_OP_OREQ,		/* |= */
	CLEX_OP_XOREQ,		/* ^= */
	CLEX_OP_LTEQ,		/* <= */
	CLEX_OP_GTEQ,		/* >= */
	CLEX_OP_NOTEQ,		/* != */
	CLEX_OP_EQ,			/* == */
	// -- shifteq --
	CLEX_OP_LSHEQ,		/* <<= */
	CLEX_OP_RSHEQ		/* >>= */
};

/*
 * Determines the exact operator from a token of type CLEX_TOK_OP1 at offset off
 * by inspecting the first byte of the token.
 *
 * If the token is not of type CLEX_TOK_OP1, the behaviour is undefined.
 */
static inline enum clex_op clex_op1(const char *buf, unsigned int off) {
	switch (buf[off]) {
		case '+': return CLEX_OP_PLUS;
		case '-': return CLEX_OP_MINUS;
		case '*': return CLEX_OP_MULT;
		case '/': return CLEX_OP_DIV;
		case '=': return CLEX_OP_ASSIGN;
		case '<': return CLEX_OP_LT;
		case '>': return CLEX_OP_GT;
		case '!': return CLEX_OP_NOT;
		case '&': return CLEX_OP_BITAND;
		case '|': return CLEX_OP_BITOR;
		case '^': return CLEX_OP_XOR;
		case '~': return CLEX_OP_BITNOT;
		case '.': return CLEX_OP_DOT;
		case ',': return CLEX_OP_COMMA;
		case '(': return CLEX_OP_LPAREN;
		case ')': return CLEX_OP_RPAREN;
		case '[': return CLEX_OP_LSQ;
		case ']': return CLEX_OP_RSQ;
		case '{': return CLEX_OP_LCURL;
		case '}': return CLEX_OP_RCURL;
		case ':': return CLEX_OP_COLON;
		case ';': return CLEX_OP_SEMICOL;
		case '?': return CLEX_OP_QUESTION;
		case '#': return CLEX_OP_HASH;
	}
	_clex_unreachable;
}

/*
 * Determines the exact operator from a token of type CLEX_TOK_OP2 at offset off
 * by inspecting the end of the token.
 *
 * If the token is not of type CLEX_TOK_OP2, the behaviour is undefined.
 */
static inline enum clex_op clex_op2(const char *buf, unsigned int off) {
	for (;;) {
		switch (buf[off + 1]) {
			case '+': return CLEX_OP_INC;
			case '-': return CLEX_OP_DEC;
			case '>': return CLEX_OP_ARROW;
			case '&': return CLEX_OP_AND;
			case '|': return CLEX_OP_OR;
			case '#': return CLEX_OP_PASTE;
			case ':': return CLEX_OP_DCOLON;
			// initial lex has validated backslash-newlines, so we can just
			// ignore any of these characters here
			case '\\': case ' ': case '\t': case '\r': case '\n': continue;
		}
		_clex_unreachable;
	}
}

/*
 * Determines the exact operator from a token of type CLEX_TOK_SHIFT at offset
 * off by inspecting the first byte of the token.
 *
 * If the token is not of type CLEX_TOK_SHIFT, the behaviour is undefined.
 */
static inline enum clex_op clex_shift(const char *buf, unsigned int off) {
	switch (buf[off]) {
		case '>': return CLEX_OP_RSH;
		case '<': return CLEX_OP_LSH;
	}
	_clex_unreachable;
}

/*
 * Determines the exact operator from a token of type CLEX_TOK_OPEQ at offset
 * off by inspecting the first byte of the token.
 *
 * If the token is not of type CLEX_TOK_OPEQ, the behaviour is undefined.
 */
static inline enum clex_op clex_opeq(const char *buf, unsigned int off) {
	switch (buf[off]) {
		case '+': return CLEX_OP_PLUSEQ;
		case '-': return CLEX_OP_MINUSEQ;
		case '*': return CLEX_OP_MULTEQ;
		case '/': return CLEX_OP_DIVEQ;
		case '&': return CLEX_OP_ANDEQ;
		case '|': return CLEX_OP_OREQ;
		case '^': return CLEX_OP_XOREQ;
		case '<': return CLEX_OP_LTEQ;
		case '>': return CLEX_OP_GTEQ;
		case '!': return CLEX_OP_NOTEQ;
		case '=': return CLEX_OP_EQ;
	}
	_clex_unreachable;
}

/*
 * Determines the exact operator from a token of type CLEX_TOK_SHIFTEQ at offset
 * off by inspecting the first byte of the token.
 *
 * If the token is not of type CLEX_TOK_SHIFTEQ, the behaviour is undefined.
 */
static inline enum clex_op clex_shifteq(const char *buf, unsigned int off) {
	switch (buf[off]) {
		case '<': return CLEX_OP_RSHEQ;
		case '>': return CLEX_OP_LSHEQ;
	}
	_clex_unreachable;
}

/*
 * Determines whether the lexed token at a given index is an operator (not
 * counting the likes of sizeof, which will have to be handled as a special case
 * of an identifier/keyword for syntax purposes).
 */
static inline _clex_bool clex_isop(const struct clex *c, unsigned int idx) {
	switch (c->toks[idx]) {
		case CLEX_TOK_IDENT: return 0;
		case CLEX_TOK_NUM: return 0;
		case CLEX_TOK_OP1: return 1;
		case CLEX_TOK_OP2: return 1;
		case CLEX_TOK_SHIFT: return 1;
		case CLEX_TOK_OPEQ: return 1;
		case CLEX_TOK_SHIFTEQ: return 1;
		case CLEX_TOK_CHAR: return 0;
		case CLEX_TOK_STR: return 0;
		case CLEX_TOK_LNCOMM: return 0;
		case CLEX_TOK_BLKCOMM: return 0;
		case CLEX_TOK_EOL: return 0;
	}
	_clex_unreachable;
}

/* Returns true if a token is either a unary or binary plus operator. */
static inline _clex_bool clex_isplus(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_OP1 && buf[c->tokoffs[idx]] == '+';
}

/* Returns true if a token is either a unary or binary minus operator. */
static inline _clex_bool clex_isminus(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_OP1 && buf[c->tokoffs[idx]] == '-';
}

/* Returns true if a token is either a multiply or deference operator. */
static inline _clex_bool clex_ismult(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_OP1 && buf[c->tokoffs[idx]] == '*';
}

/* Returns true a token is a division operator. */
static inline _clex_bool clex_isdiv(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_OP1 && buf[c->tokoffs[idx]] == '/';
}

/* Returns true if a token is an assignment operator. */
static inline _clex_bool clex_isassign(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_OP1 && buf[c->tokoffs[idx]] == '=';
}

/* Returns true if a token is a less-than operator. */
static inline _clex_bool clex_islt(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_OP1 && buf[c->tokoffs[idx]] == '<';
}

/* Returns true if a token is a greater-than operator. */
static inline _clex_bool clex_isgt(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_OP1 && buf[c->tokoffs[idx]] == '>';
}

/* Returns true if a token is a negation operator. */
static inline _clex_bool clex_isnot(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_OP1 && buf[c->tokoffs[idx]] == '!';
}

/* Returns true if a token is either a bitwise and operator or an indirection. */
static inline _clex_bool clex_isbitand(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_OP1 && buf[c->tokoffs[idx]] == '&';
}

/* Returns true if a token is a bitwise or operator. */
static inline _clex_bool clex_isbitor(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_OP1 && buf[c->tokoffs[idx]] == '|';
}

/* Returns true if a token is a bitwise exclusive-or operator. */
static inline _clex_bool clex_isxor(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_OP1 && buf[c->tokoffs[idx]] == '^';
}

/* Returns true if a token is a bitwise negation operator. */
static inline _clex_bool clex_isbitnot(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_OP1 && buf[c->tokoffs[idx]] == '~';
}

/* Returns true if a token is a struct member dot. */
static inline _clex_bool clex_isdot(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_OP1 && buf[c->tokoffs[idx]] == '.';
}

/* Returns true if a token is a comma. */
static inline _clex_bool clex_iscomma(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_OP1 && buf[c->tokoffs[idx]] == ',';
}

/* Returns true if a token is an opening/left parenthesis. */
static inline _clex_bool clex_islparen(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_OP1 && buf[c->tokoffs[idx]] == '(';
}

/* Returns true if a token is a closing/right parenthesis. */
static inline _clex_bool clex_isrparen(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_OP1 && buf[c->tokoffs[idx]] == ')';
}

/* Returns true if a token is an opening/left square bracket. */
static inline _clex_bool clex_islsq(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_OP1 && buf[c->tokoffs[idx]] == '[';
}

/* Returns true if a token is a closing/right square bracket. */
static inline _clex_bool clex_isrsq(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_OP1 && buf[c->tokoffs[idx]] == ']';
}

/* Returns true if a token is an opening/left curly brace. */
static inline _clex_bool clex_islcurl(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_OP1 && buf[c->tokoffs[idx]] == '{';
}

/* Returns true if a token is a closing/right curly brace. */
static inline _clex_bool clex_isrcurl(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_OP1 && buf[c->tokoffs[idx]] == '}';
}

/* Returns true if a token is a colon. */
static inline _clex_bool clex_iscolon(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_OP1 && buf[c->tokoffs[idx]] == ':';
}

/* Returns true if a token is a semicolon. */
static inline _clex_bool clex_issemicol(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_OP1 && buf[c->tokoffs[idx]] == ';';
}

/* Returns true if a token is a question mark. */
static inline _clex_bool clex_isquestion(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_OP1 && buf[c->tokoffs[idx]] == '?';
}

/* Returns true if a token is a preprocessor hash. */
static inline _clex_bool clex_ishash(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_OP1 && buf[c->tokoffs[idx]] == '#';
}

/* Returns true if a token is a unary increment operator. */
static inline _clex_bool clex_isinc(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_OP2 && buf[c->tokoffs[idx] + 1] == '+';
}

/* Returns true if a token is a unary decrement operator. */
static inline _clex_bool clex_isdec(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_OP2 && buf[c->tokoffs[idx] + 1] == '-';
}

/* Returns true if a token is an arrow for indirect struct member access. */
static inline _clex_bool clex_isarrow(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_OP2 && buf[c->tokoffs[idx] + 1] == '>';
}

/* Returns true if a token is a conditional and operator. */
static inline _clex_bool clex_isand(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_OP2 && buf[c->tokoffs[idx] + 1] == '&';
}

/* Returns true if a token is a conditional or operator. */
static inline _clex_bool clex_isor(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_OP2 && buf[c->tokoffs[idx] + 1] == '|';
}

/* Returns true if a token is a token-pasting operator. */
static inline _clex_bool clex_ispaste(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_OP2 && buf[c->tokoffs[idx] + 1] == '#';
}

/* Returns true if a token is a double colon for C23 attribute namespacing. */
static inline _clex_bool clex_isdcol(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_OP2 && buf[c->tokoffs[idx] + 1] == ':';
}

/* Returns true if a token is a left shift operator. */
static inline _clex_bool clex_islsh(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_SHIFT && buf[c->tokoffs[idx]] == '<';
}

/* Returns true if a token is a right shift operator. */
static inline _clex_bool clex_isrsh(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_SHIFT && buf[c->tokoffs[idx]] == '>';
}

/* Returns true if a token is a binary increment (plus-assignment) operator. */
static inline _clex_bool clex_ispluseq(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_OPEQ && buf[c->tokoffs[idx]] == '+';
}

/* Returns true if a token is a binary decrement (plus-assignment) operator. */
static inline _clex_bool clex_isminuseq(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_OPEQ && buf[c->tokoffs[idx]] == '-';
}

/* Returns true if a token is a multiply-assignment operator. */
static inline _clex_bool clex_ismulteq(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_OPEQ && buf[c->tokoffs[idx]] == '*';
}

/* Returns true if a token is a divide-assignment operator. */
static inline _clex_bool clex_isdiveq(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_OPEQ && buf[c->tokoffs[idx]] == '/';
}

/* Returns true if a token is a bitwise-and-assignment operator. */
static inline _clex_bool clex_isandeq(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_OPEQ && buf[c->tokoffs[idx]] == '&';
}

/* Returns true if a token is a bitwise-or-assignment operator. */
static inline _clex_bool clex_isoreq(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_OPEQ && buf[c->tokoffs[idx]] == '|';
}

/* Returns true if a token is a bitwise-xor-assignment operator. */
static inline _clex_bool clex_isxoreq(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_OPEQ && buf[c->tokoffs[idx]] == '^';
}

/* Returns true if a token is a less-than-or-equal comparison operator. */
static inline _clex_bool clex_islteq(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_OPEQ && buf[c->tokoffs[idx]] == '<';
}

/* Returns true if a token is a greater-than-or-equal comparison operator. */
static inline _clex_bool clex_isgteq(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_OPEQ && buf[c->tokoffs[idx]] == '>';
}

/* Returns true if a token is an inequality comparison operator. */
static inline _clex_bool clex_isnoteq(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_OPEQ && buf[c->tokoffs[idx]] == '!';
}

/* Returns true if a token is an equality comparison operator. */
static inline _clex_bool clex_iseq(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_OPEQ && buf[c->tokoffs[idx]] == '=';
}

/* Returns true if a token is a left-shift-assignment operator. */
static inline _clex_bool clex_islshifteq(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_SHIFTEQ && buf[c->tokoffs[idx]] == '<';
}

/* Returns true if a token is a right-shift-assignment operator. */
static inline _clex_bool clex_isrshifteq(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	return c->toks[idx] == CLEX_TOK_SHIFTEQ && buf[c->tokoffs[idx]] == '>';
}

/*
 * Determines the exact operator from a token of type CLEX_TOK_OP1,
 * CLEX_TOK_OP2, CLEX_TOK_SHIFT, CLEX_TOK_OPEQ, or CLEX_TOK_SHIFTEQ.
 *
 * If the token is not of one of the above types, the behaviour is undefined.
 * Call clex_isop() first to determine whether a token is of a valid type.
 */
static inline enum clex_op clex_op(const struct clex *c,
		const char *_clex_restrict buf, unsigned int idx) {
	unsigned int off = c->tokoffs[idx];
	switch (c->toks[idx]) {
		case CLEX_TOK_OP1: return clex_op1(buf, off);
		case CLEX_TOK_OP2: return clex_op2(buf, off);
		case CLEX_TOK_SHIFT: return clex_shift(buf, off);
		case CLEX_TOK_OPEQ: return clex_opeq(buf, off);
		case CLEX_TOK_SHIFTEQ: return clex_shifteq(buf, off);
	}
	_clex_unreachable;
}

enum clex_ident_validate_err {
	// NOTE: do not reorder these values! clex_ident_errstr() relies on them
	CLEX_IDENT_OK,
	CLEX_IDENT_BADUCNHEX4, /* invalid \uXXXX UCN sequence */
	CLEX_IDENT_BADCODEPOINT, /* invalid Unicode codepoint in UCN */
	CLEX_IDENT_BADUCNHEX6, /* invalid \UXXXXXX UCN sequence */
	CLEX_IDENT_UNEXPBACKSLASH /* unexpected backslash in identifier */
};

/*
 * Double-checks that a TOK_IDENT token is syntactically valid (as the initial
 * lexer pass does not account for bad UCN sequences or misplaced backslashes).
 *
 * Determines the extent of the token, i.e. how many bytes it occupies in the
 * source, along with the length of the evaluated name, i.e. how many bytes are
 * required to store the evaluated name as UTF-8 once line-continuations and
 * UCNs are taken into account.
 *
 * This function may only be called on tokens of type TOK_IDENT; anything else
 * produces undefined behaviour.
 */
struct clex_ident_validate_ret {
	enum clex_ident_validate_err err; /* zero on success, nonzero on error */
	// wonky union interweaving here because C++ doesn't do anonymous structs :(
	union {
		int ext; /* if !err: extent (source length) */
		unsigned int err_off; /* if err: position of error in file */
	};
	int len; /* if !err: length of evaluated name (as utf-8). else undefined! */
} clex_ident_validate(const struct clex *c, const char *_clex_restrict buf,
		unsigned int idx);

/*
 * Evaluates the name of an identifier, taking into account line continuations
 * and UCNs. The identifier has to have first been successfully validated using
 * clex_ident_validate(). The buffer out must be large enough to hold the result
 * which is indicated by the len member of the clex_ident_validate_ret struct.
 */
void clex_ident(const struct clex *c, const char *_clex_restrict buf,
		unsigned int idx, char *_clex_restrict out);

// NOTE: this is precalculated based on the implementation of clex_ident_errstr
// and will need changed if the function changes!
/*
 * Determines the buffer size required to format an error message using
 * clex_ident_errstr(), given the length of the filename.
 *
 * clex_ident_errstr() formats a string in the form "filename:line:col: message"
 * so this macro can determine the worst-case memory requirement in advance.
 *
 * If a constant value is desired, e.g. for a stack buffer, simply use the known
 * longest possible file path, for instance PATH_MAX on Unix-likes or MAX_PATH
 * on Windows. The reason this is a macro is to allow it to produce an integer
 * constant expression, so that a stack buffer can be used without creating a
 * VLA.
 */
#define CLEX_IDENT_ERRSTR_MEMREQ(namelen) ((namelen) + 24 + 33)

/*
 * Formats an error message in the form "filename:line:col: message", given an
 * error result from clex_ident_validate(). The resulting string is written to
 * the buffer pointed to by out.
 *
 * out must point to a buffer of at least CLEX_IDENT_ERRSTR_MEMREQ(
 * strlen(filename)) bytes. The resulting string is null-terminated for
 * convenience, and the length of the string excluding the null terminator is
 * returned.
 *
 * err has to be an error value from the clex_ident_validate_err enum; calling
 * this function with a success result will produce undefined behaviour.
 */
int clex_ident_errstr(char *_clex_restrict out, const char *_clex_restrict buf,
		enum clex_ident_validate_err err, unsigned int err_off,
		const char *_clex_restrict filename);

#undef _clex_unreachable
#undef _clex_restrict
#undef _clex_bool

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
