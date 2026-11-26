/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The tests of Content (syntax-tree): the content objects of the leaves, their
 * owner and base counts and their release; laid content built piece by piece
 * in the order of the lines (a tab cut by a boundary becomes spaces, the pieces
 * of different lines are joined by a virtual LF, the pieces of one line without
 * anything); free content normalized to LF; the line count that tells empty
 * content from one empty line; the readers; the checks of the pieces; the
 * internal check of the content and the allocation failures (7.9, 8.1, 8.2).
 * The content tokens: their recording on the leaf and its inline descendants,
 * the checks of their range and kind, moving and splitting (INPUT.md 3.1 built
 * by the steps of the delimiter processing), the base and its count, the
 * internal check and the allocation failures (8.3, 8.4, 8.5, 8.8).
 */
#include "tree/tree.h"

#include <amark/allocator.h>

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

/* A node of type with the line range [first, first + count). */
static amark_node_t *new_block(amark_document_t *document, amark_node_type_t type,
                               amark_line_id_t first, uint32_t count)
{
    amark_node_t *node = NULL;

    if (amark_node_create(document, type, &node) != AMARK_ERROR_NONE)
    {
        return NULL;
    }
    if ((count > 0) && (amark_block_set_lines(node, first, count) != AMARK_ERROR_NONE))
    {
        amark_node_destroy(node);
        return NULL;
    }
    return node;
}

/* Checks the bytes of the content of leaf. */
#define CHECK_CONTENT(leaf, expected, expected_length) \
    do { \
        const char *b_ = NULL; \
        size_t l_ = 999; \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_content_bytes((leaf), &b_, &l_)); \
        NAOS_CHECK_UINT_EQ((expected_length), l_); \
        NAOS_CHECK_TRUE(b_ != NULL); \
        if (b_ != NULL) \
        { \
            NAOS_CHECK_TRUE(memcmp(b_, (expected), (expected_length)) == 0); \
            NAOS_CHECK_INT_EQ(0, b_[l_]); \
        } \
    } while (0)

/* Checks piece index of the content of leaf. */
#define CHECK_PIECE(leaf, index, line_, start_, end_, cstart_, clength_) \
    do { \
        amark_content_piece_t p_; \
        memset(&p_, 0, sizeof(p_)); \
        p_.struct_size = sizeof(p_); \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_content_piece_at((leaf), (index), &p_)); \
        NAOS_CHECK_UINT_EQ((line_), p_.line); \
        NAOS_CHECK_UINT_EQ((start_), p_.column_start); \
        NAOS_CHECK_UINT_EQ((end_), p_.column_end); \
        NAOS_CHECK_UINT_EQ((cstart_), p_.content_start); \
        NAOS_CHECK_UINT_EQ((clength_), p_.content_length); \
    } while (0)

#define CHECK_TREE_OK(document) \
    NAOS_CHECK_STR_EQ("none", amark_tree_violation_name(amark_tree_check((document), NULL)))

/* ---- A new leaf ----------------------------------------------------------------- */

NAOS_TEST(a_new_leaf_has_no_content)
{
    amark_document_t *document = new_document("a\n");
    amark_node_t *paragraph;
    const char *bytes = NULL;
    size_t length = 7;

    NAOS_REQUIRE_TRUE(document != NULL);
    paragraph = new_block(document, AMARK_NODE_PARAGRAPH, 0, 1);
    NAOS_REQUIRE_TRUE(paragraph != NULL);
    NAOS_CHECK_INT_EQ(AMARK_CONTENT_NONE, amark_block_content_state(paragraph));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_content_bytes(paragraph, &bytes, &length));
    NAOS_CHECK_STR_EQ("", bytes);
    NAOS_CHECK_UINT_EQ(0, length);
    NAOS_CHECK_UINT_EQ(0, amark_block_content_piece_count(paragraph));
    NAOS_CHECK_TRUE(paragraph->content == NULL);
    NAOS_CHECK_INT_EQ(AMARK_CONTENT_NONE, amark_block_content_state(NULL));
    NAOS_CHECK_INT_EQ(AMARK_CONTENT_NONE,
                      amark_block_content_state(amark_document_root(document)));
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

/* ---- Laid content ------------------------------------------------------------------ */

/* INPUT.md 3.1: the paragraph in the item in the block quote. */
NAOS_TEST(input_3_1_laid_content_joins_lines_with_a_virtual_line_break)
{
    amark_document_t *document = new_document("> - one *two\n>   three* four\n");
    amark_node_t *paragraph;

    NAOS_REQUIRE_TRUE(document != NULL);
    paragraph = new_block(document, AMARK_NODE_PARAGRAPH, 0, 2);
    NAOS_REQUIRE_TRUE(paragraph != NULL);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(paragraph, 0, 4, 12));
    NAOS_CHECK_INT_EQ(AMARK_CONTENT_LAID, amark_block_content_state(paragraph));
    CHECK_CONTENT(paragraph, "one *two", 8);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(paragraph, 1, 4, 15));
    CHECK_CONTENT(paragraph, "one *two\nthree* four", 20);
    NAOS_CHECK_UINT_EQ(2, amark_block_content_piece_count(paragraph));
    CHECK_PIECE(paragraph, 0, 0, 4, 12, 0, 8);
    CHECK_PIECE(paragraph, 1, 1, 4, 15, 9, 11);
    NAOS_REQUIRE_TRUE(paragraph->content != NULL);
    NAOS_CHECK_UINT_EQ(2, paragraph->content->line_count);
    NAOS_CHECK_UINT_EQ(1, paragraph->content->owners);
    NAOS_CHECK_UINT_EQ(0, paragraph->content->bases);
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

/* INPUT.md 3.3: `>\t\tfoo`, an indented code block whose content starts in the
 * middle of the second tab. */
NAOS_TEST(input_3_3_a_tab_cut_by_the_start_becomes_spaces)
{
    amark_document_t *document = new_document(">\t\tfoo\n");
    amark_node_t *quote;
    amark_node_t *code;

    NAOS_REQUIRE_TRUE(document != NULL);
    quote = new_block(document, AMARK_NODE_BLOCK_QUOTE, 0, 1);
    code = new_block(document, AMARK_NODE_CODE_BLOCK, 0, 1);
    NAOS_REQUIRE_TRUE((quote != NULL) && (code != NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(quote, code));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_append_child(amark_document_root(document), quote));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(code, 0, 6, 11));
    CHECK_CONTENT(code, "  foo", 5);
    NAOS_CHECK_UINT_EQ(1, amark_block_content_piece_count(code));
    CHECK_PIECE(code, 0, 0, 6, 11, 0, 5);
    NAOS_CHECK_UINT_EQ(1, code->content->line_count);
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(tabs_cut_by_either_boundary_become_spaces_and_whole_tabs_stay)
{
    /* Line 0: a tab at column 1 (3 columns) then "b"; line 1: two tabs then
     * "x"; line 2: one tab. */
    amark_document_t *document = new_document("a\tb\n\t\tx\n\t\n");
    amark_node_t *code;

    NAOS_REQUIRE_TRUE(document != NULL);
    code = new_block(document, AMARK_NODE_CODE_BLOCK, 0, 3);
    NAOS_REQUIRE_TRUE(code != NULL);
    /* The end inside the tab: "a" and one column of the tab. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(code, 0, 0, 2));
    CHECK_CONTENT(code, "a ", 2);
    /* The start inside the first tab, the second tab whole. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(code, 1, 2, 9));
    CHECK_CONTENT(code, "a \n  \tx", 7);
    /* Both boundaries inside the same tab. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(code, 2, 1, 3));
    CHECK_CONTENT(code, "a \n  \tx\n  ", 10);
    CHECK_PIECE(code, 0, 0, 0, 2, 0, 2);
    CHECK_PIECE(code, 1, 1, 2, 9, 3, 4);
    CHECK_PIECE(code, 2, 2, 1, 3, 8, 2);
    NAOS_CHECK_UINT_EQ(3, code->content->line_count);
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

/* 7.9: the bytes between the pieces of one line are the own spelling of the
 * leaf (here the `\` of a GFM table cell `a\|b`). */
NAOS_TEST(pieces_of_one_line_join_without_a_line_break)
{
    amark_document_t *document = new_document("a\\|b\nc\n");
    amark_node_t *paragraph;

    NAOS_REQUIRE_TRUE(document != NULL);
    paragraph = new_block(document, AMARK_NODE_PARAGRAPH, 0, 2);
    NAOS_REQUIRE_TRUE(paragraph != NULL);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(paragraph, 0, 0, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(paragraph, 0, 2, 4));
    CHECK_CONTENT(paragraph, "a|b", 3);
    NAOS_CHECK_UINT_EQ(1, paragraph->content->line_count);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(paragraph, 1, 0, 1));
    CHECK_CONTENT(paragraph, "a|b\nc", 5);
    CHECK_PIECE(paragraph, 0, 0, 0, 1, 0, 1);
    CHECK_PIECE(paragraph, 1, 0, 2, 4, 1, 2);
    CHECK_PIECE(paragraph, 2, 1, 0, 1, 4, 1);
    NAOS_CHECK_UINT_EQ(2, paragraph->content->line_count);
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

/* The line count tells content without lines from content with one empty line. */
NAOS_TEST(the_line_count_tells_empty_content_from_an_empty_line)
{
    amark_document_t *document = new_document("\n\n");
    amark_node_t *one;
    amark_node_t *two;
    amark_node_t *none;

    NAOS_REQUIRE_TRUE(document != NULL);
    one = new_block(document, AMARK_NODE_CODE_BLOCK, 0, 1);
    two = new_block(document, AMARK_NODE_CODE_BLOCK, 0, 2);
    none = new_block(document, AMARK_NODE_CODE_BLOCK, 0, 0);
    NAOS_REQUIRE_TRUE((one != NULL) && (two != NULL) && (none != NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(one, 0, 0, 0));
    CHECK_CONTENT(one, "", 0);
    NAOS_CHECK_INT_EQ(AMARK_CONTENT_LAID, amark_block_content_state(one));
    NAOS_CHECK_UINT_EQ(1, one->content->line_count);
    NAOS_CHECK_UINT_EQ(1, amark_block_content_piece_count(one));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(two, 0, 0, 0));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(two, 1, 0, 0));
    CHECK_CONTENT(two, "\n", 1);
    NAOS_CHECK_UINT_EQ(2, two->content->line_count);
    /* Free content: empty has no line, a LF makes two. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_set_free_content(none, NULL, 0));
    NAOS_CHECK_INT_EQ(AMARK_CONTENT_FREE, amark_block_content_state(none));
    CHECK_CONTENT(none, "", 0);
    NAOS_CHECK_UINT_EQ(0, none->content->line_count);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_set_free_content(none, "", 0));
    NAOS_CHECK_UINT_EQ(0, none->content->line_count);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_set_free_content(none, "x", 1));
    NAOS_CHECK_UINT_EQ(1, none->content->line_count);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_set_free_content(none, "\n", 1));
    NAOS_CHECK_UINT_EQ(2, none->content->line_count);
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(pieces_out_of_order_or_outside_the_lines_are_refused)
{
    /* "é" is 2 bytes at column 1 to 3 ("aé" then "b"); line 1 is "cd". */
    amark_document_t *document = new_document("a\xC3\xA9" "b\ncd\nef\n");
    amark_node_t *paragraph;
    amark_node_t *quote;
    amark_node_t *emph;
    amark_node_t *outside;
    const char *bytes = NULL;
    size_t length = 0;

    NAOS_REQUIRE_TRUE(document != NULL);
    paragraph = new_block(document, AMARK_NODE_PARAGRAPH, 0, 2);
    quote = new_block(document, AMARK_NODE_BLOCK_QUOTE, 0, 2);
    outside = new_block(document, AMARK_NODE_PARAGRAPH, 0, 0);
    emph = new_block(document, AMARK_NODE_EMPH, 0, 0);
    NAOS_REQUIRE_TRUE((paragraph != NULL) && (quote != NULL) && (emph != NULL) &&
                      (outside != NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_block_add_content(NULL, 0, 0, 1));
    /* Not a block with content. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_block_add_content(quote, 0, 0, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_block_add_content(emph, 0, 0, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_block_set_free_content(quote, "x", 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_block_content_bytes(quote, &bytes, &length));
    /* No line range, a line outside the range or the table. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_block_add_content(outside, 0, 0, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_block_add_content(paragraph, 2, 0, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_block_add_content(paragraph, 9, 0, 1));
    /* Columns: reversed, beyond the width, inside a character. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_block_add_content(paragraph, 0, 2, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_block_add_content(paragraph, 0, 0, 5));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_block_add_content(paragraph, 0, 0, 2));
    NAOS_CHECK_INT_EQ(AMARK_CONTENT_NONE, amark_block_content_state(paragraph));
    NAOS_CHECK_TRUE(paragraph->content == NULL);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(paragraph, 0, 0, 3));
    CHECK_CONTENT(paragraph, "a\xC3\xA9", 3);
    /* The order: overlapping, an empty piece on a line that has one. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_block_add_content(paragraph, 0, 2, 4));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_block_add_content(paragraph, 0, 3, 3));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(paragraph, 1, 0, 2));
    /* An earlier line. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_block_add_content(paragraph, 0, 3, 4));
    CHECK_CONTENT(paragraph, "a\xC3\xA9\ncd", 6);
    NAOS_CHECK_UINT_EQ(2, amark_block_content_piece_count(paragraph));
    /* The line range cannot leave out a piece. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_block_set_lines(paragraph, 1, 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_block_set_lines(paragraph, 0, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_block_set_lines(paragraph, 0, 0));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(paragraph, 0, 3));
    /* Free content does not take pieces. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_set_free_content(outside, "z", 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(outside, 0, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_block_add_content(outside, 0, 0, 1));
    CHECK_CONTENT(outside, "z", 1);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_block_set_free_content(outside, NULL, 1));
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

/* ---- Free content -------------------------------------------------------------------- */

NAOS_TEST(free_content_is_normalized_to_line_feeds)
{
    amark_document_t *document = new_document("one\ntwo\n");
    amark_node_t *paragraph;
    amark_tree_content_t *laid;

    NAOS_REQUIRE_TRUE(document != NULL);
    paragraph = new_block(document, AMARK_NODE_PARAGRAPH, 0, 2);
    NAOS_REQUIRE_TRUE(paragraph != NULL);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(paragraph, 0, 0, 3));
    laid = paragraph->content;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_block_set_free_content(paragraph, "a\r\nb\rc\nd\r\r\n", 11));
    NAOS_CHECK_INT_EQ(AMARK_CONTENT_FREE, amark_block_content_state(paragraph));
    NAOS_CHECK_TRUE(paragraph->content != laid);
    CHECK_CONTENT(paragraph, "a\nb\nc\nd\n\n", 9);
    NAOS_CHECK_UINT_EQ(6, paragraph->content->line_count);
    NAOS_CHECK_UINT_EQ(0, amark_block_content_piece_count(paragraph));
    NAOS_CHECK_UINT_EQ(1, paragraph->content->owners);
    /* Bytes are kept as they are otherwise, NUL and invalid UTF-8 included. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_block_set_free_content(paragraph, "x\0\xFFy\r", 5));
    CHECK_CONTENT(paragraph, "x\0\xFFy\n", 5);
    NAOS_CHECK_UINT_EQ(2, paragraph->content->line_count);
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

/* ---- Readers ------------------------------------------------------------------------- */

NAOS_TEST(readers_refuse_bad_arguments)
{
    amark_document_t *document = new_document("ab\n");
    amark_node_t *paragraph;
    amark_content_piece_t piece;
    const char *bytes = NULL;
    size_t length = 0;

    NAOS_REQUIRE_TRUE(document != NULL);
    paragraph = new_block(document, AMARK_NODE_PARAGRAPH, 0, 1);
    NAOS_REQUIRE_TRUE(paragraph != NULL);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(paragraph, 0, 0, 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_block_content_bytes(NULL, &bytes, &length));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_block_content_bytes(paragraph, NULL, &length));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_block_content_bytes(paragraph, &bytes, NULL));
    NAOS_CHECK_UINT_EQ(0, amark_block_content_piece_count(NULL));
    memset(&piece, 0, sizeof(piece));
    piece.struct_size = sizeof(piece);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_block_content_piece_at(paragraph, 1, &piece));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_block_content_piece_at(NULL, 0, &piece));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_block_content_piece_at(paragraph, 0, NULL));
    piece.struct_size = sizeof(piece) - 1;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_block_content_piece_at(paragraph, 0, &piece));
    amark_document_destroy(document);
}

NAOS_TEST(recording_content_notifies_the_change)
{
    amark_document_t *document = new_document("ab\n");
    amark_node_t *root;
    amark_node_t *paragraph;

    NAOS_REQUIRE_TRUE(document != NULL);
    root = amark_document_root(document);
    paragraph = new_block(document, AMARK_NODE_PARAGRAPH, 0, 1);
    NAOS_REQUIRE_TRUE(paragraph != NULL);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(root, paragraph));
    root->cache_flags |= AMARK_TREE_CACHE_VALID;
    paragraph->cache_flags |= AMARK_TREE_CACHE_VALID;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(paragraph, 0, 0, 2));
    NAOS_CHECK_UINT_EQ(0, paragraph->cache_flags & AMARK_TREE_CACHE_VALID);
    NAOS_CHECK_UINT_EQ(0, root->cache_flags & AMARK_TREE_CACHE_VALID);
    root->cache_flags |= AMARK_TREE_CACHE_VALID;
    paragraph->cache_flags |= AMARK_TREE_CACHE_VALID;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_set_free_content(paragraph, "x", 1));
    NAOS_CHECK_UINT_EQ(0, paragraph->cache_flags & AMARK_TREE_CACHE_VALID);
    NAOS_CHECK_UINT_EQ(0, root->cache_flags & AMARK_TREE_CACHE_VALID);
    /* A refused record changes nothing. */
    root->cache_flags |= AMARK_TREE_CACHE_VALID;
    paragraph->cache_flags |= AMARK_TREE_CACHE_VALID;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_block_add_content(paragraph, 0, 0, 2));
    NAOS_CHECK_UINT_EQ(AMARK_TREE_CACHE_VALID, paragraph->cache_flags & AMARK_TREE_CACHE_VALID);
    root->cache_flags = 0;
    paragraph->cache_flags = 0;
    amark_document_destroy(document);
}

/* ---- Owners, bases and release -------------------------------------------------------- */

NAOS_TEST(a_content_object_is_freed_when_its_counts_reach_zero)
{
    amark_document_t *document = new_document("ab\ncd\n");
    amark_node_t *root;
    amark_node_t *quote;
    amark_node_t *paragraph;
    amark_node_t *loose;
    amark_tree_content_t *content;

    NAOS_REQUIRE_TRUE(document != NULL);
    root = amark_document_root(document);
    quote = new_block(document, AMARK_NODE_BLOCK_QUOTE, 0, 2);
    paragraph = new_block(document, AMARK_NODE_PARAGRAPH, 0, 2);
    loose = new_block(document, AMARK_NODE_PARAGRAPH, 1, 1);
    NAOS_REQUIRE_TRUE((quote != NULL) && (paragraph != NULL) && (loose != NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(root, quote));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(quote, paragraph));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(paragraph, 0, 0, 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(paragraph, 1, 0, 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(loose, 1, 1, 2));

    /* A base keeps the object alive after its owner lets it go (the base count
     * is used by the content tokens; here it is raised by hand). */
    content = paragraph->content;
    content->bases++;
    amark_tree_content_release(content, true);
    paragraph->content = NULL;
    NAOS_CHECK_UINT_EQ(0, content->owners);
    NAOS_CHECK_UINT_EQ(1, content->bases);
    amark_tree_content_release(content, false);
    amark_tree_content_release(NULL, true);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(paragraph, 0, 0, 1));
    /* Destroying a subtree and the document release the remaining objects (the
     * sanitizer build reports any leak). */
    amark_node_destroy(quote);
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

/* ---- The internal check ----------------------------------------------------------------- */

NAOS_TEST(the_check_detects_broken_content)
{
    amark_document_t *document = new_document("ab\ncd\n");
    amark_node_t *paragraph;
    amark_node_t *quote;
    amark_tree_check_report_t report;
    amark_tree_content_t *content;

    NAOS_REQUIRE_TRUE(document != NULL);
    paragraph = new_block(document, AMARK_NODE_PARAGRAPH, 0, 2);
    quote = new_block(document, AMARK_NODE_BLOCK_QUOTE, 0, 2);
    NAOS_REQUIRE_TRUE((paragraph != NULL) && (quote != NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(paragraph, 0, 0, 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(paragraph, 1, 1, 2));
    CHECK_TREE_OK(document);
    content = paragraph->content;

    /* The owner count. */
    content->owners = 2;
    NAOS_CHECK_INT_EQ(AMARK_TREE_VIOLATION_CONTENT, amark_tree_check(document, &report));
    NAOS_CHECK_STR_EQ("content", amark_tree_violation_name(report.kind));
    content->owners = 1;
    /* The base count. */
    content->bases = 1;
    NAOS_CHECK_INT_EQ(AMARK_TREE_VIOLATION_CONTENT, amark_tree_check(document, NULL));
    content->bases = 0;
    /* The bytes differ from the pieces. */
    content->bytes[3] = 'x';
    NAOS_CHECK_INT_EQ(AMARK_TREE_VIOLATION_CONTENT, amark_tree_check(document, &report));
    NAOS_CHECK_TRUE(report.node == paragraph);
    content->bytes[3] = 'd';
    /* The line count. */
    content->line_count = 1;
    NAOS_CHECK_INT_EQ(AMARK_TREE_VIOLATION_CONTENT, amark_tree_check(document, NULL));
    content->line_count = 2;
    /* A piece out of order. */
    content->pieces[1].line = 0;
    NAOS_CHECK_INT_EQ(AMARK_TREE_VIOLATION_CONTENT, amark_tree_check(document, NULL));
    content->pieces[1].line = 1;
    /* A piece beyond the width of its line. */
    content->pieces[1].end = 3;
    NAOS_CHECK_INT_EQ(AMARK_TREE_VIOLATION_CONTENT, amark_tree_check(document, NULL));
    content->pieces[1].end = 2;
    /* The content positions of a piece. */
    content->pieces[1].content_start = 2;
    NAOS_CHECK_INT_EQ(AMARK_TREE_VIOLATION_CONTENT, amark_tree_check(document, NULL));
    content->pieces[1].content_start = 3;
    /* An unknown state. */
    content->state = AMARK_CONTENT_NONE;
    NAOS_CHECK_INT_EQ(AMARK_TREE_VIOLATION_CONTENT, amark_tree_check(document, NULL));
    content->state = AMARK_CONTENT_LAID;
    /* Content on a node that is not a leaf with content. */
    quote->content = content;
    NAOS_CHECK_INT_EQ(AMARK_TREE_VIOLATION_CONTENT, amark_tree_check(document, &report));
    NAOS_CHECK_TRUE(report.node == quote);
    quote->content = NULL;
    CHECK_TREE_OK(document);

    /* Free content that holds a CR, or a wrong line count. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_set_free_content(paragraph, "a\nb", 3));
    CHECK_TREE_OK(document);
    content = paragraph->content;
    content->bytes[1] = '\r';
    NAOS_CHECK_INT_EQ(AMARK_TREE_VIOLATION_CONTENT, amark_tree_check(document, NULL));
    content->bytes[1] = '\n';
    content->line_count = 1;
    NAOS_CHECK_INT_EQ(AMARK_TREE_VIOLATION_CONTENT, amark_tree_check(document, NULL));
    content->line_count = 2;
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

/* ---- Content tokens ------------------------------------------------------------------- */

/* Appends the token [start, end) of kind (facet none, role 0) on the content of
 * leaf to node. */
static amark_error_t add_token(amark_node_t *node, const amark_node_t *leaf, size_t start,
                               size_t end, amark_spell_kind_t kind)
{
    amark_content_token_t token;

    memset(&token, 0, sizeof(token));
    token.struct_size = sizeof(token);
    token.start = start;
    token.end = end;
    token.kind = kind;
    return amark_node_add_content_token(node, leaf, &token);
}

/* Checks token index of node. */
#define CHECK_TOKEN(node, index, kind_, start_, end_) \
    do { \
        amark_content_token_t t_; \
        memset(&t_, 0, sizeof(t_)); \
        t_.struct_size = sizeof(t_); \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_content_token_at((node), (index), &t_)); \
        NAOS_CHECK_INT_EQ((kind_), t_.kind); \
        NAOS_CHECK_UINT_EQ((start_), t_.start); \
        NAOS_CHECK_UINT_EQ((end_), t_.end); \
    } while (0)

/* An inline node of type appended to parent. */
static amark_node_t *new_inline(amark_document_t *document, amark_node_type_t type,
                                amark_node_t *parent)
{
    amark_node_t *node = NULL;

    if (amark_node_create(document, type, &node) != AMARK_ERROR_NONE)
    {
        return NULL;
    }
    if ((parent != NULL) && (amark_node_append_child(parent, node) != AMARK_ERROR_NONE))
    {
        amark_node_destroy(node);
        return NULL;
    }
    return node;
}

/* Checks the spelling layer of INPUT.md 3.1 on paragraph: text_one, emph (with
 * text_two, softbreak and text_three) and text_four, all on the content of the
 * paragraph. */
static void check_input_3_1_layer(amark_node_t *paragraph, amark_node_t *text_one,
                                  amark_node_t *emph, amark_node_t *text_two,
                                  amark_node_t *softbreak, amark_node_t *text_three,
                                  amark_node_t *text_four)
{
    const char *bytes = NULL;
    size_t length = 0;
    amark_content_state_t state = AMARK_CONTENT_NONE;
    bool own = false;

    NAOS_CHECK_TRUE(amark_node_first_child(paragraph) == text_one);
    NAOS_CHECK_TRUE(amark_node_next(text_one) == emph);
    NAOS_CHECK_TRUE(amark_node_next(emph) == text_four);
    NAOS_CHECK_TRUE(amark_node_next(text_four) == NULL);
    NAOS_CHECK_UINT_EQ(1, amark_node_content_token_count(text_one));
    CHECK_TOKEN(text_one, 0, AMARK_SPELL_TEXT, 0, 4);
    NAOS_CHECK_UINT_EQ(2, amark_node_content_token_count(emph));
    CHECK_TOKEN(emph, 0, AMARK_SPELL_DELIMITER, 4, 5);
    CHECK_TOKEN(emph, 1, AMARK_SPELL_DELIMITER, 14, 15);
    NAOS_CHECK_TRUE(amark_node_first_child(emph) == text_two);
    NAOS_CHECK_UINT_EQ(1, amark_node_content_token_count(text_two));
    CHECK_TOKEN(text_two, 0, AMARK_SPELL_TEXT, 5, 8);
    NAOS_CHECK_TRUE(amark_node_next(text_two) == softbreak);
    NAOS_CHECK_UINT_EQ(1, amark_node_content_token_count(softbreak));
    CHECK_TOKEN(softbreak, 0, AMARK_SPELL_LINE_BREAK, 8, 9);
    NAOS_CHECK_TRUE(amark_node_next(softbreak) == text_three);
    NAOS_CHECK_TRUE(amark_node_next(text_three) == NULL);
    NAOS_CHECK_UINT_EQ(1, amark_node_content_token_count(text_three));
    CHECK_TOKEN(text_three, 0, AMARK_SPELL_TEXT, 9, 14);
    NAOS_CHECK_UINT_EQ(1, amark_node_content_token_count(text_four));
    CHECK_TOKEN(text_four, 0, AMARK_SPELL_TEXT, 15, 20);

    /* Every token is on the content of the paragraph: one base per node. */
    NAOS_CHECK_UINT_EQ(1, paragraph->content->owners);
    NAOS_CHECK_UINT_EQ(6, paragraph->content->bases);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_content_base(text_three, &bytes, &length, &state, &own));
    NAOS_CHECK_UINT_EQ(20, length);
    NAOS_CHECK_TRUE(memcmp(bytes, "one *two\nthree* four", 20) == 0);
    NAOS_CHECK_INT_EQ(AMARK_CONTENT_LAID, state);
    NAOS_CHECK_TRUE(own);
}

/*
 * INPUT.md 3.1: the spelling layer of "one *two\nthree* four", built as the
 * delimiter processing of an inline parser builds it. The scan records one text
 * node per line of the content, holding the tokens of the line in order (the
 * text runs as TEXT, the delimiter runs as DELIMITER, the line break). The
 * delimiter processing then moves the delimiter tokens to the emph node, and
 * the tokens of the text and of the line break inside the emphasis to their own
 * nodes under it; the text after the emphasis stays on the node of the second
 * line. With this scan no text token crosses a node boundary of 3.1; the split
 * of a text token that does is the first step of
 * input_3_1_text_scanned_across_a_boundary_is_split below.
 */
NAOS_TEST(input_3_1_spelling_is_built_by_the_delimiter_processing)
{
    amark_document_t *document = new_document("> - one *two\n>   three* four\n");
    amark_node_t *root;
    amark_node_t *paragraph;
    amark_node_t *text_one;
    amark_node_t *line_two;
    amark_node_t *text_two;
    amark_node_t *emph;
    amark_node_t *softbreak;
    amark_node_t *text_three;
    amark_node_t *text_four;

    NAOS_REQUIRE_TRUE(document != NULL);
    root = amark_document_root(document);
    paragraph = new_block(document, AMARK_NODE_PARAGRAPH, 0, 2);
    NAOS_REQUIRE_TRUE(paragraph != NULL);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(root, paragraph));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(paragraph, 0, 4, 12));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(paragraph, 1, 4, 15));
    CHECK_CONTENT(paragraph, "one *two\nthree* four", 20);

    /* The scan of the first line: "one ", "*", "two", the line break. */
    text_one = new_inline(document, AMARK_NODE_TEXT, paragraph);
    line_two = new_inline(document, AMARK_NODE_TEXT, paragraph);
    NAOS_REQUIRE_TRUE((text_one != NULL) && (line_two != NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_token(text_one, paragraph, 0, 4, AMARK_SPELL_TEXT));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      add_token(text_one, paragraph, 4, 5, AMARK_SPELL_DELIMITER));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_token(text_one, paragraph, 5, 8, AMARK_SPELL_TEXT));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      add_token(text_one, paragraph, 8, 9, AMARK_SPELL_LINE_BREAK));
    /* The second line: "three", "*", " four". */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      add_token(line_two, paragraph, 9, 14, AMARK_SPELL_TEXT));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      add_token(line_two, paragraph, 14, 15, AMARK_SPELL_DELIMITER));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      add_token(line_two, paragraph, 15, 20, AMARK_SPELL_TEXT));
    CHECK_TREE_OK(document);

    /* The delimiter processing: the emph node takes the two delimiter tokens;
     * the text inside it goes to text nodes under it; the text after it stays
     * on the node of the second line. */
    emph = new_inline(document, AMARK_NODE_EMPH, NULL);
    text_two = new_inline(document, AMARK_NODE_TEXT, emph);
    softbreak = new_inline(document, AMARK_NODE_SOFTBREAK, emph);
    text_three = new_inline(document, AMARK_NODE_TEXT, emph);
    NAOS_REQUIRE_TRUE((emph != NULL) && (text_two != NULL) && (softbreak != NULL) &&
                      (text_three != NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_after(text_one, emph));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_move_content_tokens(text_one, emph, 1, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_move_content_tokens(line_two, emph, 1, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_move_content_tokens(text_one, text_two, 1, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_move_content_tokens(text_one, softbreak, 1, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_move_content_tokens(line_two, text_three, 0, 1));
    text_four = line_two;
    CHECK_TREE_OK(document);

    check_input_3_1_layer(paragraph, text_one, emph, text_two, softbreak, text_three,
                          text_four);
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

/*
 * INPUT.md 3.1 again, from a scan that records one text token per line (it
 * does not tell the delimiter runs yet) and a softbreak node for the line
 * break. The delimiter processing finds the runs at 4 and 14: it splits the text
 * tokens of the lines around them, moves the parts to their nodes (each run to a
 * delimiter run node, as cmark keeps a run as a text node until it is used),
 * then records the delimiter tokens of the emphasis on the emph node and
 * destroys the run nodes with their text tokens.
 */
NAOS_TEST(input_3_1_text_scanned_across_a_boundary_is_split)
{
    amark_document_t *document = new_document("> - one *two\n>   three* four\n");
    amark_node_t *paragraph;
    amark_node_t *text_one;
    amark_node_t *softbreak;
    amark_node_t *text_three;
    amark_node_t *opener;
    amark_node_t *closer;
    amark_node_t *text_two;
    amark_node_t *text_four;
    amark_node_t *emph;

    NAOS_REQUIRE_TRUE(document != NULL);
    paragraph = new_block(document, AMARK_NODE_PARAGRAPH, 0, 2);
    NAOS_REQUIRE_TRUE(paragraph != NULL);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(paragraph, 0, 4, 12));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(paragraph, 1, 4, 15));
    text_one = new_inline(document, AMARK_NODE_TEXT, paragraph);
    softbreak = new_inline(document, AMARK_NODE_SOFTBREAK, paragraph);
    text_three = new_inline(document, AMARK_NODE_TEXT, paragraph);
    NAOS_REQUIRE_TRUE((text_one != NULL) && (softbreak != NULL) && (text_three != NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_token(text_one, paragraph, 0, 8, AMARK_SPELL_TEXT));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      add_token(softbreak, paragraph, 8, 9, AMARK_SPELL_LINE_BREAK));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      add_token(text_three, paragraph, 9, 20, AMARK_SPELL_TEXT));

    /* Split the line tokens around the runs. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_split_content_token(text_one, 0, 4));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_split_content_token(text_one, 1, 5));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_split_content_token(text_three, 0, 14));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_split_content_token(text_three, 1, 15));
    NAOS_CHECK_UINT_EQ(3, amark_node_content_token_count(text_one));
    CHECK_TOKEN(text_one, 0, AMARK_SPELL_TEXT, 0, 4);
    CHECK_TOKEN(text_one, 1, AMARK_SPELL_TEXT, 4, 5);
    CHECK_TOKEN(text_one, 2, AMARK_SPELL_TEXT, 5, 8);
    CHECK_TREE_OK(document);

    /* Move the parts to their nodes. */
    opener = new_inline(document, AMARK_NODE_TEXT, NULL);
    text_two = new_inline(document, AMARK_NODE_TEXT, NULL);
    closer = new_inline(document, AMARK_NODE_TEXT, NULL);
    text_four = new_inline(document, AMARK_NODE_TEXT, NULL);
    NAOS_REQUIRE_TRUE((opener != NULL) && (text_two != NULL) && (closer != NULL) &&
                      (text_four != NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_after(text_one, opener));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_after(opener, text_two));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_after(text_three, closer));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_after(closer, text_four));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_move_content_tokens(text_one, text_two, 2, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_move_content_tokens(text_one, opener, 1, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_move_content_tokens(text_three, closer, 1, 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_move_content_tokens(closer, text_four, 1, 1));
    CHECK_TREE_OK(document);

    /* The emphasis: its delimiter tokens, its children; the run nodes go. */
    emph = new_inline(document, AMARK_NODE_EMPH, NULL);
    NAOS_REQUIRE_TRUE(emph != NULL);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_after(opener, emph));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_token(emph, paragraph, 4, 5, AMARK_SPELL_DELIMITER));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      add_token(emph, paragraph, 14, 15, AMARK_SPELL_DELIMITER));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(emph, text_two));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(emph, softbreak));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(emph, text_three));
    amark_node_destroy(opener);
    amark_node_destroy(closer);

    check_input_3_1_layer(paragraph, text_one, emph, text_two, softbreak, text_three,
                          text_four);
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(content_tokens_are_recorded_in_order_on_the_leaf_and_its_inlines)
{
    amark_document_t *document = new_document("ab cd\nef\n");
    amark_node_t *paragraph;
    amark_node_t *other;
    amark_node_t *emph;
    amark_node_t *text;
    amark_node_t *quote;
    amark_node_t *stray;
    amark_content_token_t token;

    NAOS_REQUIRE_TRUE(document != NULL);
    paragraph = new_block(document, AMARK_NODE_PARAGRAPH, 0, 2);
    other = new_block(document, AMARK_NODE_PARAGRAPH, 0, 1);
    quote = new_block(document, AMARK_NODE_BLOCK_QUOTE, 0, 0);
    NAOS_REQUIRE_TRUE((paragraph != NULL) && (other != NULL) && (quote != NULL));
    emph = new_inline(document, AMARK_NODE_EMPH, paragraph);
    text = new_inline(document, AMARK_NODE_TEXT, emph);
    stray = new_inline(document, AMARK_NODE_TEXT, NULL);
    NAOS_REQUIRE_TRUE((emph != NULL) && (text != NULL) && (stray != NULL));

    /* A leaf without content takes no token. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      add_token(paragraph, paragraph, 0, 1, AMARK_SPELL_TEXT));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(paragraph, 0, 0, 5));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(paragraph, 1, 0, 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(other, 0, 0, 5));
    CHECK_CONTENT(paragraph, "ab cd\nef", 8);

    /* The leaf itself and its inline descendants at any depth. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_token(paragraph, paragraph, 0, 1, AMARK_SPELL_MARKER));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_token(text, paragraph, 0, 2, AMARK_SPELL_TEXT));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_token(emph, paragraph, 2, 3, AMARK_SPELL_SPACE));
    /* The same position on another node: the order is per node. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_token(text, paragraph, 2, 3, AMARK_SPELL_TEXT));
    /* Not a descendant, a block that is not the leaf, a leaf of another node. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      add_token(stray, paragraph, 0, 1, AMARK_SPELL_TEXT));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      add_token(other, paragraph, 0, 1, AMARK_SPELL_TEXT));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      add_token(text, other, 3, 4, AMARK_SPELL_TEXT));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      add_token(quote, quote, 0, 1, AMARK_SPELL_TEXT));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      add_token(text, quote, 3, 4, AMARK_SPELL_TEXT));
    /* The order: before the end of the last token, overlapping. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      add_token(text, paragraph, 0, 1, AMARK_SPELL_TEXT));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      add_token(text, paragraph, 2, 4, AMARK_SPELL_TEXT));
    /* The range: empty, reversed, beyond the content. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      add_token(text, paragraph, 4, 4, AMARK_SPELL_TEXT));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      add_token(text, paragraph, 5, 4, AMARK_SPELL_TEXT));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      add_token(text, paragraph, 6, 9, AMARK_SPELL_TEXT));
    /* Bad arguments. */
    memset(&token, 0, sizeof(token));
    token.struct_size = sizeof(token);
    token.start = 3;
    token.end = 5;
    token.kind = AMARK_SPELL_TEXT;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_add_content_token(NULL, paragraph, &token));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_add_content_token(text, NULL, &token));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_add_content_token(text, paragraph, NULL));
    token.struct_size = sizeof(token) - 1;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_add_content_token(text, paragraph, &token));
    token.struct_size = sizeof(token);
    token.kind = (amark_spell_kind_t)0;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_add_content_token(text, paragraph, &token));
    token.kind = (amark_spell_kind_t)(AMARK_SPELL_TAB_REMAINDER + 1);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_add_content_token(text, paragraph, &token));
    /* A facet the type does not have (text has 1 facet). */
    token.kind = AMARK_SPELL_TEXT;
    token.facet = 2;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_add_content_token(text, paragraph, &token));
    token.facet = 1;
    token.role = 7;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_add_content_token(text, paragraph, &token));
    memset(&token, 0, sizeof(token));
    token.struct_size = sizeof(token);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_content_token_at(text, 2, &token));
    NAOS_CHECK_UINT_EQ(3, token.start);
    NAOS_CHECK_UINT_EQ(5, token.end);
    NAOS_CHECK_UINT_EQ(1, token.facet);
    NAOS_CHECK_UINT_EQ(7, token.role);
    NAOS_CHECK_UINT_EQ(3, amark_node_content_token_count(text));
    NAOS_CHECK_UINT_EQ(0, amark_node_content_token_count(NULL));
    NAOS_CHECK_UINT_EQ(0, amark_node_content_token_count(stray));
    /* The readers. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_content_token_at(text, 3, &token));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_content_token_at(NULL, 0, &token));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_content_token_at(text, 0, NULL));
    token.struct_size = sizeof(token) - 1;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_content_token_at(text, 0, &token));

    /* A node keeps one base: once the leaf has new content, its tokens stay on
     * the old one, and tokens on the new one are refused. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_set_free_content(paragraph, "ab cd ef", 8));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      add_token(text, paragraph, 6, 8, AMARK_SPELL_TEXT));
    NAOS_CHECK_UINT_EQ(3, amark_node_content_token_count(text));
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(the_kind_of_a_content_token_must_match_its_bytes)
{
    /* Line 0: "a\\*&amp;&bogus;" then a NUL, an invalid byte, "\xEF\xBF\xBD"
     * (a valid U+FFFD), "\xC3\xA9" ("é"); line 1: ">\tb" whose tab is cut at
     * column 2. */
    static const char text[] = "a\\*&amp;&bogus;\0\xFF\xEF\xBF\xBD\xC3\xA9\n>\tb\n";
    amark_document_t *document = NULL;
    amark_source_id_t source = 0;
    amark_node_t *paragraph;
    amark_node_t *code;
    amark_node_t *text_node;
    amark_node_t *free_text;
    amark_node_t *free_leaf;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_add_source(document, text, sizeof(text) - 1,
                                                  AMARK_SOURCE_FINISHED, &source));
    paragraph = new_block(document, AMARK_NODE_PARAGRAPH, 0, 2);
    code = new_block(document, AMARK_NODE_CODE_BLOCK, 0, 2);
    free_leaf = new_block(document, AMARK_NODE_PARAGRAPH, 0, 0);
    NAOS_REQUIRE_TRUE((paragraph != NULL) && (code != NULL) && (free_leaf != NULL));
    text_node = new_inline(document, AMARK_NODE_TEXT, paragraph);
    free_text = new_inline(document, AMARK_NODE_TEXT, free_leaf);
    NAOS_REQUIRE_TRUE((text_node != NULL) && (free_text != NULL));
    /* Paragraph: "a\\*&amp;&bogus;" NUL FF EF BF BD C3 A9 (0..22), LF (22), "  b"
     * (23..26): the tab cut at column 2 is 2 spaces. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(paragraph, 0, 0, 22));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(paragraph, 1, 2, 5));
    CHECK_CONTENT(paragraph, "a\\*&amp;&bogus;\0\xFF\xEF\xBF\xBD\xC3\xA9\n  b", 26);
    /* Code: the whole tab of line 1 stays a tab. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(code, 0, 0, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(code, 1, 0, 5));
    CHECK_CONTENT(code, "a\n>\tb", 5);

    /* TEXT takes anything on character boundaries. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_token(text_node, paragraph, 0, 1, AMARK_SPELL_TEXT));
    /* ESCAPE: a backslash and an ASCII punctuation character. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      add_token(text_node, paragraph, 1, 2, AMARK_SPELL_ESCAPE));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      add_token(text_node, paragraph, 1, 4, AMARK_SPELL_ESCAPE));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      add_token(text_node, paragraph, 2, 4, AMARK_SPELL_ESCAPE));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_token(text_node, paragraph, 1, 3, AMARK_SPELL_ESCAPE));
    /* CHAR_REF: a reference that decodes, and nothing more. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      add_token(text_node, paragraph, 3, 7, AMARK_SPELL_CHAR_REF));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      add_token(text_node, paragraph, 3, 9, AMARK_SPELL_CHAR_REF));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      add_token(text_node, paragraph, 3, 8, AMARK_SPELL_CHAR_REF));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      add_token(text_node, paragraph, 8, 15, AMARK_SPELL_CHAR_REF));
    /* REPLACED: NUL and invalid UTF-8 only; not text, not a valid U+FFFD. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      add_token(text_node, paragraph, 14, 16, AMARK_SPELL_REPLACED));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      add_token(text_node, paragraph, 15, 20, AMARK_SPELL_REPLACED));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      add_token(text_node, paragraph, 15, 17, AMARK_SPELL_REPLACED));
    /* A boundary inside a character ("\xEF\xBF\xBD" at 17, "é" at 20). */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      add_token(text_node, paragraph, 17, 18, AMARK_SPELL_TEXT));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      add_token(text_node, paragraph, 17, 21, AMARK_SPELL_TEXT));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_token(text_node, paragraph, 17, 22, AMARK_SPELL_TEXT));
    /* LINE_BREAK: the virtual LF only. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      add_token(text_node, paragraph, 22, 24, AMARK_SPELL_LINE_BREAK));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      add_token(text_node, paragraph, 22, 23, AMARK_SPELL_LINE_BREAK));
    /* TAB_REMAINDER: the spaces of the cut tab only (not "b"). */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      add_token(text_node, paragraph, 23, 26, AMARK_SPELL_TAB_REMAINDER));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      add_token(text_node, paragraph, 23, 25, AMARK_SPELL_TAB_REMAINDER));
    NAOS_CHECK_UINT_EQ(7, amark_node_content_token_count(text_node));
    /* A whole tab is not a remainder; a space that is a space is not either. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      add_token(code, code, 3, 4, AMARK_SPELL_TAB_REMAINDER));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      add_token(code, code, 0, 1, AMARK_SPELL_LINE_BREAK));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_token(code, code, 1, 2, AMARK_SPELL_LINE_BREAK));

    /* Free content: the LFs are the line breaks; no tab remainder. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_block_set_free_content(free_leaf, "x\r\n  y", 6));
    CHECK_CONTENT(free_leaf, "x\n  y", 5);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      add_token(free_text, free_leaf, 0, 1, AMARK_SPELL_LINE_BREAK));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      add_token(free_text, free_leaf, 1, 2, AMARK_SPELL_LINE_BREAK));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      add_token(free_text, free_leaf, 2, 4, AMARK_SPELL_TAB_REMAINDER));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_token(free_text, free_leaf, 2, 4, AMARK_SPELL_SPACE));
    NAOS_CHECK_UINT_EQ(2, amark_node_content_token_count(free_text));
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(moving_and_splitting_keep_the_order_of_the_content)
{
    amark_document_t *document = new_document("one two three\nzz\n");
    amark_node_t *paragraph;
    amark_node_t *other;
    amark_node_t *a;
    amark_node_t *b;
    amark_node_t *c;
    amark_node_t *quote;
    amark_node_t *link;
    amark_document_t *elsewhere;
    amark_node_t *foreign;

    NAOS_REQUIRE_TRUE(document != NULL);
    elsewhere = new_document("x\n");
    NAOS_REQUIRE_TRUE(elsewhere != NULL);
    paragraph = new_block(document, AMARK_NODE_PARAGRAPH, 0, 1);
    other = new_block(document, AMARK_NODE_PARAGRAPH, 1, 1);
    quote = new_block(document, AMARK_NODE_BLOCK_QUOTE, 0, 0);
    NAOS_REQUIRE_TRUE((paragraph != NULL) && (other != NULL) && (quote != NULL));
    a = new_inline(document, AMARK_NODE_TEXT, paragraph);
    b = new_inline(document, AMARK_NODE_TEXT, paragraph);
    c = new_inline(document, AMARK_NODE_TEXT, other);
    link = new_inline(document, AMARK_NODE_LINK, paragraph);
    foreign = new_inline(elsewhere, AMARK_NODE_TEXT, NULL);
    NAOS_REQUIRE_TRUE((a != NULL) && (b != NULL) && (c != NULL) && (link != NULL) &&
                      (foreign != NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(paragraph, 0, 0, 13));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(other, 1, 0, 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_token(a, paragraph, 0, 3, AMARK_SPELL_TEXT));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_token(a, paragraph, 3, 4, AMARK_SPELL_SPACE));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_token(a, paragraph, 4, 7, AMARK_SPELL_TEXT));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_token(b, paragraph, 8, 13, AMARK_SPELL_TEXT));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_token(c, other, 0, 2, AMARK_SPELL_TEXT));
    NAOS_CHECK_UINT_EQ(2, paragraph->content->bases);
    NAOS_CHECK_UINT_EQ(1, other->content->bases);

    /* Splitting: only TEXT, only strictly inside, on a character boundary. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_split_content_token(a, 1, 4));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_split_content_token(a, 0, 0));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_split_content_token(a, 0, 3));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_split_content_token(a, 3, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_split_content_token(NULL, 0, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_split_content_token(b, 0, 10));
    NAOS_CHECK_UINT_EQ(2, amark_node_content_token_count(b));
    CHECK_TOKEN(b, 0, AMARK_SPELL_TEXT, 8, 10);
    CHECK_TOKEN(b, 1, AMARK_SPELL_TEXT, 10, 13);

    /* Moving: into an empty node, between the tokens of a node, in order. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_move_content_tokens(a, link, 1, 2));
    NAOS_CHECK_UINT_EQ(1, amark_node_content_token_count(a));
    CHECK_TOKEN(link, 0, AMARK_SPELL_SPACE, 3, 4);
    CHECK_TOKEN(link, 1, AMARK_SPELL_TEXT, 4, 7);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_move_content_tokens(b, link, 1, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_move_content_tokens(a, link, 0, 1));
    NAOS_CHECK_UINT_EQ(4, amark_node_content_token_count(link));
    CHECK_TOKEN(link, 0, AMARK_SPELL_TEXT, 0, 3);
    CHECK_TOKEN(link, 3, AMARK_SPELL_TEXT, 10, 13);
    /* Emptied, a holds no base any more. */
    NAOS_CHECK_UINT_EQ(0, amark_node_content_token_count(a));
    NAOS_CHECK_UINT_EQ(2, paragraph->content->bases);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_move_content_tokens(b, link, 0, 1));
    CHECK_TOKEN(link, 3, AMARK_SPELL_TEXT, 8, 10);
    NAOS_CHECK_UINT_EQ(1, paragraph->content->bases);
    CHECK_TREE_OK(document);

    /* Refused moves. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_token(a, paragraph, 2, 9, AMARK_SPELL_TEXT));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_move_content_tokens(a, link, 0, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_move_content_tokens(c, link, 0, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_move_content_tokens(link, a, 0, 0));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_move_content_tokens(link, b, 5, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_move_content_tokens(link, b, 4, 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_move_content_tokens(link, link, 0, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_move_content_tokens(link, quote, 0, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_move_content_tokens(link, foreign, 0, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_move_content_tokens(NULL, b, 0, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_move_content_tokens(link, NULL, 0, 1));
    NAOS_CHECK_UINT_EQ(5, amark_node_content_token_count(link));
    NAOS_CHECK_UINT_EQ(1, amark_node_content_token_count(a));
    /* A token of a facet the destination does not have (link has more facets
     * than text). */
    {
        amark_content_token_t token;
        amark_node_t *d = new_inline(document, AMARK_NODE_LINK, paragraph);

        NAOS_REQUIRE_TRUE(d != NULL);
        memset(&token, 0, sizeof(token));
        token.struct_size = sizeof(token);
        token.start = 0;
        token.end = 2;
        token.kind = AMARK_SPELL_TEXT;
        token.facet = 3;
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_add_content_token(d, paragraph, &token));
        NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                          amark_node_move_content_tokens(d, b, 0, 1));
    }
    /* The leaf itself can receive tokens on its own content. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_move_content_tokens(a, paragraph, 0, 1));
    CHECK_TOKEN(paragraph, 0, AMARK_SPELL_TEXT, 2, 9);
    CHECK_TREE_OK(document);
    amark_document_destroy(elsewhere);
    amark_document_destroy(document);
}

NAOS_TEST(moving_and_splitting_drop_the_derived_values_and_notify)
{
    amark_document_t *document = new_document("abcd\n");
    amark_node_t *root;
    amark_node_t *paragraph;
    amark_node_t *a;
    amark_node_t *b;

    NAOS_REQUIRE_TRUE(document != NULL);
    root = amark_document_root(document);
    paragraph = new_block(document, AMARK_NODE_PARAGRAPH, 0, 1);
    NAOS_REQUIRE_TRUE(paragraph != NULL);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(root, paragraph));
    a = new_inline(document, AMARK_NODE_TEXT, paragraph);
    b = new_inline(document, AMARK_NODE_TEXT, paragraph);
    NAOS_REQUIRE_TRUE((a != NULL) && (b != NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(paragraph, 0, 0, 4));

#define SET_CACHES() \
    do { \
        root->cache_flags |= AMARK_TREE_CACHE_VALID; \
        paragraph->cache_flags |= AMARK_TREE_CACHE_VALID; \
        a->cache_flags |= AMARK_TREE_CACHE_VALID; \
        b->cache_flags |= AMARK_TREE_CACHE_VALID; \
        if (a->facets[0].cache == NULL) \
        { \
            a->facets[0].cache = (char *)amark_malloc(2); \
            a->facets[0].cache_length = 1; \
        } \
        if (b->facets[0].cache == NULL) \
        { \
            b->facets[0].cache = (char *)amark_malloc(2); \
            b->facets[0].cache_length = 1; \
        } \
    } while (0)

    SET_CACHES();
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_token(a, paragraph, 0, 4, AMARK_SPELL_TEXT));
    NAOS_CHECK_TRUE(a->facets[0].cache == NULL);
    NAOS_CHECK_UINT_EQ(0, a->facets[0].cache_length);
    NAOS_CHECK_UINT_EQ(0, a->cache_flags & AMARK_TREE_CACHE_VALID);
    NAOS_CHECK_UINT_EQ(0, root->cache_flags & AMARK_TREE_CACHE_VALID);
    NAOS_CHECK_TRUE(b->facets[0].cache != NULL);

    SET_CACHES();
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_split_content_token(a, 0, 2));
    NAOS_CHECK_TRUE(a->facets[0].cache == NULL);
    NAOS_CHECK_UINT_EQ(0, a->cache_flags & AMARK_TREE_CACHE_VALID);
    NAOS_CHECK_UINT_EQ(0, root->cache_flags & AMARK_TREE_CACHE_VALID);
    NAOS_CHECK_TRUE(b->facets[0].cache != NULL);
    NAOS_CHECK_UINT_EQ(AMARK_TREE_CACHE_VALID, b->cache_flags & AMARK_TREE_CACHE_VALID);

    SET_CACHES();
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_move_content_tokens(a, b, 1, 1));
    NAOS_CHECK_TRUE(a->facets[0].cache == NULL);
    NAOS_CHECK_TRUE(b->facets[0].cache == NULL);
    NAOS_CHECK_UINT_EQ(0, a->cache_flags & AMARK_TREE_CACHE_VALID);
    NAOS_CHECK_UINT_EQ(0, b->cache_flags & AMARK_TREE_CACHE_VALID);
    NAOS_CHECK_UINT_EQ(0, root->cache_flags & AMARK_TREE_CACHE_VALID);

    /* A refused operation changes nothing. */
    SET_CACHES();
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_split_content_token(a, 0, 3));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_move_content_tokens(a, b, 1, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      add_token(a, paragraph, 1, 3, AMARK_SPELL_TEXT));
    NAOS_CHECK_TRUE(a->facets[0].cache != NULL);
    NAOS_CHECK_UINT_EQ(AMARK_TREE_CACHE_VALID, a->cache_flags & AMARK_TREE_CACHE_VALID);
#undef SET_CACHES
    root->cache_flags = 0;
    paragraph->cache_flags = 0;
    a->cache_flags = 0;
    b->cache_flags = 0;
    amark_document_destroy(document);
}

NAOS_TEST(the_base_of_moved_tokens_is_their_original_content)
{
    amark_document_t *document = new_document("ab\ncd\n");
    amark_node_t *first;
    amark_node_t *second;
    amark_node_t *text;
    amark_node_t *empty;
    const char *bytes = NULL;
    size_t length = 9;
    amark_content_state_t state = AMARK_CONTENT_NONE;
    bool own = true;
    amark_tree_content_t *laid;

    NAOS_REQUIRE_TRUE(document != NULL);
    first = new_block(document, AMARK_NODE_PARAGRAPH, 0, 1);
    second = new_block(document, AMARK_NODE_PARAGRAPH, 1, 1);
    NAOS_REQUIRE_TRUE((first != NULL) && (second != NULL));
    text = new_inline(document, AMARK_NODE_TEXT, first);
    empty = new_inline(document, AMARK_NODE_TEXT, NULL);
    NAOS_REQUIRE_TRUE((text != NULL) && (empty != NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(first, 0, 0, 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(second, 1, 0, 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_token(text, first, 0, 2, AMARK_SPELL_TEXT));

    /* No tokens: no base. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_content_base(empty, &bytes, &length, &state, &own));
    NAOS_CHECK_STR_EQ("", bytes);
    NAOS_CHECK_UINT_EQ(0, length);
    NAOS_CHECK_INT_EQ(AMARK_CONTENT_NONE, state);
    NAOS_CHECK_FALSE(own);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_content_base(NULL, &bytes, &length, &state, &own));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_content_base(text, NULL, NULL, NULL, NULL));

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_content_base(text, &bytes, &length, &state, &own));
    NAOS_CHECK_UINT_EQ(2, length);
    NAOS_CHECK_TRUE(memcmp(bytes, "ab", 2) == 0);
    NAOS_CHECK_INT_EQ(AMARK_CONTENT_LAID, state);
    NAOS_CHECK_TRUE(own);
    /* The node moves to another leaf: the base stays the first content. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(second, text));
    own = true;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_content_base(text, &bytes, &length, &state, &own));
    NAOS_CHECK_TRUE(memcmp(bytes, "ab", 2) == 0);
    NAOS_CHECK_FALSE(own);
    /* Detached, it has no leaf. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_unlink(text));
    own = true;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_content_base(text, &bytes, &length, &state, &own));
    NAOS_CHECK_FALSE(own);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(first, text));
    /* The leaf gets free content: the old content lives on as the base. */
    laid = first->content;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_set_free_content(first, "xy", 2));
    NAOS_CHECK_UINT_EQ(0, laid->owners);
    NAOS_CHECK_UINT_EQ(1, laid->bases);
    own = true;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_content_base(text, &bytes, &length, &state, &own));
    NAOS_CHECK_TRUE(memcmp(bytes, "ab", 2) == 0);
    NAOS_CHECK_INT_EQ(AMARK_CONTENT_LAID, state);
    NAOS_CHECK_FALSE(own);
    CHECK_TREE_OK(document);
    /* Releasing the last node with tokens on it frees the object (the
     * sanitizer build reports a leak otherwise). */
    amark_node_destroy(text);
    CHECK_TREE_OK(document);
    /* The leaf's own tokens are on its own content. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_token(first, first, 0, 2, AMARK_SPELL_TEXT));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_content_base(first, &bytes, &length, &state, &own));
    NAOS_CHECK_INT_EQ(AMARK_CONTENT_FREE, state);
    NAOS_CHECK_TRUE(own);
    NAOS_CHECK_UINT_EQ(1, first->content->owners);
    NAOS_CHECK_UINT_EQ(1, first->content->bases);
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(the_check_detects_broken_content_tokens)
{
    amark_document_t *document = new_document("abcd\n");
    amark_node_t *paragraph;
    amark_node_t *text;
    amark_node_t *quote;
    amark_tree_check_report_t report;
    amark_tree_content_tokens_t *tokens;

    NAOS_REQUIRE_TRUE(document != NULL);
    paragraph = new_block(document, AMARK_NODE_PARAGRAPH, 0, 1);
    quote = new_block(document, AMARK_NODE_BLOCK_QUOTE, 0, 0);
    NAOS_REQUIRE_TRUE((paragraph != NULL) && (quote != NULL));
    text = new_inline(document, AMARK_NODE_TEXT, paragraph);
    NAOS_REQUIRE_TRUE(text != NULL);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(paragraph, 0, 0, 4));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_token(text, paragraph, 0, 2, AMARK_SPELL_TEXT));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, add_token(text, paragraph, 2, 4, AMARK_SPELL_TEXT));
    CHECK_TREE_OK(document);
    tokens = text->content_tokens;
    NAOS_REQUIRE_TRUE(tokens != NULL);

    /* Out of order, overlapping. */
    tokens->items[1].start = 1;
    NAOS_CHECK_INT_EQ(AMARK_TREE_VIOLATION_CONTENT, amark_tree_check(document, &report));
    NAOS_CHECK_TRUE(report.node == text);
    tokens->items[1].start = 2;
    /* Empty, beyond the base. */
    tokens->items[1].end = 2;
    NAOS_CHECK_INT_EQ(AMARK_TREE_VIOLATION_CONTENT, amark_tree_check(document, NULL));
    tokens->items[1].end = 5;
    NAOS_CHECK_INT_EQ(AMARK_TREE_VIOLATION_CONTENT, amark_tree_check(document, NULL));
    tokens->items[1].end = 4;
    /* An unknown kind, a facet the type does not have. */
    tokens->items[0].kind = 0;
    NAOS_CHECK_INT_EQ(AMARK_TREE_VIOLATION_CONTENT, amark_tree_check(document, NULL));
    tokens->items[0].kind = AMARK_SPELL_TEXT;
    tokens->items[0].facet = 2;
    NAOS_CHECK_INT_EQ(AMARK_TREE_VIOLATION_CONTENT, amark_tree_check(document, NULL));
    tokens->items[0].facet = 0;
    /* A count beyond the capacity. */
    tokens->count = tokens->capacity + 1;
    NAOS_CHECK_INT_EQ(AMARK_TREE_VIOLATION_CONTENT, amark_tree_check(document, NULL));
    tokens->count = 2;
    /* Tokens without a base. */
    tokens->base = NULL;
    NAOS_CHECK_INT_EQ(AMARK_TREE_VIOLATION_CONTENT, amark_tree_check(document, NULL));
    tokens->base = paragraph->content;
    /* The base count. */
    paragraph->content->bases = 2;
    NAOS_CHECK_INT_EQ(AMARK_TREE_VIOLATION_CONTENT, amark_tree_check(document, &report));
    paragraph->content->bases = 1;
    CHECK_TREE_OK(document);
    /* Tokens on a block that is not a leaf with content. */
    quote->content_tokens = tokens;
    NAOS_CHECK_INT_EQ(AMARK_TREE_VIOLATION_CONTENT, amark_tree_check(document, &report));
    NAOS_CHECK_TRUE(report.node == quote);
    quote->content_tokens = NULL;
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

/* The steps of the delimiter processing of INPUT.md 3.1 under allocation
 * failures: each failed operation leaves the nodes as they were. */
static amark_error_t token_oom_scenario(void *context)
{
    amark_document_t *document = NULL;
    amark_node_t *paragraph = NULL;
    amark_node_t *text = NULL;
    amark_node_t *emph = NULL;
    amark_node_t *other = NULL;
    amark_source_id_t source = 0;
    amark_content_token_t token;
    amark_error_t error;
    size_t i;
    const char *input = "> - one *two\n>   three* four\n";

    (void)context;
    error = amark_document_create(NULL, &document);
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    error = amark_document_add_source(document, input, strlen(input), AMARK_SOURCE_FINISHED,
                                      &source);
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_create(document, AMARK_NODE_PARAGRAPH, &paragraph);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_block_set_lines(paragraph, 0, 2);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_block_add_content(paragraph, 0, 4, 12);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_block_add_content(paragraph, 1, 4, 15);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_create(document, AMARK_NODE_TEXT, &text);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_append_child(paragraph, text);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_create(document, AMARK_NODE_EMPH, &emph);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_create(document, AMARK_NODE_TEXT, &other);
    }
    /* Six tokens on text: the array grows. */
    for (i = 0; (error == AMARK_ERROR_NONE) && (i < 6); i++)
    {
        static const size_t bounds[7] = {0, 4, 5, 8, 9, 14, 15};
        static const amark_spell_kind_t kinds[6] = {
            AMARK_SPELL_TEXT, AMARK_SPELL_DELIMITER, AMARK_SPELL_TEXT,
            AMARK_SPELL_LINE_BREAK, AMARK_SPELL_TEXT, AMARK_SPELL_DELIMITER};

        memset(&token, 0, sizeof(token));
        token.struct_size = sizeof(token);
        token.start = bounds[i];
        token.end = bounds[i + 1];
        token.kind = kinds[i];
        error = amark_node_add_content_token(text, paragraph, &token);
        if ((error != AMARK_ERROR_NONE) && (amark_node_content_token_count(text) != i))
        {
            error = AMARK_ERROR_INVALID_STATE;
        }
    }
    if (error == AMARK_ERROR_NONE)
    {
        /* " four" is one text token, split into " " and "four". */
        memset(&token, 0, sizeof(token));
        token.struct_size = sizeof(token);
        token.start = 15;
        token.end = 20;
        token.kind = AMARK_SPELL_TEXT;
        error = amark_node_add_content_token(text, paragraph, &token);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_split_content_token(text, 6, 16);
        if ((error != AMARK_ERROR_NONE) && (amark_node_content_token_count(text) != 7))
        {
            error = AMARK_ERROR_INVALID_STATE;
        }
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_move_content_tokens(text, emph, 1, 1);
        if ((error != AMARK_ERROR_NONE) && ((amark_node_content_token_count(text) != 8) ||
                                            (amark_node_content_token_count(emph) != 0)))
        {
            error = AMARK_ERROR_INVALID_STATE;
        }
    }
    if (error == AMARK_ERROR_NONE)
    {
        /* The second delimiter is now at index 4. */
        error = amark_node_move_content_tokens(text, emph, 4, 1);
        if ((error != AMARK_ERROR_NONE) && ((amark_node_content_token_count(text) != 7) ||
                                            (amark_node_content_token_count(emph) != 1)))
        {
            error = AMARK_ERROR_INVALID_STATE;
        }
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_move_content_tokens(text, other, 1, 3);
        if ((error != AMARK_ERROR_NONE) && ((amark_node_content_token_count(text) != 6) ||
                                            (amark_node_content_token_count(other) != 0)))
        {
            error = AMARK_ERROR_INVALID_STATE;
        }
    }
    if ((error == AMARK_ERROR_NONE) &&
        (amark_tree_check(document, NULL) != AMARK_TREE_VIOLATION_NONE))
    {
        error = AMARK_ERROR_INVALID_STATE;
    }
    if ((error == AMARK_ERROR_NONE) && ((paragraph->content->bases != 3) ||
                                        (amark_node_content_token_count(emph) != 2)))
    {
        error = AMARK_ERROR_INVALID_STATE;
    }
    amark_document_destroy(document);
    return error;
}

NAOS_TEST(out_of_memory_leaves_the_content_tokens_unchanged)
{
    AMARK_CHECK_OOM_SWEEP(token_oom_scenario, NULL);
}

/* ---- Allocation failures ------------------------------------------------------------ */

static amark_error_t oom_scenario(void *context)
{
    amark_document_t *document = NULL;
    amark_node_t *code = NULL;
    amark_source_id_t source = 0;
    amark_error_t error;
    uint32_t i;
    const char *text = ">\t\tfoo\n> b\n> c\n> d\n> e\n> f\n";

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
        error = amark_node_create(document, AMARK_NODE_CODE_BLOCK, &code);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_block_set_lines(code, 0, 6);
    }
    for (i = 0; (error == AMARK_ERROR_NONE) && (i < 6); i++)
    {
        size_t before = amark_block_content_piece_count(code);

        /* The first piece builds the tab index of the line. */
        error = amark_block_add_content(code, i, (i == 0) ? 6u : 2u, (i == 0) ? 11u : 3u);
        if ((error != AMARK_ERROR_NONE) && (amark_block_content_piece_count(code) != before))
        {
            error = AMARK_ERROR_INVALID_STATE;
        }
    }
    if ((error == AMARK_ERROR_NONE) &&
        (amark_tree_check(document, NULL) != AMARK_TREE_VIOLATION_NONE))
    {
        error = AMARK_ERROR_INVALID_STATE;
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_block_set_free_content(code, "x\r\ny", 4);
        if ((error != AMARK_ERROR_NONE) &&
            (amark_block_content_state(code) != AMARK_CONTENT_LAID))
        {
            error = AMARK_ERROR_INVALID_STATE;
        }
    }
    if ((error == AMARK_ERROR_NONE) &&
        (amark_tree_check(document, NULL) != AMARK_TREE_VIOLATION_NONE))
    {
        error = AMARK_ERROR_INVALID_STATE;
    }
    amark_document_destroy(document);
    return error;
}

NAOS_TEST(out_of_memory_leaves_the_content_unchanged)
{
    AMARK_CHECK_OOM_SWEEP(oom_scenario, NULL);
}

NAOS_SUITE(content,
    NAOS_ADD_TEST(a_new_leaf_has_no_content),
    NAOS_ADD_TEST(input_3_1_laid_content_joins_lines_with_a_virtual_line_break),
    NAOS_ADD_TEST(input_3_3_a_tab_cut_by_the_start_becomes_spaces),
    NAOS_ADD_TEST(tabs_cut_by_either_boundary_become_spaces_and_whole_tabs_stay),
    NAOS_ADD_TEST(pieces_of_one_line_join_without_a_line_break),
    NAOS_ADD_TEST(the_line_count_tells_empty_content_from_an_empty_line),
    NAOS_ADD_TEST(pieces_out_of_order_or_outside_the_lines_are_refused),
    NAOS_ADD_TEST(free_content_is_normalized_to_line_feeds),
    NAOS_ADD_TEST(readers_refuse_bad_arguments),
    NAOS_ADD_TEST(recording_content_notifies_the_change),
    NAOS_ADD_TEST(a_content_object_is_freed_when_its_counts_reach_zero),
    NAOS_ADD_TEST(the_check_detects_broken_content),
    NAOS_ADD_TEST(out_of_memory_leaves_the_content_unchanged),
    NAOS_ADD_TEST(input_3_1_spelling_is_built_by_the_delimiter_processing),
    NAOS_ADD_TEST(input_3_1_text_scanned_across_a_boundary_is_split),
    NAOS_ADD_TEST(content_tokens_are_recorded_in_order_on_the_leaf_and_its_inlines),
    NAOS_ADD_TEST(the_kind_of_a_content_token_must_match_its_bytes),
    NAOS_ADD_TEST(moving_and_splitting_keep_the_order_of_the_content),
    NAOS_ADD_TEST(moving_and_splitting_drop_the_derived_values_and_notify),
    NAOS_ADD_TEST(the_base_of_moved_tokens_is_their_original_content),
    NAOS_ADD_TEST(the_check_detects_broken_content_tokens),
    NAOS_ADD_TEST(out_of_memory_leaves_the_content_tokens_unchanged)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(content)
)
