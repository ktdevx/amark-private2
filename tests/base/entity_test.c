/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The tests of the decoding of character references (3.3):
 * amark_char_ref_decode follows the entity and numeric character references of
 * CommonMark 0.31.2 and the named references of the WHATWG entities.json.
 */
#include <amark/entity.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <naos/naos.h>

#define MARKER 0xFFFFFFFFu

/* Decodes text and checks that it succeeds with expected[0..count). The output
 * slots past the result are filled with a marker first, so a test also sees
 * that the function writes no more than it reports. */
static void check_decodes(const char *text, const uint32_t *expected, size_t count)
{
    uint32_t out[2] = {MARKER, MARKER};
    size_t got = 99;
    size_t i;

    NAOS_REQUIRE_TRUE(amark_char_ref_decode(text, strlen(text), out, &got));
    NAOS_REQUIRE_UINT_EQ(count, got);
    for (i = 0; i < count; i++)
    {
        NAOS_CHECK_UINT_EQ(expected[i], out[i]);
    }
    for (; i < 2; i++)
    {
        NAOS_CHECK_UINT_EQ(MARKER, out[i]);
    }
}

static void check_decodes_one(const char *text, uint32_t expected)
{
    check_decodes(text, &expected, 1);
}

/* Checks that text of length bytes is rejected without writing the outputs. */
static void check_rejects_n(const char *text, size_t length)
{
    uint32_t out[2] = {MARKER, MARKER};
    size_t got = 99;

    NAOS_CHECK_FALSE(amark_char_ref_decode(text, length, out, &got));
    NAOS_CHECK_UINT_EQ(MARKER, out[0]);
    NAOS_CHECK_UINT_EQ(MARKER, out[1]);
    NAOS_CHECK_UINT_EQ(99, got);
}

static void check_rejects(const char *text)
{
    check_rejects_n(text, strlen(text));
}

NAOS_TEST(named_single_code_point)
{
    check_decodes_one("&amp;", 0x26u);
    check_decodes_one("&lt;", 0x3Cu);
    check_decodes_one("&nbsp;", 0xA0u);
    check_decodes_one("&copy;", 0xA9u);
    check_decodes_one("&AElig;", 0xC6u);
    check_decodes_one("&Dcaron;", 0x10Eu);
    check_decodes_one("&frac34;", 0xBEu);
    check_decodes_one("&HilbertSpace;", 0x210Bu);
    check_decodes_one("&DifferentialD;", 0x2146u);
    check_decodes_one("&ClockwiseContourIntegral;", 0x2232u);
}

NAOS_TEST(named_beyond_bmp)
{
    check_decodes_one("&Afr;", 0x1D504u);
    check_decodes_one("&zopf;", 0x1D56Bu);
}

NAOS_TEST(named_two_code_points)
{
    static const uint32_t not_equal_tilde[2] = {0x2242u, 0x0338u};
    static const uint32_t nvlt[2] = {0x003Cu, 0x20D2u};
    static const uint32_t thick_space[2] = {0x205Fu, 0x200Au};

    check_decodes("&NotEqualTilde;", not_equal_tilde, 2);
    check_decodes("&nvlt;", nvlt, 2);
    check_decodes("&ThickSpace;", thick_space, 2);
}

NAOS_TEST(named_first_and_last_of_table)
{
    /* The smallest and the largest names in byte order. */
    check_decodes_one("&AElig;", 0xC6u);
    check_decodes_one("&zwnj;", 0x200Cu);
}

NAOS_TEST(named_is_case_sensitive)
{
    check_decodes_one("&Gt;", 0x226Bu);
    check_decodes_one("&gt;", 0x3Eu);
    /* "&GT;" and "&AMP;" are names of their own; "&gT;" and "&aMp;" are not. */
    check_decodes_one("&GT;", 0x3Eu);
    check_decodes_one("&AMP;", 0x26u);
    check_rejects("&gT;");
    check_rejects("&aMp;");
    check_rejects("&NOTEQUALTILDE;");
}

NAOS_TEST(named_unknown_is_rejected)
{
    check_rejects("&nosuchentity;");
    check_rejects("&MadeUpEntity;");
    check_rejects("&x;");
    /* A prefix or an extension of a name is not a name. */
    check_rejects("&am;");
    check_rejects("&ampx;");
    check_rejects("&NotEqualTild;");
    /* Longer than any name of the table. */
    check_rejects("&CounterClockwiseContourIntegralXXXXXXXXXXXXXX;");
}

NAOS_TEST(named_without_semicolon_in_table_is_rejected)
{
    /* entities.json also lists "&amp" and "&copy" without the semicolon, but
     * CommonMark recognizes only the names that end in one. */
    check_rejects("&amp");
    check_rejects("&copy");
}

NAOS_TEST(decimal)
{
    check_decodes_one("&#35;", 0x23u);
    check_decodes_one("&#1234;", 0x4D2u);
    check_decodes_one("&#992;", 0x3E0u);
    check_decodes_one("&#9;", 0x09u);
    check_decodes_one("&#0065;", 0x41u);
    check_decodes_one("&#1114111;", 0x10FFFFu);
    /* Seven digits, the most CommonMark allows. */
    check_decodes_one("&#0000065;", 0x41u);
}

NAOS_TEST(hexadecimal)
{
    check_decodes_one("&#x22;", 0x22u);
    check_decodes_one("&#XD06;", 0xD06u);
    check_decodes_one("&#xcab;", 0xCABu);
    check_decodes_one("&#xCaB;", 0xCABu);
    check_decodes_one("&#x1D504;", 0x1D504u);
    check_decodes_one("&#x10FFFF;", 0x10FFFFu);
    /* Six digits, the most CommonMark allows. */
    check_decodes_one("&#x000041;", 0x41u);
}

NAOS_TEST(numeric_zero_is_replacement)
{
    check_decodes_one("&#0;", 0xFFFDu);
    check_decodes_one("&#0000000;", 0xFFFDu);
    check_decodes_one("&#x0;", 0xFFFDu);
    check_decodes_one("&#X000000;", 0xFFFDu);
}

NAOS_TEST(numeric_surrogate_is_replacement)
{
    check_decodes_one("&#xD800;", 0xFFFDu);
    check_decodes_one("&#xDBFF;", 0xFFFDu);
    check_decodes_one("&#xDFFF;", 0xFFFDu);
    check_decodes_one("&#55296;", 0xFFFDu);
    check_decodes_one("&#57343;", 0xFFFDu);
    /* The neighbours of the surrogate range are kept. */
    check_decodes_one("&#xD7FF;", 0xD7FFu);
    check_decodes_one("&#xE000;", 0xE000u);
}

NAOS_TEST(numeric_out_of_range_is_replacement)
{
    check_decodes_one("&#x110000;", 0xFFFDu);
    check_decodes_one("&#xFFFFFF;", 0xFFFDu);
    check_decodes_one("&#1114112;", 0xFFFDu);
    check_decodes_one("&#9999999;", 0xFFFDu);
}

NAOS_TEST(numeric_too_many_digits_is_rejected)
{
    check_rejects("&#12345678;");
    check_rejects("&#00000000;");
    check_rejects("&#x1234567;");
    check_rejects("&#x0000000;");
}

NAOS_TEST(numeric_malformed_is_rejected)
{
    check_rejects("&#;");
    check_rejects("&#x;");
    check_rejects("&#X;");
    check_rejects("&#-1;");
    check_rejects("&#+1;");
    check_rejects("&# 1;");
    check_rejects("&#1 ;");
    check_rejects("&#12a;");
    check_rejects("&#xG;");
    check_rejects("&#x12g;");
    check_rejects("&#abcdef;");
    check_rejects("&#xx1;");
    check_rejects("&#x-1;");
    check_rejects("&#1");
    check_rejects("&#x41");
}

NAOS_TEST(malformed_is_rejected)
{
    check_rejects("");
    check_rejects("&");
    check_rejects(";");
    check_rejects("&;");
    check_rejects("&#");
    check_rejects("amp;");
    check_rejects("#35;");
    check_rejects(" &amp;");
    check_rejects("&amp; ");
    check_rejects("&amp;;");
    check_rejects("&&amp;");
    check_rejects("&am p;");
    check_rejects("&1;");
    check_rejects("&a\xC3\xA9;");
    check_rejects("&\xC3\xA9;");
    check_rejects_n("&lt\0;", 5);
    check_rejects_n("&#1\0;", 5);
}

NAOS_TEST(length_bounds_the_text)
{
    /* Only the first length bytes are read: the text need not be terminated,
     * and bytes past length do not take part. */
    static const char text[] = {'&', 'l', 't', ';', '&', 'g', 't', ';'};
    static const char amp_prefix[] = "&amp;";

    check_decodes_one("&lt;", 0x3Cu);
    {
        uint32_t out[2] = {MARKER, MARKER};
        size_t got = 0;

        NAOS_REQUIRE_TRUE(amark_char_ref_decode(text, 4, out, &got));
        NAOS_CHECK_UINT_EQ(1, got);
        NAOS_CHECK_UINT_EQ(0x3Cu, out[0]);
        NAOS_REQUIRE_TRUE(amark_char_ref_decode(text + 4, 4, out, &got));
        NAOS_CHECK_UINT_EQ(0x3Eu, out[0]);
    }
    check_rejects_n(amp_prefix, 4);
    check_rejects_n(amp_prefix, 0);
    check_rejects_n(NULL, 0);
}

NAOS_SUITE(entity,
    NAOS_ADD_TEST(named_single_code_point),
    NAOS_ADD_TEST(named_beyond_bmp),
    NAOS_ADD_TEST(named_two_code_points),
    NAOS_ADD_TEST(named_first_and_last_of_table),
    NAOS_ADD_TEST(named_is_case_sensitive),
    NAOS_ADD_TEST(named_unknown_is_rejected),
    NAOS_ADD_TEST(named_without_semicolon_in_table_is_rejected),
    NAOS_ADD_TEST(decimal),
    NAOS_ADD_TEST(hexadecimal),
    NAOS_ADD_TEST(numeric_zero_is_replacement),
    NAOS_ADD_TEST(numeric_surrogate_is_replacement),
    NAOS_ADD_TEST(numeric_out_of_range_is_replacement),
    NAOS_ADD_TEST(numeric_too_many_digits_is_rejected),
    NAOS_ADD_TEST(numeric_malformed_is_rejected),
    NAOS_ADD_TEST(malformed_is_rejected),
    NAOS_ADD_TEST(length_bounds_the_text)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(entity)
)
