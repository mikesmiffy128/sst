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

#include "bitbuf.h"
#include "con_.h"
#include "democustom.h"
#include "demorec.h"
#include "engineapi.h"
#include "errmsg.h"
#include "feature.h"
#include "gamedata.h"
#include "intdefs.h"
#include "mem.h"
#include "ppmagic.h"
#include "vcall.h"

FEATURE()
REQUIRE(demorec)
REQUIRE_GAMEDATA(vtidx_GetEngineBuildNumber)
REQUIRE_GAMEDATA(vtidx_RecordPacket)

static int nbits_msgtype, nbits_datalen;

// The engine allows usermessages up to 255 bytes, we add 2 bytes of overhead,
// and then there's the leading bits before that too (see create_message)
static char bb_buf[DEMOCUSTOM_MSG_MAX + 4];
static struct bitbuf bb = {
	bb_buf, sizeof(bb_buf), sizeof(bb_buf) * 8, 0, false, false, "SST"
};

static void create_message(struct bitbuf *msg, const void *buf, int len) {
	// The way we pack our custom demo data is via a user message packet with
	// type "HudText" - this causes the client to do a text lookup which will
	// simply silently fail on invalid keys. By making the first byte null
	// (creating an empty string), we get the rest of the packet to stick in
	// whatever other data we want.
	//
	// Notes from Uncrafted:
	// > But yeah the data you want to append is as follows:
	// > - 6 bits (5 bits in older versions) for the message type - should be 23
	// >   for user message
	bitbuf_appendbits(msg, 23, nbits_msgtype);
	// > - 1 byte for the user message type - should be 2 for HudText
	bitbuf_appendbyte(msg, 2);
	// > - ~~an int~~ 11 or 12 bits for the length of your data in bits,
	bitbuf_appendbits(msg, len * 8, nbits_datalen); // NOTE: assuming len <= 254
	// > - your data
	// [first the aforementioned null byte, plus an arbitrary marker byte to
	// avoid confusion when parsing the demo later...
	bitbuf_appendbyte(msg, 0);
	bitbuf_appendbyte(msg, 0xAC);
	// ... and then just the data itself]
	bitbuf_appendbuf(msg, buf, len);
	// Thanks Uncrafted, very cool!
}

typedef void (*VCALLCONV WriteMessages_func)(void *this, struct bitbuf *msg);
static WriteMessages_func WriteMessages = 0;

void democustom_write(const void *buf, int len) {
	create_message(&bb, buf, len);
	WriteMessages(demorecorder, &bb);
	bitbuf_reset(&bb);
}

static bool find_WriteMessages(void) {
	// TODO(compat): rewrite this to just scan for a call instruction!
	const uchar *insns = (*(uchar ***)demorecorder)[vtidx_RecordPacket];
	// RecordPacket calls WriteMessages pretty much right away:
	// 56           push  esi
	// 57           push  edi
	// 8B F1        mov   esi,ecx
	// 8D BE        lea   edi,[esi + 0x68c]
	// 8C 06 00 00
	// 57           push  edi
	// E8           call  CDemoRecorder_WriteMessages
	// B0 EF FF FF
	// So we just double check the byte pattern...
	static const uchar bytes[] =
#ifdef _WIN32
		HEXBYTES(56, 57, 8B, F1, 8D, BE, 8C, 06, 00, 00, 57, E8);
#else
#warning This is possibly different on Linux too, have a look!
		{-1, -1, -1, -1, -1, -1};
#endif
	if (!memcmp(insns, bytes, sizeof(bytes))) {
		ssize off = mem_loadoffset(insns + sizeof(bytes));
		WriteMessages = (WriteMessages_func)(insns + sizeof(bytes) + 4 + off);
		return true;
	}
	return false;
}

DECL_VFUNC_DYN(int, GetEngineBuildNumber)

INIT {
	// More UncraftedkNowledge:
	// > yeah okay so [the usermessage length is] 11 bits if the demo protocol
	// > is 11 or if the game is l4d2 and the network protocol is 2042.
	// > otherwise it's 12 bits
	// > there might be some other l4d2 versions where it's 11 but idk
	// So here we have to figure out the network protocol version!
	// NOTE: assuming engclient != null as GEBN index relies on client version
	int buildnum = GetEngineBuildNumber(engclient);
	// condition is redundant until other GetEngineBuildNumber offsets are added
	// if (GAMETYPE_MATCHES(L4D2)) {
		nbits_msgtype = 6;
		// based on Some Code I Read, buildnum *should* be the protocol version,
		// however L4D2 returns the actual game version instead, because sure
		// why not. The only practical difference though is that the network
		// protocol froze after 2042, so we just have to do a >=. No big deal
		// really.
		if (buildnum >= 2042) nbits_datalen = 11; else nbits_datalen = 12;
	// }

	return find_WriteMessages();
}

// vi: sw=4 ts=4 noet tw=80 cc=80
