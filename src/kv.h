/*
 * Copyright © 2021 Michael Smith <mikesmiffy128@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef INC_KV_H
#define INC_KV_H

#include <stdbool.h>

#include "intdefs.h"

/*
 * Maximum length of a single token. Since this code is trying to avoid dynamic
 * memory allocations, this arbitrary limit is chosen to accomodate all known
 * "reasonable" tokens likely to come in any real files, probably.
 */
#define KV_TOKEN_MAX 512

/*
 * Contains all the state associated with parsing (lexing?) a KeyValues file.
 * Should be zeroed out prior to the first call (initialise with `= {0};`).
 */
struct kv_parser {
	ushort line, col;	/* the current line and column in the text */
	char state : 7;		/* internal, shouldn't usually be touched directly */
	bool incomment : 1;	/* internal */
	ushort nestlvl;		/* internal */
	const char *errmsg; /* the error message, *IF* parsing just failed */

	// trying to avoid dynamic allocations - valve's own parser seems to have
	// a similar limit as well and our use case doesn't really need to worry
	// about stupid massive values, so it's fine
	char *outp;
	char tokbuf[KV_TOKEN_MAX];
};

/*
 * These are the tokens that can be received by a kv_parser_cb (below).
 * The x-macro and string descriptions are given to allow for easy debug
 * stringification. Note that this "parser" is really just lexing out these
 * tokens - handling the actual structure of the file should be done in the
 * callback. This is so that data can be streamed rather than all read into
 * memory at once.
 */
#define KV_TOKENS(X) \
	X(KV_IDENT, "ident") \
	X(KV_IDENT_QUOTED, "quoted-ident") \
	X(KV_VAL, "value") \
	X(KV_VAL_QUOTED, "quoted-value") \
	X(KV_COND_PREFIX, "cond-prefix") \
	X(KV_COND_SUFFIX, "cond-suffix") \
	X(KV_NEST_START, "object-start") \
	X(KV_NEST_END, "object-end")

#define _ENUM(s, ignore) s,
enum kv_token { KV_TOKENS(_ENUM) };
#undef _ENUM

typedef void (*kv_parser_cb)(enum kv_token type, const char *p, uint len,
		void *ctxt);

/*
 * Feed a block of text into the lexer. This would usually be a block of data
 * read in from a file.
 *
 * The lexer is reentrant and can be fed arbitrarily sized blocks of data at a
 * time. The function may return early in the event of an error; a return value
 * of false indicates thaat this has happened, otherwise true is returned.
 *
 * In the event of an error, the errmsg, line and col fields of the parser
 * struct can be used for diagnostics.
 */
bool kv_parser_feed(struct kv_parser *this, const char *in, uint sz,
		kv_parser_cb cb, void *ctxt);

/*
 * This indicates that parsing is done; if this is called at an unexpected time,
 * a parsing error will result; this is indicated in the return value as with
 * kv_parser_feed.
 */
bool kv_parser_done(struct kv_parser *this);

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
