/* This file is dedicated to the public domain. */

// This is a special header force-included in every source file.
// Currently it's just used to ban certain functions from the codebase.
// Ideally this could be done using the poison thing but that triggers a bunch
// of anger when including Windows.h. Of course. So we just use #defines.

#ifndef SST_DBG // we wrap these in debug mode, so leave them unbanned there.
// These should not be used explicitly. Use mem_copy, mem_set and mem_cmp from
// mem.h instead. Those are optimised for both code size and speed on x86.
#define memcpy please_use_mem_copy_from_mem_dot_h_instead_of_memcpy
#define memset please_use_mem_set_from_mem_dot_h_instead_of_memset
#define memcmp please_use_mem_cmp_from_mem_dot_h_instead_of_memcmp
#endif

// TODO(opt): implement this if and when we ever need it (unlikely really)
#define memmove sorry_memmove_needs_an_equivalent_in_mem_dot_h_not_done_yet

// These functions are impossible to use correctly and probably have better
// alternatives already within the codebase. Or, if not, then better
// alternatives can be written easily.
#define strcpy strcpy_is_broken_and_should_never_be_used
#define strcat strcat_is_broken_and_should_never_be_used
#define strncpy strncpy_is_broken_and_should_never_be_used
#define strncat strncat_is_broken_and_should_never_be_used
#define strtok strtok_is_broken_and_slow_and_should_never_be_used
#define strtok_r strtok_r_is_broken_and_slow_and_should_never_be_used
// Windows headers throw a fit when redefining these, lol. The following two
// effectively prevent their use anyway since they're inline functions in
// stdio.h. Dunno about on Linux, but we compile first on Windows anyway innit.
//#define sprintf sprintf_is_dangerous_and_slow_and_should_never_be_used
//#define snprintf snprintf_is_dangerous_and_slow_and_should_never_be_used
#define vsprintf vsprintf_is_dangerous_and_slow_and_should_never_be_used
// XXX: have some straggling vsnprintf usage I'd like to get rid of...
//#define vsnprintf vsnprintf_is_dangerous_and_slow_and_should_never_be_used

// vi: sw=4 ts=4 noet tw=80 cc=80
