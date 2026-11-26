/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The descriptors of the built-in node types and the amark.core extension
 * (src/tree/builtin_types.c).
 *
 * Suite builtin_types (2.1, 2.4, 2.5, 8.1): each descriptor equals its row of the
 * table of the design (identifier, name, level, provides, accepts, leaf or not,
 * literal mode, type data), amark.core holds the 19 descriptors in identifier
 * order and no slot, the function tables are the expected ones, the defaults of
 * the type data, the comparison of list (start only when ordered), the copy,
 * comparison and release of the string attributes through the global allocator
 * (with the out-of-memory sweep of the copies), the literal check of code, and the
 * nodes created in a document (4.1): their level, leaves, containment and default
 * type data.
 */
#include "tree/syntax_set.h"

#include <amark/allocator.h>
#include <amark/document.h>
#include <amark/error.h>
#include <amark/extension.h>
#include <amark/node.h>

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <naos/naos.h>

#include "test_allocator.h"

/* The test allocator, set by the tests that count allocations; static so that no
 * dangling pointer stays set if a test stops early. */
static amark_test_allocator_t counting;

NAOS_TEARDOWN(restore)
{
    amark_allocator_set(NULL);
}

/* Sets the counting test allocator and stops the test if that fails (it is then
 * not set). While it is set, the tests use no NAOS_REQUIRE_*. */
#define SET_COUNTING() \
    do { \
        amark_test_allocator_init(&counting); \
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, \
                            amark_allocator_set(amark_test_allocator_activate(&counting))); \
    } while (0)

/* Gets the descriptor of a built-in type and stops the test if it is missing. */
#define REQUIRE_DESC(d, type) \
    do { \
        (d) = amark_builtin_types[(type)]; \
        NAOS_REQUIRE_NOT_NULL(d); \
    } while (0)

#define BLOCK AMARK_CONTENT_BLOCK
#define INLINE AMARK_CONTENT_INLINE
#define LIST_ITEM AMARK_CONTENT_LIST_ITEM

/* A row of the table of the design. */
typedef struct expected_type
{
    amark_node_type_t id;
    const char *name;
    amark_node_level_t level;
    uint32_t provides;
    uint32_t accepts;
    amark_literal_mode_t literal_mode;
    size_t data_size;
} expected_type_t;

static const expected_type_t expected_types[] = {
    { AMARK_NODE_DOCUMENT, "document", AMARK_NODE_LEVEL_BLOCK, 0, BLOCK, AMARK_LITERAL_NONE, 0 },
    { AMARK_NODE_BLOCK_QUOTE, "block_quote", AMARK_NODE_LEVEL_BLOCK, BLOCK, BLOCK,
      AMARK_LITERAL_NONE, 0 },
    { AMARK_NODE_LIST, "list", AMARK_NODE_LEVEL_BLOCK, BLOCK, LIST_ITEM, AMARK_LITERAL_NONE,
      sizeof(amark_list_data_t) },
    { AMARK_NODE_ITEM, "item", AMARK_NODE_LEVEL_BLOCK, LIST_ITEM, BLOCK, AMARK_LITERAL_NONE, 0 },
    { AMARK_NODE_CODE_BLOCK, "code_block", AMARK_NODE_LEVEL_BLOCK, BLOCK, 0, AMARK_LITERAL_LINES,
      sizeof(amark_code_block_data_t) },
    { AMARK_NODE_HTML_BLOCK, "html_block", AMARK_NODE_LEVEL_BLOCK, BLOCK, 0, AMARK_LITERAL_LINES,
      0 },
    { AMARK_NODE_PARAGRAPH, "paragraph", AMARK_NODE_LEVEL_BLOCK, BLOCK, INLINE,
      AMARK_LITERAL_NONE, 0 },
    { AMARK_NODE_HEADING, "heading", AMARK_NODE_LEVEL_BLOCK, BLOCK, INLINE, AMARK_LITERAL_NONE,
      sizeof(amark_heading_data_t) },
    { AMARK_NODE_THEMATIC_BREAK, "thematic_break", AMARK_NODE_LEVEL_BLOCK, BLOCK, 0,
      AMARK_LITERAL_NONE, 0 },
    { AMARK_NODE_LINK_DEFINITION, "link_definition", AMARK_NODE_LEVEL_BLOCK, BLOCK, 0,
      AMARK_LITERAL_NONE, sizeof(amark_link_definition_data_t) },
    { AMARK_NODE_TEXT, "text", AMARK_NODE_LEVEL_INLINE, INLINE, 0, AMARK_LITERAL_LINE, 0 },
    { AMARK_NODE_SOFTBREAK, "softbreak", AMARK_NODE_LEVEL_INLINE, INLINE, 0, AMARK_LITERAL_NONE,
      0 },
    { AMARK_NODE_LINEBREAK, "linebreak", AMARK_NODE_LEVEL_INLINE, INLINE, 0, AMARK_LITERAL_NONE,
      0 },
    { AMARK_NODE_CODE, "code", AMARK_NODE_LEVEL_INLINE, INLINE, 0, AMARK_LITERAL_LINE, 0 },
    { AMARK_NODE_HTML_INLINE, "html_inline", AMARK_NODE_LEVEL_INLINE, INLINE, 0,
      AMARK_LITERAL_SPAN, 0 },
    { AMARK_NODE_EMPH, "emph", AMARK_NODE_LEVEL_INLINE, INLINE, INLINE, AMARK_LITERAL_NONE, 0 },
    { AMARK_NODE_STRONG, "strong", AMARK_NODE_LEVEL_INLINE, INLINE, INLINE, AMARK_LITERAL_NONE,
      0 },
    { AMARK_NODE_LINK, "link", AMARK_NODE_LEVEL_INLINE, INLINE, INLINE, AMARK_LITERAL_NONE,
      sizeof(amark_link_data_t) },
    { AMARK_NODE_IMAGE, "image", AMARK_NODE_LEVEL_INLINE, INLINE, INLINE, AMARK_LITERAL_NONE,
      sizeof(amark_link_data_t) },
};

#define EXPECTED_COUNT (sizeof(expected_types) / sizeof(expected_types[0]))

/* Whether type has string attributes (the deinit, copy and equal functions). */
static bool has_strings(amark_node_type_t type)
{
    return (type == AMARK_NODE_CODE_BLOCK) || (type == AMARK_NODE_LINK)
           || (type == AMARK_NODE_IMAGE) || (type == AMARK_NODE_LINK_DEFINITION);
}

/* The type data of any built-in type with string attributes. */
typedef union string_data
{
    amark_code_block_data_t code_block;
    amark_link_data_t link;
    amark_link_definition_data_t link_definition;
} string_data_t;

/* Sets *s to a copy of text allocated with amark_malloc; "" gives NULL. */
static amark_error_t set_string(amark_builtin_string_t *s, const char *text)
{
    size_t length = strlen(text);

    s->text = NULL;
    s->length = 0;
    if (length == 0)
    {
        return AMARK_ERROR_NONE;
    }

    s->text = amark_malloc(length + 1);
    if (s->text == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }

    memcpy(s->text, text, length + 1);
    s->length = length;

    return AMARK_ERROR_NONE;
}

/* Whether s holds text ("" matches NULL). */
static bool string_is(const amark_builtin_string_t *s, const char *text)
{
    size_t length = strlen(text);

    if (s->length != length)
    {
        return false;
    }

    if (length == 0)
    {
        return true;
    }

    return (s->text != NULL) && (memcmp(s->text, text, length + 1) == 0);
}

/* --- The table ----------------------------------------------------------------- */

NAOS_TEST(the_builtin_table_is_indexed_by_identifier)
{
    /* 2.1: the identifiers 1-19 are fixed, and index 0 (AMARK_NODE_NONE) is empty. */
    size_t i;

    NAOS_CHECK_UINT_EQ(19, AMARK_BUILTIN_TYPE_COUNT);
    NAOS_CHECK_UINT_EQ(AMARK_BUILTIN_TYPE_COUNT, EXPECTED_COUNT);
    NAOS_CHECK_NULL(amark_builtin_types[AMARK_NODE_NONE]);

    for (i = 0; i < EXPECTED_COUNT; i++)
    {
        NAOS_CHECK_UINT_EQ(i + 1, expected_types[i].id);
        NAOS_CHECK_NOT_NULL(amark_builtin_types[expected_types[i].id]);
    }
}

NAOS_TEST(each_descriptor_matches_its_row)
{
    /* 2.1, 2.4: the name, the level, the content classes, leaf or not, the literal
     * mode and the size of the type data of the table of the design. */
    size_t i;

    for (i = 0; i < EXPECTED_COUNT; i++)
    {
        const expected_type_t *e = &expected_types[i];
        const amark_node_type_desc_t *d;
        bool leaf;

        REQUIRE_DESC(d, e->id);
        leaf = (d->accepts == 0) && (d->can_contain == NULL);
        NAOS_CHECK_UINT_EQ(sizeof(amark_node_type_desc_t), d->struct_size);
        NAOS_CHECK_STR_EQ(e->name, d->name);
        NAOS_CHECK_INT_EQ(e->level, d->level);
        NAOS_CHECK_UINT_EQ(e->provides, d->provides);
        NAOS_CHECK_UINT_EQ(e->accepts, d->accepts);
        NAOS_CHECK_INT_EQ(e->accepts == 0, leaf);
        NAOS_CHECK_INT_EQ(e->literal_mode, d->literal_mode);
        NAOS_CHECK_UINT_EQ(e->data_size, d->data_size);
        NAOS_CHECK_TRUE(d->can_contain == NULL);
        NAOS_CHECK_NULL(d->user_data);
    }
}

NAOS_TEST(builtin_names_are_unique_and_without_a_dot)
{
    /* 2.5, 8.1: a name without "." is reserved for the built-in types, and the
     * names do not repeat. */
    size_t i;
    size_t j;

    for (i = 1; i <= AMARK_BUILTIN_TYPE_COUNT; i++)
    {
        const amark_node_type_desc_t *d;

        REQUIRE_DESC(d, i);
        NAOS_CHECK_NULL(strchr(d->name, '.'));
        for (j = i + 1; j <= AMARK_BUILTIN_TYPE_COUNT; j++)
        {
            NAOS_REQUIRE_NOT_NULL(amark_builtin_types[j]);
            NAOS_CHECK_INT_NE(0, strcmp(d->name, amark_builtin_types[j]->name));
        }
    }
}

NAOS_TEST(the_function_tables_are_the_expected_ones)
{
    /* The string attributes have deinit, copy and equal; list has init and equal;
     * heading has init; code has validate_literal; the built-in attributes are
     * written by the dump itself, so no built-in type has a dump function. */
    amark_node_type_t type;

    for (type = 1; type <= AMARK_BUILTIN_TYPE_COUNT; type++)
    {
        const amark_node_type_desc_t *d;
        bool strings = has_strings(type);

        REQUIRE_DESC(d, type);
        NAOS_CHECK_INT_EQ((type == AMARK_NODE_LIST) || (type == AMARK_NODE_HEADING),
                          d->init != NULL);
        NAOS_CHECK_INT_EQ(strings, d->deinit != NULL);
        NAOS_CHECK_INT_EQ(strings, d->copy != NULL);
        NAOS_CHECK_INT_EQ(strings || (type == AMARK_NODE_LIST), d->equal != NULL);
        NAOS_CHECK_INT_EQ(type == AMARK_NODE_CODE, d->validate_literal != NULL);
        NAOS_CHECK_TRUE(d->dump == NULL);
    }
}

/* --- amark.core ---------------------------------------------------------------- */

NAOS_TEST(the_core_extension_holds_the_builtin_types_in_order)
{
    /* 2.5, 8.1: amark.core uses the descriptor of an extension: the 19 types in
     * identifier order, no slot. */
    size_t i;

    NAOS_CHECK_UINT_EQ(sizeof(amark_extension_t), amark_core_extension.struct_size);
    NAOS_CHECK_STR_EQ("amark.core", amark_core_extension.name);
    NAOS_REQUIRE_UINT_EQ(AMARK_BUILTIN_TYPE_COUNT, amark_core_extension.node_type_count);
    NAOS_REQUIRE_NOT_NULL(amark_core_extension.node_types);
    for (i = 0; i < AMARK_BUILTIN_TYPE_COUNT; i++)
    {
        NAOS_CHECK_PTR_EQ(amark_builtin_types[i + 1], amark_core_extension.node_types[i]);
    }

    NAOS_CHECK_NULL(amark_core_extension.slots);
    NAOS_CHECK_UINT_EQ(0, amark_core_extension.slot_count);
    NAOS_CHECK_NULL(amark_core_extension.user_data);
}

/* --- The defaults ---------------------------------------------------------------- */

NAOS_TEST(list_defaults_to_a_tight_bullet_list_from_one)
{
    const amark_node_type_desc_t *d;
    amark_list_data_t data;

    REQUIRE_DESC(d, AMARK_NODE_LIST);
    NAOS_REQUIRE_TRUE(d->init != NULL);
    memset(&data, 0, sizeof(data));

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, d->init(NULL, &data));
    NAOS_CHECK_INT_EQ(AMARK_LIST_BULLET, data.type);
    NAOS_CHECK_INT_EQ(1, data.start);
    NAOS_CHECK_TRUE(data.tight);
}

NAOS_TEST(heading_defaults_to_level_one)
{
    const amark_node_type_desc_t *d;
    amark_heading_data_t data;

    REQUIRE_DESC(d, AMARK_NODE_HEADING);
    NAOS_REQUIRE_TRUE(d->init != NULL);
    memset(&data, 0, sizeof(data));

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, d->init(NULL, &data));
    NAOS_CHECK_INT_EQ(1, data.level);
}

NAOS_TEST(zero_filled_strings_are_empty_and_release_nothing)
{
    /* The string attributes default to "" by the zero filling: deinit, copy and
     * equal accept them and allocate nothing. */
    amark_node_type_t types[] = { AMARK_NODE_CODE_BLOCK, AMARK_NODE_LINK, AMARK_NODE_IMAGE,
                                  AMARK_NODE_LINK_DEFINITION };
    string_data_t a;
    string_data_t b;
    const amark_node_type_desc_t *d[4];
    amark_error_t err[4];
    bool equal[4];
    size_t i;

    for (i = 0; i < 4; i++)
    {
        REQUIRE_DESC(d[i], types[i]);
        NAOS_REQUIRE_TRUE(d[i]->deinit != NULL);
        NAOS_REQUIRE_TRUE(d[i]->copy != NULL);
        NAOS_REQUIRE_TRUE(d[i]->equal != NULL);
        NAOS_REQUIRE_TRUE(d[i]->data_size <= sizeof(a));
    }

    SET_COUNTING();
    for (i = 0; i < 4; i++)
    {
        memset(&a, 0, sizeof(a));
        memset(&b, 0, sizeof(b));
        err[i] = d[i]->copy(NULL, &b, NULL, &a);
        equal[i] = d[i]->equal(NULL, &a, NULL, &b);
        d[i]->deinit(NULL, &a);
        d[i]->deinit(NULL, &b);
    }

    amark_allocator_set(NULL);
    NAOS_CHECK_UINT_EQ(0, counting.calls);
    for (i = 0; i < 4; i++)
    {
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, err[i]);
        NAOS_CHECK_TRUE(equal[i]);
    }
}

/* --- Created nodes ----------------------------------------------------------------- */

/* Whether the string attributes of the type data of node (a type with strings) are
 * all "" (NULL, length 0). */
static bool strings_are_empty(const amark_node_t *node)
{
    const string_data_t *data = amark_node_data_const(node);

    switch (amark_node_get_type(node))
    {
    case AMARK_NODE_CODE_BLOCK:
        return (data->code_block.info.text == NULL) && (data->code_block.info.length == 0);
    case AMARK_NODE_LINK:
    case AMARK_NODE_IMAGE:
        return (data->link.destination.text == NULL) && (data->link.destination.length == 0)
               && (data->link.title.text == NULL) && (data->link.title.length == 0);
    case AMARK_NODE_LINK_DEFINITION:
        return (data->link_definition.link.destination.text == NULL)
               && (data->link_definition.link.destination.length == 0)
               && (data->link_definition.link.title.text == NULL)
               && (data->link_definition.link.title.length == 0)
               && (data->link_definition.label.text == NULL)
               && (data->link_definition.label.length == 0);
    default:
        return false;
    }
}

NAOS_TEST(created_nodes_have_the_defaults_and_the_rules_of_their_row)
{
    amark_document_t *document = NULL;
    amark_node_t *nodes[AMARK_BUILTIN_TYPE_COUNT + 1] = { NULL };
    const amark_list_data_t *list;
    const amark_heading_data_t *heading;
    size_t i;
    size_t j;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    nodes[AMARK_NODE_DOCUMENT] = amark_document_root(document);
    for (i = 1; i < EXPECTED_COUNT; i++)
    {
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                            amark_node_create(document, expected_types[i].id,
                                              &nodes[expected_types[i].id]));
    }

    /* The level, the leaves and the containment of each row (2.4). */
    for (i = 0; i < EXPECTED_COUNT; i++)
    {
        const expected_type_t *row = &expected_types[i];
        const amark_node_t *node = nodes[row->id];

        NAOS_CHECK_STR_EQ(row->name, amark_node_type_name(node));
        NAOS_CHECK_INT_EQ(row->level == AMARK_NODE_LEVEL_BLOCK, amark_node_is_block(node));
        NAOS_CHECK_INT_EQ(row->level == AMARK_NODE_LEVEL_INLINE, amark_node_is_inline(node));
        NAOS_CHECK_INT_EQ(row->accepts != 0, amark_node_can_have_children(node));
        for (j = 0; j < EXPECTED_COUNT; j++)
        {
            NAOS_CHECK_INT_EQ((row->accepts & expected_types[j].provides) != 0,
                              amark_node_can_contain(node, expected_types[j].id));
        }

        NAOS_CHECK_FALSE(amark_node_can_contain(node, AMARK_NODE_NONE));
        NAOS_CHECK_FALSE(amark_node_can_contain(node, AMARK_NODE_DYNAMIC_FIRST));
        if (row->data_size == 0)
        {
            NAOS_CHECK_NULL(amark_node_data_const(node));
        }
        else if (has_strings(row->id))
        {
            NAOS_CHECK_TRUE(strings_are_empty(node));
        }
    }

    /* The defaults of list and heading (4.1). */
    list = amark_node_data_const(nodes[AMARK_NODE_LIST]);
    heading = amark_node_data_const(nodes[AMARK_NODE_HEADING]);
    NAOS_REQUIRE_NOT_NULL(list);
    NAOS_REQUIRE_NOT_NULL(heading);
    NAOS_CHECK_INT_EQ(AMARK_LIST_BULLET, list->type);
    NAOS_CHECK_INT_EQ(1, list->start);
    NAOS_CHECK_TRUE(list->tight);
    NAOS_CHECK_INT_EQ(1, heading->level);

    amark_document_destroy(document);
}

/* --- list equal ------------------------------------------------------------------ */

NAOS_TEST(list_equal_compares_start_only_when_ordered)
{
    const amark_node_type_desc_t *d;
    amark_list_data_t a;
    amark_list_data_t b;

    REQUIRE_DESC(d, AMARK_NODE_LIST);
    NAOS_REQUIRE_TRUE(d->equal != NULL);
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));

    /* Bullet lists: start is ignored. */
    a.type = AMARK_LIST_BULLET;
    a.start = 1;
    a.tight = true;
    b = a;
    b.start = 7;
    NAOS_CHECK_TRUE(d->equal(NULL, &a, NULL, &b));

    /* Ordered lists: start counts. */
    a.type = AMARK_LIST_ORDERED;
    b.type = AMARK_LIST_ORDERED;
    NAOS_CHECK_FALSE(d->equal(NULL, &a, NULL, &b));
    b.start = 1;
    NAOS_CHECK_TRUE(d->equal(NULL, &a, NULL, &b));

    /* The type and the tightness always count. */
    b.type = AMARK_LIST_BULLET;
    NAOS_CHECK_FALSE(d->equal(NULL, &a, NULL, &b));
    b.type = AMARK_LIST_ORDERED;
    b.tight = false;
    NAOS_CHECK_FALSE(d->equal(NULL, &a, NULL, &b));
}

/* --- The string attributes ------------------------------------------------------- */

/* Fills a link_definition data (whose layout begins with the link data) with
 * label, destination and title. */
static amark_error_t fill_link_definition(amark_link_definition_data_t *data, const char *label,
                                          const char *destination, const char *title)
{
    amark_error_t err;

    memset(data, 0, sizeof(*data));
    err = set_string(&data->label, label);
    if (err == AMARK_ERROR_NONE)
    {
        err = set_string(&data->link.destination, destination);
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = set_string(&data->link.title, title);
    }

    return err;
}

NAOS_TEST(link_definition_copy_equal_and_deinit)
{
    const amark_node_type_desc_t *d;
    amark_link_definition_data_t src;
    amark_link_definition_data_t dst;
    amark_error_t fill_err;
    amark_error_t copy_err;
    bool same_after_copy;
    bool same_after_change;
    bool label_ok;
    bool destination_ok;
    bool title_ok;
    bool owns_its_strings;
    size_t live_after_copy;

    REQUIRE_DESC(d, AMARK_NODE_LINK_DEFINITION);
    NAOS_REQUIRE_TRUE(d->copy != NULL);
    NAOS_REQUIRE_TRUE(d->equal != NULL);
    NAOS_REQUIRE_TRUE(d->deinit != NULL);

    SET_COUNTING();
    fill_err = fill_link_definition(&src, "Foo", "/url", "a title");
    memset(&dst, 0, sizeof(dst));
    copy_err = d->copy(NULL, &dst, NULL, &src);
    live_after_copy = counting.live_blocks;
    label_ok = string_is(&dst.label, "Foo");
    destination_ok = string_is(&dst.link.destination, "/url");
    title_ok = string_is(&dst.link.title, "a title");
    owns_its_strings = (dst.label.text != src.label.text)
                       && (dst.link.destination.text != src.link.destination.text)
                       && (dst.link.title.text != src.link.title.text);
    same_after_copy = d->equal(NULL, &src, NULL, &dst);
    if (dst.label.text != NULL)
    {
        dst.label.text[0] = 'G';
    }

    same_after_change = d->equal(NULL, &src, NULL, &dst);
    d->deinit(NULL, &src);
    d->deinit(NULL, &dst);

    amark_allocator_set(NULL);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, fill_err);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, copy_err);
    NAOS_CHECK_UINT_EQ(6, live_after_copy);
    NAOS_CHECK_TRUE(label_ok);
    NAOS_CHECK_TRUE(destination_ok);
    NAOS_CHECK_TRUE(title_ok);
    NAOS_CHECK_TRUE(owns_its_strings);
    NAOS_CHECK_TRUE(same_after_copy);
    NAOS_CHECK_FALSE(same_after_change);
    NAOS_CHECK_UINT_EQ(0, counting.live_blocks);
}

NAOS_TEST(string_equal_compares_each_attribute)
{
    /* link and image compare the destination and the title, code_block the info,
     * link_definition also the label; a different length is a difference. */
    const amark_node_type_desc_t *link;
    const amark_node_type_desc_t *image;
    const amark_node_type_desc_t *code_block;
    const amark_node_type_desc_t *definition;
    amark_link_definition_data_t a;
    amark_link_definition_data_t b;
    amark_code_block_data_t ca;
    amark_code_block_data_t cb;

    REQUIRE_DESC(link, AMARK_NODE_LINK);
    REQUIRE_DESC(image, AMARK_NODE_IMAGE);
    REQUIRE_DESC(code_block, AMARK_NODE_CODE_BLOCK);
    REQUIRE_DESC(definition, AMARK_NODE_LINK_DEFINITION);
    NAOS_REQUIRE_TRUE(link->equal != NULL);
    NAOS_REQUIRE_TRUE(image->equal != NULL);
    NAOS_REQUIRE_TRUE(code_block->equal != NULL);
    NAOS_REQUIRE_TRUE(definition->equal != NULL);

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, fill_link_definition(&a, "x", "/u", "t"));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, fill_link_definition(&b, "y", "/u", "t"));
    NAOS_CHECK_TRUE(link->equal(NULL, &a.link, NULL, &b.link));
    NAOS_CHECK_TRUE(image->equal(NULL, &a.link, NULL, &b.link));
    NAOS_CHECK_FALSE(definition->equal(NULL, &a, NULL, &b));
    definition->deinit(NULL, &b);

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, fill_link_definition(&b, "x", "/u", "tt"));
    NAOS_CHECK_FALSE(link->equal(NULL, &a.link, NULL, &b.link));
    NAOS_CHECK_FALSE(image->equal(NULL, &a.link, NULL, &b.link));
    NAOS_CHECK_FALSE(definition->equal(NULL, &a, NULL, &b));
    definition->deinit(NULL, &b);

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, fill_link_definition(&b, "x", "", "t"));
    NAOS_CHECK_FALSE(link->equal(NULL, &a.link, NULL, &b.link));
    NAOS_CHECK_FALSE(definition->equal(NULL, &a, NULL, &b));
    definition->deinit(NULL, &b);
    definition->deinit(NULL, &a);

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, set_string(&ca.info, "c"));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, set_string(&cb.info, "c"));
    NAOS_CHECK_TRUE(code_block->equal(NULL, &ca, NULL, &cb));
    code_block->deinit(NULL, &cb);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, set_string(&cb.info, "d"));
    NAOS_CHECK_FALSE(code_block->equal(NULL, &ca, NULL, &cb));
    code_block->deinit(NULL, &cb);
    code_block->deinit(NULL, &ca);
}

/* Fills data, zero-filled, with the strings of type. */
static amark_error_t fill_strings(amark_node_type_t type, string_data_t *data)
{
    if (type == AMARK_NODE_CODE_BLOCK)
    {
        return set_string(&data->code_block.info, "c");
    }

    if (type == AMARK_NODE_LINK_DEFINITION)
    {
        return fill_link_definition(&data->link_definition, "label", "/destination", "title");
    }

    if (set_string(&data->link.destination, "/destination") != AMARK_ERROR_NONE)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }

    return set_string(&data->link.title, "title");
}

/* The context of the out-of-memory sweep of a copy: the type to copy. */
typedef struct copy_case
{
    amark_node_type_t type;
} copy_case_t;

/* Fills a source of the type of the case, copies it and compares the copy. On
 * failure the destination owns nothing that deinit would not release, so a
 * leak or a double free shows. */
static amark_error_t copy_scenario(void *context)
{
    const copy_case_t *c = context;
    const amark_node_type_desc_t *d = amark_builtin_types[c->type];
    string_data_t src;
    string_data_t dst;
    amark_error_t err;

    memset(&src, 0, sizeof(src));
    memset(&dst, 0, sizeof(dst));
    err = fill_strings(c->type, &src);
    if (err == AMARK_ERROR_NONE)
    {
        err = d->copy(NULL, &dst, NULL, &src);
    }

    if ((err == AMARK_ERROR_NONE) && !d->equal(NULL, &src, NULL, &dst))
    {
        err = AMARK_ERROR_INVALID_STATE;
    }

    d->deinit(NULL, &dst);
    d->deinit(NULL, &src);

    return err;
}

NAOS_TEST(string_copies_pass_the_out_of_memory_sweep)
{
    /* Each copy fails with AMARK_ERROR_OUT_OF_MEMORY at every allocation, without
     * leaking, and succeeds when nothing fails. */
    copy_case_t cases[] = { { AMARK_NODE_CODE_BLOCK }, { AMARK_NODE_LINK }, { AMARK_NODE_IMAGE },
                            { AMARK_NODE_LINK_DEFINITION } };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        const amark_node_type_desc_t *d;

        REQUIRE_DESC(d, cases[i].type);
        NAOS_REQUIRE_TRUE(d->copy != NULL);
        NAOS_REQUIRE_TRUE(d->equal != NULL);
        NAOS_REQUIRE_TRUE(d->deinit != NULL);
        AMARK_CHECK_OOM_SWEEP(copy_scenario, &cases[i]);
    }
}

/* --- code ------------------------------------------------------------------------ */

NAOS_TEST(code_rejects_an_empty_literal)
{
    const amark_node_type_desc_t *d;

    REQUIRE_DESC(d, AMARK_NODE_CODE);
    NAOS_REQUIRE_TRUE(d->validate_literal != NULL);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, d->validate_literal(NULL, "", 0));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, d->validate_literal(NULL, "x", 0));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, d->validate_literal(NULL, "x", 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, d->validate_literal(NULL, " ", 1));
}

NAOS_SUITE(builtin_types,
    NAOS_SET_TEARDOWN(restore),
    NAOS_ADD_TEST(the_builtin_table_is_indexed_by_identifier),
    NAOS_ADD_TEST(each_descriptor_matches_its_row),
    NAOS_ADD_TEST(builtin_names_are_unique_and_without_a_dot),
    NAOS_ADD_TEST(the_function_tables_are_the_expected_ones),
    NAOS_ADD_TEST(the_core_extension_holds_the_builtin_types_in_order),
    NAOS_ADD_TEST(list_defaults_to_a_tight_bullet_list_from_one),
    NAOS_ADD_TEST(heading_defaults_to_level_one),
    NAOS_ADD_TEST(zero_filled_strings_are_empty_and_release_nothing),
    NAOS_ADD_TEST(created_nodes_have_the_defaults_and_the_rules_of_their_row),
    NAOS_ADD_TEST(list_equal_compares_start_only_when_ordered),
    NAOS_ADD_TEST(link_definition_copy_equal_and_deinit),
    NAOS_ADD_TEST(string_equal_compares_each_attribute),
    NAOS_ADD_TEST(string_copies_pass_the_out_of_memory_sweep),
    NAOS_ADD_TEST(code_rejects_an_empty_literal))

NAOS_MAIN(
    NAOS_ADD_SUITE(builtin_types)
)
