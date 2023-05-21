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

/* Unix-specific definitions for os.h - don't include this directly! */

#ifdef __GNUC__
#define EXPORT __attribute__((visibility("default")))
#else
#define EXPORT int novis[-!!"visibility attribute requires Clang or GCC"];
#endif
#define IMPORT

typedef char os_char;
#define OS_LIT(x) x
#define os_snprintf snprintf
#define os_strchr strchr
#define os_strcmp strcmp
#define os_strcpy strcpy
#define os_strlen strlen
#define os_fopen fopen
#define os_open open
#define os_access access
#define os_stat stat
#define os_mkdir(f) mkdir(f, 0755) // use real mkdir(2) if the mode matters
#define os_unlink unlink
#define os_getenv getenv
#define os_getcwd getcwd

#define OS_DLSUFFIX ".so"

#define OS_MAIN main

#define os_dlsym dlsym

#ifdef __linux__
// note: this is glibc-specific. it shouldn't be used in build-time code, just
// the plugin itself (that really shouldn't be a problem).
static inline int os_dlfile(void *m, char *buf, int sz) {
	// private struct hidden behind _GNU_SOURCE. see dlinfo(3) or <link.h>
	struct gnu_link_map {
		unsigned long l_addr;
		const char *l_name;
		void *l_ld;
		struct gnu_link_map *l_next, *l_prev;
		// [more private stuff below]
	};
	struct gnu_link_map *lm = m;
	int ssz = strlen(lm->l_name) + 1;
	if (ssz > sz) { errno = ENAMETOOLONG; return -1; }
	memcpy(buf, lm->l_name, ssz);
	return ssz;
}
#endif

// unix mprot flags are much nicer but cannot be defined in terms of the windows
// ones, so we use the windows ones and define them in terms of the unix ones.
// another victory for stupid!
#define PAGE_NOACCESS			0
#define PAGE_READONLY			PROT_READ
#define PAGE_READWRITE			PROT_READ | PROT_WRITE
#define PAGE_EXECUTE_READ		PROT_READ |              PROT_EXEC
#define PAGE_EXECUTE_READWRITE	PROT_READ | PROT_WRITE | PROT_EXEC

static inline bool os_mprot(void *addr, int len, int fl) {
	// round down address and round up size
	addr = (void *)((unsigned long)addr & ~(4095));
	len = len + 4095 & ~(4095);
	return mprotect(addr, len, fl) != -1;
}

static inline void os_randombytes(void *buf, int sz) {
	// if this call fails, the system is doomed, so loop until success and pray.
	while (getentropy(buf, sz) == -1);
}

// vi: sw=4 ts=4 noet tw=80 cc=80
