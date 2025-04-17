/*
 * Copyright © Michael Smith <mikesmiffy128@gmail.com>
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

#include <string.h>

#include "bitbuf.h"
#include "demorec.h"
#include "engineapi.h"
#include "feature.h"
#include "gamedata.h"
#include "intdefs.h"
#include "langext.h"
#include "mem.h"
#include "vcall.h"
#include "x86.h"
#include "x86util.h"

FEATURE()
REQUIRE(demorec)
REQUIRE_GAMEDATA(vtidx_GetEngineBuildNumber)
REQUIRE_GAMEDATA(vtidx_RecordPacket)

static int nbits_msgtype, nbits_datalen;

// engine limit is 255, we use 2 bytes for header + round the bitstream to the
// next whole byte, which gives 3 bytes overhead hence 252 here.
#define CHUNKSZ 252

static union {
	char x[CHUNKSZ + /*7*/ 8]; // needs to be multiple of of 4!
	bitbuf_cell _align; // just in case...
} bb_buf;
static struct bitbuf bb = {
	{bb_buf.x}, ssizeof(bb_buf), ssizeof(bb_buf) * 8, 0, false, false, "SST"
};

static const void *createhdr(struct bitbuf *msg, int len, bool last) {
	// We pack custom data into user message packets of type "HudText," with a
	// leading null byte which the engine treats as an empty string. On demo
	// playback, the client does a text lookup which fails silently on invalid
	// keys, giving us the rest of the packet to stick in whatever data we want.
	//
	// Big thanks to our resident demo expert, Uncrafted, for explaining what to
	// do here way back when this was first being figured out!
	bitbuf_appendbits(msg, 23, nbits_msgtype); // type: 23 is user message
	bitbuf_appendbyte(msg, 2); // user message type: 2 is HudText
	bitbuf_appendbits(msg, len * 8, nbits_datalen); // our data length in bits
	bitbuf_appendbyte(msg, 0); // aforementionied null byte
	bitbuf_appendbyte(msg, 0xAC + last); // arbitrary marker byte to aid parsing
	// store the data itself byte-aligned so there's no need to bitshift the
	// universe (which would be both slower and more annoying to do)
	bitbuf_roundup(msg);
	return msg->buf + (msg->nbits >> 3);
}

typedef void (*VCALLCONV WriteMessages_func)(void *this, struct bitbuf *msg);
static WriteMessages_func WriteMessages = 0;

void democustom_write(const void *buf, int len) {
	for (; len > CHUNKSZ; len -= CHUNKSZ) {
		createhdr(&bb, CHUNKSZ, false);
		memcpy(bb.buf + (bb.nbits >> 3), buf, CHUNKSZ);
		bb.nbits += CHUNKSZ << 3;
		WriteMessages(demorecorder, &bb);
		bitbuf_reset(&bb);
	}
	createhdr(&bb, len, true);
	memcpy(bb.buf + (bb.nbits >> 3), buf, len);
	bb.nbits += len << 3;
	WriteMessages(demorecorder, &bb);
	bitbuf_reset(&bb);
}

static bool find_WriteMessages() {
	const uchar *insns = (uchar *)demorecorder->vtable[vtidx_RecordPacket];
	// RecordPacket calls WriteMessages right away, so just look for a call
	for (const uchar *p = insns; p - insns < 32;) {
		if (*p == X86_CALL) {
			WriteMessages = (WriteMessages_func)(p + 5 + mem_loads32(p + 1));
			return true;
		}
		NEXT_INSN(p, "WriteMessages function");
	}
	return false;
}

DECL_VFUNC_DYN(struct VEngineClient, int, GetEngineBuildNumber)

INIT {
	// More UncraftedkNowledge:
	// - usermessage length is:
	//   - 11 bits in protocol 11, or l4d2 protocol 2042
	//   - otherwise 12 bits
	// So here we have to figure out the network protocol version!
	// NOTE: assuming engclient != null as GEBN index relies on client version
	int buildnum = GetEngineBuildNumber(engclient);
	//if (GAMETYPE_MATCHES(L4D2)) { // redundant until we add more GEBN offsets!
		nbits_msgtype = 6;
		// based on Some Code I Read, buildnum *should* be the protocol version,
		// however L4D2 returns the actual game version instead, because sure
		// why not. The only practical difference though is that the network
		// protocol froze after 2042, so we just have to do a >=. Fair enough!
		// TODO(compat): how does TLS affect this? no idea yet
		if (buildnum >= 2042) nbits_datalen = 11; else nbits_datalen = 12;
	//}

	if (!find_WriteMessages()) return FEAT_INCOMPAT;
	return FEAT_OK;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
