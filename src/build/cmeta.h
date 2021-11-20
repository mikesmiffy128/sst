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

#ifndef INC_CMETA_H
#define INC_CMETA_H

#include <stdbool.h>

#include "../os.h"

struct cmeta;

const struct cmeta *cmeta_loadfile(const os_char *f);

/*
 * Iterates through all the #include directives in a file, passing each one in
 * turn to the callback cb.
 */
void cmeta_includes(const struct cmeta *cm,
		void (*cb)(const char *f, bool issys, void *ctxt), void *ctxt);

/*
 * Iterates through all commands and variables declared using the macros in
 * con_.h, passing each one in turn to the callback cb.
 */
void cmeta_conmacros(const struct cmeta *cm,
		void (*cb)(const char *name, bool isvar));

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
