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

#ifndef INC_HEXCOLOUR_H
#define INC_HEXCOLOUR_H

#include "intdefs.h"

/*
 * Parses a user-provided RGB hex string, writing the RGBA bytes to out. May or
 * may not modify the alpha byte; it is assumed to always be set to 255. That
 * probably sounds dumb but makes sense for our specific use cases.
 *
 * Falls back on white if the input isn't valid. This also makes sense for our
 * use cases.
 */
void hexcolour_rgb(uchar out[static 4], const char *s);

/*
 * Parses a user-provided RGBA hex string, writing the RGBA bytes to out.
 * If both the alpha digits are missing, provides full opacity instead. If the
 * value is malformed in some other way, falls back on solid white.
 */
void hexcolour_rgba(uchar out[static 4], const char *s);

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
