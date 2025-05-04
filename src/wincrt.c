/* This file is dedicated to the public domain. */

// We get most of the libc functions from ucrtbase.dll, which comes with
// Windows, but for some reason a few of the intrinsic-y things are part of
// vcruntime, which does *not* come with Windows!!! We can statically link just
// that part but it adds ~12KiB of random useless bloat to our binary. So, let's
// just implement the handful of required things here instead. This is only for
// release/non-debug builds; we want the extra checks in Microsoft's CRT when
// debugging.
//
// Is it actually reasonable to have to do any of this? Of course not.

int memcmp(const void *restrict x, const void *restrict y, unsigned int sz) {
#if defined(__GNUC__) || defined(__clang__)
	int a, b;
	__asm__ volatile (
		"xor %%eax, %%eax\n"
		"repz cmpsb\n"
		: "+D" (x), "+S" (y), "+c" (sz), "=@cca"(a), "=@ccb"(b)
		:
		: "ax", "memory"
	);
	return b - a;
#else
	const char *x = x_, *y = y_;
	for (unsigned int i = 0; i < sz; ++i) {
		if (x[i] > y[i]) return 1;
		if (x[i] < y[i]) return -1;
	}
	return 0;
#endif
}

void *memcpy(void *restrict x, const void *restrict y, unsigned int sz) {
#if defined(__GNUC__) || defined(__clang__)
	void *r = x;
	__asm__ volatile (
		"rep movsb\n"
		: "+D" (x), "+S" (y), "+c" (sz)
		:
		: "memory"
	);
	return r;
#else
	char *restrict xb = x; const char *restrict yb = y;
	for (unsigned int i = 0; i < sz; ++i) xb[i] = yb[i];
	return x;
#endif
}

int __stdcall _DllMainCRTStartup(void *inst, unsigned int reason,
		void *reserved) {
	return 1;
}

#ifdef __clang__
__attribute__((used))
#endif
int _fltused = 1;

// vi: sw=4 ts=4 noet tw=80 cc=80
