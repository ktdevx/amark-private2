/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The tests of Position (syntax-tree): the range of a block from its line range
 * and the records of its subtree (2.9), the range of an inline node from its
 * content tokens mapped through the pieces of its base (2.10), and the nodes
 * without a position (2.11).
 */
#include "tree/tree.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <naos/naos.h>

#include "test_allocator.h"
#include "tree_builder.h"

/* ---- Helpers ------------------------------------------------------------------- */

/* A document of the built-in syntax set with one finished source of length bytes. */
static amark_document_t *new_document_n(const char *text, size_t length)
{
    amark_document_t *document = NULL;
    amark_source_id_t source = 0;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_add_source(document, text, length,
                                                  AMARK_SOURCE_FINISHED, &source));
    return document;
}

static amark_document_t *new_document(const char *text)
{
    return new_document_n(text, strlen(text));
}

/* The child index of node (NULL when it has fewer children). */
static amark_node_t *child_at(const amark_node_t *node, size_t index)
{
    amark_node_t *child = amark_node_first_child(node);

    while (child != NULL && index > 0)
    {
        child = amark_node_next(child);
        index--;
    }
    return child;
}

#define CHECK_TREE_OK(document) \
    NAOS_CHECK_STR_EQ("none", amark_tree_violation_name(amark_tree_check((document), NULL)))

/* Checks that node has the range source_ [so, eo) from (sl, sc) to (el, ec). */
#define CHECK_RANGE(node, source_, so, eo, sl, sc, el, ec)                                   \
    do                                                                                       \
    {                                                                                        \
        amark_source_range_t range_;                                                         \
        bool has_ = false;                                                                   \
        memset(&range_, 0xA5, sizeof(range_));                                               \
        range_.struct_size = sizeof(range_);                                                 \
        NAOS_REQUIRE_NOT_NULL(node);                                                         \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_source_range((node), &range_, &has_)); \
        NAOS_CHECK_TRUE(has_);                                                               \
        NAOS_CHECK_UINT_EQ((source_), range_.source);                                        \
        NAOS_CHECK_UINT_EQ((so), range_.start_offset);                                       \
        NAOS_CHECK_UINT_EQ((eo), range_.end_offset);                                         \
        NAOS_CHECK_UINT_EQ((sl), range_.start_line);                                         \
        NAOS_CHECK_UINT_EQ((sc), range_.start_column);                                       \
        NAOS_CHECK_UINT_EQ((el), range_.end_line);                                          \
        NAOS_CHECK_UINT_EQ((ec), range_.end_column);                                         \
        NAOS_CHECK_UINT_EQ(sizeof(range_), range_.struct_size);                              \
    } while (0)

/* Checks that node has no position, and that the range is not written. */
#define CHECK_NO_POSITION(node)                                                              \
    do                                                                                       \
    {                                                                                        \
        amark_source_range_t range_;                                                         \
        amark_source_range_t before_;                                                        \
        bool has_ = true;                                                                    \
        memset(&range_, 0xA5, sizeof(range_));                                               \
        range_.struct_size = sizeof(range_);                                                 \
        before_ = range_;                                                                    \
        NAOS_REQUIRE_NOT_NULL(node);                                                         \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_source_range((node), &range_, &has_)); \
        NAOS_CHECK_FALSE(has_);                                                              \
        NAOS_CHECK_INT_EQ(0, memcmp(&before_, &range_, sizeof(range_)));                     \
    } while (0)

/* ---- INPUT.md 3.1 -------------------------------------------------------------------- */

/* L0 "> - one *two" at 0 (12 bytes), L1 ">   three* four" at 13 (15 bytes). */
static const char input_3_1_source[] = "> - one *two\n>   three* four\n";

static const char input_3_1_tree[] =
    "(document\n"
    "  :lines (0 2)\n"
    "  (block_quote\n"
    "    :lines (0 2)\n"
    "    :margins ((0 0 2 present) (1 0 2 present))\n"
    "    (list :type bullet :start 1 :tight true :style (:bullet \"-\")\n"
    "      :lines (0 2)\n"
    "      (item\n"
    "        :lines (0 2)\n"
    "        :margins ((0 2 4 present first) (1 2 4 present))\n"
    "        (paragraph\n"
    "          :lines (0 2)\n"
    "          :content laid ((0 4 12) (1 4 15))\n"
    "          (text :spelling ((text 0 4 :facet literal)) \"one \")\n"
    "          (emph :style (:delimiter \"*\")\n"
    "                :spelling ((delimiter 4 5 :role 1) (delimiter 14 15 :role 2))\n"
    "            (text :spelling ((text 5 8 :facet literal)) \"two\")\n"
    "            (softbreak :spelling ((line_break 8 9)))\n"
    "            (text :spelling ((text 9 14 :facet literal)) \"three\"))\n"
    "          (text :spelling ((text 15 20 :facet literal)) \" four\"))))))\n";

NAOS_TEST(input_3_1_every_node_has_its_range)
{
    amark_document_t *document = new_document(input_3_1_source);
    amark_node_t *root;
    amark_node_t *quote;
    amark_node_t *list;
    amark_node_t *item;
    amark_node_t *paragraph;
    amark_node_t *emph;

    NAOS_REQUIRE_NOT_NULL(document);
    root = amark_document_root(document);
    AMARK_CHECK_TREE_BUILD(document, NULL, input_3_1_tree);
    CHECK_TREE_OK(document);
    quote = amark_node_first_child(root);
    list = amark_node_first_child(quote);
    item = amark_node_first_child(list);
    paragraph = amark_node_first_child(item);
    emph = child_at(paragraph, 1);

    /* The blocks: the leftmost record on L0 and the rightmost on L1. The
     * document and the block quote start at the margin "> ", the list and the
     * item at the margin "- ", the paragraph at its content; all end at the end
     * of the content on L1 (byte 15, offset 28). */
    CHECK_RANGE(root, 0, 0, 28, 1, 1, 2, 15);
    CHECK_RANGE(quote, 0, 0, 28, 1, 1, 2, 15);
    CHECK_RANGE(list, 0, 2, 28, 1, 3, 2, 15);
    CHECK_RANGE(item, 0, 2, 28, 1, 3, 2, 15);
    CHECK_RANGE(paragraph, 0, 4, 28, 1, 5, 2, 15);

    /* The inline nodes: the content "one *two\nthree* four" mapped to L0 from
     * column 4 and to L1 from column 4. */
    CHECK_RANGE(child_at(paragraph, 0), 0, 4, 8, 1, 5, 1, 8);    /* "one " */
    CHECK_RANGE(emph, 0, 8, 23, 1, 9, 2, 10);                    /* "*two\nthree*" */
    CHECK_RANGE(child_at(emph, 0), 0, 9, 12, 1, 10, 1, 12);      /* "two" */
    CHECK_RANGE(child_at(emph, 1), 0, 12, 13, 1, 13, 1, 13);     /* the LF of L0 */
    CHECK_RANGE(child_at(emph, 2), 0, 17, 22, 2, 5, 2, 9);       /* "three" */
    CHECK_RANGE(child_at(paragraph, 2), 0, 23, 28, 2, 11, 2, 15); /* " four" */

    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

/* ---- Blocks ---------------------------------------------------------------------------- */

NAOS_TEST(a_block_with_only_a_line_range_covers_its_lines)
{
    amark_document_t *document = new_document("abc\nde\n");
    amark_node_t *paragraph;

    NAOS_REQUIRE_NOT_NULL(document);
    AMARK_CHECK_TREE_BUILD(document, NULL, "(paragraph :lines (0 2))");
    paragraph = amark_node_first_child(amark_document_root(document));
    CHECK_RANGE(paragraph, 0, 0, 6, 1, 1, 2, 2);
    amark_document_destroy(document);
}

NAOS_TEST(a_block_starts_and_ends_inside_tabs)
{
    /* INPUT.md 3.3, ">\t\tfoo": the indentation [2,6) of the code block starts
     * inside the first tab (byte 1), its content ends at the width 11 (byte 6). */
    amark_document_t *document = new_document(">\t\tfoo");
    amark_node_t *quote;
    amark_node_t *code;

    NAOS_REQUIRE_NOT_NULL(document);
    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(block_quote :lines (0 1) :margins ((0 0 2 present))\n"
                           "  (code_block :style (:form indented)\n"
                           "    :lines (0 1)\n"
                           "    :tokens ((space 0 2 6))\n"
                           "    :content laid ((0 6 11))))");
    quote = amark_node_first_child(amark_document_root(document));
    code = amark_node_first_child(quote);
    CHECK_RANGE(quote, 0, 0, 6, 1, 1, 1, 6);
    CHECK_RANGE(code, 0, 1, 6, 1, 2, 1, 6);
    amark_document_destroy(document);
}

NAOS_TEST(a_block_counts_the_layout_tokens_of_its_descendants)
{
    /* "# a", a blank line held by the document, "  b": the heading starts at its
     * marker, the paragraph at its indentation; the document spans all lines. */
    amark_document_t *document = new_document("# a\n  \n  b");
    amark_node_t *root;

    NAOS_REQUIRE_NOT_NULL(document);
    root = amark_document_root(document);
    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(document :lines (0 3) :blanks ((1 0 2))\n"
                           "  (heading :level 1 :style (:form atx) :lines (0 1)\n"
                           "    :tokens ((marker 0 0 1) (space 0 1 2))\n"
                           "    :content laid ((0 2 3)))\n"
                           "  (paragraph :lines (2 1) :tokens ((space 2 0 2))\n"
                           "    :content laid ((2 2 3))))");
    CHECK_TREE_OK(document);
    CHECK_RANGE(root, 0, 0, 10, 1, 1, 3, 3);
    CHECK_RANGE(amark_node_first_child(root), 0, 0, 3, 1, 1, 1, 3);
    CHECK_RANGE(child_at(root, 1), 0, 7, 10, 3, 1, 3, 3);
    amark_document_destroy(document);
}

NAOS_TEST(a_block_range_ends_on_the_source_of_its_start)
{
    /* The line range of the document spans two sources: the range is cut to the
     * first source. */
    amark_document_t *document = new_document("ab");
    amark_source_id_t second = 0;
    amark_node_t *root;

    NAOS_REQUIRE_NOT_NULL(document);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_add_source(document, "xyz", 3, AMARK_SOURCE_FINISHED,
                                                  &second));
    root = amark_document_root(document);
    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(document :lines (0 2)\n"
                           "  (paragraph :lines (0 1) :content laid ((0 0 2)))\n"
                           "  (paragraph :lines (1 1) :content laid ((1 0 3))))");
    CHECK_RANGE(root, 0, 0, 2, 1, 1, 1, 2);
    CHECK_RANGE(child_at(root, 1), 1, 0, 3, 1, 1, 1, 3);
    amark_document_destroy(document);
}

NAOS_TEST(an_inline_range_ends_on_the_source_of_its_start)
{
    /* A paragraph laid on a line of each source: the text over both pieces
     * ends with the last piece of the first source. */
    amark_document_t *document = new_document("ab");
    amark_source_id_t second = 0;
    amark_node_t *paragraph;

    NAOS_REQUIRE_NOT_NULL(document);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_add_source(document, "x\tz", 3, AMARK_SOURCE_FINISHED,
                                                  &second));
    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(paragraph :lines (0 2) :content laid ((0 0 2) (1 0 5))\n"
                           "  (text :spelling ((text 0 6 :facet literal)))\n"
                           "  (emph (text :spelling ((text 3 6 :facet literal)))))");
    paragraph = amark_node_first_child(amark_document_root(document));
    CHECK_RANGE(paragraph, 0, 0, 2, 1, 1, 1, 2);
    CHECK_RANGE(amark_node_first_child(paragraph), 0, 0, 2, 1, 1, 1, 2);
    /* Within the second source, the tab is one byte. */
    CHECK_RANGE(child_at(paragraph, 1), 1, 0, 3, 1, 1, 1, 3);
    amark_document_destroy(document);
}

NAOS_TEST(the_offsets_count_the_bom)
{
    amark_document_t *document = new_document("\xEF\xBB\xBF" "ab");
    amark_node_t *paragraph;

    NAOS_REQUIRE_NOT_NULL(document);
    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(paragraph :lines (0 1) :content laid ((0 0 2))\n"
                           "  (text :spelling ((text 1 2 :facet literal)) \"b\"))");
    paragraph = amark_node_first_child(amark_document_root(document));
    CHECK_RANGE(paragraph, 0, 3, 5, 1, 1, 1, 2);
    CHECK_RANGE(amark_node_first_child(paragraph), 0, 4, 5, 1, 2, 1, 2);
    amark_document_destroy(document);
}

/* ---- Inline nodes ------------------------------------------------------------------------ */

NAOS_TEST(an_inline_range_maps_the_expanded_spaces_of_a_tab_to_the_tab)
{
    /* ">\tfoo bar": the margin ends inside the tab (columns 1-4), the content
     * starts with its 2 columns left as spaces: "  foo bar". */
    amark_document_t *document = new_document(">\tfoo bar");
    amark_node_t *paragraph;

    NAOS_REQUIRE_NOT_NULL(document);
    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(block_quote :lines (0 1) :margins ((0 0 2 present))\n"
                           "  (paragraph :lines (0 1) :content laid ((0 2 11))\n"
                           "    (text :spelling ((text 0 2 :facet literal)))\n"
                           "    (text :spelling ((text 2 5 :facet literal)))\n"
                           "    (text :spelling ((text 5 9 :facet literal)))))");
    CHECK_TREE_OK(document);
    paragraph = amark_node_first_child(amark_node_first_child(amark_document_root(document)));
    CHECK_RANGE(paragraph, 0, 1, 9, 1, 2, 1, 9);
    CHECK_RANGE(child_at(paragraph, 0), 0, 1, 2, 1, 2, 1, 2);
    CHECK_RANGE(child_at(paragraph, 1), 0, 2, 5, 1, 3, 1, 5);
    CHECK_RANGE(child_at(paragraph, 2), 0, 5, 9, 1, 6, 1, 9);
    amark_document_destroy(document);
}

NAOS_TEST(an_inline_range_maps_a_piece_that_ends_inside_a_tab)
{
    /* "ab\tc": the content [0,3) ends inside the tab (columns 2-4) with one
     * space, the rest of the line is a layout token. */
    amark_document_t *document = new_document("ab\tc");
    amark_node_t *paragraph;

    NAOS_REQUIRE_NOT_NULL(document);
    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(paragraph :lines (0 1) :tokens ((space 0 3 5))\n"
                           "  :content laid ((0 0 3))\n"
                           "  (text :spelling ((text 0 2 :facet literal)))\n"
                           "  (text :spelling ((text 2 3 :facet literal))))");
    paragraph = amark_node_first_child(amark_document_root(document));
    CHECK_RANGE(paragraph, 0, 0, 4, 1, 1, 1, 4);
    CHECK_RANGE(child_at(paragraph, 0), 0, 0, 2, 1, 1, 1, 2);
    CHECK_RANGE(child_at(paragraph, 1), 0, 2, 3, 1, 3, 1, 3);
    amark_document_destroy(document);
}

NAOS_TEST(a_virtual_lf_maps_to_the_line_ending)
{
    /* "a\r\nb": the softbreak covers the CRLF of the first line. */
    amark_document_t *document = new_document("a\r\nb");
    amark_node_t *paragraph;

    NAOS_REQUIRE_NOT_NULL(document);
    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(paragraph :lines (0 2) :content laid ((0 0 1) (1 0 1))\n"
                           "  (text :spelling ((text 0 1 :facet literal)))\n"
                           "  (softbreak :spelling ((line_break 1 2)))\n"
                           "  (text :spelling ((text 2 3 :facet literal))))");
    paragraph = amark_node_first_child(amark_document_root(document));
    CHECK_RANGE(paragraph, 0, 0, 4, 1, 1, 2, 1);
    CHECK_RANGE(child_at(paragraph, 0), 0, 0, 1, 1, 1, 1, 1);
    CHECK_RANGE(child_at(paragraph, 1), 0, 1, 3, 1, 2, 1, 3);
    CHECK_RANGE(child_at(paragraph, 2), 0, 3, 4, 2, 1, 2, 1);
    amark_document_destroy(document);
}

NAOS_TEST(a_virtual_lf_after_an_empty_line_maps_to_its_line_ending)
{
    /* "a\n\nb" laid on 3 lines, the middle one an empty piece. */
    amark_document_t *document = new_document("a\n\nb");
    amark_node_t *paragraph;

    NAOS_REQUIRE_NOT_NULL(document);
    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(paragraph :lines (0 3) :content laid ((0 0 1) (1 0 0) (2 0 1))\n"
                           "  (text :spelling ((text 0 1 :facet literal)))\n"
                           "  (softbreak :spelling ((line_break 1 2)))\n"
                           "  (softbreak :spelling ((line_break 2 3)))\n"
                           "  (text :spelling ((text 3 4 :facet literal))))");
    paragraph = amark_node_first_child(amark_document_root(document));
    CHECK_RANGE(child_at(paragraph, 1), 0, 1, 2, 1, 2, 1, 2);
    CHECK_RANGE(child_at(paragraph, 2), 0, 2, 3, 2, 1, 2, 1);
    CHECK_RANGE(child_at(paragraph, 3), 0, 3, 4, 3, 1, 3, 1);
    amark_document_destroy(document);
}

NAOS_TEST(an_inline_node_without_tokens_takes_the_range_of_its_descendants)
{
    amark_document_t *document = new_document("x *ab* y");
    amark_node_t *paragraph;
    amark_node_t *emph;

    NAOS_REQUIRE_NOT_NULL(document);
    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(paragraph :lines (0 1) :content laid ((0 0 8))\n"
                           "  (emph :dropped (structure)\n"
                           "    (text :spelling ((text 3 4 :facet literal)))\n"
                           "    (strong (text :spelling ((text 4 5 :facet literal))))))");
    paragraph = amark_node_first_child(amark_document_root(document));
    emph = amark_node_first_child(paragraph);
    CHECK_RANGE(emph, 0, 3, 5, 1, 4, 1, 5);
    CHECK_RANGE(child_at(emph, 1), 0, 4, 5, 1, 5, 1, 5);
    amark_document_destroy(document);
}

/* ---- No position ------------------------------------------------------------------------- */

NAOS_TEST(nodes_without_lines_spelling_or_laid_content_have_no_position)
{
    amark_document_t *document = new_document("abc");
    amark_node_t *root;
    amark_node_t *fresh = NULL;
    amark_node_t *free_leaf;

    NAOS_REQUIRE_NOT_NULL(document);
    root = amark_document_root(document);
    /* The document node of a new tree has no line range. */
    CHECK_NO_POSITION(root);

    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(paragraph :content free \"a b\"\n"
                           "  (text :spelling ((text 0 1 :facet literal)))\n"
                           "  (emph (text)))");
    free_leaf = amark_node_first_child(root);
    /* A new block without a line range or records. */
    CHECK_NO_POSITION(free_leaf);
    /* An inline node on free content. */
    CHECK_NO_POSITION(amark_node_first_child(free_leaf));
    /* An inline node without tokens on itself or its descendants. */
    CHECK_NO_POSITION(child_at(free_leaf, 1));
    CHECK_NO_POSITION(amark_node_first_child(child_at(free_leaf, 1)));

    /* A new node, out of the tree. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, AMARK_NODE_TEXT, &fresh));
    CHECK_NO_POSITION(fresh);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(document, AMARK_NODE_PARAGRAPH, &fresh));
    CHECK_NO_POSITION(fresh);
    amark_document_destroy(document);
}

NAOS_TEST(nodes_whose_spelling_was_discarded_lose_their_position)
{
    amark_document_t *document = new_document("*a*");
    amark_node_t *paragraph;
    amark_node_t *emph;
    amark_node_t *text;

    NAOS_REQUIRE_NOT_NULL(document);
    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(paragraph :lines (0 1) :content laid ((0 0 3))\n"
                           "  (emph :style (:delimiter \"*\")\n"
                           "    :spelling ((delimiter 0 1 :role 1) (delimiter 2 3 :role 2))\n"
                           "    (text :spelling ((text 1 2 :facet literal)) \"a\")))");
    paragraph = amark_node_first_child(amark_document_root(document));
    emph = amark_node_first_child(paragraph);
    text = amark_node_first_child(emph);
    CHECK_RANGE(emph, 0, 0, 3, 1, 1, 1, 3);

    /* Without its own spelling, the emph takes the range of its child. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_discard_spelling(emph));
    CHECK_RANGE(emph, 0, 1, 2, 1, 2, 1, 2);
    /* Without a spelling on itself or its descendants, it has no position. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_discard_spelling(text));
    CHECK_NO_POSITION(text);
    CHECK_NO_POSITION(emph);
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(the_inline_nodes_of_a_leaf_made_free_have_no_position)
{
    amark_document_t *document = new_document("ab");
    amark_node_t *paragraph;
    amark_node_t *text;

    NAOS_REQUIRE_NOT_NULL(document);
    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(paragraph :lines (0 1) :content laid ((0 0 2))\n"
                           "  (text :spelling ((text 0 2 :facet literal)) \"ab\"))");
    paragraph = amark_node_first_child(amark_document_root(document));
    text = amark_node_first_child(paragraph);
    CHECK_RANGE(text, 0, 0, 2, 1, 1, 1, 2);

    /* The content becomes free and the tokens of the text move to it; the
     * paragraph keeps its line range, so it keeps a position. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_discard_spelling(paragraph));
    NAOS_CHECK_INT_EQ(AMARK_CONTENT_FREE, amark_block_content_state(paragraph));
    NAOS_CHECK_UINT_EQ(1, amark_node_content_token_count(text));
    CHECK_NO_POSITION(text);
    CHECK_RANGE(paragraph, 0, 0, 2, 1, 1, 1, 2);
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

/* ---- Arguments and errors ------------------------------------------------------------------ */

NAOS_TEST(invalid_arguments_are_refused)
{
    amark_document_t *document = new_document("abc");
    amark_node_t *root;
    amark_source_range_t range;
    bool has = true;

    NAOS_REQUIRE_NOT_NULL(document);
    root = amark_document_root(document);
    memset(&range, 0, sizeof(range));
    range.struct_size = sizeof(range);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_source_range(NULL, &range, &has));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_source_range(root, NULL, &has));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_source_range(root, &range, NULL));
    range.struct_size = sizeof(range) - 1;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_source_range(root, &range, &has));
    NAOS_CHECK_TRUE(has);
    amark_document_destroy(document);
}

NAOS_TEST(a_failure_to_index_the_tabs_changes_no_output)
{
    /* A block with only a line range on a line with a tab: its range needs the
     * index of the tabs of that line, which nothing built yet. */
    amark_document_t *document = new_document("\tx");
    amark_test_allocator_t test_allocator;
    amark_node_t *paragraph;
    amark_source_range_t range;
    amark_source_range_t before;
    bool has = true;
    amark_error_t error;

    NAOS_REQUIRE_NOT_NULL(document);
    AMARK_CHECK_TREE_BUILD(document, NULL, "(paragraph :lines (0 1))");
    paragraph = amark_node_first_child(amark_document_root(document));
    NAOS_REQUIRE_NULL(document->lines[0].tab_index);
    memset(&range, 0xA5, sizeof(range));
    range.struct_size = sizeof(range);
    before = range;

    amark_test_allocator_init(&test_allocator);
    test_allocator.fail_at = 1;
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_allocator_set(amark_test_allocator_activate(&test_allocator)));
    error = amark_node_source_range(paragraph, &range, &has);
    (void)amark_allocator_set(NULL);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_OUT_OF_MEMORY, error);
    NAOS_CHECK_TRUE(has);
    NAOS_CHECK_INT_EQ(0, memcmp(&before, &range, sizeof(range)));
    NAOS_CHECK_UINT_EQ(0, test_allocator.live_blocks);

    CHECK_RANGE(paragraph, 0, 0, 2, 1, 1, 1, 2);
    amark_document_destroy(document);
}

NAOS_TEST(many_lines_and_many_inline_nodes)
{
    /* A paragraph of 2000 lines with one text node per line: each query of a
     * text node finds its piece by binary search. */
    enum { LINES = 2000 };
    static char text[LINES * 2];
    amark_document_t *document;
    amark_node_t *root;
    amark_node_t *paragraph = NULL;
    amark_node_t *child;
    size_t i;

    for (i = 0; i < LINES; i++)
    {
        text[2 * i] = 'a';
        text[2 * i + 1] = '\n';
    }
    document = new_document_n(text, sizeof(text));
    NAOS_REQUIRE_NOT_NULL(document);
    root = amark_document_root(document);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(document, AMARK_NODE_PARAGRAPH, &paragraph));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(root, paragraph));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(paragraph, 0, LINES));
    for (i = 0; i < LINES; i++)
    {
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                            amark_block_add_content(paragraph, (amark_line_id_t)i, 0, 1));
    }
    for (i = 0; i < LINES; i++)
    {
        amark_content_token_t token;

        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                            amark_node_create(document, AMARK_NODE_TEXT, &child));
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(paragraph, child));
        memset(&token, 0, sizeof(token));
        token.struct_size = sizeof(token);
        token.start = 2 * i;
        token.end = 2 * i + 1;
        token.kind = AMARK_SPELL_TEXT;
        token.facet = AMARK_TEXT_LITERAL;
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                            amark_node_add_content_token(child, paragraph, &token));
    }
    CHECK_RANGE(paragraph, 0, 0, 2 * LINES - 1, 1, 1, LINES, 1);
    child = amark_node_first_child(paragraph);
    for (i = 0; i < LINES; i++)
    {
        CHECK_RANGE(child, 0, 2 * i, 2 * i + 1, i + 1, 1, i + 1, 1);
        child = amark_node_next(child);
    }
    amark_document_destroy(document);
}

NAOS_SUITE(position,
    NAOS_ADD_TEST(input_3_1_every_node_has_its_range),
    NAOS_ADD_TEST(a_block_with_only_a_line_range_covers_its_lines),
    NAOS_ADD_TEST(a_block_starts_and_ends_inside_tabs),
    NAOS_ADD_TEST(a_block_counts_the_layout_tokens_of_its_descendants),
    NAOS_ADD_TEST(a_block_range_ends_on_the_source_of_its_start),
    NAOS_ADD_TEST(an_inline_range_ends_on_the_source_of_its_start),
    NAOS_ADD_TEST(the_offsets_count_the_bom),
    NAOS_ADD_TEST(an_inline_range_maps_the_expanded_spaces_of_a_tab_to_the_tab),
    NAOS_ADD_TEST(an_inline_range_maps_a_piece_that_ends_inside_a_tab),
    NAOS_ADD_TEST(a_virtual_lf_maps_to_the_line_ending),
    NAOS_ADD_TEST(a_virtual_lf_after_an_empty_line_maps_to_its_line_ending),
    NAOS_ADD_TEST(an_inline_node_without_tokens_takes_the_range_of_its_descendants),
    NAOS_ADD_TEST(nodes_without_lines_spelling_or_laid_content_have_no_position),
    NAOS_ADD_TEST(nodes_whose_spelling_was_discarded_lose_their_position),
    NAOS_ADD_TEST(the_inline_nodes_of_a_leaf_made_free_have_no_position),
    NAOS_ADD_TEST(invalid_arguments_are_refused),
    NAOS_ADD_TEST(a_failure_to_index_the_tabs_changes_no_output),
    NAOS_ADD_TEST(many_lines_and_many_inline_nodes)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(position)
)
