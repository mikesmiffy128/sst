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

#ifndef INC_L4DMM_H
#define INC_L4DMM_H

/*
 * Returns the ID of the current campaign, like L4D2C2 (L4D2) or Farm (L4D1).
 * Copies to an internal buffer if required, so the caller is not required to
 * manage memory.
 *
 * Returns null if no map is loaded (or the relevant metadata is somehow
 * missing).
 */
const char *l4dmm_curcampaign(void);

/*
 * Returns true if the current map is known to be the first map of a campaign,
 * false otherwise.
 */
bool l4dmm_firstmap(void);

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
