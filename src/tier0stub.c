/* This file is dedicated to the public domain. */

// Produce a dummy tier0.dll/libtier0.so to allow linking without dlsym faff.
// Windows needs additional care because it's dumb.

#ifdef _WIN32
#define F(name) __declspec(dllexport) void name(void) {}
#define V(name) __declspec(dllexport) void *name;
#else
#define F(name) void *name;
#define V(name) void *name;
#endif

F(Msg);
F(Warning);
// F(Error); // unused in plugin
V(g_pMemAlloc);

// vi: sw=4 ts=4 noet tw=80 cc=80
