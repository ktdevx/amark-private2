/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The tests of DefinitionIndex (syntax-tree): the label normalization (9.1),
 * which strips the leading and trailing whitespace of CommonMark labels (space,
 * tab, LF, CR), collapses each internal run of it to one space and applies the
 * full case fold (amark_unicode_case_fold) to every code point; the other
 * Unicode spaces (a full-width space, a no-break space) and the form feed are
 * ordinary characters. The result is appended to the output buffer, which keeps
 * its content on failure.
 */
#include "tree/tree.h"

#include <stddef.h>
#include <string.h>

#include <naos/naos.h>

#include "test_allocator.h"

/* ---- Helpers ------------------------------------------------------------------ */

/* Normalizes the NUL-terminated label into a new buffer and returns whether the
 * result is expected (also NUL-terminated). */
static int normalizes_to(const char *label, const char *expected)
{
    amark_buffer_t *out = NULL;
    int same;

    if (amark_buffer_create(&out) != AMARK_ERROR_NONE)
    {
        return 0;
    }
    same = (amark_label_normalize(label, strlen(label), out) == AMARK_ERROR_NONE) &&
           (amark_buffer_length(out) == strlen(expected)) &&
           (memcmp(amark_buffer_data(out), expected, strlen(expected)) == 0);
    amark_buffer_destroy(out);
    return same;
}

/* ---- Whitespace ----------------------------------------------------------------- */

NAOS_TEST(ascii_labels_are_folded_and_their_spaces_collapsed)
{
    NAOS_CHECK_TRUE(normalizes_to("Foo", "foo"));
    NAOS_CHECK_TRUE(normalizes_to("  Foo   BAR  ", "foo bar"));
    NAOS_CHECK_TRUE(normalizes_to("\tfoo\t \tbar\t", "foo bar"));
    NAOS_CHECK_TRUE(normalizes_to("a b c", "a b c"));
    NAOS_CHECK_TRUE(normalizes_to("[Foo*bar\\]]", "[foo*bar\\]]"));
}

NAOS_TEST(full_width_space_and_no_break_space_are_ordinary_characters)
{
    /* U+3000 IDEOGRAPHIC SPACE around and between the words: kept as is. */
    NAOS_CHECK_TRUE(normalizes_to("\xE3\x80\x80" "Foo" "\xE3\x80\x80\xE3\x80\x80" "Bar"
                                  "\xE3\x80\x80",
                                  "\xE3\x80\x80" "foo" "\xE3\x80\x80\xE3\x80\x80" "bar"
                                  "\xE3\x80\x80"));
    /* The ASCII spaces around a full-width space are stripped; it stays. */
    NAOS_CHECK_TRUE(normalizes_to(" \xE3\x80\x80 ", "\xE3\x80\x80"));
    /* U+00A0 NO-BREAK SPACE and U+2003 EM SPACE are kept; the runs of ASCII
     * spaces between them each become one space. */
    NAOS_CHECK_TRUE(normalizes_to("A  \xC2\xA0\t\xE2\x80\x83  B", "a \xC2\xA0 \xE2\x80\x83 b"));
    /* A label of full-width letters: U+FF21 FULLWIDTH A folds to U+FF41. */
    NAOS_CHECK_TRUE(normalizes_to("\xEF\xBC\xA1\xE3\x80\x80\xEF\xBC\xA2",
                                  "\xEF\xBD\x81\xE3\x80\x80\xEF\xBD\x82"));
}

NAOS_TEST(line_breaks_are_whitespace)
{
    NAOS_CHECK_TRUE(normalizes_to("foo\nbar", "foo bar"));
    NAOS_CHECK_TRUE(normalizes_to("foo\r\nbar", "foo bar"));
    NAOS_CHECK_TRUE(normalizes_to("foo\rbar", "foo bar"));
    NAOS_CHECK_TRUE(normalizes_to("foo \n\t  Bar", "foo bar"));
    NAOS_CHECK_TRUE(normalizes_to("\nFoo\r\n", "foo"));
    NAOS_CHECK_TRUE(normalizes_to("a\r\r\nb", "a b"));
    NAOS_CHECK_TRUE(normalizes_to("\r\n\r\nFoo \r\n\t bar\n\r", "foo bar"));
    NAOS_CHECK_TRUE(normalizes_to("foo\n\nbar", "foo bar"));
}

NAOS_TEST(characters_outside_the_whitespace_definition_are_kept)
{
    /* U+000C (form feed), U+000B (vertical tab), U+0085 (next line) and
     * U+200B (zero width space) are not whitespace of a label. */
    NAOS_CHECK_TRUE(normalizes_to("a\fb", "a\fb"));
    NAOS_CHECK_TRUE(normalizes_to(" \f ", "\f"));
    NAOS_CHECK_TRUE(normalizes_to("a\vb", "a\vb"));
    NAOS_CHECK_TRUE(normalizes_to("\xC2\x85" "a", "\xC2\x85" "a"));
    NAOS_CHECK_TRUE(normalizes_to("\xE2\x80\x8B" "a", "\xE2\x80\x8B" "a"));
}

NAOS_TEST(a_label_of_whitespace_only_normalizes_to_empty)
{
    amark_buffer_t *out = NULL;

    NAOS_CHECK_TRUE(normalizes_to("", ""));
    NAOS_CHECK_TRUE(normalizes_to(" ", ""));
    NAOS_CHECK_TRUE(normalizes_to(" \t\r\n \r\n\t ", ""));

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_create(&out));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_label_normalize(NULL, 0, out));
    NAOS_CHECK_UINT_EQ(0, amark_buffer_length(out));
    amark_buffer_destroy(out);
}

/* ---- Case fold ------------------------------------------------------------------ */

NAOS_TEST(sharp_s_and_capital_sharp_s_fold_to_ss)
{
    NAOS_CHECK_TRUE(normalizes_to("\xC3\x9F", "ss"));         /* U+00DF */
    NAOS_CHECK_TRUE(normalizes_to("\xE1\xBA\x9E", "ss"));     /* U+1E9E */
    NAOS_CHECK_TRUE(normalizes_to("Stra\xC3\x9F" "e", "strasse"));
    NAOS_CHECK_TRUE(normalizes_to("STRA\xE1\xBA\x9E" "E", "strasse"));
    NAOS_CHECK_TRUE(normalizes_to("STRASSE", "strasse"));
}

NAOS_TEST(folds_to_several_code_points_are_written_in_full)
{
    /* U+0130 LATIN CAPITAL LETTER I WITH DOT ABOVE -> U+0069 U+0307. */
    NAOS_CHECK_TRUE(normalizes_to("\xC4\xB0", "i\xCC\x87"));
    /* U+FB03 LATIN SMALL LIGATURE FFI -> "ffi". */
    NAOS_CHECK_TRUE(normalizes_to("o\xEF\xAC\x83" "ce", "office"));
    /* U+0390 -> U+03B9 U+0308 U+0301 (three code points). */
    NAOS_CHECK_TRUE(normalizes_to("\xCE\x90", "\xCE\xB9\xCC\x88\xCC\x81"));
    /* U+0149 -> U+02BC U+006E, between whitespace. */
    NAOS_CHECK_TRUE(normalizes_to(" \xC5\x89  \xC5\x89 ", "\xCA\xBC" "n \xCA\xBC" "n"));
}

NAOS_TEST(final_sigma_and_sigma_fold_alike)
{
    /* U+0391 U+03A3 and U+03B1 U+03C2 both give U+03B1 U+03C3. */
    NAOS_CHECK_TRUE(normalizes_to("\xCE\x91\xCE\xA3", "\xCE\xB1\xCF\x83"));
    NAOS_CHECK_TRUE(normalizes_to("\xCE\xB1\xCF\x82", "\xCE\xB1\xCF\x83"));
}

NAOS_TEST(invalid_utf8_becomes_replacement_and_nul_is_kept)
{
    static const char label[] = {'A', '\0', ' ', '\xFF', ' ', '\xE3', '\x80'};
    static const char expected[] = {'a', '\0', ' ', '\xEF', '\xBF', '\xBD', ' ',
                                    '\xEF', '\xBF', '\xBD'};
    amark_buffer_t *out = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_create(&out));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_label_normalize(label, sizeof(label), out));
    NAOS_CHECK_UINT_EQ(sizeof(expected), amark_buffer_length(out));
    NAOS_CHECK_TRUE(memcmp(amark_buffer_data(out), expected, sizeof(expected)) == 0);
    amark_buffer_destroy(out);
}

/* ---- The output ----------------------------------------------------------------- */

NAOS_TEST(the_result_is_appended_to_the_buffer)
{
    amark_buffer_t *out = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_create(&out));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_cstr(out, "key:"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_label_normalize(" A  B ", 6, out));
    NAOS_CHECK_STR_EQ("key:a b", amark_buffer_data(out));
    /* Only length bytes are read. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_label_normalize("CdEF", 2, out));
    NAOS_CHECK_STR_EQ("key:a bcd", amark_buffer_data(out));
    amark_buffer_destroy(out);
}

NAOS_TEST(invalid_arguments_leave_the_buffer_unchanged)
{
    amark_buffer_t *out = NULL;

    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_label_normalize("a", 1, NULL));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_create(&out));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_cstr(out, "x"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_label_normalize(NULL, 1, out));
    NAOS_CHECK_STR_EQ("x", amark_buffer_data(out));
    amark_buffer_destroy(out);
}

/* Normalizes a long label whose fold is longer than itself (U+0130 takes two
 * bytes and folds to three) into a buffer that already holds "x", so that the
 * buffer grows both before and during the normalization: on failure the buffer
 * still holds "x". */
static amark_error_t normalize_oom_scenario(void *context)
{
    static const char unit[] = "\xE1\xBA\x9E \xC4\xB0\xC4\xB0\xC4\xB0\xC4\xB0\n";
    char label[64 * (sizeof(unit) - 1)];
    amark_buffer_t *out = NULL;
    amark_error_t error;
    size_t i;

    (void)context;
    for (i = 0; i < 64; i++)
    {
        memcpy(label + i * (sizeof(unit) - 1), unit, sizeof(unit) - 1);
    }
    error = amark_buffer_create(&out);
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_buffer_append_byte(out, 'x');
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_label_normalize(label, sizeof(label), out);
        if (error != AMARK_ERROR_NONE)
        {
            if ((amark_buffer_length(out) != 1) || (amark_buffer_data(out)[0] != 'x'))
            {
                error = AMARK_ERROR_INVALID_STATE;
            }
        }
        else if ((amark_buffer_length(out) != 1 + 64 * 16 - 1) ||
                 (memcmp(amark_buffer_data(out), "xss i\xCC\x87i\xCC\x87i\xCC\x87i\xCC\x87 ss",
                         19) != 0) ||
                 (amark_buffer_data(out)[1 + 64 * 16 - 2] != '\x87'))
        {
            error = AMARK_ERROR_INVALID_STATE;
        }
    }
    amark_buffer_destroy(out);
    return error;
}

NAOS_TEST(out_of_memory_leaves_the_buffer_unchanged)
{
    amark_test_oom_result_t result =
        amark_test_oom_sweep(amark_allocator_set, normalize_oom_scenario, NULL,
                             AMARK_TEST_OOM_MAX_ITERATIONS);

    NAOS_CHECK_TRUE(amark_test_oom_check(result, "normalize_oom_scenario", __FILE__, __LINE__));
    /* The creation, the "x", the reservation and a growth during the loop. */
    NAOS_CHECK_UINT_EQ(4, result.iterations);
}

NAOS_SUITE(label_normalize,
    NAOS_ADD_TEST(ascii_labels_are_folded_and_their_spaces_collapsed),
    NAOS_ADD_TEST(full_width_space_and_no_break_space_are_ordinary_characters),
    NAOS_ADD_TEST(line_breaks_are_whitespace),
    NAOS_ADD_TEST(characters_outside_the_whitespace_definition_are_kept),
    NAOS_ADD_TEST(a_label_of_whitespace_only_normalizes_to_empty),
    NAOS_ADD_TEST(sharp_s_and_capital_sharp_s_fold_to_ss),
    NAOS_ADD_TEST(folds_to_several_code_points_are_written_in_full),
    NAOS_ADD_TEST(final_sigma_and_sigma_fold_alike),
    NAOS_ADD_TEST(invalid_utf8_becomes_replacement_and_nul_is_kept),
    NAOS_ADD_TEST(the_result_is_appended_to_the_buffer),
    NAOS_ADD_TEST(invalid_arguments_leave_the_buffer_unchanged),
    NAOS_ADD_TEST(out_of_memory_leaves_the_buffer_unchanged)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(label_normalize)
)
