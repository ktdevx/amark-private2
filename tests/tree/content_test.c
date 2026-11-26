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
    NAOS_ADD_TEST(out_of_memory_leaves_the_content_unchanged)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(content)
)
