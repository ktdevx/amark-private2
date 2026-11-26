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

NAOS_TEST(syntax_set_functions_link)
{
    amark_syntax_set_t *set = NULL;
    amark_node_type_t type = 0;

    NAOS_REQUIRE_NOT_NULL(amark_syntax_set_core());
    NAOS_CHECK_TRUE(amark_syntax_set_is_frozen(amark_syntax_set_core()));

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_create(&set));
    NAOS_REQUIRE_NOT_NULL(set);
    NAOS_CHECK_FALSE(amark_syntax_set_is_frozen(set));
    NAOS_CHECK_UINT_EQ(1, amark_syntax_set_extension_count(set));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_freeze(set));
    NAOS_CHECK_TRUE(amark_syntax_set_is_frozen(set));

    /* The queries by name need a frozen set. */
    NAOS_CHECK_TRUE(amark_syntax_set_find_node_type(set, "paragraph", &type));
    NAOS_CHECK_UINT_EQ(AMARK_NODE_PARAGRAPH, type);

    amark_syntax_set_destroy(set);
}

NAOS_TEST(document_and_node_functions_link)
{
    amark_document_t *document = NULL;
    amark_node_t *paragraph = NULL;
    amark_node_t *text = NULL;
    amark_source_id_t source = 0;
    const char *literal = NULL;
    size_t length = 0;
    size_t line = 0;
    size_t column = 0;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_create(amark_syntax_set_core(), &document));
    NAOS_REQUIRE_NOT_NULL(document);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_add_source(document, "a\nb", 3, &source));
    NAOS_CHECK_UINT_EQ(1, amark_document_source_count(document));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_document_position(document, source, 2, &line, &column));
    NAOS_CHECK_UINT_EQ(2, line);
    NAOS_CHECK_UINT_EQ(1, column);

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(document, AMARK_NODE_PARAGRAPH, &paragraph));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, AMARK_NODE_TEXT, &text));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_literal(text, "C++", 3));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(paragraph, text));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_append_child(amark_document_root(document), paragraph));

    NAOS_CHECK_STR_EQ("paragraph", amark_node_type_name(paragraph));
    NAOS_CHECK_TRUE(amark_node_parent(text) == paragraph);
    NAOS_CHECK_TRUE(amark_node_is_block(paragraph));
    NAOS_CHECK_TRUE(amark_node_is_inline(text));
    literal = amark_node_literal(text, &length);
    NAOS_REQUIRE_NOT_NULL(literal);
    NAOS_CHECK_UINT_EQ(3, length);
    NAOS_CHECK_INT_EQ(0, std::memcmp(literal, "C++", 3));

    amark_document_destroy(document);
}

NAOS_TEST(token_functions_link)
{
    amark_document_t *document = NULL;
    amark_node_t *text = NULL;
    amark_token_t token;
    amark_token_t read;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_create(amark_syntax_set_core(), &document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, AMARK_NODE_TEXT, &text));

    std::memset(&token, 0, sizeof(token));
    token.struct_size = sizeof(token);
    token.kind = AMARK_TOKEN_TEXT;
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_add_source(document, "hi", 2, &token.source));
    token.length = 2;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_add_token(text, &token));
    NAOS_CHECK_UINT_EQ(1, amark_node_token_count(text));

    std::memset(&read, 0, sizeof(read));
    read.struct_size = sizeof(read);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_token_at(text, 0, &read));
    NAOS_CHECK_UINT_EQ(2, read.length);
    NAOS_CHECK_STR_EQ("text", amark_token_kind_name(read.kind));

    amark_node_destroy(text);
    amark_document_destroy(document);
}

NAOS_TEST(iterator_functions_link)
{
    amark_document_t *document = NULL;
    amark_node_t *paragraph = NULL;
    amark_iter_t *iter = NULL;
    amark_event_t event = AMARK_EVENT_NONE;
    int events = 0;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_create(amark_syntax_set_core(), &document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(document, AMARK_NODE_PARAGRAPH, &paragraph));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_append_child(amark_document_root(document), paragraph));

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_iter_create(amark_document_root(document), &iter));
    NAOS_REQUIRE_NOT_NULL(iter);
    NAOS_CHECK_INT_EQ(AMARK_EVENT_ENTER, amark_iter_next(iter));
    NAOS_CHECK_TRUE(amark_iter_node(iter) == amark_document_root(document));
    event = amark_iter_next(iter);
    while (event != AMARK_EVENT_DONE)
    {
        events++;
        event = amark_iter_next(iter);
    }

    /* (paragraph, ENTER), (paragraph, EXIT), (root, EXIT). */
    NAOS_CHECK_INT_EQ(3, events);

    amark_iter_destroy(iter);
    amark_document_destroy(document);
}

NAOS_TEST(dump_functions_link)
{
    amark_document_t *document = NULL;
    amark_node_t *text = NULL;
    amark_buffer_t *out = NULL;
    static const char expected[] = "(text \"C++\")\n";

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_create(amark_syntax_set_core(), &document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, AMARK_NODE_TEXT, &text));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_literal(text, "C++", 3));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_create(&out));

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_dump(text, 0, out));
    NAOS_CHECK_UINT_EQ(sizeof(expected) - 1, amark_buffer_length(out));
    NAOS_CHECK_INT_EQ(0, std::memcmp(amark_buffer_data(out), expected, sizeof(expected) - 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_dump_attr_int(NULL, "level", 1));

    amark_buffer_destroy(out);
    amark_node_destroy(text);
    amark_document_destroy(document);
}

NAOS_SUITE(cxx_include,
    NAOS_ADD_TEST(version_matches_header),
    NAOS_ADD_TEST(error_string_describes_codes),
    NAOS_ADD_TEST(buffer_appends_and_detaches),
    NAOS_ADD_TEST(allocator_functions_link),
    NAOS_ADD_TEST(utf8_and_unicode_functions_link),
    NAOS_ADD_TEST(syntax_set_functions_link),
    NAOS_ADD_TEST(document_and_node_functions_link),
    NAOS_ADD_TEST(token_functions_link),
    NAOS_ADD_TEST(iterator_functions_link),
    NAOS_ADD_TEST(dump_functions_link)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(cxx_include)
)
