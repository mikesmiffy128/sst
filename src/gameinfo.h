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

#ifndef INC_GAMEINFO_H
#define INC_GAMEINFO_H

#include "intdefs.h"
#include "os.h"

/* These variables are only set after calling gameinfo_init(). */
extern const char *gameinfo_title;			/* Name of the game (window title) */
extern const os_char *gameinfo_clientlib;	/* Path to the client library */
extern const os_char *gameinfo_serverlib;	/* Path to the server library */

/*
 * This function is called early in the plugin load and does a whole bunch of
 * spaghetti magic to figure out which game/engine we're in and where its
 * libraries (which we want to hook) are located.
 */
bool gameinfo_init(void);

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
