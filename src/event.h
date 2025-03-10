/*
 * Copyright © 2025 Michael Smith <mikesmiffy128@gmail.com>
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

/*
 * Declares an event defined somewhere in the codebase, allowing a handler to be
 * defined with HANDLE_EVENT() below. Takes an optional list of types for
 * parameters. The handler will be called every time the event is emitted by the
 * declaring module.
 */
#define DECL_EVENT(evname, ...) typedef void _must_declare_event_##evname;

/*
 * Declares a predicate - a special type of event returning bool. Predicates are
 * used to determine whether some other action should be performed, and
 * generally should not have side effects, since they get short-circuited and
 * thus won't always fire when a check is being performed.
 */
#define DECL_PREDICATE(evname, ...) typedef bool _must_declare_event_##evname;

/*
 * Defines an event belonging to this module. Doing so allows EMIT_<event>() to
 * be called to fire handlers in all modules. Two modules (i.e. source files)
 * cannot define an event (or predicate) with the same name.
 */
#define DEF_EVENT(event, ...) void EMIT_##event(__VA_ARGS__);

/*
 * Defines a predicate belonging to this module. Doing so allows CHECK_<pred>()
 * to be called to determine whether to perform some action. Predicates share a
 * namespace with events and two modules cannot define two things with the same
 * name.
 */
#define DEF_PREDICATE(pred, ...) bool CHECK_##pred(__VA_ARGS__);

/*
 * Begins an event handler function that gets hooked up to an event by the code
 * generation system. This is type-generic: if the event is a regular event,
 * the function will return void; if it is a predicate it will return bool.
 * Takes a function argument list which must match the type lists given to the
 * above DEF/DECL macros.
 *
 * Note again that predicates are not guaranteed to fire at all due to
 * short-circuiting and thus generally should not have side effects.
 *
 * In the current event implementation, each source file may handle only one of
 * each event type, as any more wouldn't be too useful anyway.
 */
#define HANDLE_EVENT(evname, ...) \
	_must_declare_event_##evname _EVENT_CAT4(_evhandler_, MODULE_NAME, _, \
			evname)(__VA_ARGS__) /* function body here */

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
