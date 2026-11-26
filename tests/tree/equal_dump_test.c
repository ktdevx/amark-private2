/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The semantic comparison (src/tree/equal.c) and the dump (src/tree/dump.c), through
 * the public API (the internal struct only to set unknown edit record bits).
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
 * order) and ignores empty text nodes; a code with the literal of a text is not
 * text.
 * Suite dump (9.3, 9.4, 9.5, 7.10, 4.9): the example of the design, every built-in
 * attribute in order with the extension attributes, the same text for the same
 * tree (appended after the content of the buffer), each string escape (valid UTF-8
 * kept), the tokens with their flags, role, source and tab columns, the spans, the
 * edit records with unknown bits as bitN, unknown flag bits ignored, NULL arguments
 * and a chain of 10000 nodes.
 * Suite dump_writer (9.6, 8.12): the failure of a dump function is returned and the
 * buffer gets back its length; invalid attribute names, symbols and strings fail the
 * writer, which keeps the failure for every later write and for the dump; each
 * value kind at its limits; the out-of-memory sweep of the dump.
 */
#include "tree/tree.h"

#include <amark/allocator.h>
#include <amark/buffer.h>
#include <amark/document.h>
#include <amark/dump.h>
#include <amark/error.h>
#include <amark/extension.h>
#include <amark/node.h>
#include <amark/token.h>

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
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

/* What box_dump does (the dump suites change it; DUMP_NAME by default). */
enum
{
    /* Writes :name with the name. */
    DUMP_NAME,
    /* Writes :name, then fails with AMARK_ERROR_INVALID_STATE. */
    DUMP_FAIL_AFTER_NAME,
    /* Writes an attribute with the name dump_bad_name, ignores the failure and
     * returns AMARK_ERROR_NONE. */
    DUMP_BAD_NAME_IGNORED,
    /* Writes a symbol dump_bad_name, ignores the failure and returns
     * AMARK_ERROR_NONE. */
    DUMP_BAD_SYMBOL_IGNORED,
    /* Writes :bytes with the dump_bytes. */
    DUMP_BYTES
};

static int dump_mode = DUMP_NAME;
static const char *dump_bad_name;
static const char *dump_bytes;
static size_t dump_bytes_length;
/* The results of the writer calls of box_dump in the last dump. */
static amark_error_t dump_results[3];

static amark_error_t box_dump(const amark_node_t *node, const void *data,
                              amark_dump_writer_t *writer)
{
    const char *name = ((const box_data_t *)data)->name;

    (void)node;
    switch (dump_mode)
    {
    case DUMP_FAIL_AFTER_NAME:
        dump_results[0] = amark_dump_attr_string(writer, "name", name, strlen(name));
        return AMARK_ERROR_INVALID_STATE;

    case DUMP_BAD_NAME_IGNORED:
        dump_results[0] = amark_dump_attr_int(writer, dump_bad_name, 1);
        /* The writer has failed: every later write returns the failure. */
        dump_results[1] = amark_dump_attr_int(writer, "after", 2);
        return AMARK_ERROR_NONE;

    case DUMP_BAD_SYMBOL_IGNORED:
        dump_results[0] = amark_dump_attr_symbol(writer, "align", dump_bad_name);
        dump_results[1] = amark_dump_attr_bool(writer, "after", true);
        return AMARK_ERROR_NONE;

    case DUMP_BYTES:
        return amark_dump_attr_string(writer, "bytes", dump_bytes, dump_bytes_length);

    default:
        return amark_dump_attr_string(writer, "name", name, (name != NULL) ? strlen(name) : 0);
    }
}

/* Writes the number of a raw node and the other value kinds of the writer. */
static amark_error_t raw_dump(const amark_node_t *node, const void *data,
                              amark_dump_writer_t *writer)
{
    const raw_data_t *raw = data;
    amark_error_t err;

    (void)node;
    err = amark_dump_attr_int(writer, "number", (long long)raw->number);
    if (err == AMARK_ERROR_NONE)
    {
        err = amark_dump_attr_bool(writer, "odd", ((raw->number % 2u) == 1u));
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = amark_dump_attr_symbol(writer, "align", "left_2");
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = amark_dump_attr_string(writer, "empty", NULL, 0);
    }

    return err;
}

static const amark_node_type_desc_t box_type = {
    sizeof(amark_node_type_desc_t), "x.box", AMARK_NODE_LEVEL_BLOCK, AMARK_CONTENT_BLOCK,
    AMARK_CONTENT_BLOCK, AMARK_LITERAL_NONE, sizeof(box_data_t), NULL, NULL, NULL, box_deinit,
    NULL, box_equal, box_dump, NULL
};

static const amark_node_type_desc_t raw_type = {
    sizeof(amark_node_type_desc_t), "x.raw", AMARK_NODE_LEVEL_BLOCK, AMARK_CONTENT_BLOCK, 0,
    AMARK_LITERAL_NONE, sizeof(raw_data_t), NULL, NULL, NULL, NULL, NULL, NULL, raw_dump, NULL
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

/* Creates a code node with literal and appends it to parent. */
static amark_node_t *add_code(amark_document_t *document, amark_node_t *parent,
                              const char *literal)
{
    amark_node_t *node = add(document, parent, AMARK_NODE_CODE);

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_literal(node, literal, strlen(literal)));

    return node;
}

NAOS_TEST(a_code_with_the_literal_of_a_text_is_not_text)
{
    amark_document_t *d = NULL;
    amark_document_t *e = NULL;
    amark_node_t *a;
    amark_node_t *b;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &d));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &e));

    /* As roots, and as the only child. */
    CHECK_TEXT(add_text(d, NULL, "ab"), add_code(e, NULL, "ab"), false, false);
    a = PARAGRAPH(d, "ab");
    b = add(e, NULL, AMARK_NODE_PARAGRAPH);
    (void)add_code(e, b, "ab");
    CHECK_TEXT(a, b, false, false);

    /* A code is not part of a run of text: "a" + code "b" is not the run "ab". */
    a = PARAGRAPH(d, "ab");
    b = PARAGRAPH(e, "a");
    (void)add_code(e, b, "b");
    CHECK_TEXT(a, b, false, false);

    /* Nor between two texts. */
    a = PARAGRAPH(d, "a", "b", "c");
    b = PARAGRAPH(e, "a");
    (void)add_code(e, b, "b");
    (void)add_text(e, b, "c");
    CHECK_TEXT(a, b, false, false);
    amark_document_destroy(d);
    amark_document_destroy(e);
}

NAOS_SUITE(equal_text,
    NAOS_SET_TEARDOWN(restore),
    NAOS_ADD_TEST(text_split_differently_is_equal_only_when_normalized),
    NAOS_ADD_TEST(runs_do_not_join_across_other_nodes),
    NAOS_ADD_TEST(empty_text_is_ignored_when_normalized),
    NAOS_ADD_TEST(a_code_with_the_literal_of_a_text_is_not_text))

/* --- Suites dump and dump_writer ----------------------------------------------- */

NAOS_TEARDOWN(restore_dump)
{
    amark_allocator_set(NULL);
    dump_mode = DUMP_NAME;
    dump_bad_name = NULL;
    dump_bytes = NULL;
    dump_bytes_length = 0;
}

/* Dumps node with flags into a new buffer and checks that it succeeds. */
static amark_buffer_t *dump_of(const amark_node_t *node, uint32_t flags)
{
    amark_buffer_t *out = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_create(&out));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_dump(node, flags, out));

    return out;
}

/* Checks that the dump of node with flags is expected (a NUL-terminated string). */
#define CHECK_DUMP(node, flags, expected) \
    do { \
        amark_buffer_t *check_out_ = dump_of((node), (flags)); \
        NAOS_CHECK_STR_EQ((expected), amark_buffer_data(check_out_)); \
        NAOS_CHECK_UINT_EQ(strlen(expected), amark_buffer_length(check_out_)); \
        amark_buffer_destroy(check_out_); \
    } while (0)

/* The dump of the sample of build_sample. */
static const char sample_dump[] =
    "(document\n"
    "  (heading :level 2\n"
    "    (text \"Title\")\n"
    "    (emph\n"
    "      (text \"em\")))\n"
    "  (list :type ordered :start 3 :tight true\n"
    "    (item\n"
    "      (paragraph\n"
    "        (text \"x\")\n"
    "        (link :destination \"d\" :title \"t\"\n"
    "          (text \"l\"))\n"
    "        (image :destination \"i\" :title \"it\"\n"
    "          (text \"alt\")))))\n"
    "  (code_block :info \"c\" \"code\\n\")\n"
    "  (link_definition :label \"L\" :destination \"u\" :title \"t2\")\n"
    "  (x.box :name \"outer\"\n"
    "    (x.raw :number 7 :odd true :align left_2 :empty \"\"))\n"
    "  (thematic_break))\n";

NAOS_TEST(the_example_of_the_design_is_reproduced)
{
    amark_document_t *document = NULL;
    amark_node_t *root;
    amark_node_t *heading;
    amark_node_t *list;
    amark_node_t *paragraph;
    amark_node_t *code;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    root = amark_document_root(document);
    heading = add(document, root, AMARK_NODE_HEADING);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_heading_level(heading, 2));
    (void)add_text(document, heading, "Hello");
    list = add(document, root, AMARK_NODE_LIST);
    /* The start of a bullet list is not written. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_list_start(list, 5));
    paragraph = add(document, add(document, list, AMARK_NODE_ITEM), AMARK_NODE_PARAGRAPH);
    (void)add_text(document, paragraph, "a");
    (void)add(document, paragraph, AMARK_NODE_SOFTBREAK);
    code = add(document, paragraph, AMARK_NODE_CODE);
    SET_STRING(amark_node_set_literal, code, "x");
    CHECK_DUMP(root, 0,
               "(document\n"
               "  (heading :level 2\n"
               "    (text \"Hello\"))\n"
               "  (list :type bullet :tight true\n"
               "    (item\n"
               "      (paragraph\n"
               "        (text \"a\")\n"
               "        (softbreak)\n"
               "        (code \"x\")))))\n");
    /* A subtree is dumped from its own depth, and a leaf alone. */
    CHECK_DUMP(heading, 0, "(heading :level 2\n  (text \"Hello\"))\n");
    CHECK_DUMP(code, 0, "(code \"x\")\n");
    amark_document_destroy(document);
}

NAOS_TEST(every_builtin_attribute_and_the_extension_attributes_are_written)
{
    sample_t s;
    amark_node_t *empty;

    build_sample(&s, NULL, false);
    CHECK_DUMP(s.root, 0, sample_dump);
    /* The defaults: an empty literal and empty strings, a loose list. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_list_tight(s.list, false));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_list_start(s.list, 0));
    CHECK_DUMP(s.list, 0,
               "(list :type ordered :start 0 :tight false\n"
               "  (item\n"
               "    (paragraph\n"
               "      (text \"x\")\n"
               "      (link :destination \"d\" :title \"t\"\n"
               "        (text \"l\"))\n"
               "      (image :destination \"i\" :title \"it\"\n"
               "        (text \"alt\")))))\n");
    empty = add(s.document, NULL, AMARK_NODE_LINK_DEFINITION);
    CHECK_DUMP(empty, 0, "(link_definition :label \"\" :destination \"\" :title \"\")\n");
    empty = add(s.document, NULL, AMARK_NODE_CODE_BLOCK);
    CHECK_DUMP(empty, 0, "(code_block :info \"\" \"\")\n");
    empty = add(s.document, NULL, AMARK_NODE_TEXT);
    CHECK_DUMP(empty, 0, "(text \"\")\n");
    empty = add(s.document, NULL, AMARK_NODE_HTML_INLINE);
    SET_STRING(amark_node_set_literal, empty, "<a\r\nb>");
    CHECK_DUMP(empty, 0, "(html_inline \"<a\\nb>\")\n");
    amark_document_destroy(s.document);
}

NAOS_TEST(the_same_tree_always_gives_the_same_dump)
{
    sample_t a;
    sample_t b;
    amark_buffer_t *out = NULL;
    size_t length;

    build_sample(&a, NULL, true);
    build_sample(&b, NULL, true);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_create(&out));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_cstr(out, "prefix"));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_dump(a.root, 0, out));
    length = amark_buffer_length(out);
    /* The dump is appended after the content already there, then again the same
     * text, and a tree built the same way in another document gives it too. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_dump(a.root, 0, out));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_dump(b.root, 0, out));
    NAOS_CHECK_UINT_EQ(6 + (3 * (sizeof(sample_dump) - 1)), amark_buffer_length(out));
    NAOS_CHECK_MEM_EQ(amark_buffer_data(out), 6, "prefix", 6);
    NAOS_CHECK_MEM_EQ(amark_buffer_data(out) + 6, length - 6, sample_dump, sizeof(sample_dump) - 1);
    NAOS_CHECK_MEM_EQ(amark_buffer_data(out) + length, length - 6, sample_dump,
                      sizeof(sample_dump) - 1);
    NAOS_CHECK_MEM_EQ(amark_buffer_data(out) + (2 * length) - 6, length - 6, sample_dump,
                      sizeof(sample_dump) - 1);
    amark_buffer_destroy(out);
    /* With every flag too (the records, the same in both documents). */
    out = dump_of(a.root, AMARK_DUMP_TOKENS | AMARK_DUMP_EDITS | AMARK_DUMP_SPANS);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_dump(b.root,
                                        AMARK_DUMP_TOKENS | AMARK_DUMP_EDITS | AMARK_DUMP_SPANS,
                                        out));
    length = amark_buffer_length(out) / 2;
    NAOS_CHECK_MEM_EQ(amark_buffer_data(out), length, amark_buffer_data(out) + length, length);
    amark_buffer_destroy(out);
    amark_document_destroy(a.document);
    amark_document_destroy(b.document);
}

NAOS_TEST(strings_are_escaped)
{
    static const char bytes[] =
        "q\"b\\n\nr\rt\tz\0"
        "\x01\x1f\x7f"
        " ~\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80\xef\xbf\xbd"
        "|\xff|\x80|\xc3|\xe2\x82|\xed\xa0\x80|\xc0\xaf|\xf4\x90\x80\x80|\xf0\x9f\x98";
    sample_t s;
    amark_node_t *code_block;

    build_sample(&s, NULL, false);
    dump_mode = DUMP_BYTES;
    dump_bytes = bytes;
    dump_bytes_length = sizeof(bytes) - 1;
    amark_node_destroy(s.raw);
    CHECK_DUMP(s.box, 0,
               "(x.box :bytes \"q\\\"b\\\\n\\nr\\rt\\tz\\x00\\x01\\x1f\\x7f"
               " ~\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80\xef\xbf\xbd"
               "|\\xff|\\x80|\\xc3|\\xe2\\x82|\\xed\\xa0\\x80|\\xc0\\xaf|\\xf4\\x90\\x80\\x80"
               "|\\xf0\\x9f\\x98\")\n");
    /* The literals and the built-in string attributes are escaped the same way. */
    code_block = add(s.document, NULL, AMARK_NODE_CODE_BLOCK);
    SET_STRING(amark_node_set_code_info, code_block, "a\"\\\t");
    SET_STRING(amark_node_set_literal, code_block, "\t\"x\"\\\x01\r\n\x7f");
    CHECK_DUMP(code_block, 0,
               "(code_block :info \"a\\\"\\\\\\t\" \"\\t\\\"x\\\"\\\\\\x01\\n\\x7f\\n\")\n");
    amark_document_destroy(s.document);
}

/* Adds a token with every field to node. */
static void add_full_token(amark_node_t *node, amark_token_kind_t kind, amark_source_id_t source,
                           size_t offset, size_t length, uint32_t flags, uint16_t role,
                           unsigned tab_offset, unsigned tab_columns)
{
    amark_token_t token;

    memset(&token, 0, sizeof(token));
    token.struct_size = sizeof(token);
    token.kind = kind;
    token.role = role;
    token.flags = flags;
    token.source = source;
    token.offset = offset;
    token.length = length;
    token.tab_column_offset = tab_offset;
    token.tab_columns = tab_columns;
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_add_token(node, &token));
}

NAOS_TEST(tokens_and_spans_are_written_when_asked)
{
    amark_document_t *document = NULL;
    amark_source_id_t first = 9;
    amark_source_id_t second = 9;
    amark_node_t *root;
    amark_node_t *heading;
    amark_node_t *text;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_add_source(document, "# Hi\n", 5, &first));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_add_source(document, "\tx", 2, &second));
    NAOS_REQUIRE_UINT_EQ(0, first);
    NAOS_REQUIRE_UINT_EQ(1, second);
    root = amark_document_root(document);
    heading = add(document, root, AMARK_NODE_HEADING);
    text = add_text(document, heading, "Hi");
    (void)add(document, root, AMARK_NODE_THEMATIC_BREAK);
    add_full_token(heading, AMARK_TOKEN_SPACE, second, 0, 1,
                   AMARK_TOKEN_FLAG_CONTAINER_PREFIX | AMARK_TOKEN_FLAG_PARTIAL_TAB, 0, 1, 2);
    add_full_token(heading, AMARK_TOKEN_MARKER, first, 0, 1, 0, 0, 0, 0);
    add_full_token(heading, AMARK_TOKEN_SPACE, first, 1, 1, 0, 3, 0, 0);
    add_full_token(heading, AMARK_TOKEN_LINE_ENDING, first, 4, 1, AMARK_TOKEN_FLAG_CONTAINER_PREFIX,
                   0, 0, 0);
    add_full_token(heading, AMARK_TOKEN_SPACE, second, 0, 1, AMARK_TOKEN_FLAG_PARTIAL_TAB, 65535,
                   3, 1);
    add_token(text, AMARK_TOKEN_TEXT, first, 2, 2);

    CHECK_DUMP(root, AMARK_DUMP_TOKENS,
               "(document\n"
               "  (heading :level 1 :tokens ((marker 0 1) (space 1 1 :role 3)"
               " (line_ending 4 1 prefix) (space 0 1 prefix partial_tab :source 1 :tab 1 2)"
               " (space 0 1 partial_tab :role 65535 :source 1 :tab 3 1))\n"
               "    (text :tokens ((text 2 2)) \"Hi\"))\n"
               "  (thematic_break))\n");
    /* The span of a node covers the tokens of its subtree in the source of its
     * first token; a subtree without tokens has none. */
    CHECK_DUMP(root, AMARK_DUMP_SPANS,
               "(document :span (1 1 2 1)\n"
               "  (heading :level 1 :span (1 1 2 1)\n"
               "    (text :span (1 3 1 5) \"Hi\"))\n"
               "  (thematic_break))\n");
    CHECK_DUMP(text, AMARK_DUMP_TOKENS | AMARK_DUMP_SPANS | AMARK_DUMP_EDITS,
               "(text :span (1 3 1 5) :tokens ((text 2 2)) \"Hi\")\n");
    /* Unknown flag bits are ignored. */
    CHECK_DUMP(text, 0xfffffff8u, "(text \"Hi\")\n");
    CHECK_DUMP(text, 0xffffffffu, "(text :span (1 3 1 5) :tokens ((text 2 2)) \"Hi\")\n");
    amark_document_destroy(document);
}

NAOS_TEST(edit_records_are_written_when_asked)
{
    amark_document_t *document = NULL;
    amark_node_t *root;
    amark_node_t *heading;
    amark_node_t *text;
    amark_node_t *rule;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    root = amark_document_root(document);
    rule = add(document, root, AMARK_NODE_THEMATIC_BREAK);
    amark_document_set_edit_tracking(document, true);
    heading = add(document, root, AMARK_NODE_HEADING);
    text = add_text(document, heading, "Hi");
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_after(heading, rule));
    CHECK_DUMP(root, AMARK_DUMP_EDITS,
               "(document :edits (children descendants)\n"
               "  (heading :level 1 :edits (children descendants created)\n"
               "    (text :edits (content created) \"Hi\"))\n"
               "  (thematic_break :edits (relocated)))\n");
    /* Without the flag, and for a node without records, nothing is written. */
    CHECK_DUMP(root, 0,
               "(document\n"
               "  (heading :level 1\n"
               "    (text \"Hi\"))\n"
               "  (thematic_break))\n");
    /* Every known bit in order, then the unknown bits by number. */
    text->edits = 0x8000003fu;
    rule->edits = 0;
    CHECK_DUMP(text, AMARK_DUMP_EDITS,
               "(text :edits (content children descendants relocated created bit5 bit31)"
               " \"Hi\")\n");
    text->edits = 0x00000100u;
    CHECK_DUMP(text, AMARK_DUMP_EDITS, "(text :edits (bit8) \"Hi\")\n");
    CHECK_DUMP(rule, AMARK_DUMP_EDITS, "(thematic_break)\n");
    amark_document_destroy(document);
}

NAOS_TEST(null_arguments_are_rejected)
{
    amark_document_t *document = NULL;
    amark_buffer_t *out = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_create(&out));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_dump(NULL, 0, out));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_dump(amark_document_root(document), 0, NULL));
    NAOS_CHECK_UINT_EQ(0, amark_buffer_length(out));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_dump_attr_string(NULL, "a", "b", 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_dump_attr_int(NULL, "a", 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_dump_attr_bool(NULL, "a", true));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_dump_attr_symbol(NULL, "a", "b"));
    amark_buffer_destroy(out);
    amark_document_destroy(document);
}

NAOS_TEST(a_deep_chain_is_dumped_without_recursion)
{
    enum { DEPTH = 10000 };
    amark_document_t *document = NULL;
    amark_buffer_t *out;
    const char *data;
    size_t expected = 0;
    size_t length;
    size_t depth;
    size_t closing;
    size_t i;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    build_chain(document, DEPTH, false);
    out = dump_of(amark_document_root(document), 0);
    /* "(document" ")" LF, each block quote "\n" indent "(block_quote" ")", then the
     * paragraph and the text one and two levels deeper. */
    expected = 9 + 1 + 1;
    for (depth = 1; depth <= DEPTH; depth++)
    {
        expected += 1 + (2 * depth) + 12 + 1;
    }

    expected += 1 + (2 * (DEPTH + 1)) + 10 + 1;
    expected += 1 + (2 * (DEPTH + 2)) + 9 + 1;
    data = amark_buffer_data(out);
    length = amark_buffer_length(out);
    NAOS_CHECK_UINT_EQ(expected, length);
    NAOS_REQUIRE_UINT_GE(length, DEPTH + 40);
    NAOS_CHECK_MEM_EQ(data, 29, "(document\n  (block_quote\n    (", 29);
    /* The text, then the DEPTH + 3 closing parentheses and the final LF. */
    NAOS_CHECK_MEM_EQ(data + length - (DEPTH + 13), 9, "(text \"x\"", 9);
    closing = 0;
    for (i = length - (DEPTH + 4); i < (length - 1); i++)
    {
        closing += (data[i] == ')') ? 1u : 0u;
    }

    NAOS_CHECK_UINT_EQ(DEPTH + 3, closing);
    NAOS_CHECK_INT_EQ('\n', data[length - 1]);
    amark_buffer_destroy(out);
    amark_document_destroy(document);
}

NAOS_SUITE(dump,
    NAOS_SET_TEARDOWN(restore_dump),
    NAOS_ADD_TEST(the_example_of_the_design_is_reproduced),
    NAOS_ADD_TEST(every_builtin_attribute_and_the_extension_attributes_are_written),
    NAOS_ADD_TEST(the_same_tree_always_gives_the_same_dump),
    NAOS_ADD_TEST(strings_are_escaped),
    NAOS_ADD_TEST(tokens_and_spans_are_written_when_asked),
    NAOS_ADD_TEST(edit_records_are_written_when_asked),
    NAOS_ADD_TEST(null_arguments_are_rejected),
    NAOS_ADD_TEST(a_deep_chain_is_dumped_without_recursion))

/* Dumps the root of s after "prefix" and checks that the dump fails with expected
 * and leaves only "prefix" in the buffer. */
static void check_failed_dump(const sample_t *s, amark_error_t expected)
{
    amark_buffer_t *out = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_create(&out));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_cstr(out, "prefix"));
    NAOS_CHECK_INT_EQ(expected,
                      amark_node_dump(s->root, AMARK_DUMP_TOKENS | AMARK_DUMP_EDITS, out));
    NAOS_CHECK_STR_EQ("prefix", amark_buffer_data(out));
    NAOS_CHECK_UINT_EQ(6, amark_buffer_length(out));
    amark_buffer_destroy(out);
}

NAOS_TEST(the_failure_of_an_extension_is_returned_and_the_buffer_restored)
{
    sample_t s;

    build_sample(&s, NULL, false);
    dump_mode = DUMP_FAIL_AFTER_NAME;
    memset(dump_results, 0xff, sizeof(dump_results));
    check_failed_dump(&s, AMARK_ERROR_INVALID_STATE);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, dump_results[0]);
    amark_document_destroy(s.document);
}

NAOS_TEST(invalid_attribute_names_fail_the_dump)
{
    static const char *const bad_names[] = { "", "A", "1a", "_a", "a-b", "a.b", "a b", "aB", NULL };
    static const char *const good_names[] = { "a", "z9", "a_b_", "x0_y" };
    sample_t s;
    size_t i;

    build_sample(&s, NULL, false);
    dump_mode = DUMP_BAD_NAME_IGNORED;
    amark_node_destroy(s.raw);
    for (i = 0; i < (sizeof(bad_names) / sizeof(bad_names[0])); i++)
    {
        dump_bad_name = bad_names[i];
        memset(dump_results, 0xff, sizeof(dump_results));
        /* The extension ignores the failure; the writer keeps it and the dump
         * returns it. */
        check_failed_dump(&s, AMARK_ERROR_INVALID_ARGUMENT);
        NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, dump_results[0]);
        NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, dump_results[1]);
    }

    for (i = 0; i < (sizeof(good_names) / sizeof(good_names[0])); i++)
    {
        amark_buffer_t *out;
        char expected[64];

        dump_bad_name = good_names[i];
        memset(dump_results, 0xff, sizeof(dump_results));
        out = dump_of(s.box, 0);
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, dump_results[0]);
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, dump_results[1]);
        (void)snprintf(expected, sizeof(expected), "(x.box :%s 1 :after 2)\n", good_names[i]);
        NAOS_CHECK_STR_EQ(expected, amark_buffer_data(out));
        amark_buffer_destroy(out);
    }

    amark_document_destroy(s.document);
}

NAOS_TEST(invalid_symbols_and_strings_fail_the_dump)
{
    static const char *const bad_symbols[] = { "", "Left", "1", "-a", "a-b", "a.b", "\"a\"", NULL };
    sample_t s;
    size_t i;

    build_sample(&s, NULL, false);
    dump_mode = DUMP_BAD_SYMBOL_IGNORED;
    for (i = 0; i < (sizeof(bad_symbols) / sizeof(bad_symbols[0])); i++)
    {
        dump_bad_name = bad_symbols[i];
        memset(dump_results, 0xff, sizeof(dump_results));
        check_failed_dump(&s, AMARK_ERROR_INVALID_ARGUMENT);
        NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, dump_results[0]);
        NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, dump_results[1]);
    }

    /* A NULL string with a length other than 0. */
    dump_mode = DUMP_BYTES;
    dump_bytes = NULL;
    dump_bytes_length = 1;
    check_failed_dump(&s, AMARK_ERROR_INVALID_ARGUMENT);
    amark_document_destroy(s.document);
}

/* Writes integers at the limits through the writer. */
static amark_error_t limits_dump(const amark_node_t *node, const void *data,
                                 amark_dump_writer_t *writer)
{
    amark_error_t err;

    (void)node;
    (void)data;
    err = amark_dump_attr_int(writer, "min", LLONG_MIN);
    if (err == AMARK_ERROR_NONE)
    {
        err = amark_dump_attr_int(writer, "max", LLONG_MAX);
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = amark_dump_attr_int(writer, "zero", 0);
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = amark_dump_attr_bool(writer, "no", false);
    }

    return err;
}

static const amark_node_type_desc_t limits_type = {
    sizeof(amark_node_type_desc_t), "y.limits", AMARK_NODE_LEVEL_INLINE, AMARK_CONTENT_INLINE, 0,
    AMARK_LITERAL_LINE, 0, NULL, NULL, NULL, NULL, NULL, NULL, limits_dump, NULL
};

static const amark_node_type_desc_t *const y_types[] = { &limits_type };

static const amark_extension_t y_extension = {
    sizeof(amark_extension_t), "y", y_types, 1, NULL, 0, NULL
};

NAOS_TEST(the_writer_formats_each_value_kind)
{
    amark_syntax_set_t *set = NULL;
    amark_document_t *document = NULL;
    amark_node_t *node;
    char expected[160];

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_create(&set));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &y_extension));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_freeze(set));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(set, &document));
    node = add(document, NULL, type_of(document, &limits_type));
    SET_STRING(amark_node_set_literal, node, "v");
    /* The extension attributes come before the records, the tokens and the
     * literal. */
    amark_document_set_edit_tracking(document, true);
    amark_node_mark_edited(node);
    (void)snprintf(expected, sizeof(expected),
                   "(y.limits :min %lld :max %lld :zero 0 :no false :edits (content) \"v\")\n",
                   LLONG_MIN, LLONG_MAX);
    CHECK_DUMP(node, AMARK_DUMP_EDITS, expected);
    amark_document_destroy(document);
    amark_syntax_set_destroy(set);
}

/* Dumps the sample in context after "prefix" with every flag; on a failure, checks
 * that only "prefix" is left. */
static size_t oom_mismatches;

static amark_error_t dump_scenario(void *context)
{
    const sample_t *s = context;
    amark_buffer_t *out = NULL;
    amark_error_t err = amark_buffer_create(&out);

    if (err != AMARK_ERROR_NONE)
    {
        return err;
    }

    err = amark_buffer_append_cstr(out, "prefix");
    if (err == AMARK_ERROR_NONE)
    {
        err = amark_node_dump(s->root, AMARK_DUMP_TOKENS | AMARK_DUMP_EDITS | AMARK_DUMP_SPANS,
                              out);
        if ((err != AMARK_ERROR_NONE)
            && ((amark_buffer_length(out) != 6) || (strcmp(amark_buffer_data(out), "prefix") != 0)))
        {
            oom_mismatches++;
        }
    }

    amark_buffer_destroy(out);
    return err;
}

NAOS_TEST(the_dump_survives_every_allocation_failure)
{
    sample_t s;
    amark_source_id_t source = 0;

    build_sample(&s, NULL, true);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_add_source(s.document, "abc", 3, &source));
    add_token(s.title, AMARK_TOKEN_TEXT, source, 0, 3);
    oom_mismatches = 0;
    AMARK_CHECK_OOM_SWEEP(dump_scenario, &s);
    NAOS_CHECK_UINT_EQ(0, oom_mismatches);
    amark_document_destroy(s.document);
}

NAOS_SUITE(dump_writer,
    NAOS_SET_TEARDOWN(restore_dump),
    NAOS_ADD_TEST(the_failure_of_an_extension_is_returned_and_the_buffer_restored),
    NAOS_ADD_TEST(invalid_attribute_names_fail_the_dump),
    NAOS_ADD_TEST(invalid_symbols_and_strings_fail_the_dump),
    NAOS_ADD_TEST(the_writer_formats_each_value_kind),
    NAOS_ADD_TEST(the_dump_survives_every_allocation_failure))

NAOS_MAIN(
    NAOS_ADD_SUITE(equal),
    NAOS_ADD_SUITE(equal_text),
    NAOS_ADD_SUITE(dump),
    NAOS_ADD_SUITE(dump_writer)
)
