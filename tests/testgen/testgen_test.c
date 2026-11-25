/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The self-test of the test generation (4.7, 4.8): the hand-written suite of
 * testgen_test.
 *
 * gen_bytes.cmake turns data/bytes.bin into the suite testgen_test_bytes, which
 * holds the lines and the whole file as byte arrays and gives them through the
 * functions declared below. This suite checks them byte for byte against the
 * content of data/bytes.bin written here as C literals, independently of CMake:
 * NUL, CR (at the end of a line and inside it), an empty line, invalid UTF-8,
 * ";", "[" and "]", and a last line that does not end with LF.
 *
 * Keep the literals in step with data/bytes.bin.
 */
#include <stddef.h>

#include <naos/naos.h>

#include "test_check.h"

/* Defined in the source generated from data/bytes.bin. */
size_t testgen_test_bytes_line_count(void);
const unsigned char *testgen_test_bytes_line(size_t index, size_t *size);
const unsigned char *testgen_test_bytes_file(size_t *size);

/* Checks that line index (from 0) of the generated source is expected_literal. */
#define CHECK_LINE(index, expected_literal) \
    do { \
        size_t size_ = 99; \
        const unsigned char *line_ = testgen_test_bytes_line((index), &size_); \
        NAOS_REQUIRE_NOT_NULL(line_); \
        AMARK_CHECK_BYTES_EQ(line_, size_, expected_literal); \
    } while (0)

NAOS_TEST(data_has_nine_lines)
{
    NAOS_CHECK_UINT_EQ(9, testgen_test_bytes_line_count());
}

NAOS_TEST(plain_line)
{
    CHECK_LINE(0, "plain ascii line");
}

NAOS_TEST(nul_is_kept)
{
    CHECK_LINE(1, "nul:\0:end");
}

NAOS_TEST(cr_is_kept)
{
    /* LF ends a line, a CR before it stays in the line. */
    CHECK_LINE(2, "cr at end\r");
    CHECK_LINE(3, "cr\rinside");
}

NAOS_TEST(empty_line_has_length_zero)
{
    CHECK_LINE(4, "");
}

NAOS_TEST(invalid_utf8_is_kept)
{
    CHECK_LINE(5, "invalid utf-8: \xC0\x80 \xFF \xED\xA0\x80 \xF4\x90\x80\x80 truncated \xE3\x81");
}

NAOS_TEST(list_and_bracket_characters_are_kept)
{
    /* ";" separates CMake list elements, "[" and "]" can group them. */
    CHECK_LINE(6, "semicolons ;; and ; brackets [ ] [[ ]] [=[ ]=]");
    CHECK_LINE(7, "backslash \\ quote \" hash # dollar ${x} @x@ valid \xE6\x97\xA5\xE6\x9C\xAC");
}

NAOS_TEST(last_line_without_lf_is_kept)
{
    CHECK_LINE(8, "last line without LF");
}

NAOS_TEST(whole_file_is_kept)
{
    size_t size = 0;
    const unsigned char *file = testgen_test_bytes_file(&size);

    NAOS_REQUIRE_NOT_NULL(file);
    AMARK_CHECK_BYTES_EQ(file, size,
        "plain ascii line\n"
        "nul:\0:end\n"
        "cr at end\r\n"
        "cr\rinside\n"
        "\n"
        "invalid utf-8: \xC0\x80 \xFF \xED\xA0\x80 \xF4\x90\x80\x80 truncated \xE3\x81\n"
        "semicolons ;; and ; brackets [ ] [[ ]] [=[ ]=]\n"
        "backslash \\ quote \" hash # dollar ${x} @x@ valid \xE6\x97\xA5\xE6\x9C\xAC\n"
        "last line without LF");
}

NAOS_TEST(out_of_range_line_is_null)
{
    size_t size = 99;

    NAOS_CHECK_NULL(testgen_test_bytes_line(9, &size));
    NAOS_CHECK_UINT_EQ(0, size);
}

NAOS_SUITE(testgen_expected,
    NAOS_ADD_TEST(data_has_nine_lines),
    NAOS_ADD_TEST(plain_line),
    NAOS_ADD_TEST(nul_is_kept),
    NAOS_ADD_TEST(cr_is_kept),
    NAOS_ADD_TEST(empty_line_has_length_zero),
    NAOS_ADD_TEST(invalid_utf8_is_kept),
    NAOS_ADD_TEST(list_and_bracket_characters_are_kept),
    NAOS_ADD_TEST(last_line_without_lf_is_kept),
    NAOS_ADD_TEST(whole_file_is_kept),
    NAOS_ADD_TEST(out_of_range_line_is_null)
)
