/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The semantic comparison (src/tree/equal.c), through the public API.
 *
 * Suite equal (8.12, 9.1, 4.9): two copies of a sample tree built in two documents
 * are equal, also when one has tokens, edit records or associated pointers the
 * other lacks, and when the documents use different syntax sets that hold the same
 * descriptors under other identifiers; changing one built-in attribute (heading
 * level, list type, start of an ordered list, tightness, code info, link and image
 * destination and title, link definition label, destination and title), a literal,
 * the type data of an extension type (by its equal function, or by its bytes), a
 * type, the number or the order of the children makes them unequal; the start of a
 * bullet list is ignored; NULL is never equal; unknown flag bits are ignored; a
 * chain of 100000 nodes is compared without recursion; no comparison allocates.
 * Suite equal_text (9.2): text split differently is equal only with
 * AMARK_EQUAL_NORMALIZE_TEXT, which compares each run of adjacent text siblings as
 * one string (not across other nodes, with the nodes after the runs compared in
 * order) and ignores empty text nodes.
 */
#include <amark/allocator.h>
#include <amark/document.h>
#include <amark/error.h>
#include <amark/extension.h>
#include <amark/node.h>
#include <amark/token.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <naos/naos.h>

#include "test_allocator.h"

NAOS_TEARDOWN(restore)
{
    amark_allocator_set(NULL);
}

/* --- Extension types ------------------------------------------------------------ */

/* x.box: a block container whose type data owns a name (copy, equal, deinit). */
typedef struct box_data
{
    char *name;
} box_data_t;

static size_t box_equal_calls;

static void box_deinit(amark_node_t *node, void *data)
{
    (void)node;
    amark_free(((box_data_t *)data)->name);
}

static bool box_equal(const amark_node_t *a, const void *a_data, const amark_node_t *b,
                      const void *b_data)
{
    const char *x = ((const box_data_t *)a_data)->name;
    const char *y = ((const box_data_t *)b_data)->name;

    (void)a;
    (void)b;
    box_equal_calls++;
    if ((x == NULL) || (y == NULL))
    {
        return (x == y);
    }

    return (strcmp(x, y) == 0);
}

/* x.raw: a block leaf whose type data is compared by its bytes. */
typedef struct raw_data
{
    uint32_t number;
    unsigned char bytes[12];
} raw_data_t;

static const amark_node_type_desc_t box_type = {
    sizeof(amark_node_type_desc_t), "x.box", AMARK_NODE_LEVEL_BLOCK, AMARK_CONTENT_BLOCK,
    AMARK_CONTENT_BLOCK, AMARK_LITERAL_NONE, sizeof(box_data_t), NULL, NULL, NULL, box_deinit,
    NULL, box_equal, NULL, NULL
};

static const amark_node_type_desc_t raw_type = {
    sizeof(amark_node_type_desc_t), "x.raw", AMARK_NODE_LEVEL_BLOCK, AMARK_CONTENT_BLOCK, 0,
    AMARK_LITERAL_NONE, sizeof(raw_data_t), NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

static const amark_node_type_desc_t pad_type = {
    sizeof(amark_node_type_desc_t), "pad.node", AMARK_NODE_LEVEL_BLOCK, AMARK_CONTENT_BLOCK, 0,
    AMARK_LITERAL_NONE, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

static const amark_node_type_desc_t *const x_types[] = { &box_type, &raw_type };

static const amark_extension_t x_extension = {
    sizeof(amark_extension_t), "x", x_types, 2, NULL, 0, NULL
};

static const amark_node_type_desc_t *const pad_types[] = { &pad_type };

static const amark_extension_t pad_extension = {
    sizeof(amark_extension_t), "pad", pad_types, 1, NULL, 0, NULL
};

/* Creates a frozen set: with with_pad, core + pad + x (x.box is 257), otherwise
 * core + x (x.box is 256), so that the same descriptors have other identifiers. */
static amark_syntax_set_t *create_set(bool with_pad)
{
    amark_syntax_set_t *set = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_create(&set));
    if (with_pad)
    {
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &pad_extension));
    }

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &x_extension));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_freeze(set));

    return set;
}

/* The type of desc in the syntax set of document. */
static amark_node_type_t type_of(const amark_document_t *document,
                                 const amark_node_type_desc_t *desc)
{
    amark_node_type_t type = AMARK_NODE_NONE;

    NAOS_REQUIRE_TRUE(amark_syntax_set_node_type_of(amark_document_syntax_set(document), desc,
                                                    &type));

    return type;
}

/* --- Helpers -------------------------------------------------------------------- */

/* Creates a node of type and appends it to parent (when not NULL). */
static amark_node_t *add(amark_document_t *document, amark_node_t *parent, amark_node_type_t type)
{
    amark_node_t *node = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, type, &node));
    if (parent != NULL)
    {
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(parent, node));
    }

    return node;
}

/* Creates a text node with literal (none for NULL) and appends it to parent. */
static amark_node_t *add_text(amark_document_t *document, amark_node_t *parent, const char *literal)
{
    amark_node_t *node = add(document, parent, AMARK_NODE_TEXT);

    if (literal != NULL)
    {
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                            amark_node_set_literal(node, literal, strlen(literal)));
    }

    return node;
}

/* Sets the name of a box. */
static void set_box_name(amark_node_t *box, const char *name)
{
    box_data_t *data = amark_node_data(box);
    size_t length = strlen(name);
    char *copy = amark_malloc(length + 1);

    NAOS_REQUIRE_NOT_NULL(copy);
    memcpy(copy, name, length + 1);
    amark_free(data->name);
    data->name = copy;
}

/* Adds a token of kind over the length bytes at offset of source to node. */
static void add_token(amark_node_t *node, amark_token_kind_t kind, amark_source_id_t source,
                      size_t offset, size_t length)
{
    amark_token_t token;

    memset(&token, 0, sizeof(token));
    token.struct_size = sizeof(token);
    token.kind = kind;
    token.source = source;
    token.offset = offset;
    token.length = length;
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_add_token(node, &token));
}

#define SET_STRING(setter, node, text) \
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, setter((node), (text), strlen(text)))

/* The nodes of the sample that the tests change. */
typedef struct sample
{
    amark_document_t *document;
    amark_node_t *root;
    amark_node_t *heading;
    amark_node_t *title;
    amark_node_t *emph;
    amark_node_t *list;
    amark_node_t *link;
    amark_node_t *image;
    amark_node_t *code_block;
    amark_node_t *definition;
    amark_node_t *box;
    amark_node_t *raw;
    amark_node_t *rule;
} sample_t;

/*
 * Builds in a new document whose syntax set is set (core + x, or NULL for a set
 * made here and owned by the document), with edit tracking on when tracking:
 *   heading 2 > text "Title", emph > text "em"
 *   list ordered 3 tight > item > paragraph > text "x", link "d" "t" > text "l",
 *                                             image "i" "it" > text "alt"
 *   code_block "c" "code\n"
 *   link_definition "L" "u" "t2"
 *   x.box "outer" > x.raw (number 7)
 *   thematic_break
 */
static void build_sample(sample_t *s, amark_syntax_set_t *set, bool tracking)
{
    amark_node_t *item;
    amark_node_t *paragraph;
    raw_data_t *raw;

    memset(s, 0, sizeof(*s));
    if (set == NULL)
    {
        set = create_set(false);
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(set, &s->document));
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_take_syntax_set(s->document, set));
    }
    else
    {
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(set, &s->document));
    }

    amark_document_set_edit_tracking(s->document, tracking);
    s->root = amark_document_root(s->document);

    s->heading = add(s->document, s->root, AMARK_NODE_HEADING);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_heading_level(s->heading, 2));
    s->title = add_text(s->document, s->heading, "Title");
    s->emph = add(s->document, s->heading, AMARK_NODE_EMPH);
    (void)add_text(s->document, s->emph, "em");

    s->list = add(s->document, s->root, AMARK_NODE_LIST);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_list_type(s->list, AMARK_LIST_ORDERED));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_list_start(s->list, 3));
    item = add(s->document, s->list, AMARK_NODE_ITEM);
    paragraph = add(s->document, item, AMARK_NODE_PARAGRAPH);
    (void)add_text(s->document, paragraph, "x");
    s->link = add(s->document, paragraph, AMARK_NODE_LINK);
    SET_STRING(amark_node_set_link_destination, s->link, "d");
    SET_STRING(amark_node_set_link_title, s->link, "t");
    (void)add_text(s->document, s->link, "l");
    s->image = add(s->document, paragraph, AMARK_NODE_IMAGE);
    SET_STRING(amark_node_set_link_destination, s->image, "i");
    SET_STRING(amark_node_set_link_title, s->image, "it");
    (void)add_text(s->document, s->image, "alt");

    s->code_block = add(s->document, s->root, AMARK_NODE_CODE_BLOCK);
    SET_STRING(amark_node_set_code_info, s->code_block, "c");
    SET_STRING(amark_node_set_literal, s->code_block, "code\n");

    s->definition = add(s->document, s->root, AMARK_NODE_LINK_DEFINITION);
    SET_STRING(amark_node_set_link_label, s->definition, "L");
    SET_STRING(amark_node_set_link_destination, s->definition, "u");
    SET_STRING(amark_node_set_link_title, s->definition, "t2");

    s->box = add(s->document, s->root, type_of(s->document, &box_type));
    set_box_name(s->box, "outer");
    s->raw = add(s->document, s->box, type_of(s->document, &raw_type));
    raw = amark_node_data(s->raw);
    raw->number = 7;

    s->rule = add(s->document, s->root, AMARK_NODE_THEMATIC_BREAK);
}

/* The changes that make the sample unequal. */
enum
{
    CHANGE_HEADING_LEVEL,
    CHANGE_LIST_TYPE,
    CHANGE_LIST_START,
    CHANGE_LIST_TIGHT,
    CHANGE_CODE_INFO,
    CHANGE_LINK_DESTINATION,
    CHANGE_LINK_TITLE,
    CHANGE_IMAGE_DESTINATION,
    CHANGE_IMAGE_TITLE,
    CHANGE_DEFINITION_LABEL,
    CHANGE_DEFINITION_DESTINATION,
    CHANGE_DEFINITION_TITLE,
    CHANGE_TEXT_LITERAL,
    CHANGE_CODE_LITERAL,
    CHANGE_BOX_NAME,
    CHANGE_RAW_BYTES,
    CHANGE_TYPE,
    CHANGE_FEWER_CHILDREN,
    CHANGE_MORE_CHILDREN,
    CHANGE_NO_CHILDREN,
    CHANGE_ORDER,
    CHANGE_COUNT
};

/* Applies change to s. */
static void apply_change(sample_t *s, int change)
{
    amark_node_t *node;

    switch (change)
    {
    case CHANGE_HEADING_LEVEL:
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_heading_level(s->heading, 3));
        break;

    case CHANGE_LIST_TYPE:
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_list_type(s->list, AMARK_LIST_BULLET));
        break;

    case CHANGE_LIST_START:
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_list_start(s->list, 4));
        break;

    case CHANGE_LIST_TIGHT:
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_list_tight(s->list, false));
        break;

    case CHANGE_CODE_INFO:
        SET_STRING(amark_node_set_code_info, s->code_block, "d");
        break;

    case CHANGE_LINK_DESTINATION:
        SET_STRING(amark_node_set_link_destination, s->link, "e");
        break;

    case CHANGE_LINK_TITLE:
        SET_STRING(amark_node_set_link_title, s->link, "T");
        break;

    case CHANGE_IMAGE_DESTINATION:
        SET_STRING(amark_node_set_link_destination, s->image, "j");
        break;

    case CHANGE_IMAGE_TITLE:
        SET_STRING(amark_node_set_link_title, s->image, "");
        break;

    case CHANGE_DEFINITION_LABEL:
        SET_STRING(amark_node_set_link_label, s->definition, "M");
        break;

    case CHANGE_DEFINITION_DESTINATION:
        SET_STRING(amark_node_set_link_destination, s->definition, "v");
        break;

    case CHANGE_DEFINITION_TITLE:
        SET_STRING(amark_node_set_link_title, s->definition, "t3");
        break;

    case CHANGE_TEXT_LITERAL:
        SET_STRING(amark_node_set_literal, s->title, "Titlf");
        break;

    case CHANGE_CODE_LITERAL:
        SET_STRING(amark_node_set_literal, s->code_block, "code\ncode\n");
        break;

    case CHANGE_BOX_NAME:
        set_box_name(s->box, "inner");
        break;

    case CHANGE_RAW_BYTES:
        ((raw_data_t *)amark_node_data(s->raw))->bytes[11] = 1;
        break;

    case CHANGE_TYPE:
        node = add(s->document, NULL, AMARK_NODE_STRONG);
        (void)add_text(s->document, node, "em");
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_replace(s->emph, node));
        amark_node_destroy(s->emph);
        break;

    case CHANGE_FEWER_CHILDREN:
        amark_node_destroy(s->rule);
        break;

    case CHANGE_MORE_CHILDREN:
        (void)add_text(s->document, s->emph, "!");
        break;

    case CHANGE_NO_CHILDREN:
        amark_node_destroy(s->raw);
        break;

    default:
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_before(s->list, s->code_block));
        break;
    }
}

/* Checks the result of the comparison of a and b (each evaluated once) without and
 * with normalization, in both orders. */
#define CHECK_TEXT(a, b, plain, normalized) \
    do { \
        const amark_node_t *check_a_ = (a); \
        const amark_node_t *check_b_ = (b); \
        NAOS_CHECK_INT_EQ((plain), amark_node_equal(check_a_, check_b_, 0)); \
        NAOS_CHECK_INT_EQ((plain), amark_node_equal(check_b_, check_a_, 0)); \
        NAOS_CHECK_INT_EQ((normalized), \
                          amark_node_equal(check_a_, check_b_, AMARK_EQUAL_NORMALIZE_TEXT)); \
        NAOS_CHECK_INT_EQ((normalized), \
                          amark_node_equal(check_b_, check_a_, AMARK_EQUAL_NORMALIZE_TEXT)); \
    } while (0)

/* Checks that a and b are equal, or unequal, the same with and without
 * normalization, in both orders. */
#define CHECK_EQUAL(a, b, expected) CHECK_TEXT((a), (b), (expected), (expected))

/* --- Suite equal ---------------------------------------------------------------- */

NAOS_TEST(two_copies_of_the_sample_are_equal)
{
    sample_t a;
    sample_t b;

    build_sample(&a, NULL, false);
    build_sample(&b, NULL, false);
    CHECK_EQUAL(a.root, b.root, true);
    CHECK_EQUAL(a.root, a.root, true);
    CHECK_EQUAL(a.heading, b.heading, true);
    CHECK_EQUAL(a.box, b.box, true);
    /* Subtrees of different types, and a subtree and the whole tree. */
    CHECK_EQUAL(a.heading, b.list, false);
    CHECK_EQUAL(a.root, b.heading, false);
    amark_document_destroy(a.document);
    amark_document_destroy(b.document);
}

NAOS_TEST(the_tokens_records_and_associated_pointers_are_ignored)
{
    static const char text[] = "## Title *em*\n";
    sample_t a;
    sample_t b;
    amark_source_id_t source = 0;
    int marker = 0;

    build_sample(&a, NULL, false);
    build_sample(&b, NULL, true);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_add_source(b.document, text, strlen(text), &source));
    add_token(b.heading, AMARK_TOKEN_MARKER, source, 0, 2);
    add_token(b.title, AMARK_TOKEN_TEXT, source, 3, 5);
    add_token(b.emph, AMARK_TOKEN_MARKER, source, 9, 1);
    amark_node_set_user_data(b.box, &marker);
    amark_node_set_user_data(b.root, &marker);
    amark_node_mark_edited(b.rule);
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_CREATED | AMARK_EDIT_CONTENT, amark_node_edits(b.rule));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(a.rule));
    CHECK_EQUAL(a.root, b.root, true);
    amark_document_destroy(a.document);
    amark_document_destroy(b.document);
}

NAOS_TEST(each_change_makes_the_trees_unequal)
{
    int first_equal = -1;
    int change;

    for (change = 0; change < CHANGE_COUNT; change++)
    {
        sample_t a;
        sample_t b;

        build_sample(&a, NULL, false);
        build_sample(&b, NULL, false);
        apply_change(&b, change);
        if ((first_equal < 0)
            && (amark_node_equal(a.root, b.root, 0) || amark_node_equal(b.root, a.root, 0)
                || amark_node_equal(a.root, b.root, AMARK_EQUAL_NORMALIZE_TEXT)
                || amark_node_equal(b.root, a.root, AMARK_EQUAL_NORMALIZE_TEXT)))
        {
            first_equal = change;
        }

        amark_document_destroy(a.document);
        amark_document_destroy(b.document);
    }

    /* The first change that left the trees equal (-1 for none). */
    NAOS_CHECK_INT_EQ(-1, first_equal);
}

NAOS_TEST(the_start_of_a_bullet_list_is_ignored)
{
    sample_t a;
    sample_t b;

    build_sample(&a, NULL, false);
    build_sample(&b, NULL, false);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_list_type(a.list, AMARK_LIST_BULLET));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_list_type(b.list, AMARK_LIST_BULLET));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_list_start(b.list, 9));
    CHECK_EQUAL(a.root, b.root, true);
    amark_document_destroy(a.document);
    amark_document_destroy(b.document);
}

NAOS_TEST(the_type_data_uses_the_equal_function_or_the_bytes)
{
    sample_t a;
    sample_t b;

    build_sample(&a, NULL, false);
    build_sample(&b, NULL, false);
    box_equal_calls = 0;
    NAOS_CHECK_TRUE(amark_node_equal(a.box, b.box, 0));
    NAOS_CHECK_UINT_EQ(1, box_equal_calls);

    /* The bytes of the name pointers differ; only the function compares them. */
    NAOS_CHECK_TRUE(((box_data_t *)amark_node_data(a.box))->name
                    != ((box_data_t *)amark_node_data(b.box))->name);

    /* x.raw: every byte counts. */
    ((raw_data_t *)amark_node_data(b.raw))->number = 8;
    NAOS_CHECK_FALSE(amark_node_equal(a.raw, b.raw, 0));
    ((raw_data_t *)amark_node_data(b.raw))->number = 7;
    NAOS_CHECK_TRUE(amark_node_equal(a.raw, b.raw, 0));
    ((raw_data_t *)amark_node_data(b.raw))->bytes[0] = 0xffu;
    NAOS_CHECK_FALSE(amark_node_equal(a.raw, b.raw, 0));

    /* A box with no name against a named one. */
    box_equal_calls = 0;
    NAOS_CHECK_FALSE(amark_node_equal(add(a.document, NULL, type_of(a.document, &box_type)), b.box,
                                      0));
    NAOS_CHECK_UINT_EQ(1, box_equal_calls);
    amark_document_destroy(a.document);
    amark_document_destroy(b.document);
}

NAOS_TEST(the_same_descriptors_in_other_syntax_sets_are_the_same_types)
{
    amark_syntax_set_t *plain = create_set(false);
    amark_syntax_set_t *padded = create_set(true);
    sample_t a;
    sample_t b;
    sample_t c;
    amark_document_t *core = NULL;
    amark_node_t *x;
    amark_node_t *y;

    build_sample(&a, plain, false);
    build_sample(&b, padded, false);
    build_sample(&c, plain, true);
    NAOS_CHECK_TRUE(amark_node_get_type(a.box) != amark_node_get_type(b.box));
    CHECK_EQUAL(a.root, b.root, true);
    CHECK_EQUAL(a.root, c.root, true);

    /* A built-in subtree in a document of the core set. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &core));
    x = add(core, add(core, amark_document_root(core), AMARK_NODE_PARAGRAPH), AMARK_NODE_EMPH);
    (void)add_text(core, x, "em");
    CHECK_EQUAL(x, a.emph, true);

    /* A type of another descriptor is another type. */
    x = add(b.document, NULL, type_of(b.document, &pad_type));
    y = add(a.document, NULL, AMARK_NODE_THEMATIC_BREAK);
    CHECK_EQUAL(x, y, false);
    amark_document_destroy(a.document);
    amark_document_destroy(b.document);
    amark_document_destroy(c.document);
    amark_document_destroy(core);
    amark_syntax_set_destroy(plain);
    amark_syntax_set_destroy(padded);
}

NAOS_TEST(null_is_never_equal_and_unknown_flags_are_ignored)
{
    sample_t a;
    sample_t b;

    build_sample(&a, NULL, false);
    build_sample(&b, NULL, false);
    NAOS_CHECK_FALSE(amark_node_equal(NULL, NULL, 0));
    NAOS_CHECK_FALSE(amark_node_equal(a.root, NULL, 0));
    NAOS_CHECK_FALSE(amark_node_equal(NULL, a.root, AMARK_EQUAL_NORMALIZE_TEXT));
    NAOS_CHECK_TRUE(amark_node_equal(a.root, b.root, 0xfffffffeu));
    NAOS_CHECK_TRUE(amark_node_equal(a.root, b.root, 0xffffffffu));
    apply_change(&b, CHANGE_TEXT_LITERAL);
    NAOS_CHECK_FALSE(amark_node_equal(a.root, b.root, 0xfffffffeu));
    amark_document_destroy(a.document);
    amark_document_destroy(b.document);
}

/* Builds a chain of count block quotes under the root of document, with a
 * thematic break, or a text in a paragraph, at the bottom. */
static void build_chain(amark_document_t *document, size_t count, bool rule_at_bottom)
{
    amark_node_t *node = amark_document_root(document);
    size_t i;

    for (i = 0; i < count; i++)
    {
        node = add(document, node, AMARK_NODE_BLOCK_QUOTE);
    }

    if (rule_at_bottom)
    {
        (void)add(document, node, AMARK_NODE_THEMATIC_BREAK);
    }
    else
    {
        (void)add_text(document, add(document, node, AMARK_NODE_PARAGRAPH), "x");
    }
}

NAOS_TEST(a_deep_chain_is_compared_without_recursion)
{
    amark_document_t *a = NULL;
    amark_document_t *b = NULL;
    amark_document_t *c = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &a));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &b));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &c));
    build_chain(a, 100000, true);
    build_chain(b, 100000, true);
    build_chain(c, 100000, false);
    /* A sibling after the chain is compared after climbing back. */
    (void)add(a, amark_document_root(a), AMARK_NODE_THEMATIC_BREAK);
    (void)add(b, amark_document_root(b), AMARK_NODE_THEMATIC_BREAK);
    CHECK_EQUAL(amark_document_root(a), amark_document_root(b), true);
    CHECK_EQUAL(amark_document_root(a), amark_document_root(c), false);
    (void)add(b, amark_document_root(b), AMARK_NODE_THEMATIC_BREAK);
    CHECK_EQUAL(amark_document_root(a), amark_document_root(b), false);
    amark_document_destroy(a);
    amark_document_destroy(b);
    amark_document_destroy(c);
}

NAOS_TEST(no_comparison_allocates)
{
    static amark_test_allocator_t t;
    sample_t a;
    sample_t b;
    bool results[6];

    build_sample(&a, NULL, false);
    build_sample(&b, NULL, true);
    (void)add_text(b.document, b.emph, "");
    amark_test_allocator_init(&t);
    amark_allocator_set(amark_test_allocator_activate(&t));
    results[0] = amark_node_equal(a.root, b.root, 0);
    results[1] = amark_node_equal(a.root, b.root, AMARK_EQUAL_NORMALIZE_TEXT);
    results[2] = amark_node_equal(a.root, a.root, 0);
    results[3] = amark_node_equal(a.box, b.box, 0);
    results[4] = amark_node_equal(a.heading, b.heading, AMARK_EQUAL_NORMALIZE_TEXT);
    results[5] = amark_node_equal(a.list, b.list, AMARK_EQUAL_NORMALIZE_TEXT);
    amark_allocator_set(NULL);
    NAOS_CHECK_UINT_EQ(0, t.calls);
    NAOS_CHECK_FALSE(results[0]);
    NAOS_CHECK_TRUE(results[1]);
    NAOS_CHECK_TRUE(results[2]);
    NAOS_CHECK_TRUE(results[3]);
    NAOS_CHECK_TRUE(results[4]);
    NAOS_CHECK_TRUE(results[5]);
    amark_document_destroy(a.document);
    amark_document_destroy(b.document);
}

NAOS_SUITE(equal,
    NAOS_SET_TEARDOWN(restore),
    NAOS_ADD_TEST(two_copies_of_the_sample_are_equal),
    NAOS_ADD_TEST(the_tokens_records_and_associated_pointers_are_ignored),
    NAOS_ADD_TEST(each_change_makes_the_trees_unequal),
    NAOS_ADD_TEST(the_start_of_a_bullet_list_is_ignored),
    NAOS_ADD_TEST(the_type_data_uses_the_equal_function_or_the_bytes),
    NAOS_ADD_TEST(the_same_descriptors_in_other_syntax_sets_are_the_same_types),
    NAOS_ADD_TEST(null_is_never_equal_and_unknown_flags_are_ignored),
    NAOS_ADD_TEST(a_deep_chain_is_compared_without_recursion),
    NAOS_ADD_TEST(no_comparison_allocates))

/* --- Suite equal_text ----------------------------------------------------------- */

/* Children of a paragraph: a text for each string, an emph for "*" followed by a
 * text with the next string, a softbreak for "\n" and an empty text for "". */
static amark_node_t *paragraph_of(amark_document_t *document, const char *const *parts,
                                  size_t count)
{
    amark_node_t *paragraph = add(document, NULL, AMARK_NODE_PARAGRAPH);
    size_t i;

    for (i = 0; i < count; i++)
    {
        if (strcmp(parts[i], "*") == 0)
        {
            amark_node_t *emph = add(document, paragraph, AMARK_NODE_EMPH);

            i++;
            (void)add_text(document, emph, parts[i]);
        }
        else if (strcmp(parts[i], "\n") == 0)
        {
            (void)add(document, paragraph, AMARK_NODE_SOFTBREAK);
        }
        else
        {
            (void)add_text(document, paragraph, parts[i]);
        }
    }

    return paragraph;
}

#define PARAGRAPH(document, ...) \
    paragraph_of((document), (const char *const[]){ __VA_ARGS__ }, \
                 sizeof((const char *const[]){ __VA_ARGS__ }) / sizeof(const char *))

NAOS_TEST(text_split_differently_is_equal_only_when_normalized)
{
    amark_document_t *d = NULL;
    amark_document_t *e = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &d));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &e));
    CHECK_TEXT(PARAGRAPH(d, "ab"), PARAGRAPH(e, "ab"), true, true);
    CHECK_TEXT(PARAGRAPH(d, "ab"), PARAGRAPH(e, "a", "b"), false, true);
    CHECK_TEXT(PARAGRAPH(d, "a", "bc"), PARAGRAPH(e, "ab", "c"), false, true);
    CHECK_TEXT(PARAGRAPH(d, "abcdef"), PARAGRAPH(e, "a", "bcd", "e", "f"), false, true);
    /* Different bytes, or a run that is longer, stay unequal. */
    CHECK_TEXT(PARAGRAPH(d, "ab"), PARAGRAPH(e, "a", "c"), false, false);
    CHECK_TEXT(PARAGRAPH(d, "ab"), PARAGRAPH(e, "a", "b", "c"), false, false);
    CHECK_TEXT(PARAGRAPH(d, "abc"), PARAGRAPH(e, "a", "b"), false, false);
    /* The nodes after the runs are compared in order. */
    CHECK_TEXT(PARAGRAPH(d, "a", "b", "*", "c", "d", "\n", "e"),
               PARAGRAPH(e, "ab", "*", "c", "d", "\n", "e"), false, true);
    CHECK_TEXT(PARAGRAPH(d, "a", "b", "*", "c", "d"), PARAGRAPH(e, "ab", "*", "c", "d", "\n"),
               false, false);
    /* Inside a child too. */
    CHECK_TEXT(PARAGRAPH(d, "*", "cd"), PARAGRAPH(e, "*", "cd"), true, true);
    amark_document_destroy(d);
    amark_document_destroy(e);
}

NAOS_TEST(runs_do_not_join_across_other_nodes)
{
    amark_document_t *d = NULL;
    amark_document_t *e = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &d));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &e));
    CHECK_TEXT(PARAGRAPH(d, "ab", "\n"), PARAGRAPH(e, "a", "\n", "b"), false, false);
    CHECK_TEXT(PARAGRAPH(d, "a", "\n", "b"), PARAGRAPH(e, "a", "\n", "b"), true, true);
    CHECK_TEXT(PARAGRAPH(d, "a", "*", "x", "b"), PARAGRAPH(e, "a", "*", "x", "", "b"), false,
               true);
    CHECK_TEXT(PARAGRAPH(d, "a", "*", "x", "b"), PARAGRAPH(e, "ab", "*", "x"), false, false);
    /* The text of a child does not join the text of its parent. */
    CHECK_TEXT(PARAGRAPH(d, "a", "*", "b"), PARAGRAPH(e, "a", "b"), false, false);
    amark_document_destroy(d);
    amark_document_destroy(e);
}

NAOS_TEST(empty_text_is_ignored_when_normalized)
{
    amark_document_t *d = NULL;
    amark_document_t *e = NULL;
    amark_node_t *empty_paragraph;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &d));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &e));
    empty_paragraph = add(d, NULL, AMARK_NODE_PARAGRAPH);
    CHECK_TEXT(empty_paragraph, PARAGRAPH(e, ""), false, true);
    CHECK_TEXT(empty_paragraph, PARAGRAPH(e, "", ""), false, true);
    CHECK_TEXT(empty_paragraph, PARAGRAPH(e, "", "x"), false, false);
    CHECK_TEXT(PARAGRAPH(d, "x"), PARAGRAPH(e, "", "x", ""), false, true);
    CHECK_TEXT(PARAGRAPH(d, "a", "b"), PARAGRAPH(e, "a", "", "b"), false, true);
    CHECK_TEXT(PARAGRAPH(d, "\n", "\n"), PARAGRAPH(e, "", "\n", "", "", "\n", ""), false, true);
    CHECK_TEXT(PARAGRAPH(d, "*", "a", "*", "b"), PARAGRAPH(e, "*", "a", "", "*", "b"), false,
               true);
    CHECK_TEXT(PARAGRAPH(d, "*", ""), PARAGRAPH(e, "*", "", ""), false, true);
    /* An empty text in an emph that is otherwise empty. */
    CHECK_TEXT(PARAGRAPH(d, "*", ""), PARAGRAPH(e, "\n"), false, false);
    /* Empty text roots compare by their literals. */
    CHECK_TEXT(add_text(d, NULL, NULL), add_text(e, NULL, ""), true, true);
    CHECK_TEXT(add_text(d, NULL, NULL), add_text(e, NULL, "a"), false, false);
    amark_document_destroy(d);
    amark_document_destroy(e);
}

NAOS_SUITE(equal_text,
    NAOS_SET_TEARDOWN(restore),
    NAOS_ADD_TEST(text_split_differently_is_equal_only_when_normalized),
    NAOS_ADD_TEST(runs_do_not_join_across_other_nodes),
    NAOS_ADD_TEST(empty_text_is_ignored_when_normalized))

NAOS_MAIN(
    NAOS_ADD_SUITE(equal),
    NAOS_ADD_SUITE(equal_text)
)
