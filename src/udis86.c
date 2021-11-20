/* This file is dedicated to the public domain. */

#include "3p/udis86/udis86.c"
#include "3p/udis86/decode.c"
#include "3p/udis86/itab.c"
// this stuff is optional but llvm is smart enough to remove it if it's unused,
// so we keep it in here to be able to use it conveniently for debugging etc.
#include "3p/udis86/syn.c"
#include "3p/udis86/syn-intel.c"

// vi: sw=4 ts=4 noet tw=80 cc=80
