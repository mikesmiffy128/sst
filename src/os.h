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

#ifndef INC_OS_H
#define INC_OS_H

#include <stdbool.h>

/*
 * Here we declare an absolute ton of wrappers, macros, compatibility shims,
 * reimplementations and so on to try in vain to sweep the inconsistencies
 * between Windows and not-Windows under the rug.
 *
 * If this file gets any bigger it might need to be split up a bit...
 */

#include <fcntl.h>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <io.h>
#include <wchar.h>
#include <Windows.h>
#else
#include <dlfcn.h>
#include <limits.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "intdefs.h"

#ifdef _WIN32

#define IMPORT __declspec(dllimport) // only needed for variables
#define EXPORT __declspec(dllexport)
void *os_dlsym(void *mod, const char *sym);
#define os_char ushort
#define _OS_CAT(L, x) L##x
#define OS_LIT(x) _OS_CAT(L, x)
#define os_snprintf _snwprintf
#define os_strchr wcschr
#define os_strcmp wcscmp
#define os_strcpy wcscpy
#define os_strlen wcslen
#define strncasecmp _strnicmp // stupid!
#define OS_DLSUFFIX ".dll"
#ifndef PATH_MAX
// XXX win32/crt has this dumb 260 limit even though the actual kernel imposes
// no limit (though apparently NTFS has a limit of 65535). Theoerically we could
// do some memes with UNC paths to extend it to at least have parity with Unix
// PATH_MAX (4096), but for now we just kind of accept that Windows is a
// disaster.
#define PATH_MAX MAX_PATH
#endif
#define os_fopen _wfopen
// yuck :(
#define _os_open3(path, flags, mode) _wopen((path), (flags) | _O_BINARY, (mode))
#define _os_open2(path, flags) _wopen((path), (flags) | _O_BINARY)
#define _os_open(a, b, c, x, ...) x
#define os_open(...) _os_open(__VA_ARGS__, _os_open3, _os_open2, _)(__VA_ARGS__)
#define os_access _waccess
#define os_stat _stat64
// ucrt defines __stat64 to _stat64. we want _wstat64 to be the actual function
#define _stat64(path, buf) _wstat64(path, buf)
// why exactly _does_ windows prefix so many things with underscores?
#define read _read
#define write _write
#define close _close
#define O_RDONLY _O_RDONLY
#define O_RDWR _O_RDWR
#define O_CLOEXEC _O_NOINHERIT
#define O_CREAT _O_CREAT
#define O_EXCL _O_EXCL
#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK R_OK // there's no actual X bit
#define alloca _alloca
#define os_getenv _wgetenv
#define OS_MAIN wmain
// just dump this boilerplate here as well, I spose
#define OS_WINDOWS_ERROR(arrayname) \
	FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, 0, GetLastError(), \
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), arrayname, \
			sizeof(arrayname), 0)

#else

#ifdef __GNUC__
#define EXPORT __attribute__((visibility("default")))
#else
#define EXPORT int novis[-!!"visibility attribute requires Clang or GCC"];
#endif
#define IMPORT
#define os_dlsym dlsym
#define os_char char
#define OS_LIT(x) x
#define os_snprintf snprintf
#define os_strchr strchr
#define os_strcmp strcmp
#define os_strcpy strcpy
#define os_strlen strlen
#define OS_DLSUFFIX ".so"
#define os_fopen fopen
#define os_open open
#define os_access access
#define os_stat stat
// unix mprot flags are much nicer but cannot be defined in terms of the windows
// ones, so we use the windows ones and define them in terms of the unix ones.
// another victory for stupid!
#define PAGE_NOACCESS			0
#define PAGE_READONLY			PROT_READ
#define PAGE_READWRITE			PROT_READ | PROT_WRITE
#define PAGE_EXECUTE_READ		PROT_READ |              PROT_EXEC
#define PAGE_EXECUTE_READWRITE	PROT_READ | PROT_WRITE | PROT_EXEC
#define os_getenv getenv
#define OS_MAIN main

#endif

bool os_mprot(void *addr, int len, int fl);
/*
 * NOTE: this should be called with a reasonably small buffer (e.g., the size of
 * a private key). The maximum size of the buffer on Linux is 256, on Windows
 * it's God Knows What.
 */
void os_randombytes(void *buf, int len);

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
