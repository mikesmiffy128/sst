/*
 * Copyright © 2023 Michael Smith <mikesmiffy128@gmail.com>
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

#ifndef INC_FASTFWD_H
#define INC_FASTFWD_H

/*
 * Fast-forwards in-game time by a number of seconds, ignoring the usual
 * host_framerate and host_timescale settings. timescale controls how many
 * seconds of game pass per real-time second.
 */
void fastfwd(float seconds, float timescale);

/*
 * Extends the length of an ongoing fast-forward (or otherwise does the same as
 * fastfwd() above). Overrides the existing timescale speed, for lack of better
 * idea of what to do.
 */
void fastfwd_add(float seconds, float timescale);

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
