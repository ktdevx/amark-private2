/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The tests of deep trees and the complexity counters (syntax-tree):
 *   - on trees 100000 levels deep (blocks, and inlines), the traversal, the
 *     copy, the import, the release, the comparison, the dump, the verbatim
 *     judgment and the lossless check run without overflowing the stack (5.10);
 *   - with the complexity counters of the document (document->stats), the
 *     records (also the inline tokens of deep inline nesting), the lossless check and the judgment of every node of an n-line
 *     document are linear (14.6, 14.7, 14.9), successive insertions cost the
 *     depth (14.5), and the lookup of the only definition of a label costs the
 *     depth (14.8).
 * The deep trees are built from the bottom up: an insertion visits the
 * ancestors of the insertion point, so building them from the top down would
 * cost the square of the depth.
 */
#include "tree/tree.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <naos/naos.h>

/* The depth of the deep trees (5.10). */
#define DEEP 100000u

/* ---- Helpers ------------------------------------------------------------------- */

#define OK(call) NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, (call))

#define CHECK_TREE_OK(document) \
    NAOS_CHECK_STR_EQ("none", amark_tree_violation_name(amark_tree_check((document), NULL)))

/* A document of the built-in syntax set with one finished source of length
 * bytes (no source when text is NULL). */
static amark_document_t *new_document_n(const char *text, size_t length)
{
    amark_document_t *document = NULL;
    amark_source_id_t source = 0;

    OK(amark_document_create(NULL, &document));
    NAOS_REQUIRE_NOT_NULL(document);
    if (text != NULL)
    {
        OK(amark_document_add_source(document, text, length, AMARK_SOURCE_FINISHED, &source));
    }
    return document;
}

static amark_node_t *make(amark_document_t *document, amark_node_type_t type)
{
    amark_node_t *node = NULL;

    OK(amark_node_create(document, type, &node));
    NAOS_REQUIRE_NOT_NULL(node);
    return node;
}

static void add_margin(amark_node_t *container, amark_line_id_t line, uint32_t start,
                       uint32_t end, uint32_t flags)
{
    amark_margin_t margin;

    memset(&margin, 0, sizeof(margin));
    margin.struct_size = sizeof(margin);
    margin.line = line;
    margin.state = AMARK_MARGIN_PRESENT;
    margin.flags = flags;
    margin.column_start = start;
    margin.column_end = end;
    OK(amark_block_add_margin(container, &margin));
}

static void add_token(amark_node_t *node, const amark_node_t *leaf, amark_spell_kind_t kind,
                      amark_facet_t facet, size_t start, size_t end)
{
    amark_content_token_t token;

    memset(&token, 0, sizeof(token));
    token.struct_size = sizeof(token);
    token.start = start;
    token.end = end;
    token.kind = kind;
    token.facet = facet;
    OK(amark_node_add_content_token(node, leaf, &token));
}

/* The next node of the preorder of the subtree of top (NULL after the last). */
static amark_node_t *preorder_next(const amark_node_t *top, amark_node_t *node)
{
    if (amark_node_first_child(node) != NULL)
    {
        return amark_node_first_child(node);
    }
    while (node != top && amark_node_next(node) == NULL)
    {
        node = amark_node_parent(node);
    }
    return (node == top) ? NULL : amark_node_next(node);
}

/* The number of nodes and the depth of the subtree of top, walked by the
 * iterator; checks that the events nest. */
/* (The text is the only node of these trees that cannot have children.) */
static void traverse(amark_node_t *top, size_t *nodes, size_t *depth)
{
    amark_iter_t *iter = NULL;
    amark_event_t event;
    size_t level = 0;

    *nodes = 0;
    *depth = 0;
    OK(amark_iter_create(top, &iter));
    while ((event = amark_iter_next(iter)) != AMARK_EVENT_DONE)
    {
        if (event == AMARK_EVENT_ENTER)
        {
            (*nodes)++;
            level++;
            if (level > *depth)
            {
                *depth = level;
            }
            /* A text cannot have children: no exit event. */
            if (amark_node_get_type(amark_iter_node(iter)) == AMARK_NODE_TEXT)
            {
                level--;
            }
        }
        else
        {
            NAOS_REQUIRE_UINT_GT(level, 0);
            level--;
        }
    }
    NAOS_CHECK_UINT_EQ(0, level);
    NAOS_CHECK_PTR_EQ(top, amark_iter_node(iter));
    amark_iter_destroy(iter);
}

/* The steps of judging every node of the subtree of top, in preorder; *all
 * receives whether every node can be output as it is spelled. */
static size_t judge_every_node(amark_document_t *document, amark_node_t *top, bool *all)
{
    size_t before = document->stats.verbatim_steps;
    amark_node_t *node;

    *all = true;
    for (node = top; node != NULL; node = preorder_next(top, node))
    {
        bool out = false;

        OK(amark_node_verbatim(node, &out));
        *all = *all && out;
    }
    return document->stats.verbatim_steps - before;
}

/* The dump of node with flags (the caller destroys it). */
static amark_buffer_t *dump(const amark_node_t *node, uint32_t flags)
{
    amark_buffer_t *buffer = NULL;

    OK(amark_buffer_create(&buffer));
    OK(amark_node_dump(node, flags, buffer));
    return buffer;
}

#define ALL_DUMP_FLAGS (AMARK_DUMP_STYLE | AMARK_DUMP_LAYOUT | AMARK_DUMP_SPELLING)

/* ---- Deep trees (5.10) -------------------------------------------------------------- */

/*
 * One line of DEEP '>' then "a": DEEP block quotes nested in each other, each
 * with its marker as its margin, around a paragraph "a" with its text. Built
 * from the bottom up; the quotes are not attached to the document node.
 * Returns the outermost quote.
 */
static amark_node_t *deep_quotes(amark_document_t **out_document)
{
    size_t length = DEEP + 2;
    char *source = (char *)malloc(length);
    amark_document_t *document;
    amark_node_t *paragraph;
    amark_node_t *text;
    amark_node_t *current;
    size_t i;

    NAOS_REQUIRE_TRUE(source != NULL);
    memset(source, '>', DEEP);
    source[DEEP] = 'a';
    source[DEEP + 1] = '\n';
    document = new_document_n(source, length);
    free(source);

    paragraph = make(document, AMARK_NODE_PARAGRAPH);
    OK(amark_block_set_lines(paragraph, 0, 1));
    OK(amark_block_add_content(paragraph, 0, DEEP, DEEP + 1));
    text = make(document, AMARK_NODE_TEXT);
    OK(amark_node_append_child(paragraph, text));
    add_token(text, paragraph, AMARK_SPELL_TEXT, AMARK_TEXT_LITERAL, 0, 1);
    current = paragraph;
    for (i = DEEP; i > 0; i--)
    {
        amark_node_t *quote = make(document, AMARK_NODE_BLOCK_QUOTE);

        OK(amark_block_set_lines(quote, 0, 1));
        add_margin(quote, 0, (uint32_t)(i - 1), (uint32_t)i, 0);
        OK(amark_node_append_child(quote, current));
        current = quote;
    }
    *out_document = document;
    return current;
}

NAOS_TEST(a_tree_of_deep_blocks_needs_no_deep_stack)
{
    amark_document_t *document = NULL;
    amark_document_t *target = NULL;
    amark_node_t *top = deep_quotes(&document);
    amark_node_t *root = amark_document_root(document);
    amark_node_t *copy = NULL;
    amark_node_t *imported = NULL;
    amark_buffer_t *original_dump;
    amark_buffer_t *other_dump;
    size_t nodes = 0;
    size_t depth = 0;
    size_t steps;
    size_t released;
    bool all = false;
    bool out = false;

    OK(amark_block_set_lines(root, 0, 1));
    OK(amark_node_append_child(root, top));
    CHECK_TREE_OK(document);

    /* Traversal. */
    traverse(root, &nodes, &depth);
    NAOS_CHECK_UINT_EQ(DEEP + 3, nodes);
    NAOS_CHECK_UINT_EQ(DEEP + 3, depth);
    NAOS_CHECK_UINT_EQ(DEEP + 3, document->node_count);

    /* The lossless check and the verbatim judgment (of the document node, then
     * of every node: linear in the nodes and the records). */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_check_lossless(document, 0, NULL));
    OK(amark_node_verbatim(root, &out));
    NAOS_CHECK_TRUE(out);
    steps = judge_every_node(document, root, &all);
    NAOS_CHECK_TRUE(all);
    NAOS_CHECK_UINT_LE(steps, 16u * (DEEP + 3));

    /* Dump: the indentation stops growing, so the dump is linear. */
    original_dump = dump(top, ALL_DUMP_FLAGS);
    NAOS_CHECK_UINT_GT(amark_buffer_length(original_dump), DEEP);
    NAOS_CHECK_UINT_LE(amark_buffer_length(original_dump), 256u * DEEP);

    /* Copy and comparison. */
    OK(amark_node_copy(top, &copy));
    NAOS_REQUIRE_NOT_NULL(copy);
    NAOS_CHECK_TRUE(amark_node_equal(top, copy, 0));
    NAOS_CHECK_TRUE(amark_node_equal(top, copy, AMARK_EQUAL_STYLE | AMARK_EQUAL_NORMALIZE_TEXT));
    /* Unequal only at the bottom: the whole depth is compared. */
    NAOS_CHECK_FALSE(amark_node_equal(top, amark_node_first_child(copy), 0));
    other_dump = dump(copy, ALL_DUMP_FLAGS);
    NAOS_CHECK_UINT_EQ(amark_buffer_length(original_dump), amark_buffer_length(other_dump));
    NAOS_CHECK_INT_EQ(0, memcmp(amark_buffer_data(original_dump), amark_buffer_data(other_dump),
                                amark_buffer_length(original_dump)));
    amark_buffer_destroy(other_dump);
    OK(amark_node_verbatim(copy, &out));
    NAOS_CHECK_TRUE(out);

    /* Import into another document: the same tree on the same line. */
    target = new_document_n(NULL, 0);
    OK(amark_node_import(target, top, &imported));
    NAOS_REQUIRE_NOT_NULL(imported);
    NAOS_CHECK_TRUE(amark_node_equal(top, imported, AMARK_EQUAL_STYLE));
    other_dump = dump(imported, ALL_DUMP_FLAGS);
    NAOS_CHECK_UINT_EQ(amark_buffer_length(original_dump), amark_buffer_length(other_dump));
    NAOS_CHECK_INT_EQ(0, memcmp(amark_buffer_data(original_dump), amark_buffer_data(other_dump),
                                amark_buffer_length(original_dump)));
    amark_buffer_destroy(other_dump);
    OK(amark_block_set_lines(amark_document_root(target), 0, 1));
    OK(amark_node_append_child(amark_document_root(target), imported));
    CHECK_TREE_OK(target);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_check_lossless(target, 0, NULL));
    steps = judge_every_node(target, amark_document_root(target), &all);
    NAOS_CHECK_TRUE(all);
    NAOS_CHECK_UINT_LE(steps, 16u * (DEEP + 3));
    amark_buffer_destroy(original_dump);

    /* Release: a detached copy, an attached subtree, then whole documents. */
    released = document->stats.nodes_released;
    steps = document->stats.release_steps;
    amark_node_destroy(copy);
    NAOS_CHECK_UINT_EQ(DEEP + 2, document->stats.nodes_released - released);
    NAOS_CHECK_UINT_LE(document->stats.release_steps - steps, 4u * (DEEP + 2));
    CHECK_TREE_OK(document);
    amark_node_destroy(amark_node_first_child(top));
    NAOS_CHECK_UINT_EQ(2, document->node_count);
    CHECK_TREE_OK(document);
    amark_document_destroy(target);
    amark_document_destroy(document);
}

/*
 * One paragraph "*...*a*...*": depth emphasis nodes nested in each other around
 * the text "a", each spelled by its two delimiters. The structure is built from
 * the bottom up, then the tokens are recorded in document order from the top
 * down (a delimiter token at every level, the text token at the bottom);
 * *record_steps receives the record steps of the tokens. Returns the
 * paragraph, attached to the document node.
 */
static amark_node_t *deep_emphasis(size_t depth, amark_document_t **out_document,
                                   size_t *record_steps)
{
    size_t length = 2 * depth + 1;
    char *source = (char *)malloc(length + 1);
    amark_document_t *document;
    amark_node_t *root;
    amark_node_t *paragraph;
    amark_node_t *text;
    amark_node_t *current;
    size_t before;
    size_t i;

    NAOS_REQUIRE_TRUE(source != NULL);
    memset(source, '*', length);
    source[depth] = 'a';
    source[length] = '\n';
    document = new_document_n(source, length + 1);
    free(source);
    root = amark_document_root(document);
    paragraph = make(document, AMARK_NODE_PARAGRAPH);
    OK(amark_block_set_lines(paragraph, 0, 1));
    OK(amark_block_add_content(paragraph, 0, 0, (uint32_t)length));
    text = make(document, AMARK_NODE_TEXT);
    current = text;
    for (i = 0; i < depth; i++)
    {
        amark_node_t *emph = make(document, AMARK_NODE_EMPH);

        OK(amark_node_append_child(emph, current));
        current = emph;
    }
    OK(amark_node_append_child(paragraph, current));
    OK(amark_block_set_lines(root, 0, 1));
    OK(amark_node_append_child(root, paragraph));

    before = document->stats.record_steps;
    current = amark_node_first_child(paragraph);
    for (i = 0; i < depth; i++)
    {
        add_token(current, paragraph, AMARK_SPELL_DELIMITER, AMARK_FACET_NONE, i, i + 1);
        add_token(current, paragraph, AMARK_SPELL_DELIMITER, AMARK_FACET_NONE, length - 1 - i,
                  length - i);
        current = amark_node_first_child(current);
    }
    NAOS_REQUIRE_PTR_EQ(text, current);
    add_token(text, paragraph, AMARK_SPELL_TEXT, AMARK_TEXT_LITERAL, depth, depth + 1);
    *record_steps = document->stats.record_steps - before;
    *out_document = document;
    return paragraph;
}

NAOS_TEST(a_tree_of_deep_inlines_needs_no_deep_stack)
{
    amark_document_t *document = NULL;
    amark_document_t *target = NULL;
    size_t record_steps = 0;
    amark_node_t *paragraph = deep_emphasis(DEEP, &document, &record_steps);
    amark_node_t *top = amark_node_first_child(paragraph);
    amark_node_t *copy = NULL;
    amark_node_t *imported = NULL;
    amark_buffer_t *original_dump;
    amark_buffer_t *other_dump;
    size_t nodes = 0;
    size_t depth = 0;
    size_t steps;
    bool all = false;
    bool out = false;

    NAOS_CHECK_UINT_LE(record_steps, 8u * DEEP);
    CHECK_TREE_OK(document);
    traverse(amark_document_root(document), &nodes, &depth);
    NAOS_CHECK_UINT_EQ(DEEP + 3, nodes);
    NAOS_CHECK_UINT_EQ(DEEP + 3, depth);

    /* The content is covered by the delimiters and the text. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_check_lossless(document, 0, NULL));
    OK(amark_node_verbatim(paragraph, &out));
    NAOS_CHECK_TRUE(out);
    steps = judge_every_node(document, amark_document_root(document), &all);
    NAOS_CHECK_TRUE(all);
    NAOS_CHECK_UINT_LE(steps, 16u * (DEEP + 3));

    original_dump = dump(paragraph, ALL_DUMP_FLAGS);
    NAOS_CHECK_UINT_GT(amark_buffer_length(original_dump), DEEP);
    NAOS_CHECK_UINT_LE(amark_buffer_length(original_dump), 256u * DEEP);

    OK(amark_node_copy(paragraph, &copy));
    NAOS_REQUIRE_NOT_NULL(copy);
    NAOS_CHECK_TRUE(amark_node_equal(paragraph, copy, AMARK_EQUAL_STYLE));
    NAOS_CHECK_TRUE(amark_node_equal(paragraph, copy, AMARK_EQUAL_NORMALIZE_TEXT));
    /* The text one level higher: unequal only at the bottom. */
    NAOS_CHECK_FALSE(amark_node_equal(top, amark_node_first_child(amark_node_first_child(copy)),
                                      0));
    other_dump = dump(copy, ALL_DUMP_FLAGS);
    NAOS_CHECK_UINT_EQ(amark_buffer_length(original_dump), amark_buffer_length(other_dump));
    NAOS_CHECK_INT_EQ(0, memcmp(amark_buffer_data(original_dump), amark_buffer_data(other_dump),
                                amark_buffer_length(original_dump)));
    amark_buffer_destroy(other_dump);

    target = new_document_n(NULL, 0);
    OK(amark_node_import(target, paragraph, &imported));
    NAOS_REQUIRE_NOT_NULL(imported);
    NAOS_CHECK_TRUE(amark_node_equal(paragraph, imported, AMARK_EQUAL_STYLE));
    other_dump = dump(imported, ALL_DUMP_FLAGS);
    NAOS_CHECK_UINT_EQ(amark_buffer_length(original_dump), amark_buffer_length(other_dump));
    NAOS_CHECK_INT_EQ(0, memcmp(amark_buffer_data(original_dump), amark_buffer_data(other_dump),
                                amark_buffer_length(original_dump)));
    amark_buffer_destroy(other_dump);
    amark_buffer_destroy(original_dump);
    OK(amark_block_set_lines(amark_document_root(target), 0, 1));
    OK(amark_node_append_child(amark_document_root(target), imported));
    CHECK_TREE_OK(target);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_check_lossless(target, 0, NULL));

    /* Release: the copy, the emphasis under the paragraph, the documents. */
    amark_node_destroy(copy);
    amark_node_destroy(top);
    NAOS_CHECK_UINT_EQ(2, document->node_count);
    CHECK_TREE_OK(document);
    amark_document_destroy(target);
    amark_document_destroy(document);
}

/* ---- n-line documents (14.6, 14.7, 14.9) ------------------------------------------ */

/*
 * The counters of an n-line document (n even): a block quote holding a tight
 * bullet list with one item per two lines,
 *     > - a *b*
 *     >   c
 * each item holding a paragraph "a *b*\nc" with its inline nodes and tokens.
 */
typedef struct
{
    size_t lines;
    size_t nodes;
    size_t record_steps;
    size_t lossless_steps;
    size_t verbatim_steps;
} counters_t;

static counters_t document_counters(size_t lines)
{
    static const char pair[] = "> - a *b*\n>   c\n";
    size_t pair_length = sizeof(pair) - 1;
    size_t items = lines / 2;
    char *source = (char *)malloc(items * pair_length);
    amark_document_t *document;
    amark_node_t *root;
    amark_node_t *quote;
    amark_node_t *list;
    counters_t c;
    size_t record_steps;
    size_t i;
    bool all = false;

    NAOS_REQUIRE_TRUE(source != NULL);
    for (i = 0; i < items; i++)
    {
        memcpy(source + i * pair_length, pair, pair_length);
    }
    document = new_document_n(source, items * pair_length);
    free(source);
    record_steps = document->stats.record_steps;

    root = amark_document_root(document);
    quote = make(document, AMARK_NODE_BLOCK_QUOTE);
    list = make(document, AMARK_NODE_LIST);
    OK(amark_block_set_lines(root, 0, (uint32_t)lines));
    OK(amark_node_append_child(root, quote));
    OK(amark_block_set_lines(quote, 0, (uint32_t)lines));
    OK(amark_node_append_child(quote, list));
    OK(amark_block_set_lines(list, 0, (uint32_t)lines));
    OK(amark_node_record_int(list, AMARK_LIST_TYPE, AMARK_LIST_TYPE_BULLET));
    OK(amark_node_record_int(list, AMARK_LIST_TIGHT, 1));
    for (i = 0; i < lines; i++)
    {
        add_margin(quote, (amark_line_id_t)i, 0, 2, 0);
    }
    for (i = 0; i < items; i++)
    {
        amark_line_id_t line = (amark_line_id_t)(2 * i);
        amark_node_t *item = make(document, AMARK_NODE_ITEM);
        amark_node_t *paragraph = make(document, AMARK_NODE_PARAGRAPH);
        amark_node_t *emph = make(document, AMARK_NODE_EMPH);
        amark_node_t *inner = make(document, AMARK_NODE_TEXT);
        amark_node_t *first = make(document, AMARK_NODE_TEXT);
        amark_node_t *softbreak = make(document, AMARK_NODE_SOFTBREAK);
        amark_node_t *last = make(document, AMARK_NODE_TEXT);

        /* Recorded in document order: the lines, then the content of the
         * leaf, then the tokens of its inline nodes. */
        OK(amark_node_append_child(list, item));
        OK(amark_block_set_lines(item, line, 2));
        add_margin(item, line, 2, 4, AMARK_MARGIN_FIRST);
        add_margin(item, line + 1, 2, 4, 0);
        OK(amark_node_append_child(item, paragraph));
        OK(amark_block_set_lines(paragraph, line, 2));
        OK(amark_block_add_content(paragraph, line, 4, 9));
        OK(amark_block_add_content(paragraph, line + 1, 4, 5));
        OK(amark_node_append_child(paragraph, first));
        OK(amark_node_append_child(paragraph, emph));
        OK(amark_node_append_child(emph, inner));
        OK(amark_node_append_child(paragraph, softbreak));
        OK(amark_node_append_child(paragraph, last));
        /* "a *b*\nc" */
        add_token(first, paragraph, AMARK_SPELL_TEXT, AMARK_TEXT_LITERAL, 0, 2);
        add_token(emph, paragraph, AMARK_SPELL_DELIMITER, AMARK_FACET_NONE, 2, 3);
        add_token(inner, paragraph, AMARK_SPELL_TEXT, AMARK_TEXT_LITERAL, 3, 4);
        add_token(emph, paragraph, AMARK_SPELL_DELIMITER, AMARK_FACET_NONE, 4, 5);
        add_token(softbreak, paragraph, AMARK_SPELL_LINE_BREAK, AMARK_FACET_NONE, 5, 6);
        add_token(last, paragraph, AMARK_SPELL_TEXT, AMARK_TEXT_LITERAL, 6, 7);
    }
    c.lines = lines;
    c.nodes = document->node_count;
    c.record_steps = document->stats.record_steps - record_steps;
    CHECK_TREE_OK(document);

    c.lossless_steps = document->stats.lossless_steps;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_check_lossless(document, 0, NULL));
    c.lossless_steps = document->stats.lossless_steps - c.lossless_steps;

    c.verbatim_steps = judge_every_node(document, root, &all);
    NAOS_CHECK_TRUE(all);
    amark_document_destroy(document);
    return c;
}

NAOS_TEST(records_check_and_judgment_of_n_lines_are_linear)
{
    counters_t small = document_counters(2000);
    counters_t large = document_counters(16000);

    /* Eight times the lines, the nodes and the records. */
    NAOS_CHECK_UINT_EQ(8 * (small.nodes - 3), large.nodes - 3);

    /* Recording (14.6): amortized constant per record (3 margins, 2 content
     * pieces and 6 tokens per item). */
    NAOS_CHECK_UINT_GT(small.record_steps, 0);
    NAOS_CHECK_UINT_LE(large.record_steps, 8 * small.record_steps + 64);
    NAOS_CHECK_UINT_LE(large.record_steps, 16 * large.lines);

    /* The lossless check (14.7): the lines, the columns and the tokens. */
    NAOS_CHECK_UINT_GT(small.lossless_steps, small.lines);
    NAOS_CHECK_UINT_LE(large.lossless_steps, 8 * small.lossless_steps + 64);
    NAOS_CHECK_UINT_LE(large.lossless_steps, 64 * large.lines);

    /* Judging every node (14.9): the nodes and the records. */
    NAOS_CHECK_UINT_GT(small.verbatim_steps, small.nodes);
    NAOS_CHECK_UINT_LE(large.verbatim_steps, 8 * small.verbatim_steps + 64);
    NAOS_CHECK_UINT_LE(large.verbatim_steps, 16 * (large.nodes + large.lines * 8));
}

/* ---- Deep inline spelling (14.6) ----------------------------------------------------- */

/* The record steps of the tokens of deep_emphasis(depth). */
static size_t emphasis_token_steps(size_t depth)
{
    amark_document_t *document = NULL;
    size_t steps = 0;

    (void)deep_emphasis(depth, &document, &steps);
    CHECK_TREE_OK(document);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_check_lossless(document, 0, NULL));
    amark_document_destroy(document);
    return steps;
}

NAOS_TEST(recording_the_tokens_of_deep_inlines_is_linear)
{
    size_t small = emphasis_token_steps(2000);
    size_t large = emphasis_token_steps(16000);

    /* 2 delimiters per level and a text: each token checks that its node is an
     * inline descendant of the leaf, which costs a constant once the leaf of
     * its parent is known. */
    NAOS_CHECK_UINT_GE(small, 2u * 2000u + 1u);
    NAOS_CHECK_UINT_LE(large, 8 * small + 64);
    NAOS_CHECK_UINT_LE(large, 8u * 16000u);
}

NAOS_TEST(a_remembered_leaf_follows_the_structure)
{
    amark_document_t *document = NULL;
    size_t steps = 0;
    amark_node_t *paragraph = deep_emphasis(3, &document, &steps);
    amark_node_t *other = make(document, AMARK_NODE_PARAGRAPH);
    amark_node_t *top = amark_node_first_child(paragraph);
    amark_node_t *middle = amark_node_first_child(top);
    amark_content_token_t token;

    /* The tokens remembered the paragraph as the leaf of the emphasis; moved
     * under another paragraph, a token on the old one is refused, and one on
     * the new one is accepted. */
    OK(amark_block_set_lines(other, 0, 1));
    OK(amark_block_add_content(other, 0, 0, 7));
    OK(amark_node_append_child(amark_document_root(document), other));
    OK(amark_node_append_child(other, middle));
    memset(&token, 0, sizeof(token));
    token.struct_size = sizeof(token);
    token.start = 0;
    token.end = 1;
    token.kind = AMARK_SPELL_DELIMITER;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_add_content_token(amark_node_first_child(middle), paragraph,
                                                   &token));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_add_content_token(top, other, &token));
    /* The innermost emphasis has tokens on the old content: a new node instead. */
    {
        amark_node_t *emph = make(document, AMARK_NODE_EMPH);

        OK(amark_node_append_child(amark_node_first_child(middle), emph));
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_add_content_token(emph, other, &token));
        NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                          amark_node_add_content_token(emph, paragraph, &token));
    }
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

/* ---- Successive insertions (14.5) --------------------------------------------------- */

/* A document with width paragraphs under its document node and a chain of
 * depth block quotes (built from the bottom up); *bottom receives the deepest
 * quote. */
static amark_document_t *chain(size_t depth, size_t width, amark_node_t **bottom)
{
    amark_document_t *document = new_document_n(NULL, 0);
    amark_node_t *root = amark_document_root(document);
    amark_node_t *current;
    size_t i;

    for (i = 0; i < width; i++)
    {
        OK(amark_node_append_child(root, make(document, AMARK_NODE_PARAGRAPH)));
    }
    current = make(document, AMARK_NODE_BLOCK_QUOTE);
    *bottom = current;
    for (i = 1; i < depth; i++)
    {
        amark_node_t *quote = make(document, AMARK_NODE_BLOCK_QUOTE);

        OK(amark_node_append_child(quote, current));
        current = quote;
    }
    OK(amark_node_append_child(root, current));
    return document;
}

/* The structure and invalidation steps of count successive insertions of new
 * paragraphs at the bottom of a chain of depth quotes among width siblings. */
static size_t insertion_steps(size_t depth, size_t width, size_t count)
{
    amark_node_t *bottom = NULL;
    amark_document_t *document = chain(depth, width, &bottom);
    size_t before = document->stats.structure_steps + document->stats.invalidation_steps;
    size_t steps;
    size_t i;

    for (i = 0; i < count; i++)
    {
        OK(amark_node_append_child(bottom, make(document, AMARK_NODE_PARAGRAPH)));
    }
    steps = document->stats.structure_steps + document->stats.invalidation_steps - before;
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
    return steps;
}

NAOS_TEST(successive_insertions_cost_the_depth)
{
    size_t shallow = insertion_steps(1000, 10, 500);
    size_t deep = insertion_steps(8000, 10, 500);
    size_t crowded = insertion_steps(1000, 20000, 500);
    size_t deepest = insertion_steps(DEEP, 10, 50);

    /* At least one step per level per insertion, at most a few. */
    NAOS_CHECK_UINT_GE(shallow, 500u * 1000u);
    NAOS_CHECK_UINT_LE(shallow, 500u * (3u * 1001u + 8u));
    NAOS_CHECK_UINT_LE(deep, 8 * shallow + 500u * 8u);
    NAOS_CHECK_UINT_GE(deep, 7 * shallow);
    /* Not the number of nodes. */
    NAOS_CHECK_UINT_EQ(shallow, crowded);
    NAOS_CHECK_UINT_GE(deepest, 50u * DEEP);
    NAOS_CHECK_UINT_LE(deepest, 50u * (3u * (DEEP + 1u) + 8u));
}

/* ---- The lookup of one definition (14.8) ------------------------------------------ */

/* The index steps of the first lookup, after a structural change, of the only
 * definition of its label, at the bottom of a chain of depth quotes among
 * others definitions of another label. */
static size_t lookup_steps(size_t depth, size_t others)
{
    amark_node_t *bottom = NULL;
    amark_document_t *document = chain(depth, 0, &bottom);
    amark_node_t *root = amark_document_root(document);
    amark_node_t *definition = make(document, AMARK_NODE_LINK_DEFINITION);
    amark_node_t *found = NULL;
    size_t before;
    size_t i;

    for (i = 0; i < others; i++)
    {
        amark_node_t *other = make(document, AMARK_NODE_LINK_DEFINITION);

        OK(amark_node_record_string(other, AMARK_LINK_DEFINITION_LABEL, "Other", 5));
        OK(amark_node_record_string(other, AMARK_LINK_DEFINITION_DESTINATION, "/o", 2));
        OK(amark_node_prepend_child(root, other));
    }
    OK(amark_node_record_string(definition, AMARK_LINK_DEFINITION_LABEL, "Label", 5));
    OK(amark_node_record_string(definition, AMARK_LINK_DEFINITION_DESTINATION, "/url", 4));
    OK(amark_node_append_child(bottom, definition));
    OK(amark_document_find_definition(document, "other", 5, &found));
    NAOS_CHECK_NOT_NULL(found);
    OK(amark_document_find_definition(document, "label", 5, &found));
    NAOS_CHECK_PTR_EQ(definition, found);
    /* A structural change: the result is decided again. */
    OK(amark_node_append_child(root, make(document, AMARK_NODE_PARAGRAPH)));
    before = document->stats.index_steps;
    OK(amark_document_find_definition(document, "LABEL", 5, &found));
    NAOS_CHECK_PTR_EQ(definition, found);
    before = document->stats.index_steps - before;
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
    return before;
}

NAOS_TEST(a_lookup_of_one_definition_costs_the_depth)
{
    size_t shallow = lookup_steps(1000, 10);
    size_t crowded = lookup_steps(1000, 5000);
    size_t deepest = lookup_steps(DEEP, 10);

    /* One step per ancestor, and a few more; not the other definitions. */
    NAOS_CHECK_UINT_GE(shallow, 1000);
    NAOS_CHECK_UINT_LE(shallow, 1000 + 16);
    NAOS_CHECK_UINT_EQ(shallow, crowded);
    NAOS_CHECK_UINT_GE(deepest, DEEP);
    NAOS_CHECK_UINT_LE(deepest, DEEP + 16);
}

NAOS_SUITE(scale,
    NAOS_ADD_TEST(a_tree_of_deep_blocks_needs_no_deep_stack),
    NAOS_ADD_TEST(a_tree_of_deep_inlines_needs_no_deep_stack),
    NAOS_ADD_TEST(records_check_and_judgment_of_n_lines_are_linear),
    NAOS_ADD_TEST(recording_the_tokens_of_deep_inlines_is_linear),
    NAOS_ADD_TEST(a_remembered_leaf_follows_the_structure),
    NAOS_ADD_TEST(successive_insertions_cost_the_depth),
    NAOS_ADD_TEST(a_lookup_of_one_definition_costs_the_depth)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(scale)
)
