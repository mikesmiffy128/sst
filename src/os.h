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

#ifndef INC_OS_H
#define INC_OS_H

#include <stdbool.h>

/*
 * Here we declare an absolute ton of wrappers, macros, compatibility shims,
 * reimplementations and so on to try in vain to sweep the inconsistencies
 * between Windows and not-Windows under the rug.
 */

#include <errno.h>
#include <fcntl.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <wchar.h>
// DUMB HACK: noreturn.h is alphabetically before os.h so including it after
// looks weird and inconsistent. including it before breaks Windows.h though
// because of __declspec(noreturn). Undef for now, and restore at the end of
// this header.
#undef noreturn
#include <Windows.h>
#include <winternl.h>
#else
#include <dlfcn.h>
#include <limits.h>
#include <link.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

// Things were getting unwieldy so they're now split into these headers...
#ifdef _WIN32
#include "os-win32.h"
// DUMB HACK part 2: restore what the #including source file had asked for
#ifdef INC_NORETURN_H
#define noreturn _Noreturn void
#endif
#else
#include "os-unix.h"
#endif

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
