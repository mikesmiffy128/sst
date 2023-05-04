/*
 * Copyright © 2021 Willian Henrique <wsimanbrazil@yahoo.com.br>
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

#ifndef INC_DEMOREC_H
#define INC_DEMOREC_H

#include "event.h"

// For internal use by democustom
extern void *demorecorder;

/*
 * Whether to ignore the value of the sst_autorecord cvar and just keep
 * recording anyway. Will be used to further automate demo recording later.
 */
extern bool demorec_forceauto;

/*
 * The current/last basename for recorded demos - to which _2, _3, etc. is
 * appended by the engine. May contain garbage or null bytes if recording hasn't
 * been started.
 *
 * This is currently implemented as a pointer directly inside the engine demo
 * recorder instance.
 */
extern const char *demorec_basename;

/*
 * Starts recording a demo with the provided name (or relative path). If a demo
 * is already being recorded, or the path was deemed invalid by the game, does
 * nothing and returns false. Otherwise returns true.
 *
 * Assumes any subdirectories already exist - recording may silently fail
 * otherwise.
 */
bool demorec_start(const char *name);

/*
 * Stops recording the current demo and returns the number of demos recorded
 * (the first will have the original basename + .dem extension; the rest will
 * have the _N.dem suffixes).
 */
int demorec_stop(void);

/*
 * Queries whether a demo is currently being recorded.
 */
bool demorec_recording(void);

/*
 * Used to determine whether to allow usage of the normal record and stop
 * commands. Code which takes over control of demo recording can use this to
 * block the user from interfering.
 */
DECL_PREDICATE(AllowDemoControl)

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
