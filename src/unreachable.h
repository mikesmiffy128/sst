/* This file is dedicated to the public domain. */

#ifndef INC_UNREACHABLE_H
#define INC_UNREACHABLE_H

#if defined(__GNUC__) || defined(__clang__)
#define unreachable __builtin_unreachable()
#else
#define unreachable do; while (0)
#endif

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
