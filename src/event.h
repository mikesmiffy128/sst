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

#ifndef INC_EVENT_H
#define INC_EVENT_H

#define _EVENT_CAT4_(a, b, c, d) a##b##c##d
#define _EVENT_CAT4(a, b, c, d) _EVENT_CAT4_(a, b, c, d)

#define DECL_EVENT(evname) void _evemit_##evname(void);
#define DEF_EVENT(evname) \
	DECL_EVENT(evname) \
	static inline void _evown_##evname(void) { _evemit_##evname(); }
#define EMIT_EVENT(evname) _evown_##evname();

#define HANDLE_EVENT(evname) \
	void _EVENT_CAT4(_evhandler_, MODULE_NAME, _, evname)(void) \
	/* function body here */

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
