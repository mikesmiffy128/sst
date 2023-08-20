/* This file is dedicated to the public domain. */

#include "stub.h"

F(Msg)
F(Warning)
#ifdef _WIN32
V(g_pMemAlloc) // this doesn't exist at all on Linux
#else
F(Error) // only used for extmalloc() and nothing else :^)
#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
