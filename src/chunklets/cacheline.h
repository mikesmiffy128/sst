/* This file is dedicated to the public domain. */

#ifndef INC_CHUNKLETS_CACHELINE_H
#define INC_CHUNKLETS_CACHELINE_H

/*
 * CACHELINE_SIZE is the size/alignment which can be reasonably assumed to fit
 * in a single cache line on the target architecture. Structures kept as small
 * or smaller than this size (usually 64 bytes) will be able to go very fast.
 */
#ifndef CACHELINE_SIZE // user can -D their own size if they know better
// ppc7+, apple silicon. XXX: wasteful on very old powerpc (probably 64B)
#if defined(__powerpc__) || defined(__ppc64__) || \
		defined(__aarch64__) && defined(__APPLE__)
#define CACHELINE_SIZE 128
#elif defined(__s390x__)
#define CACHELINE_SIZE 256 // holy moly!
#elif defined(__mips__) || defined(__riscv__)
#define CACHELINE_SIZE 32 // lower end of range, some chips could have 64
#else
#define CACHELINE_SIZE 64
#endif
#endif

/*
 * CACHELINE_FALSESHARE_SIZE is the largest size/alignment which might get
 * interfered with by a single write. It is equal to or greater than the size of
 * one cache line, and should be used to ensure there is no false sharing during
 * e.g. lock contention, or atomic fetch-increments on queue indices.
 */
#ifndef CACHELINE_FALSESHARE_SIZE
// modern intel CPUs sometimes false-share *pairs* of cache lines
#if defined(__i386__) || defined(__x86_64__) || defined(_M_X86) || \
	defined(_M_IX86)
#define CACHELINE_FALSESHARE_SIZE (CACHELINE_SIZE * 2)
#elif CACHELINE_SIZE < 64
#define CACHELINE_FALSESHARE_SIZE 64 // be paranoid on mips and riscv
#else
#define CACHELINE_FALSESHARE_SIZE CACHELINE_SIZE
#endif
#endif

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
