/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The fuzz target of the UTF-8 functions (FuzzTargets).
 *
 * For any input, checks that:
 *   - decoding forward from the start covers the whole input without overlap:
 *     every step takes 1 to 4 bytes inside the input and gives a Unicode scalar
 *     value, and asking only for the length gives the same length;
 *   - at every boundary of that decoding, amark_utf8_decode_before returns the
 *     character that ends there, with the same length and code point;
 *   - amark_utf8_validate agrees with the decoding: it accepts the input exactly
 *     when no step was an invalid sequence (a U+FFFD that is not the bytes
 *     EF BF BD), and otherwise reports the offset of the first one; it accepts
 *     the prefix before that offset, every valid character alone and rejects
 *     every invalid sequence alone;
 *   - encoding the code point of every valid step gives back its bytes, and the
 *     input with every invalid sequence replaced by U+FFFD is well-formed;
 *   - every 4-byte group of the input, read as a code point, encodes to 1 to 4
 *     bytes that decode back to it (to U+FFFD for a surrogate or a value above
 *     U+10FFFF);
 *   - no function reads outside the given length (the input is copied into a
 *     block of its exact size, so a sanitizer catches an overread) or writes to
 *     the input.
 * A violated property aborts, which libFuzzer and the corpus replay report.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <amark/utf8.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static void check_failed(const char *expression, int line)
{
    fprintf(stderr, "utf8_fuzz.c:%d: property violated: %s\n", line, expression);
    abort();
}

#define CHECK(expression)                         \
    do {                                          \
        if (!(expression)) {                      \
            check_failed(#expression, __LINE__);  \
        }                                         \
    } while (0)

static const unsigned char replacement_bytes[3] = {0xEF, 0xBF, 0xBD};

static bool is_scalar_value(uint32_t codepoint)
{
    return codepoint <= 0x10FFFFu && (codepoint < 0xD800u || codepoint > 0xDFFFu);
}

/* Checks the forward decoding, the backward decoding at its boundaries, the
 * validation and the encoding of the valid parts, on text of length size. */
static void check_decoding(const char *text, size_t size)
{
    /* The input with every invalid sequence replaced by U+FFFD: at most 3 bytes
     * per input byte. */
    size_t replaced_capacity = size * 3 + 1;
    char *replaced = (char *)malloc(replaced_capacity);
    size_t replaced_length = 0;
    size_t first_invalid = size;
    size_t position = 0;
    size_t error_offset = 0;
    bool valid;

    CHECK(replaced != NULL);

    CHECK(amark_utf8_decode(text, 0, NULL) == 0);
    CHECK(amark_utf8_decode_before(text, 0, NULL) == 0);

    while (position < size)
    {
        uint32_t codepoint = 0xFFFFFFFFu;
        uint32_t before_codepoint = 0xFFFFFFFFu;
        char encoded[AMARK_UTF8_MAX_LENGTH];
        size_t encoded_length;
        size_t length = amark_utf8_decode(text + position, size - position, &codepoint);
        size_t before_length;
        bool is_invalid;

        /* Covers the input without overlap or gap. */
        CHECK(length >= 1 && length <= AMARK_UTF8_MAX_LENGTH);
        CHECK(length <= size - position);
        CHECK(is_scalar_value(codepoint));
        CHECK(amark_utf8_decode(text + position, size - position, NULL) == length);

        /* The backward decoding at the boundary after this character. */
        before_length = amark_utf8_decode_before(text, position + length, &before_codepoint);
        CHECK(before_length == length);
        CHECK(before_codepoint == codepoint);
        CHECK(amark_utf8_decode_before(text, position + length, NULL) == length);

        is_invalid = codepoint == AMARK_UTF8_REPLACEMENT &&
                     !(length == 3 && memcmp(text + position, replacement_bytes, 3) == 0);

        encoded_length = amark_utf8_encode(codepoint, encoded);
        CHECK(encoded_length >= 1 && encoded_length <= AMARK_UTF8_MAX_LENGTH);
        if (is_invalid)
        {
            CHECK(encoded_length == 3 && memcmp(encoded, replacement_bytes, 3) == 0);
            if (first_invalid == size)
            {
                first_invalid = position;
            }
            /* The invalid sequence alone is not valid either. */
            CHECK(!amark_utf8_validate(text + position, length, NULL));
        }
        else
        {
            /* The encoding of a valid character gives back its bytes. */
            CHECK(encoded_length == length);
            CHECK(memcmp(encoded, text + position, length) == 0);
            /* The valid character alone is valid. */
            CHECK(amark_utf8_validate(text + position, length, NULL));
        }
        CHECK(replaced_length + encoded_length < replaced_capacity);
        memcpy(replaced + replaced_length, encoded, encoded_length);
        replaced_length += encoded_length;
        position += length;
    }
    CHECK(position == size);

    /* The validation agrees with the positions of the replacements. */
    valid = amark_utf8_validate(text, size, &error_offset);
    CHECK(valid == (first_invalid == size));
    CHECK(error_offset == first_invalid);
    CHECK(amark_utf8_validate(text, size, NULL) == valid);
    /* The prefix before the first invalid sequence is valid. */
    error_offset = size + 1;
    CHECK(amark_utf8_validate(text, first_invalid, &error_offset));
    CHECK(error_offset == first_invalid);

    /* The input with its invalid sequences replaced is well-formed, and is the
     * input itself when the input is valid. */
    error_offset = 0;
    CHECK(amark_utf8_validate(replaced, replaced_length, &error_offset));
    CHECK(error_offset == replaced_length);
    if (valid)
    {
        CHECK(replaced_length == size && memcmp(replaced, text, size) == 0);
    }
    free(replaced);
}

/* Encodes every 4-byte group of the input, read as a little-endian code point,
 * and decodes it back. */
static void check_encoding(const uint8_t *data, size_t size)
{
    size_t i;

    for (i = 0; i + 4 <= size; i += 4)
    {
        uint32_t codepoint = (uint32_t)data[i] | ((uint32_t)data[i + 1] << 8) |
                             ((uint32_t)data[i + 2] << 16) | ((uint32_t)data[i + 3] << 24);
        /* Also try the low 21 bits, which hit the Unicode range far more often. */
        uint32_t candidates[2];
        size_t c;

        candidates[0] = codepoint;
        candidates[1] = codepoint & 0x1FFFFFu;
        for (c = 0; c < 2; c++)
        {
            char encoded[AMARK_UTF8_MAX_LENGTH];
            uint32_t decoded = 0xFFFFFFFFu;
            uint32_t expected = is_scalar_value(candidates[c]) ? candidates[c]
                                                               : AMARK_UTF8_REPLACEMENT;
            size_t length = amark_utf8_encode(candidates[c], encoded);
            size_t expected_length = expected < 0x80u      ? 1
                                     : expected < 0x800u   ? 2
                                     : expected < 0x10000u ? 3
                                                           : 4;

            CHECK(length == expected_length);
            CHECK(amark_utf8_validate(encoded, length, NULL));
            CHECK(amark_utf8_decode(encoded, length, &decoded) == length);
            CHECK(decoded == expected);
        }
    }
    CHECK(amark_utf8_encode(0x41u, NULL) == 0);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    /* A block of exactly size bytes (at least 1 so that it is never NULL), so that
     * a read past the length is a heap overflow for a sanitizer, and a copy to
     * check that the input is not written to. */
    char *text = (char *)malloc(size > 0 ? size : 1);
    char *original = (char *)malloc(size > 0 ? size : 1);

    CHECK(text != NULL && original != NULL);
    if (size > 0)
    {
        memcpy(text, data, size);
        memcpy(original, data, size);
    }

    CHECK(amark_utf8_decode(NULL, size, NULL) == 0);
    CHECK(amark_utf8_decode_before(NULL, size, NULL) == 0);
    CHECK(amark_utf8_validate(NULL, 0, NULL));
    if (size > 0)
    {
        size_t error_offset = size;
        CHECK(!amark_utf8_validate(NULL, size, &error_offset));
        CHECK(error_offset == 0);
    }

    check_decoding(text, size);
    check_encoding(data, size);

    CHECK(size == 0 || memcmp(text, original, size) == 0);
    free(original);
    free(text);
    return 0;
}
