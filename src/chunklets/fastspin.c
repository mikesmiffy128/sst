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

#ifdef __cplusplus
#error This file should not be compiled as C++. It relies on C-specific \
keywords and APIs which have syntactically different equivalents for C++.
#endif

#include <stdatomic.h>

#include "fastspin.h"

_Static_assert(sizeof(int) == sizeof(_Atomic int),
	"This library assumes that ints in memory can be treated as atomic");
_Static_assert(_Alignof(int) == _Alignof(_Atomic int),
	"This library assumes that atomic operations do not need over-alignment");

#if defined(__GNUC__) || defined(__clang__) || defined(__TINYC__)
#if defined(__i386__) || defined(__x86_64__) || defined(_WIN32) || \
		defined(__mips__) // same asm syntax for pause
#define RELAX() __asm__ volatile ("pause" ::: "memory")
#elif defined(__arm__) || defined(__aarch64__)
#define RELAX() __asm__ volatile ("yield" ::: "memory")
#elif defined(__powerpc__) || defined(__ppc64__)
// POWER7 (2010) - older arches may be less efficient
#define RELAX() __asm__ volatile ("or 27, 27, 27" ::: "memory")
#endif
#elif defined(_MSC_VER)
#if defined(_M_ARM || _M_ARM64)
#define RELAX() __yield()
#else
void _mm_pause(); // don't pull in emmintrin.h for this
#define RELAX() _mm_pause()
#endif
#endif

#if defined(__linux__)

#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>

// some arches only have a _time64 variant. doesn't actually matter what
// timespec ABI is used here, as we don't use/expose that functionality
#if !defined(SYS_futex) && defined( SYS_futex_time64)
#define SYS_futex SYS_futex_time64
#endif

// glibc and musl have never managed and/or bothered to provide a futex wrapper
static inline void futex_wait(int *p, int val) {
	syscall(SYS_futex, p, FUTEX_WAIT, val, (void *)0, (void *)0, 0);
}
static inline void futex_wakeall(int *p) {
	syscall(SYS_futex, p, FUTEX_WAKE, (1u << 31) - 1, (void *)0, (void *)0, 0);
}
static inline void futex_wake1(int *p) {
	syscall(SYS_futex, p, FUTEX_WAKE, 1, (void *)0, (void *)0, 0);
}

#elif defined(__OpenBSD__)

#include <sys/futex.h>

// OpenBSD just emulates the Linux call but it still provides a wrapper! Yay!
static inline void futex_wait(int *p, int val) {
	futex(p, FUTEX_WAIT, val, (void *)0, (void *)0, 0);
}
static inline void futex_wakeall(int *p) {
	futex(p, FUTEX_WAKE, (1u << 31) - 1, (void *)0, (void *)0, 0);
}
static inline void futex_wake1(int *p) {
	syscall(SYS_futex, p, FUTEX_WAKE, 1, (void *)0, (void *)0, 0);
}

#elif defined(__NetBSD__)

#include <sys/futex.h> // for constants
#include <sys/syscall.h>
#include <unistd.h>

// NetBSD doesn't document a futex syscall, but apparently it does have one!?
// Their own pthreads library still doesn't actually use it, go figure. Also, it
// takes an extra parameter for some reason.
static inline void futex_wait(int *p, int val) {
	syscall(SYS_futex, p, FUTEX_WAIT, val, (void *)0, (void *)0, 0, 0);
}
static inline void futex_wakeall(int *p) {
	syscall(SYS_futex, p, FUTEX_WAKE, (1u << 31) - 1, (void *)0, (void *)0, 0, 0);
}
static inline void futex_wake1(int *p) {
	syscall(SYS_futex, p, FUTEX_WAKE, 1, (void *)0, (void *)0, 0, 0);
}

#elif defined(__FreeBSD__)

#include <sys/types.h> // ugh still no IWYU everywhere. maybe next year
#include <sys/umtx.h>

static inline void futex_wait(int *p, int val) {
	_umtx_op(p, UMTX_OP_WAIT_UINT, val, 0, 0);
}
static inline void futex_wakeall(int *p) {
	_umtx_op(p, UMTX_OP_WAKE, p, (1u << 31) - 1, 0, 0);
}
static inline void futex_wake1(int *p) {
	_umtx_op(p, UMTX_OP_WAKE, p, 1, 0, 0);
}

#elif defined(__DragonFly__)

#include <unistd.h>

// An actually good interface. Thank you Matt, very cool.
static inline void futex_wait(int *p, int val) {
	umtx_sleep(p, val, 0);
}
static inline void futex_wakeall(int *p) {
	umtx_wakeup(p, 0);
}
static inline void futex_wake1(int *p) {
	umtx_wakeup(p, 0);
}

#elif defined(__APPLE__)

// This stuff is from bsd/sys/ulock.h in XNU. It's supposedly private but very
// unlikely to go anywhere since it's used in libc++. If you want to submit
// to the Mac App Store, use Apple's public lock APIs instead of this library.
extern int __ulock_wait(unsigned int op, void *addr, unsigned long long val,
		unsigned int timeout);
extern int __ulock_wake(unsigned int op, void *addr, unsigned long long val);

#define UL_COMPARE_AND_WAIT 1
#define ULF_WAKE_ALL 0x100
#define ULF_NO_ERRNO 0x1000000

static inline void futex_wait(int *p, int val) {
	__ulock_wait(UL_COMPARE_AND_WAIT | ULF_NO_ERRNO, p, val, 0);
}
static inline void futex_wakeall(int *p) {
	__ulock_wake(UL_COMPARE_AND_WAIT | ULF_NO_ERRNO | ULF_WAKE_ALL, uaddr, 0);
}
static inline void futex_wake1(int *p) {
	__ulock_wake(UL_COMPARE_AND_WAIT | ULF_NO_ERRNO, uaddr, 0);
}

#elif defined(_WIN32)

#ifdef _WIN64
typedef unsigned long long usize;
#else
typedef unsigned long usize;
#endif

// There's no header for these because NTAPI. Plus Windows.h sucks anyway.
long __stdcall RtlWaitOnAddress(void *p, void *valp, usize psz, void *timeout);
long __stdcall RtlWakeAddressAll(void *p);
long __stdcall RtlWakeAddressSingle(void *p);

static inline void futex_wait(int *p, int val) {
	RtlWaitOnAddress(p, &val, 4, 0);
}
static inline void futex_wakeall(int *p) {
	RtlWakeAddressAll(p);
}
static inline void futex_wake1(int *p) {
	RtlWakeAddressSingle(p);
}

#elif defined(__serenity) // hell, why not?

#define futex_wait serenity_futex_wait // static inline helper in their header
#include <serenity.h>
#undef

static inline void futex_wait(int *p, int val) {
	futex(p, FUTEX_WAIT, val, 0, 0, 0);
}
static inline void futex_wakeall(int *p) {
	futex(p, FUTEX_WAKE, 0, 0, 0, 0);
}
static inline void futex_wake1(int *p) {
	futex(p, FUTEX_WAKE, 1, 0, 0, 0);
}

#else
#ifdef RELAX
// note: #warning doesn't work in MSVC but we won't hit that case here
#warning No futex call for this OS. Falling back on pure spinlock. \
Performance will suffer during contention.
#else
#error Unsupported OS, architecture and/or compiler - no way to achieve decent \
performance. Need either CPU spinlock hints or futexes, ideally both.
#endif
#define NO_FUTEX
#endif

#ifndef RELAX
#define RELAX do; while (0) // avoid having to #ifdef RELAX everywhere now
#endif

void fastspin_raise(volatile int *p_, int val) {
	_Atomic int *p = (_Atomic int *)p_;
#ifdef NO_FUTEX
	atomic_store_explicit(p, val, memory_order_release);
#else
	// for the futex implementation, try to avoid the wake syscall if we know
	// nothing had to sleep
	if (atomic_exchange_explicit(p, val, memory_order_release)) {
		futex_wakeall((int *)p);
	}
#endif
}

int fastspin_wait(volatile int *p_) {
	_Atomic int *p = (_Atomic int *)p_;
	int x = atomic_load_explicit(p, memory_order_acquire);
#ifdef NO_FUTEX
	if (x) return x;
	// only need acquire ordering once, then can avoid cache coherence overhead.
	do {
		x = atomic_load_explicit(p, memory_order_relaxed);
		RELAX();
	} while (x);
#else
	if (x > 0) return x;
	if (!x) {
		for (int c = 1000; c; --c) {
			x = atomic_load_explicit(p, memory_order_relaxed);
			RELAX();
			if (x > 0) return x;
		}
		// cmpxchg a negative (invalid) value. this will fail in two cases:
		// 1. someone else already cmpxchg'd: the futex_wait() will work fine
		// 2. raise() was already called: the futex_wait() will return instantly
		atomic_compare_exchange_strong_explicit(p, &(int){0}, -1,
				memory_order_acq_rel, memory_order_relaxed);
		futex_wait((int *)p, -1);
	}
	return atomic_load_explicit(p, memory_order_relaxed);
#endif
}

void fastspin_lock(volatile int *p_) {
	_Atomic int *p = (_Atomic int *)p_;
	int x;
	for (;;) {
#ifdef NO_FUTEX
		if (!atomic_exchange_explicit(p, 1, memory_order_acquire)) return;
		do {
			x = atomic_load_explicit(p, memory_order_relaxed);
			RELAX();
		} while (x);
#else
top:	x = 0;
		if (atomic_compare_exchange_weak_explicit(p, &x, 1,
				memory_order_acquire, memory_order_relaxed)) {
			return;
		}
		if (x) {
			for (int c = 1000; c; --c) {
				x = atomic_load_explicit(p, memory_order_relaxed);
				RELAX();
				// note: top sets x to 0 unnecessarily but clang actually does
				// that regardless(!), probably to break loop-carried dependency
				if (!x) goto top;
			}
			atomic_compare_exchange_strong_explicit(p, &(int){0}, -1,
					memory_order_acq_rel, memory_order_relaxed);
			futex_wait((int *)p, -1); // (then spin once more to avoid spuria)
		}
#endif
	}
}

void fastspin_unlock(volatile int *p_) {
	_Atomic int *p = (_Atomic int *)p_;
#ifdef NO_FUTEX
	atomic_store_explicit((_Atomic int *)p, 0, memory_order_release);
#else
	if (atomic_exchange_explicit(p, 0, memory_order_release) < 0) {
		futex_wake1((int *)p);
	}
#endif
}

// vi: sw=4 ts=4 noet tw=80 cc=80
