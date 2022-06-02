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

#ifndef INC_ERRMSG_H
#define INC_ERRMSG_H

#include "con_.h"
#include "os.h"

extern const char msg_note[], msg_warn[], msg_error[];

#define _ERRMSG_STR1(x) #x
#define _ERRMSG_STR(x) _ERRMSG_STR1(x)

#ifdef MODULE_NAME
#define _errmsg_msg(mod, msg, s, ...) \
	con_warn("sst: %s: %s" s ": %s\n", mod, msg, __VA_ARGS__)
// XXX: can we avoid using the ##__VA_ARGS__ extension here somehow?
#define _errmsg_msgx(mod, msg, s, ...) \
	con_warn("sst: %s: %s" s "\n", mod, msg, ##__VA_ARGS__)
#else
// dumb hack: we don't want sst.c to say "sst: sst:" - easier to just drop the
// module parameter here so all the stuff below can just remain the same
#define _errmsg_msg(ignored, msg, s, ...) \
	con_warn("sst: %s" s ": %s\n", msg, __VA_ARGS__)
#define _errmsg_msgx(ignored, msg, s, ...) \
	con_warn("sst: %s" s "\n", msg, ##__VA_ARGS__)
#endif

#define _errmsg_std(msg, ...) \
	_errmsg_msg(_ERRMSG_STR(MODULE_NAME), msg, __VA_ARGS__, strerror(errno))

#define _errmsg_x(msg, ...) \
	_errmsg_msgx(_ERRMSG_STR(MODULE_NAME), msg, __VA_ARGS__)

#ifdef _WIN32
#define _errmsg_sys(msg, ...) do { \
	char _warnsys_buf[512]; \
	OS_WINDOWS_ERROR(_warnsys_buf); \
	_errmsg_msg(_ERRMSG_STR(MODULE_NAME), msg, __VA_ARGS__, _warnsys_buf); \
} while (0)
#define _errmsg_dl _errmsg_sys
#else
#define _errmsg_sys _errmsg_std
static inline const char *_errmsg_dlerror(void) {
	const char *e = dlerror();
	if (!e) return "Unknown error"; // just in case, better avoid weirdness!
	return e;
}
#define _errmsg_dl(msg, ...) \
	_errmsg_msg(_ERRMSG_STR(MODULE_NAME), msg, __VA_ARGS__, _errmsg_dlerror());
#endif

// Reminder: will need add warnsock/errsock if we ever do stuff with sockets,
// because of Windows's WSAGetLastError() abomination.
#define errmsg_warnstd(...) _errmsg_std(msg_warn, __VA_ARGS__)
#define errmsg_warnsys(...) _errmsg_sys(msg_warn, __VA_ARGS__)
#define errmsg_warndl(...) _errmsg_dl(msg_warn, __VA_ARGS__)
#define errmsg_warnx(...) _errmsg_x(msg_warn, __VA_ARGS__)

#define errmsg_errorstd(...) _errmsg_std(msg_error, __VA_ARGS__)
#define errmsg_errorsys(...) _errmsg_sys(msg_error, __VA_ARGS__)
#define errmsg_errordl(...) _errmsg_dl(msg_error, __VA_ARGS__)
#define errmsg_errorx(...) _errmsg_x(msg_error, __VA_ARGS__)

#define errmsg_note(...) _errmsg_x(msg_note, __VA_ARGS__)

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
