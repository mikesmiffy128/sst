/*
 * Copyright © 2025 Michael Smith <mikesmiffy128@gmail.com>
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

#ifdef INC_LANGEXT_H // we can't rely on include order!
#undef noreturn // ugh, this breaks Windows headers. how annoying. temp-undef it
#endif

#include <string.h>
#include <sys/stat.h> // XXX: try abstracting stat() and avoiding ucrt dep here

#ifdef _WIN32

#include <wchar.h> // XXX: there's kind of a lot of junk in this header!

typedef unsigned short os_char;
#define _OS_CAT(x, y) x##y
#define OS_LIT(x) _OS_CAT(L, x)
#define os_strcmp wcscmp
#define os_strlen wcslen
// ucrt defines __stat64 to _stat64. we want _wstat64 to be the actual function
#define _stat64(path, buf) _wstat64(path, buf)
#define os_stat _stat64

#define OS_DLPREFIX ""
#define OS_DLSUFFIX ".dll"

#define OS_MAIN wmain

// add to these as needed...
#define OS_EEXIST /*ERROR_ALREADY_EXISTS*/ 183
#define OS_ENOENT /*ERROR_PATH_NOT_FOUND*/ 3

// Further Windows-specific workarounds because Windows is great...

#ifndef PATH_MAX
// XXX: win32/crt has this dumb 260 limit even though the actual kernel imposes
// no limit (though apparently NTFS has a limit of 65535). Theoretically we
// could do some memes with UNC paths to extend it to at least have parity with
// Unix PATH_MAX (4096), but for now we just kind of accept that Windows is a
// disaster.
#define PATH_MAX 260
#endif

// windows doesn't define this for some reason!? note: add others as needed...
#define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)

#define alloca _alloca // ugh!

// one last thing: mprot constants are part of os.h API, whether or not
// Windows.h was included. this is a bit of a hack, but whatever!
#ifndef _INC_WINDOWS
#define PAGE_NOACCESS			1
#define PAGE_READONLY			2
#define PAGE_READWRITE			4
#define PAGE_EXECUTE_READ		32
#define PAGE_EXECUTE_READWRITE	64
#endif

#else

#include <errno.h> // meh

// trying to avoid pulling in unnecessary headers as much as possible: define
// our own constants for os_mprot() / mprotect()
#if defined(__linux__) // apparently linux is pretty much the sole oddball here!
#define _OS_PROT_READ 4
#define _OS_PROT_WRITE 2
#define _OS_PROT_EXEC 1
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || \
		defined(__DragonFly__) || defined(__sun) || defined(__serenity)
#define _OS_PROT_READ 1
#define _OS_PROT_WRITE 2
#define _OS_PROT_EXEC 4
#elif !defined(_WIN32) // what else could this possibly even be!?
#include <sys/mman.h> // just fall back on this. not the end of the world...
#define _OS_PROT_READ PROT_READ
#define _OS_PROT_WRITE PROT_WRITE
#define _OS_PROT_EXEC PROT_EXEC
#endif

typedef char os_char;
#define OS_LIT(x) x
#define os_strcmp strcmp
#define os_strlen strlen
#define os_stat stat

#define OS_DLPREFIX "lib"
#define OS_DLSUFFIX ".so"

#define OS_MAIN main

// unix mprotect flags are much nicer but cannot be defined in terms of the
// windows ones, so we use the windows ones and define them in terms of the unix
// ones. another victory for stupid!
#define PAGE_NOACCESS			0
#define PAGE_READONLY			_OS_PROT_READ
#define PAGE_READWRITE			_OS_PROT_READ | _OS_PROT_WRITE
#define PAGE_EXECUTE_READ		_OS_PROT_READ |                  _OS_PROT_EXEC
#define PAGE_EXECUTE_READWRITE	_OS_PROT_READ | _OS_PROT_WRITE | _OS_PROT_EXEC

#define OS_EEXIST EEXIST
#define OS_ENOENT ENOENT

#endif

/* Copies n characters from src to dest, using the OS-specific char type. */
static inline void os_spancopy(os_char *restrict dest,
		const os_char *restrict src, int n) {
	memcpy(dest, src, n * sizeof(os_char));
}

/*
 * Returns the last error code from an OS function - equivalent to calling
 * GetLastError() in Windows and reading errno in Unix-like operating systems.
 * For standard libc functions (implemented by UCRT on Windows), the value of
 * errno should be used directly instead.
 */
int os_lasterror();

/*
 * Opens a file for reading. Returns an OS-specific file handle, or -1 on error.
 */
int os_open_read(const os_char *path);

/*
 * Opens a file for writing, creating it if required. Returns an OS-specific
 * file handle, or -1 on error.
 */
int os_open_write(const os_char *path);

/*
 * Opens a file for writing, creating it if required, or truncating it if it
 * already exists. Returns an OS-specific file handle, or -1 on error.
 */
int os_open_writetrunc(const os_char *path);

/*
 * Reads up to max bytes from OS-specific file handle f into buf. Returns the
 * number of bytes read, or -1 on error.
 */
int os_read(int f, void *buf, int max);

/*
 * Reads up to len bytes from buf to OS-specific file handle f. Returns the
 * number of bytes written, or -1 on error. Generally the number of bytes
 * written will be len, unless writing to a pipe/socket, which has a limited
 * internal buffer, or possibly being preempted by a signal handler.
 */
int os_write(int f, const void *buf, int len);

/*
 * Returns the length of the on-disk file referred to by OS-specific file handle
 * f, or -1 on error (the most likely error would be that the file is not
 * actually on disk and instead refers to a pipe or something).
 */
long long os_fsize(int f);

/*
 * Closes the OS-specific file handle f. On Windows, this causes pending writes
 * to be flushed; on Unix-likes, this generally happens asynchronously. If
 * blocking is a concern when writing files to disk, some sort of thread pool
 * should always be used.
 */
void os_close(int f);

/*
 * Gets the current working directory, which may be up to PATH_MAX characters in
 * length (using the OS-specific character type).
 */
void os_getcwd(os_char buf[static PATH_MAX]);

/*
 * Tries to create a directory at path. Returns true on success, false on
 * failure. One may wish to ignore a failure if the directory already exists;
 * this can be done by checking if os_lasterror() returns OS_EEXIST.
 */
bool os_mkdir(const os_char *path);

/*
 * Tries to delete a file(name) at path. Returns true on success, false on
 * failure. One may wish to ignore a failure if the file already doesn't exist;
 * this can be done by checking if os_lasterror() returns OS_ENOENT.
 *
 * On some Unix-like operating systems, this may work on empty directories, but
 * for portably reliable results, one should call os_rmdir() for that instead.
 */
bool os_unlink(const os_char *path);

/*
 * Tries to delete a directory at path. Returns true on success, false on
 * failure. One may wish to ignore a failure if the directory already doesn't
 * exist; this can be done by checking if os_lasterror() returns OS_ENOENT.
 */
bool os_rmdir(const os_char *path);

/*
 * Tries to look up the symbol sym from the shared library handle lib. Returns
 * an opaque pointer on success, or null on failure.
 */
void *os_dlsym(void *restrict lib, const char *restrict sym);

#if defined(_WIN32) || defined(__linux__)
/*
 * Tries to get a handle to an already-loaded shared library matching name.
 * Returns the library handle on success, or null on failure.
 */
void *os_dlhandle(const os_char *name);

/*
 * Tries to determine the file path to the shared library handle lib, and stores
 * it in buf (with max length given by sz). Returns the length of the resulting
 * string, or -1 on failure.
 */
int os_dlfile(void *lib, os_char *buf, int sz);
#endif

/*
 * Changes memory protection for the address range given by addr and len, using
 * one of the Win32-style PAGE_* flags specified above. Returns true on success
 * and false on failure.
 */
bool os_mprot(void *addr, int len, int mode);

/*
 * Fills buf with up to sz cryptographically random bytes. sz has an OS-specific
 * upper limit - a safe value across all major operating systems is 256.
 */
void os_randombytes(void *buf, int sz);

#ifdef INC_LANGEXT_H
#define noreturn _Noreturn void // HACK: put this back if undef'd above
#endif

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
