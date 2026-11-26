/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The tests of Layout (syntax-tree): the line ranges of the blocks, the margins
 * of the containers, the blank lines and the layout tokens, recorded in order
 * and read back; the checks of the records (a line outside the line table or the
 * range of the block, a column beyond the width, a boundary inside a character
 * other than a tab, a lazy margin with columns, a margin on a type without
 * margins, a blank line on a type without block children, the order); the
 * lookup of a margin by line; the discarding of a margin; the change
 * notification; the internal check of the layout; the linear time of recording
 * and the allocation failures (7.1-7.8, 11.2, 14.6).
 */
#include "tree/tree.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <naos/naos.h>

#include "test_allocator.h"

/* ---- Helpers ------------------------------------------------------------------- */

/* A document of the built-in syntax set with one finished source holding text. */
static amark_document_t *new_document(const char *text)
{
    amark_document_t *document = NULL;
    amark_source_id_t source = 0;

    if (amark_document_create(NULL, &document) != AMARK_ERROR_NONE)
    {
        return NULL;
    }
    if (amark_document_add_source(document, text, strlen(text), AMARK_SOURCE_FINISHED,
                                  &source) != AMARK_ERROR_NONE)
    {
        amark_document_destroy(document);
        return NULL;
    }
    return document;
}

static amark_node_t *new_node(amark_document_t *document, amark_node_type_t type)
{
    amark_node_t *node = NULL;

    if (amark_node_create(document, type, &node) != AMARK_ERROR_NONE)
    {
        return NULL;
    }
    return node;
}

static amark_error_t add_margin(amark_node_t *node, amark_line_id_t line,
                                amark_margin_state_t state, uint32_t flags, uint32_t start,
                                uint32_t end)
{
    amark_margin_t margin;

    memset(&margin, 0, sizeof(margin));
    margin.struct_size = sizeof(margin);
    margin.line = line;
    margin.state = state;
    margin.flags = flags;
    margin.column_start = start;
    margin.column_end = end;
    margin.role = 0;
    return amark_block_add_margin(node, &margin);
}

static amark_error_t add_blank(amark_node_t *node, amark_line_id_t line, uint32_t start,
                               uint32_t end)
{
    amark_blank_line_t blank;

    memset(&blank, 0, sizeof(blank));
    blank.struct_size = sizeof(blank);
    blank.line = line;
    blank.column_start = start;
    blank.column_end = end;
    return amark_block_add_blank_line(node, &blank);
}

static amark_error_t add_token(amark_node_t *node, amark_line_id_t line, uint32_t start,
                               uint32_t end, amark_spell_kind_t kind, amark_facet_t facet,
                               uint16_t role)
{
    amark_layout_token_t token;

    memset(&token, 0, sizeof(token));
    token.struct_size = sizeof(token);
    token.line = line;
    token.column_start = start;
    token.column_end = end;
    token.kind = kind;
    token.facet = facet;
    token.role = role;
    return amark_block_add_token(node, &token);
}

/* Checks margin index of node. */
#define CHECK_MARGIN(node, index, line_, state_, flags_, start_, end_) \
    do { \
        amark_margin_t m_; \
        memset(&m_, 0, sizeof(m_)); \
        m_.struct_size = sizeof(m_); \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_margin_at((node), (index), &m_)); \
        NAOS_CHECK_UINT_EQ((line_), m_.line); \
        NAOS_CHECK_INT_EQ((state_), m_.state); \
        NAOS_CHECK_UINT_EQ((flags_), m_.flags); \
        NAOS_CHECK_UINT_EQ((start_), m_.column_start); \
        NAOS_CHECK_UINT_EQ((end_), m_.column_end); \
    } while (0)

#define CHECK_BLANK(node, index, line_, start_, end_) \
    do { \
        amark_blank_line_t b_; \
        memset(&b_, 0, sizeof(b_)); \
        b_.struct_size = sizeof(b_); \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_blank_line_at((node), (index), &b_)); \
        NAOS_CHECK_UINT_EQ((line_), b_.line); \
        NAOS_CHECK_UINT_EQ((start_), b_.column_start); \
        NAOS_CHECK_UINT_EQ((end_), b_.column_end); \
    } while (0)

#define CHECK_LINES(node, first_, count_) \
    do { \
        amark_line_id_t f_ = 0; \
        uint32_t c_ = 0; \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_lines((node), &f_, &c_)); \
        NAOS_CHECK_UINT_EQ((first_), f_); \
        NAOS_CHECK_UINT_EQ((count_), c_); \
    } while (0)

#define CHECK_TREE_OK(document) \
    NAOS_CHECK_STR_EQ("none", amark_tree_violation_name(amark_tree_check((document), NULL)))

/* The state of the records of a node, to see that a refused record changed
 * nothing. */
typedef struct layout_snapshot
{
    amark_line_id_t first;
    uint32_t count;
    size_t margins;
    size_t blanks;
    size_t tokens;
    uint32_t dropped;
} layout_snapshot_t;

static layout_snapshot_t snapshot(const amark_node_t *node)
{
    layout_snapshot_t s;

    s.first = AMARK_LINE_NONE;
    s.count = 0;
    (void)amark_block_lines(node, &s.first, &s.count);
    s.margins = amark_block_margin_count(node);
    s.blanks = amark_block_blank_line_count(node);
    s.tokens = amark_block_token_count(node);
    s.dropped = node->dropped;
    return s;
}

/* The record is refused as an invalid argument, the node keeps its records, and
 * no change is notified (the cache stays valid). */
#define CHECK_REFUSED(node, call) \
    do { \
        layout_snapshot_t before_ = snapshot(node); \
        layout_snapshot_t after_; \
        (node)->cache_flags |= AMARK_TREE_CACHE_VALID; \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, (call)); \
        after_ = snapshot(node); \
        NAOS_CHECK_UINT_EQ(before_.first, after_.first); \
        NAOS_CHECK_UINT_EQ(before_.count, after_.count); \
        NAOS_CHECK_UINT_EQ(before_.margins, after_.margins); \
        NAOS_CHECK_UINT_EQ(before_.blanks, after_.blanks); \
        NAOS_CHECK_UINT_EQ(before_.tokens, after_.tokens); \
        NAOS_CHECK_UINT_EQ(before_.dropped, after_.dropped); \
        NAOS_CHECK_UINT_EQ(AMARK_TREE_CACHE_VALID, \
                           (node)->cache_flags & AMARK_TREE_CACHE_VALID); \
        (node)->cache_flags &= ~(uint32_t)AMARK_TREE_CACHE_VALID; \
    } while (0)

/* ---- Line ranges (7.1) ------------------------------------------------------------ */

NAOS_TEST(a_new_block_has_no_line_range)
{
    amark_document_t *document = new_document("a\nb\n");
    amark_node_t *paragraph;
    amark_node_t *text;

    NAOS_REQUIRE_TRUE(document != NULL);
    paragraph = new_node(document, AMARK_NODE_PARAGRAPH);
    text = new_node(document, AMARK_NODE_TEXT);
    NAOS_REQUIRE_TRUE(paragraph != NULL && text != NULL);

    CHECK_LINES(paragraph, AMARK_LINE_NONE, 0);
    NAOS_CHECK_UINT_EQ(0, amark_block_margin_count(paragraph));
    NAOS_CHECK_UINT_EQ(0, amark_block_blank_line_count(paragraph));
    NAOS_CHECK_UINT_EQ(0, amark_block_token_count(paragraph));
    NAOS_CHECK_TRUE(paragraph->records == NULL);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_block_lines(text, NULL, NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_block_lines(NULL, NULL, NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_lines(paragraph, NULL, NULL));
    NAOS_CHECK_UINT_EQ(0, amark_block_margin_count(NULL));
    NAOS_CHECK_UINT_EQ(0, amark_block_blank_line_count(NULL));
    NAOS_CHECK_UINT_EQ(0, amark_block_token_count(NULL));

    amark_document_destroy(document);
}

NAOS_TEST(set_lines_records_the_range_within_the_line_table)
{
    amark_document_t *document = new_document("a\nb\nc\n");
    amark_node_t *paragraph;
    amark_node_t *text;

    NAOS_REQUIRE_TRUE(document != NULL);
    paragraph = new_node(document, AMARK_NODE_PARAGRAPH);
    text = new_node(document, AMARK_NODE_TEXT);
    NAOS_REQUIRE_TRUE(paragraph != NULL && text != NULL);

    paragraph->cache_flags |= AMARK_TREE_CACHE_VALID;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(paragraph, 1, 2));
    CHECK_LINES(paragraph, 1, 2);
    NAOS_CHECK_UINT_EQ(0, paragraph->cache_flags & AMARK_TREE_CACHE_VALID);
    /* The same range changes nothing. */
    paragraph->cache_flags |= AMARK_TREE_CACHE_VALID;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(paragraph, 1, 2));
    NAOS_CHECK_UINT_EQ(AMARK_TREE_CACHE_VALID, paragraph->cache_flags & AMARK_TREE_CACHE_VALID);
    paragraph->cache_flags = 0;

    /* Outside the line table, or not a block. */
    CHECK_REFUSED(paragraph, amark_block_set_lines(paragraph, 3, 1));
    CHECK_REFUSED(paragraph, amark_block_set_lines(paragraph, 2, 2));
    CHECK_REFUSED(paragraph, amark_block_set_lines(paragraph, 1, UINT32_MAX));
    CHECK_REFUSED(paragraph, amark_block_set_lines(paragraph, AMARK_LINE_NONE, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_block_set_lines(text, 0, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_block_set_lines(NULL, 0, 1));

    /* Count 0 removes the range. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(paragraph, 5, 0));
    CHECK_LINES(paragraph, AMARK_LINE_NONE, 0);
    CHECK_TREE_OK(document);

    amark_document_destroy(document);
}

NAOS_TEST(set_lines_cannot_leave_out_a_record)
{
    amark_document_t *document = new_document("> a\n> b\n> c\n");
    amark_node_t *quote;

    NAOS_REQUIRE_TRUE(document != NULL);
    quote = new_node(document, AMARK_NODE_BLOCK_QUOTE);
    NAOS_REQUIRE_TRUE(quote != NULL);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(quote, 0, 3));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, add_margin(quote, 1, AMARK_MARGIN_PRESENT, 0, 0, 2));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, add_token(quote, 2, 2, 3, AMARK_SPELL_TEXT, 0, 0));

    CHECK_REFUSED(quote, amark_block_set_lines(quote, 2, 1));
    CHECK_REFUSED(quote, amark_block_set_lines(quote, 0, 2));
    CHECK_REFUSED(quote, amark_block_set_lines(quote, 0, 0));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(quote, 1, 2));
    CHECK_LINES(quote, 1, 2);
    CHECK_TREE_OK(document);

    amark_document_destroy(document);
}

/* ---- INPUT.md 3.1: a paragraph in a list in a block quote (7.1, 7.2) ---------------- */

NAOS_TEST(input_3_1_margins_can_be_recorded)
{
    amark_document_t *document = new_document("> - one *two\n>   three* four\n");
    amark_node_t *root;
    amark_node_t *quote;
    amark_node_t *list;
    amark_node_t *item;
    amark_node_t *paragraph;
    amark_margin_t margin;
    bool found = false;

    NAOS_REQUIRE_TRUE(document != NULL);
    root = amark_document_root(document);
    quote = new_node(document, AMARK_NODE_BLOCK_QUOTE);
    list = new_node(document, AMARK_NODE_LIST);
    item = new_node(document, AMARK_NODE_ITEM);
    paragraph = new_node(document, AMARK_NODE_PARAGRAPH);
    NAOS_REQUIRE_TRUE(quote != NULL && list != NULL && item != NULL && paragraph != NULL);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(root, quote));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(quote, list));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(list, item));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(item, paragraph));

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(root, 0, 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(quote, 0, 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_margin(quote, 0, AMARK_MARGIN_PRESENT, 0, 0, 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_margin(quote, 1, AMARK_MARGIN_PRESENT, 0, 0, 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(list, 0, 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(item, 0, 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      add_margin(item, 0, AMARK_MARGIN_PRESENT, AMARK_MARGIN_FIRST, 2, 4));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_margin(item, 1, AMARK_MARGIN_PRESENT, 0, 2, 4));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(paragraph, 0, 2));

    CHECK_LINES(root, 0, 2);
    CHECK_LINES(quote, 0, 2);
    CHECK_LINES(paragraph, 0, 2);
    NAOS_CHECK_UINT_EQ(2, amark_block_margin_count(quote));
    CHECK_MARGIN(quote, 0, 0, AMARK_MARGIN_PRESENT, 0, 0, 2);
    CHECK_MARGIN(quote, 1, 1, AMARK_MARGIN_PRESENT, 0, 0, 2);
    NAOS_CHECK_UINT_EQ(2, amark_block_margin_count(item));
    CHECK_MARGIN(item, 0, 0, AMARK_MARGIN_PRESENT, AMARK_MARGIN_FIRST, 2, 4);
    CHECK_MARGIN(item, 1, 1, AMARK_MARGIN_PRESENT, 0, 2, 4);
    NAOS_CHECK_UINT_EQ(0, amark_block_margin_count(list));

    /* The margins of each ancestor of the paragraph on its lines (11.2, 11.5). */
    memset(&margin, 0, sizeof(margin));
    margin.struct_size = sizeof(margin);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_find_margin(item, 1, &margin, &found));
    NAOS_CHECK_TRUE(found);
    NAOS_CHECK_UINT_EQ(2, margin.column_start);
    NAOS_CHECK_UINT_EQ(4, margin.column_end);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_find_margin(quote, 0, &margin, &found));
    NAOS_CHECK_TRUE(found);
    NAOS_CHECK_UINT_EQ(0, margin.column_start);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_find_margin(list, 0, &margin, &found));
    NAOS_CHECK_FALSE(found);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_find_margin(root, 1, NULL, &found));
    NAOS_CHECK_FALSE(found);
    CHECK_TREE_OK(document);

    amark_document_destroy(document);
}

/* ---- INPUT.md 3.4: lazy continuation (7.3) ---------------------------------------- */

NAOS_TEST(input_3_4_lazy_margins_can_be_recorded)
{
    amark_document_t *document = new_document("> a\nb\n");
    amark_node_t *root;
    amark_node_t *quote;
    amark_node_t *paragraph;
    amark_margin_t margin;
    bool found = false;

    NAOS_REQUIRE_TRUE(document != NULL);
    root = amark_document_root(document);
    quote = new_node(document, AMARK_NODE_BLOCK_QUOTE);
    paragraph = new_node(document, AMARK_NODE_PARAGRAPH);
    NAOS_REQUIRE_TRUE(quote != NULL && paragraph != NULL);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(root, quote));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(quote, paragraph));

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(root, 0, 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(quote, 0, 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_margin(quote, 0, AMARK_MARGIN_PRESENT, 0, 0, 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_margin(quote, 1, AMARK_MARGIN_LAZY, 0, 0, 0));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(paragraph, 0, 2));

    CHECK_MARGIN(quote, 1, 1, AMARK_MARGIN_LAZY, 0, 0, 0);
    memset(&margin, 0, sizeof(margin));
    margin.struct_size = sizeof(margin);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_find_margin(quote, 1, &margin, &found));
    NAOS_CHECK_TRUE(found);
    NAOS_CHECK_INT_EQ(AMARK_MARGIN_LAZY, margin.state);
    CHECK_TREE_OK(document);

    amark_document_destroy(document);
}

NAOS_TEST(nested_lazy_margins_sit_where_the_outer_margin_ends)
{
    /* "> - a" then "b": both the block quote and the item leave out their margin
     * on line 1, at column 0 (no outer margin). On "> - a" then "> b" the item
     * margin is lazy at column 2, where the block quote margin ends. */
    amark_document_t *document = new_document("> - a\nb\n> - c\n> d\n");
    amark_node_t *quote;
    amark_node_t *item;
    amark_node_t *quote2;
    amark_node_t *item2;

    NAOS_REQUIRE_TRUE(document != NULL);
    quote = new_node(document, AMARK_NODE_BLOCK_QUOTE);
    item = new_node(document, AMARK_NODE_ITEM);
    quote2 = new_node(document, AMARK_NODE_BLOCK_QUOTE);
    item2 = new_node(document, AMARK_NODE_ITEM);
    NAOS_REQUIRE_TRUE(quote != NULL && item != NULL && quote2 != NULL && item2 != NULL);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(quote, 0, 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(item, 0, 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_margin(quote, 0, AMARK_MARGIN_PRESENT, 0, 0, 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_margin(quote, 1, AMARK_MARGIN_LAZY, 0, 0, 0));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      add_margin(item, 0, AMARK_MARGIN_PRESENT, AMARK_MARGIN_FIRST, 2, 4));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_margin(item, 1, AMARK_MARGIN_LAZY, 0, 0, 0));

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(quote2, 2, 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(item2, 2, 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_margin(quote2, 2, AMARK_MARGIN_PRESENT, 0, 0, 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_margin(quote2, 3, AMARK_MARGIN_PRESENT, 0, 0, 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      add_margin(item2, 2, AMARK_MARGIN_PRESENT, AMARK_MARGIN_FIRST, 2, 4));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_margin(item2, 3, AMARK_MARGIN_LAZY, 0, 2, 2));
    CHECK_MARGIN(item2, 1, 3, AMARK_MARGIN_LAZY, 0, 2, 2);
    CHECK_TREE_OK(document);

    amark_document_destroy(document);
}

/* ---- Blank lines (7.4) ------------------------------------------------------------- */

NAOS_TEST(blank_lines_belong_to_the_innermost_block_with_block_children)
{
    /* L0 "- a", L1 "" (between the items: the list), L2 "- b", L3 "  " (after
     * the list, whose range ends at its last item: the document), L4 ">" (in
     * the block quote: the block quote), L5 "> c". */
    amark_document_t *document = new_document("- a\n\n- b\n  \n>\n> c\n");
    amark_node_t *root;
    amark_node_t *list;
    amark_node_t *item1;
    amark_node_t *item2;
    amark_node_t *quote;

    NAOS_REQUIRE_TRUE(document != NULL);
    root = amark_document_root(document);
    list = new_node(document, AMARK_NODE_LIST);
    item1 = new_node(document, AMARK_NODE_ITEM);
    item2 = new_node(document, AMARK_NODE_ITEM);
    quote = new_node(document, AMARK_NODE_BLOCK_QUOTE);
    NAOS_REQUIRE_TRUE(list != NULL && item1 != NULL && item2 != NULL && quote != NULL);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(root, 0, 6));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(list, 0, 3));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(item1, 0, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(item2, 2, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(quote, 4, 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_blank(list, 1, 0, 0));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_blank(root, 3, 0, 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_margin(quote, 4, AMARK_MARGIN_PRESENT, 0, 0, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_margin(quote, 5, AMARK_MARGIN_PRESENT, 0, 0, 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_blank(quote, 4, 1, 1));

    NAOS_CHECK_UINT_EQ(1, amark_block_blank_line_count(list));
    CHECK_BLANK(list, 0, 1, 0, 0);
    NAOS_CHECK_UINT_EQ(1, amark_block_blank_line_count(root));
    CHECK_BLANK(root, 0, 3, 0, 2);
    NAOS_CHECK_UINT_EQ(1, amark_block_blank_line_count(quote));
    CHECK_BLANK(quote, 0, 4, 1, 1);
    CHECK_TREE_OK(document);

    amark_document_destroy(document);
}

NAOS_TEST(an_item_has_an_empty_present_margin_on_a_blank_line)
{
    /* "- a", "", "  b": the item covers the three lines and holds the blank
     * line between its paragraphs; its margin on the blank line is present and
     * empty. */
    amark_document_t *document = new_document("- a\n\n  b\n");
    amark_node_t *item;

    NAOS_REQUIRE_TRUE(document != NULL);
    item = new_node(document, AMARK_NODE_ITEM);
    NAOS_REQUIRE_TRUE(item != NULL);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(item, 0, 3));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      add_margin(item, 0, AMARK_MARGIN_PRESENT, AMARK_MARGIN_FIRST, 0, 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_margin(item, 1, AMARK_MARGIN_PRESENT, 0, 0, 0));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_margin(item, 2, AMARK_MARGIN_PRESENT, 0, 0, 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_blank(item, 1, 0, 0));
    CHECK_MARGIN(item, 1, 1, AMARK_MARGIN_PRESENT, 0, 0, 0);
    CHECK_BLANK(item, 0, 1, 0, 0);
    CHECK_TREE_OK(document);

    amark_document_destroy(document);
}

/* ---- Layout tokens and tabs (7.5, 7.6) ---------------------------------------------- */

NAOS_TEST(input_3_3_a_tab_is_shared_by_columns)
{
    /* ">\t\tfoo": the margin [0,2) ends inside the first tab, the indentation
     * [2,6) of the code block ends inside the second one. */
    amark_document_t *document = new_document(">\t\tfoo");
    amark_node_t *quote;
    amark_node_t *code;
    amark_layout_token_t token;

    NAOS_REQUIRE_TRUE(document != NULL);
    quote = new_node(document, AMARK_NODE_BLOCK_QUOTE);
    code = new_node(document, AMARK_NODE_CODE_BLOCK);
    NAOS_REQUIRE_TRUE(quote != NULL && code != NULL);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(quote, 0, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_margin(quote, 0, AMARK_MARGIN_PRESENT, 0, 0, 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(code, 0, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_token(code, 0, 2, 6, AMARK_SPELL_SPACE, 0, 1));
    NAOS_CHECK_UINT_EQ(1, amark_block_token_count(code));
    memset(&token, 0, sizeof(token));
    token.struct_size = sizeof(token);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_token_at(code, 0, &token));
    NAOS_CHECK_UINT_EQ(0, token.line);
    NAOS_CHECK_UINT_EQ(2, token.column_start);
    NAOS_CHECK_UINT_EQ(6, token.column_end);
    NAOS_CHECK_INT_EQ(AMARK_SPELL_SPACE, token.kind);
    NAOS_CHECK_UINT_EQ(AMARK_FACET_NONE, token.facet);
    NAOS_CHECK_UINT_EQ(1, token.role);
    CHECK_TREE_OK(document);

    amark_document_destroy(document);
}

NAOS_TEST(tokens_are_recorded_in_the_order_of_lines_and_columns)
{
    /* A fenced code block: "```rust", "x", "```". */
    amark_document_t *document = new_document("```rust\nx\n```\n");
    amark_node_t *code;
    amark_layout_token_t token;

    NAOS_REQUIRE_TRUE(document != NULL);
    code = new_node(document, AMARK_NODE_CODE_BLOCK);
    NAOS_REQUIRE_TRUE(code != NULL);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(code, 0, 3));

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_token(code, 0, 0, 3, AMARK_SPELL_MARKER, 0, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      add_token(code, 0, 3, 7, AMARK_SPELL_TEXT, AMARK_CODE_BLOCK_INFO, 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_token(code, 2, 0, 3, AMARK_SPELL_MARKER, 0, 3));
    NAOS_CHECK_UINT_EQ(3, amark_block_token_count(code));
    memset(&token, 0, sizeof(token));
    token.struct_size = sizeof(token);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_token_at(code, 1, &token));
    NAOS_CHECK_UINT_EQ(AMARK_CODE_BLOCK_INFO, token.facet);
    NAOS_CHECK_INT_EQ(AMARK_SPELL_TEXT, token.kind);

    /* Overlapping, before the last one, or empty. */
    CHECK_REFUSED(code, add_token(code, 2, 2, 3, AMARK_SPELL_SPACE, 0, 0));
    CHECK_REFUSED(code, add_token(code, 1, 0, 1, AMARK_SPELL_TEXT, 0, 0));
    CHECK_REFUSED(code, add_token(code, 2, 3, 3, AMARK_SPELL_SPACE, 0, 0));
    /* The kinds that are not layout tokens, an unknown kind, a facet the type
     * does not have. */
    CHECK_REFUSED(code, add_token(code, 2, 3, 3, AMARK_SPELL_LINE_BREAK, 0, 0));
    CHECK_REFUSED(code, add_token(code, 2, 0, 3, AMARK_SPELL_LINE_BREAK, 0, 0));
    CHECK_REFUSED(code, add_token(code, 2, 0, 3, AMARK_SPELL_TAB_REMAINDER, 0, 0));
    CHECK_REFUSED(code, add_token(code, 2, 0, 3, (amark_spell_kind_t)0, 0, 0));
    CHECK_REFUSED(code, add_token(code, 2, 0, 3, (amark_spell_kind_t)10, 0, 0));
    CHECK_REFUSED(code, add_token(code, 2, 0, 3, AMARK_SPELL_TEXT, 30, 0));
    CHECK_TREE_OK(document);

    amark_document_destroy(document);
}

/* ---- The checks of the records (7.7) --------------------------------------------- */

NAOS_TEST(records_outside_the_lines_or_columns_are_refused)
{
    /* L0 "> \xC3\xA9x" (the 2-byte e-acute spans columns 2 and 3), L1 "> b". */
    amark_document_t *document = new_document("> \xC3\xA9x\n> b\n");
    amark_node_t *root;
    amark_node_t *quote;
    amark_node_t *paragraph;
    amark_margin_t margin;

    NAOS_REQUIRE_TRUE(document != NULL);
    root = amark_document_root(document);
    quote = new_node(document, AMARK_NODE_BLOCK_QUOTE);
    paragraph = new_node(document, AMARK_NODE_PARAGRAPH);
    NAOS_REQUIRE_TRUE(quote != NULL && paragraph != NULL);

    /* Without a line range, every record is outside it. */
    CHECK_REFUSED(quote, add_margin(quote, 0, AMARK_MARGIN_PRESENT, 0, 0, 2));
    CHECK_REFUSED(root, add_blank(root, 0, 0, 0));
    CHECK_REFUSED(paragraph, add_token(paragraph, 0, 0, 1, AMARK_SPELL_TEXT, 0, 0));

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(quote, 1, 1));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(paragraph, 0, 1));
    /* Outside the range of the block, outside the line table. */
    CHECK_REFUSED(quote, add_margin(quote, 0, AMARK_MARGIN_PRESENT, 0, 0, 2));
    CHECK_REFUSED(quote, add_margin(quote, 2, AMARK_MARGIN_PRESENT, 0, 0, 2));
    CHECK_REFUSED(quote, add_margin(quote, AMARK_LINE_NONE, AMARK_MARGIN_PRESENT, 0, 0, 2));
    CHECK_REFUSED(paragraph, add_token(paragraph, 1, 0, 1, AMARK_SPELL_TEXT, 0, 0));
    /* Beyond the width (3 on L1, 5 on L0), or reversed. */
    CHECK_REFUSED(quote, add_margin(quote, 1, AMARK_MARGIN_PRESENT, 0, 0, 4));
    CHECK_REFUSED(quote, add_margin(quote, 1, AMARK_MARGIN_PRESENT, 0, 2, 1));
    CHECK_REFUSED(paragraph, add_token(paragraph, 0, 4, 6, AMARK_SPELL_TEXT, 0, 0));
    /* A boundary inside the e-acute. */
    CHECK_REFUSED(paragraph, add_token(paragraph, 0, 2, 3, AMARK_SPELL_TEXT, 0, 0));
    CHECK_REFUSED(paragraph, add_token(paragraph, 0, 3, 5, AMARK_SPELL_TEXT, 0, 0));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      add_token(paragraph, 0, 2, 4, AMARK_SPELL_TEXT, 0, 0));
    /* A wrong state or flag, a lazy margin with columns, a present margin
     * without columns on a line that is not blank. */
    CHECK_REFUSED(quote, add_margin(quote, 1, (amark_margin_state_t)0, 0, 0, 2));
    CHECK_REFUSED(quote, add_margin(quote, 1, (amark_margin_state_t)3, 0, 0, 2));
    CHECK_REFUSED(quote, add_margin(quote, 1, AMARK_MARGIN_PRESENT, 0x02u, 0, 2));
    CHECK_REFUSED(quote, add_margin(quote, 1, AMARK_MARGIN_LAZY, 0, 0, 2));
    CHECK_REFUSED(quote, add_margin(quote, 1, AMARK_MARGIN_LAZY, 0, 1, 2));
    CHECK_REFUSED(quote, add_margin(quote, 1, AMARK_MARGIN_PRESENT, 0, 0, 0));
    /* NULL and a short struct_size. */
    memset(&margin, 0, sizeof(margin));
    margin.struct_size = sizeof(margin) - 1;
    margin.line = 1;
    margin.state = AMARK_MARGIN_PRESENT;
    margin.column_end = 2;
    CHECK_REFUSED(quote, amark_block_add_margin(quote, &margin));
    CHECK_REFUSED(quote, amark_block_add_margin(quote, NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_block_add_margin(NULL, &margin));
    margin.struct_size = sizeof(margin);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_margin(quote, &margin));
    CHECK_TREE_OK(document);

    amark_document_destroy(document);
}

NAOS_TEST(records_on_types_that_cannot_hold_them_are_refused)
{
    amark_document_t *document = new_document("a\n\n");
    amark_node_t *paragraph;
    amark_node_t *list;
    amark_node_t *text;

    NAOS_REQUIRE_TRUE(document != NULL);
    paragraph = new_node(document, AMARK_NODE_PARAGRAPH);
    list = new_node(document, AMARK_NODE_LIST);
    text = new_node(document, AMARK_NODE_TEXT);
    NAOS_REQUIRE_TRUE(paragraph != NULL && list != NULL && text != NULL);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(paragraph, 0, 2));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(list, 0, 2));

    /* A margin on a type without margins (a leaf, a list). */
    CHECK_REFUSED(paragraph, add_margin(paragraph, 0, AMARK_MARGIN_PRESENT, 0, 0, 1));
    CHECK_REFUSED(list, add_margin(list, 0, AMARK_MARGIN_PRESENT, 0, 0, 1));
    /* A blank line on a type that cannot have block children. */
    CHECK_REFUSED(paragraph, add_blank(paragraph, 1, 0, 0));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, add_blank(text, 1, 0, 0));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      add_token(text, 0, 0, 1, AMARK_SPELL_TEXT, 0, 0));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      add_margin(text, 0, AMARK_MARGIN_PRESENT, 0, 0, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_blank(list, 1, 0, 0));
    CHECK_TREE_OK(document);

    amark_document_destroy(document);
}

NAOS_TEST(blank_lines_must_be_blank_to_the_end_of_the_line)
{
    amark_document_t *document = new_document(" \t \nx \n\n");
    amark_node_t *root;

    NAOS_REQUIRE_TRUE(document != NULL);
    root = amark_document_root(document);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(root, 0, 3));

    /* L0 " \t " is 5 columns wide. */
    CHECK_REFUSED(root, add_blank(root, 0, 0, 4));
    CHECK_REFUSED(root, add_blank(root, 0, 0, 6));
    CHECK_REFUSED(root, add_blank(root, 0, 6, 5));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_blank(root, 0, 2, 5));
    /* L1 "x " is not blank from column 0. */
    CHECK_REFUSED(root, add_blank(root, 1, 0, 2));
    /* Not after the last blank line. */
    CHECK_REFUSED(root, add_blank(root, 0, 0, 5));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_blank(root, 2, 0, 0));
    CHECK_BLANK(root, 0, 0, 2, 5);
    CHECK_BLANK(root, 1, 2, 0, 0);
    CHECK_TREE_OK(document);

    amark_document_destroy(document);
}

NAOS_TEST(margins_out_of_order_are_refused_and_the_node_is_unchanged)
{
    amark_document_t *document = new_document("> a\n> b\n> c\n");
    amark_node_t *quote;

    NAOS_REQUIRE_TRUE(document != NULL);
    quote = new_node(document, AMARK_NODE_BLOCK_QUOTE);
    NAOS_REQUIRE_TRUE(quote != NULL);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(quote, 0, 3));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, add_margin(quote, 1, AMARK_MARGIN_PRESENT, 0, 0, 2));

    /* The same line again, an earlier line. */
    CHECK_REFUSED(quote, add_margin(quote, 1, AMARK_MARGIN_PRESENT, 0, 0, 2));
    CHECK_REFUSED(quote, add_margin(quote, 0, AMARK_MARGIN_PRESENT, 0, 0, 2));
    CHECK_MARGIN(quote, 0, 1, AMARK_MARGIN_PRESENT, 0, 0, 2);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_margin(quote, 2, AMARK_MARGIN_PRESENT, 0, 0, 1));
    NAOS_CHECK_UINT_EQ(2, amark_block_margin_count(quote));
    CHECK_TREE_OK(document);

    amark_document_destroy(document);
}

NAOS_TEST(readers_refuse_bad_arguments)
{
    amark_document_t *document = new_document("> a\n");
    amark_node_t *quote;
    amark_margin_t margin;
    amark_blank_line_t blank;
    amark_layout_token_t token;
    bool found = true;

    NAOS_REQUIRE_TRUE(document != NULL);
    quote = new_node(document, AMARK_NODE_BLOCK_QUOTE);
    NAOS_REQUIRE_TRUE(quote != NULL);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(quote, 0, 1));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, add_margin(quote, 0, AMARK_MARGIN_PRESENT, 0, 0, 2));
    memset(&margin, 0, sizeof(margin));
    memset(&blank, 0, sizeof(blank));
    memset(&token, 0, sizeof(token));
    margin.struct_size = sizeof(margin);
    blank.struct_size = sizeof(blank);
    token.struct_size = sizeof(token);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_block_margin_at(quote, 1, &margin));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_block_margin_at(NULL, 0, &margin));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_block_margin_at(quote, 0, NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_block_blank_line_at(quote, 0, &blank));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_block_token_at(quote, 0, &token));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_block_find_margin(NULL, 0, &margin, &found));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_block_find_margin(quote, 0, &margin, NULL));
    margin.struct_size = sizeof(margin) - 1;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_block_margin_at(quote, 0, &margin));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_block_find_margin(quote, 0, &margin, &found));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_find_margin(quote, 0, NULL, &found));
    NAOS_CHECK_TRUE(found);

    amark_document_destroy(document);
}

/* ---- Lookup and discarding (11.2) -------------------------------------------------- */

NAOS_TEST(find_margin_searches_the_margins_by_line)
{
    char text[64 * 3 + 1];
    amark_document_t *document;
    amark_node_t *quote;
    amark_line_id_t line;
    size_t i;

    for (i = 0; i < 64; i++)
    {
        memcpy(text + 3 * i, ">a\n", 3);
    }
    text[64 * 3] = '\0';
    document = new_document(text);
    NAOS_REQUIRE_TRUE(document != NULL);
    quote = new_node(document, AMARK_NODE_BLOCK_QUOTE);
    NAOS_REQUIRE_TRUE(quote != NULL);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(quote, 0, 64));
    /* A margin on the even lines only. */
    for (line = 0; line < 64; line += 2)
    {
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                            add_margin(quote, line, AMARK_MARGIN_PRESENT, 0, 0, 1));
    }

    for (line = 0; line < 70; line++)
    {
        amark_margin_t margin;
        bool found = false;

        memset(&margin, 0, sizeof(margin));
        margin.struct_size = sizeof(margin);
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_find_margin(quote, line, &margin, &found));
        NAOS_CHECK_INT_EQ((line < 64) && (line % 2 == 0), found);
        if (found)
        {
            NAOS_CHECK_UINT_EQ(line, margin.line);
        }
    }

    amark_document_destroy(document);
}

NAOS_TEST(discard_margin_removes_it_and_drops_the_structural_spelling)
{
    amark_document_t *document = new_document("> a\n> b\n> c\n");
    amark_node_t *root;
    amark_node_t *quote;
    bool found = true;

    NAOS_REQUIRE_TRUE(document != NULL);
    root = amark_document_root(document);
    quote = new_node(document, AMARK_NODE_BLOCK_QUOTE);
    NAOS_REQUIRE_TRUE(quote != NULL);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(root, quote));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(quote, 0, 3));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, add_margin(quote, 0, AMARK_MARGIN_PRESENT, 0, 0, 2));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, add_margin(quote, 1, AMARK_MARGIN_PRESENT, 0, 0, 2));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, add_margin(quote, 2, AMARK_MARGIN_PRESENT, 0, 0, 2));
    NAOS_CHECK_UINT_EQ(0, quote->dropped);

    root->cache_flags |= AMARK_TREE_CACHE_VALID;
    quote->cache_flags |= AMARK_TREE_CACHE_VALID;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_discard_margin(quote, 1));
    NAOS_CHECK_UINT_EQ(0, quote->cache_flags & AMARK_TREE_CACHE_VALID);
    NAOS_CHECK_UINT_EQ(0, root->cache_flags & AMARK_TREE_CACHE_VALID);
    NAOS_CHECK_UINT_EQ(AMARK_TREE_DROPPED_STRUCTURE, quote->dropped);
    NAOS_CHECK_UINT_EQ(2, amark_block_margin_count(quote));
    CHECK_MARGIN(quote, 0, 0, AMARK_MARGIN_PRESENT, 0, 0, 2);
    CHECK_MARGIN(quote, 1, 2, AMARK_MARGIN_PRESENT, 0, 0, 2);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_find_margin(quote, 1, NULL, &found));
    NAOS_CHECK_FALSE(found);

    /* No margin on the line, not a container, NULL. */
    CHECK_REFUSED(quote, amark_block_discard_margin(quote, 1));
    CHECK_REFUSED(root, amark_block_discard_margin(root, 0));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_block_discard_margin(NULL, 0));
    CHECK_TREE_OK(document);

    amark_document_destroy(document);
}

NAOS_TEST(recording_notifies_the_change)
{
    amark_document_t *document = new_document("> a\n\n");
    amark_node_t *root;
    amark_node_t *quote;

    NAOS_REQUIRE_TRUE(document != NULL);
    root = amark_document_root(document);
    quote = new_node(document, AMARK_NODE_BLOCK_QUOTE);
    NAOS_REQUIRE_TRUE(quote != NULL);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(root, quote));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(root, 0, 2));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(quote, 0, 1));

    root->cache_flags |= AMARK_TREE_CACHE_VALID;
    quote->cache_flags |= AMARK_TREE_CACHE_VALID;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_margin(quote, 0, AMARK_MARGIN_PRESENT, 0, 0, 2));
    NAOS_CHECK_UINT_EQ(0, quote->cache_flags & AMARK_TREE_CACHE_VALID);
    NAOS_CHECK_UINT_EQ(0, root->cache_flags & AMARK_TREE_CACHE_VALID);

    quote->cache_flags |= AMARK_TREE_CACHE_VALID;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_token(quote, 0, 2, 3, AMARK_SPELL_TEXT, 0, 0));
    NAOS_CHECK_UINT_EQ(0, quote->cache_flags & AMARK_TREE_CACHE_VALID);

    root->cache_flags |= AMARK_TREE_CACHE_VALID;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_blank(root, 1, 0, 0));
    NAOS_CHECK_UINT_EQ(0, root->cache_flags & AMARK_TREE_CACHE_VALID);
    /* Recording is not an edit: the spelling is kept. */
    NAOS_CHECK_UINT_EQ(0, quote->dropped);
    NAOS_CHECK_UINT_EQ(0, root->dropped);

    amark_document_destroy(document);
}

/* ---- The internal check ------------------------------------------------------------ */

NAOS_TEST(the_check_detects_a_broken_layout)
{
    amark_document_t *document = new_document("> a\n> b\n");
    amark_node_t *root;
    amark_node_t *quote;
    amark_tree_check_report_t report;
    amark_tree_margin_t saved;

    NAOS_REQUIRE_TRUE(document != NULL);
    root = amark_document_root(document);
    quote = new_node(document, AMARK_NODE_BLOCK_QUOTE);
    NAOS_REQUIRE_TRUE(quote != NULL);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(root, quote));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(quote, 0, 2));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, add_margin(quote, 0, AMARK_MARGIN_PRESENT, 0, 0, 2));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, add_margin(quote, 1, AMARK_MARGIN_PRESENT, 0, 0, 2));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, add_token(quote, 0, 2, 3, AMARK_SPELL_TEXT, 0, 0));
    CHECK_TREE_OK(document);
    NAOS_CHECK_STR_EQ("layout", amark_tree_violation_name(AMARK_TREE_VIOLATION_LAYOUT));

    /* Margins out of order. */
    saved = quote->records->margins[1];
    quote->records->margins[1].line = 0;
    NAOS_CHECK_STR_EQ("layout", amark_tree_violation_name(amark_tree_check(document, &report)));
    NAOS_CHECK_PTR_EQ(quote, report.node);
    quote->records->margins[1] = saved;
    /* A lazy margin with columns. */
    quote->records->margins[1].state = AMARK_MARGIN_LAZY;
    NAOS_CHECK_STR_EQ("layout", amark_tree_violation_name(amark_tree_check(document, NULL)));
    quote->records->margins[1] = saved;
    /* A column beyond the width. */
    quote->records->tokens[0].end = 9;
    NAOS_CHECK_STR_EQ("layout", amark_tree_violation_name(amark_tree_check(document, NULL)));
    quote->records->tokens[0].end = 3;
    /* A record outside the line range. */
    quote->line_count = 1;
    NAOS_CHECK_STR_EQ("layout", amark_tree_violation_name(amark_tree_check(document, NULL)));
    quote->line_count = 2;
    /* A range outside the line table. */
    root->line_first = 1;
    root->line_count = 2;
    NAOS_CHECK_STR_EQ("layout", amark_tree_violation_name(amark_tree_check(document, NULL)));
    root->line_count = 0;
    /* A margin on a type without margins. */
    root->records = quote->records;
    root->line_first = 0;
    root->line_count = 2;
    NAOS_CHECK_STR_EQ("layout", amark_tree_violation_name(amark_tree_check(document, NULL)));
    root->records = NULL;
    root->line_count = 0;
    /* A dropped mark of a facet the type does not have. */
    quote->dropped = 1u << 5;
    NAOS_CHECK_STR_EQ("layout", amark_tree_violation_name(amark_tree_check(document, NULL)));
    quote->dropped = 0;
    CHECK_TREE_OK(document);

    amark_document_destroy(document);
}

/* ---- Complexity (14.6) ------------------------------------------------------------- */

/* Records a margin and two tokens on each of count lines and returns the
 * record steps. */
static size_t record_steps_for(size_t count)
{
    amark_document_t *document = NULL;
    amark_node_t *quote = NULL;
    amark_node_t *code = NULL;
    char *text;
    size_t steps = (size_t)-1;
    size_t i;
    amark_source_id_t source = 0;

    text = (char *)malloc(count * 3 + 1);
    if (text == NULL)
    {
        return steps;
    }
    for (i = 0; i < count; i++)
    {
        memcpy(text + 3 * i, ">a\n", 3);
    }
    if (amark_document_create(NULL, &document) == AMARK_ERROR_NONE &&
        amark_document_add_source(document, text, count * 3, AMARK_SOURCE_FINISHED, &source) ==
            AMARK_ERROR_NONE &&
        amark_node_create(document, AMARK_NODE_BLOCK_QUOTE, &quote) == AMARK_ERROR_NONE &&
        amark_node_create(document, AMARK_NODE_CODE_BLOCK, &code) == AMARK_ERROR_NONE &&
        amark_block_set_lines(quote, 0, (uint32_t)count) == AMARK_ERROR_NONE &&
        amark_block_set_lines(code, 0, (uint32_t)count) == AMARK_ERROR_NONE)
    {
        size_t before = document->stats.record_steps;
        bool ok = true;

        for (i = 0; ok && (i < count); i++)
        {
            ok = (add_margin(quote, (amark_line_id_t)i, AMARK_MARGIN_PRESENT, 0, 0, 1) ==
                  AMARK_ERROR_NONE) &&
                 (add_token(code, (amark_line_id_t)i, 0, 1, AMARK_SPELL_MARKER, 0, 0) ==
                  AMARK_ERROR_NONE) &&
                 (add_token(code, (amark_line_id_t)i, 1, 2, AMARK_SPELL_TEXT, 0, 0) ==
                  AMARK_ERROR_NONE);
        }
        if (ok)
        {
            steps = document->stats.record_steps - before;
        }
    }
    amark_document_destroy(document);
    free(text);
    return steps;
}

NAOS_TEST(recording_time_is_linear_in_the_records)
{
    size_t small = record_steps_for(1000);
    size_t large = record_steps_for(100000);

    NAOS_REQUIRE_TRUE(small != (size_t)-1 && large != (size_t)-1);
    /* 3 records per line; each record counts one step plus at most two moved
     * by the doublings of its array. */
    NAOS_CHECK_TRUE(small >= 3000);
    NAOS_CHECK_TRUE(small <= 3 * 3000);
    NAOS_CHECK_TRUE(large <= 3 * 300000);
}

/* ---- Allocation failures ------------------------------------------------------------ */

static amark_error_t oom_scenario(void *context)
{
    amark_document_t *document = NULL;
    amark_node_t *root;
    amark_node_t *quote = NULL;
    amark_node_t *code = NULL;
    amark_source_id_t source = 0;
    amark_error_t error;
    uint32_t i;
    const char *text = ">\t\tfoo\n> b\n>\n> c\n> d\n";

    (void)context;
    error = amark_document_create(NULL, &document);
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    error = amark_document_add_source(document, text, strlen(text), AMARK_SOURCE_FINISHED,
                                      &source);
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_create(document, AMARK_NODE_BLOCK_QUOTE, &quote);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_create(document, AMARK_NODE_CODE_BLOCK, &code);
    }
    root = amark_document_root(document);
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_block_set_lines(quote, 0, 5);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_block_set_lines(code, 0, 1);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_block_set_lines(root, 0, 5);
    }
    for (i = 0; (error == AMARK_ERROR_NONE) && (i < 5); i++)
    {
        size_t before = amark_block_margin_count(quote);

        error = add_margin(quote, i, AMARK_MARGIN_PRESENT, 0, 0, (i == 0) ? 2u : 1u);
        if ((error != AMARK_ERROR_NONE) && (amark_block_margin_count(quote) != before))
        {
            error = AMARK_ERROR_INVALID_STATE;
        }
    }
    if (error == AMARK_ERROR_NONE)
    {
        /* The column conversion builds the tab index of the line. */
        error = add_token(code, 0, 2, 6, AMARK_SPELL_SPACE, 0, 0);
        if ((error != AMARK_ERROR_NONE) && (amark_block_token_count(code) != 0))
        {
            error = AMARK_ERROR_INVALID_STATE;
        }
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = add_blank(quote, 2, 1, 1);
        if ((error != AMARK_ERROR_NONE) && (amark_block_blank_line_count(quote) != 0))
        {
            error = AMARK_ERROR_INVALID_STATE;
        }
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_block_discard_margin(quote, 3);
    }
    if ((error == AMARK_ERROR_NONE) &&
        (amark_tree_check(document, NULL) != AMARK_TREE_VIOLATION_NONE))
    {
        error = AMARK_ERROR_INVALID_STATE;
    }
    amark_document_destroy(document);
    return error;
}

NAOS_TEST(out_of_memory_leaves_the_records_unchanged)
{
    AMARK_CHECK_OOM_SWEEP(oom_scenario, NULL);
}

NAOS_SUITE(layout,
    NAOS_ADD_TEST(a_new_block_has_no_line_range),
    NAOS_ADD_TEST(set_lines_records_the_range_within_the_line_table),
    NAOS_ADD_TEST(set_lines_cannot_leave_out_a_record),
    NAOS_ADD_TEST(input_3_1_margins_can_be_recorded),
    NAOS_ADD_TEST(input_3_4_lazy_margins_can_be_recorded),
    NAOS_ADD_TEST(nested_lazy_margins_sit_where_the_outer_margin_ends),
    NAOS_ADD_TEST(blank_lines_belong_to_the_innermost_block_with_block_children),
    NAOS_ADD_TEST(an_item_has_an_empty_present_margin_on_a_blank_line),
    NAOS_ADD_TEST(input_3_3_a_tab_is_shared_by_columns),
    NAOS_ADD_TEST(tokens_are_recorded_in_the_order_of_lines_and_columns),
    NAOS_ADD_TEST(records_outside_the_lines_or_columns_are_refused),
    NAOS_ADD_TEST(records_on_types_that_cannot_hold_them_are_refused),
    NAOS_ADD_TEST(blank_lines_must_be_blank_to_the_end_of_the_line),
    NAOS_ADD_TEST(margins_out_of_order_are_refused_and_the_node_is_unchanged),
    NAOS_ADD_TEST(readers_refuse_bad_arguments),
    NAOS_ADD_TEST(find_margin_searches_the_margins_by_line),
    NAOS_ADD_TEST(discard_margin_removes_it_and_drops_the_structural_spelling),
    NAOS_ADD_TEST(recording_notifies_the_change),
    NAOS_ADD_TEST(the_check_detects_a_broken_layout),
    NAOS_ADD_TEST(recording_time_is_linear_in_the_records),
    NAOS_ADD_TEST(out_of_memory_leaves_the_records_unchanged)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(layout)
)
