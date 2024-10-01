/* This file is dedicated to the public domain. */

// We have to define a real function with the right number of arguments to get
// the mangled symbol _ProcessPrng@8, which the .def file apparently turns
// into an "undecorate" .lib entry through some undocumented magic (of course!).
int __stdcall ProcessPrng(void *x, unsigned int y) { return 0; }
