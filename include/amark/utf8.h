/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * Decoding, validation and encoding of UTF-8.
 *
 * The functions never fail: invalid input is not an error but decodes to
 * U+FFFD REPLACEMENT CHARACTER, so that processing can go on.
 *
 * Common rules:
 *   - The input is a byte sequence with an explicit length. It need not be
 *     NUL-terminated, and a NUL byte inside it is an ordinary character (U+0000).
 *   - No function reads outside of the given length or modifies the input.
 *
 * Well-formed sequences (Unicode Standard, chapter 3, table 3-7):
 *   00..7F
 *   C2..DF 80..BF
 *   E0     A0..BF 80..BF
 *   E1..EC 80..BF 80..BF
 *   ED     80..9F 80..BF
 *   EE..EF 80..BF 80..BF
 *   F0     90..BF 80..BF 80..BF
 *   F1..F3 80..BF 80..BF 80..BF
 *   F4     80..8F 80..BF 80..BF
 * Everything else (overlong forms, surrogates, values above U+10FFFF, truncated
 * sequences, lone continuation bytes, the bytes C0, C1 and F5..FF) is invalid.
 *
 * An invalid sequence is replaced following the "maximal subpart" practice of the
 * Unicode Standard (chapter 3, U+FFFD substitution): one U+FFFD for the longest
 * prefix of a well-formed sequence, or for a single byte when no such prefix
 * longer than one byte exists (an invalid lead byte or a lone continuation byte).
 */
#ifndef AMARK_UTF8_H
#define AMARK_UTF8_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <amark/export.h>

AMARK_BEGIN_DECLS

/* U+FFFD REPLACEMENT CHARACTER, the result of decoding an invalid sequence. */
#define AMARK_UTF8_REPLACEMENT 0xFFFDu

/* The longest UTF-8 sequence of one code point, in bytes. */
#define AMARK_UTF8_MAX_LENGTH 4

/*
 * Decodes the first character of the length bytes at text. Returns the number of
 * bytes it takes (1 to 4) and stores its code point in *out_codepoint, or
 * AMARK_UTF8_REPLACEMENT for an invalid sequence, which then takes the bytes of
 * its maximal subpart. Returns 0 and stores nothing when length is 0 or text is
 * NULL. out_codepoint may be NULL to learn only the length.
 */
AMARK_API size_t amark_utf8_decode(const char *text, size_t length, uint32_t *out_codepoint);

/*
 * Decodes the last character of the position bytes at text, that is, the
 * character that ends just before position. Returns its number of bytes (1 to 4)
 * and stores its code point in *out_codepoint, like amark_utf8_decode. When
 * position is a boundary of decoding text from its start with amark_utf8_decode,
 * the result is the character that ends there, with the same code point and
 * length. Returns 0 and stores nothing when position is 0 or text is NULL.
 * out_codepoint may be NULL.
 */
AMARK_API size_t amark_utf8_decode_before(const char *text, size_t position,
                                          uint32_t *out_codepoint);

/*
 * Checks whether the length bytes at text are well-formed UTF-8. Returns true and
 * stores length in *out_error_offset when they are; otherwise returns false and
 * stores the offset of the first invalid sequence. A NULL text is valid only when
 * length is 0. out_error_offset may be NULL.
 */
AMARK_API bool amark_utf8_validate(const char *text, size_t length, size_t *out_error_offset);

/*
 * Encodes codepoint as UTF-8 into out and returns the number of bytes written
 * (1 to 4). A surrogate (U+D800..U+DFFF) or a value above U+10FFFF is encoded as
 * AMARK_UTF8_REPLACEMENT (3 bytes). Returns 0 and writes nothing when out is
 * NULL.
 */
AMARK_API size_t amark_utf8_encode(uint32_t codepoint, char out[AMARK_UTF8_MAX_LENGTH]);

AMARK_END_DECLS

#endif /* AMARK_UTF8_H */
