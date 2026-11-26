/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * Copies and imports (src/tree/copy_import.c), through the public API. The copies
 * are compared with the originals by a reference function of this test (the
 * descriptors, the type data, the literals, the tokens with the bytes of their
 * sources, the records and the shape of the subtree); the internal
 * amark_tree_check checks the invariants after every operation, and the ring of
 * the document gives the number of its nodes.
 *
 * Suite copy (4.6, 4.9, 8.12): the copy is detached, in the same document and
 * equal to the original (the built-in attributes, an extension type with a copy
 * function and one copied by its bytes); the associated pointers are not copied;
 * the strings are copied, not shared; a failing copy function leaves the document
 * unchanged; the root and NULL are rejected; a chain of 100000 nodes is copied
 * without recursion.
 * Suite copy_records (7.3): the copy keeps the records as they are (no
 * AMARK_EDIT_CREATED is added for the copy itself), and its insertion records
 * AMARK_EDIT_RELOCATED (none for a copy of a AMARK_EDIT_CREATED node).
 * Suite import (4.7, 4.8, 4.9): an import into a document whose syntax set gives
 * other identifiers to the same descriptors maps the types and copies the
 * referenced sources (only those) with the tokens renumbered, which stay readable
 * after the source document is destroyed; a type the destination set lacks is
 * AMARK_ERROR_INVALID_ARGUMENT and leaves the destination unchanged; an import in
 * the same document is a copy; the root is rejected; a deep chain is imported.
 * Suite import_children (4.9, 4.10, 4.11, 7.2, 7.3, 7.5): the copies of the
 * children go before before (at the end when NULL) in their order; the sources
 * they share are copied once and stay readable after the source document is
 * destroyed; the root as source_parent imports a whole document; the records
 * (AMARK_EDIT_CHILDREN on the parent, AMARK_EDIT_RELOCATED on each copy,
 * AMARK_EDIT_DESCENDANTS on the ancestors, the records of the source kept and
 * propagated); a child the parent cannot contain, a type the destination set
 * lacks, a before that is not a child of parent and NULL are
 * AMARK_ERROR_INVALID_ARGUMENT and leave the destination (its nodes, links,
 * records and sources) unchanged; a source_parent without children does nothing;
 * in the same document the children as they are at the call are copied (also when
 * source_parent is parent or one of its ancestors) and no source is added.
 * Suite copy_oom: the out-of-memory sweeps of the copy, the import and the import
 * of the children leave the nodes and the sources of the destination as they were
 * on every failure.
 */
#include "tree/tree.h"

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

/* Expects the check of document to pass. */
#define CHECK_VALID(document) \
    do { \
        const char *check_reason_ = "unset"; \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_tree_check((document), &check_reason_)); \
        NAOS_CHECK_NULL(check_reason_); \
    } while (0)

/* Returns err from the enclosing function when it is not AMARK_ERROR_NONE. */
#define TRY(expr) \
    do { \
        amark_error_t try_err_ = (expr); \
        if (try_err_ != AMARK_ERROR_NONE) \
        { \
            return try_err_; \
        } \
    } while (0)

/* --- Extension types ------------------------------------------------------------ */

/* x.box: a block container whose type data owns a name (copy, equal, deinit). */
typedef struct box_data
{
    char *name;
} box_data_t;

static size_t box_copies;

static void box_deinit(amark_node_t *node, void *data)
{
    (void)node;
    amark_free(((box_data_t *)data)->name);
}

static amark_error_t box_copy(amark_node_t *dst, void *dst_data, const amark_node_t *src,
                              const void *src_data)
{
    const box_data_t *from = src_data;
    box_data_t *to = dst_data;

    (void)dst;
    (void)src;
    box_copies++;
    to->name = NULL;
    if (from->name != NULL)
    {
        size_t length = strlen(from->name);

        to->name = amark_malloc(length + 1);
        if (to->name == NULL)
        {
            return AMARK_ERROR_OUT_OF_MEMORY;
        }

        memcpy(to->name, from->name, length + 1);
    }

    return AMARK_ERROR_NONE;
}

static bool box_equal(const amark_node_t *a, const void *a_data, const amark_node_t *b,
                      const void *b_data)
{
    const char *x = ((const box_data_t *)a_data)->name;
    const char *y = ((const box_data_t *)b_data)->name;

    (void)a;
    (void)b;
    if ((x == NULL) || (y == NULL))
    {
        return (x == y);
    }

    return (strcmp(x, y) == 0);
}

/* Sets the name of a box (the previous one is freed). */
static amark_error_t box_set_name(amark_node_t *box, const char *name)
{
    box_data_t *data = amark_node_data(box);
    size_t length = strlen(name);
    char *copy = amark_malloc(length + 1);

    if (copy == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }

    memcpy(copy, name, length + 1);
    amark_free(data->name);
    data->name = copy;
    return AMARK_ERROR_NONE;
}

/* x.raw: a block leaf whose type data is copied and compared by its bytes. */
typedef struct raw_data
{
    uint32_t number;
    unsigned char bytes[12];
} raw_data_t;

/* x.fail: a block leaf whose copy function always fails. */
static size_t fail_deinits;

static void fail_deinit(amark_node_t *node, void *data)
{
    (void)node;
    (void)data;
    fail_deinits++;
}

static amark_error_t fail_copy(amark_node_t *dst, void *dst_data, const amark_node_t *src,
                               const void *src_data)
{
    (void)dst;
    (void)dst_data;
    (void)src;
    (void)src_data;
    return AMARK_ERROR_LIMIT_EXCEEDED;
}

static const amark_node_type_desc_t box_type = {
    sizeof(amark_node_type_desc_t), "x.box", AMARK_NODE_LEVEL_BLOCK, AMARK_CONTENT_BLOCK,
    AMARK_CONTENT_BLOCK, AMARK_LITERAL_NONE, sizeof(box_data_t), NULL, NULL, NULL, box_deinit,
    box_copy, box_equal, NULL, NULL
};

static const amark_node_type_desc_t raw_type = {
    sizeof(amark_node_type_desc_t), "x.raw", AMARK_NODE_LEVEL_BLOCK, AMARK_CONTENT_BLOCK, 0,
    AMARK_LITERAL_NONE, sizeof(raw_data_t), NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

static const amark_node_type_desc_t fail_type = {
    sizeof(amark_node_type_desc_t), "x.fail", AMARK_NODE_LEVEL_BLOCK, AMARK_CONTENT_BLOCK, 0,
    AMARK_LITERAL_NONE, 4, NULL, NULL, NULL, fail_deinit, fail_copy, NULL, NULL, NULL
};

static const amark_node_type_desc_t pad_type = {
    sizeof(amark_node_type_desc_t), "pad.node", AMARK_NODE_LEVEL_BLOCK, AMARK_CONTENT_BLOCK, 0,
    AMARK_LITERAL_NONE, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

/* x.any: a block container that accepts blocks and list items. */
static const amark_node_type_desc_t any_type = {
    sizeof(amark_node_type_desc_t), "x.any", AMARK_NODE_LEVEL_BLOCK, AMARK_CONTENT_BLOCK,
    AMARK_CONTENT_BLOCK | AMARK_CONTENT_LIST_ITEM, AMARK_LITERAL_NONE, 0, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL
};

static const amark_node_type_desc_t *const x_types[] = {
    &box_type, &raw_type, &fail_type, &any_type
};

static const amark_extension_t x_extension = {
    sizeof(amark_extension_t), "x", x_types, 4, NULL, 0, NULL
};

static const amark_node_type_desc_t *const pad_types[] = { &pad_type };

static const amark_extension_t pad_extension = {
    sizeof(amark_extension_t), "pad", pad_types, 1, NULL, 0, NULL
};

/*
 * Creates in *out_set a frozen set: with with_pad, core + pad + x (x.box is 257),
 * otherwise core + x (x.box is 256), so that the same descriptors have other
 * identifiers.
 */
static amark_error_t create_set(bool with_pad, amark_syntax_set_t **out_set)
{
    amark_syntax_set_t *set = NULL;
    amark_error_t err;

    *out_set = NULL;
    err = amark_syntax_set_create(&set);
    if ((err == AMARK_ERROR_NONE) && with_pad)
    {
        err = amark_syntax_set_append(set, &pad_extension);
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = amark_syntax_set_append(set, &x_extension);
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = amark_syntax_set_freeze(set);
    }

    if (err != AMARK_ERROR_NONE)
    {
        amark_syntax_set_destroy(set);
        return err;
    }

    *out_set = set;
    return AMARK_ERROR_NONE;
}

/* The type of desc in the syntax set of document. */
static amark_node_type_t type_of(const amark_document_t *document,
                                 const amark_node_type_desc_t *desc)
{
    amark_node_type_t type = AMARK_NODE_NONE;

    (void)amark_syntax_set_node_type_of(amark_document_syntax_set(document), desc, &type);
    return type;
}

/* --- Helpers -------------------------------------------------------------------- */

/* The number of nodes of document (its ring). */
static size_t ring_count(const amark_document_t *document)
{
    const amark_node_t *node;
    size_t count = 1;

    if (document->ring == NULL)
    {
        return 0;
    }

    for (node = document->ring->ring_next; node != document->ring; node = node->ring_next)
    {
        count++;
    }

    return count;
}

static amark_error_t add_token(amark_node_t *node, amark_token_kind_t kind,
                               amark_source_id_t source, size_t offset, size_t length)
{
    amark_token_t token;

    memset(&token, 0, sizeof(token));
    token.struct_size = sizeof(token);
    token.kind = kind;
    token.source = source;
    token.offset = offset;
    token.length = length;
    return amark_node_add_token(node, &token);
}

static amark_error_t new_child(amark_node_t *parent, amark_node_type_t type,
                               amark_node_t **out_node)
{
    TRY(amark_node_create(amark_node_document(parent), type, out_node));
    return amark_node_append_child(parent, *out_node);
}

static amark_error_t new_text(amark_node_t *parent, const char *text, amark_node_t **out_node)
{
    TRY(new_child(parent, AMARK_NODE_TEXT, out_node));
    return amark_node_set_literal(*out_node, text, strlen(text));
}

/* The source bytes of the sample (source 1 holds the heading line). */
static const char unused_source[] = "unused\n";
static const char line_source[] = "# Title *em* [a](b)\r\n";
static const char box_source[] = "box";

/*
 * Builds in document, whose set is one of create_set, under the root:
 *   x.box "outer" (user_data) >
 *     heading 3 (tokens) > text "Title " (token), emph > text "em",
 *                          link "b" "t" > text "a"
 *     x.raw (bytes, tokens of two sources)
 *     code_block "c" "x\n"
 *     list ordered 5 tight=false > item > paragraph > text "p"
 *     link_definition "d" "t2" "label"
 * and the sources unused_source (0, not referenced), line_source (1) and
 * box_source (2). Writes the box to *out_top.
 */
static amark_error_t build_sample(amark_document_t *document, amark_node_t **out_top)
{
    amark_node_t *root = amark_document_root(document);
    amark_node_t *top;
    amark_node_t *heading;
    amark_node_t *node;
    amark_node_t *emph;
    amark_node_t *link;
    amark_node_t *list;
    amark_node_t *item;
    amark_node_t *paragraph;
    amark_source_id_t id;
    raw_data_t *raw;

    *out_top = NULL;
    TRY(amark_document_add_source(document, unused_source, sizeof(unused_source) - 1, &id));
    TRY(amark_document_add_source(document, line_source, sizeof(line_source) - 1, &id));
    TRY(amark_document_add_source(document, box_source, sizeof(box_source) - 1, &id));

    TRY(new_child(root, type_of(document, &box_type), &top));
    TRY(box_set_name(top, "outer"));
    amark_node_set_user_data(top, &box_copies);

    TRY(new_child(top, AMARK_NODE_HEADING, &heading));
    TRY(amark_node_set_heading_level(heading, 3));
    TRY(add_token(heading, AMARK_TOKEN_MARKER, 1, 0, 1));
    TRY(add_token(heading, AMARK_TOKEN_SPACE, 1, 1, 1));
    TRY(new_text(heading, "Title ", &node));
    TRY(add_token(node, AMARK_TOKEN_TEXT, 1, 2, 6));
    TRY(new_child(heading, AMARK_NODE_EMPH, &emph));
    TRY(new_text(emph, "em", &node));
    TRY(new_child(heading, AMARK_NODE_LINK, &link));
    TRY(amark_node_set_link_destination(link, "b", 1));
    TRY(amark_node_set_link_title(link, "t", 1));
    TRY(new_text(link, "a", &node));

    TRY(new_child(top, type_of(document, &raw_type), &node));
    raw = amark_node_data(node);
    raw->number = 0x12345678u;
    memcpy(raw->bytes, "raw-bytes-01", sizeof(raw->bytes));
    TRY(add_token(node, AMARK_TOKEN_LINE_ENDING, 1, 19, 2));
    TRY(add_token(node, AMARK_TOKEN_TEXT, 2, 0, 3));

    TRY(new_child(top, AMARK_NODE_CODE_BLOCK, &node));
    TRY(amark_node_set_code_info(node, "c", 1));
    TRY(amark_node_set_literal(node, "x\n", 2));

    TRY(new_child(top, AMARK_NODE_LIST, &list));
    TRY(amark_node_set_list_type(list, AMARK_LIST_ORDERED));
    TRY(amark_node_set_list_start(list, 5));
    TRY(amark_node_set_list_tight(list, false));
    TRY(new_child(list, AMARK_NODE_ITEM, &item));
    TRY(new_child(item, AMARK_NODE_PARAGRAPH, &paragraph));
    TRY(new_text(paragraph, "p", &node));

    TRY(new_child(top, AMARK_NODE_LINK_DEFINITION, &node));
    TRY(amark_node_set_link_destination(node, "d", 1));
    TRY(amark_node_set_link_title(node, "t2", 2));
    TRY(amark_node_set_link_label(node, "label", 5));

    *out_top = top;
    return AMARK_ERROR_NONE;
}

/* The node after current in the preorder of the subtree of top, or NULL. */
static const amark_node_t *next_in(const amark_node_t *current, const amark_node_t *top)
{
    if (amark_node_first_child(current) != NULL)
    {
        return amark_node_first_child(current);
    }

    while ((current != top) && (amark_node_next(current) == NULL))
    {
        current = amark_node_parent(current);
    }

    return (current == top) ? NULL : amark_node_next(current);
}

/* The bytes of the token at index of node. */
static const char *token_bytes(const amark_node_t *node, size_t index, amark_token_t *out_token)
{
    const char *data;
    size_t length = 0;

    out_token->struct_size = sizeof(*out_token);
    if (amark_node_token_at(node, index, out_token) != AMARK_ERROR_NONE)
    {
        return NULL;
    }

    data = amark_document_source_data(amark_node_document(node), out_token->source, &length);
    if ((data == NULL) && (out_token->length == 0))
    {
        return "";
    }

    if ((data == NULL) || (out_token->offset > length)
        || (out_token->length > (length - out_token->offset)))
    {
        return NULL;
    }

    return data + out_token->offset;
}

/*
 * The reference comparison of two nodes alone: the descriptor (the identifiers
 * may differ between documents), the type data (equal, or else the bytes), the
 * literal, the records, and the tokens with the bytes they cover (the source
 * identifiers may differ between documents). Not the associated pointer.
 */
static bool same_node(const amark_node_t *a, const amark_node_t *b)
{
    const amark_node_type_desc_t *desc = amark_node_desc(a);
    const char *a_literal;
    const char *b_literal;
    size_t a_length;
    size_t b_length;
    size_t i;

    if ((desc != amark_node_desc(b)) || (amark_node_edits(a) != amark_node_edits(b)))
    {
        return false;
    }

    if (desc->data_size != 0)
    {
        const void *a_data = amark_node_data_const(a);
        const void *b_data = amark_node_data_const(b);

        if (a_data == b_data)
        {
            return false;
        }

        if (desc->equal != NULL)
        {
            if (!desc->equal(a, a_data, b, b_data))
            {
                return false;
            }
        }
        else if (memcmp(a_data, b_data, desc->data_size) != 0)
        {
            return false;
        }
    }

    a_literal = amark_node_literal(a, &a_length);
    b_literal = amark_node_literal(b, &b_length);
    if ((a_length != b_length) || (memcmp(a_literal, b_literal, a_length) != 0))
    {
        return false;
    }

    if (amark_node_token_count(a) != amark_node_token_count(b))
    {
        return false;
    }

    for (i = 0; i < amark_node_token_count(a); i++)
    {
        amark_token_t x;
        amark_token_t y;
        const char *x_bytes = token_bytes(a, i, &x);
        const char *y_bytes = token_bytes(b, i, &y);

        if ((x_bytes == NULL) || (y_bytes == NULL) || (x.kind != y.kind) || (x.role != y.role)
            || (x.flags != y.flags) || (x.offset != y.offset) || (x.length != y.length)
            || (x.tab_column_offset != y.tab_column_offset) || (x.tab_columns != y.tab_columns)
            || (memcmp(x_bytes, y_bytes, x.length) != 0))
        {
            return false;
        }
    }

    return true;
}

/* The reference comparison of two subtrees: same_node on each pair in preorder, and
 * the same shape (each pair has children, and a next sibling below the tops,
 * alike). */
static bool same_subtree(const amark_node_t *a, const amark_node_t *b)
{
    const amark_node_t *x = a;
    const amark_node_t *y = b;

    while ((x != NULL) && (y != NULL))
    {
        if (!same_node(x, y) || ((amark_node_first_child(x) == NULL)
                                 != (amark_node_first_child(y) == NULL)))
        {
            return false;
        }

        if ((x != a) && ((amark_node_next(x) == NULL) != (amark_node_next(y) == NULL)))
        {
            return false;
        }

        x = next_in(x, a);
        y = next_in(y, b);
    }

    return (x == NULL) && (y == NULL);
}

/* The number of nodes of the subtree of node. */
static size_t subtree_size(const amark_node_t *node)
{
    const amark_node_t *current;
    size_t count = 0;

    for (current = node; current != NULL; current = next_in(current, node))
    {
        count++;
    }

    return count;
}

/* The fixture: a document with a set of create_set(true) and the sample. */
typedef struct fixture
{
    amark_syntax_set_t *set;
    amark_document_t *document;
    amark_node_t *top;
} fixture_t;

static void fixture_build(fixture_t *f)
{
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, create_set(true, &f->set));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(f->set, &f->document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, build_sample(f->document, &f->top));
    CHECK_VALID(f->document);
}

static void fixture_destroy(fixture_t *f)
{
    amark_document_destroy(f->document);
    amark_syntax_set_destroy(f->set);
    f->document = NULL;
    f->set = NULL;
}

/* Builds a chain of count block quotes under the root of document and returns
 * its top. */
static amark_node_t *build_chain(amark_document_t *document, size_t count)
{
    amark_node_t *parent = amark_document_root(document);
    amark_node_t *top = NULL;
    size_t i;

    for (i = 0; i < count; i++)
    {
        amark_node_t *node = NULL;

        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, new_child(parent, AMARK_NODE_BLOCK_QUOTE, &node));
        if (top == NULL)
        {
            top = node;
        }

        parent = node;
    }

    return top;
}

/* The depth of the chain of first children of node (node included). */
static size_t chain_depth(const amark_node_t *node)
{
    size_t depth = 0;

    while (node != NULL)
    {
        depth++;
        node = amark_node_first_child(node);
    }

    return depth;
}

NAOS_TEARDOWN(restore)
{
    amark_allocator_set(NULL);
}

/* --- Suite copy ----------------------------------------------------------------- */

NAOS_TEST(a_copy_is_detached_in_the_same_document_and_equal)
{
    fixture_t f;
    amark_node_t *copy = (amark_node_t *)&f;
    size_t before;
    size_t sources;

    fixture_build(&f);
    before = ring_count(f.document);
    sources = amark_document_source_count(f.document);
    box_copies = 0;

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_copy(f.top, &copy));
    NAOS_REQUIRE_NOT_NULL(copy);
    NAOS_CHECK_TRUE(f.top != copy);
    NAOS_CHECK_NULL(amark_node_parent(copy));
    NAOS_CHECK_NULL(amark_node_next(copy));
    NAOS_CHECK_NULL(amark_node_previous(copy));
    NAOS_CHECK_PTR_EQ(f.document, amark_node_document(copy));
    NAOS_CHECK_INT_EQ(amark_node_get_type(f.top), amark_node_get_type(copy));
    NAOS_CHECK_TRUE(same_subtree(f.top, copy));
    NAOS_CHECK_UINT_EQ(14, subtree_size(copy));
    NAOS_CHECK_UINT_EQ(before + 14, ring_count(f.document));
    NAOS_CHECK_UINT_EQ(sources, amark_document_source_count(f.document));
    NAOS_CHECK_UINT_EQ(1, box_copies);

    /* The associated pointer is not copied. */
    NAOS_CHECK_PTR_EQ(&box_copies, amark_node_user_data(f.top));
    NAOS_CHECK_NULL(amark_node_user_data(copy));

    /* The original is unchanged and still in the tree. */
    NAOS_CHECK_PTR_EQ(amark_document_root(f.document), amark_node_parent(f.top));
    CHECK_VALID(f.document);

    fixture_destroy(&f);
}

NAOS_TEST(the_copy_holds_the_attributes_literals_tokens_and_children_in_order)
{
    fixture_t f;
    amark_node_t *copy = NULL;
    amark_node_t *node;
    amark_token_t token;
    const char *text;
    size_t length;

    fixture_build(&f);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_copy(f.top, &copy));

    NAOS_CHECK_STR_EQ("outer", ((box_data_t *)amark_node_data(copy))->name);
    NAOS_CHECK_TRUE(((box_data_t *)amark_node_data(f.top))->name
                    != ((box_data_t *)amark_node_data(copy))->name);

    node = amark_node_first_child(copy);
    NAOS_REQUIRE_INT_EQ(AMARK_NODE_HEADING, amark_node_get_type(node));
    NAOS_CHECK_INT_EQ(3, amark_node_heading_level(node));
    NAOS_REQUIRE_UINT_EQ(2, amark_node_token_count(node));
    text = token_bytes(node, 0, &token);
    NAOS_REQUIRE_NOT_NULL(text);
    NAOS_CHECK_INT_EQ(AMARK_TOKEN_MARKER, token.kind);
    NAOS_CHECK_INT_EQ('#', text[0]);
    NAOS_CHECK_INT_EQ(AMARK_NODE_TEXT, amark_node_get_type(amark_node_first_child(node)));
    NAOS_CHECK_STR_EQ("Title ", amark_node_literal(amark_node_first_child(node), NULL));
    NAOS_CHECK_INT_EQ(AMARK_NODE_LINK, amark_node_get_type(amark_node_last_child(node)));
    NAOS_CHECK_STR_EQ("b", amark_node_link_destination(amark_node_last_child(node), NULL));
    NAOS_CHECK_STR_EQ("t", amark_node_link_title(amark_node_last_child(node), NULL));

    node = amark_node_next(node);
    NAOS_REQUIRE_INT_EQ(type_of(f.document, &raw_type), amark_node_get_type(node));
    NAOS_CHECK_INT_EQ(0, memcmp(amark_node_data(node), amark_node_data(amark_node_next(
                                    amark_node_first_child(f.top))), sizeof(raw_data_t)));
    NAOS_REQUIRE_UINT_EQ(2, amark_node_token_count(node));
    text = token_bytes(node, 1, &token);
    NAOS_REQUIRE_NOT_NULL(text);
    NAOS_CHECK_INT_EQ(2, token.source);
    NAOS_CHECK_MEM_EQ("box", 3, text, token.length);

    node = amark_node_next(node);
    NAOS_REQUIRE_INT_EQ(AMARK_NODE_CODE_BLOCK, amark_node_get_type(node));
    NAOS_CHECK_STR_EQ("c", amark_node_code_info(node, NULL));
    NAOS_CHECK_STR_EQ("x\n", amark_node_literal(node, &length));
    NAOS_CHECK_UINT_EQ(2, length);

    node = amark_node_next(node);
    NAOS_REQUIRE_INT_EQ(AMARK_NODE_LIST, amark_node_get_type(node));
    NAOS_CHECK_INT_EQ(AMARK_LIST_ORDERED, amark_node_list_type(node));
    NAOS_CHECK_INT_EQ(5, amark_node_list_start(node));
    NAOS_CHECK_FALSE(amark_node_list_tight(node));

    node = amark_node_next(node);
    NAOS_REQUIRE_INT_EQ(AMARK_NODE_LINK_DEFINITION, amark_node_get_type(node));
    NAOS_CHECK_STR_EQ("label", amark_node_link_label(node, NULL));
    NAOS_CHECK_NULL(amark_node_next(node));

    /* The strings are copies: changing the original leaves the copy. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_set_link_label(amark_node_last_child(f.top), "other", 5));
    NAOS_CHECK_STR_EQ("label", amark_node_link_label(node, NULL));
    CHECK_VALID(f.document);

    fixture_destroy(&f);
}

NAOS_TEST(copies_of_leaves_and_inner_nodes_are_equal)
{
    fixture_t f;
    const amark_node_t *node;

    fixture_build(&f);
    for (node = f.top; node != NULL; node = next_in(node, f.top))
    {
        amark_node_t *copy = NULL;

        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_copy(node, &copy));
        NAOS_CHECK_TRUE(same_subtree(node, copy));
        NAOS_CHECK_NULL(amark_node_parent(copy));
    }

    CHECK_VALID(f.document);
    fixture_destroy(&f);
}

NAOS_TEST(a_failing_copy_function_leaves_the_document_unchanged)
{
    fixture_t f;
    amark_node_t *bad = NULL;
    amark_node_t *copy = (amark_node_t *)&f;
    size_t before;

    fixture_build(&f);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        new_child(f.top, type_of(f.document, &fail_type), &bad));
    before = ring_count(f.document);
    fail_deinits = 0;

    NAOS_CHECK_INT_EQ(AMARK_ERROR_LIMIT_EXCEEDED, amark_node_copy(f.top, &copy));
    NAOS_CHECK_NULL(copy);
    NAOS_CHECK_UINT_EQ(before, ring_count(f.document));
    /* The node whose copy failed is not deinitialized. */
    NAOS_CHECK_UINT_EQ(0, fail_deinits);
    CHECK_VALID(f.document);

    fixture_destroy(&f);
    NAOS_CHECK_UINT_EQ(1, fail_deinits);
}

NAOS_TEST(the_root_and_null_are_rejected)
{
    amark_document_t *document = NULL;
    amark_node_t *copy = (amark_node_t *)&document;
    amark_node_t *text = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_copy(amark_document_root(document), &copy));
    NAOS_CHECK_NULL(copy);
    copy = (amark_node_t *)&document;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_copy(NULL, &copy));
    NAOS_CHECK_NULL(copy);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, AMARK_NODE_TEXT, &text));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_copy(text, NULL));
    NAOS_CHECK_UINT_EQ(2, ring_count(document));
    CHECK_VALID(document);

    amark_document_destroy(document);
}

NAOS_TEST(a_deep_chain_is_copied_without_recursion)
{
    amark_document_t *document = NULL;
    amark_node_t *top;
    amark_node_t *copy = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    top = build_chain(document, 100000);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_copy(top, &copy));
    NAOS_REQUIRE_NOT_NULL(copy);
    NAOS_CHECK_UINT_EQ(100000, chain_depth(copy));
    NAOS_CHECK_UINT_EQ(200001, ring_count(document));
    CHECK_VALID(document);

    amark_document_destroy(document);
}

/* --- Suite copy_records ----------------------------------------------------------- */

NAOS_TEST(the_copy_keeps_the_records_as_they_are)
{
    amark_document_t *document = NULL;
    amark_node_t *paragraph = NULL;
    amark_node_t *text = NULL;
    amark_node_t *emph = NULL;
    amark_node_t *copy = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        new_child(amark_document_root(document), AMARK_NODE_PARAGRAPH,
                                  &paragraph));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, new_text(paragraph, "a", &text));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, new_child(paragraph, AMARK_NODE_EMPH, &emph));

    /* Tracking on: a content record below and a node created while tracking. */
    amark_document_set_edit_tracking(document, true);
    amark_node_mark_edited(text);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, new_text(emph, "b", &text));
    NAOS_REQUIRE_INT_EQ(AMARK_EDIT_DESCENDANTS, amark_node_edits(paragraph));

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_copy(paragraph, &copy));
    NAOS_REQUIRE_NOT_NULL(copy);
    NAOS_CHECK_TRUE(same_subtree(paragraph, copy));
    NAOS_CHECK_INT_EQ(AMARK_EDIT_DESCENDANTS, amark_node_edits(copy));
    NAOS_CHECK_INT_EQ(AMARK_EDIT_CONTENT, amark_node_edits(amark_node_first_child(copy)));
    NAOS_CHECK_TRUE((amark_node_edits(text) & AMARK_EDIT_CREATED) != 0);
    NAOS_CHECK_INT_EQ(amark_node_edits(text),
                      amark_node_edits(amark_node_first_child(amark_node_last_child(copy))));
    CHECK_VALID(document);

    amark_document_destroy(document);
}

NAOS_TEST(a_copy_of_nodes_without_records_has_none_with_tracking_on)
{
    fixture_t f;
    amark_node_t *copy = NULL;
    const amark_node_t *node;

    fixture_build(&f);
    amark_document_set_edit_tracking(f.document, true);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_copy(f.top, &copy));
    for (node = copy; node != NULL; node = next_in(node, copy))
    {
        NAOS_CHECK_INT_EQ(0, amark_node_edits(node));
    }

    NAOS_CHECK_INT_EQ(0, amark_node_edits(amark_document_root(f.document)));
    fixture_destroy(&f);
}

NAOS_TEST(an_inserted_copy_gets_relocated)
{
    fixture_t f;
    amark_node_t *copy = NULL;
    amark_node_t *root;

    fixture_build(&f);
    root = amark_document_root(f.document);
    amark_document_set_edit_tracking(f.document, true);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_copy(f.top, &copy));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(root, copy));

    NAOS_CHECK_INT_EQ(AMARK_EDIT_RELOCATED, amark_node_edits(copy));
    NAOS_CHECK_INT_EQ(AMARK_EDIT_CHILDREN | AMARK_EDIT_DESCENDANTS, amark_node_edits(root));
    NAOS_CHECK_INT_EQ(0, amark_node_edits(amark_node_first_child(copy)));
    NAOS_CHECK_INT_EQ(0, amark_node_edits(f.top));
    CHECK_VALID(f.document);

    fixture_destroy(&f);
}

NAOS_TEST(an_inserted_copy_of_a_created_node_keeps_created)
{
    amark_document_t *document = NULL;
    amark_node_t *paragraph = NULL;
    amark_node_t *copy = NULL;
    amark_node_t *root;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    root = amark_document_root(document);
    amark_document_set_edit_tracking(document, true);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(document, AMARK_NODE_PARAGRAPH, &paragraph));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_copy(paragraph, &copy));
    NAOS_CHECK_INT_EQ(AMARK_EDIT_CREATED, amark_node_edits(copy));

    /* Inserted with tracking off: no new record, but its record propagates. */
    amark_document_set_edit_tracking(document, false);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(root, copy));
    NAOS_CHECK_INT_EQ(AMARK_EDIT_CREATED, amark_node_edits(copy));
    NAOS_CHECK_INT_EQ(AMARK_EDIT_DESCENDANTS, amark_node_edits(root));
    CHECK_VALID(document);

    amark_document_destroy(document);
}

/* --- Suite import --------------------------------------------------------------- */

NAOS_TEST(an_import_maps_the_types_and_copies_the_referenced_sources)
{
    fixture_t f;
    amark_syntax_set_t *set = NULL;
    amark_document_t *document = NULL;
    amark_node_t *copy = (amark_node_t *)&f;
    amark_node_t *raw;
    amark_source_id_t id;
    amark_token_t token;
    const char *data;
    size_t length;
    size_t i;
    static const char pre[] = "pre";

    fixture_build(&f);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, create_set(false, &set));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(set, &document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_add_source(document, pre, 3, &id));
    NAOS_REQUIRE_INT_NE(type_of(f.document, &box_type), type_of(document, &box_type));
    box_copies = 0;

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_import(document, f.top, &copy));
    NAOS_REQUIRE_NOT_NULL(copy);
    NAOS_CHECK_PTR_EQ(document, amark_node_document(copy));
    NAOS_CHECK_NULL(amark_node_parent(copy));
    NAOS_CHECK_INT_EQ(type_of(document, &box_type), amark_node_get_type(copy));
    NAOS_CHECK_PTR_EQ(&box_type, amark_node_desc(copy));
    NAOS_CHECK_TRUE(same_subtree(f.top, copy));
    NAOS_CHECK_NULL(amark_node_user_data(copy));
    NAOS_CHECK_UINT_EQ(1, box_copies);
    NAOS_CHECK_UINT_EQ(15, ring_count(document));

    /* Sources 1 and 2 of the original, not the unused 0, after "pre". */
    NAOS_CHECK_UINT_EQ(3, amark_document_source_count(document));
    raw = amark_node_next(amark_node_first_child(copy));
    memset(&token, 0, sizeof(token));
    token.struct_size = sizeof(token);
    NAOS_CHECK_INT_EQ(type_of(document, &raw_type), amark_node_get_type(raw));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_token_at(raw, 0, &token));
    NAOS_CHECK_INT_EQ(1, token.source);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_token_at(raw, 1, &token));
    NAOS_CHECK_INT_EQ(2, token.source);
    CHECK_VALID(document);
    CHECK_VALID(f.document);

    /* The sources stay readable after the original document is gone. */
    fixture_destroy(&f);
    data = amark_document_source_data(document, 1, &length);
    NAOS_CHECK_MEM_EQ(line_source, sizeof(line_source) - 1, data, length);
    data = amark_document_source_data(document, 2, &length);
    NAOS_CHECK_MEM_EQ(box_source, sizeof(box_source) - 1, data, length);
    for (i = 0; i < amark_node_token_count(raw); i++)
    {
        data = token_bytes(raw, i, &token);
        NAOS_REQUIRE_NOT_NULL(data);
    }

    data = token_bytes(raw, 1, &token);
    NAOS_CHECK_MEM_EQ("box", 3, data, token.length);
    data = token_bytes(amark_node_first_child(amark_node_first_child(copy)), 0, &token);
    NAOS_REQUIRE_NOT_NULL(data);
    NAOS_CHECK_MEM_EQ("Title ", 6, data, token.length);
    NAOS_CHECK_INT_EQ(1, token.source);

    /* The import can be inserted, and gets relocated. */
    amark_document_set_edit_tracking(document, true);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_append_child(amark_document_root(document), copy));
    NAOS_CHECK_INT_EQ(AMARK_EDIT_RELOCATED, amark_node_edits(copy));
    CHECK_VALID(document);

    amark_document_destroy(document);
    amark_syntax_set_destroy(set);
}

NAOS_TEST(an_import_into_a_set_without_a_type_is_rejected)
{
    fixture_t f;
    amark_document_t *document = NULL;
    amark_node_t *copy = (amark_node_t *)&f;
    amark_node_t *quote = NULL;
    amark_node_t *node = NULL;
    amark_node_t *heading;
    amark_source_id_t id;

    fixture_build(&f);
    heading = amark_node_first_child(f.top);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_add_source(document, "pre", 3, &id));
    box_copies = 0;

    /* The top is an extension type, and so is a leaf below a built-in node. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_import(document, f.top, &copy));
    NAOS_CHECK_NULL(copy);
    NAOS_CHECK_UINT_EQ(1, ring_count(document));
    NAOS_CHECK_UINT_EQ(1, amark_document_source_count(document));
    NAOS_CHECK_UINT_EQ(0, box_copies);

    /* A built-in container with an extension type below it. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(f.document, AMARK_NODE_BLOCK_QUOTE, &quote));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, new_child(quote, AMARK_NODE_PARAGRAPH, &node));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, new_child(quote, type_of(f.document, &raw_type), &node));
    copy = (amark_node_t *)&f;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_import(document, quote, &copy));
    NAOS_CHECK_NULL(copy);
    NAOS_CHECK_UINT_EQ(1, ring_count(document));
    NAOS_CHECK_UINT_EQ(1, amark_document_source_count(document));
    CHECK_VALID(document);
    CHECK_VALID(f.document);

    /* A built-in subtree with tokens imports into the core set. */
    copy = NULL;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_import(document, heading, &copy));
    NAOS_CHECK_TRUE(same_subtree(heading, copy));
    NAOS_CHECK_UINT_EQ(2, amark_document_source_count(document));
    CHECK_VALID(document);

    amark_document_destroy(document);
    fixture_destroy(&f);
}

NAOS_TEST(an_import_in_the_same_document_is_a_copy)
{
    fixture_t f;
    amark_node_t *copy = NULL;
    size_t sources;
    size_t before;

    fixture_build(&f);
    sources = amark_document_source_count(f.document);
    before = ring_count(f.document);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_import(f.document, f.top, &copy));
    NAOS_REQUIRE_NOT_NULL(copy);
    NAOS_CHECK_TRUE(same_subtree(f.top, copy));
    NAOS_CHECK_NULL(amark_node_parent(copy));
    NAOS_CHECK_UINT_EQ(sources, amark_document_source_count(f.document));
    NAOS_CHECK_UINT_EQ(before + 14, ring_count(f.document));
    CHECK_VALID(f.document);

    fixture_destroy(&f);
}

NAOS_TEST(an_import_of_a_root_or_null_is_rejected)
{
    fixture_t f;
    amark_document_t *document = NULL;
    amark_node_t *copy = (amark_node_t *)&f;

    fixture_build(&f);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(f.set, &document));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_import(document, amark_document_root(f.document), &copy));
    NAOS_CHECK_NULL(copy);
    copy = (amark_node_t *)&f;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_import(f.document, amark_document_root(f.document), &copy));
    NAOS_CHECK_NULL(copy);
    copy = (amark_node_t *)&f;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_import(NULL, f.top, &copy));
    NAOS_CHECK_NULL(copy);
    copy = (amark_node_t *)&f;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_import(document, NULL, &copy));
    NAOS_CHECK_NULL(copy);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_import(document, f.top, NULL));
    NAOS_CHECK_UINT_EQ(1, ring_count(document));
    NAOS_CHECK_UINT_EQ(0, amark_document_source_count(document));

    amark_document_destroy(document);
    fixture_destroy(&f);
}

NAOS_TEST(a_deep_chain_is_imported_without_recursion)
{
    amark_document_t *from = NULL;
    amark_document_t *to = NULL;
    amark_node_t *top;
    amark_node_t *copy = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &from));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &to));
    top = build_chain(from, 100000);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_import(to, top, &copy));
    amark_document_destroy(from);
    NAOS_REQUIRE_NOT_NULL(copy);
    NAOS_CHECK_UINT_EQ(100000, chain_depth(copy));
    NAOS_CHECK_UINT_EQ(100001, ring_count(to));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(amark_document_root(to), copy));
    CHECK_VALID(to);

    amark_document_destroy(to);
}

/* --- Suite import_children ------------------------------------------------------ */

/* The most nodes a snapshot holds. */
#define SNAPSHOT_CAPACITY 64

/* The state of a document that an import of children could change: its nodes in
 * preorder from the root with their parents and records (which fix the links), the
 * number of nodes of the ring and the number of sources. */
typedef struct snapshot
{
    size_t count;
    const amark_node_t *nodes[SNAPSHOT_CAPACITY];
    const amark_node_t *parents[SNAPSHOT_CAPACITY];
    uint32_t edits[SNAPSHOT_CAPACITY];
    size_t ring;
    size_t sources;
} snapshot_t;

/* Takes the snapshot of document; false when it holds too many nodes. */
static bool take_snapshot(const amark_document_t *document, snapshot_t *out)
{
    const amark_node_t *root = amark_document_root(document);
    const amark_node_t *node;

    memset(out, 0, sizeof(*out));
    for (node = root; node != NULL; node = next_in(node, root))
    {
        if (out->count == SNAPSHOT_CAPACITY)
        {
            return false;
        }

        out->nodes[out->count] = node;
        out->parents[out->count] = amark_node_parent(node);
        out->edits[out->count] = amark_node_edits(node);
        out->count++;
    }

    out->ring = ring_count(document);
    out->sources = amark_document_source_count(document);
    return true;
}

/* Whether document is as in the snapshot. */
static bool same_snapshot(const amark_document_t *document, const snapshot_t *snapshot)
{
    snapshot_t now;
    size_t i;

    if (!take_snapshot(document, &now) || (now.count != snapshot->count)
        || (now.ring != snapshot->ring) || (now.sources != snapshot->sources))
    {
        return false;
    }

    for (i = 0; i < now.count; i++)
    {
        if ((now.nodes[i] != snapshot->nodes[i]) || (now.parents[i] != snapshot->parents[i])
            || (now.edits[i] != snapshot->edits[i]))
        {
            return false;
        }
    }

    return true;
}

/* The number of children of node. */
static size_t child_count(const amark_node_t *node)
{
    const amark_node_t *child;
    size_t count = 0;

    for (child = amark_node_first_child(node); child != NULL; child = amark_node_next(child))
    {
        count++;
    }

    return count;
}

/* The child at index of node, or NULL. */
static amark_node_t *child_at(amark_node_t *node, size_t index)
{
    amark_node_t *child = amark_node_first_child(node);

    while ((child != NULL) && (index > 0))
    {
        child = amark_node_next(child);
        index--;
    }

    return child;
}

/* Expects the children of parent from index on to be copies, in the document of
 * parent, of the children of source_parent in their order. */
static void check_copies(amark_node_t *parent, size_t index, const amark_node_t *source_parent)
{
    const amark_node_t *original;
    amark_node_t *copy = child_at(parent, index);

    for (original = amark_node_first_child(source_parent); original != NULL;
         original = amark_node_next(original))
    {
        NAOS_REQUIRE_NOT_NULL(copy);
        NAOS_CHECK_TRUE(original != copy);
        NAOS_CHECK_PTR_EQ(parent, amark_node_parent(copy));
        NAOS_CHECK_PTR_EQ(amark_node_document(parent), amark_node_document(copy));
        NAOS_CHECK_TRUE(same_subtree(original, copy));
        copy = amark_node_next(copy);
    }
}

/* Creates in *out_document a document with a set of create_set(false), the source
 * "pre", and under the root two paragraphs with the texts "a" and "b". */
static void destination_build(amark_syntax_set_t **out_set, amark_document_t **out_document,
                              amark_node_t **out_a, amark_node_t **out_b)
{
    amark_node_t *root;
    amark_node_t *text = NULL;
    amark_source_id_t id;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, create_set(false, out_set));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(*out_set, out_document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_add_source(*out_document, "pre", 3, &id));
    root = amark_document_root(*out_document);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, new_child(root, AMARK_NODE_PARAGRAPH, out_a));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, new_text(*out_a, "a", &text));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, new_child(root, AMARK_NODE_PARAGRAPH, out_b));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, new_text(*out_b, "b", &text));
    CHECK_VALID(*out_document);
}

NAOS_TEST(the_children_go_in_order_before_a_child_or_at_the_end)
{
    fixture_t f;
    amark_syntax_set_t *set = NULL;
    amark_document_t *document = NULL;
    amark_node_t *a = NULL;
    amark_node_t *b = NULL;
    amark_node_t *root;
    amark_token_t token;
    const char *data;
    size_t children;
    size_t nodes;

    fixture_build(&f);
    destination_build(&set, &document, &a, &b);
    root = amark_document_root(document);
    children = child_count(f.top);
    nodes = ring_count(document);
    NAOS_REQUIRE_UINT_EQ(5, children);

    /* Before b: a, the copies, b. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_import_children(root, b, f.top));
    NAOS_CHECK_UINT_EQ(2 + children, child_count(root));
    NAOS_CHECK_PTR_EQ(a, amark_node_first_child(root));
    NAOS_CHECK_PTR_EQ(b, amark_node_last_child(root));
    check_copies(root, 1, f.top);
    NAOS_CHECK_UINT_EQ(nodes + subtree_size(f.top) - 1, ring_count(document));
    NAOS_CHECK_INT_EQ(type_of(document, &raw_type), amark_node_get_type(child_at(root, 2)));
    /* "pre" and the two referenced sources of the five children. */
    NAOS_CHECK_UINT_EQ(3, amark_document_source_count(document));
    CHECK_VALID(document);
    CHECK_VALID(f.document);

    /* At the end: after b. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_import_children(root, NULL, f.top));
    NAOS_CHECK_UINT_EQ(2 + (2 * children), child_count(root));
    NAOS_CHECK_PTR_EQ(b, child_at(root, 1 + children));
    check_copies(root, 2 + children, f.top);
    NAOS_CHECK_UINT_EQ(5, amark_document_source_count(document));
    CHECK_VALID(document);

    /* Before the first child. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_import_children(root, a, f.top));
    check_copies(root, 0, f.top);
    NAOS_CHECK_PTR_EQ(a, child_at(root, children));
    CHECK_VALID(document);

    /* The tokens stay readable after the source document is gone. */
    fixture_destroy(&f);
    data = token_bytes(child_at(root, 1), 1, &token);
    NAOS_REQUIRE_NOT_NULL(data);
    NAOS_CHECK_MEM_EQ("box", 3, data, token.length);
    data = token_bytes(amark_node_first_child(child_at(root, 0)), 0, &token);
    NAOS_REQUIRE_NOT_NULL(data);
    NAOS_CHECK_MEM_EQ("Title ", 6, data, token.length);
    CHECK_VALID(document);
    amark_document_destroy(document);
    amark_syntax_set_destroy(set);
}

NAOS_TEST(a_whole_document_is_imported_with_a_shared_source_copied_once)
{
    static const char text[] = "ab\ncd\n";
    amark_document_t *from = NULL;
    amark_document_t *to = NULL;
    amark_node_t *from_root;
    amark_node_t *to_root;
    amark_node_t *first = NULL;
    amark_node_t *second = NULL;
    amark_node_t *node = NULL;
    amark_source_id_t id;
    amark_token_t token;
    const char *data;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &from));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_add_source(from, "zero", 4, &id));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_add_source(from, text, sizeof(text) - 1, &id));
    from_root = amark_document_root(from);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, new_child(from_root, AMARK_NODE_PARAGRAPH, &first));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, new_text(first, "ab", &node));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, add_token(node, AMARK_TOKEN_TEXT, 1, 0, 2));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, add_token(first, AMARK_TOKEN_LINE_ENDING, 1, 2, 1));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, new_child(from_root, AMARK_NODE_PARAGRAPH, &second));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, new_text(second, "cd", &node));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, add_token(node, AMARK_TOKEN_TEXT, 1, 3, 2));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, add_token(second, AMARK_TOKEN_LINE_ENDING, 1, 5, 1));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_check_tokens(from, 1, NULL));

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &to));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_add_source(to, "pre", 3, &id));
    to_root = amark_document_root(to);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_import_children(to_root, NULL, from_root));
    NAOS_CHECK_UINT_EQ(2, child_count(to_root));
    check_copies(to_root, 0, from_root);
    /* The source shared by both children once; not the unused one. */
    NAOS_CHECK_UINT_EQ(2, amark_document_source_count(to));
    NAOS_CHECK_UINT_EQ(5, ring_count(to));
    CHECK_VALID(to);
    CHECK_VALID(from);

    amark_document_destroy(from);
    data = token_bytes(amark_node_first_child(child_at(to_root, 1)), 0, &token);
    NAOS_REQUIRE_NOT_NULL(data);
    NAOS_CHECK_INT_EQ(1, token.source);
    NAOS_CHECK_MEM_EQ("cd", 2, data, token.length);
    data = token_bytes(child_at(to_root, 0), 0, &token);
    NAOS_REQUIRE_NOT_NULL(data);
    NAOS_CHECK_MEM_EQ("\n", 1, data, token.length);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_check_tokens(to, 1, NULL));
    CHECK_VALID(to);

    amark_document_destroy(to);
}

NAOS_TEST(the_import_records_children_relocated_and_descendants)
{
    fixture_t f;
    amark_document_t *document = NULL;
    amark_node_t *quote = NULL;
    amark_node_t *paragraph = NULL;
    amark_node_t *root;
    amark_node_t *copy;
    size_t i;

    fixture_build(&f);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(f.set, &document));
    root = amark_document_root(document);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, new_child(root, AMARK_NODE_BLOCK_QUOTE, &quote));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, new_child(quote, AMARK_NODE_PARAGRAPH, &paragraph));
    amark_document_set_edit_tracking(document, true);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_import_children(quote, paragraph, f.top));
    NAOS_CHECK_INT_EQ(AMARK_EDIT_CHILDREN | AMARK_EDIT_DESCENDANTS, amark_node_edits(quote));
    NAOS_CHECK_INT_EQ(AMARK_EDIT_DESCENDANTS, amark_node_edits(root));
    NAOS_CHECK_INT_EQ(0, amark_node_edits(paragraph));
    for (i = 0, copy = amark_node_first_child(quote); copy != paragraph;
         i++, copy = amark_node_next(copy))
    {
        const amark_node_t *below;

        NAOS_CHECK_INT_EQ(AMARK_EDIT_RELOCATED, amark_node_edits(copy));
        for (below = next_in(copy, copy); below != NULL; below = next_in(below, copy))
        {
            NAOS_CHECK_INT_EQ(0, amark_node_edits(below));
        }
    }

    NAOS_CHECK_UINT_EQ(5, i);
    NAOS_CHECK_INT_EQ(0, amark_node_edits(f.top));
    CHECK_VALID(document);
    CHECK_VALID(f.document);

    amark_document_destroy(document);
    fixture_destroy(&f);
}

NAOS_TEST(the_records_of_the_source_are_kept_and_propagate)
{
    amark_document_t *from = NULL;
    amark_document_t *to = NULL;
    amark_node_t *from_root;
    amark_node_t *quote = NULL;
    amark_node_t *paragraph = NULL;
    amark_node_t *text = NULL;
    amark_node_t *copy;
    amark_node_t *root;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &from));
    from_root = amark_document_root(from);
    amark_document_set_edit_tracking(from, true);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, new_child(from_root, AMARK_NODE_PARAGRAPH, &paragraph));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, new_text(paragraph, "x", &text));
    NAOS_REQUIRE_INT_EQ(AMARK_EDIT_CREATED | AMARK_EDIT_CHILDREN | AMARK_EDIT_DESCENDANTS,
                        amark_node_edits(paragraph));

    /* Tracking off in the destination: no new record, but the records propagate. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &to));
    root = amark_document_root(to);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, new_child(root, AMARK_NODE_BLOCK_QUOTE, &quote));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_import_children(quote, NULL, from_root));
    copy = amark_node_first_child(quote);
    NAOS_REQUIRE_NOT_NULL(copy);
    NAOS_CHECK_INT_EQ(amark_node_edits(paragraph), amark_node_edits(copy));
    NAOS_CHECK_INT_EQ(amark_node_edits(text), amark_node_edits(amark_node_first_child(copy)));
    NAOS_CHECK_INT_EQ(AMARK_EDIT_DESCENDANTS, amark_node_edits(quote));
    NAOS_CHECK_INT_EQ(AMARK_EDIT_DESCENDANTS, amark_node_edits(root));
    CHECK_VALID(to);

    /* Tracking on: a copy of a created node keeps AMARK_EDIT_CREATED without
     * AMARK_EDIT_RELOCATED. */
    amark_document_set_edit_tracking(to, true);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_import_children(root, NULL, from_root));
    copy = amark_node_last_child(root);
    NAOS_CHECK_INT_EQ(amark_node_edits(paragraph), amark_node_edits(copy));
    NAOS_CHECK_INT_EQ(AMARK_EDIT_CHILDREN | AMARK_EDIT_DESCENDANTS, amark_node_edits(root));
    CHECK_VALID(to);
    CHECK_VALID(from);

    amark_document_destroy(from);
    amark_document_destroy(to);
}

NAOS_TEST(rejected_imports_leave_the_destination_unchanged)
{
    fixture_t f;
    amark_syntax_set_t *set = NULL;
    amark_document_t *document = NULL;
    amark_document_t *core = NULL;
    amark_node_t *a = NULL;
    amark_node_t *b = NULL;
    amark_node_t *any = NULL;
    amark_node_t *node = NULL;
    amark_node_t *heading;
    amark_node_t *root;
    amark_source_id_t id;
    snapshot_t snapshot;
    snapshot_t source_snapshot;

    fixture_build(&f);
    destination_build(&set, &document, &a, &b);
    root = amark_document_root(document);
    amark_document_set_edit_tracking(document, true);
    heading = amark_node_first_child(f.top);

    /* In the source: x.any > paragraph, item (the item cannot go under a root). */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(f.document, type_of(f.document, &any_type), &any));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, new_child(any, AMARK_NODE_PARAGRAPH, &node));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, new_child(any, AMARK_NODE_ITEM, &node));
    NAOS_REQUIRE_TRUE(take_snapshot(document, &snapshot));
    NAOS_REQUIRE_TRUE(take_snapshot(f.document, &source_snapshot));
    box_copies = 0;

    /* A child the parent cannot contain, after one it can. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_import_children(root, NULL, any));
    NAOS_CHECK_TRUE(same_snapshot(document, &snapshot));
    CHECK_VALID(document);

    /* Blocks under a paragraph. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_import_children(a, NULL, f.top));
    NAOS_CHECK_TRUE(same_snapshot(document, &snapshot));
    CHECK_VALID(document);

    /* before not a child of parent: a grandchild, the parent itself, a node of the
     * source document, a detached node. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_import_children(root, amark_node_first_child(a), f.top));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_import_children(root, root, f.top));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_import_children(root, heading, f.top));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, AMARK_NODE_PARAGRAPH, &node));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_import_children(root, node, f.top));
    amark_node_destroy(node);
    NAOS_CHECK_TRUE(same_snapshot(document, &snapshot));
    CHECK_VALID(document);

    /* NULL. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_import_children(NULL, NULL, f.top));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_import_children(root, NULL, NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_import_children(NULL, b, NULL));
    NAOS_CHECK_TRUE(same_snapshot(document, &snapshot));
    CHECK_VALID(document);

    /* A type the destination set lacks (x.raw, after a built-in heading). */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &core));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_add_source(core, "pre", 3, &id));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        new_child(amark_document_root(core), AMARK_NODE_THEMATIC_BREAK, &node));
    NAOS_REQUIRE_TRUE(take_snapshot(core, &snapshot));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_import_children(amark_document_root(core), NULL, f.top));
    NAOS_CHECK_TRUE(same_snapshot(core, &snapshot));
    CHECK_VALID(core);

    /* Nothing was copied, and the source is unchanged. */
    NAOS_CHECK_UINT_EQ(0, box_copies);
    NAOS_CHECK_TRUE(same_snapshot(f.document, &source_snapshot));
    CHECK_VALID(f.document);

    amark_document_destroy(core);
    amark_document_destroy(document);
    amark_syntax_set_destroy(set);
    fixture_destroy(&f);
}

NAOS_TEST(a_source_parent_without_children_does_nothing)
{
    amark_syntax_set_t *set = NULL;
    amark_document_t *document = NULL;
    amark_document_t *other = NULL;
    amark_node_t *a = NULL;
    amark_node_t *b = NULL;
    amark_node_t *empty = NULL;
    amark_node_t *root;
    snapshot_t snapshot;

    destination_build(&set, &document, &a, &b);
    root = amark_document_root(document);
    amark_document_set_edit_tracking(document, true);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &other));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(other, AMARK_NODE_PARAGRAPH, &empty));
    NAOS_REQUIRE_TRUE(take_snapshot(document, &snapshot));

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_import_children(root, b, amark_document_root(other)));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_import_children(root, NULL, empty));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_import_children(a, NULL, empty));
    NAOS_CHECK_TRUE(same_snapshot(document, &snapshot));
    NAOS_CHECK_INT_EQ(0, amark_node_edits(root));
    CHECK_VALID(document);
    CHECK_VALID(other);

    amark_document_destroy(other);
    amark_document_destroy(document);
    amark_syntax_set_destroy(set);
}

NAOS_TEST(an_import_in_the_same_document_copies_the_children_at_the_call)
{
    amark_syntax_set_t *set = NULL;
    amark_document_t *document = NULL;
    amark_node_t *a = NULL;
    amark_node_t *b = NULL;
    amark_node_t *quote = NULL;
    amark_node_t *copy;
    amark_node_t *root;
    amark_node_t *inner = NULL;
    size_t nodes;
    size_t i;

    destination_build(&set, &document, &a, &b);
    root = amark_document_root(document);
    nodes = ring_count(document);

    /* source_parent is parent and before one of its children: a, a', b', b. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_import_children(root, b, root));
    NAOS_REQUIRE_UINT_EQ(4, child_count(root));
    NAOS_CHECK_PTR_EQ(a, child_at(root, 0));
    NAOS_CHECK_PTR_EQ(b, child_at(root, 3));
    NAOS_CHECK_TRUE(same_subtree(a, child_at(root, 1)));
    NAOS_CHECK_TRUE(same_subtree(b, child_at(root, 2)));
    NAOS_CHECK_TRUE(a != child_at(root, 1));
    NAOS_CHECK_TRUE(b != child_at(root, 2));
    NAOS_CHECK_UINT_EQ(nodes + 4, ring_count(document));
    NAOS_CHECK_UINT_EQ(1, amark_document_source_count(document));
    CHECK_VALID(document);

    /* source_parent is the parent of parent: the copy of quote holds only the
     * child quote had at the call. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, new_child(root, AMARK_NODE_BLOCK_QUOTE, &quote));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, new_child(quote, AMARK_NODE_PARAGRAPH, &inner));
    nodes = ring_count(document);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_import_children(quote, NULL, root));
    NAOS_REQUIRE_UINT_EQ(6, child_count(quote));
    NAOS_CHECK_PTR_EQ(inner, amark_node_first_child(quote));
    for (i = 0; i < 4; i++)
    {
        NAOS_CHECK_TRUE(same_subtree(child_at(root, i), child_at(quote, 1 + i)));
    }

    copy = amark_node_last_child(quote);
    NAOS_CHECK_TRUE(quote != copy);
    NAOS_CHECK_PTR_EQ(amark_node_desc(quote), amark_node_desc(copy));
    NAOS_CHECK_UINT_EQ(1, child_count(copy));
    NAOS_CHECK_TRUE(same_subtree(inner, amark_node_first_child(copy)));
    NAOS_CHECK_UINT_EQ(nodes + 4 + 4 + 2, ring_count(document));
    NAOS_CHECK_UINT_EQ(1, amark_document_source_count(document));
    CHECK_VALID(document);

    amark_document_destroy(document);
    amark_syntax_set_destroy(set);
}

/* --- Suite copy_oom --------------------------------------------------------------- */

/* The failures of the sweeps after which the destination was not as before. */
static size_t oom_mismatches;

/* Builds the sample, copies its top (import into a second document when
 * context is not NULL) and checks the destination on failure, or the copy. */
static amark_error_t copy_scenario(void *context)
{
    bool import = (context != NULL);
    amark_syntax_set_t *set = NULL;
    amark_syntax_set_t *other_set = NULL;
    amark_document_t *document = NULL;
    amark_document_t *destination = NULL;
    amark_node_t *top = NULL;
    amark_node_t *copy = NULL;
    amark_source_id_t id;
    size_t nodes;
    size_t sources;
    amark_error_t err;

    err = create_set(true, &set);
    if (err == AMARK_ERROR_NONE)
    {
        err = amark_document_create(set, &document);
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = build_sample(document, &top);
    }

    if (import && (err == AMARK_ERROR_NONE))
    {
        err = create_set(false, &other_set);
        if (err == AMARK_ERROR_NONE)
        {
            err = amark_document_create(other_set, &destination);
        }

        if (err == AMARK_ERROR_NONE)
        {
            err = amark_document_add_source(destination, "pre", 3, &id);
        }
    }
    else
    {
        destination = document;
    }

    if (err == AMARK_ERROR_NONE)
    {
        nodes = ring_count(destination);
        sources = amark_document_source_count(destination);
        if (import)
        {
            err = amark_node_import(destination, top, &copy);
        }
        else
        {
            err = amark_node_copy(top, &copy);
        }

        if (err != AMARK_ERROR_NONE)
        {
            if ((copy != NULL) || (ring_count(destination) != nodes)
                || (amark_document_source_count(destination) != sources)
                || (amark_tree_check(destination, NULL) != AMARK_ERROR_NONE)
                || (amark_tree_check(document, NULL) != AMARK_ERROR_NONE))
            {
                oom_mismatches++;
            }
        }
        else if (!same_subtree(top, copy)
                 || (amark_tree_check(destination, NULL) != AMARK_ERROR_NONE))
        {
            oom_mismatches++;
        }
    }

    if (destination != document)
    {
        amark_document_destroy(destination);
    }

    amark_document_destroy(document);
    amark_syntax_set_destroy(other_set);
    amark_syntax_set_destroy(set);
    return err;
}

NAOS_TEST(the_copy_survives_every_allocation_failure)
{
    oom_mismatches = 0;
    AMARK_CHECK_OOM_SWEEP(copy_scenario, NULL);
    NAOS_CHECK_UINT_EQ(0, oom_mismatches);
}

NAOS_TEST(the_import_survives_every_allocation_failure)
{
    static int import_flag;

    oom_mismatches = 0;
    AMARK_CHECK_OOM_SWEEP(copy_scenario, &import_flag);
    NAOS_CHECK_UINT_EQ(0, oom_mismatches);
}

/*
 * Builds the sample and imports the children of its top before a paragraph of a
 * second document with tracking on (into its own root when context is not NULL),
 * then checks the destination: as before on failure, or the copies in order.
 */
static amark_error_t import_children_scenario(void *context)
{
    bool same = (context != NULL);
    amark_syntax_set_t *set = NULL;
    amark_syntax_set_t *other_set = NULL;
    amark_document_t *document = NULL;
    amark_document_t *destination = NULL;
    amark_node_t *top = NULL;
    amark_node_t *before = NULL;
    amark_source_id_t id;
    amark_error_t err;

    err = create_set(true, &set);
    if (err == AMARK_ERROR_NONE)
    {
        err = amark_document_create(set, &document);
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = build_sample(document, &top);
    }

    if (same)
    {
        destination = document;
    }
    else if (err == AMARK_ERROR_NONE)
    {
        err = create_set(false, &other_set);
        if (err == AMARK_ERROR_NONE)
        {
            err = amark_document_create(other_set, &destination);
        }

        if (err == AMARK_ERROR_NONE)
        {
            err = amark_document_add_source(destination, "pre", 3, &id);
        }
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = new_child(amark_document_root(destination), AMARK_NODE_PARAGRAPH, &before);
    }

    if (err == AMARK_ERROR_NONE)
    {
        amark_node_t *root = amark_document_root(destination);
        snapshot_t snapshot;

        amark_document_set_edit_tracking(destination, true);
        if (!take_snapshot(destination, &snapshot))
        {
            oom_mismatches++;
        }

        err = amark_node_import_children(root, before, top);
        if (err != AMARK_ERROR_NONE)
        {
            if (!same_snapshot(destination, &snapshot)
                || (amark_tree_check(destination, NULL) != AMARK_ERROR_NONE)
                || (amark_tree_check(document, NULL) != AMARK_ERROR_NONE))
            {
                oom_mismatches++;
            }
        }
        else
        {
            const amark_node_t *original = amark_node_first_child(top);
            const amark_node_t *copy = same ? amark_node_next(top) : amark_node_first_child(root);

            for (; original != NULL; original = amark_node_next(original))
            {
                if ((copy == NULL) || (copy == before)
                    || (amark_node_edits(copy) != AMARK_EDIT_RELOCATED)
                    || (amark_node_desc(copy) != amark_node_desc(original)))
                {
                    oom_mismatches++;
                    break;
                }

                copy = amark_node_next(copy);
            }

            if ((copy != before) || (amark_tree_check(destination, NULL) != AMARK_ERROR_NONE))
            {
                oom_mismatches++;
            }
        }
    }

    if (destination != document)
    {
        amark_document_destroy(destination);
    }

    amark_document_destroy(document);
    amark_syntax_set_destroy(other_set);
    amark_syntax_set_destroy(set);
    return err;
}

NAOS_TEST(the_import_of_children_survives_every_allocation_failure)
{
    static int same_flag;

    oom_mismatches = 0;
    AMARK_CHECK_OOM_SWEEP(import_children_scenario, NULL);
    NAOS_CHECK_UINT_EQ(0, oom_mismatches);
    AMARK_CHECK_OOM_SWEEP(import_children_scenario, &same_flag);
    NAOS_CHECK_UINT_EQ(0, oom_mismatches);
}

NAOS_SUITE(copy,
    NAOS_SET_TEARDOWN(restore),
    NAOS_ADD_TEST(a_copy_is_detached_in_the_same_document_and_equal),
    NAOS_ADD_TEST(the_copy_holds_the_attributes_literals_tokens_and_children_in_order),
    NAOS_ADD_TEST(copies_of_leaves_and_inner_nodes_are_equal),
    NAOS_ADD_TEST(a_failing_copy_function_leaves_the_document_unchanged),
    NAOS_ADD_TEST(the_root_and_null_are_rejected),
    NAOS_ADD_TEST(a_deep_chain_is_copied_without_recursion))

NAOS_SUITE(copy_records,
    NAOS_SET_TEARDOWN(restore),
    NAOS_ADD_TEST(the_copy_keeps_the_records_as_they_are),
    NAOS_ADD_TEST(a_copy_of_nodes_without_records_has_none_with_tracking_on),
    NAOS_ADD_TEST(an_inserted_copy_gets_relocated),
    NAOS_ADD_TEST(an_inserted_copy_of_a_created_node_keeps_created))

NAOS_SUITE(import,
    NAOS_SET_TEARDOWN(restore),
    NAOS_ADD_TEST(an_import_maps_the_types_and_copies_the_referenced_sources),
    NAOS_ADD_TEST(an_import_into_a_set_without_a_type_is_rejected),
    NAOS_ADD_TEST(an_import_in_the_same_document_is_a_copy),
    NAOS_ADD_TEST(an_import_of_a_root_or_null_is_rejected),
    NAOS_ADD_TEST(a_deep_chain_is_imported_without_recursion))

NAOS_SUITE(import_children,
    NAOS_SET_TEARDOWN(restore),
    NAOS_ADD_TEST(the_children_go_in_order_before_a_child_or_at_the_end),
    NAOS_ADD_TEST(a_whole_document_is_imported_with_a_shared_source_copied_once),
    NAOS_ADD_TEST(the_import_records_children_relocated_and_descendants),
    NAOS_ADD_TEST(the_records_of_the_source_are_kept_and_propagate),
    NAOS_ADD_TEST(rejected_imports_leave_the_destination_unchanged),
    NAOS_ADD_TEST(a_source_parent_without_children_does_nothing),
    NAOS_ADD_TEST(an_import_in_the_same_document_copies_the_children_at_the_call))

NAOS_SUITE(copy_oom,
    NAOS_SET_TEARDOWN(restore),
    NAOS_ADD_TEST(the_copy_survives_every_allocation_failure),
    NAOS_ADD_TEST(the_import_survives_every_allocation_failure),
    NAOS_ADD_TEST(the_import_of_children_survives_every_allocation_failure))

NAOS_MAIN(
    NAOS_ADD_SUITE(copy),
    NAOS_ADD_SUITE(copy_records),
    NAOS_ADD_SUITE(import),
    NAOS_ADD_SUITE(import_children),
    NAOS_ADD_SUITE(copy_oom)
)
