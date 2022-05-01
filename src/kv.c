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

#include <stdbool.h>

#include "intdefs.h"
#include "kv.h"
#include "unreachable.h"

#define EOF -1

// parser states, implemented by STATE() macros in kv_parser_feed() below.
// needs to be kept in sync!
enum {
	ok, ok_slash,
	ident, ident_slash, identq,
	sep, sep_slash, condsep, condsep_slash,
	cond_prefix,
	val, val_slash, valq, afterval, afterval_slash,
	cond_suffix
};

bool kv_parser_feed(struct kv_parser *this, const char *in, uint sz,
		kv_parser_cb cb, void *ctxt) {
	const char *p = in;
	short c;

	// slight hack, makes init more convenient (just {0})
	if (!this->line) this->line = 1;
	if (!this->outp) this->outp = this->tokbuf;

	// this is a big ol' blob of ugly state machine macro spaghetti - too bad!
	#define INCCOL() (*p == '\n' ? (++this->line, this->col = 0) : ++this->col)
	#define READ() (p == in + sz ? EOF : (INCCOL(), *p++))
	#define ERROR(s) do { \
		this->errmsg = s; \
		return false; \
	} while (0)
	#define OUT(c) do { \
		if (this->outp - this->tokbuf == KV_TOKEN_MAX) { \
			ERROR("token unreasonably large!"); \
		} \
		*this->outp++ = (c); \
	} while (0)
	#define CASE_WS case ' ': case '\t': case '\n': case '\r'
	// note: multi-eval
	#define IS_WS(c) ((c) == ' ' || (c) == '\t' || (c) == '\n' || (c) == '\r')
	#define STATE(s) case s: s
	#define HANDLE_EOF() do { case EOF: return true; } while (0)
	#define SKIP_COMMENT(next) do { \
		this->state = next; \
		this->incomment = true; \
		goto start;  \
	} while (0)
	#define GOTO(s) do { this->state = s; goto s; } while (0)
	#define CB(type) do { \
		cb(type, this->tokbuf, this->outp - this->tokbuf, ctxt); \
		this->outp = this->tokbuf; \
	} while (0)
	// prefix and suffix conditions are more or less the same, just in different
	// contexts, because very good syntax yes.
	#define CONDSTATE(name, type, next) do { \
		STATE(name): \
			switch (c = READ()) { \
				HANDLE_EOF(); \
				CASE_WS: ERROR("unexpected whitespace in conditional"); \
				case '[': ERROR("unexpected opening bracket in conditional"); \
				case '{': case '}': ERROR("unexpected brace in conditional"); \
				case '/': ERROR("unexpected slash in conditional"); \
				case ']': CB(type); GOTO(next); \
				default: OUT(c); goto name; \
			} \
	} while (0)

start: // special spaghetti so we don't have a million different comment states
	if (this->incomment) while ((c = READ()) != '\n') if (c == EOF) return true;
	this->incomment = false;

switch (this->state) {

STATE(ok):
	c = READ();
ident_postread:
	switch (c) {
		HANDLE_EOF();
		CASE_WS: goto ok;
		case '#': ERROR("kv macros not supported");
		case '{': ERROR("unexpected control character");
		case '}':
			if (!this->nestlvl) ERROR("too many closing braces");
			--this->nestlvl;
			char c_ = c;
			cb(KV_NEST_END, &c_, 1, ctxt);
			goto ok;
		case '"': GOTO(identq);
		case '/': GOTO(ok_slash);
		case '[': case ']': ERROR("unexpected conditional bracket");
		default: GOTO(ident);
	}

STATE(ok_slash):
	switch (c = READ()) {
		HANDLE_EOF();
		case '/': SKIP_COMMENT(ok);
		default: GOTO(ident);
	}

ident:
	OUT(c);
case ident: // continue here
	switch (c = READ()) {
		HANDLE_EOF();
		case '{':
			CB(KV_IDENT);
			++this->nestlvl;
			char c_ = c;
			cb(KV_NEST_START, &c_, 1, ctxt);
			GOTO(ok);
		// XXX: assuming [ is a token break; haven't checked Valve's code
		case '[': CB(KV_IDENT); GOTO(cond_prefix);
		case '}': ERROR("unexpected closing brace");
		case ']': ERROR("unexpected closing bracket");
		case '"': ERROR("unexpected quote mark");
		CASE_WS: CB(KV_IDENT); GOTO(sep);
		case '/': GOTO(ident_slash);
		default: goto ident;
	}

STATE(ident_slash):
	switch (c = READ()) {
		HANDLE_EOF();
		case '/': CB(KV_IDENT); SKIP_COMMENT(sep);
		default: OUT('/'); GOTO(ident);
	}

STATE(identq):
	switch (c = READ()) {
		HANDLE_EOF();
		case '"': CB(KV_IDENT_QUOTED); GOTO(sep);
		default: OUT(c); goto identq;
	}

STATE(sep):
	do c = READ(); while (IS_WS(c));
	switch (c) {
		HANDLE_EOF();
		case '{':;
			char c_ = c;
			++this->nestlvl;
			cb(KV_NEST_START, &c_, 1, ctxt);
			GOTO(ok);
		case '[': GOTO(cond_prefix);
		case '"': GOTO(valq);
		case '}': ERROR("unexpected closing brace");
		case ']': ERROR("unexpected closing bracket");
		case '/': GOTO(sep_slash);
		default: GOTO(val);
	}

STATE(sep_slash):
	switch (c = READ()) {
		HANDLE_EOF();
		case '/': SKIP_COMMENT(sep);
		default: GOTO(val);
	}

CONDSTATE(cond_prefix, KV_COND_PREFIX, condsep);

STATE(condsep):
	do c = READ(); while (IS_WS(c));
	switch (c) {
		HANDLE_EOF();
		case '{':;
			char c_ = c;
			++this->nestlvl;
			cb(KV_NEST_START, &c_, 1, ctxt);
			GOTO(ok);
		case '}': ERROR("unexpected closing brace");
		case '[': ERROR("unexpected opening bracket");
		case ']': ERROR("unexpected closing bracket");
		case '/': GOTO(condsep_slash);
		// these conditions only go before braces because very good syntax
		default: ERROR("unexpected string value after prefix condition");
	}

STATE(condsep_slash):
	switch (c = READ()) {
		HANDLE_EOF();
		case '/': SKIP_COMMENT(condsep);
		default: ERROR("unexpected string value after prefix condition");
	}

val:
	OUT(c);
case val: // continue here
	switch (c = READ()) {
		HANDLE_EOF();
		case '{': ERROR("unexpected opening brace");
		case ']': ERROR("unexpected closing bracket");
		case '"': ERROR("unexpected quotation mark");
		// might get [ or } with no whitespace
		case '}':
			CB(KV_VAL);
			--this->nestlvl;
			char c_ = c;
			cb(KV_NEST_END, &c_, 1, ctxt);
			GOTO(afterval);
		case '[': CB(KV_VAL); GOTO(cond_suffix);
		CASE_WS: CB(KV_VAL); GOTO(afterval);
		case '/': GOTO(val_slash);
		default: goto val;
	}

STATE(val_slash):
	switch (c = READ()) {
		HANDLE_EOF();
		case '/': CB(KV_VAL); SKIP_COMMENT(afterval);
		default: OUT('/'); GOTO(val);
	}

STATE(valq):
	switch (c = READ()) {
		HANDLE_EOF();
		case '"': CB(KV_VAL_QUOTED); GOTO(afterval);
		default: OUT(c); goto valq;
	}

STATE(afterval):
	switch (c = READ()) {
		HANDLE_EOF();
		CASE_WS: goto afterval;
		case '[': GOTO(cond_suffix);
		case '/': GOTO(afterval_slash);
		// mildly dumb hack: if no conditional, we can just use the regular
		// starting state handler to get next transition correct - just avoid
		// double-reading the character
		default: goto ident_postread;
	}

STATE(afterval_slash):
	switch (c = READ()) {
		HANDLE_EOF();
		case '/': SKIP_COMMENT(afterval);
		default: GOTO(ident);
	}

CONDSTATE(cond_suffix, KV_COND_SUFFIX, ok);

}

	#undef CONDSTATE
	#undef CB
	#undef GOTO
	#undef SKIP_COMMENT
	#undef HANDLE_EOF
	#undef STATE
	#undef IS_WS
	#undef CASE_WS
	#undef OUT
	#undef ERROR
	#undef READ
	#undef INCCOL

	unreachable; // pretty sure!
}

bool kv_parser_done(struct kv_parser *this) {
	if (this->state != ok && this->state != afterval) {
		this->errmsg = "unexpected end of input";
		return false;
	}
	if (this->nestlvl != 0) {
		this->errmsg = "unterminated object (unbalanced braces)";
		return false;
	}
	return true;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
