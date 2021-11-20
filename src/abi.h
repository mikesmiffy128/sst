/*
 * Copyright © 2021 Michael Smith <mikesmiffy128@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef INC_ABI_H
#define INC_ABI_H

#include "intdefs.h"

/*
 * This file defines miscellaneous C++ ABI stuff. Looking at it may cause
 * brain damage and/or emotional trauma.
 */

#ifdef _WIN32 // Windows RTTI stuff, obviously only used on Windows.

// MSVC RTTI is quite a black box, but thankfully there's some useful sources:
// - https://doxygen.reactos.org/d0/dcf/cxx_8h_source.html
// - https://blog.quarkslab.com/visual-c-rtti-inspection.html
// - https://www.geoffchappell.com/studies/msvc/language/predefined/
// - https://docs.rs/pelite/0.5.0/src/pelite/pe32/msvc.rs.html

// By the way, while I'm here I'd just like to point out how ridiculous this
// layout is. Whoever decided to put this many levels of indirection over what
// should be a small lookup table is an absolute nutcase. I hope that individual
// has gotten some help by now, mostly for the sake of others.

struct msvc_rtti_descriptor {
	void *vtab;
	void *unknown; // ???
	// XXX: pretty sure this is supposed to be flexible, but too lazy to write
	// the stupid union init macros to make that fully portable
	char classname[80];
};

// "pointer to member displacement"
struct msvc_pmd { int mdisp, pdisp, vdisp; };

struct msvc_basedesc {
	struct msvc_rtti_descriptor *desc;
	uint nbases;
	struct msvc_pmd where;
	uint attr;
};

struct msvc_rtti_hierarchy {
	uint sig;
	uint attrs;
	uint nbaseclasses;
	struct msvc_basedesc **baseclasses;
};

struct msvc_rtti_locator {
	uint sig;
	int baseoff;
	// ctor offset, or some flags; reactos and rust pelite say different things?
	int unknown;
	struct msvc_rtti_descriptor *desc;
	struct msvc_rtti_hierarchy *hier;
};

// I mean seriously look at this crap!
#define DEF_MSVC_BASIC_RTTI(mod, name, vtab, typestr) \
mod struct msvc_rtti_locator name; \
static struct msvc_rtti_descriptor _desc_##name = {(vtab) + 1, 0, typestr}; \
static struct msvc_basedesc _basedesc_##name = {&_desc_##name, 0, {0, 0, 0}, 0}; \
mod struct msvc_rtti_locator name = { \
	0, 0, 0, \
	&_desc_##name, \
	&(struct msvc_rtti_hierarchy){ \
		0, 1 /* per engine */, 1, (struct msvc_basedesc *[]){&_basedesc_##name} \
	} \
};

#endif

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
