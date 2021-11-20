/* This file is dedicated to the public domain. */

#ifndef INC_INTDEFS_H
#define INC_INTDEFS_H

typedef signed char schar;
typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned long ulong;
typedef long long vlong;
typedef unsigned long long uvlong;

typedef schar s8;
typedef uchar u8;
typedef short s16;
typedef ushort u16;
typedef int s32;
typedef uint u32;
typedef vlong s64;
typedef uvlong u64;

// just in case there's ever a need to support 64-bit builds of Source, define a
// size type, since Windows isn't LP64 so (u)long won't quite do
#ifdef _WIN64
typedef vlong ssize;
typedef uvlong usize;
#else
typedef long ssize;
typedef ulong usize;
#endif

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
