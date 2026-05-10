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

#include <fcntl.h>
#include <stdlib.h>
#ifdef _WIN32
#include <Windows.h>
#else
#include <errno.h>
#include <dlfcn.h>
#include <limits.h>
#include <link.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "intdefs.h"
#include "langext.h"
#include "os.h"

// HACK: host tools link in os.c as well. if cross-compiling, we can't use our
// inline asm mem_copy implementation. just use memcpy if not on 32-bit x86.
// the host tools will have the C runtime available, unlike SST itself
#if defined(__i386__) || defined(_M_IX86)
#include "mem.h"
#endif

void os_spancopy(os_char *restrict dest, const os_char *restrict src, int n) {
#if defined(__i386__) || defined(_M_IX86)
	mem_copy(dest, src, n * sizeof(os_char));
#else
	memcpy(dest, src, n * sizeof(os_char));
#endif
}

#ifdef _WIN32

int os_lasterror() { return GetLastError(); }

// N.B. file handle values are 32-bit, even in 64-bit builds. I'm not crazy!

int os_open_read(const ushort *path) {
	return (int)(ssize)CreateFileW(path, GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL, 0);
}
int os_open_write(const ushort *file) {
	return (int)(ssize)CreateFileW(file, GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, 0);
}
int os_open_writetrunc(const ushort *file) {
	return (int)(ssize)CreateFileW(file, GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE, 0, CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, 0);
}

int os_read(int f, void *buf, int max) {
	ulong n;
	return ReadFile((void *)(ssize)f, buf, max, &n, 0) ? n : -1;
}
int os_write(int f, const void *buf, int len) {
	ulong n;
	return WriteFile((void *)(ssize)f, buf, len, &n, 0) ? n : -1;
}

vlong os_fsize(int f) {
	vlong ret;
	if_cold (!GetFileSizeEx((void *)(ssize)f, (LARGE_INTEGER *)&ret)) return -1;
	return ret;
}

void os_close(int f) {
	CloseHandle((void *)(ssize)f);
}

void os_getcwd(ushort buf[static 260]) { GetCurrentDirectoryW(260, buf); }

bool os_mkdir(const ushort *path) { return CreateDirectoryW(path, 0); }
bool os_unlink(const ushort *path) { return DeleteFileW(path); }
bool os_rmdir(const ushort *path) { return RemoveDirectoryW(path); }

int __stdcall ProcessPrng(char *data, usize sz); // from bcryptprimitives.dll
void os_randombytes(void *buf, int sz) { ProcessPrng(buf, sz); }

void *os_dlhandle(const ushort *name) {
	return GetModuleHandleW(name);
}
void *os_dlsym(void *restrict lib, const char *restrict s) {
	return (void *)GetProcAddress(lib, s);
}
int os_dlfile(void *lib, ushort *buf, int sz) {
	unsigned int n = GetModuleFileNameW(lib, buf, sz);
	if_cold (n == 0 || n == sz) return -1;
	// get the canonical capitalisation, as for some reason GetModuleFileName()
	// returns all lowercase. this doesn't really matter except it looks nicer
	GetLongPathNameW(buf, buf, n + 1);
	// the drive letter will also be lower case, if it is an actual drive letter
	// of course. it should be; I'm not gonna lose sleep over UNC paths and such
	if_hot (buf[0] >= L'a' && buf[0] <= L'z' && buf[1] == L':') buf[0] &= ~32u;
	return n;
}

void *os_pagealloc(int sz, int prot) {
	return VirtualAlloc(0, sz, MEM_RESERVE | MEM_COMMIT, prot);
}
void os_pagefree(void *addr, int sz) {
	VirtualFree(addr, sz, MEM_RELEASE);
}

bool os_mprot(void *addr, int len, int mode) {
	ulong old;
	return !!VirtualProtect(addr, len, mode, &old);
}
bool os_mlock(void *addr, int sz) {
	return VirtualLock(addr, 4096);
}

#else

int os_lasterror() { return errno; }

int os_open_read(const char *path) {
	return open(path, O_RDONLY | O_CLOEXEC);
}
int os_open_write(const char *path) {
	return open(path, O_RDWR | O_CLOEXEC | O_CREAT, 0644);
}
int os_open_writetrunc(const char *path) {
	return open(path, O_RDWR | O_CLOEXEC | O_CREAT | O_TRUNC, 0644);
}
int os_read(int f, void *buf, int max) { return read(f, buf, max); }
int os_write(int f, const void *buf, int max) { return write(f, buf, max); }
void os_close(int f) { close(f); }

vlong os_fsize(int f) {
	struct stat s;
	if_cold (stat(f, &s) == -1) return -1;
	return s.st_size;
}

void os_getcwd(char buf[PATH_MAX]) { getcwd(buf, PATH_MAX); }

bool os_mkdir(const char *path) { return mkdir(path, 0555) != -1; }
bool os_unlink(const char *path) { return unlink(path) != -1; }
bool os_rmdir(const char *path) { return rmdir(path) != -1; }

void *os_dlsym(void *restrict lib, const char *restrict name) {
	return dlsym(lib, name);
}

void *os_pagealloc(int sz, int prot) {
	sz = sz + 4095 & ~4095; // round up size
	void *ret = mmap(0, sz, prot, MAP_SHARED | MAP_ANON, -1, 0);
	if (ret == MAP_FAILED) return 0;
	return ret;
}
void os_pagefree(void *addr, int sz) {
	// round down address and round up size
	addr = (void *)((ulong)addr & ~4095);
	len = len + 4095 & ~4095;
	munmap(addr, len);
}

bool os_mprot(void *addr, int len, int mode) {
	// round down address and round up size
	addr = (void *)((ulong)addr & ~4095);
	len = len + 4095 & ~4095;
	return mprotect(addr, len, mode) != -1;
}

bool os_mlock(void *addr, int sz) {
#ifdef __linux__
	// Linux-specific madvise stuff (there are some equivalents in OpenBSD and
	// FreeBSD, if anyone's wondering, but we don't use need them for our
	// purposes).
	if (madvise(addr, len, MADV_DONTFORK) == -1 ||
			madvise(addr, len, MADV_DONTDUMP) == - 1) {
		return false;
	}
#endif
	return mlock(addr, len) != -1;
}

void os_randombytes(void *buf, int sz) { while (getentropy(buf, sz) == -1); }

#endif

#ifdef __linux__
// glibc-specific stuff here. it shouldn't be used in build-time code, just
// the plugin itself (that really shouldn't be a problem).

// private struct hidden behind _GNU_SOURCE. see dlinfo(3) or <link.h>
struct link_map {
	ulong l_addr;
	const char *l_name;
	void *l_ld;
	struct link_map *l_next, *l_prev;
	// [more private stuff below]
};

static struct link_map *lmbase = 0;

void *os_dlhandle(const char *name) {
	if_cold (!lmbase) { // IMPORTANT: not thread safe. don't forget later!
		lmbase = (struct link_map *)dlopen("libc.so.6", RTLD_LAZY | RTLD_NOLOAD);
		dlclose(lmbase); // assume success
		while (lmbase->l_prev) lmbase = lmbase->l_prev;
	}
	// this is a tiny bit crude, but basically okay. we just want to find
	// something that roughly matches the basename, rather than needing an exact
	// path, in a manner vaguely similar to Windows' GetModuleHandle(). that way
	// we can just look up client.so or something without having to figure out
	// where exactly that is.
	for (struct link_map *lm = lmbase; lm; lm = lm->l_next) {
		if (name[0] == '/') {
			if (!strcmp(name, lm->l_name)) return lm;
			continue;
		}
		int namelen = strlen(lm->l_name);
		int sublen = strlen(name);
		if (sublen >= namelen) continue;
		if (lm->l_name[namelen - sublen - 1] == '/' &&
				!memcmp(lm->l_name + namelen - sublen, name, sublen)) {
			return lm;
		}
	}
	return 0;
}

int os_dlfile(void *lib, char *buf, int sz) {
	struct link_map *lm = lib;
	int ssz = strlen(lm->l_name) + 1;
	if_cold (ssz > sz) { errno = ENAMETOOLONG; return -1; }
	mem_copy(buf, lm->l_name, ssz);
	return ssz;
}
#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
