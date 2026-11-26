/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The lossless check of the syntax tokens (src/tree/token_check.c), through the
 * public API, with tokens built by hand the way the parsers lay them out.
 *
 * Suite lossless (6.9): the seams of the downstream parsers all pass: a block
 * quote prefix with CRLF, a lazy continuation line, a tab split between a block
 * quote and an indented code block (`>\t\tfoo`), tabs split on a line after an LF
 * and after a CRLF, the continuation indent of a list
 * item, a BOM (line 1 counts its columns after it), a replaced NUL, escapes and
 * character references, zero-length tokens, an empty source, the tokens of
 * another source and many tokens met out of order; the report is NONE on a
 * success.
 * Suite lossless_problems (6.9): a gap (at the start, in the middle, at the end),
 * an overlap, the tab column errors (a wrong sum, columns that do not follow each
 * other from 0, a regular token over a split tab), the tokens of detached nodes
 * left out, the same code with a NULL report, the invalid arguments, and the
 * out-of-memory sweep with the report NONE on each failure.
 */
#include <amark/allocator.h>
#include <amark/document.h>
#include <amark/error.h>
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

#define PREFIX AMARK_TOKEN_FLAG_CONTAINER_PREFIX

/* A document being built: the first error is kept and the later steps are skipped
 * (the out-of-memory sweep builds through the same helpers). */
typedef struct builder
{
    amark_document_t *document;
    amark_source_id_t source;
    amark_error_t error;
} builder_t;

/* Creates the document and its source of length bytes (a second source "zz" comes
 * first when two_sources is true, so the checked source is not 0). */
static void begin(builder_t *b, const char *text, size_t length, bool two_sources)
{
    amark_source_id_t other;

    b->document = NULL;
    b->source = 0;
    b->error = amark_document_create(NULL, &b->document);
    if ((b->error == AMARK_ERROR_NONE) && two_sources)
    {
        b->error = amark_document_add_source(b->document, "zz", 2, &other);
    }

    if (b->error == AMARK_ERROR_NONE)
    {
        b->error = amark_document_add_source(b->document, text, length, &b->source);
    }
}

/* Creates a node of type and appends it to parent (the root when parent is NULL;
 * left detached when detached is true). NULL after an error. */
static amark_node_t *add_node(builder_t *b, amark_node_t *parent, amark_node_type_t type,
                              bool detached)
{
    amark_node_t *node = NULL;

    if (b->error != AMARK_ERROR_NONE)
    {
        return NULL;
    }

    b->error = amark_node_create(b->document, type, &node);
    if ((b->error == AMARK_ERROR_NONE) && !detached)
    {
        if (parent == NULL)
        {
            parent = amark_document_root(b->document);
        }

        b->error = amark_node_append_child(parent, node);
    }

    return (b->error == AMARK_ERROR_NONE) ? node : NULL;
}

/* Adds a token of the source of b to node (the root when node is NULL), with the
 * tab fields of a partial tab when columns is not 0. */
static void add_tab(builder_t *b, amark_node_t *node, amark_token_kind_t kind, uint32_t flags,
                    size_t offset, size_t length, unsigned column_offset, unsigned columns)
{
    amark_token_t token;

    if (b->error != AMARK_ERROR_NONE)
    {
        return;
    }

    memset(&token, 0, sizeof(token));
    token.struct_size = sizeof(token);
    token.kind = kind;
    token.flags = flags;
    token.source = b->source;
    token.offset = offset;
    token.length = length;
    if (columns != 0)
    {
        token.flags |= AMARK_TOKEN_FLAG_PARTIAL_TAB;
        token.tab_column_offset = column_offset;
        token.tab_columns = columns;
    }

    if (node == NULL)
    {
        node = amark_document_root(b->document);
    }

    b->error = amark_node_add_token(node, &token);
}

static void add(builder_t *b, amark_node_t *node, amark_token_kind_t kind, uint32_t flags,
                size_t offset, size_t length)
{
    add_tab(b, node, kind, flags, offset, length, 0, 0);
}

/* A report filled with values that the check must overwrite. */
static amark_token_report_t dirty_report(void)
{
    amark_token_report_t report;

    memset(&report, 0x5A, sizeof(report));
    report.struct_size = sizeof(report);
    report.problem = AMARK_TOKEN_PROBLEM_OVERLAP;
    report.offset = 99;

    return report;
}

/* Expects the check of the source of b to pass, with a report NONE, and the same
 * result with a NULL report. */
#define CHECK_LOSSLESS(b) \
    do { \
        amark_token_report_t report_ = dirty_report(); \
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, (b)->error); \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, \
                          amark_document_check_tokens((b)->document, (b)->source, &report_)); \
        NAOS_CHECK_INT_EQ(AMARK_TOKEN_PROBLEM_NONE, report_.problem); \
        NAOS_CHECK_UINT_EQ(0, report_.offset); \
        NAOS_CHECK_NULL(report_.node); \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, \
                          amark_document_check_tokens((b)->document, (b)->source, NULL)); \
    } while (0)

/* Expects the check of the source of b to find problem at offset with owner node,
 * and the same code with a NULL report. */
#define CHECK_PROBLEM(b, expected_problem, expected_offset, expected_node) \
    do { \
        amark_token_report_t report_ = dirty_report(); \
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, (b)->error); \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NOT_LOSSLESS, \
                          amark_document_check_tokens((b)->document, (b)->source, &report_)); \
        NAOS_CHECK_INT_EQ((expected_problem), report_.problem); \
        NAOS_CHECK_UINT_EQ((expected_offset), report_.offset); \
        NAOS_CHECK_PTR_EQ((const amark_node_t *)(expected_node), report_.node); \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NOT_LOSSLESS, \
                          amark_document_check_tokens((b)->document, (b)->source, NULL)); \
    } while (0)

#define TEXT_OF(literal) literal, (sizeof(literal) - 1)

/* --- Suite lossless ----------------------------------------------------------- */

/*
 * "> a\n> b\r\n": the block quote owns the prefixes of both lines, the paragraph
 * the final CRLF; its tokens come before those of its children in the walk, so the
 * check must sort them.
 */
static void build_quote_crlf(builder_t *b)
{
    amark_node_t *quote;
    amark_node_t *paragraph;
    amark_node_t *text;
    amark_node_t *softbreak;

    begin(b, TEXT_OF("> a\n> b\r\n"), false);
    quote = add_node(b, NULL, AMARK_NODE_BLOCK_QUOTE, false);
    paragraph = add_node(b, quote, AMARK_NODE_PARAGRAPH, false);
    text = add_node(b, paragraph, AMARK_NODE_TEXT, false);
    softbreak = add_node(b, paragraph, AMARK_NODE_SOFTBREAK, false);
    add(b, quote, AMARK_TOKEN_MARKER, PREFIX, 0, 1);
    add(b, quote, AMARK_TOKEN_SPACE, PREFIX, 1, 1);
    add(b, quote, AMARK_TOKEN_MARKER, PREFIX, 4, 1);
    add(b, quote, AMARK_TOKEN_SPACE, PREFIX, 5, 1);
    add(b, text, AMARK_TOKEN_TEXT, 0, 2, 1);
    add(b, softbreak, AMARK_TOKEN_LINE_ENDING, 0, 3, 1);
    text = add_node(b, paragraph, AMARK_NODE_TEXT, false);
    add(b, text, AMARK_TOKEN_TEXT, 0, 6, 1);
    add(b, paragraph, AMARK_TOKEN_LINE_ENDING, 0, 7, 2);
}

NAOS_TEST(a_quote_prefix_with_crlf_is_lossless)
{
    builder_t b;

    build_quote_crlf(&b);
    CHECK_LOSSLESS(&b);

    amark_document_destroy(b.document);
}

NAOS_TEST(a_lazy_continuation_is_lossless)
{
    builder_t b;
    amark_node_t *quote;
    amark_node_t *paragraph;
    amark_node_t *text;
    amark_node_t *softbreak;

    /* "> a\nb": the prefix of line 2 is a zero-length token at its start. */
    begin(&b, TEXT_OF("> a\nb"), false);
    quote = add_node(&b, NULL, AMARK_NODE_BLOCK_QUOTE, false);
    paragraph = add_node(&b, quote, AMARK_NODE_PARAGRAPH, false);
    text = add_node(&b, paragraph, AMARK_NODE_TEXT, false);
    softbreak = add_node(&b, paragraph, AMARK_NODE_SOFTBREAK, false);
    add(&b, quote, AMARK_TOKEN_MARKER, PREFIX, 0, 1);
    add(&b, quote, AMARK_TOKEN_SPACE, PREFIX, 1, 1);
    add(&b, quote, AMARK_TOKEN_LAZY_CONTINUATION, PREFIX, 4, 0);
    add(&b, text, AMARK_TOKEN_TEXT, 0, 2, 1);
    add(&b, softbreak, AMARK_TOKEN_LINE_ENDING, 0, 3, 1);
    text = add_node(&b, paragraph, AMARK_NODE_TEXT, false);
    add(&b, text, AMARK_TOKEN_TEXT, 0, 4, 1);
    CHECK_LOSSLESS(&b);

    amark_document_destroy(b.document);
}

/*
 * ">\t\tfoo": the first tab, at column 1, is 3 columns wide: 1 for the optional
 * space of the block quote and 2 of the indentation of the code block; the second
 * tab, at column 4, is 4 columns: 2 more of the indentation and 2 of content.
 */
static void build_split_tabs(builder_t *b)
{
    amark_node_t *quote;
    amark_node_t *code;

    begin(b, TEXT_OF(">\t\tfoo"), false);
    quote = add_node(b, NULL, AMARK_NODE_BLOCK_QUOTE, false);
    code = add_node(b, quote, AMARK_NODE_CODE_BLOCK, false);
    add(b, quote, AMARK_TOKEN_MARKER, PREFIX, 0, 1);
    add_tab(b, quote, AMARK_TOKEN_SPACE, PREFIX, 1, 1, 0, 1);
    add_tab(b, code, AMARK_TOKEN_SPACE, 0, 1, 1, 1, 2);
    add_tab(b, code, AMARK_TOKEN_SPACE, 0, 2, 1, 0, 2);
    add_tab(b, code, AMARK_TOKEN_TEXT, 0, 2, 1, 2, 2);
    add(b, code, AMARK_TOKEN_TEXT, 0, 3, 3);
}

NAOS_TEST(a_tab_split_by_columns_is_lossless)
{
    builder_t b;

    build_split_tabs(&b);
    CHECK_LOSSLESS(&b);

    amark_document_destroy(b.document);
}

NAOS_TEST(a_partial_tab_of_the_whole_width_is_lossless)
{
    builder_t b;
    amark_node_t *quote;
    amark_node_t *paragraph;
    amark_node_t *text;

    /* ">\tb": one partial tab token of all 3 columns of the tab at column 1. */
    begin(&b, TEXT_OF(">\tb"), false);
    quote = add_node(&b, NULL, AMARK_NODE_BLOCK_QUOTE, false);
    paragraph = add_node(&b, quote, AMARK_NODE_PARAGRAPH, false);
    text = add_node(&b, paragraph, AMARK_NODE_TEXT, false);
    add(&b, quote, AMARK_TOKEN_MARKER, PREFIX, 0, 1);
    add_tab(&b, quote, AMARK_TOKEN_SPACE, PREFIX, 1, 1, 0, 3);
    add(&b, text, AMARK_TOKEN_TEXT, 0, 2, 1);
    CHECK_LOSSLESS(&b);

    amark_document_destroy(b.document);
}

/* "> ab", then a line ending of length line_ending_length, then ">\tb": the tab
 * of line 2 is at column 1 only when the line ending starts a new line, so 3
 * columns wide (1 for the quote, 2 for the paragraph); counting on from line 1 it
 * would be at column 6 (after an LF) and 2 wide. */
static void build_tab_after_line_ending(builder_t *b, const char *text, size_t length,
                                        size_t line_ending_length)
{
    amark_node_t *quote;
    amark_node_t *paragraph;
    amark_node_t *first;
    amark_node_t *softbreak;
    amark_node_t *second;
    size_t line2 = 4 + line_ending_length;

    begin(b, text, length, false);
    quote = add_node(b, NULL, AMARK_NODE_BLOCK_QUOTE, false);
    paragraph = add_node(b, quote, AMARK_NODE_PARAGRAPH, false);
    first = add_node(b, paragraph, AMARK_NODE_TEXT, false);
    softbreak = add_node(b, paragraph, AMARK_NODE_SOFTBREAK, false);
    second = add_node(b, paragraph, AMARK_NODE_TEXT, false);
    add(b, quote, AMARK_TOKEN_MARKER, PREFIX, 0, 1);
    add(b, quote, AMARK_TOKEN_SPACE, PREFIX, 1, 1);
    add(b, first, AMARK_TOKEN_TEXT, 0, 2, 2);
    add(b, softbreak, AMARK_TOKEN_LINE_ENDING, 0, 4, line_ending_length);
    add(b, quote, AMARK_TOKEN_MARKER, PREFIX, line2, 1);
    add_tab(b, quote, AMARK_TOKEN_SPACE, PREFIX, line2 + 1, 1, 0, 1);
    add_tab(b, paragraph, AMARK_TOKEN_SPACE, 0, line2 + 1, 1, 1, 2);
    add(b, second, AMARK_TOKEN_TEXT, 0, line2 + 2, 1);
}

NAOS_TEST(a_tab_split_after_an_lf_is_lossless)
{
    builder_t b;

    build_tab_after_line_ending(&b, TEXT_OF("> ab\n>\tb"), 1);
    CHECK_LOSSLESS(&b);

    amark_document_destroy(b.document);
}

NAOS_TEST(a_tab_split_after_a_crlf_is_lossless)
{
    builder_t b;

    build_tab_after_line_ending(&b, TEXT_OF("> ab\r\n>\tb"), 2);
    CHECK_LOSSLESS(&b);

    amark_document_destroy(b.document);
}

NAOS_TEST(a_list_continuation_indent_is_lossless)
{
    builder_t b;
    amark_node_t *list;
    amark_node_t *item;
    amark_node_t *paragraph;
    amark_node_t *text;

    /* "- a\n\n  b": the item owns its marker, the blank line and the indent of
     * line 3. */
    begin(&b, TEXT_OF("- a\n\n  b"), false);
    list = add_node(&b, NULL, AMARK_NODE_LIST, false);
    item = add_node(&b, list, AMARK_NODE_ITEM, false);
    paragraph = add_node(&b, item, AMARK_NODE_PARAGRAPH, false);
    text = add_node(&b, paragraph, AMARK_NODE_TEXT, false);
    add(&b, item, AMARK_TOKEN_MARKER, PREFIX, 0, 1);
    add(&b, item, AMARK_TOKEN_SPACE, PREFIX, 1, 1);
    add(&b, text, AMARK_TOKEN_TEXT, 0, 2, 1);
    add(&b, paragraph, AMARK_TOKEN_LINE_ENDING, 0, 3, 1);
    add(&b, item, AMARK_TOKEN_LINE_ENDING, 0, 4, 1);
    add(&b, item, AMARK_TOKEN_SPACE, PREFIX, 5, 2);
    paragraph = add_node(&b, item, AMARK_NODE_PARAGRAPH, false);
    text = add_node(&b, paragraph, AMARK_NODE_TEXT, false);
    add(&b, text, AMARK_TOKEN_TEXT, 0, 7, 1);
    CHECK_LOSSLESS(&b);

    amark_document_destroy(b.document);
}

NAOS_TEST(a_bom_is_lossless_and_takes_no_column)
{
    builder_t b;
    amark_node_t *quote;
    amark_node_t *paragraph;
    amark_node_t *text;

    /* BOM ">\tb\r>\tc": the tab of line 1 is at column 1, as on line 2 (after a
     * CR), so 3 columns wide on both lines; counting the BOM would make it 4. */
    begin(&b, TEXT_OF("\xEF\xBB\xBF>\tb\r>\tc"), true);
    quote = add_node(&b, NULL, AMARK_NODE_BLOCK_QUOTE, false);
    paragraph = add_node(&b, quote, AMARK_NODE_PARAGRAPH, false);
    text = add_node(&b, paragraph, AMARK_NODE_TEXT, false);
    add(&b, NULL, AMARK_TOKEN_BOM, 0, 0, 3);
    add(&b, quote, AMARK_TOKEN_MARKER, PREFIX, 3, 1);
    add_tab(&b, quote, AMARK_TOKEN_SPACE, PREFIX, 4, 1, 0, 1);
    add_tab(&b, paragraph, AMARK_TOKEN_SPACE, 0, 4, 1, 1, 2);
    add(&b, text, AMARK_TOKEN_TEXT, 0, 5, 1);
    add(&b, paragraph, AMARK_TOKEN_LINE_ENDING, 0, 6, 1);
    add(&b, quote, AMARK_TOKEN_MARKER, PREFIX, 7, 1);
    add_tab(&b, quote, AMARK_TOKEN_SPACE, PREFIX, 8, 1, 0, 1);
    add_tab(&b, paragraph, AMARK_TOKEN_SPACE, 0, 8, 1, 1, 2);
    add(&b, text, AMARK_TOKEN_TEXT, 0, 9, 1);
    CHECK_LOSSLESS(&b);

    amark_document_destroy(b.document);
}

NAOS_TEST(a_replaced_nul_escapes_and_char_refs_are_lossless)
{
    builder_t b;
    amark_node_t *paragraph;
    amark_node_t *text;

    /* "a" NUL "\\*&amp;b\n". */
    begin(&b, TEXT_OF("a\0\\*&amp;b\n"), false);
    paragraph = add_node(&b, NULL, AMARK_NODE_PARAGRAPH, false);
    text = add_node(&b, paragraph, AMARK_NODE_TEXT, false);
    add(&b, text, AMARK_TOKEN_TEXT, 0, 0, 1);
    add(&b, text, AMARK_TOKEN_REPLACED, 0, 1, 1);
    add(&b, text, AMARK_TOKEN_ESCAPE, 0, 2, 2);
    add(&b, text, AMARK_TOKEN_CHAR_REF, 0, 4, 5);
    add(&b, text, AMARK_TOKEN_TEXT, 0, 9, 1);
    add(&b, paragraph, AMARK_TOKEN_LINE_ENDING, 0, 10, 1);
    CHECK_LOSSLESS(&b);

    amark_document_destroy(b.document);
}

NAOS_TEST(zero_length_tokens_cover_nothing)
{
    builder_t b;
    amark_node_t *paragraph;
    amark_node_t *text;

    /* Position markers at the start, inside a token, between two tokens, on a
     * partial tab's offset and at the end, twice at one offset. */
    begin(&b, TEXT_OF("abc>\td"), false);
    paragraph = add_node(&b, NULL, AMARK_NODE_PARAGRAPH, false);
    text = add_node(&b, paragraph, AMARK_NODE_TEXT, false);
    add(&b, paragraph, AMARK_TOKEN_TEXT, 0, 0, 0);
    add(&b, text, AMARK_TOKEN_TEXT, 0, 0, 2);
    add(&b, paragraph, AMARK_TOKEN_TEXT, 0, 1, 0);
    add(&b, paragraph, AMARK_TOKEN_LAZY_CONTINUATION, 0, 2, 0);
    add(&b, text, AMARK_TOKEN_TEXT, 0, 2, 2);
    add(&b, paragraph, AMARK_TOKEN_TEXT, 0, 4, 0);
    add_tab(&b, text, AMARK_TOKEN_SPACE, 0, 4, 1, 0, 2);
    add_tab(&b, paragraph, AMARK_TOKEN_SPACE, 0, 4, 1, 2, 2);
    add(&b, text, AMARK_TOKEN_TEXT, 0, 5, 1);
    add(&b, text, AMARK_TOKEN_TEXT, 0, 6, 0);
    add(&b, paragraph, AMARK_TOKEN_TEXT, 0, 6, 0);
    CHECK_LOSSLESS(&b);

    amark_document_destroy(b.document);
}

NAOS_TEST(an_empty_source_without_tokens_is_lossless)
{
    builder_t b;

    begin(&b, "", 0, false);
    add(&b, NULL, AMARK_TOKEN_LAZY_CONTINUATION, 0, 0, 0);
    CHECK_LOSSLESS(&b);

    amark_document_destroy(b.document);
}

NAOS_TEST(the_tokens_of_another_source_are_ignored)
{
    builder_t b;
    amark_node_t *paragraph;
    amark_node_t *text;
    amark_token_t token;
    amark_token_report_t report = dirty_report();

    /* Source 0 ("zz") is covered twice at 0 and not at 1; the checked source 1 is
     * covered exactly once. */
    begin(&b, TEXT_OF("ab"), true);
    paragraph = add_node(&b, NULL, AMARK_NODE_PARAGRAPH, false);
    text = add_node(&b, paragraph, AMARK_NODE_TEXT, false);
    add(&b, text, AMARK_TOKEN_TEXT, 0, 0, 2);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, b.error);

    memset(&token, 0, sizeof(token));
    token.struct_size = sizeof(token);
    token.kind = AMARK_TOKEN_TEXT;
    token.source = 0;
    token.offset = 0;
    token.length = 1;
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_add_token(text, &token));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_add_token(paragraph, &token));
    NAOS_CHECK_UINT_EQ(1, b.source);
    CHECK_LOSSLESS(&b);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NOT_LOSSLESS,
                      amark_document_check_tokens(b.document, 0, &report));
    NAOS_CHECK_INT_EQ(AMARK_TOKEN_PROBLEM_OVERLAP, report.problem);
    NAOS_CHECK_UINT_EQ(0, report.offset);
    NAOS_CHECK_PTR_EQ((const amark_node_t *)text, report.node);

    amark_document_destroy(b.document);
}

NAOS_TEST(many_tokens_in_reverse_order_are_sorted)
{
    builder_t b;
    amark_node_t *paragraph;
    amark_node_t *nodes[37];
    size_t i;

    /* 37 text nodes in the tree, node i owning byte 36 - i, so the walk meets the
     * tokens backwards; then a duplicate of byte 20 at the last node. */
    begin(&b, TEXT_OF("abcdefghijklmnopqrstuvwxyz0123456789!"), false);
    paragraph = add_node(&b, NULL, AMARK_NODE_PARAGRAPH, false);
    for (i = 0; i < 37; i++)
    {
        nodes[i] = add_node(&b, paragraph, AMARK_NODE_TEXT, false);
        add(&b, nodes[i], AMARK_TOKEN_TEXT, 0, 36 - i, 1);
    }

    CHECK_LOSSLESS(&b);

    add(&b, nodes[36], AMARK_TOKEN_TEXT, 0, 20, 1);
    CHECK_PROBLEM(&b, AMARK_TOKEN_PROBLEM_OVERLAP, 20, nodes[36]);

    amark_document_destroy(b.document);
}

NAOS_SUITE(lossless,
    NAOS_SET_TEARDOWN(restore),
    NAOS_ADD_TEST(a_quote_prefix_with_crlf_is_lossless),
    NAOS_ADD_TEST(a_lazy_continuation_is_lossless),
    NAOS_ADD_TEST(a_tab_split_by_columns_is_lossless),
    NAOS_ADD_TEST(a_partial_tab_of_the_whole_width_is_lossless),
    NAOS_ADD_TEST(a_tab_split_after_an_lf_is_lossless),
    NAOS_ADD_TEST(a_tab_split_after_a_crlf_is_lossless),
    NAOS_ADD_TEST(a_list_continuation_indent_is_lossless),
    NAOS_ADD_TEST(a_bom_is_lossless_and_takes_no_column),
    NAOS_ADD_TEST(a_replaced_nul_escapes_and_char_refs_are_lossless),
    NAOS_ADD_TEST(zero_length_tokens_cover_nothing),
    NAOS_ADD_TEST(an_empty_source_without_tokens_is_lossless),
    NAOS_ADD_TEST(the_tokens_of_another_source_are_ignored),
    NAOS_ADD_TEST(many_tokens_in_reverse_order_are_sorted))

/* --- Suite lossless_problems -------------------------------------------------- */

/* "abcd" with one text node per covered byte range. */
typedef struct row
{
    builder_t b;
    amark_node_t *paragraph;
    amark_node_t *first;
    amark_node_t *second;
} row_t;

static void build_row(row_t *r)
{
    begin(&r->b, TEXT_OF("abcd"), false);
    r->paragraph = add_node(&r->b, NULL, AMARK_NODE_PARAGRAPH, false);
    r->first = add_node(&r->b, r->paragraph, AMARK_NODE_TEXT, false);
    r->second = add_node(&r->b, r->paragraph, AMARK_NODE_TEXT, false);
}

NAOS_TEST(a_gap_in_the_middle_is_reported_after_the_previous_owner)
{
    row_t r;

    build_row(&r);
    add(&r.b, r.first, AMARK_TOKEN_TEXT, 0, 0, 1);
    add(&r.b, r.second, AMARK_TOKEN_TEXT, 0, 2, 2);
    CHECK_PROBLEM(&r.b, AMARK_TOKEN_PROBLEM_GAP, 1, r.first);

    amark_document_destroy(r.b.document);
}

NAOS_TEST(a_gap_at_the_start_has_no_owner)
{
    row_t r;

    build_row(&r);
    add(&r.b, r.second, AMARK_TOKEN_TEXT, 0, 2, 2);
    add(&r.b, r.first, AMARK_TOKEN_TEXT, 0, 1, 1);
    add(&r.b, r.first, AMARK_TOKEN_TEXT, 0, 0, 0);
    CHECK_PROBLEM(&r.b, AMARK_TOKEN_PROBLEM_GAP, 0, NULL);

    amark_document_destroy(r.b.document);
}

NAOS_TEST(a_gap_at_the_end_follows_the_last_token)
{
    row_t r;

    build_row(&r);
    add(&r.b, r.second, AMARK_TOKEN_TEXT, 0, 2, 1);
    add(&r.b, r.first, AMARK_TOKEN_TEXT, 0, 0, 2);
    CHECK_PROBLEM(&r.b, AMARK_TOKEN_PROBLEM_GAP, 3, r.second);

    amark_document_destroy(r.b.document);
}

NAOS_TEST(a_source_without_tokens_is_a_gap)
{
    row_t r;

    build_row(&r);
    CHECK_PROBLEM(&r.b, AMARK_TOKEN_PROBLEM_GAP, 0, NULL);

    amark_document_destroy(r.b.document);
}

NAOS_TEST(an_overlap_is_reported_at_its_first_byte)
{
    row_t r;

    build_row(&r);
    add(&r.b, r.first, AMARK_TOKEN_TEXT, 0, 0, 3);
    add(&r.b, r.second, AMARK_TOKEN_TEXT, 0, 1, 3);
    CHECK_PROBLEM(&r.b, AMARK_TOKEN_PROBLEM_OVERLAP, 1, r.second);

    amark_document_destroy(r.b.document);
}

NAOS_TEST(two_tokens_at_one_offset_overlap)
{
    row_t r;

    /* The second in the walk (preorder, then the order of the node) is the
     * owner. */
    build_row(&r);
    add(&r.b, r.first, AMARK_TOKEN_TEXT, 0, 0, 2);
    add(&r.b, r.second, AMARK_TOKEN_TEXT, 0, 2, 2);
    add(&r.b, r.paragraph, AMARK_TOKEN_TEXT, 0, 2, 1);
    CHECK_PROBLEM(&r.b, AMARK_TOKEN_PROBLEM_OVERLAP, 2, r.second);

    amark_document_destroy(r.b.document);
}

NAOS_TEST(the_first_problem_is_reported)
{
    row_t r;

    /* An overlap at 1 comes before the gap at 3. */
    build_row(&r);
    add(&r.b, r.first, AMARK_TOKEN_TEXT, 0, 0, 2);
    add(&r.b, r.second, AMARK_TOKEN_TEXT, 0, 1, 2);
    CHECK_PROBLEM(&r.b, AMARK_TOKEN_PROBLEM_OVERLAP, 1, r.second);

    amark_document_destroy(r.b.document);
}

/* ">\tb" with a quote, a paragraph and a text node, and the marker and the text
 * covered; the tab is left to each test. */
typedef struct tab_row
{
    builder_t b;
    amark_node_t *quote;
    amark_node_t *paragraph;
} tab_row_t;

static void build_tab_row(tab_row_t *t, bool cover_tab_with_marker)
{
    amark_node_t *text;

    begin(&t->b, TEXT_OF(">\tb"), false);
    t->quote = add_node(&t->b, NULL, AMARK_NODE_BLOCK_QUOTE, false);
    t->paragraph = add_node(&t->b, t->quote, AMARK_NODE_PARAGRAPH, false);
    text = add_node(&t->b, t->paragraph, AMARK_NODE_TEXT, false);
    add(&t->b, t->quote, AMARK_TOKEN_TEXT, PREFIX, 0, cover_tab_with_marker ? 2 : 1);
    add(&t->b, text, AMARK_TOKEN_TEXT, 0, 2, 1);
}

NAOS_TEST(partial_tabs_whose_columns_do_not_add_up_are_reported)
{
    tab_row_t t;

    /* The tab at column 1 is 3 columns wide, not 4 nor 2. */
    build_tab_row(&t, false);
    add_tab(&t.b, t.quote, AMARK_TOKEN_SPACE, PREFIX, 1, 1, 0, 1);
    add_tab(&t.b, t.paragraph, AMARK_TOKEN_SPACE, 0, 1, 1, 1, 3);
    CHECK_PROBLEM(&t.b, AMARK_TOKEN_PROBLEM_TAB_COLUMNS, 1, t.paragraph);
    amark_document_destroy(t.b.document);

    build_tab_row(&t, false);
    add_tab(&t.b, t.quote, AMARK_TOKEN_SPACE, PREFIX, 1, 1, 0, 1);
    add_tab(&t.b, t.paragraph, AMARK_TOKEN_SPACE, 0, 1, 1, 1, 1);
    CHECK_PROBLEM(&t.b, AMARK_TOKEN_PROBLEM_TAB_COLUMNS, 1, t.paragraph);
    amark_document_destroy(t.b.document);

    build_tab_row(&t, false);
    add_tab(&t.b, t.quote, AMARK_TOKEN_SPACE, PREFIX, 1, 1, 0, 4);
    CHECK_PROBLEM(&t.b, AMARK_TOKEN_PROBLEM_TAB_COLUMNS, 1, t.quote);
    amark_document_destroy(t.b.document);
}

NAOS_TEST(partial_tabs_whose_columns_do_not_follow_from_0_are_reported)
{
    tab_row_t t;

    /* Starting at column 1. */
    build_tab_row(&t, false);
    add_tab(&t.b, t.paragraph, AMARK_TOKEN_SPACE, 0, 1, 1, 1, 2);
    CHECK_PROBLEM(&t.b, AMARK_TOKEN_PROBLEM_TAB_COLUMNS, 1, t.paragraph);
    amark_document_destroy(t.b.document);

    /* A hole between columns 1 and 2. */
    build_tab_row(&t, false);
    add_tab(&t.b, t.quote, AMARK_TOKEN_SPACE, PREFIX, 1, 1, 0, 1);
    add_tab(&t.b, t.paragraph, AMARK_TOKEN_SPACE, 0, 1, 1, 2, 1);
    CHECK_PROBLEM(&t.b, AMARK_TOKEN_PROBLEM_TAB_COLUMNS, 1, t.paragraph);
    amark_document_destroy(t.b.document);

    /* Column 0 twice (an overlap of partial tabs is a tab column error). */
    build_tab_row(&t, false);
    add_tab(&t.b, t.quote, AMARK_TOKEN_SPACE, PREFIX, 1, 1, 0, 1);
    add_tab(&t.b, t.paragraph, AMARK_TOKEN_SPACE, 0, 1, 1, 0, 2);
    CHECK_PROBLEM(&t.b, AMARK_TOKEN_PROBLEM_TAB_COLUMNS, 1, t.paragraph);
    amark_document_destroy(t.b.document);
}

NAOS_TEST(a_regular_token_over_a_split_tab_is_reported)
{
    tab_row_t t;

    /* A regular token at the offset of the tab. */
    build_tab_row(&t, false);
    add_tab(&t.b, t.quote, AMARK_TOKEN_SPACE, PREFIX, 1, 1, 0, 1);
    add_tab(&t.b, t.paragraph, AMARK_TOKEN_SPACE, 0, 1, 1, 1, 2);
    add(&t.b, NULL, AMARK_TOKEN_SPACE, 0, 1, 1);
    CHECK_PROBLEM(&t.b, AMARK_TOKEN_PROBLEM_TAB_COLUMNS, 1, amark_document_root(t.b.document));
    amark_document_destroy(t.b.document);

    /* A regular token from the byte before over the tab. */
    build_tab_row(&t, true);
    add_tab(&t.b, t.quote, AMARK_TOKEN_SPACE, PREFIX, 1, 1, 0, 1);
    add_tab(&t.b, t.paragraph, AMARK_TOKEN_SPACE, 0, 1, 1, 1, 2);
    CHECK_PROBLEM(&t.b, AMARK_TOKEN_PROBLEM_TAB_COLUMNS, 1, t.quote);
    amark_document_destroy(t.b.document);
}

NAOS_TEST(a_tab_covered_whole_by_a_regular_token_is_lossless)
{
    tab_row_t t;

    build_tab_row(&t, true);
    CHECK_LOSSLESS(&t.b);
    amark_document_destroy(t.b.document);
}

NAOS_TEST(a_wrong_split_after_a_tab_stop_is_reported)
{
    builder_t b;
    amark_node_t *quote;
    amark_node_t *code;

    /* ">\t\tfoo" with the second tab (at column 4, 4 wide) split as 2 + 1. */
    begin(&b, TEXT_OF(">\t\tfoo"), false);
    quote = add_node(&b, NULL, AMARK_NODE_BLOCK_QUOTE, false);
    code = add_node(&b, quote, AMARK_NODE_CODE_BLOCK, false);
    add(&b, quote, AMARK_TOKEN_MARKER, PREFIX, 0, 1);
    add_tab(&b, quote, AMARK_TOKEN_SPACE, PREFIX, 1, 1, 0, 1);
    add_tab(&b, code, AMARK_TOKEN_SPACE, 0, 1, 1, 1, 2);
    add_tab(&b, code, AMARK_TOKEN_SPACE, 0, 2, 1, 0, 2);
    add_tab(&b, quote, AMARK_TOKEN_TEXT, 0, 2, 1, 2, 1);
    add(&b, code, AMARK_TOKEN_TEXT, 0, 3, 3);
    CHECK_PROBLEM(&b, AMARK_TOKEN_PROBLEM_TAB_COLUMNS, 2, quote);

    amark_document_destroy(b.document);
}

NAOS_TEST(the_tokens_of_detached_nodes_are_left_out)
{
    builder_t b;
    amark_node_t *quote;
    amark_node_t *paragraph;
    amark_node_t *stray;

    /* A detached node covering all the source twice changes nothing. */
    build_quote_crlf(&b);
    stray = add_node(&b, NULL, AMARK_NODE_PARAGRAPH, true);
    add(&b, stray, AMARK_TOKEN_TEXT, 0, 0, 9);
    add(&b, stray, AMARK_TOKEN_TEXT, 0, 0, 9);
    CHECK_LOSSLESS(&b);

    /* Unlinking the paragraph leaves out its subtree: the gap at 2 follows the
     * space of the quote prefix. */
    quote = amark_node_first_child(amark_document_root(b.document));
    paragraph = amark_node_first_child(quote);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_unlink(paragraph));
    CHECK_PROBLEM(&b, AMARK_TOKEN_PROBLEM_GAP, 2, quote);

    /* Linking it back makes the document lossless again. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(quote, paragraph));
    CHECK_LOSSLESS(&b);

    amark_document_destroy(b.document);
}

NAOS_TEST(the_invalid_arguments_are_rejected)
{
    builder_t b;
    amark_token_report_t report;

    build_quote_crlf(&b);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, b.error);

    report = dirty_report();
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_document_check_tokens(NULL, 0, &report));
    NAOS_CHECK_INT_EQ(AMARK_TOKEN_PROBLEM_NONE, report.problem);
    NAOS_CHECK_UINT_EQ(0, report.offset);
    NAOS_CHECK_NULL(report.node);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_document_check_tokens(NULL, 0, NULL));

    report = dirty_report();
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_document_check_tokens(b.document, 1, &report));
    NAOS_CHECK_INT_EQ(AMARK_TOKEN_PROBLEM_NONE, report.problem);
    NAOS_CHECK_UINT_EQ(0, report.offset);
    NAOS_CHECK_NULL(report.node);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_document_check_tokens(b.document, UINT32_MAX, NULL));

    /* A struct_size below version 1: the report is not written. */
    report = dirty_report();
    report.struct_size = sizeof(report) - 1;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_document_check_tokens(b.document, 0, &report));
    NAOS_CHECK_UINT_EQ(99, report.offset);
    report.struct_size = 0;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_document_check_tokens(b.document, 0, &report));

    /* A larger struct_size (a later version) is accepted. */
    {
        struct
        {
            amark_token_report_t report;
            char more[16];
        } larger;

        memset(&larger, 0x5A, sizeof(larger));
        larger.report.struct_size = sizeof(larger);
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                          amark_document_check_tokens(b.document, 0, &larger.report));
        NAOS_CHECK_INT_EQ(AMARK_TOKEN_PROBLEM_NONE, larger.report.problem);
    }

    amark_document_destroy(b.document);
}

/* The out-of-memory sweep: builds a document and checks it, expecting the report
 * NONE on every failure. The check of the lossless document and that of one with
 * a tab column error both run; a correct problem report counts as a success. */
static amark_error_t check_scenario(void *context)
{
    bool *ok = context;
    builder_t b;
    amark_token_report_t report;
    amark_error_t err;

    build_split_tabs(&b);
    err = b.error;
    if (err == AMARK_ERROR_NONE)
    {
        report = dirty_report();
        err = amark_document_check_tokens(b.document, b.source, &report);
        if ((report.problem != AMARK_TOKEN_PROBLEM_NONE) || (report.offset != 0)
            || (report.node != NULL))
        {
            *ok = false;
        }
    }

    if (err == AMARK_ERROR_NONE)
    {
        amark_node_t *root = amark_document_root(b.document);

        /* A regular token over the first tab: TAB_COLUMNS at 1, owned by the root. */
        add(&b, NULL, AMARK_TOKEN_SPACE, 0, 1, 1);
        err = b.error;
        if (err == AMARK_ERROR_NONE)
        {
            report = dirty_report();
            err = amark_document_check_tokens(b.document, b.source, &report);
            if (err == AMARK_ERROR_NOT_LOSSLESS)
            {
                if ((report.problem != AMARK_TOKEN_PROBLEM_TAB_COLUMNS) || (report.offset != 1)
                    || (report.node != root))
                {
                    *ok = false;
                }

                err = AMARK_ERROR_NONE;
            }
            else if ((report.problem != AMARK_TOKEN_PROBLEM_NONE) || (report.offset != 0)
                     || (report.node != NULL) || (err == AMARK_ERROR_NONE))
            {
                *ok = false;
            }
        }
    }

    amark_document_destroy(b.document);

    return err;
}

NAOS_TEST(checking_passes_the_out_of_memory_sweep)
{
    bool ok = true;

    AMARK_CHECK_OOM_SWEEP(check_scenario, &ok);
    NAOS_CHECK_TRUE(ok);
}

NAOS_TEST(checking_allocates_one_block_and_frees_it)
{
    amark_test_allocator_t allocator;
    builder_t b;
    size_t calls;
    size_t live_blocks;

    amark_test_allocator_init(&allocator);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_allocator_set(amark_test_allocator_activate(&allocator)));
    build_split_tabs(&b);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, b.error);

    calls = allocator.calls;
    live_blocks = allocator.live_blocks;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_check_tokens(b.document, b.source, NULL));
    NAOS_CHECK_UINT_EQ(calls + 1, allocator.calls);
    NAOS_CHECK_UINT_EQ(live_blocks, allocator.live_blocks);

    allocator.fail_at = allocator.calls + 1;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_OUT_OF_MEMORY,
                      amark_document_check_tokens(b.document, b.source, NULL));
    NAOS_CHECK_UINT_EQ(1, allocator.failures);
    NAOS_CHECK_UINT_EQ(live_blocks, allocator.live_blocks);
    allocator.fail_at = 0;

    amark_document_destroy(b.document);
    NAOS_CHECK_UINT_EQ(0, allocator.live_blocks);
    amark_allocator_set(NULL);
}

NAOS_SUITE(lossless_problems,
    NAOS_SET_TEARDOWN(restore),
    NAOS_ADD_TEST(a_gap_in_the_middle_is_reported_after_the_previous_owner),
    NAOS_ADD_TEST(a_gap_at_the_start_has_no_owner),
    NAOS_ADD_TEST(a_gap_at_the_end_follows_the_last_token),
    NAOS_ADD_TEST(a_source_without_tokens_is_a_gap),
    NAOS_ADD_TEST(an_overlap_is_reported_at_its_first_byte),
    NAOS_ADD_TEST(two_tokens_at_one_offset_overlap),
    NAOS_ADD_TEST(the_first_problem_is_reported),
    NAOS_ADD_TEST(partial_tabs_whose_columns_do_not_add_up_are_reported),
    NAOS_ADD_TEST(partial_tabs_whose_columns_do_not_follow_from_0_are_reported),
    NAOS_ADD_TEST(a_regular_token_over_a_split_tab_is_reported),
    NAOS_ADD_TEST(a_tab_covered_whole_by_a_regular_token_is_lossless),
    NAOS_ADD_TEST(a_wrong_split_after_a_tab_stop_is_reported),
    NAOS_ADD_TEST(the_tokens_of_detached_nodes_are_left_out),
    NAOS_ADD_TEST(the_invalid_arguments_are_rejected),
    NAOS_ADD_TEST(checking_passes_the_out_of_memory_sweep),
    NAOS_ADD_TEST(checking_allocates_one_block_and_frees_it))

NAOS_MAIN(
    NAOS_ADD_SUITE(lossless),
    NAOS_ADD_SUITE(lossless_problems)
)
