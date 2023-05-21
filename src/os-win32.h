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

/* Windows-specific definitions for os.h - don't include this directly! */

#define IMPORT __declspec(dllimport) // only needed for variables
#define EXPORT __declspec(dllexport)

typedef unsigned short os_char;
#define _OS_CAT(L, x) L##x
#define OS_LIT(x) _OS_CAT(L, x)

#define os_snprintf _snwprintf
#define os_strchr wcschr
#define os_strcmp wcscmp
#define os_strcpy wcscpy
#define os_strlen wcslen
#define strncasecmp _strnicmp // stupid!

#define os_fopen _wfopen
// yuck :(
#define _os_open3(path, flags, mode) _wopen((path), (flags) | _O_BINARY, (mode))
#define _os_open2(path, flags) _wopen((path), (flags) | _O_BINARY)
#define _os_open(a, b, c, x, ...) x
#define os_open(...) _os_open(__VA_ARGS__, _os_open3, _os_open2, _)(__VA_ARGS__)
#define os_access _waccess
// ucrt defines __stat64 to _stat64. we want _wstat64 to be the actual function
#define _stat64(path, buf) _wstat64(path, buf)
#define os_stat _stat64
#define os_mkdir _wmkdir
#define os_unlink _wunlink
#define os_getenv _wgetenv
#define os_getcwd _wgetcwd

#define OS_DLSUFFIX ".dll"

#define OS_MAIN wmain

static inline void *os_dlsym(void *m, const char *s) {
	return (void *)GetProcAddress(m, s);
}

static inline int os_dlfile(void *m, unsigned short *buf, int sz) {
	unsigned int n = GetModuleFileNameW(m, buf, sz);
	if (n == 0 || n == sz) return -1;
	// get the canonical capitalisation, as for some reason GetModuleFileName()
	// returns all lowercase. this doesn't really matter except it looks nicer
	GetLongPathNameW(buf, buf, n + 1);
	// the drive letter will also be lower case, if it is an actual drive letter
	// of course. it should be; I'm not gonna lose sleep over UNC paths and such
	if (buf[0] >= L'a' && buf[0] <= L'z' && buf[1] == L':') buf[0] &= ~32u;
	return n;
}

static inline bool os_mprot(void *addr, int len, int fl) {
	unsigned long old;
	return !!VirtualProtect(addr, len, fl, &old);
}

// SystemFunction036 is the *real* name of "RtlGenRandom," and is also
// incorrectly defined in system headers. Yay.
int __stdcall SystemFunction036(void *buf, unsigned long sz);

static inline void os_randombytes(void *buf, int sz) {
	// if this call fails, the system is doomed, so loop until success and pray.
	while (!SystemFunction036(buf, sz));
}

/* Further Windows-specific workarounds because Windows is great */

#ifndef PATH_MAX
// XXX: win32/crt has this dumb 260 limit even though the actual kernel imposes
// no limit (though apparently NTFS has a limit of 65535). Theoretically we
// could do some memes with UNC paths to extend it to at least have parity with
// Unix PATH_MAX (4096), but for now we just kind of accept that Windows is a
// disaster.
#define PATH_MAX MAX_PATH
#endif

// why does Windows prefix so many things with underscores?
#define alloca _alloca
#define read _read
#define write _write
#define close _close
#define fdopen _fdopen
#define dup _dup
#define dup2 _dup2
#define O_RDONLY _O_RDONLY
#define O_RDWR _O_RDWR
#define O_CLOEXEC _O_NOINHERIT // and why did they rename this!?
#define O_CREAT _O_CREAT
#define O_EXCL _O_EXCL
// missing access() flags
#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK R_OK // there's no actual X bit, just pretend

// windows doesn't define this for some reason!? note: add others as needed...
#define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)

// just dump this boilerplate here as well, I spose
#define OS_WINDOWS_ERROR(arrayname) \
	FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, 0, GetLastError(), \
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), arrayname, \
			sizeof(arrayname), 0)

// vi: sw=4 ts=4 noet tw=80 cc=80
