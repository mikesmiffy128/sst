/* This file is dedicated to the public domain. */

// We get most of the libc functions from ucrtbase.dll, which comes with
// Windows, but for some reason a few of the intrinsic-y things are part of
// vcruntime, which does *not* come with Windows!!!
//
// At this point, we no longer use memcpy/memcmp/memset (preferring to use the
// x86 single-instruction equivalents and/or explicit builtins/intrinsics).
//
// So all we actually have to define here are a couple of dummy symbols to
// appease the linker.

int __stdcall _DllMainCRTStartup(void *inst, unsigned int reason,
		void *reserved) {
	return 1;
}

#ifdef __clang__
__attribute((used))
#endif
int _fltused = 1;

// vi: sw=4 ts=4 noet tw=80 cc=80
