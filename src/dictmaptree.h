/*
 * Copyright © 2023 Michael Smith <mikesmiffy128@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED “AS IS” AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef INC_DICTMAPTREE_H
#define INC_DICTMAPTREE_H

#include "engineapi.h"
#include "intdefs.h"

/*
 * Valve's dict/map/tree structures come in various shapes and sizes, so here we
 * do the generic macro thing for future-proofing. For now we just define a
 * CUtlDict (map with string keys) of pointers, with ushort indices, which is
 * sufficient for server entity factory lookup, and probably some other stuff.
 * Functions for actually modifying the dicts/maps/trees aren't implemented.
 */

#define DECL_CUTLRBTREE_NODE(name, ktype, vtype, idxtype) \
typedef typeof(ktype) _cutlrbtree_##name##_key; \
typedef typeof(vtype) _cutlrbtree_##name##_val; \
typedef typeof(idxtype) _cutlrbtree_##name##_idx; \
struct name { \
	struct { \
		_cutlrbtree_##name##_idx l, r, p, tags; \
	} links; \
	_cutlrbtree_##name##_key k; \
	_cutlrbtree_##name##_val v; \
};

#define DEF_CUTLRBTREE(name, nodetype) \
struct name { \
	bool (*cmp)(const _cutlrbtree_##nodetype##_key *, \
			const _cutlrbtree_##nodetype##_key *); \
	struct CUtlMemory elems; \
	_cutlrbtree_##nodetype##_idx root, count, firstfree, lastalloc; \
	struct nodetype *nodes; \
}; \
\
static _cutlrbtree_##nodetype##_idx name##_find(const struct name *rb, \
		const _cutlrbtree_##nodetype##_key k) { \
	_cutlrbtree_##nodetype##_idx idx = rb->root; \
	while (idx != (_cutlrbtree_##nodetype##_idx)-1) { \
		struct nodetype *nodes = rb->elems.mem; \
		if (rb->cmp(&k, &nodes[idx].k)) idx = nodes[idx].links.l; \
		else if (rb->cmp(&nodes[idx].k, &k)) idx = nodes[idx].links.r; \
		else break; \
	} \
	return idx; \
} \
\
static inline _cutlrbtree_##nodetype##_val name##_findval(const struct name *rb, \
		const _cutlrbtree_##nodetype##_key k) { \
	const _cutlrbtree_##nodetype##_idx idx = name##_find(rb, k); \
	if (idx == (_cutlrbtree_##nodetype##_idx)-1) { \
		return (_cutlrbtree_CUtlDict_node_p_ushort_val){0}; \
	} \
	struct nodetype *nodes = rb->elems.mem; \
	return nodes[idx].v; \
}

DECL_CUTLRBTREE_NODE(CUtlDict_node_p_ushort, const char *, void *, ushort)
DEF_CUTLRBTREE(CUtlDict_p_ushort, CUtlDict_node_p_ushort)

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
