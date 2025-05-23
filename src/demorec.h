/*
 * Copyright © Willian Henrique <wsimanbrazil@yahoo.com.br>
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

#ifndef INC_DEMOREC_H
#define INC_DEMOREC_H

#include "event.h"

// For internal use by democustom. Consider this opaque.
// XXX: should the struct be put in engineapi or something?
extern struct CDemoRecorder { void **vtable; } *demorecorder;

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
 * have the _N.dem suffixes). Value will be zero if the recording is stopped
 * before the game has even gotten a chance to create the first demo file.
 */
int demorec_stop();

/*
 * Returns the current number in the recording sequence, or -1 if not recording.
 * Value may be 0 if recording was requested but has yet to start (say, because
 * we have yet to join a map).
 */
int demorec_demonum();

/*
 * Used to determine whether to allow usage of the normal record and stop
 * commands. Code which takes over control of demo recording can use this to
 * block the user from interfering.
 */
DECL_PREDICATE(DemoControlAllowed, void)

/*
 * Emitted whenever a recording session is about to be started, as a result of
 * either the record command or a call to the demorec_start() function. A demo
 * file won't actually have been created yet; this merely indicates that a
 * request to record has happened.
 */
DECL_EVENT(DemoRecordStarting, void)

/*
 * Emitted when the current demo or series of demos has finished recording.
 * Receives the number of recorded demo files (which could be 0) as an argument.
 */
DECL_EVENT(DemoRecordStopped, int)

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
