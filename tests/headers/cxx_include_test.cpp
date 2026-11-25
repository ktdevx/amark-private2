/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The public API from C++ (4.9, 9.1, 9.2): this C++11 source includes only the
 * umbrella header and calls a function of every public header, so the build fails
 * if a header does not compile as C++ or a function does not link with its C name.
 *
 * Tests that can stop hold no local object with a destructor (Naos leaves a test
 * with longjmp).
 */
#include <amark/amark.h>

#include <naos/naos.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

NAOS_TEST(version_matches_header)
{
    NAOS_CHECK_INT_EQ(AMARK_VERSION_NUMBER, amark_version_number());
    NAOS_REQUIRE_NOT_NULL(amark_version_string());
    NAOS_CHECK_STR_EQ(AMARK_VERSION_STRING, amark_version_string());
}

NAOS_TEST(error_string_describes_codes)
{
    amark_error_t error = AMARK_ERROR_OUT_OF_MEMORY;

    NAOS_CHECK_STR_EQ("no error", amark_error_string(AMARK_ERROR_NONE));
    NAOS_CHECK_STR_EQ("out of memory", amark_error_string(error));
    NAOS_CHECK_STR_EQ("unrepresentable", amark_error_string(AMARK_ERROR_UNREPRESENTABLE));
}

NAOS_TEST(buffer_appends_and_detaches)
{
    amark_buffer_t *buffer = NULL;
    char *data = NULL;
    size_t length = 0;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_create(&buffer));
    NAOS_REQUIRE_NOT_NULL(buffer);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_cstr(buffer, "C++"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_byte(buffer, ' '));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append(buffer, "ok", 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_codepoint(buffer, 0x00E9u));
    NAOS_CHECK_UINT_EQ(8, amark_buffer_length(buffer));
    NAOS_CHECK_INT_EQ(0, std::memcmp(amark_buffer_data(buffer), "C++ ok\xC3\xA9", 9));

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_detach(buffer, &data, &length));
    amark_buffer_destroy(buffer);
    NAOS_REQUIRE_NOT_NULL(data);
    NAOS_CHECK_UINT_EQ(8, length);
    NAOS_CHECK_STR_EQ("C++ ok\xC3\xA9", data);

    amark_free(data);
}

NAOS_TEST(allocator_functions_link)
{
    void *block = amark_malloc(16);

    NAOS_CHECK_NOT_NULL(block);
    amark_free(block);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_allocator_set(NULL));
}

NAOS_TEST(utf8_and_unicode_functions_link)
{
    char out[AMARK_UTF8_MAX_LENGTH];
    uint32_t codepoint = 0;

    NAOS_CHECK_UINT_EQ(2, amark_utf8_encode(0x00E9u, out));
    NAOS_CHECK_UINT_EQ(2, amark_utf8_decode(out, 2, &codepoint));
    NAOS_CHECK_UINT_EQ(0x00E9u, codepoint);
    NAOS_CHECK_TRUE(amark_utf8_validate(out, 2, NULL));

    NAOS_CHECK_TRUE(amark_unicode_is_whitespace(0x3000u));
    NAOS_CHECK_TRUE(amark_unicode_is_punctuation(0x00A1u));
    NAOS_CHECK_TRUE(amark_ascii_is_punctuation('!'));
}

NAOS_SUITE(cxx_include,
    NAOS_ADD_TEST(version_matches_header),
    NAOS_ADD_TEST(error_string_describes_codes),
    NAOS_ADD_TEST(buffer_appends_and_detaches),
    NAOS_ADD_TEST(allocator_functions_link),
    NAOS_ADD_TEST(utf8_and_unicode_functions_link)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(cxx_include)
)
