/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The tests of the Unicode case fold (9.1): amark_unicode_case_fold follows the
 * statuses C and F of CaseFolding.txt of Unicode 17.0.0.
 */
#include <amark/unicode.h>

#include <stddef.h>
#include <stdint.h>

#include <naos/naos.h>

/* Folds codepoint and checks the result against expected[0..count). The output
 * slots past the result are filled with a marker first, so a test also sees
 * that the function writes no more than it reports. */
static void check_fold(uint32_t codepoint, const uint32_t *expected, size_t count)
{
    uint32_t out[3] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
    size_t i;

    NAOS_REQUIRE_UINT_EQ(count, amark_unicode_case_fold(codepoint, out));
    for (i = 0; i < count; i++)
    {
        NAOS_CHECK_UINT_EQ(expected[i], out[i]);
    }
    for (; i < 3; i++)
    {
        NAOS_CHECK_UINT_EQ(0xFFFFFFFFu, out[i]);
    }
}

NAOS_TEST(ascii_upper_folds_to_lower)
{
    uint32_t c;

    for (c = 'A'; c <= 'Z'; c++)
    {
        uint32_t lower = c + 0x20u;

        check_fold(c, &lower, 1);
    }
}

NAOS_TEST(ascii_other_folds_to_itself)
{
    uint32_t c;

    for (c = 0; c < 0x80u; c++)
    {
        if ((c >= 'A') && (c <= 'Z'))
        {
            continue;
        }
        check_fold(c, &c, 1);
    }
}

NAOS_TEST(sharp_s_folds_to_ss)
{
    static const uint32_t ss[] = {0x73u, 0x73u};

    /* U+00DF LATIN SMALL LETTER SHARP S: status F. */
    check_fold(0x00DFu, ss, 2);
}

NAOS_TEST(capital_sharp_s_folds_to_ss)
{
    static const uint32_t ss[] = {0x73u, 0x73u};

    /* U+1E9E LATIN CAPITAL LETTER SHARP S: status F (the S fold to U+00DF is
     * not used). */
    check_fold(0x1E9Eu, ss, 2);
}

NAOS_TEST(greek_final_sigma_folds_to_sigma)
{
    static const uint32_t sigma = 0x03C3u;

    /* U+03C2 GREEK SMALL LETTER FINAL SIGMA and U+03A3 GREEK CAPITAL LETTER
     * SIGMA both fold to U+03C3. */
    check_fold(0x03C2u, &sigma, 1);
    check_fold(0x03A3u, &sigma, 1);
    check_fold(0x03C3u, &sigma, 1);
}

NAOS_TEST(three_code_point_fold)
{
    static const uint32_t expected[] = {0x03B9u, 0x0308u, 0x0301u};

    /* U+0390 GREEK SMALL LETTER IOTA WITH DIALYTIKA AND TONOS: status F. */
    check_fold(0x0390u, expected, 3);
}

NAOS_TEST(simple_fold_outside_ascii)
{
    static const uint32_t e_acute = 0x00E9u;
    static const uint32_t deseret = 0x10428u;

    check_fold(0x00C9u, &e_acute, 1);
    /* U+10400 DESERET CAPITAL LETTER LONG I: beyond the BMP. */
    check_fold(0x10400u, &deseret, 1);
}

NAOS_TEST(code_point_not_in_table_folds_to_itself)
{
    static const uint32_t not_in_table[] = {
        0x00E9u, 0x3042u, 0x4E00u, 0x1F600u, 0x10FFFFu, 0xD800u, 0x110000u, 0xFFFFFFFFu,
    };
    size_t i;

    for (i = 0; i < sizeof(not_in_table) / sizeof(not_in_table[0]); i++)
    {
        check_fold(not_in_table[i], &not_in_table[i], 1);
    }
}

NAOS_SUITE(case_fold,
    NAOS_ADD_TEST(ascii_upper_folds_to_lower),
    NAOS_ADD_TEST(ascii_other_folds_to_itself),
    NAOS_ADD_TEST(sharp_s_folds_to_ss),
    NAOS_ADD_TEST(capital_sharp_s_folds_to_ss),
    NAOS_ADD_TEST(greek_final_sigma_folds_to_sigma),
    NAOS_ADD_TEST(three_code_point_fold),
    NAOS_ADD_TEST(simple_fold_outside_ascii),
    NAOS_ADD_TEST(code_point_not_in_table_folds_to_itself)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(case_fold)
)
