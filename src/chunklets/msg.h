/*
 * Copyright © 2025 Michael Smith <mikesmiffy128@gmail.com>
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

#ifndef INC_CHUNKLETS_MSG_H
#define INC_CHUNKLETS_MSG_H

#ifdef __cplusplus
#define _msg_Bool bool
extern "C" {
#else
#define _msg_Bool _Bool
#endif

/*
 * Writes a nil (null) message to the buffer `out`.
 *
 * `out` must point to at least 1 byte of space.
 *
 * Always writes a single byte.
 */
static inline void msg_putnil(unsigned char *out) {
	*out = 0xC0;
}

/*
 * Writes a nil (null) message immediately before the pointer `end`.
 *
 * `end` must point immediately beyond at least 1 byte of space.
 *
 * Always writes a single byte.
 *
 */
static inline void msg_rputnil(unsigned char *end) {
	end[-1] = 0xC0;
}

/*
 * Writes the boolean `val` to the buffer `out`.
 *
 * `out` must point to at least 1 byte of space.
 *
 * Always writes a single byte.
 */
static inline void msg_putbool(unsigned char *out, _msg_Bool val) {
	*out = 0xC2 | val;
}

/*
 * Writes the boolean `val` immediately before the pointer `end`.
 *
 * `end` must point immediately beyond at least 1 byte of space.
 *
 * Always writes a single byte.
 */
static inline void msg_rputbool(unsigned char *end, _msg_Bool val) {
	end[-1] = 0xC2 | val;
}

/*
 * Writes the integer `val` in the range [-32, 127] to the buffer `out`. Values
 * outside this range will produce an undefined encoding.
 *
 * `out` must point to at least 1 byte of space.
 *
 * Always writes a single byte.
 *
 * It is recommended to use msg_puts() for arbitrary signed values or msg_putu()
 * for arbitrary unsigned values. Those functions will produce the smallest
 * possible encoding for any value.
 */
static inline void msg_puti7(unsigned char *out, signed char val) {
	*out = val; // a fixnum is just the literal byte! genius!
}

/*
 * Writes the integer `val` in the range [-32, 127] immediately before the
 * pointer `end`. Values outside this range will produce an undefined encoding.
 *
 * `end` must point immediately beyond at least 1 byte of space.
 *
 * Always writes a single byte.
 *
 * It is recommended to use msg_rputs() for arbitrary signed values or
 * msg_rputu() for arbitrary unsigned values. Those functions will produce the
 * smallest possible encoding for any value.
 */
static inline void msg_rputi7(unsigned char *end, signed char val) {
	end[-1] = val;
}

/*
 * Writes the signed int `val` in the range [-128, 127] to the buffer `out`.
 *
 * `out` must point to at least 2 bytes of space.
 *
 * Returns the number of bytes written, one of {1, 2}.
 *
 * It is recommended to use msg_puts() for arbitrary signed values. That
 * function will produce the smallest possible encoding for any value.
 */
int msg_puts8(unsigned char *out, signed char val);

/*
 * Writes the signed int `val` in the range [-128, 127] immediately before the
 * pointer `end`.
 *
 * `end` must point immediately beyond at least 2 bytes of space.
 *
 * Returns the number of bytes written, one of {1, 2}.
 *
 * It is recommended to use msg_rputs() for arbitrary signed values. That
 * function will produce the smallest possible encoding for any value.
 */
int msg_rputs8(unsigned char *end, signed char val);

/*
 * Writes the unsigned int `val` in the range [0, 255] to the buffer `out`.
 *
 * `out` must point to at least 2 bytes of space.
 *
 * Returns the number of bytes written, one of {1, 2}.
 *
 * It is recommended to use msg_putu() for arbitrary unsigned values. That
 * function will produce the smallest possible encoding for any value.
 */
int msg_putu8(unsigned char *out, unsigned char val);

/*
 * Writes the unsigned int `val` in the range [0, 255] immediately before the
 * pointer `end`.
 *
 * `end` must point immediately beyond at least 2 bytes of space.
 *
 * Returns the number of bytes written, one of {1, 2}.
 *
 * It is recommended to use msg_rputu() for arbitrary unsigned values. That
 * function will produce the smallest possible encoding for any value.
 */
int msg_rputu8(unsigned char *end, unsigned char val);

/*
 * Writes the signed int `val` in the range [-32768, 32767] to the buffer `out`.
 *
 * `out` must point to at least 3 bytes of space.
 *
 * Returns the number of bytes written, one of {1, 2, 3}.
 *
 * It is recommended to use msg_puts() for arbitrary signed values. That
 * function will produce the smallest possible encoding for any value.
 */
int msg_puts16(unsigned char *out, short val);

/*
 * Writes the signed int `val` in the range [-32768, 32767] immediately before
 * the pointer `end`.
 *
 * `end` must point immediately beyond at least 3 bytes of space.
 *
 * Returns the number of bytes written, one of {1, 2, 3}.
 *
 * It is recommended to use msg_rputs() for arbitrary signed values. That
 * function will produce the smallest possible encoding for any value.
 */
int mmsg_rputs16(unsigned char *end, short val);

/*
 * Writes the unsigned int `val` in the range [0, 65536] to the buffer `out`.
 *
 * `out` must point to at least 3 bytes of space.
 *
 * Returns the number of bytes written, one of {1, 2, 3}.
 *
 * It is recommended to use msg_putu() for arbitrary unsigned values. That
 * function will produce the smallest possible encoding for any value.
 */
int msg_putu16(unsigned char *out, unsigned short val);

/*
 * Writes the signed int `val` in the range [0, 65536] immediately before the
 * pointer `end`.
 *
 * `end` must point immediately beyond at least 3 bytes of space.
 *
 * Returns the number of bytes written, one of {1, 2}.
 *
 * It is recommended to use msg_rputu() for arbitrary unsigned values. That
 * function will produce the smallest possible encoding for any value.
 */
int msg_rputu16(unsigned char *end, unsigned short val);

/*
 * Writes the signed int `val` in the range [-2147483648, 2147483647] to the
 * buffer `out`.
 *
 * `out` must point to at least 5 bytes of space.
 *
 * Returns the number of bytes written, one of {1, 2, 3, 5}.
 *
 * It is recommended to use msg_puts() for arbitrary signed values. That
 * function will produce the smallest possible encoding for any value.
 */
int msg_puts32(unsigned char *out, int val);

/*
 * Writes the signed int `val` in the range [-2147483648, 2147483647]
 * immediately before the pointer `end`.
 *
 * `end` must point immediately beyond at least least 5 bytes of space.
 *
 * Returns the number of bytes written, one of {1, 2, 3, 5}.
 *
 * It is recommended to use msg_rputs() for arbitrary signed values. That
 * function will produce the smallest possible encoding for any value.
 */
int msg_rputs32(unsigned char *end, int val);

/*
 * Writes the unsigned int `val` in the range [0, 4294967295] to the buffer
 * `out`.
 *
 * `out` must point to at least 5 bytes of space.
 *
 * Returns the number of bytes written, one of {1, 2, 3, 5}.
 *
 * It is recommended to use msg_putu() for arbitrary unsigned values. That
 * function will produce the smallest possible encoding for any value.
 */
int msg_putu32(unsigned char *out, unsigned int val);

/*
 * Writes the unsigned int `val` in the range [0, 4294967295] immediately before
 * the pointer `end`.
 *
 * `end` must point immediately beyond at least 5 bytes of space.
 *
 * Returns the number of bytes written, one of {1, 2, 3, 5}.
 *
 * It is recommended to use msg_rputu() for arbitrary unsigned values. That
 * function will produce the smallest possible encoding for any value.
 */
int msg_rputu32(unsigned char *end, unsigned int val);

/*
 * Writes the signed int `val` in the range [-9223372036854775808,
 * 9223372036854775807] to the buffer `out`.
 *
 * `out` must point to at least 9 bytes of space.
 *
 * Returns the number of bytes written, one of {1, 2, 3, 5, 9}.
 */
int msg_puts(unsigned char *out, long long val);

/*
 * Writes the signed int `val` in the range [-9223372036854775808,
 * 9223372036854775807] immediately before the pointer `end`.
 *
 * `end` must point immediately beyond at least 9 bytes of space.
 *
 * Returns the number of bytes written, one of {1, 2, 3, 5, 9}.
 */
int msg_rputs(unsigned char *end, long long val);

/*
 * Writes the unsigned int `val` in the range [0, 18446744073709551616] to the
 * buffer `out`.
 *
 * `out` must point to at least 9 bytes of space.
 *
 * Returns the number of bytes written, one of {1, 2, 3, 5, 9}.
 */
int msg_putu(unsigned char *out, unsigned long long val);

/*
 * Writes the unsigned int `val` in the range [0, 18446744073709551616]
 * immediately before the pointer `end`.
 *
 * `end` must point immediately beyond at least 9 bytes of space.
 *
 * Returns the number of bytes written, one of {1, 2, 3, 5, 9}.
 */
int msg_rputu(unsigned char *end, unsigned long long val);

/*
 * Writes the IEEE 754 single-precision float `val` to the buffer `out`.
 *
 * `out` must point to at least 5 bytes of space.
 *
 * Always writes 5 bytes.
 */
void msg_putf(unsigned char *out, float val);

/*
 * Writes the IEEE 754 single-precision float `val` immediately before the pointer
 * `end`.
 *
 * `end` must point immediately beyond at least 5 bytes of space.
 *
 * Always writes 5 bytes.
 */
static inline void msg_rputf(unsigned char *end, float val) {
	msg_putf(end - 5, val);
}

/*
 * Writes the IEEE 754 double-precision float `val` to the buffer `out`, or writes
 * a single-precision float if the exact value is the same.
 *
 * `out` must point to at least 9 bytes of space.
 *
 * Returns the number of bytes written, one of {5, 9}.
 */
int msg_putd(unsigned char *out, double val);

/*
 * Writes the IEEE 754 double-precision float `val` immediately before the pointer
 * `end`, or writes a single-precision float if the exact value is the same.
 *
 * `end` must point immediately beyond at least 9 bytes of space.
 *
 * Returns the number of bytes written, one of {5, 9}.
 */
int msg_rputd(unsigned char *end, double val);

/*
 * Writes the string size `sz` in the range [0, 31] to the buffer `out`. Values
 * outside this range will produce an undefined encoding.
 *
 * `out` must point to at least 1 byte of space.
 *
 * Always writes a single byte.
 *
 * In a complete message stream, a size of N must be immediately followed by N
 * bytes of the actual string, which must be valid UTF-8.
 *
 * It is recommended to use msg_putssz() for arbitrary string sizes. That
 * function will produce the smallest possible encoding for any size value.
 */
static inline void msg_putssz5(unsigned char *out, int sz) {
	*out = 0xA0 | sz;
}

/*
 * Writes the string size `sz` in the range [0, 31] immediately before the pointer
 * `end`. Values outside this range will produce an undefined encoding.
 *
 * `end` must point immediately beyond at least 1 byte of space.
 *
 * Always writes a single byte.
 *
 * In a complete message stream, a size of N must be immediately followed by N
 * bytes of the actual string, which must be valid UTF-8.
 *
 * It is recommended to use msg_rputssz() for arbitrary string sizes. That
 * function will produce the smallest possible encoding for any size value.
 */
static inline void msg_rputssz5(unsigned char *end, int sz) {
	msg_putssz5(end - 1, sz);
}

/*
 * Writes the string size `sz` in the range [0, 255] to the buffer `out`. Values
 * outside this range will produce an undefined encoding.
 *
 * `out` must point to at least 2 bytes of space.
 *
 * Returns the number of bytes written, one of {1, 2}.
 *
 * In a complete message stream, a size of N must be immediately followed by N
 * bytes of the actual string, which must be valid UTF-8.
 *
 * It is recommended to use msg_putssz() for arbitrary string sizes. That
 * function will produce the smallest possible encoding for any size value.
 */
int msg_putssz8(unsigned char *out, int sz);

/*
 * Writes the string size `sz` in the range [0, 255] immediately before the
 * pointer `end`. Values outside this range will produce an undefined encoding.
 *
 * `end` must point immediately beyond at least 2 byte of space.
 *
 * Returns the number of bytes written, one of {1, 2}.
 *
 * In a complete message stream, a size of N must be immediately followed by N
 * bytes of the actual string, which must be valid UTF-8.
 *
 * It is recommended to use msg_rputssz() for arbitrary string sizes. That
 * function will produce the smallest possible encoding for any size value.
 */
int msg_rputssz8(unsigned char *end, int sz);

/*
 * Writes the string size `sz` in the range [0, 65535] to the buffer `out`. Values
 * outside this range will produce an undefined encoding.
 *
 * `out` must point to at least 3 bytes of space.
 *
 * Returns the number of bytes written, one of {1, 2, 3}.
 *
 * In a complete message stream, a size of N must be immediately followed by N
 * bytes of the actual string, which must be valid UTF-8.
 *
 * It is recommended to use msg_putssz() for arbitrary string sizes. That
 * function will produce the smallest possible encoding for any size value.
 */
int msg_putssz16(unsigned char *out, int sz);

/*
 * Writes the string size `sz` in the range [0, 65535] immediately before the
 * pointer `end`. Values outside this range will produce an undefined encoding.
 *
 * `end` must point immediately beyond at least 3 bytes of space.
 *
 * Returns the number of bytes written, one of {1, 2, 3}.
 *
 * In a complete message stream, a size of N must be immediately followed by N
 * bytes of the actual string, which must be valid UTF-8.
 *
 * It is recommended to use msg_rputssz() for arbitrary string sizes. That
 * function will produce the smallest possible encoding for any size value.
 */
int msg_rputssz16(unsigned char *end, int sz);

/*
 * Writes the string size `sz` in the range [0, 4294967295] to the buffer `out`.
 *
 * `out` must point to at least 5 bytes of space.
 *
 * Returns the number of bytes written, one of {1, 2, 3, 5}.
 *
 * In a complete message stream, a size of N must be immediately followed by N
 * bytes of the actual string, which must be valid UTF-8.
 */
int msg_putssz(unsigned char *out, unsigned int sz);

/*
 * Writes the string size `sz` in the range [0, 4294967295] immediately before
 * the pointer `end`.
 *
 * `end` must point immediately beyond at least 5 bytes of space.
 *
 * Returns the number of bytes written, one of {1, 2, 3, 5}.
 *
 * In a complete message stream, a size of N must be immediately followed by N
 * bytes of the actual string, which must be valid UTF-8.
 */
int msg_rputssz(unsigned char *end, unsigned int sz);

/*
 * Writes the binary blob size `sz` in the range [0, 255] to the buffer `out`.
 * Values outside this range will produce an undefined encoding.
 *
 * `out` must point to at least 2 bytes of space.
 *
 * Always writes 2 bytes.
 *
 * In a complete message stream, a size of N must be immediately followed by
 * N bytes of the actual data.
 *
 * It is recommended to use msg_putbsz() for arbitrary binary blob sizes. That
 * function will produce the smallest possible encoding for any size value.
 */
static inline void msg_putbsz8(unsigned char *out, int sz) {
	out[0] = 0xC4; out[1] = sz;
}

/*
 * Writes the binary blob size `sz` in the range [0, 255] immediately before the
 * pointer `end`. Values outside this range will produce an undefined encoding.
 *
 * `end` must point immediately beyond at least 2 bytes of space.
 *
 * Always writes 2 bytes.
 *
 * In a complete message stream, a size of N must be immediately followed by
 * N bytes of the actual data.
 *
 * It is recommended to use msg_rputbsz() for arbitrary binary blob sizes. That
 * function will produce the smallest possible encoding for any size value.
 */
static inline void msg_rputbsz8(unsigned char *end, int sz) {
	end[-2] = 0xC4; end[-1] = sz;
}

/*
 * Writes the binary blob size `sz` in the range [0, 65535] to the buffer `out`.
 * Values outside this range will produce an undefined encoding.
 *
 * `out` must point to at least 3 bytes of space.
 *
 * Returns the number of bytes written, one of {2, 3}.
 *
 * In a complete message stream, a size of N must be immediately followed by
 * N bytes of the actual data.
 *
 * It is recommended to use msg_putbsz() for arbitrary binary blob sizes. That
 * function will produce the smallest possible encoding for any size value.
 */
int msg_putbsz16(unsigned char *out, int sz);

/*
 * Writes the binary blob size `sz` in the range [0, 65535] immediately before
 * the pointer `end`. Values outside this range will produce an undefined
 * encoding.
 *
 * `end` must point immediately beyond at least 3 bytes of space.
 *
 * Returns the number of bytes written, one of {2, 3}.
 *
 * In a complete message stream, a size of N must be immediately followed by
 * N bytes of the actual data.
 *
 * It is recommended to use msg_rputbsz() for arbitrary binary blob sizes. That
 * function will produce the smallest possible encoding for any size value.
 */
int msg_rputbsz16(unsigned char *end, int sz);

/*
 * Writes the binary blob size `sz` in the range [0, 4294967295] to the buffer
 * `out`.
 *
 * `out` must point to at least 5 bytes of space.
 *
 * Returns the number of bytes written, one of {2, 3, 5}.
 *
 * In a complete message stream, a size of N must be immediately followed by
 * N bytes of the actual data.
 */
int msg_putbsz(unsigned char *out, unsigned int sz);

/*
 * Writes the binary blob size `sz` in the range [0, 4294967295] immediately
 * before the pointer `end`.
 *
 * `end` must point immediately beyond at least 5 bytes of space.
 *
 * Returns the number of bytes written, one of {2, 3, 5}.
 *
 * In a complete message stream, a size of N must be immediately followed by
 * N bytes of the actual data.
 */
int msg_rputbsz(unsigned char *end, unsigned int sz);

/*
 * Writes the array size `sz` in the range [0, 15] to the buffer `out`. Values
 * outside this range will produce an undefined encoding.
 *
 * `out` must point to at least 1 byte of space.
 *
 * Always writes a single byte.
 *
 * In a complete message stream, a size of N must be immediately followed by N
 * other messages, which form the contents of the array.
 *
 * It is recommended to use msg_putasz() for arbitrary array sizes. That
 * function will produce the smallest possible encoding for any size value.
 */
static inline void msg_putasz4(unsigned char *out, int sz) {
	*out = 0x90 | sz;
}

/*
 * Writes the array size `sz` in the range [0, 15] immediately before the
 * pointer `end`. Values outside this range will produce an undefined encoding.
 *
 * `end` must point immediately beyond at least 1 byte of space.
 *
 * Always writes a single byte.
 *
 * In a complete message stream, a size of N must be immediately followed by N
 * other messages, which form the contents of the array.
 *
 * It is recommended to use msg_rputasz() for arbitrary array sizes. That
 * function will produce the smallest possible encoding for any size value.
 */
static inline void msg_rputasz4(unsigned char *end, int sz) {
	end[-1] = 0x90 | sz;
}

/*
 * Writes the array size `sz` in the range [0, 65535] to the buffer `out`.
 * Values outside this range will produce an undefined encoding.
 *
 * `out` must point to at least 3 bytes of space.
 *
 * Returns the number of bytes written, one of {1, 3}.
 *
 * In a complete message stream, a size of N must be immediately followed by N
 * other messages, which form the contents of the array.
 *
 * It is recommended to use msg_putasz() for arbitrary array sizes. That
 * function will produce the smallest possible encoding for any size value.
 */
int msg_putasz16(unsigned char *out, int sz);

/*
 * Writes the array size `sz` in the range [0, 65535] immediately before the
 * pointer `end`.
 *
 * `end` must point immediately beyond at least 3 bytes of space.
 *
 * Returns the number of bytes written, one of {1, 3}.
 *
 * In a complete message stream, a size of N must be immediately followed by N
 * other messages, which form the contents of the array.
 *
 * It is recommended to use msg_rputasz() for arbitrary array sizes. That
 * function will produce the smallest possible encoding for any size value.
 */
int msg_rputasz16(unsigned char *end, int sz);

/*
 * Writes the array size `sz` in the range [0, 4294967295] to the buffer `out`.
 *
 * `out` must point to at least 5 bytes of space.
 *
 * Returns the number of bytes written, one of {1, 3, 5}.
 *
 * In a complete message stream, a size of N must be immediately followed by N
 * other messages, which form the contents of the array.
 */
int msg_putasz(unsigned char *out, unsigned int sz);

/*
 * Writes the array size `sz` in the range [0, 4294967295] immediately before
 * the pointer `end`.
 *
 * `end` must point immediately beyond at least 5 bytes of space.
 *
 * Returns the number of bytes written, one of {1, 3, 5}.
 *
 * In a complete message stream, a size of N must be immediately followed by N
 * other messages, which form the contents of the array.
 */
int msg_rputasz(unsigned char *end, unsigned int sz);

/*
 * Writes the map size `sz` in the range [0, 15] to the buffer `out`. Values
 * outside this range will produce an undefined encoding.
 * `out` must point to at least 1 byte of space.
 *
 * Always writes a single byte.
 *
 * In a complete message stream, a size of N must be immediately followed by
 * N other pairs of messages, each containing a key followed by a value, making
 * up the contents of the map.
 *
 * It is recommended to use msg_putmsz() for arbitrary map sizes. That function
 * will produce the smallest possible encoding for any size value.
 */
static inline void msg_putmsz4(unsigned char *out, int sz) {
	*out = 0x80 | sz;
}

/*
 * Writes the map size `sz` in the range [0, 15] immediately before the pointer
 * `end`. Values outside this range will produce an undefined encoding.
 *
 * `end` must point immediately at least 1 byte of space.
 *
 * Always writes a single byte.
 *
 * In a complete message stream, a size of N must be immediately followed by
 * N other pairs of messages, each containing a key followed by a value, making
 * up the contents of the map.
 *
 * It is recommended to use msg_rputmsz() for arbitrary map sizes. That function
 * will produce the smallest possible encoding for any size value.
 */
static inline void msg_rputmsz4(unsigned char *end, int sz) {
	end[-1] = 0x80 | sz;
}

/*
 * Writes the map size `sz` in the range [0, 65536] to the buffer `out`.
 *
 * `out` must point to at least 3 bytes of space.
 *
 * Returns the number of bytes written, one of {1, 3}.
 *
 * In a complete message stream, a size of N must be immediately followed by
 * N * 2 other messages, which form the contents of the map as alternating
 * pairs, each containing one key followed by one value.
 *
 * It is recommended to use msg_putmsz() for arbitrary map sizes. That function
 * will produce the smallest possible encoding for any size value.
 */
int msg_putmsz16(unsigned char *out, int sz);

/*
 * Writes the map size `sz` in the range [0, 65536] immediately before the
 * pointer `end`. Values outside this range will produce an undefined encoding.
 *
 * `end` must point immediately beyond least 3 bytes of space.
 *
 * Returns the number of bytes written, one of {1, 3}.
 *
 * In a complete message stream, a size of N must be immediately followed by
 * N other pairs of messages, each containing a key followed by a value, making
 * up the contents of the map.
 *
 * It is recommended to use msg_rputmsz() for arbitrary map sizes. That function
 * will produce the smallest possible encoding for any size value.
 */
int msg_rputmsz16(unsigned char *end, int sz);

/*
 * Writes the map size `sz` in the range [0, 4294967295] to the buffer `out`.
 *
 * `out` must point to at least 5 bytes of space.
 *
 * Returns the number of bytes written, one of {1, 3, 5}.
 *
 * In a complete message stream, a size of N must be immediately followed by
 * N other pairs of messages, each containing a key followed by a value, making
 * up the contents of the map.
 */
int msg_putmsz(unsigned char *out, unsigned int sz);

/*
 * Writes the map size `sz` in the range [0, 4294967295] immediately before the 
 * pointer `end`.
 *
 * `end` must point immediately beyond at least 5 bytes of space.
 *
 * Returns the number of bytes written, one of {1, 3, 5}.
 *
 * In a complete message stream, a size of N must be immediately followed by
 * N other pairs of messages, each containing a key followed by a value, making
 * up the contents of the map.
 */
int msg_putmszr(unsigned char *end, unsigned int sz);

#ifdef __cplusplus
}
#endif
#undef _msg_Bool

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
