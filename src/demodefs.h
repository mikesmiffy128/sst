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

#ifndef INC_DEMODEFS_H
#define INC_DEMODEFS_H

#include "intdefs.h"

/*
 * This file has demo format-related constants, mostly derived from Uncrafted's
 * C# demo parser.
 */

/* Windows' MAX_PATH is also used for player/map/etc. names in the demo... */
#define DEMO_HDR_STRLEN 260

struct demo_hdr {
	char sig[8]; /* HL2DEMO\0 */
	s32 demover;
	s32 netver;
	char servername[DEMO_HDR_STRLEN];
	char playername[DEMO_HDR_STRLEN];
	char mapname[DEMO_HDR_STRLEN];
	char gamedir[DEMO_HDR_STRLEN];
	float realtime;
	s32 nticks;
	s32 nframes;
	s32 signonlen;
};

enum demo_cmd {
	// all protocols:
	DEMO_CMD_SIGNON = 1,
	DEMO_CMD_PACKET,
	DEMO_CMD_SYNC,
	DEMO_CMD_CONCMD,
	DEMO_CMD_USERCMD,
	DEMO_CMD_DATATABLES,
	DEMO_CMD_STOP,
	DEMO_CMD_STRINGTABLES14 = 8, // protocols 14 and 15
	DEMO_CMD_CUSTOMDATA = 8, // protocol 36+
	DEMO_CMD_STRINGTABLES36  // "
};

/* these are seemingly consistent across games/branches */
#define DEMO_MAXEDICTBITS 11
#define DEMO_MAXEDICTS (1 << DEMO_MAXEDICTS)
#define DEMO_NETHANDLESERIALBITS 10
#define DEMO_NETHANDLEBITS (DEMO_MAXEDICTBITS + DEMO_NETHANDLEBITS)
#define DEMO_NULLHANDLE ((1u << DEMO_NETHANDLEBITS) - 1)
#define DEMO_SUBSTRINGBITS 5
#define DEMO_MAXUSERDATABITS 14
#define DEMO_HANDLESERIALBITS 10
// TODO: clarify what these ones do, and/or remove
#define DEMO_MAXNETMSG 6
#define DEMO_AREABITSNUMBITS 8
#define DEMO_MAXSNDIDXBITS 13
#define DEMO_SNDSEQBITS 10
#define DEMO_MAXSNDLVLBITS 9
#define DEMO_MAXSNDDELAYBITS 13
#define DEMO_SNDSEQMASK ((1 << DEMO_SNDSEQBITS) - 1)
// end of todo :^)
#define DEMO_PLAYERNAMELEN 32
#define DEMO_GUIDLEN 32

/* protocol versions (seem somewhat arbitrary but just copying Uncrafted) */
// (note: these aren't version numbers, they're just our own identifiers)
enum {
	DEMO_PROTO_HL2OE,
	DEMO_PROTO_PORTAL_5135,
	DEMO_PROTO_PORTAL_3420,
	DEMO_PROTO_PORTAL_STEAM,
	DEMO_PROTO_PORTAL2,
	DEMO_PROTO_L4D2000,
	DEMO_PROTO_L4D2042,
	DEMO_PROTO_UNKNOWN
};

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
