/*
 * Copyright © 2021 Willian Henrique <wsimanbrazil@yahoo.com.br>
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

#ifndef INC_DEMOREC_H
#define INC_DEMOREC_H

#include <stdbool.h>

bool demorec_init(void);
void demorec_end(void);

bool demorec_custom_init(void);

/* maximum length of a custom demo message, in bytes */
#define DEMOREC_CUSTOM_MSG_MAX 253

/*
 * Write a block of up to DEMOWRITER_MSG_MAX bytes into the currently recording
 * demo - NOT bounds checked, caller MUST ensure length is okay!
 */
void demorec_writecustom(void *buf, int len);

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
