// We produce dummy libraries for vstdlib and tier0 to allow linking without
// dlsym faff. These macros are because Windows needs additional care because
// it's dumb.

#ifdef _WIN32
#define F(name) __declspec(dllexport) void name(void) {}
#define V(name) __declspec(dllexport) void *name;
#else
#define F(name) void *name;
#define V(name) void *name;
#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
