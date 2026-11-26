/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The public API from C++ (4.9, 9.1, 9.2): this C++11 source includes only the
 * umbrella header and calls a function of every public header that declares one,
 * so the build fails if a header does not compile as C++ or a function does not
 * link with its C name.
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
    uint32_t folded[3] = {0, 0, 0};

    NAOS_CHECK_UINT_EQ(2, amark_utf8_encode(0x00E9u, out));
    NAOS_CHECK_UINT_EQ(2, amark_utf8_decode(out, 2, &codepoint));
    NAOS_CHECK_UINT_EQ(0x00E9u, codepoint);
    NAOS_CHECK_TRUE(amark_utf8_validate(out, 2, NULL));

    NAOS_CHECK_TRUE(amark_unicode_is_whitespace(0x3000u));
    NAOS_CHECK_TRUE(amark_unicode_is_punctuation(0x00A1u));
    NAOS_CHECK_TRUE(amark_ascii_is_punctuation('!'));

    NAOS_CHECK_UINT_EQ(2, amark_unicode_case_fold(0x00DFu, folded));
    NAOS_CHECK_UINT_EQ(0x73u, folded[0]);
    NAOS_CHECK_UINT_EQ(0x73u, folded[1]);
}

NAOS_TEST(entity_functions_link)
{
    uint32_t codepoints[2] = {0, 0};
    std::size_t count = 0;

    NAOS_CHECK_TRUE(amark_char_ref_decode("&NotEqualTilde;", 15, codepoints, &count));
    NAOS_CHECK_UINT_EQ(2, count);
    NAOS_CHECK_UINT_EQ(0x2242u, codepoints[0]);
    NAOS_CHECK_UINT_EQ(0x0338u, codepoints[1]);
}

static const amark_facet_desc_t cxx_facets[] = {
    {sizeof(amark_facet_desc_t), "title", AMARK_FACET_VALUE, AMARK_FACET_STRING, 0, 0, 0, NULL, 0,
     0, NULL, NULL, NULL, NULL}};
static const amark_node_type_desc_t cxx_type = {
    sizeof(amark_node_type_desc_t), "cxx.box", AMARK_NODE_LEVEL_BLOCK, AMARK_CONTENT_BLOCK,
    AMARK_CONTENT_BLOCK, AMARK_NODE_TYPE_MARGINS, cxx_facets, 1, 0,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL};
static const amark_node_type_desc_t *const cxx_types[] = {&cxx_type};
static const amark_slot_t cxx_slot = {sizeof(amark_slot_t), AMARK_SLOT_KIND_USER_FIRST};
static const amark_slot_t *const cxx_slots[] = {&cxx_slot};
static const amark_extension_t cxx_extension = {
    sizeof(amark_extension_t), "cxx.ext", cxx_types, 1, cxx_slots, 1, NULL};
static const amark_extension_t cxx_other = {
    sizeof(amark_extension_t), "cxx.other", NULL, 0, NULL, 0, NULL};

NAOS_TEST(registry_functions_link)
{
    amark_syntax_set_t *set = NULL;
    amark_syntax_set_t *copy = NULL;
    amark_node_type_t type = AMARK_NODE_NONE;
    const amark_extension_t *extension = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_create(&set));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &cxx_extension));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_insert_before(set, "cxx.ext", &cxx_other));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_replace(set, "cxx.other", &cxx_other));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_remove(set, "cxx.other"));
    NAOS_CHECK_UINT_EQ(2, amark_syntax_set_extension_count(set));
    NAOS_CHECK_PTR_EQ(&cxx_extension, amark_syntax_set_extension_at(set, 1));
    NAOS_CHECK_PTR_EQ(&cxx_extension, amark_syntax_set_find_extension(set, "cxx.ext"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_freeze(set));
    NAOS_CHECK_TRUE(amark_syntax_set_is_frozen(set));

    NAOS_CHECK_TRUE(amark_syntax_set_find_node_type(set, "cxx.box", &type));
    NAOS_CHECK_UINT_EQ(AMARK_NODE_DYNAMIC_FIRST, type);
    NAOS_CHECK_TRUE(amark_syntax_set_node_type_of(set, &cxx_type, &type));
    NAOS_CHECK_PTR_EQ(&cxx_type, amark_syntax_set_node_type_desc(set, type));
    NAOS_CHECK_UINT_EQ(1, amark_syntax_set_slot_count(set, AMARK_SLOT_KIND_USER_FIRST));
    NAOS_CHECK_PTR_EQ(&cxx_slot,
                      amark_syntax_set_slot_at(set, AMARK_SLOT_KIND_USER_FIRST, 0, &extension));
    NAOS_CHECK_PTR_EQ(&cxx_extension, extension);
    NAOS_CHECK_UINT_EQ(1, amark_node_type_find_facet(&cxx_type, "title"));
    NAOS_CHECK_PTR_EQ(&cxx_facets[0], amark_node_type_facet(&cxx_type, 1));

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_clone(set, &copy));
    NAOS_CHECK_FALSE(amark_syntax_set_is_frozen(copy));
    amark_syntax_set_destroy(copy);
    amark_syntax_set_destroy(set);
}

NAOS_TEST(builtin_types_link)
{
    const amark_syntax_set_t *core = amark_syntax_set_core();
    const amark_node_type_desc_t *heading;
    amark_node_type_t type = AMARK_NODE_NONE;

    NAOS_REQUIRE_NOT_NULL(core);
    NAOS_CHECK_TRUE(amark_syntax_set_is_frozen(core));
    NAOS_CHECK_TRUE(amark_syntax_set_find_node_type(core, "heading", &type));
    NAOS_CHECK_UINT_EQ(AMARK_NODE_HEADING, type);
    heading = amark_syntax_set_node_type_desc(core, AMARK_NODE_HEADING);
    NAOS_REQUIRE_NOT_NULL(heading);
    NAOS_CHECK_UINT_EQ(AMARK_HEADING_FORM, amark_node_type_find_facet(heading, "form"));
    NAOS_CHECK_STR_EQ("setext", amark_node_type_facet(heading, AMARK_HEADING_FORM)
                                    ->enum_names[AMARK_HEADING_FORM_SETEXT - 1]);
}

NAOS_TEST(document_and_node_functions_link)
{
    amark_document_t *document = NULL;
    amark_node_t *root;
    amark_node_t *node = NULL;
    amark_syntax_set_t *set = NULL;
    int value = 0;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    NAOS_CHECK_PTR_EQ(amark_syntax_set_core(), amark_document_syntax_set(document));
    root = amark_document_root(document);
    NAOS_REQUIRE_NOT_NULL(root);
    NAOS_CHECK_UINT_EQ(AMARK_NODE_DOCUMENT, amark_node_get_type(root));
    NAOS_CHECK_NOT_NULL(amark_node_type_desc(root));
    NAOS_CHECK_PTR_EQ(document, amark_node_document(root));
    NAOS_CHECK_TRUE(amark_node_is_block(root));
    NAOS_CHECK_FALSE(amark_node_is_inline(root));
    NAOS_CHECK_FALSE(amark_node_is_container(root));
    NAOS_CHECK_FALSE(amark_node_has_content(root));
    NAOS_CHECK_TRUE(amark_node_can_contain(root, AMARK_NODE_PARAGRAPH));

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, AMARK_NODE_TEXT, &node));
    NAOS_CHECK_NULL(amark_node_parent(node));
    NAOS_CHECK_NULL(amark_node_first_child(node));
    NAOS_CHECK_NULL(amark_node_last_child(node));
    NAOS_CHECK_NULL(amark_node_next(node));
    NAOS_CHECK_NULL(amark_node_previous(node));
    NAOS_CHECK_NULL(amark_node_data(node));
    amark_node_set_user_data(node, &value);
    NAOS_CHECK_PTR_EQ(&value, amark_node_user_data(node));
    amark_node_destroy(node);
    amark_document_destroy(document);

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_create(&set));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_freeze(set));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(set, &document));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_take_syntax_set(document, set));
    amark_document_destroy(document);
}

NAOS_SUITE(cxx_include,
    NAOS_ADD_TEST(version_matches_header),
    NAOS_ADD_TEST(error_string_describes_codes),
    NAOS_ADD_TEST(buffer_appends_and_detaches),
    NAOS_ADD_TEST(allocator_functions_link),
    NAOS_ADD_TEST(utf8_and_unicode_functions_link),
    NAOS_ADD_TEST(entity_functions_link),
    NAOS_ADD_TEST(registry_functions_link),
    NAOS_ADD_TEST(builtin_types_link),
    NAOS_ADD_TEST(document_and_node_functions_link)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(cxx_include)
)
