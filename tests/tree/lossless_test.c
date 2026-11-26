/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The tests of LosslessCheck (syntax-tree): the division of each line of a
 * source by the records of the tree (10.1, 10.3, 10.5), the coverage of the
 * content of each leaf by its content tokens (10.2), the report of the first
 * inconsistency (10.4), the time proportional to the lines, the columns and the
 * records (14.7), and the failures of the allocator.
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

/* Checks that source of document is lossless, and that the report is not
 * written. */
#define CHECK_LOSSLESS(document, source)                                                    \
    do                                                                                      \
    {                                                                                       \
        amark_lossless_report_t report_;                                                    \
        amark_lossless_report_t before_;                                                    \
        memset(&report_, 0xA5, sizeof(report_));                                            \
        report_.struct_size = sizeof(report_);                                              \
        before_ = report_;                                                                  \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,                                                 \
                          amark_document_check_lossless((document), (source), &report_));   \
        NAOS_CHECK_INT_EQ(0, memcmp(&before_, &report_, sizeof(report_)));                  \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,                                                 \
                          amark_document_check_lossless((document), (source), NULL));       \
    } while (0)

/* Checks that the check of source reports problem_ at (line_, column_) of the
 * division of the lines. */
#define CHECK_LINE_PROBLEM(document, source, problem_, line_, column_)                      \
    do                                                                                      \
    {                                                                                       \
        amark_lossless_report_t report_;                                                    \
        memset(&report_, 0xA5, sizeof(report_));                                            \
        report_.struct_size = sizeof(report_);                                              \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NOT_LOSSLESS,                                         \
                          amark_document_check_lossless((document), (source), &report_));   \
        NAOS_CHECK_INT_EQ((problem_), report_.problem);                                     \
        NAOS_CHECK_UINT_EQ((line_), report_.line);                                          \
        NAOS_CHECK_UINT_EQ((column_), report_.column);                                      \
        NAOS_CHECK_NULL(report_.leaf);                                                      \
        NAOS_CHECK_UINT_EQ(0, report_.content_offset);                                      \
        NAOS_CHECK_UINT_EQ(sizeof(report_), report_.struct_size);                           \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NOT_LOSSLESS,                                         \
                          amark_document_check_lossless((document), (source), NULL));       \
    } while (0)

/* Checks that the check of source reports problem_ at offset_ of the content
 * of leaf_. */
#define CHECK_CONTENT_PROBLEM(document, source, problem_, leaf_, offset_)                   \
    do                                                                                      \
    {                                                                                       \
        amark_lossless_report_t report_;                                                    \
        memset(&report_, 0xA5, sizeof(report_));                                            \
        report_.struct_size = sizeof(report_);                                              \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NOT_LOSSLESS,                                         \
                          amark_document_check_lossless((document), (source), &report_));   \
        NAOS_CHECK_INT_EQ((problem_), report_.problem);                                     \
        NAOS_CHECK_UINT_EQ(AMARK_LINE_NONE, report_.line);                                  \
        NAOS_CHECK_UINT_EQ(0, report_.column);                                              \
        NAOS_CHECK_TRUE(report_.leaf == (leaf_));                                           \
        NAOS_CHECK_UINT_EQ((offset_), report_.content_offset);                              \
    } while (0)

/* ---- INPUT.md 3.1, 3.3, 3.4 ----------------------------------------------------------- */

/* L0 "> - one *two" (12 bytes), L1 ">   three* four" (15 bytes). */
static const char input_3_1_source[] = "> - one *two\n>   three* four\n";

#define INPUT_3_1_TREE(item_margins, paragraph_tokens, last_text)                    \
    "(document\n"                                                                    \
    "  :lines (0 2)\n"                                                               \
    "  (block_quote\n"                                                               \
    "    :lines (0 2)\n"                                                             \
    "    :margins ((0 0 2 present) (1 0 2 present))\n"                               \
    "    (list :type bullet :start 1 :tight true :style (:bullet \"-\")\n"           \
    "      :lines (0 2)\n"                                                           \
    "      (item\n"                                                                  \
    "        :lines (0 2)\n"                                                         \
    "        :margins " item_margins "\n"                                            \
    "        (paragraph\n"                                                           \
    "          :lines (0 2)\n"                                                       \
    "          " paragraph_tokens "\n"                                               \
    "          :content laid ((0 4 12) (1 4 15))\n"                                  \
    "          (text :spelling ((text 0 4 :facet literal)) \"one \")\n"              \
    "          (emph :style (:delimiter \"*\")\n"                                    \
    "                :spelling ((delimiter 4 5 :role 1) (delimiter 14 15 :role 2))\n" \
    "            (text :spelling ((text 5 8 :facet literal)) \"two\")\n"             \
    "            (softbreak :spelling ((line_break 8 9)))\n"                         \
    "            (text :spelling ((text 9 14 :facet literal)) \"three\"))\n"         \
    "          " last_text ")))))\n"

#define INPUT_3_1_ITEM_MARGINS "((0 2 4 present first) (1 2 4 present))"
#define INPUT_3_1_LAST_TEXT "(text :spelling ((text 15 20 :facet literal)) \" four\")"

NAOS_TEST(input_3_1_is_lossless)
{
    amark_document_t *document = new_document(input_3_1_source);

    NAOS_REQUIRE_NOT_NULL(document);
    AMARK_CHECK_TREE_BUILD(document, NULL,
                           INPUT_3_1_TREE(INPUT_3_1_ITEM_MARGINS, "", INPUT_3_1_LAST_TEXT));
    CHECK_TREE_OK(document);
    CHECK_LOSSLESS(document, 0);
    amark_document_destroy(document);
}

NAOS_TEST(input_3_3_is_lossless)
{
    /* ">\t\tfoo": the margin [0,2) ends inside the first tab, the indentation
     * [2,6) of the code block ends inside the second tab, the content [6,11)
     * is "  foo", covered by a tab remainder and a text token. */
    amark_document_t *document = new_document(">\t\tfoo\n");

    NAOS_REQUIRE_NOT_NULL(document);
    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(document :lines (0 1)\n"
                           "  (block_quote :lines (0 1) :margins ((0 0 2 present))\n"
                           "    (code_block :style (:form indented)\n"
                           "      :lines (0 1)\n"
                           "      :tokens ((space 0 2 6))\n"
                           "      :content laid ((0 6 11))\n"
                           "      :spelling ((tab_remainder 0 2 :facet literal)\n"
                           "                 (text 2 5 :facet literal)))))");
    CHECK_TREE_OK(document);
    CHECK_LOSSLESS(document, 0);
    amark_document_destroy(document);
}

#define INPUT_3_4_TREE(quote_margins)                                                \
    "(document :lines (0 2)\n"                                                       \
    "  (block_quote :lines (0 2) :margins " quote_margins "\n"                       \
    "    (paragraph :lines (0 2) :content laid ((0 2 3) (1 0 1))\n"                  \
    "      (text :spelling ((text 0 1 :facet literal)) \"a\")\n"                    \
    "      (softbreak :spelling ((line_break 1 2)))\n"                               \
    "      (text :spelling ((text 2 3 :facet literal)) \"b\"))))"

NAOS_TEST(input_3_4_is_lossless)
{
    /* "> a" then the lazy continuation "b": the lazy margin has no columns. */
    amark_document_t *document = new_document("> a\nb\n");

    NAOS_REQUIRE_NOT_NULL(document);
    AMARK_CHECK_TREE_BUILD(document, NULL,
                           INPUT_3_4_TREE("((0 0 2 present) (1 0 0 lazy))"));
    CHECK_TREE_OK(document);
    CHECK_LOSSLESS(document, 0);
    amark_document_destroy(document);
}

/* ---- The division of the lines (10.1, 10.3, 10.4) --------------------------------- */

NAOS_TEST(a_deleted_margin_is_a_gap_at_its_position)
{
    amark_document_t *document = new_document(input_3_1_source);

    NAOS_REQUIRE_NOT_NULL(document);
    AMARK_CHECK_TREE_BUILD(document, NULL,
                           INPUT_3_1_TREE("((0 2 4 present first))", "",
                                          INPUT_3_1_LAST_TEXT));
    CHECK_LINE_PROBLEM(document, 0, AMARK_LOSSLESS_GAP, 1, 2);
    amark_document_destroy(document);
}

NAOS_TEST(a_deleted_margin_of_input_3_4_is_a_gap_at_the_start_of_the_line)
{
    amark_document_t *document = new_document("> a\nb\n");

    NAOS_REQUIRE_NOT_NULL(document);
    AMARK_CHECK_TREE_BUILD(document, NULL, INPUT_3_4_TREE("((1 0 0 lazy))"));
    CHECK_LINE_PROBLEM(document, 0, AMARK_LOSSLESS_GAP, 0, 0);
    amark_document_destroy(document);
}

NAOS_TEST(a_duplicated_record_is_an_overlap_at_its_position)
{
    /* The columns [2,4) of L1 are the margin of the item and, again, a layout
     * token of the paragraph. */
    amark_document_t *document = new_document(input_3_1_source);

    NAOS_REQUIRE_NOT_NULL(document);
    AMARK_CHECK_TREE_BUILD(document, NULL,
                           INPUT_3_1_TREE(INPUT_3_1_ITEM_MARGINS, ":tokens ((space 1 2 4))",
                                          INPUT_3_1_LAST_TEXT));
    CHECK_LINE_PROBLEM(document, 0, AMARK_LOSSLESS_OVERLAP, 1, 2);
    amark_document_destroy(document);
}

NAOS_TEST(a_duplicated_content_piece_is_an_overlap)
{
    /* Two leaves sharing the same columns of L0. */
    amark_document_t *document = new_document("ab\n");

    NAOS_REQUIRE_NOT_NULL(document);
    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(paragraph :lines (0 1) :content laid ((0 0 2))\n"
                           "  (text :spelling ((text 0 2 :facet literal)) \"ab\"))\n"
                           "(paragraph :lines (0 1) :content laid ((0 1 2))\n"
                           "  (text :spelling ((text 0 1 :facet literal)) \"b\"))");
    CHECK_LINE_PROBLEM(document, 0, AMARK_LOSSLESS_OVERLAP, 0, 1);
    amark_document_destroy(document);
}

NAOS_TEST(the_end_of_a_line_must_be_covered)
{
    amark_document_t *document = new_document("abc\n");

    NAOS_REQUIRE_NOT_NULL(document);
    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(paragraph :lines (0 1) :content laid ((0 0 2))\n"
                           "  (text :spelling ((text 0 2 :facet literal)) \"ab\"))");
    CHECK_LINE_PROBLEM(document, 0, AMARK_LOSSLESS_GAP, 0, 2);
    amark_document_destroy(document);
}

NAOS_TEST(an_empty_line_must_be_referred_to)
{
    /* L1 is empty: without a blank line it is a gap at its column 0; a blank
     * line of the document node refers to it. */
    static const char text[] = "a\n\nb\n";
    static const char paragraphs[] =
        "  (paragraph :lines (0 1) :content laid ((0 0 1))\n"
        "    (text :spelling ((text 0 1 :facet literal)) \"a\"))\n"
        "  (paragraph :lines (2 1) :content laid ((2 0 1))\n"
        "    (text :spelling ((text 0 1 :facet literal)) \"b\"))";
    char tree[512];
    amark_document_t *document = new_document(text);

    NAOS_REQUIRE_NOT_NULL(document);
    strcpy(tree, "(document :lines (0 3)\n");
    strcat(tree, paragraphs);
    strcat(tree, ")");
    AMARK_CHECK_TREE_BUILD(document, NULL, tree);
    CHECK_LINE_PROBLEM(document, 0, AMARK_LOSSLESS_GAP, 1, 0);
    amark_document_destroy(document);

    document = new_document(text);
    NAOS_REQUIRE_NOT_NULL(document);
    strcpy(tree, "(document :lines (0 3) :blanks ((1 0 0))\n");
    strcat(tree, paragraphs);
    strcat(tree, ")");
    AMARK_CHECK_TREE_BUILD(document, NULL, tree);
    CHECK_TREE_OK(document);
    CHECK_LOSSLESS(document, 0);
    amark_document_destroy(document);
}

NAOS_TEST(an_empty_content_piece_refers_to_an_empty_line)
{
    /* A fenced code block whose content ends with an empty line. */
    amark_document_t *document = new_document("```\na\n\n```\n");

    NAOS_REQUIRE_NOT_NULL(document);
    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(code_block :style (:form fenced)\n"
                           "  :lines (0 4)\n"
                           "  :tokens ((marker 0 0 3) (marker 3 0 3))\n"
                           "  :content laid ((1 0 1) (2 0 0))\n"
                           "  :spelling ((text 0 1 :facet literal)\n"
                           "             (line_break 1 2 :facet literal)))");
    CHECK_LOSSLESS(document, 0);
    amark_document_destroy(document);
}

NAOS_TEST(a_lazy_margin_does_not_refer_to_an_empty_line)
{
    amark_document_t *document = new_document("> a\n\n");

    NAOS_REQUIRE_NOT_NULL(document);
    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(block_quote :lines (0 2) :margins ((0 0 2 present) (1 0 0 lazy))\n"
                           "  (paragraph :lines (0 1) :content laid ((0 2 3))\n"
                           "    (text :spelling ((text 0 1 :facet literal)) \"a\")))");
    CHECK_LINE_PROBLEM(document, 0, AMARK_LOSSLESS_GAP, 1, 0);
    amark_document_destroy(document);
}

NAOS_TEST(a_boundary_inside_a_character_is_reported)
{
    /* "\xC3\xA9" (2 columns) then "a": a marker [0,2) and the content [2,3),
     * then moved by hand (the records refuse such a boundary) to [0,1) and
     * [1,3), which still cover each column once. */
    amark_document_t *document = new_document("\xC3\xA9" "a\n");
    amark_node_t *paragraph;

    NAOS_REQUIRE_NOT_NULL(document);
    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(paragraph :lines (0 1) :tokens ((marker 0 0 2))\n"
                           "  :content laid ((0 2 3))\n"
                           "  (text :spelling ((text 0 1 :facet literal)) \"a\"))");
    CHECK_LOSSLESS(document, 0);
    paragraph = amark_node_first_child(amark_document_root(document));
    paragraph->records->tokens[0].end = 1;
    paragraph->content->pieces[0].start = 1;
    CHECK_LINE_PROBLEM(document, 0, AMARK_LOSSLESS_SPLIT_CHARACTER, 0, 1);
    amark_document_destroy(document);
}

NAOS_TEST(a_split_character_comes_first_at_its_column)
{
    /* The boundary at column 1 is inside the character and column 1 is also
     * covered twice: the split is reported. */
    amark_document_t *document = new_document("\xC3\xA9" "a\n");
    amark_node_t *paragraph;

    NAOS_REQUIRE_NOT_NULL(document);
    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(paragraph :lines (0 1) :tokens ((marker 0 0 2))\n"
                           "  :content laid ((0 2 3))\n"
                           "  (text :spelling ((text 0 1 :facet literal)) \"a\"))");
    paragraph = amark_node_first_child(amark_document_root(document));
    paragraph->content->pieces[0].start = 1;
    CHECK_LINE_PROBLEM(document, 0, AMARK_LOSSLESS_SPLIT_CHARACTER, 0, 1);
    amark_document_destroy(document);
}

NAOS_TEST(the_first_problem_in_line_order_is_reported)
{
    /* L0 has an overlap at column 3, L1 a gap at column 0: L0 comes first;
     * within L0 the earlier column comes first. */
    amark_document_t *document = new_document("abcdef\nxy\n");

    NAOS_REQUIRE_NOT_NULL(document);
    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(paragraph :lines (0 1) :tokens ((marker 0 0 4))\n"
                           "  :content laid ((0 3 5))\n"
                           "  (text :spelling ((text 0 2 :facet literal)) \"de\"))\n"
                           "(paragraph :lines (1 1) :content laid ((1 1 2))\n"
                           "  (text :spelling ((text 0 1 :facet literal)) \"y\"))");
    CHECK_LINE_PROBLEM(document, 0, AMARK_LOSSLESS_OVERLAP, 0, 3);
    amark_document_destroy(document);
}

NAOS_TEST(only_the_nodes_connected_to_the_root_and_the_lines_of_the_source_count)
{
    amark_document_t *document = new_document("x\n");
    amark_source_id_t second = 0;
    amark_node_t *detached;

    NAOS_REQUIRE_NOT_NULL(document);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_add_source(document, "y\n", 2, AMARK_SOURCE_FINISHED,
                                                  &second));
    NAOS_REQUIRE_UINT_EQ(1, second);
    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(paragraph :lines (0 1) :content laid ((0 0 1))\n"
                           "  (text :spelling ((text 0 1 :facet literal)) \"x\"))\n"
                           "(paragraph :lines (0 1) :content laid ((0 0 1)))");
    /* The second paragraph overlaps the first and its content is not covered:
     * once detached, it does not count. */
    detached = child_at(amark_document_root(document), 1);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_unlink(detached));
    CHECK_LOSSLESS(document, 0);
    /* Nothing covers the line of the second source (line 1). */
    CHECK_LINE_PROBLEM(document, 1, AMARK_LOSSLESS_GAP, 1, 0);
    amark_document_destroy(document);
}

NAOS_TEST(the_lines_of_a_source_that_follow_another_source)
{
    /* The lines of the second source come after those of the first. */
    amark_document_t *document = new_document("x\n");
    amark_source_id_t second = 0;

    NAOS_REQUIRE_NOT_NULL(document);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_add_source(document, "yz\nw", 4, AMARK_SOURCE_FINISHED,
                                                  &second));
    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(paragraph :lines (1 2) :content laid ((1 0 2) (2 0 1))\n"
                           "  (text :spelling ((text 0 2 :facet literal)) \"yz\")\n"
                           "  (softbreak :spelling ((line_break 2 3)))\n"
                           "  (text :spelling ((text 3 4 :facet literal)) \"w\"))");
    CHECK_LOSSLESS(document, second);
    CHECK_LINE_PROBLEM(document, 0, AMARK_LOSSLESS_GAP, 0, 0);
    amark_document_destroy(document);
}

/* ---- The coverage of the contents (10.2, 10.4) --------------------------------------- */

NAOS_TEST(a_content_byte_without_a_token_is_a_gap)
{
    amark_document_t *document = new_document(input_3_1_source);
    amark_node_t *paragraph;

    NAOS_REQUIRE_NOT_NULL(document);
    AMARK_CHECK_TREE_BUILD(document, NULL,
                           INPUT_3_1_TREE(INPUT_3_1_ITEM_MARGINS, "", "(text \" four\")"));
    paragraph = amark_node_first_child(amark_node_first_child(
        amark_node_first_child(amark_node_first_child(amark_document_root(document)))));
    CHECK_CONTENT_PROBLEM(document, 0, AMARK_LOSSLESS_GAP, paragraph, 15);
    amark_document_destroy(document);
}

NAOS_TEST(a_content_byte_covered_twice_is_an_overlap)
{
    amark_document_t *document = new_document(input_3_1_source);
    amark_node_t *paragraph;

    NAOS_REQUIRE_NOT_NULL(document);
    AMARK_CHECK_TREE_BUILD(document, NULL,
                           INPUT_3_1_TREE(INPUT_3_1_ITEM_MARGINS, "",
                                          "(text :spelling ((text 14 20 :facet literal)) \"* four\")"));
    paragraph = amark_node_first_child(amark_node_first_child(
        amark_node_first_child(amark_node_first_child(amark_document_root(document)))));
    CHECK_CONTENT_PROBLEM(document, 0, AMARK_LOSSLESS_OVERLAP, paragraph, 14);
    amark_document_destroy(document);
}

NAOS_TEST(a_token_on_another_content_is_foreign)
{
    /* The text of the second paragraph, moved to the first, keeps its base. */
    amark_document_t *document = new_document("a\nb\n");
    amark_node_t *first;
    amark_node_t *second;
    amark_node_t *own;

    NAOS_REQUIRE_NOT_NULL(document);
    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(paragraph :lines (0 1) :content laid ((0 0 1))\n"
                           "  (text :spelling ((text 0 1 :facet literal)) \"a\"))\n"
                           "(paragraph :lines (1 1) :content laid ((1 0 1))\n"
                           "  (text :spelling ((text 0 1 :facet literal)) \"b\"))");
    CHECK_LOSSLESS(document, 0);
    first = child_at(amark_document_root(document), 0);
    second = child_at(amark_document_root(document), 1);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_append_child(first, amark_node_first_child(second)));
    CHECK_TREE_OK(document);
    CHECK_CONTENT_PROBLEM(document, 0, AMARK_LOSSLESS_FOREIGN_TOKEN, first, 0);
    /* Moved back, both are covered; without its text, the first paragraph has
     * a gap at its start. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_append_child(second, amark_node_last_child(first)));
    CHECK_LOSSLESS(document, 0);
    own = amark_node_first_child(first);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_unlink(own));
    CHECK_CONTENT_PROBLEM(document, 0, AMARK_LOSSLESS_GAP, first, 0);
    amark_document_destroy(document);
}

NAOS_TEST(free_content_is_covered_by_its_tokens)
{
    /* A paragraph whose content was replaced by free content without tokens:
     * the lines are still divided, the content is not covered. */
    amark_document_t *document = new_document("a\n");
    amark_node_t *paragraph;

    NAOS_REQUIRE_NOT_NULL(document);
    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(paragraph :lines (0 1) :tokens ((text 0 0 1)))");
    CHECK_LOSSLESS(document, 0);
    paragraph = amark_node_first_child(amark_document_root(document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_set_free_content(paragraph, "xy", 2));
    CHECK_CONTENT_PROBLEM(document, 0, AMARK_LOSSLESS_GAP, paragraph, 0);
    amark_document_destroy(document);
}

NAOS_TEST(the_division_of_the_lines_comes_before_the_contents)
{
    /* Both a gap at the end of L0 and a content gap: the line is reported. */
    amark_document_t *document = new_document("abc\n");

    NAOS_REQUIRE_NOT_NULL(document);
    AMARK_CHECK_TREE_BUILD(document, NULL, "(paragraph :lines (0 1) :content laid ((0 0 2)))");
    CHECK_LINE_PROBLEM(document, 0, AMARK_LOSSLESS_GAP, 0, 2);
    amark_document_destroy(document);
}

NAOS_TEST(the_leaves_are_checked_in_document_order)
{
    /* Two leaves without tokens: the first in document order is on the second
     * line, and it is reported. */
    amark_document_t *document = new_document("a\nb\n");

    NAOS_REQUIRE_NOT_NULL(document);
    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(block_quote :lines (1 1) :margins ((1 0 0 lazy))\n"
                           "  (paragraph :lines (1 1) :content laid ((1 0 1))))\n"
                           "(paragraph :lines (0 1) :content laid ((0 0 1)))");
    CHECK_CONTENT_PROBLEM(document, 0, AMARK_LOSSLESS_GAP,
                          amark_node_first_child(amark_node_first_child(
                              amark_document_root(document))),
                          0);
    amark_document_destroy(document);
}

/* ---- Arguments --------------------------------------------------------------------- */

NAOS_TEST(invalid_arguments_are_refused)
{
    amark_document_t *document = new_document("a\n");
    amark_lossless_report_t report;
    amark_lossless_report_t before;

    NAOS_REQUIRE_NOT_NULL(document);
    memset(&report, 0xA5, sizeof(report));
    report.struct_size = sizeof(report);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_document_check_lossless(NULL, 0, &report));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_document_check_lossless(document, 1, &report));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_document_check_lossless(document, UINT32_MAX, NULL));
    report.struct_size = sizeof(report) - 1;
    before = report;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_document_check_lossless(document, 0, &report));
    NAOS_CHECK_INT_EQ(0, memcmp(&before, &report, sizeof(report)));
    /* A line without records is a gap at its start. */
    CHECK_LINE_PROBLEM(document, 0, AMARK_LOSSLESS_GAP, 0, 0);
    amark_document_destroy(document);
}

NAOS_TEST(a_source_without_lines_and_an_empty_tree_are_lossless)
{
    amark_document_t *document = new_document("");

    NAOS_REQUIRE_NOT_NULL(document);
    CHECK_LOSSLESS(document, 0);
    amark_document_destroy(document);
}

NAOS_TEST(the_bom_and_the_line_endings_are_not_records)
{
    /* The BOM belongs to the source and the CRLF and CR to the line table. */
    amark_document_t *document = new_document("\xEF\xBB\xBF" "a\r\nb\r");

    NAOS_REQUIRE_NOT_NULL(document);
    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(paragraph :lines (0 2) :content laid ((0 0 1) (1 0 1))\n"
                           "  (text :spelling ((text 0 1 :facet literal)) \"a\")\n"
                           "  (softbreak :spelling ((line_break 1 2)))\n"
                           "  (text :spelling ((text 2 3 :facet literal)) \"b\"))");
    CHECK_LOSSLESS(document, 0);
    amark_document_destroy(document);
}

/* ---- Complexity (14.7) --------------------------------------------------------------- */

/* A document of lines lines "ab\tcd" (width 6), one paragraph per line, its
 * content the whole line (5 bytes) covered by one text token; returns the steps
 * of one check. */
static size_t steps_of_lines(size_t lines)
{
    amark_document_t *document = NULL;
    amark_source_id_t source = 0;
    amark_node_t *root;
    size_t i;
    size_t before;
    amark_error_t error;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_add_source(document, NULL, 0, 0, &source));
    for (i = 0; i < lines; i++)
    {
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                            amark_document_append_source(document, source, "ab\tcd\n", 6));
    }
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_finish_source(document, source));
    root = amark_document_root(document);
    for (i = 0; i < lines; i++)
    {
        amark_node_t *paragraph = NULL;
        amark_node_t *text = NULL;
        amark_content_token_t token;

        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                            amark_node_create(document, AMARK_NODE_PARAGRAPH, &paragraph));
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(root, paragraph));
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                            amark_block_set_lines(paragraph, (amark_line_id_t)i, 1));
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                            amark_block_add_content(paragraph, (amark_line_id_t)i, 0, 6));
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                            amark_node_create(document, AMARK_NODE_TEXT, &text));
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(paragraph, text));
        memset(&token, 0, sizeof(token));
        token.struct_size = sizeof(token);
        token.start = 0;
        token.end = 5;
        token.kind = AMARK_SPELL_TEXT;
        token.facet = AMARK_TEXT_LITERAL;
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                            amark_node_add_content_token(text, paragraph, &token));
    }
    before = document->stats.lossless_steps;
    error = amark_document_check_lossless(document, source, NULL);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, error);
    i = document->stats.lossless_steps - before;
    amark_document_destroy(document);
    return i;
}

/* The steps of one check of one line of width columns, divided into width
 * records of one column. */
static size_t steps_of_columns(size_t width)
{
    char *text = (char *)amark_malloc(width + 1);
    amark_document_t *document;
    amark_node_t *paragraph = NULL;
    size_t i;
    size_t before;

    NAOS_REQUIRE_NOT_NULL(text);
    memset(text, 'a', width);
    text[width] = '\n';
    document = new_document_n(text, width + 1);
    amark_free(text);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(document, AMARK_NODE_PARAGRAPH, &paragraph));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_append_child(amark_document_root(document), paragraph));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(paragraph, 0, 1));
    for (i = 0; i < width; i++)
    {
        amark_layout_token_t token;

        memset(&token, 0, sizeof(token));
        token.struct_size = sizeof(token);
        token.line = 0;
        token.column_start = (uint32_t)i;
        token.column_end = (uint32_t)i + 1;
        token.kind = AMARK_SPELL_TEXT;
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_add_token(paragraph, &token));
    }
    before = document->stats.lossless_steps;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_check_lossless(document, 0, NULL));
    i = document->stats.lossless_steps - before;
    amark_document_destroy(document);
    return i;
}

NAOS_TEST(the_time_is_linear_in_the_lines_the_columns_and_the_records)
{
    size_t lines_1 = steps_of_lines(1000);
    size_t lines_2 = steps_of_lines(2000);
    size_t columns_1 = steps_of_columns(5000);
    size_t columns_2 = steps_of_columns(10000);

    /* Each line: 6 columns, 1 piece, 2 nodes, 1 token, 5 bytes of content. */
    NAOS_CHECK_TRUE(lines_1 > 1000);
    NAOS_CHECK_TRUE(lines_1 <= 1000 * 40);
    NAOS_CHECK_TRUE(lines_2 <= 2 * lines_1 + 40);
    NAOS_CHECK_TRUE(columns_1 > 5000);
    NAOS_CHECK_TRUE(columns_1 <= 5000 * 8 + 40);
    NAOS_CHECK_TRUE(columns_2 <= 2 * columns_1 + 40);
}

/* ---- Allocation failures --------------------------------------------------------------- */

static amark_error_t check_scenario(void *context)
{
    return amark_document_check_lossless((const amark_document_t *)context, 0, NULL);
}

NAOS_TEST(every_allocation_failure_is_reported)
{
    /* INPUT.md 3.1 and a paragraph whose content is longer than its lines. */
    amark_document_t *document = new_document("> - one *two\n>   three* four\nab\nab\nab\n");
    amark_test_oom_result_t result;

    NAOS_REQUIRE_NOT_NULL(document);
    AMARK_CHECK_TREE_BUILD(document, NULL,
                           INPUT_3_1_TREE(INPUT_3_1_ITEM_MARGINS, "", INPUT_3_1_LAST_TEXT));
    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(paragraph :lines (2 3) :content laid ((2 0 2) (3 0 2) (4 0 2))\n"
                           "  (text :spelling ((text 0 2 :facet literal)) \"ab\")\n"
                           "  (softbreak :spelling ((line_break 2 3)))\n"
                           "  (text :spelling ((text 3 5 :facet literal)) \"ab\")\n"
                           "  (softbreak :spelling ((line_break 5 6)))\n"
                           "  (text :spelling ((text 6 8 :facet literal)) \"ab\"))");
    CHECK_LOSSLESS(document, 0);
    result = amark_test_oom_sweep(amark_allocator_set, check_scenario, document,
                                  AMARK_TEST_OOM_MAX_ITERATIONS);
    NAOS_CHECK_TRUE(amark_test_oom_check(result, "check_scenario", __FILE__, __LINE__) == 1);
    NAOS_CHECK_TRUE(result.iterations >= 3);
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(a_failure_of_the_content_coverage_is_reported)
{
    /* amark_tree_content_cover grows a scratch shared by all leaves: with an
     * empty scratch and a failing allocator it fails without writing out. */
    static amark_test_allocator_t test_allocator;
    amark_document_t *document = new_document("ab\n");
    amark_tree_cover_scratch_t scratch;
    amark_tree_cover_t cover;
    amark_tree_cover_t before;
    amark_node_t *paragraph;
    amark_error_t error;

    NAOS_REQUIRE_NOT_NULL(document);
    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(paragraph :lines (0 1) :content laid ((0 0 2))\n"
                           "  (text :spelling ((text 0 2 :facet literal)) \"ab\"))");
    paragraph = amark_node_first_child(amark_document_root(document));
    memset(&scratch, 0, sizeof(scratch));
    memset(&cover, 0xA5, sizeof(cover));
    before = cover;
    amark_test_allocator_init(&test_allocator);
    test_allocator.fail_at = 1;
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_allocator_set(amark_test_allocator_activate(&test_allocator)));
    error = amark_tree_content_cover(paragraph, &scratch, &cover);
    (void)amark_allocator_set(NULL);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_OUT_OF_MEMORY, error);
    NAOS_CHECK_INT_EQ(0, memcmp(&before, &cover, sizeof(cover)));
    NAOS_CHECK_NULL(scratch.counts);
    NAOS_CHECK_UINT_EQ(0, test_allocator.live_blocks);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_tree_content_cover(paragraph, &scratch, &cover));
    NAOS_CHECK_TRUE(cover.covered);
    amark_tree_cover_scratch_free(&scratch);
    amark_document_destroy(document);
}

NAOS_SUITE(lossless,
    NAOS_ADD_TEST(input_3_1_is_lossless),
    NAOS_ADD_TEST(input_3_3_is_lossless),
    NAOS_ADD_TEST(input_3_4_is_lossless),
    NAOS_ADD_TEST(a_deleted_margin_is_a_gap_at_its_position),
    NAOS_ADD_TEST(a_deleted_margin_of_input_3_4_is_a_gap_at_the_start_of_the_line),
    NAOS_ADD_TEST(a_duplicated_record_is_an_overlap_at_its_position),
    NAOS_ADD_TEST(a_duplicated_content_piece_is_an_overlap),
    NAOS_ADD_TEST(the_end_of_a_line_must_be_covered),
    NAOS_ADD_TEST(an_empty_line_must_be_referred_to),
    NAOS_ADD_TEST(an_empty_content_piece_refers_to_an_empty_line),
    NAOS_ADD_TEST(a_lazy_margin_does_not_refer_to_an_empty_line),
    NAOS_ADD_TEST(a_boundary_inside_a_character_is_reported),
    NAOS_ADD_TEST(a_split_character_comes_first_at_its_column),
    NAOS_ADD_TEST(the_first_problem_in_line_order_is_reported),
    NAOS_ADD_TEST(only_the_nodes_connected_to_the_root_and_the_lines_of_the_source_count),
    NAOS_ADD_TEST(the_lines_of_a_source_that_follow_another_source),
    NAOS_ADD_TEST(a_content_byte_without_a_token_is_a_gap),
    NAOS_ADD_TEST(a_content_byte_covered_twice_is_an_overlap),
    NAOS_ADD_TEST(a_token_on_another_content_is_foreign),
    NAOS_ADD_TEST(free_content_is_covered_by_its_tokens),
    NAOS_ADD_TEST(the_division_of_the_lines_comes_before_the_contents),
    NAOS_ADD_TEST(the_leaves_are_checked_in_document_order),
    NAOS_ADD_TEST(invalid_arguments_are_refused),
    NAOS_ADD_TEST(a_source_without_lines_and_an_empty_tree_are_lossless),
    NAOS_ADD_TEST(the_bom_and_the_line_endings_are_not_records),
    NAOS_ADD_TEST(the_time_is_linear_in_the_lines_the_columns_and_the_records),
    NAOS_ADD_TEST(every_allocation_failure_is_reported),
    NAOS_ADD_TEST(a_failure_of_the_content_coverage_is_reported)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(lossless)
)
