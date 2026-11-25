/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * UTF-8 decoding, validation and encoding (13.1-13.7), with hand-written cases.
 * The data-driven test vectors are in a separate test.
 *
 * Every input is copied into a heap block of exactly its size (a block of one
 * byte, passed one past its end, for the empty input), so that a read outside of
 * the given length is caught by AddressSanitizer. The helpers also check that the
 * input is left unchanged (13.6).
 */
#include <amark/amark.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <naos/naos.h>

#include "test_check.h"

/* --- Helpers --- */

/* A copy of the input in a heap block of exactly its size. */
typedef struct exact
{
    char *block;
    char *text;
    size_t size;
} exact_t;

static exact_t exact_copy(const char *bytes, size_t size)
{
    exact_t e;

    if (size == 0)
    {
        e.block = malloc(1);
        e.text = e.block != NULL ? e.block + 1 : NULL;
    }
    else
    {
        e.block = malloc(size);
        if (e.block != NULL)
        {
            memcpy(e.block, bytes, size);
        }
        e.text = e.block;
    }
    e.size = size;
    return e;
}

/* Frees the copy and returns 1 when the input was changed (or not allocated). */
static int exact_release(exact_t *e, const char *original)
{
    int changed = e->block == NULL || (e->size > 0 && memcmp(e->text, original, e->size) != 0);

    free(e->block);
    e->block = NULL;
    e->text = NULL;
    return changed;
}

/* One character of an expected decoding. */
typedef struct piece
{
    uint32_t codepoint;
    size_t length;
} piece_t;

#define PIECES(...) ((const piece_t[]){__VA_ARGS__}), (sizeof((const piece_t[]){__VA_ARGS__}) / sizeof(piece_t))

/*
 * Decodes bytes[0..size) from the start and compares the characters with
 * expected[0..count). Then decodes backward from the end, which must give the
 * same characters, and checks decode_before at every boundary. Returns the number
 * of mismatches, so that the caller can report the case at its own line.
 */
static int check_decoding(const char *bytes, size_t size, const piece_t *expected, size_t count)
{
    exact_t e = exact_copy(bytes, size);
    size_t offset = 0;
    size_t i = 0;
    int failures = 0;

    if (e.text == NULL)
    {
        return 1;
    }
    while (offset < size)
    {
        uint32_t cp = 0x12345678u;
        size_t n = amark_utf8_decode(e.text + offset, size - offset, &cp);
        size_t n_without_out = amark_utf8_decode(e.text + offset, size - offset, NULL);

        if (n == 0 || n > AMARK_UTF8_MAX_LENGTH || n > size - offset || n != n_without_out)
        {
            NAOS_CHECK_UINT_EQ(i < count ? expected[i].length : 0, n);
            failures++;
            break;
        }
        if (i >= count || expected[i].codepoint != cp || expected[i].length != n)
        {
            NAOS_CHECK_UINT_EQ(i < count ? expected[i].codepoint : 0xFFFFFFFFu, cp);
            NAOS_CHECK_UINT_EQ(i < count ? expected[i].length : 0, n);
            failures++;
        }
        offset += n;
        i++;
    }
    if (i != count)
    {
        NAOS_CHECK_UINT_EQ(count, i);
        failures++;
    }

    /* Backward from the end: the same characters in reverse order (13.3). */
    offset = size;
    i = count;
    while (offset > 0 && i > 0)
    {
        uint32_t cp = 0x12345678u;
        size_t n = amark_utf8_decode_before(e.text, offset, &cp);

        i--;
        if (n != expected[i].length || cp != expected[i].codepoint)
        {
            NAOS_CHECK_UINT_EQ(expected[i].codepoint, cp);
            NAOS_CHECK_UINT_EQ(expected[i].length, n);
            failures++;
            break;
        }
        if (amark_utf8_decode_before(e.text, offset, NULL) != n)
        {
            NAOS_CHECK_UINT_EQ(n, amark_utf8_decode_before(e.text, offset, NULL));
            failures++;
        }
        offset -= n;
    }
    if (offset != 0 || i != 0)
    {
        NAOS_CHECK_UINT_EQ(0, offset);
        failures++;
    }

    if (exact_release(&e, bytes))
    {
        NAOS_FAIL("the input was changed");
    }
    return failures;
}

#define CHECK_DECODING(literal, ...) \
    NAOS_CHECK_INT_EQ(0, check_decoding(("" literal), sizeof("" literal) - 1, PIECES(__VA_ARGS__)))

/* Validates bytes[0..size) and returns the result and the offset. */
static bool validate_exact(const char *bytes, size_t size, size_t *out_offset)
{
    exact_t e = exact_copy(bytes, size);
    bool valid;

    *out_offset = (size_t)-1;
    if (e.text == NULL)
    {
        return false;
    }
    valid = amark_utf8_validate(e.text, size, out_offset);
    if (exact_release(&e, bytes))
    {
        NAOS_FAIL("the input was changed");
    }
    return valid;
}

#define CHECK_VALID(literal)                                                   \
    do {                                                                       \
        size_t offset_;                                                        \
        NAOS_CHECK_TRUE(validate_exact(("" literal), sizeof("" literal) - 1, &offset_)); \
        NAOS_CHECK_UINT_EQ(sizeof("" literal) - 1, offset_);                   \
    } while (0)

#define CHECK_INVALID_AT(literal, expected_offset)                             \
    do {                                                                       \
        size_t offset_;                                                        \
        NAOS_CHECK_FALSE(validate_exact(("" literal), sizeof("" literal) - 1, &offset_)); \
        NAOS_CHECK_UINT_EQ((expected_offset), offset_);                        \
    } while (0)

#define R AMARK_UTF8_REPLACEMENT

/* --- Decoding well-formed sequences (13.1) --- */

NAOS_TEST(constants)
{
    NAOS_CHECK_UINT_EQ(0xFFFDu, AMARK_UTF8_REPLACEMENT);
    NAOS_CHECK_INT_EQ(4, AMARK_UTF8_MAX_LENGTH);
}

NAOS_TEST(decodes_one_byte_boundaries)
{
    CHECK_DECODING("\x00", {0x00, 1});
    CHECK_DECODING("\x01", {0x01, 1});
    CHECK_DECODING("A", {0x41, 1});
    CHECK_DECODING("\x7F", {0x7F, 1});
}

NAOS_TEST(decodes_two_byte_boundaries)
{
    CHECK_DECODING("\xC2\x80", {0x80, 2});
    CHECK_DECODING("\xC2\xBF", {0xBF, 2});
    CHECK_DECODING("\xDF\x80", {0x7C0, 2});
    CHECK_DECODING("\xDF\xBF", {0x7FF, 2});
}

NAOS_TEST(decodes_three_byte_boundaries)
{
    CHECK_DECODING("\xE0\xA0\x80", {0x800, 3});
    CHECK_DECODING("\xE0\xBF\xBF", {0xFFF, 3});
    CHECK_DECODING("\xE1\x80\x80", {0x1000, 3});
    CHECK_DECODING("\xEC\xBF\xBF", {0xCFFF, 3});
    CHECK_DECODING("\xED\x80\x80", {0xD000, 3});
    CHECK_DECODING("\xED\x9F\xBF", {0xD7FF, 3});
    CHECK_DECODING("\xEE\x80\x80", {0xE000, 3});
    CHECK_DECODING("\xEF\xBF\xBD", {0xFFFD, 3});
    CHECK_DECODING("\xEF\xBF\xBF", {0xFFFF, 3});
    CHECK_DECODING("\xE2\x82\xAC", {0x20AC, 3});
}

NAOS_TEST(decodes_four_byte_boundaries)
{
    CHECK_DECODING("\xF0\x90\x80\x80", {0x10000, 4});
    CHECK_DECODING("\xF0\xBF\xBF\xBF", {0x3FFFF, 4});
    CHECK_DECODING("\xF1\x80\x80\x80", {0x40000, 4});
    CHECK_DECODING("\xF3\xBF\xBF\xBF", {0xFFFFF, 4});
    CHECK_DECODING("\xF4\x80\x80\x80", {0x100000, 4});
    CHECK_DECODING("\xF4\x8F\xBF\xBF", {0x10FFFF, 4});
    CHECK_DECODING("\xF0\x9F\x98\x80", {0x1F600, 4});
}

NAOS_TEST(decodes_a_mixed_sequence)
{
    CHECK_DECODING("a\xC3\xA9\xE3\x81\x82\xF0\x9F\x98\x80z",
                   {0x61, 1}, {0xE9, 2}, {0x3042, 3}, {0x1F600, 4}, {0x7A, 1});
}

NAOS_TEST(decodes_only_the_first_character)
{
    uint32_t cp = 0;

    NAOS_CHECK_UINT_EQ(2, amark_utf8_decode("\xC3\xA9xyz", 5, &cp));
    NAOS_CHECK_UINT_EQ(0xE9, cp);
}

/* --- Invalid sequences: maximal subparts (13.2) --- */

NAOS_TEST(overlong_forms_are_replaced_byte_by_byte)
{
    CHECK_DECODING("\xC0\x80", {R, 1}, {R, 1});
    CHECK_DECODING("\xC0\xAF", {R, 1}, {R, 1});
    CHECK_DECODING("\xC1\xBF", {R, 1}, {R, 1});
    CHECK_DECODING("\xE0\x80\x80", {R, 1}, {R, 1}, {R, 1});
    CHECK_DECODING("\xE0\x80\xAF", {R, 1}, {R, 1}, {R, 1});
    CHECK_DECODING("\xE0\x9F\xBF", {R, 1}, {R, 1}, {R, 1});
    CHECK_DECODING("\xF0\x80\x80\x80", {R, 1}, {R, 1}, {R, 1}, {R, 1});
    CHECK_DECODING("\xF0\x8F\xBF\xBF", {R, 1}, {R, 1}, {R, 1}, {R, 1});
}

NAOS_TEST(surrogates_are_replaced_byte_by_byte)
{
    CHECK_DECODING("\xED\xA0\x80", {R, 1}, {R, 1}, {R, 1});
    CHECK_DECODING("\xED\xAD\xBF", {R, 1}, {R, 1}, {R, 1});
    CHECK_DECODING("\xED\xB0\x80", {R, 1}, {R, 1}, {R, 1});
    CHECK_DECODING("\xED\xBF\xBF", {R, 1}, {R, 1}, {R, 1});
    /* A surrogate pair (CESU-8) is two invalid sequences. */
    CHECK_DECODING("\xED\xA0\xBD\xED\xB8\x80",
                   {R, 1}, {R, 1}, {R, 1}, {R, 1}, {R, 1}, {R, 1});
}

NAOS_TEST(values_above_10ffff_are_replaced)
{
    CHECK_DECODING("\xF4\x90\x80\x80", {R, 1}, {R, 1}, {R, 1}, {R, 1});
    CHECK_DECODING("\xF4\xBF\xBF\xBF", {R, 1}, {R, 1}, {R, 1}, {R, 1});
    CHECK_DECODING("\xF5\x80\x80\x80", {R, 1}, {R, 1}, {R, 1}, {R, 1});
    CHECK_DECODING("\xF7\xBF\xBF\xBF", {R, 1}, {R, 1}, {R, 1}, {R, 1});
    /* The old 5- and 6-byte forms. */
    CHECK_DECODING("\xF8\x88\x80\x80\x80", {R, 1}, {R, 1}, {R, 1}, {R, 1}, {R, 1});
    CHECK_DECODING("\xFC\x84\x80\x80\x80\x80",
                   {R, 1}, {R, 1}, {R, 1}, {R, 1}, {R, 1}, {R, 1});
}

NAOS_TEST(invalid_lead_bytes_are_replaced_alone)
{
    CHECK_DECODING("\xC0", {R, 1});
    CHECK_DECODING("\xC1", {R, 1});
    CHECK_DECODING("\xF5", {R, 1});
    CHECK_DECODING("\xF8", {R, 1});
    CHECK_DECODING("\xFE", {R, 1});
    CHECK_DECODING("\xFF", {R, 1});
    CHECK_DECODING("\xFE\xFF", {R, 1}, {R, 1});
    CHECK_DECODING("\xC0" "A", {R, 1}, {0x41, 1});
}

NAOS_TEST(truncated_sequences_are_replaced_as_one)
{
    /* At the end of the input. */
    CHECK_DECODING("\xC2", {R, 1});
    CHECK_DECODING("\xDF", {R, 1});
    CHECK_DECODING("\xE0", {R, 1});
    CHECK_DECODING("\xE0\xA0", {R, 2});
    CHECK_DECODING("\xE2\x82", {R, 2});
    CHECK_DECODING("\xED\x9F", {R, 2});
    CHECK_DECODING("\xEF\xBF", {R, 2});
    CHECK_DECODING("\xF0", {R, 1});
    CHECK_DECODING("\xF0\x90", {R, 2});
    CHECK_DECODING("\xF0\x90\x80", {R, 3});
    CHECK_DECODING("\xF4\x8F\xBF", {R, 3});
    CHECK_DECODING("\xF3\xBF\xBF", {R, 3});
    /* Before another character. */
    CHECK_DECODING("\xC2" "A", {R, 1}, {0x41, 1});
    CHECK_DECODING("\xE2\x82" "A", {R, 2}, {0x41, 1});
    CHECK_DECODING("\xF0\x90\x80" "A", {R, 3}, {0x41, 1});
    CHECK_DECODING("\xE2\xE2\x82\xAC", {R, 1}, {0x20AC, 3});
    CHECK_DECODING("\xF0\x90\xE2\x82\xAC", {R, 2}, {0x20AC, 3});
    CHECK_DECODING("\xE2\x82\xC3\xA9", {R, 2}, {0xE9, 2});
    CHECK_DECODING("\xF4\x8F\xBF\x00", {R, 3}, {0x00, 1});
}

NAOS_TEST(lone_continuation_bytes_are_replaced_alone)
{
    CHECK_DECODING("\x80", {R, 1});
    CHECK_DECODING("\xBF", {R, 1});
    CHECK_DECODING("\x80\x80", {R, 1}, {R, 1});
    CHECK_DECODING("\x80\x80\x80\x80\x80", {R, 1}, {R, 1}, {R, 1}, {R, 1}, {R, 1});
    CHECK_DECODING("A\x80" "B", {0x41, 1}, {R, 1}, {0x42, 1});
    /* After a complete character. */
    CHECK_DECODING("\xC2\x80\x80", {0x80, 2}, {R, 1});
    CHECK_DECODING("\xE2\x82\xAC\xAC", {0x20AC, 3}, {R, 1});
    CHECK_DECODING("\xF0\x90\x80\x80\x80", {0x10000, 4}, {R, 1});
    CHECK_DECODING("\xF4\x8F\xBF\xBF\xBF\xBF", {0x10FFFF, 4}, {R, 1}, {R, 1});
}

NAOS_TEST(follows_the_unicode_examples)
{
    /* Unicode Standard, chapter 3, U+FFFD substitution of maximal subparts. */
    CHECK_DECODING("\x61\xF1\x80\x80\xE1\x80\xC2\x62\x80\x63\x80\xBF\x64",
                   {0x61, 1}, {R, 3}, {R, 2}, {R, 1}, {0x62, 1}, {R, 1}, {0x63, 1},
                   {R, 1}, {R, 1}, {0x64, 1});
    CHECK_DECODING("\x61\xC0\xAF\xE0\x80\xBF\xF0\x81\x82\x41",
                   {0x61, 1}, {R, 1}, {R, 1}, {R, 1}, {R, 1}, {R, 1}, {R, 1}, {R, 1},
                   {R, 1}, {0x41, 1});
    CHECK_DECODING("\x61\xED\xA0\x80\xED\xBF\xBF\xED\xAF\x41",
                   {0x61, 1}, {R, 1}, {R, 1}, {R, 1}, {R, 1}, {R, 1}, {R, 1}, {R, 1},
                   {R, 1}, {0x41, 1});
    CHECK_DECODING("\x61\xF4\x91\x92\x93\xFF\x41\x80\xBF\x42",
                   {0x61, 1}, {R, 1}, {R, 1}, {R, 1}, {R, 1}, {R, 1}, {0x41, 1}, {R, 1},
                   {R, 1}, {0x42, 1});
    CHECK_DECODING("\x61\xE1\x80\xE2\xF0\x91\x92\xF1\xBF\x41",
                   {0x61, 1}, {R, 2}, {R, 1}, {R, 3}, {R, 2}, {0x41, 1});
}

NAOS_TEST(length_limits_the_sequence)
{
    /* A complete character cut by the length is a truncated sequence; the bytes
     * after the length are not read (the exact-size copy catches a read). */
    exact_t e = exact_copy("\xF0\x9F\x98\x80", 4);
    uint32_t cp = 0;

    NAOS_REQUIRE_NOT_NULL(e.text);
    NAOS_CHECK_UINT_EQ(1, amark_utf8_decode(e.text, 1, &cp));
    NAOS_CHECK_UINT_EQ(R, cp);
    NAOS_CHECK_UINT_EQ(2, amark_utf8_decode(e.text, 2, &cp));
    NAOS_CHECK_UINT_EQ(R, cp);
    NAOS_CHECK_UINT_EQ(3, amark_utf8_decode(e.text, 3, &cp));
    NAOS_CHECK_UINT_EQ(R, cp);
    NAOS_CHECK_UINT_EQ(4, amark_utf8_decode(e.text, 4, &cp));
    NAOS_CHECK_UINT_EQ(0x1F600, cp);
    NAOS_CHECK_INT_EQ(0, exact_release(&e, "\xF0\x9F\x98\x80"));
}

/* --- Backward decoding (13.3) --- */

NAOS_TEST(decode_before_decodes_the_prefix)
{
    /* position inside a character: the prefix text[0..position) is decoded, as
     * if it were the whole input. */
    const char *text = "\xE2\x82\xAC";
    uint32_t cp = 0;

    NAOS_CHECK_UINT_EQ(1, amark_utf8_decode_before(text, 1, &cp));
    NAOS_CHECK_UINT_EQ(R, cp);
    NAOS_CHECK_UINT_EQ(2, amark_utf8_decode_before(text, 2, &cp));
    NAOS_CHECK_UINT_EQ(R, cp);
    NAOS_CHECK_UINT_EQ(3, amark_utf8_decode_before(text, 3, &cp));
    NAOS_CHECK_UINT_EQ(0x20AC, cp);
}

NAOS_TEST(decode_before_looks_back_at_most_three_continuation_bytes)
{
    const char *text = "A\xF0\x90\x80\x80\x80\x80";
    uint32_t cp = 0;

    NAOS_CHECK_UINT_EQ(4, amark_utf8_decode_before(text, 5, &cp));
    NAOS_CHECK_UINT_EQ(0x10000, cp);
    NAOS_CHECK_UINT_EQ(1, amark_utf8_decode_before(text, 6, &cp));
    NAOS_CHECK_UINT_EQ(R, cp);
    NAOS_CHECK_UINT_EQ(1, amark_utf8_decode_before(text, 7, &cp));
    NAOS_CHECK_UINT_EQ(R, cp);
}

NAOS_TEST(decode_before_at_the_start_of_the_input)
{
    /* Continuation bytes with no lead byte before them, down to offset 0. */
    exact_t e = exact_copy("\x80\x80\x80", 3);
    uint32_t cp = 0;

    NAOS_REQUIRE_NOT_NULL(e.text);
    NAOS_CHECK_UINT_EQ(1, amark_utf8_decode_before(e.text, 3, &cp));
    NAOS_CHECK_UINT_EQ(R, cp);
    NAOS_CHECK_UINT_EQ(1, amark_utf8_decode_before(e.text, 1, &cp));
    NAOS_CHECK_UINT_EQ(R, cp);
    NAOS_CHECK_INT_EQ(0, exact_release(&e, "\x80\x80\x80"));
}

/*
 * Every input of up to 4 bytes over a set of interesting bytes: decoding from the
 * start covers the input with characters of 1 to 4 bytes, decode_before agrees at
 * every boundary, a character is valid exactly when encoding its code point gives
 * its bytes back, and validate reports the first invalid character.
 */
static const unsigned char interesting[] = {
    0x00, 0x41, 0x7F, 0x80, 0x8F, 0x90, 0x9F, 0xA0, 0xBF, 0xC0, 0xC1, 0xC2, 0xDF,
    0xE0, 0xE1, 0xEC, 0xED, 0xEE, 0xEF, 0xF0, 0xF1, 0xF3, 0xF4, 0xF5, 0xFF,
};

#define INTERESTING_COUNT (sizeof(interesting) / sizeof(interesting[0]))

static int check_properties(const char *text, size_t size)
{
    size_t boundaries[5];
    uint32_t codepoints[5];
    size_t count = 0;
    size_t offset = 0;
    size_t first_invalid = size;
    size_t validate_offset = (size_t)-1;
    bool valid;
    size_t i;

    while (offset < size)
    {
        char encoded[AMARK_UTF8_MAX_LENGTH];
        uint32_t cp = 0;
        size_t n = amark_utf8_decode(text + offset, size - offset, &cp);
        size_t m;

        if (n == 0 || n > AMARK_UTF8_MAX_LENGTH || n > size - offset)
        {
            return 1;
        }
        m = amark_utf8_encode(cp, encoded);
        if (first_invalid == size && (m != n || memcmp(encoded, text + offset, n) != 0))
        {
            if (cp != R)
            {
                return 2; /* a valid code point that does not round-trip */
            }
            first_invalid = offset;
        }
        codepoints[count] = cp;
        offset += n;
        boundaries[count++] = offset;
    }
    for (i = count; i > 0; i--)
    {
        uint32_t cp = 0;
        size_t start = i > 1 ? boundaries[i - 2] : 0;
        size_t n = amark_utf8_decode_before(text, boundaries[i - 1], &cp);

        if (n != boundaries[i - 1] - start || cp != codepoints[i - 1])
        {
            return 3;
        }
    }
    valid = amark_utf8_validate(text, size, &validate_offset);
    if (valid != (first_invalid == size) || validate_offset != first_invalid)
    {
        return 4;
    }
    return 0;
}

NAOS_TEST(short_inputs_keep_every_property)
{
    size_t a;
    size_t b;
    size_t c;
    size_t d;
    int failures = 0;
    char *block = calloc(4, 1);

    NAOS_REQUIRE_NOT_NULL(block);
    /* Each input is placed at the end of the 4-byte block, so that a read past
     * its end is caught. */
    for (a = 0; a < INTERESTING_COUNT; a++)
    {
        block[3] = (char)interesting[a];
        failures += check_properties(block + 3, 1) != 0;
        for (b = 0; b < INTERESTING_COUNT; b++)
        {
            block[2] = (char)interesting[a];
            block[3] = (char)interesting[b];
            failures += check_properties(block + 2, 2) != 0;
            for (c = 0; c < INTERESTING_COUNT; c++)
            {
                block[1] = (char)interesting[a];
                block[2] = (char)interesting[b];
                block[3] = (char)interesting[c];
                failures += check_properties(block + 1, 3) != 0;
                for (d = 0; d < INTERESTING_COUNT; d++)
                {
                    block[0] = (char)interesting[a];
                    block[1] = (char)interesting[b];
                    block[2] = (char)interesting[c];
                    block[3] = (char)interesting[d];
                    failures += check_properties(block, 4) != 0;
                }
            }
        }
    }
    free(block);
    NAOS_CHECK_INT_EQ(0, failures);
}

NAOS_TEST(all_two_byte_inputs_keep_every_property)
{
    unsigned a;
    unsigned b;
    int failures = 0;
    char *block = calloc(2, 1);

    NAOS_REQUIRE_NOT_NULL(block);
    for (a = 0; a < 256; a++)
    {
        for (b = 0; b < 256; b++)
        {
            block[0] = (char)a;
            block[1] = (char)b;
            failures += check_properties(block, 2) != 0;
        }
    }
    free(block);
    NAOS_CHECK_INT_EQ(0, failures);
}

/* --- Validation (13.4) --- */

NAOS_TEST(validate_accepts_well_formed_input)
{
    CHECK_VALID("hello");
    CHECK_VALID("\x7F\xC2\x80\xDF\xBF\xE0\xA0\x80\xED\x9F\xBF\xEE\x80\x80\xEF\xBF\xBF");
    CHECK_VALID("\xF0\x90\x80\x80\xF4\x8F\xBF\xBF");
    CHECK_VALID("\xEF\xBF\xBD"); /* U+FFFD itself is valid */
}

NAOS_TEST(validate_reports_the_first_invalid_offset)
{
    CHECK_INVALID_AT("\x80", 0);
    CHECK_INVALID_AT("abc\xC0\x80", 3);
    CHECK_INVALID_AT("\xE2\x82\xAC\xED\xA0\x80", 3);
    CHECK_INVALID_AT("ab\xE2\x82", 2);
    CHECK_INVALID_AT("ab\xF4\x90\x80\x80", 2);
    CHECK_INVALID_AT("\xC3\xA9\xFF\x80", 2);
    CHECK_INVALID_AT("\xF0\x9F\x98\x80" "x\xE0\x9F\xBF" "\xC0", 5);
    CHECK_INVALID_AT("a\x00\xF0\x9F\x98", 2);
}

NAOS_TEST(validate_accepts_a_null_offset)
{
    NAOS_CHECK_TRUE(amark_utf8_validate("ab", 2, NULL));
    NAOS_CHECK_FALSE(amark_utf8_validate("a\x80", 2, NULL));
}

/* --- Encoding (13.5) --- */

#define CHECK_ENCODE(codepoint, literal)                                       \
    do {                                                                       \
        char out_[AMARK_UTF8_MAX_LENGTH] = {0};                                \
        size_t n_ = amark_utf8_encode((codepoint), out_);                      \
        NAOS_CHECK_UINT_EQ(sizeof("" literal) - 1, n_);                        \
        AMARK_CHECK_BYTES_EQ(out_, n_, literal);                               \
    } while (0)

NAOS_TEST(encodes_the_boundaries)
{
    CHECK_ENCODE(0x00, "\x00");
    CHECK_ENCODE(0x7F, "\x7F");
    CHECK_ENCODE(0x80, "\xC2\x80");
    CHECK_ENCODE(0x7FF, "\xDF\xBF");
    CHECK_ENCODE(0x800, "\xE0\xA0\x80");
    CHECK_ENCODE(0xD7FF, "\xED\x9F\xBF");
    CHECK_ENCODE(0xE000, "\xEE\x80\x80");
    CHECK_ENCODE(0xFFFD, "\xEF\xBF\xBD");
    CHECK_ENCODE(0xFFFF, "\xEF\xBF\xBF");
    CHECK_ENCODE(0x10000, "\xF0\x90\x80\x80");
    CHECK_ENCODE(0x10FFFF, "\xF4\x8F\xBF\xBF");
}

NAOS_TEST(encodes_invalid_values_as_the_replacement)
{
    CHECK_ENCODE(0xD800, "\xEF\xBF\xBD");
    CHECK_ENCODE(0xDBFF, "\xEF\xBF\xBD");
    CHECK_ENCODE(0xDC00, "\xEF\xBF\xBD");
    CHECK_ENCODE(0xDFFF, "\xEF\xBF\xBD");
    CHECK_ENCODE(0x110000, "\xEF\xBF\xBD");
    CHECK_ENCODE(0x7FFFFFFF, "\xEF\xBF\xBD");
    CHECK_ENCODE(0xFFFFFFFFu, "\xEF\xBF\xBD");
}

NAOS_TEST(encode_writes_only_its_bytes)
{
    char out[AMARK_UTF8_MAX_LENGTH] = {'x', 'x', 'x', 'x'};

    NAOS_CHECK_UINT_EQ(2, amark_utf8_encode(0xE9, out));
    AMARK_CHECK_BYTES_EQ(out, 4, "\xC3\xA9xx");
}

NAOS_TEST(every_code_point_round_trips)
{
    uint32_t cp;
    int failures = 0;

    for (cp = 0; cp <= 0x10FFFF; cp++)
    {
        char out[AMARK_UTF8_MAX_LENGTH];
        uint32_t decoded = 0;
        uint32_t before = 0;
        size_t n = amark_utf8_encode(cp, out);
        size_t expected = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;

        if (cp >= 0xD800 && cp <= 0xDFFF)
        {
            failures += n != 3 || memcmp(out, "\xEF\xBF\xBD", 3) != 0;
            continue;
        }
        failures += n != expected;
        failures += amark_utf8_decode(out, n, &decoded) != n || decoded != cp;
        failures += amark_utf8_decode_before(out, n, &before) != n || before != cp;
        failures += !amark_utf8_validate(out, n, NULL);
    }
    NAOS_CHECK_INT_EQ(0, failures);
}

/* --- Length 0, NULL and embedded NUL (13.6, 13.7) --- */

NAOS_TEST(empty_input_decodes_nothing)
{
    exact_t e = exact_copy("", 0);
    uint32_t cp = 0x12345678u;
    size_t offset = 99;

    NAOS_REQUIRE_NOT_NULL(e.text);
    NAOS_CHECK_UINT_EQ(0, amark_utf8_decode(e.text, 0, &cp));
    NAOS_CHECK_UINT_EQ(0x12345678u, cp);
    NAOS_CHECK_UINT_EQ(0, amark_utf8_decode_before(e.text, 0, &cp));
    NAOS_CHECK_UINT_EQ(0x12345678u, cp);
    NAOS_CHECK_TRUE(amark_utf8_validate(e.text, 0, &offset));
    NAOS_CHECK_UINT_EQ(0, offset);
    NAOS_CHECK_INT_EQ(0, exact_release(&e, ""));
}

NAOS_TEST(null_arguments_do_not_crash)
{
    uint32_t cp = 0x12345678u;
    size_t offset = 99;

    NAOS_CHECK_UINT_EQ(0, amark_utf8_decode(NULL, 0, &cp));
    NAOS_CHECK_UINT_EQ(0, amark_utf8_decode(NULL, 5, &cp));
    NAOS_CHECK_UINT_EQ(0, amark_utf8_decode_before(NULL, 0, &cp));
    NAOS_CHECK_UINT_EQ(0, amark_utf8_decode_before(NULL, 5, &cp));
    NAOS_CHECK_UINT_EQ(0x12345678u, cp);
    NAOS_CHECK_UINT_EQ(1, amark_utf8_decode("a", 1, NULL));
    NAOS_CHECK_UINT_EQ(1, amark_utf8_decode_before("a", 1, NULL));
    NAOS_CHECK_TRUE(amark_utf8_validate(NULL, 0, &offset));
    NAOS_CHECK_UINT_EQ(0, offset);
    offset = 99;
    NAOS_CHECK_FALSE(amark_utf8_validate(NULL, 5, &offset));
    NAOS_CHECK_UINT_EQ(0, offset);
    NAOS_CHECK_UINT_EQ(0, amark_utf8_encode(0x41, NULL));
}

NAOS_TEST(embedded_nul_is_an_ordinary_character)
{
    CHECK_DECODING("a\x00" "b", {0x61, 1}, {0x00, 1}, {0x62, 1});
    CHECK_DECODING("\x00\x00", {0x00, 1}, {0x00, 1});
    CHECK_DECODING("\xC2\x00", {R, 1}, {0x00, 1});
    CHECK_DECODING("\xE2\x82\x00\xC3\xA9", {R, 2}, {0x00, 1}, {0xE9, 2});
    CHECK_DECODING("\x00\xF0\x9F\x98\x80\x00", {0x00, 1}, {0x1F600, 4}, {0x00, 1});
    CHECK_VALID("a\x00" "b");
    CHECK_VALID("\x00");
    CHECK_INVALID_AT("a\x00\x80", 2);
}

NAOS_SUITE(utf8,
    NAOS_ADD_TEST(constants),
    NAOS_ADD_TEST(decodes_one_byte_boundaries),
    NAOS_ADD_TEST(decodes_two_byte_boundaries),
    NAOS_ADD_TEST(decodes_three_byte_boundaries),
    NAOS_ADD_TEST(decodes_four_byte_boundaries),
    NAOS_ADD_TEST(decodes_a_mixed_sequence),
    NAOS_ADD_TEST(decodes_only_the_first_character),
    NAOS_ADD_TEST(overlong_forms_are_replaced_byte_by_byte),
    NAOS_ADD_TEST(surrogates_are_replaced_byte_by_byte),
    NAOS_ADD_TEST(values_above_10ffff_are_replaced),
    NAOS_ADD_TEST(invalid_lead_bytes_are_replaced_alone),
    NAOS_ADD_TEST(truncated_sequences_are_replaced_as_one),
    NAOS_ADD_TEST(lone_continuation_bytes_are_replaced_alone),
    NAOS_ADD_TEST(follows_the_unicode_examples),
    NAOS_ADD_TEST(length_limits_the_sequence),
    NAOS_ADD_TEST(decode_before_decodes_the_prefix),
    NAOS_ADD_TEST(decode_before_looks_back_at_most_three_continuation_bytes),
    NAOS_ADD_TEST(decode_before_at_the_start_of_the_input),
    NAOS_ADD_TEST(short_inputs_keep_every_property),
    NAOS_ADD_TEST(all_two_byte_inputs_keep_every_property),
    NAOS_ADD_TEST(validate_accepts_well_formed_input),
    NAOS_ADD_TEST(validate_reports_the_first_invalid_offset),
    NAOS_ADD_TEST(validate_accepts_a_null_offset),
    NAOS_ADD_TEST(encodes_the_boundaries),
    NAOS_ADD_TEST(encodes_invalid_values_as_the_replacement),
    NAOS_ADD_TEST(encode_writes_only_its_bytes),
    NAOS_ADD_TEST(every_code_point_round_trips),
    NAOS_ADD_TEST(empty_input_decodes_nothing),
    NAOS_ADD_TEST(null_arguments_do_not_crash),
    NAOS_ADD_TEST(embedded_nul_is_an_ordinary_character)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(utf8)
)
