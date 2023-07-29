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

#ifndef INC_CHUNKLETS_MSG_H
#define INC_CHUNKLETS_MSG_H

#ifdef __cplusplus
#define _msg_Bool bool
extern "C" {
#else
#define _msg_Bool _Bool
#endif

/*
 * Writes a nil (null) message to the buffer out. Always writes a single byte.
 *
 * out must point to at least 1 byte.
 */
void msg_putnil(unsigned char *out);

/*
 * Writes the boolean val to the buffer out. Always writes a single byte.
 *
 * out must point to at least 1 byte.
 */
void msg_putbool(unsigned char *out, _msg_Bool val);

/*
 * Writes the integer val in the range [-32, 127] to the buffer out. Values
 * outside this range will produce an undefined encoding. Always writes a single
 * byte.
 *
 * out must point to at least 1 byte.
 *
 * It is recommended to use msg_puts() for arbitrary signed values or msg_putu()
 * for arbitrary unsigned values. Those functions will produce the smallest
 * possible encoding for any value.
 */
void msg_puti7(unsigned char *out, signed char val);

/*
 * Writes the signed int val in the range [-128, 127] to the buffer out.
 *
 * out must point to at least 2 bytes.
 *
 * Returns the number of bytes written, one of {1, 2}.
 *
 * It is recommended to use msg_puts() for arbitrary signed values. That
 * function will produce the smallest possible encoding for any value.
 */
int msg_puts8(unsigned char *out, signed char val);

/*
 * Writes the unsigned int val in the range [0, 255] to the buffer out.
 *
 * out must point to at least 2 bytes.
 *
 * Returns the number of bytes written, one of {1, 2}.
 *
 * It is recommended to use msg_putu() for arbitrary unsigned values. That
 * function will produce the smallest possible encoding for any value.
 */
int msg_putu8(unsigned char *out, unsigned char val);

/*
 * Writes the signed int val in the range [-65536, 65535] to the buffer out.
 *
 * out must point to at least 3 bytes.
 *
 * Returns the number of bytes written, one of {1, 2, 3}.
 *
 * It is recommended to use msg_puts() for arbitrary signed values. That
 * function will produce the smallest possible encoding for any value.
 */
int msg_puts16(unsigned char *out, short val);

/*
 * Writes the unsigned int val in the range [0, 65536] to the buffer out.
 *
 * out must point to at least 3 bytes.
 *
 * Returns the number of bytes written, one of {1, 2, 3}.
 *
 * It is recommended to use msg_putu() for arbitrary unsigned values. That
 * function will produce the smallest possible encoding for any value.
 */
int msg_putu16(unsigned char *out, unsigned short val);

/*
 * Writes the signed int val in the range [-2147483648, 2147483647] to the
 * buffer out.
 *
 * out must point to at least 5 bytes.
 *
 * Returns the number of bytes written, one of {1, 2, 3, 5}.
 *
 * It is recommended to use msg_puts() for arbitrary signed values. That
 * function will produce the smallest possible encoding for any value.
 */
int msg_puts32(unsigned char *out, int val);

/*
 * Writes the unsigned int val in the range [0, 4294967295] to the buffer out.
 *
 * out must point to at least 5 bytes.
 *
 * Returns the number of bytes written, one of {1, 2, 3, 5}.
 *
 * It is recommended to use msg_putu() for arbitrary unsigned values. That
 * function will produce the smallest possible encoding for any value.
 */
int msg_putu32(unsigned char *out, unsigned int val);

/*
 * Writes the signed int val in the range [-9223372036854775808,
 * 9223372036854775807] to the buffer out.
 *
 * out must point to at least 9 bytes.
 *
 * Returns the number of bytes written, one of {1, 2, 3, 5, 9}.
 */
int msg_puts(unsigned char *out, long long val);

/*
 * Writes the unsigned int val in the range [0, 18446744073709551616] to the
 * buffer out.
 *
 * out must point to at least 9 bytes.
 *
 * Returns the number of bytes written, one of {1, 2, 3, 5, 9}.
 */
int msg_putu(unsigned char *out, unsigned long long val);

/*
 * Writes the IEEE 754 single-precision float val to the buffer out. Always
 * writes 5 bytes.
 *
 * out must point to at least 5 bytes.
 */
void msg_putf(unsigned char *out, float val);

/*
 * Writes the IEEE 754 double-precision float val to the buffer out.
 *
 * out must point to at least 9 bytes.
 *
 * Returns the number of bytes written, one of {5, 9}.
 */
int msg_putd(unsigned char *out, double val);

/*
 * Writes the string size sz in the range [0, 15] to the buffer out. Values
 * outside this range will produce an undefined encoding. Always writes a single
 * byte.
 *
 * In a complete message stream, a size of N must be immediately followed by N
 * bytes of the actual string, which must be valid UTF-8.
 *
 * out must point to at least 1 byte.
 *
 * It is recommended to use msg_putssz() for arbitrary string sizes. That
 * function will produce the smallest possible encoding for any size value.
 */
void msg_putssz5(unsigned char *out, int sz);

/*
 * Writes the string size sz in the range [0, 255] to the buffer out.
 *
 * In a complete message stream, a size of N must be immediately followed by N
 * bytes of the actual string, which must be valid UTF-8.
 *
 * out must point to at least 2 bytes.
 *
 * It is recommended to use msg_putssz() for arbitrary string sizes. That
 * function will produce the smallest possible encoding for any size value.
 */
int msg_putssz8(unsigned char *out, int sz);

/*
 * Writes the string size sz in the range [0, 65535] to the buffer out.
 *
 * In a complete message stream, a size of N must be immediately followed by N
 * bytes of the actual string, which must be valid UTF-8.
 *
 * out must point to at least 3 bytes.
 *
 * It is recommended to use msg_putssz() for arbitrary string sizes. That
 * function will produce the smallest possible encoding for any size value.
 */
int msg_putssz16(unsigned char *out, int sz);

/*
 * Writes the string size sz in the range [0, 4294967295] to the buffer out.
 *
 * In a complete message stream, a size of N must be immediately followed by N
 * bytes of the actual string, which must be valid UTF-8.
 *
 * out must point to at least 5 bytes.
 */
int msg_putssz(unsigned char *out, unsigned int sz);

/*
 * Writes the binary blob size sz in the range [0, 255] to the buffer out.
 * Always writes 2 bytes.
 *
 * In a complete message stream, a size of N must be immediately followed by
 * N bytes of the actual data.
 *
 * out must point to at least 2 bytes.
 *
 * It is recommended to use msg_putbsz() for arbitrary binary blob sizes. That
 * function will produce the smallest possible encoding for any size value.
 */
void msg_putbsz8(unsigned char *out, int sz);

/*
 * Writes the binary blob size sz in the range [0, 65535] to the buffer out.
 *
 * In a complete message stream, a size of N must be immediately followed by
 * N bytes of the actual data.
 *
 * out must point to at least 3 bytes.
 *
 * Returns the number of bytes written, one of {1, 2, 3}.
 *
 * It is recommended to use msg_putbsz() for arbitrary binary blob sizes. That
 * function will produce the smallest possible encoding for any size value.
 */
int msg_putbsz16(unsigned char *out, int sz);

/*
 * Writes the binary blob size sz in the range [0, 4294967295] to the buffer out.
 *
 * In a complete message stream, a size of N must be immediately followed by
 * N bytes of the actual data.
 *
 * out must point to at least 5 bytes.
 *
 * Returns the number of bytes written, one of {1, 2, 3, 5}.
 */
int msg_putbsz(unsigned char *out, unsigned int sz);

/*
 * Writes the array size sz in the range [0, 15] to the buffer out. Values
 * outside this range will produce an undefined encoding. Always writes a single
 * byte.
 *
 * In a complete message stream, a size of N must be immediately followed by N
 * other messages, which form the contents of the array.
 *
 * out must point to at least 1 byte.
 *
 * It is recommended to use msg_putasz() for arbitrary array sizes. That
 * function will produce the smallest possible encoding for any size value.
 */
void msg_putasz4(unsigned char *out, int sz);

/*
 * Writes the array size sz in the range [0, 65535] to the buffer out.
 *
 * In a complete message stream, a size of N must be immediately followed by N
 * other messages, which form the contents of the array.
 *
 * out must point to at least 3 bytes.
 *
 * Returns the number of bytes written, one of {1, 3}.
 *
 * It is recommended to use msg_putasz() for arbitrary array sizes. That
 * function will produce the smallest possible encoding for any size value.
 */
int msg_putasz16(unsigned char *out, int sz);

/*
 * Writes the array size sz in the range [0, 4294967295] to the buffer out.
 *
 * In a complete message stream, a size of N must be immediately followed by N
 * other messages, which form the contents of the array.
 *
 * out must point to at least 5 bytes.
 *
 * Returns the number of bytes written, one of {1, 3, 5}.
 */
int msg_putasz(unsigned char *out, unsigned int sz);

/*
 * Writes the map size sz in the range [0, 15] to the buffer out. Values
 * outside this range will produce an undefined encoding. Always writes a single
 * byte.
 *
 * In a complete message stream, a size of N must be immediately followed by
 * N * 2 other messages, which form the contents of the map as keys followed by
 * values in alternation.
 *
 * out must point to at least 1 byte.
 *
 * It is recommended to use msg_putmsz() for arbitrary map sizes. That function
 * will produce the smallest possible encoding for any size value.
 */
void msg_putmsz4(unsigned char *out, int sz);

/*
 * Writes the array size sz in the range [0, 65536] to the buffer out.
 *
 * In a complete message stream, a size of N must be immediately followed by
 * N * 2 other messages, which form the contents of the map as keys followed by
 * values in alternation.
 *
 * out must point to at least 3 bytes.
 *
 * Returns the number of bytes written, one of {1, 3}.
 *
 * It is recommended to use msg_putmsz() for arbitrary map sizes. That function
 * will produce the smallest possible encoding for any size value.
 */
int msg_putmsz16(unsigned char *out, int sz);

/*
 * Writes the array size sz in the range [0, 4294967295] to the buffer out.
 *
 * In a complete message stream, a size of N must be immediately followed by
 * N * 2 other messages, which form the contents of the map as keys followed by
 * values in alternation.
 *
 * out must point to at least 5 bytes.
 *
 * Returns the number of bytes written, one of {1, 3, 5}.
 */
int msg_putmsz(unsigned char *out, unsigned int sz);

#ifdef __cplusplus
}
#endif
#undef _msg_Bool

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
