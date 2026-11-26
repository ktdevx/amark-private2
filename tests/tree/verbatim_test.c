/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The tests of Verbatim (syntax-tree): the spelling state of a node (11.1), the
 * new nodes (11.6), whether the content of a leaf can be output in its original
 * lines (11.3), the coverage of a content by its tokens, the internal check
 * that the lossless check (13.2) reuses, and whether a subtree can be output in
 * its original spelling (11.4, 11.5, 7.8) with its cache (14.9), and the
 * tightness of a list derived from its blank lines, its cache and its override
 * (3.7, 6.11).
 */
#include "tree/tree.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <naos/naos.h>

#include "test_allocator.h"
#include "tree_builder.h"

/* ---- Helpers ------------------------------------------------------------------- */

/* A document of the built-in syntax set with one finished source holding text. */
static amark_document_t *new_document(const char *text)
{
    amark_document_t *document = NULL;
    amark_source_id_t source = 0;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_add_source(document, text, strlen(text),
                                                  AMARK_SOURCE_FINISHED, &source));
    return document;
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

/* The result of amark_block_content_verbatim, which must succeed. */
static bool verbatim(const amark_node_t *leaf)
{
    bool out = true;

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_content_verbatim(leaf, &out));
    return out;
}

/* The coverage of the content of leaf (amark_tree_content_cover). */
static amark_tree_cover_t cover(const amark_node_t *leaf)
{
    amark_tree_cover_scratch_t scratch = {NULL, 0};
    amark_tree_cover_t out;

    memset(&out, 0xA5, sizeof(out));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_tree_content_cover(leaf, &scratch, &out));
    amark_tree_cover_scratch_free(&scratch);
    return out;
}

/* Checks the coverage of leaf: problem_ at offset_ (with node_ for a foreign
 * token). */
#define CHECK_COVER(leaf, problem_, offset_, node_) \
    do { \
        amark_tree_cover_t c_ = cover(leaf); \
        NAOS_CHECK_INT_EQ((problem_), c_.problem); \
        NAOS_CHECK_TRUE(c_.covered == ((problem_) == AMARK_TREE_COVER_NONE)); \
        NAOS_CHECK_UINT_EQ((offset_), c_.offset); \
        NAOS_CHECK_PTR_EQ((node_), c_.node); \
    } while (0)

static amark_spelling_state_t spelling_state(const amark_node_t *node)
{
    amark_spelling_state_t state;

    memset(&state, 0xA5, sizeof(state));
    state.struct_size = sizeof(state);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_spelling_state(node, &state));
    return state;
}

/* Two paragraphs, "a *b* c" and "d e", separated by a blank line. */
static const char two_paragraphs_source[] = "a *b* c\n\nd e\n";

static const char two_paragraphs_tree[] =
    "(document :lines (0 3) :blanks ((1 0 0))\n"
    "  (paragraph :lines (0 1) :content laid ((0 0 7))\n"
    "    (text :spelling ((text 0 2 :facet literal)) \"a \")\n"
    "    (emph :style (:delimiter \"*\")\n"
    "          :spelling ((delimiter 2 3 :role 1) (delimiter 4 5 :role 2))\n"
    "      (text :spelling ((text 3 4 :facet literal)) \"b\"))\n"
    "    (text :spelling ((text 5 7 :facet literal)) \" c\"))\n"
    "  (paragraph :lines (2 1) :content laid ((2 0 3))\n"
    "    (text :spelling ((text 0 3 :facet literal)) \"d e\")))";

static amark_document_t *two_paragraphs(void)
{
    amark_document_t *document = new_document(two_paragraphs_source);

    AMARK_CHECK_TREE_BUILD(document, NULL, two_paragraphs_tree);
    CHECK_TREE_OK(document);
    return document;
}

/* ---- Spelling state and new nodes (11.1, 11.6) ------------------------------------ */

NAOS_TEST(spelling_state_of_parsed_nodes_is_complete)
{
    amark_document_t *document = two_paragraphs();
    amark_node_t *root = amark_document_root(document);
    amark_node_t *first = child_at(root, 0);
    amark_spelling_state_t state;

    state = spelling_state(root);
    NAOS_CHECK_TRUE(state.has_spelling); /* its blank line */
    NAOS_CHECK_TRUE(state.complete);
    NAOS_CHECK_UINT_EQ(0, state.dropped_facets);
    NAOS_CHECK_FALSE(state.structure_dropped);

    state = spelling_state(first); /* laid content */
    NAOS_CHECK_TRUE(state.has_spelling);
    NAOS_CHECK_TRUE(state.complete);

    state = spelling_state(child_at(first, 1)); /* content tokens */
    NAOS_CHECK_TRUE(state.has_spelling);
    NAOS_CHECK_TRUE(state.complete);
    NAOS_CHECK_UINT_EQ(0, state.dropped_facets);
    NAOS_CHECK_FALSE(state.structure_dropped);

    NAOS_CHECK_FALSE(amark_node_is_new(root));
    NAOS_CHECK_FALSE(amark_node_is_new(first));
    NAOS_CHECK_FALSE(amark_node_is_new(child_at(first, 0)));
    amark_document_destroy(document);
}

NAOS_TEST(spelling_state_tells_the_dropped_facets_and_styles)
{
    amark_document_t *document = two_paragraphs();
    amark_node_t *first = child_at(amark_document_root(document), 0);
    amark_node_t *a = child_at(first, 0);
    amark_node_t *emph = child_at(first, 1);
    amark_spelling_state_t state;

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_string(a, AMARK_TEXT_LITERAL, "x ", 2));
    state = spelling_state(a);
    NAOS_CHECK_FALSE(state.has_spelling); /* its only token went away */
    NAOS_CHECK_FALSE(state.complete);
    NAOS_CHECK_UINT_EQ(1u << AMARK_TEXT_LITERAL, state.dropped_facets);
    NAOS_CHECK_FALSE(state.structure_dropped);
    /* Without a line range, a record, a token or the structural mark, it is
     * judged as a new node (11.6), whose value is composed. */
    NAOS_CHECK_TRUE(amark_node_is_new(a));

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_set_int(emph, AMARK_EMPH_DELIMITER,
                                         AMARK_EMPH_DELIMITER_UNDERSCORE));
    state = spelling_state(emph);
    NAOS_CHECK_TRUE(state.has_spelling); /* the delimiters spell no facet */
    NAOS_CHECK_FALSE(state.complete);
    NAOS_CHECK_UINT_EQ(1u << AMARK_EMPH_DELIMITER, state.dropped_facets);
    NAOS_CHECK_FALSE(state.structure_dropped);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_discard_spelling(emph));
    state = spelling_state(emph);
    NAOS_CHECK_FALSE(state.has_spelling);
    NAOS_CHECK_FALSE(state.complete);
    NAOS_CHECK_UINT_EQ(1u << AMARK_EMPH_DELIMITER, state.dropped_facets);
    NAOS_CHECK_TRUE(state.structure_dropped);
    NAOS_CHECK_FALSE(amark_node_is_new(emph));
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(new_nodes_have_no_range_no_record_no_token_and_no_structure_mark)
{
    amark_document_t *document = new_document("a\n");
    amark_node_t *paragraph = NULL;
    amark_node_t *text = NULL;
    amark_node_t *other = NULL;
    amark_spelling_state_t state;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(document, AMARK_NODE_PARAGRAPH, &paragraph));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, AMARK_NODE_TEXT, &text));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, AMARK_NODE_TEXT, &other));
    NAOS_CHECK_TRUE(amark_node_is_new(paragraph));
    NAOS_CHECK_TRUE(amark_node_is_new(text));
    state = spelling_state(text);
    NAOS_CHECK_FALSE(state.has_spelling);
    NAOS_CHECK_TRUE(state.complete);
    NAOS_CHECK_UINT_EQ(0, state.dropped_facets);
    NAOS_CHECK_FALSE(state.structure_dropped);

    /* A value of its own, and free content, are not spelling. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_string(text, AMARK_TEXT_LITERAL, "t", 1));
    NAOS_CHECK_TRUE(amark_node_is_new(text));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_set_free_content(paragraph, "a", 1));
    NAOS_CHECK_TRUE(amark_node_is_new(paragraph));
    NAOS_CHECK_FALSE(spelling_state(paragraph).has_spelling);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(paragraph, text));
    NAOS_CHECK_TRUE(amark_node_is_new(text));

    /* The structural mark makes a node not new. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_discard_spelling(other));
    NAOS_CHECK_FALSE(amark_node_is_new(other));
    NAOS_CHECK_TRUE(spelling_state(other).structure_dropped);

    /* A line range alone makes a node not new, without being spelling. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(amark_document_root(document),
                                                                paragraph));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(paragraph, 0, 1));
    NAOS_CHECK_FALSE(amark_node_is_new(paragraph));
    NAOS_CHECK_FALSE(spelling_state(paragraph).has_spelling);

    NAOS_CHECK_FALSE(amark_node_is_new(NULL));
    amark_node_destroy(other);
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(spelling_state_rejects_bad_arguments)
{
    amark_document_t *document = new_document("a\n");
    amark_spelling_state_t state;
    bool out = false;

    memset(&state, 0, sizeof(state));
    state.struct_size = sizeof(state);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_spelling_state(NULL, &state));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_spelling_state(amark_document_root(document), NULL));
    state.struct_size = sizeof(state) - 1;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_spelling_state(amark_document_root(document), &state));

    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_block_content_verbatim(NULL, &out));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_block_content_verbatim(amark_document_root(document), &out));
    amark_document_destroy(document);
}

/* ---- Content verbatim (11.3) ------------------------------------------------------- */

NAOS_TEST(unedited_content_is_verbatim)
{
    amark_document_t *document = two_paragraphs();
    amark_node_t *root = amark_document_root(document);

    NAOS_CHECK_TRUE(verbatim(child_at(root, 0)));
    NAOS_CHECK_TRUE(verbatim(child_at(root, 1)));
    CHECK_COVER(child_at(root, 0), AMARK_TREE_COVER_NONE, 0, NULL);
    CHECK_COVER(child_at(root, 1), AMARK_TREE_COVER_NONE, 0, NULL);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_block_content_verbatim(child_at(root, 0), NULL));
    amark_document_destroy(document);
}

NAOS_TEST(reordering_inline_nodes_is_not_verbatim)
{
    amark_document_t *document = two_paragraphs();
    amark_node_t *first = child_at(amark_document_root(document), 0);
    amark_node_t *a = child_at(first, 0);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_after(child_at(first, 1), a));
    NAOS_CHECK_FALSE(verbatim(first));
    /* The tokens still cover the content once: the order alone is broken. */
    CHECK_COVER(first, AMARK_TREE_COVER_NONE, 0, NULL);
    /* Moving it back restores it. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_before(child_at(first, 0), a));
    NAOS_CHECK_TRUE(verbatim(first));
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(moving_a_node_from_another_paragraph_is_not_verbatim)
{
    amark_document_t *document = two_paragraphs();
    amark_node_t *first = child_at(amark_document_root(document), 0);
    amark_node_t *second = child_at(amark_document_root(document), 1);
    amark_node_t *de = child_at(second, 0);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(first, de));
    NAOS_CHECK_FALSE(verbatim(first));
    CHECK_COVER(first, AMARK_TREE_COVER_FOREIGN, 0, de);
    /* The paragraph it left has its content uncovered. */
    NAOS_CHECK_FALSE(verbatim(second));
    CHECK_COVER(second, AMARK_TREE_COVER_GAP, 0, NULL);
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(changing_a_text_value_is_not_verbatim)
{
    amark_document_t *document = two_paragraphs();
    amark_node_t *first = child_at(amark_document_root(document), 0);
    amark_node_t *b = child_at(child_at(first, 1), 0);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_string(b, AMARK_TEXT_LITERAL, "B", 1));
    NAOS_CHECK_FALSE(verbatim(first));
    CHECK_COVER(first, AMARK_TREE_COVER_GAP, 3, NULL);
    /* The other paragraph is not affected. */
    NAOS_CHECK_TRUE(verbatim(child_at(amark_document_root(document), 1)));
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(changing_a_style_is_not_verbatim)
{
    amark_document_t *document = two_paragraphs();
    amark_node_t *first = child_at(amark_document_root(document), 0);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_set_int(child_at(first, 1), AMARK_EMPH_DELIMITER,
                                         AMARK_EMPH_DELIMITER_UNDERSCORE));
    NAOS_CHECK_FALSE(verbatim(first));
    CHECK_COVER(first, AMARK_TREE_COVER_NONE, 0, NULL);
    amark_document_destroy(document);
}

NAOS_TEST(inserting_a_new_node_is_not_verbatim)
{
    amark_document_t *document = two_paragraphs();
    amark_node_t *first = child_at(amark_document_root(document), 0);
    amark_node_t *text = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, AMARK_NODE_TEXT, &text));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_string(text, AMARK_TEXT_LITERAL, "z", 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(first, text));
    NAOS_CHECK_FALSE(verbatim(first));
    /* A node without tokens leaves the coverage as it was. */
    CHECK_COVER(first, AMARK_TREE_COVER_NONE, 0, NULL);
    /* Also under an inline node. */
    amark_node_destroy(text);
    NAOS_CHECK_TRUE(verbatim(first));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, AMARK_NODE_TEXT, &text));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(child_at(first, 1), text));
    NAOS_CHECK_FALSE(verbatim(first));
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(content_that_is_not_laid_is_not_verbatim)
{
    amark_document_t *document = two_paragraphs();
    amark_node_t *first = child_at(amark_document_root(document), 0);
    amark_node_t *second = child_at(amark_document_root(document), 1);
    amark_node_t *empty = NULL;

    /* Dropping the spelling of the leaf makes its content free (its children
     * keep their tokens, moved to the free copy). */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_discard_spelling(first));
    NAOS_CHECK_INT_EQ(AMARK_CONTENT_FREE, amark_block_content_state(first));
    NAOS_CHECK_FALSE(verbatim(first));
    CHECK_COVER(first, AMARK_TREE_COVER_NONE, 0, NULL);

    /* Free content set on a leaf leaves the tokens of its children on the old
     * content. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_set_free_content(second, "d e", 3));
    NAOS_CHECK_FALSE(verbatim(second));
    CHECK_COVER(second, AMARK_TREE_COVER_FOREIGN, 0, child_at(second, 0));

    /* No content. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(document, AMARK_NODE_PARAGRAPH, &empty));
    NAOS_CHECK_FALSE(verbatim(empty));
    CHECK_COVER(empty, AMARK_TREE_COVER_NONE, 0, NULL);
    amark_node_destroy(empty);
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(a_dropped_spelling_under_the_leaf_is_not_verbatim)
{
    amark_document_t *document = new_document("a *b*\n");

    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(paragraph :lines (0 1) :content laid ((0 0 5))\n"
                           "  (text :spelling ((text 0 2 :facet literal)) \"a \")\n"
                           "  (emph :style (:delimiter \"*\") :dropped (delimiter)\n"
                           "        :spelling ((delimiter 2 3) (delimiter 4 5))\n"
                           "    (text :spelling ((text 3 4 :facet literal)) \"b\")))");
    NAOS_CHECK_FALSE(verbatim(child_at(amark_document_root(document), 0)));
    CHECK_COVER(child_at(amark_document_root(document), 0), AMARK_TREE_COVER_NONE, 0, NULL);
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

/* The text left of a delimiter run used in part (task 10): "***foo**" is
 * <p>*<strong>foo</strong></p>, the first "*" a text node with a DELIMITER token
 * and a recorded literal. It is the original spelling. */
NAOS_TEST(a_leftover_delimiter_text_is_verbatim)
{
    amark_document_t *document = new_document("***foo**\n*foo**\n");
    amark_node_t *opener_case;
    amark_node_t *closer_case;

    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(paragraph :lines (0 1) :content laid ((0 0 8))\n"
                           "  (text :spelling ((delimiter 0 1)) \"*\")\n"
                           "  (strong :style (:delimiter \"*\")\n"
                           "          :spelling ((delimiter 1 2) (delimiter 2 3)\n"
                           "                     (delimiter 6 7) (delimiter 7 8))\n"
                           "    (text :spelling ((text 3 6 :facet literal)) \"foo\")))\n"
                           "(paragraph :lines (1 1) :content laid ((1 0 6))\n"
                           "  (emph :style (:delimiter \"*\")\n"
                           "        :spelling ((delimiter 0 1) (delimiter 4 5))\n"
                           "    (text :spelling ((text 1 4 :facet literal)) \"foo\"))\n"
                           "  (text :spelling ((delimiter 5 6)) \"*\"))");
    opener_case = child_at(amark_document_root(document), 0);
    closer_case = child_at(amark_document_root(document), 1);
    NAOS_CHECK_TRUE(verbatim(opener_case));
    NAOS_CHECK_TRUE(verbatim(closer_case));
    CHECK_COVER(opener_case, AMARK_TREE_COVER_NONE, 0, NULL);
    CHECK_COVER(closer_case, AMARK_TREE_COVER_NONE, 0, NULL);
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

/* An autolink made inline by setting its title (task 11) keeps the tokens of its
 * destination, but its form and title are dropped: its content is not output in
 * its original lines. */
NAOS_TEST(an_autolink_made_inline_is_not_verbatim)
{
    amark_document_t *document = new_document("<http://a>\n");
    amark_node_t *paragraph;

    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(paragraph :lines (0 1) :content laid ((0 0 10))\n"
                           "  (link :destination \"http://a\" :style (:form autolink)\n"
                           "        :spelling ((delimiter 0 1) (delimiter 9 10))\n"
                           "    (text :spelling ((text 1 9 :facet literal)) \"http://a\")))");
    paragraph = child_at(amark_document_root(document), 0);
    NAOS_CHECK_TRUE(verbatim(paragraph));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_set_string(child_at(paragraph, 0), AMARK_LINK_TITLE, "t", 1));
    NAOS_CHECK_FALSE(verbatim(paragraph));
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(code_block_content_with_its_own_tokens_is_verbatim)
{
    amark_document_t *document = new_document(">\t\tfoo\n");

    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(block_quote :lines (0 1) :margins ((0 0 2 present))\n"
                           "  (code_block :style (:form indented)\n"
                           "    :lines (0 1)\n"
                           "    :tokens ((space 0 2 6))\n"
                           "    :content laid ((0 6 11))\n"
                           "    :spelling ((tab_remainder 0 2) (text 2 5))\n"
                           "    :info \"\"\n"
                           "    \"  foo\\n\"))");
    NAOS_CHECK_TRUE(verbatim(child_at(child_at(amark_document_root(document), 0), 0)));
    amark_document_destroy(document);
}

/* The nesting order: the tokens of "abcd" cover it once, but the ranges of the
 * nodes do not nest. */
NAOS_TEST(broken_nesting_is_not_verbatim)
{
    amark_document_t *document = new_document("abcd\nabcd\nabcd\nabcd\n");
    amark_node_t *root = amark_document_root(document);
    size_t i;

    AMARK_CHECK_TREE_BUILD(document, NULL,
                           /* Overlapping siblings. */
                           "(paragraph :lines (0 1) :content laid ((0 0 4))\n"
                           "  (text :spelling ((text 0 1) (text 2 3)) \"ac\")\n"
                           "  (text :spelling ((text 1 2) (text 3 4)) \"bd\"))\n"
                           /* A child outside the range of its parent. */
                           "(paragraph :lines (1 1) :content laid ((1 0 4))\n"
                           "  (emph :spelling ((delimiter 0 1) (delimiter 1 2))\n"
                           "    (text :spelling ((text 2 4)) \"cd\")))\n"
                           /* A token of the parent within the range of a child. */
                           "(paragraph :lines (2 1) :content laid ((2 0 4))\n"
                           "  :spelling ((delimiter 1 2))\n"
                           "  (text :spelling ((text 0 1) (text 2 4)) \"acd\"))\n"
                           /* Well nested, for comparison. */
                           "(paragraph :lines (3 1) :content laid ((3 0 4))\n"
                           "  (emph :spelling ((delimiter 0 1) (delimiter 3 4))\n"
                           "    (text :spelling ((text 1 2)) \"b\")\n"
                           "    (text :spelling ((text 2 3)) \"c\")))");
    for (i = 0; i < 3; i++)
    {
        NAOS_CHECK_FALSE(verbatim(child_at(root, i)));
        CHECK_COVER(child_at(root, i), AMARK_TREE_COVER_NONE, 0, NULL);
    }
    NAOS_CHECK_TRUE(verbatim(child_at(root, 3)));
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

/* ---- Content coverage (the internal check) ---------------------------------------- */

NAOS_TEST(cover_reports_the_first_gap_or_overlap)
{
    amark_document_t *document = new_document("abcdef\nabcdef\nabcdef\nabc\n");
    amark_node_t *root = amark_document_root(document);

    AMARK_CHECK_TREE_BUILD(document, NULL,
                           /* A gap at 2 (and one at 5). */
                           "(paragraph :lines (0 1) :content laid ((0 0 6))\n"
                           "  (text :spelling ((text 0 2) (text 3 5)) \"abde\"))\n"
                           /* An overlap at 3, before a gap at 5. */
                           "(paragraph :lines (1 1) :content laid ((1 0 6))\n"
                           "  (text :spelling ((text 0 4)) \"abcd\")\n"
                           "  (text :spelling ((text 3 5)) \"de\"))\n"
                           /* A gap at the end only. */
                           "(paragraph :lines (2 1) :content laid ((2 0 6))\n"
                           "  (text :spelling ((text 0 5)) \"abcde\"))\n"
                           /* Covered once. */
                           "(paragraph :lines (3 1) :content laid ((3 0 3))\n"
                           "  (text :spelling ((text 0 3)) \"abc\"))");
    CHECK_COVER(child_at(root, 0), AMARK_TREE_COVER_GAP, 2, NULL);
    CHECK_COVER(child_at(root, 1), AMARK_TREE_COVER_OVERLAP, 3, NULL);
    CHECK_COVER(child_at(root, 2), AMARK_TREE_COVER_GAP, 5, NULL);
    CHECK_COVER(child_at(root, 3), AMARK_TREE_COVER_NONE, 0, NULL);
    NAOS_CHECK_FALSE(verbatim(child_at(root, 0)));
    NAOS_CHECK_FALSE(verbatim(child_at(root, 1)));
    NAOS_CHECK_FALSE(verbatim(child_at(root, 2)));
    NAOS_CHECK_TRUE(verbatim(child_at(root, 3)));
    amark_document_destroy(document);
}

NAOS_TEST(cover_reports_a_foreign_token_in_content_order)
{
    amark_document_t *document = new_document("abcd\nxyz\n");
    amark_node_t *root = amark_document_root(document);
    amark_node_t *first;
    amark_node_t *second;
    amark_node_t *foreign;

    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(paragraph :lines (0 1) :content laid ((0 0 4))\n"
                           "  (text :spelling ((text 1 4)) \"bcd\"))\n"
                           "(paragraph :lines (1 1) :content laid ((1 0 3))\n"
                           "  (text :spelling ((text 0 1)) \"x\")\n"
                           "  (text :spelling ((text 1 3)) \"yz\"))");
    first = child_at(root, 0);
    second = child_at(root, 1);

    /* A foreign token that starts at 1, after the gap at 0: the gap comes
     * first. */
    foreign = child_at(second, 1);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(first, foreign));
    CHECK_COVER(first, AMARK_TREE_COVER_GAP, 0, NULL);
    /* A foreign token at 0, the position of the gap, comes first. */
    foreign = child_at(second, 0);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(first, foreign));
    CHECK_COVER(first, AMARK_TREE_COVER_FOREIGN, 0, foreign);
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(cover_reuses_and_grows_its_scratch)
{
    amark_document_t *document = new_document("ab\nabcdefgh\n");
    amark_node_t *root = amark_document_root(document);
    amark_tree_cover_scratch_t scratch = {NULL, 0};
    amark_tree_cover_t out;
    int32_t *counts;

    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(paragraph :lines (0 1) :content laid ((0 0 2))\n"
                           "  (text :spelling ((text 0 2)) \"ab\"))\n"
                           "(paragraph :lines (1 1) :content laid ((1 0 8))\n"
                           "  (text :spelling ((text 0 8)) \"abcdefgh\"))");
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_tree_content_cover(child_at(root, 1), &scratch, &out));
    NAOS_CHECK_TRUE(out.covered);
    NAOS_CHECK_TRUE(scratch.capacity >= 9);
    counts = scratch.counts;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_tree_content_cover(child_at(root, 0), &scratch, &out));
    NAOS_CHECK_TRUE(out.covered);
    NAOS_CHECK_PTR_EQ(counts, scratch.counts); /* no new allocation for a shorter one */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_tree_content_cover(child_at(root, 1), &scratch, &out));
    NAOS_CHECK_TRUE(out.covered);
    amark_tree_cover_scratch_free(&scratch);
    NAOS_CHECK_PTR_EQ(NULL, scratch.counts);
    NAOS_CHECK_UINT_EQ(0, scratch.capacity);
    amark_tree_cover_scratch_free(NULL);
    amark_document_destroy(document);
}

/* ---- Subtree verbatim (11.4, 11.5, 7.8, 14.9) ------------------------------------- */

/* The result of amark_node_verbatim, which must succeed. */
static bool subtree_verbatim(const amark_node_t *node)
{
    bool out = true;

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_verbatim(node, &out));
    return out;
}

/* The node at path (child indexes from the document node, ended by -1). */
static amark_node_t *node_at(amark_document_t *document, const int *path)
{
    amark_node_t *node = amark_document_root(document);

    while (node != NULL && *path >= 0)
    {
        node = child_at(node, (size_t)*path);
        path++;
    }
    return node;
}

/* INPUT.md 3.4 in a list: L0 "> - a", L1 "b" (a lazy continuation: both the
 * block quote and the item leave out their margins at column 0), L2 ">" (a
 * blank line between the items, owned by the list), L3 "> - c". */
static const char quoted_list_source[] = "> - a\nb\n>\n> - c\n";

static const char quoted_list_tree[] =
    "(document :lines (0 4)\n"
    "  (block_quote :lines (0 4)\n"
    "    :margins ((0 0 2 present) (1 0 0 lazy) (2 0 1 present) (3 0 2 present))\n"
    "    (list :type bullet :tight false :lines (0 4) :blanks ((2 1 1))\n"
    "      (item :lines (0 2) :margins ((0 2 4 present first) (1 0 0 lazy))\n"
    "        (paragraph :lines (0 2) :content laid ((0 4 5) (1 0 1))\n"
    "          (text :spelling ((text 0 1 :facet literal)) \"a\")\n"
    "          (softbreak :spelling ((line_break 1 2)))\n"
    "          (text :spelling ((text 2 3 :facet literal)) \"b\")))\n"
    "      (item :lines (3 1) :margins ((3 2 4 present first))\n"
    "        (paragraph :lines (3 1) :content laid ((3 4 5))\n"
    "          (text :spelling ((text 0 1 :facet literal)) \"c\"))))))";

static const int quote_path[] = {0, -1};
static const int list_in_quote_path[] = {0, 0, -1};
static const int first_item_in_quote_path[] = {0, 0, 0, -1};
static const int second_item_in_quote_path[] = {0, 0, 1, -1};

static amark_document_t *quoted_list(void)
{
    amark_document_t *document = new_document(quoted_list_source);

    AMARK_CHECK_TREE_BUILD(document, NULL, quoted_list_tree);
    CHECK_TREE_OK(document);
    return document;
}

NAOS_TEST(an_unedited_list_in_a_block_quote_with_a_lazy_line_is_verbatim)
{
    amark_document_t *document = quoted_list();
    amark_node_t *root = amark_document_root(document);
    amark_node_t *node;

    NAOS_CHECK_TRUE(subtree_verbatim(root));
    /* Every node of the tree, blocks and inlines. */
    for (node = root; node != NULL;)
    {
        NAOS_CHECK_TRUE(subtree_verbatim(node));
        if (amark_node_first_child(node) != NULL)
        {
            node = amark_node_first_child(node);
            continue;
        }
        while (node != NULL && amark_node_next(node) == NULL)
        {
            node = amark_node_parent(node);
        }
        if (node != NULL)
        {
            node = amark_node_next(node);
        }
    }
    /* The cache is consistent with the tree (the internal checker). */
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(subtree_verbatim_is_false_without_a_margin_on_each_line)
{
    amark_document_t *document = quoted_list();
    amark_node_t *root = amark_document_root(document);
    amark_node_t *quote = node_at(document, quote_path);
    amark_node_t *item = node_at(document, first_item_in_quote_path);

    NAOS_CHECK_TRUE(subtree_verbatim(root));
    /* Dropping the lazy margin of the item: the item, and its ancestors, lose
     * the margin of line 1; the other item is not affected. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_discard_margin(item, 1));
    NAOS_CHECK_FALSE(subtree_verbatim(item));
    NAOS_CHECK_FALSE(subtree_verbatim(node_at(document, list_in_quote_path)));
    NAOS_CHECK_FALSE(subtree_verbatim(quote));
    NAOS_CHECK_FALSE(subtree_verbatim(root));
    NAOS_CHECK_TRUE(subtree_verbatim(node_at(document, second_item_in_quote_path)));
    NAOS_CHECK_TRUE(subtree_verbatim(amark_node_first_child(item)));
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(an_edit_invalidates_the_cached_results_of_the_ancestors)
{
    amark_document_t *document = quoted_list();
    amark_node_t *root = amark_document_root(document);
    amark_node_t *item = node_at(document, second_item_in_quote_path);
    amark_node_t *text = amark_node_first_child(amark_node_first_child(item));

    NAOS_CHECK_TRUE(subtree_verbatim(root));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_string(text, AMARK_TEXT_LITERAL, "C", 1));
    NAOS_CHECK_FALSE(subtree_verbatim(text));
    NAOS_CHECK_FALSE(subtree_verbatim(item));
    NAOS_CHECK_FALSE(subtree_verbatim(root));
    /* A style of a block (its own spelling is dropped). */
    amark_document_destroy(document);

    document = quoted_list();
    root = amark_document_root(document);
    NAOS_CHECK_TRUE(subtree_verbatim(root));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_set_int(node_at(document, list_in_quote_path), AMARK_LIST_BULLET,
                                         AMARK_LIST_BULLET_PLUS));
    NAOS_CHECK_FALSE(subtree_verbatim(root));
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

/* L0 "> a", L1 "", L2 "b", L3 "***": a paragraph moved into the block quote has
 * no margin of the block quote on its line (11.5). */
static amark_document_t *quote_and_paragraph(void)
{
    amark_document_t *document = new_document("> a\n\nb\n***\n");

    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(document :lines (0 4) :blanks ((1 0 0))\n"
                           "  (block_quote :lines (0 1) :margins ((0 0 2 present))\n"
                           "    (paragraph :lines (0 1) :content laid ((0 2 3))\n"
                           "      (text :spelling ((text 0 1 :facet literal)) \"a\")))\n"
                           "  (paragraph :lines (2 1) :content laid ((2 0 1))\n"
                           "    (text :spelling ((text 0 1 :facet literal)) \"b\"))\n"
                           "  (thematic_break :lines (3 1) :tokens ((marker 3 0 3))))");
    CHECK_TREE_OK(document);
    return document;
}

NAOS_TEST(a_moved_block_lacks_the_margins_of_its_new_ancestors)
{
    amark_document_t *document = quote_and_paragraph();
    amark_node_t *root = amark_document_root(document);
    amark_node_t *quote = child_at(root, 0);
    amark_node_t *paragraph = child_at(root, 1);
    amark_margin_t margin;
    bool found = true;

    NAOS_CHECK_TRUE(subtree_verbatim(root));
    NAOS_CHECK_TRUE(subtree_verbatim(child_at(root, 2))); /* a leaf without content */

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(quote, paragraph));
    /* The moved block keeps its own spelling... */
    NAOS_CHECK_TRUE(subtree_verbatim(paragraph));
    /* ...but its line has no margin of the block quote (11.5). */
    memset(&margin, 0, sizeof(margin));
    margin.struct_size = sizeof(margin);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_find_margin(quote, 2, &margin, &found));
    NAOS_CHECK_FALSE(found);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_find_margin(quote, 0, &margin, &found));
    NAOS_CHECK_TRUE(found);
    /* The ancestors cannot be output in their original spelling. */
    NAOS_CHECK_FALSE(subtree_verbatim(quote));
    NAOS_CHECK_FALSE(subtree_verbatim(root));
    CHECK_TREE_OK(document);

    /* Moving it back restores the original spelling. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_after(quote, paragraph));
    NAOS_CHECK_TRUE(subtree_verbatim(quote));
    NAOS_CHECK_TRUE(subtree_verbatim(root));

    /* Blocks out of the order of their lines are not verbatim either. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_before(quote, paragraph));
    NAOS_CHECK_FALSE(subtree_verbatim(root));
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(an_inline_subtree_is_judged_on_the_content_of_its_leaf)
{
    amark_document_t *document = two_paragraphs();
    amark_node_t *root = amark_document_root(document);
    amark_node_t *first = child_at(root, 0);
    amark_node_t *second = child_at(root, 1);
    amark_node_t *emph = child_at(first, 1);
    amark_node_t *text = NULL;

    NAOS_CHECK_TRUE(subtree_verbatim(emph));
    NAOS_CHECK_TRUE(subtree_verbatim(root));
    /* Moved to another paragraph, its tokens are on the content of the first. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(second, emph));
    NAOS_CHECK_FALSE(subtree_verbatim(emph));
    NAOS_CHECK_FALSE(subtree_verbatim(second));
    NAOS_CHECK_FALSE(subtree_verbatim(first));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_after(child_at(first, 0), emph));
    NAOS_CHECK_TRUE(subtree_verbatim(root));
    /* A new node under it. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, AMARK_NODE_TEXT, &text));
    NAOS_CHECK_FALSE(subtree_verbatim(text));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(emph, text));
    NAOS_CHECK_FALSE(subtree_verbatim(emph));
    NAOS_CHECK_FALSE(subtree_verbatim(root));
    amark_node_destroy(text);
    NAOS_CHECK_TRUE(subtree_verbatim(root));
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(blocks_without_a_range_or_with_dropped_spelling_are_not_verbatim)
{
    amark_document_t *document = new_document("```\n```\n");
    amark_node_t *root = amark_document_root(document);
    amark_node_t *code;
    amark_node_t *paragraph = NULL;
    bool out = true;

    /* A fenced code block without content lines: the leaf has no content. */
    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(document :lines (0 2)\n"
                           "  (code_block :info \"\" :style (:form fenced :fence_char \"`\"\n"
                           "                               :fence_length 3)\n"
                           "    :lines (0 2) :tokens ((marker 0 0 3) (marker 1 0 3))))");
    code = child_at(root, 0);
    NAOS_CHECK_TRUE(subtree_verbatim(code));
    NAOS_CHECK_TRUE(subtree_verbatim(root));
    /* Its literal set: the content is free. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_set_string(code, AMARK_CODE_BLOCK_LITERAL, "x\n", 2));
    NAOS_CHECK_FALSE(subtree_verbatim(code));
    NAOS_CHECK_FALSE(subtree_verbatim(root));

    /* A new block. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(document, AMARK_NODE_PARAGRAPH, &paragraph));
    NAOS_CHECK_FALSE(subtree_verbatim(paragraph));
    amark_node_destroy(paragraph);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_verbatim(NULL, &out));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_verbatim(root, NULL));
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(the_checker_finds_inconsistent_verbatim_results)
{
    amark_document_t *document = quoted_list();
    amark_node_t *root = amark_document_root(document);
    amark_node_t *quote = node_at(document, quote_path);
    amark_node_t *list = node_at(document, list_in_quote_path);
    amark_node_t *text = amark_node_first_child(
        amark_node_first_child(node_at(document, first_item_in_quote_path)));
    amark_tree_check_report_t report;
    uint32_t saved_flags;
    uint32_t saved_column;

    NAOS_CHECK_TRUE(subtree_verbatim(root));
    /* The list remembers its start columns; the block quote, a container, does
     * not; no line ends early. */
    NAOS_CHECK_UINT_EQ(AMARK_TREE_CACHE_VALID | AMARK_TREE_CACHE_VERBATIM |
                           AMARK_TREE_CACHE_FULL_LINES | AMARK_TREE_CACHE_STARTS,
                       list->cache_flags);
    NAOS_CHECK_UINT_EQ(2, list->verbatim_columns[0]); /* the item margin */
    NAOS_CHECK_UINT_EQ(0, list->verbatim_columns[1]); /* the lazy line */
    NAOS_CHECK_UINT_EQ(1, list->verbatim_columns[2]); /* the blank line */
    NAOS_CHECK_UINT_EQ(2, list->verbatim_columns[3]);
    NAOS_CHECK_UINT_EQ(AMARK_TREE_CACHE_VALID | AMARK_TREE_CACHE_VERBATIM |
                           AMARK_TREE_CACHE_FULL_LINES,
                       quote->cache_flags);
    NAOS_CHECK_PTR_EQ(NULL, quote->verbatim_columns);
    CHECK_TREE_OK(document);

    /* Line flags on an inline node. */
    saved_flags = text->cache_flags;
    text->cache_flags |= AMARK_TREE_CACHE_FULL_LINES;
    memset(&report, 0, sizeof(report));
    NAOS_CHECK_STR_EQ("cache", amark_tree_violation_name(amark_tree_check(document, &report)));
    NAOS_CHECK_PTR_EQ(text, report.node);
    text->cache_flags = saved_flags;

    /* A pass-through block without its start columns. */
    saved_flags = list->cache_flags;
    list->cache_flags &= ~AMARK_TREE_CACHE_STARTS;
    NAOS_CHECK_STR_EQ("cache", amark_tree_violation_name(amark_tree_check(document, &report)));
    NAOS_CHECK_PTR_EQ(list, report.node);
    list->cache_flags = saved_flags;

    /* A remembered column beyond the width of its line. */
    saved_column = list->verbatim_columns[2];
    list->verbatim_columns[2] = 7;
    NAOS_CHECK_STR_EQ("cache", amark_tree_violation_name(amark_tree_check(document, &report)));
    list->verbatim_columns[2] = saved_column;

    /* A remembered leaf that is not the leaf of the inline node. */
    NAOS_CHECK_TRUE(subtree_verbatim(text));
    NAOS_CHECK_PTR_EQ(amark_node_parent(text), text->leaf_memo);
    CHECK_TREE_OK(document);
    text->leaf_memo = quote;
    NAOS_CHECK_STR_EQ("cache", amark_tree_violation_name(amark_tree_check(document, &report)));
    NAOS_CHECK_PTR_EQ(text, report.node);
    text->leaf_memo = amark_node_parent(text);

    /* An unknown flag. */
    saved_flags = quote->cache_flags;
    quote->cache_flags |= 0x80000000u;
    NAOS_CHECK_STR_EQ("cache", amark_tree_violation_name(amark_tree_check(document, &report)));
    quote->cache_flags = saved_flags;
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

/* ---- Sharing a line (7.8) ---------------------------------------------------------- */

/* A row whose cells share its line (SHARE_LINES), a strip whose children may
 * not, and a cell: a leaf with inline content. */
static const amark_node_type_desc_t row_type = {
    sizeof(amark_node_type_desc_t), "test.row", AMARK_NODE_LEVEL_BLOCK, AMARK_CONTENT_BLOCK,
    AMARK_CONTENT_BLOCK, AMARK_NODE_TYPE_SHARE_LINES, NULL, 0, 0,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL};
static const amark_node_type_desc_t strip_type = {
    sizeof(amark_node_type_desc_t), "test.strip", AMARK_NODE_LEVEL_BLOCK, AMARK_CONTENT_BLOCK,
    AMARK_CONTENT_BLOCK, 0, NULL, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
static const amark_node_type_desc_t cell_type = {
    sizeof(amark_node_type_desc_t), "test.cell", AMARK_NODE_LEVEL_BLOCK, AMARK_CONTENT_BLOCK,
    AMARK_CONTENT_INLINE, AMARK_NODE_TYPE_CONTENT, NULL, 0, 0,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL};
static const amark_node_type_desc_t *const share_types[] = {&row_type, &strip_type, &cell_type};
static const amark_extension_t share_extension = {
    sizeof(amark_extension_t), "test.share", share_types, 3, NULL, 0, NULL};

NAOS_TEST(only_children_of_a_share_lines_type_share_a_line)
{
    amark_syntax_set_t *set = NULL;
    amark_document_t *document = NULL;
    amark_source_id_t source = 0;
    amark_node_t *root;
    amark_node_t *row;
    amark_node_t *strip;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_create(&set));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &share_extension));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_freeze(set));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(set, &document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_add_source(document, "|a|b|\n|a|b|\n", 12,
                                                  AMARK_SOURCE_FINISHED, &source));
    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(document :lines (0 2)\n"
                           "  (test.row :lines (0 1)\n"
                           "    :tokens ((marker 0 0 1) (marker 0 2 3) (marker 0 4 5))\n"
                           "    (test.cell :lines (0 1) :content laid ((0 1 2))\n"
                           "      (text :spelling ((text 0 1 :facet literal)) \"a\"))\n"
                           "    (test.cell :lines (0 1) :content laid ((0 3 4))\n"
                           "      (text :spelling ((text 0 1 :facet literal)) \"b\")))\n"
                           "  (test.strip :lines (1 1)\n"
                           "    :tokens ((marker 1 0 1) (marker 1 2 3) (marker 1 4 5))\n"
                           "    (test.cell :lines (1 1) :content laid ((1 1 2))\n"
                           "      (text :spelling ((text 0 1 :facet literal)) \"a\"))\n"
                           "    (test.cell :lines (1 1) :content laid ((1 3 4))\n"
                           "      (text :spelling ((text 0 1 :facet literal)) \"b\"))))");
    CHECK_TREE_OK(document);
    root = amark_document_root(document);
    row = child_at(root, 0);
    strip = child_at(root, 1);

    NAOS_CHECK_TRUE(subtree_verbatim(row));
    NAOS_CHECK_FALSE(subtree_verbatim(strip));
    NAOS_CHECK_FALSE(subtree_verbatim(root));
    /* A cell alone covers only a part of its line. */
    NAOS_CHECK_FALSE(subtree_verbatim(child_at(row, 0)));
    NAOS_CHECK_FALSE(subtree_verbatim(child_at(row, 1)));
    /* The cells in the reverse order of their columns. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_before(child_at(row, 0),
                                                                 child_at(row, 1)));
    NAOS_CHECK_FALSE(subtree_verbatim(row));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_before(child_at(row, 0),
                                                                 child_at(row, 1)));
    NAOS_CHECK_TRUE(subtree_verbatim(row));
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
    amark_syntax_set_destroy(set);
}

/* ---- The cost of judging every node (14.9) ----------------------------------------- */

/* A block quote holding a list of n items, one per line "> - a" (with a lazy
 * line "b" after each item), the paragraph of each with its text; built from a
 * generated S-expression. */
static amark_document_t *long_quoted_list(size_t n)
{
    size_t source_size = n * 8 + 1;
    size_t tree_size = 512 + n * 512;
    char *source = (char *)malloc(source_size);
    char *tree = (char *)malloc(tree_size);
    amark_document_t *document = NULL;
    size_t used = 0;
    size_t i;

    NAOS_REQUIRE_TRUE(source != NULL && tree != NULL);
    for (i = 0; i < n; i++)
    {
        memcpy(source + i * 8, "> - a\nb\n", 8);
    }
    source[n * 8] = '\0';
    document = new_document(source);
    used += (size_t)snprintf(tree + used, tree_size - used,
                             "(document :lines (0 %u)\n"
                             "  (block_quote :lines (0 %u) :margins (",
                             (unsigned)(2 * n), (unsigned)(2 * n));
    for (i = 0; i < n; i++)
    {
        used += (size_t)snprintf(tree + used, tree_size - used,
                                 "(%u 0 2 present) (%u 0 0 lazy) ", (unsigned)(2 * i),
                                 (unsigned)(2 * i + 1));
    }
    used += (size_t)snprintf(tree + used, tree_size - used,
                             ")\n    (list :type bullet :tight true :lines (0 %u)\n",
                             (unsigned)(2 * n));
    for (i = 0; i < n; i++)
    {
        unsigned line = (unsigned)(2 * i);

        used += (size_t)snprintf(
            tree + used, tree_size - used,
            "(item :lines (%u 2) :margins ((%u 2 4 present first) (%u 0 0 lazy))\n"
            "  (paragraph :lines (%u 2) :content laid ((%u 4 5) (%u 0 1))\n"
            "    (text :spelling ((text 0 1 :facet literal)) \"a\")\n"
            "    (softbreak :spelling ((line_break 1 2)))\n"
            "    (text :spelling ((text 2 3 :facet literal)) \"b\")))\n",
            line, line, line + 1, line, line, line + 1);
    }
    used += (size_t)snprintf(tree + used, tree_size - used, ")))");
    NAOS_REQUIRE_TRUE(used < tree_size);
    AMARK_CHECK_TREE_BUILD(document, NULL, tree);
    free(tree);
    free(source);
    return document;
}

/* The steps of judging every node of document, in preorder (each must be
 * true). */
static size_t judge_every_node(amark_document_t *document)
{
    size_t before = document->stats.verbatim_steps;
    amark_node_t *node = amark_document_root(document);
    bool all = true;

    while (node != NULL)
    {
        bool out = false;

        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_verbatim(node, &out));
        all = all && out;
        if (amark_node_first_child(node) != NULL)
        {
            node = amark_node_first_child(node);
            continue;
        }
        while (node != NULL && amark_node_next(node) == NULL)
        {
            node = amark_node_parent(node);
        }
        if (node != NULL)
        {
            node = amark_node_next(node);
        }
    }
    NAOS_CHECK_TRUE(all);
    return document->stats.verbatim_steps - before;
}

NAOS_TEST(judging_every_node_is_linear)
{
    amark_document_t *small = long_quoted_list(200);
    amark_document_t *large = long_quoted_list(1600);
    size_t small_steps;
    size_t large_steps;
    size_t again;

    small_steps = judge_every_node(small);
    large_steps = judge_every_node(large);
    NAOS_CHECK_UINT_GT(small_steps, 0);
    /* Eight times the lines, the nodes and the records: at most eight times the
     * steps (plus a constant). */
    NAOS_CHECK_UINT_LE(large_steps, 8 * small_steps + 64);
    /* Bounded by the nodes and the records (6 nodes and 7 records per item). */
    NAOS_CHECK_UINT_LE(large_steps, 16 * (13 * 1600));
    /* Once cached, judging every node again costs at most two steps per node
     * (the visit, and for an inline node its remembered leaf). */
    again = judge_every_node(large);
    NAOS_CHECK_UINT_LE(again, 2 * large->node_count);
    /* Judging from the leaves up, as from the root down. */
    CHECK_TREE_OK(large);
    amark_document_destroy(small);
    amark_document_destroy(large);
}

/* Adds the content token [start, end) of kind to node, on the content of
 * leaf. */
static amark_error_t add_token(amark_node_t *node, const amark_node_t *leaf,
                               amark_spell_kind_t kind, size_t start, size_t end)
{
    amark_content_token_t token;

    memset(&token, 0, sizeof(token));
    token.struct_size = sizeof(token);
    token.start = start;
    token.end = end;
    token.kind = kind;
    return amark_node_add_content_token(node, leaf, &token);
}

/* One paragraph "*...*a*...*": depth emphasis nodes nested in each other around
 * a text "a", each spelled by its two delimiters. */
static amark_document_t *deep_emphasis(size_t depth)
{
    size_t length = 2 * depth + 1;
    char *source = (char *)malloc(length + 2);
    amark_document_t *document;
    amark_node_t *root;
    amark_node_t *paragraph = NULL;
    amark_node_t *parent;
    amark_node_t *text = NULL;
    size_t i;

    NAOS_REQUIRE_NOT_NULL(source);
    memset(source, '*', length);
    source[depth] = 'a';
    source[length] = '\n';
    source[length + 1] = '\0';
    document = new_document(source);
    free(source);
    root = amark_document_root(document);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(root, 0, 1));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(document, AMARK_NODE_PARAGRAPH, &paragraph));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(root, paragraph));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(paragraph, 0, 1));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_block_add_content(paragraph, 0, 0, (uint32_t)length));
    parent = paragraph;
    for (i = 0; i < depth; i++)
    {
        amark_node_t *emph = NULL;

        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, AMARK_NODE_EMPH, &emph));
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(parent, emph));
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                            add_token(emph, paragraph, AMARK_SPELL_DELIMITER, i, i + 1));
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, add_token(emph, paragraph, AMARK_SPELL_DELIMITER,
                                                        length - 1 - i, length - i));
        parent = emph;
    }
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, AMARK_NODE_TEXT, &text));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(parent, text));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        add_token(text, paragraph, AMARK_SPELL_TEXT, depth, depth + 1));
    return document;
}

NAOS_TEST(judging_every_node_of_deep_emphasis_is_linear)
{
    amark_document_t *small = deep_emphasis(500);
    amark_document_t *large = deep_emphasis(4000);
    size_t small_steps;
    size_t large_steps;
    size_t again;

    /* Each inline node is judged as the root of its subtree, on the content of
     * its leaf: finding the leaf is not a walk to it from each node. */
    small_steps = judge_every_node(small);
    large_steps = judge_every_node(large);
    NAOS_CHECK_UINT_GT(small_steps, 0);
    NAOS_CHECK_UINT_LE(large_steps, 8 * small_steps + 64);
    NAOS_CHECK_UINT_LE(large_steps, 16 * large->node_count);
    again = judge_every_node(large);
    NAOS_CHECK_UINT_LE(again, 2 * large->node_count);
    CHECK_TREE_OK(large);
    /* A structural change elsewhere drops the memory of the leaves: judging
     * every node again stays linear. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_append_child(amark_document_root(large),
                                              amark_node_first_child(
                                                  amark_document_root(large))));
    again = judge_every_node(large);
    NAOS_CHECK_UINT_LE(again, 6 * large->node_count);
    CHECK_TREE_OK(large);
    amark_document_destroy(small);
    amark_document_destroy(large);
}

/* ---- Out of memory -------------------------------------------------------------------- */

/* Builds the quoted list and judges it: the computation allocates the start
 * columns of its blocks. A failure of the judgment leaves the cache consistent,
 * and judging again succeeds. */
static amark_error_t judge_oom_scenario(void *context)
{
    amark_document_t *document = NULL;
    amark_source_id_t source = 0;
    amark_error_t error;
    amark_error_t failure = AMARK_ERROR_NONE;
    bool out = false;

    (void)context;
    error = amark_document_create(NULL, &document);
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    error = amark_document_add_source(document, quoted_list_source,
                                      sizeof(quoted_list_source) - 1, AMARK_SOURCE_FINISHED,
                                      &source);
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_test_tree_build(document, NULL, quoted_list_tree, NULL);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_verbatim(amark_document_root(document), &out);
        if (error == AMARK_ERROR_OUT_OF_MEMORY)
        {
            failure = error;
            if (amark_tree_check(document, NULL) != AMARK_TREE_VIOLATION_NONE)
            {
                error = AMARK_ERROR_INVALID_STATE;
            }
            else
            {
                /* The injected failure is the only one: this one succeeds. */
                error = amark_node_verbatim(amark_document_root(document), &out);
            }
        }
    }
    if ((error == AMARK_ERROR_NONE) &&
        (!out || (amark_tree_check(document, NULL) != AMARK_TREE_VIOLATION_NONE)))
    {
        error = AMARK_ERROR_INVALID_STATE;
    }
    if ((error == AMARK_ERROR_NONE) && (failure != AMARK_ERROR_NONE))
    {
        error = failure;
    }
    amark_document_destroy(document);
    return error;
}

static amark_test_allocator_t judge_allocator;

NAOS_TEST(out_of_memory_in_the_judgment_leaves_the_cache_consistent)
{
    amark_document_t *document = NULL;
    amark_source_id_t source = 0;
    amark_error_t built;
    amark_error_t judged = AMARK_ERROR_INVALID_STATE;
    size_t judgment_calls = 0;
    bool out = false;

    /* The judgment allocates (the remembered columns), so the sweep reaches it:
     * count its allocations with the test allocator set around everything. */
    amark_test_allocator_init(&judge_allocator);
    amark_allocator_set(amark_test_allocator_activate(&judge_allocator));
    built = amark_document_create(NULL, &document);
    if (built == AMARK_ERROR_NONE)
    {
        built = amark_document_add_source(document, quoted_list_source,
                                          sizeof(quoted_list_source) - 1,
                                          AMARK_SOURCE_FINISHED, &source);
    }
    if (built == AMARK_ERROR_NONE)
    {
        built = amark_test_tree_build(document, NULL, quoted_list_tree, NULL);
    }
    if (built == AMARK_ERROR_NONE)
    {
        amark_test_allocator_reset_counters(&judge_allocator);
        judged = amark_node_verbatim(amark_document_root(document), &out);
        judgment_calls = judge_allocator.calls;
    }
    amark_document_destroy(document);
    amark_allocator_set(NULL);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, built);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, judged);
    NAOS_CHECK_TRUE(out);
    NAOS_CHECK_UINT_GT(judgment_calls, 0);
    NAOS_CHECK_UINT_EQ(0, judge_allocator.live_blocks);

    AMARK_CHECK_OOM_SWEEP(judge_oom_scenario, NULL);
}

/* ---- Tightness (3.7, 6.11) -------------------------------------------------------- */

/* The tight value of list (amark_node_get), which must succeed and be PRESENT. */
static int64_t tight(const amark_node_t *list)
{
    amark_value_t value;

    memset(&value, 0xA5, sizeof(value));
    value.struct_size = sizeof(value);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_get(list, AMARK_LIST_TIGHT, &value));
    NAOS_CHECK_INT_EQ(AMARK_VALUE_PRESENT, value.state);
    return value.number;
}

/* A document of source built from tree (without :tight, so that the tight value
 * of each list is derived), checked by the internal checker. */
static amark_document_t *built(const char *source, const char *tree)
{
    amark_document_t *document = new_document(source);

    AMARK_CHECK_TREE_BUILD(document, NULL, tree);
    CHECK_TREE_OK(document);
    return document;
}

static const int list_path[] = {0, -1};
static const int first_item_path[] = {0, 0, -1};
static const int second_item_path[] = {0, 1, -1};

/* L0 "- a", L1 "- b": no blank line. */
static const char tight_list_source[] = "- a\n- b\n";
static const char tight_list_tree[] =
    "(document :lines (0 2)\n"
    "  (list :type bullet :lines (0 2)\n"
    "    (item :lines (0 1) :margins ((0 0 2 present first))\n"
    "      (paragraph :lines (0 1) :content laid ((0 2 3))\n"
    "        (text :spelling ((text 0 1 :facet literal)) \"a\")))\n"
    "    (item :lines (1 1) :margins ((1 0 2 present first))\n"
    "      (paragraph :lines (1 1) :content laid ((1 2 3))\n"
    "        (text :spelling ((text 0 1 :facet literal)) \"b\")))))";

/* L0 "- a", L1 "" (owned by the list), L2 "- b". */
static const char items_blank_source[] = "- a\n\n- b\n";
static const char items_blank_tree[] =
    "(document :lines (0 3)\n"
    "  (list :type bullet :lines (0 3) :blanks ((1 0 0))\n"
    "    (item :lines (0 1) :margins ((0 0 2 present first))\n"
    "      (paragraph :lines (0 1) :content laid ((0 2 3))\n"
    "        (text :spelling ((text 0 1 :facet literal)) \"a\")))\n"
    "    (item :lines (2 1) :margins ((2 0 2 present first))\n"
    "      (paragraph :lines (2 1) :content laid ((2 2 3))\n"
    "        (text :spelling ((text 0 1 :facet literal)) \"b\")))))";

/* L0 "- a", L1 "" (owned by the item), L2 "  b": two paragraphs of one item. */
static const char children_blank_source[] = "- a\n\n  b\n";
static const char children_blank_tree[] =
    "(document :lines (0 3)\n"
    "  (list :type bullet :lines (0 3)\n"
    "    (item :lines (0 3) :blanks ((1 0 0))\n"
    "      :margins ((0 0 2 present first) (1 0 0 present) (2 0 2 present))\n"
    "      (paragraph :lines (0 1) :content laid ((0 2 3))\n"
    "        (text :spelling ((text 0 1 :facet literal)) \"a\"))\n"
    "      (paragraph :lines (2 1) :content laid ((2 2 3))\n"
    "        (text :spelling ((text 0 1 :facet literal)) \"b\")))))";

NAOS_TEST(a_list_without_blank_lines_between_its_blocks_is_tight)
{
    amark_document_t *document = built(tight_list_source, tight_list_tree);

    NAOS_CHECK_INT_EQ(1, tight(node_at(document, list_path)));
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(a_blank_line_between_items_makes_the_list_loose)
{
    amark_document_t *document = built(items_blank_source, items_blank_tree);
    amark_node_t *list = node_at(document, list_path);

    NAOS_CHECK_INT_EQ(0, tight(list));
    /* Again from the cache. */
    NAOS_CHECK_INT_EQ(0, tight(list));
    CHECK_TREE_OK(document);
    /* Without the second item, the blank line ends the list: tight. */
    amark_node_destroy(node_at(document, second_item_path));
    NAOS_CHECK_INT_EQ(1, tight(list));
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(a_blank_line_between_the_children_of_an_item_makes_the_list_loose)
{
    amark_document_t *document = built(children_blank_source, children_blank_tree);
    amark_node_t *list = node_at(document, list_path);
    amark_node_t *item = node_at(document, first_item_path);

    NAOS_CHECK_INT_EQ(0, tight(list));
    CHECK_TREE_OK(document);
    /* Without the second paragraph, the blank line ends the item: tight. */
    amark_node_destroy(child_at(item, 1));
    NAOS_CHECK_INT_EQ(1, tight(list));
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(blank_lines_that_are_not_between_two_blocks_keep_the_list_tight)
{
    /* L0 "- a", L1 "", L2 "- b", L3 "": records where the first item holds the
     * blank line after its only child and the list the one after its last
     * item. "Between" is after the last line of a block and before the first
     * line of the next one: blank lines at the end do not count. */
    amark_document_t *document = built(
        "- a\n\n- b\n\n",
        "(document :lines (0 4)\n"
        "  (list :type bullet :lines (0 4) :blanks ((3 0 0))\n"
        "    (item :lines (0 2) :blanks ((1 0 0))\n"
        "      :margins ((0 0 2 present first) (1 0 0 present))\n"
        "      (paragraph :lines (0 1) :content laid ((0 2 3))\n"
        "        (text :spelling ((text 0 1 :facet literal)) \"a\")))\n"
        "    (item :lines (2 1) :margins ((2 0 2 present first))\n"
        "      (paragraph :lines (2 1) :content laid ((2 2 3))\n"
        "        (text :spelling ((text 0 1 :facet literal)) \"b\")))))");

    NAOS_CHECK_INT_EQ(1, tight(node_at(document, list_path)));
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(blank_lines_in_a_nested_list_or_a_block_quote_do_not_count)
{
    /* L0 "- a", L1 "  - b", L2 "" (owned by the nested list), L3 "  - c". */
    amark_document_t *document = built(
        "- a\n  - b\n\n  - c\n",
        "(document :lines (0 4)\n"
        "  (list :type bullet :lines (0 4)\n"
        "    (item :lines (0 4)\n"
        "      :margins ((0 0 2 present first) (1 0 2 present) (2 0 0 present)\n"
        "                (3 0 2 present))\n"
        "      (paragraph :lines (0 1) :content laid ((0 2 3))\n"
        "        (text :spelling ((text 0 1 :facet literal)) \"a\"))\n"
        "      (list :type bullet :lines (1 3) :blanks ((2 0 0))\n"
        "        (item :lines (1 1) :margins ((1 2 4 present first))\n"
        "          (paragraph :lines (1 1) :content laid ((1 4 5))\n"
        "            (text :spelling ((text 0 1 :facet literal)) \"b\")))\n"
        "        (item :lines (3 1) :margins ((3 2 4 present first))\n"
        "          (paragraph :lines (3 1) :content laid ((3 4 5))\n"
        "            (text :spelling ((text 0 1 :facet literal)) \"c\")))))))");
    amark_node_t *outer = node_at(document, list_path);
    amark_node_t *inner = child_at(node_at(document, first_item_path), 1);

    NAOS_CHECK_INT_EQ(1, tight(outer));
    NAOS_CHECK_INT_EQ(0, tight(inner));
    CHECK_TREE_OK(document);
    amark_document_destroy(document);

    /* L0 "- > a", L1 "  >" (owned by the block quote), L2 "  > b". */
    document = built(
        "- > a\n  >\n  > b\n",
        "(document :lines (0 3)\n"
        "  (list :type bullet :lines (0 3)\n"
        "    (item :lines (0 3)\n"
        "      :margins ((0 0 2 present first) (1 0 2 present) (2 0 2 present))\n"
        "      (block_quote :lines (0 3) :blanks ((1 3 3))\n"
        "        :margins ((0 2 4 present) (1 2 3 present) (2 2 4 present))\n"
        "        (paragraph :lines (0 1) :content laid ((0 4 5))\n"
        "          (text :spelling ((text 0 1 :facet literal)) \"a\"))\n"
        "        (paragraph :lines (2 1) :content laid ((2 4 5))\n"
        "          (text :spelling ((text 0 1 :facet literal)) \"b\"))))))");
    NAOS_CHECK_INT_EQ(1, tight(node_at(document, list_path)));
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(the_tight_override_wins_until_it_is_unset)
{
    amark_document_t *document = built(items_blank_source, items_blank_tree);
    amark_node_t *list = node_at(document, list_path);

    NAOS_CHECK_INT_EQ(0, tight(list));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_int(list, AMARK_LIST_TIGHT, 1));
    NAOS_CHECK_INT_EQ(1, tight(list));
    CHECK_TREE_OK(document);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_unset(list, AMARK_LIST_TIGHT));
    NAOS_CHECK_INT_EQ(0, tight(list));
    /* Overriding with the derived value still holds an override (6.11): it
     * stays when the blank line goes. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_int(list, AMARK_LIST_TIGHT, 0));
    amark_node_destroy(node_at(document, second_item_path));
    NAOS_CHECK_INT_EQ(0, tight(list));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_unset(list, AMARK_LIST_TIGHT));
    NAOS_CHECK_INT_EQ(1, tight(list));
    CHECK_TREE_OK(document);
    amark_document_destroy(document);

    /* A tight list overridden as loose. */
    document = built(tight_list_source, tight_list_tree);
    list = node_at(document, list_path);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_int(list, AMARK_LIST_TIGHT, 0));
    NAOS_CHECK_INT_EQ(0, tight(list));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_unset(list, AMARK_LIST_TIGHT));
    NAOS_CHECK_INT_EQ(1, tight(list));
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(changes_of_the_items_and_their_children_invalidate_the_tightness)
{
    amark_document_t *document = built(children_blank_source, children_blank_tree);
    amark_node_t *root = amark_document_root(document);
    amark_node_t *list = node_at(document, list_path);
    amark_node_t *item = node_at(document, first_item_path);
    amark_node_t *text = amark_node_first_child(amark_node_first_child(item));
    amark_node_t *paragraph = NULL;

    /* Cached as loose, with the verbatim results of the whole tree. */
    NAOS_CHECK_TRUE(subtree_verbatim(root));
    NAOS_CHECK_INT_EQ(0, tight(list));
    /* A change under a child of an item leaves the tightness as it is, but
     * invalidates the verbatim results of the item and of the list. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_string(text, AMARK_TEXT_LITERAL, "A", 1));
    NAOS_CHECK_INT_EQ(0, tight(list));
    CHECK_TREE_OK(document);
    /* Removing the second paragraph changes the children of the item, whose
     * verbatim result is already invalid: the tightness is derived again. */
    amark_node_destroy(child_at(item, 1));
    NAOS_CHECK_INT_EQ(1, tight(list));
    CHECK_TREE_OK(document);
    /* A new paragraph without a line range is not after the blank line. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(document, AMARK_NODE_PARAGRAPH, &paragraph));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(item, paragraph));
    NAOS_CHECK_INT_EQ(1, tight(list));
    /* Its line range, a record of the grandchild of the list, puts it there. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(paragraph, 2, 1));
    NAOS_CHECK_INT_EQ(0, tight(list));
    CHECK_TREE_OK(document);
    /* Removing the blank line record (dropping the spelling of the item, which
     * gives the list its current value as an override), then unsetting the
     * override: the list is tight again. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_discard_spelling(item));
    NAOS_CHECK_INT_EQ(0, tight(list));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_unset(list, AMARK_LIST_TIGHT));
    NAOS_CHECK_INT_EQ(1, tight(list));
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(the_tightness_of_a_list_in_a_block_quote_is_cached_and_checked)
{
    amark_document_t *document = quoted_list();
    amark_node_t *quote = node_at(document, quote_path);
    amark_node_t *list = node_at(document, list_in_quote_path);
    amark_tree_check_report_t report;
    uint32_t saved_flags;

    /* The recorded value (loose) is an override; the derived value agrees:
     * the list holds the blank line ">" between its items. */
    NAOS_CHECK_INT_EQ(0, tight(list));
    NAOS_CHECK_UINT_EQ(0, list->cache_flags & AMARK_TREE_CACHE_TIGHT);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_unset(list, AMARK_LIST_TIGHT));
    NAOS_CHECK_INT_EQ(0, tight(list));
    NAOS_CHECK_UINT_EQ(AMARK_TREE_CACHE_TIGHT | AMARK_TREE_CACHE_LOOSE,
                       list->cache_flags & (AMARK_TREE_CACHE_TIGHT | AMARK_TREE_CACHE_LOOSE));
    /* The verbatim judgment keeps the cached tightness. */
    NAOS_CHECK_TRUE(subtree_verbatim(amark_document_root(document)));
    NAOS_CHECK_UINT_EQ(AMARK_TREE_CACHE_TIGHT | AMARK_TREE_CACHE_LOOSE,
                       list->cache_flags & (AMARK_TREE_CACHE_TIGHT | AMARK_TREE_CACHE_LOOSE));
    CHECK_TREE_OK(document);

    /* A cached tightness that differs from the blank lines. */
    saved_flags = list->cache_flags;
    list->cache_flags &= ~AMARK_TREE_CACHE_LOOSE;
    memset(&report, 0, sizeof(report));
    NAOS_CHECK_STR_EQ("cache", amark_tree_violation_name(amark_tree_check(document, &report)));
    NAOS_CHECK_PTR_EQ(list, report.node);
    /* A loose mark without the cache. */
    list->cache_flags = saved_flags & ~AMARK_TREE_CACHE_TIGHT;
    NAOS_CHECK_STR_EQ("cache", amark_tree_violation_name(amark_tree_check(document, &report)));
    NAOS_CHECK_PTR_EQ(list, report.node);
    list->cache_flags = saved_flags;
    /* A cached tightness on a block quote. */
    saved_flags = quote->cache_flags;
    quote->cache_flags |= AMARK_TREE_CACHE_TIGHT;
    NAOS_CHECK_STR_EQ("cache", amark_tree_violation_name(amark_tree_check(document, &report)));
    NAOS_CHECK_PTR_EQ(quote, report.node);
    quote->cache_flags = saved_flags;
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

/* The steps of deriving the tightness of the list of long_quoted_list(n). */
static size_t derive_tightness(amark_document_t *document)
{
    amark_node_t *list = node_at(document, list_in_quote_path);
    size_t before = document->stats.tight_steps;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_unset(list, AMARK_LIST_TIGHT));
    NAOS_CHECK_INT_EQ(1, tight(list));
    return document->stats.tight_steps - before;
}

NAOS_TEST(deriving_the_tightness_is_linear)
{
    amark_document_t *small = long_quoted_list(200);
    amark_document_t *large = long_quoted_list(1600);
    amark_node_t *list = node_at(large, list_in_quote_path);
    size_t small_steps = derive_tightness(small);
    size_t large_steps = derive_tightness(large);
    size_t before;

    NAOS_CHECK_UINT_GT(small_steps, 0);
    NAOS_CHECK_UINT_LE(large_steps, 8 * small_steps + 16);
    /* Bounded by the children of the list and of its items. */
    NAOS_CHECK_UINT_LE(large_steps, 4 * (2 * 1600) + 16);
    /* Cached: no step. */
    before = large->stats.tight_steps;
    NAOS_CHECK_INT_EQ(1, tight(list));
    NAOS_CHECK_UINT_EQ(before, large->stats.tight_steps);
    CHECK_TREE_OK(large);
    amark_document_destroy(small);
    amark_document_destroy(large);
}

NAOS_SUITE(verbatim,
    NAOS_ADD_TEST(spelling_state_of_parsed_nodes_is_complete),
    NAOS_ADD_TEST(spelling_state_tells_the_dropped_facets_and_styles),
    NAOS_ADD_TEST(new_nodes_have_no_range_no_record_no_token_and_no_structure_mark),
    NAOS_ADD_TEST(spelling_state_rejects_bad_arguments),
    NAOS_ADD_TEST(unedited_content_is_verbatim),
    NAOS_ADD_TEST(reordering_inline_nodes_is_not_verbatim),
    NAOS_ADD_TEST(moving_a_node_from_another_paragraph_is_not_verbatim),
    NAOS_ADD_TEST(changing_a_text_value_is_not_verbatim),
    NAOS_ADD_TEST(changing_a_style_is_not_verbatim),
    NAOS_ADD_TEST(inserting_a_new_node_is_not_verbatim),
    NAOS_ADD_TEST(content_that_is_not_laid_is_not_verbatim),
    NAOS_ADD_TEST(a_dropped_spelling_under_the_leaf_is_not_verbatim),
    NAOS_ADD_TEST(a_leftover_delimiter_text_is_verbatim),
    NAOS_ADD_TEST(an_autolink_made_inline_is_not_verbatim),
    NAOS_ADD_TEST(code_block_content_with_its_own_tokens_is_verbatim),
    NAOS_ADD_TEST(broken_nesting_is_not_verbatim),
    NAOS_ADD_TEST(cover_reports_the_first_gap_or_overlap),
    NAOS_ADD_TEST(cover_reports_a_foreign_token_in_content_order),
    NAOS_ADD_TEST(cover_reuses_and_grows_its_scratch),
    NAOS_ADD_TEST(an_unedited_list_in_a_block_quote_with_a_lazy_line_is_verbatim),
    NAOS_ADD_TEST(subtree_verbatim_is_false_without_a_margin_on_each_line),
    NAOS_ADD_TEST(an_edit_invalidates_the_cached_results_of_the_ancestors),
    NAOS_ADD_TEST(a_moved_block_lacks_the_margins_of_its_new_ancestors),
    NAOS_ADD_TEST(an_inline_subtree_is_judged_on_the_content_of_its_leaf),
    NAOS_ADD_TEST(blocks_without_a_range_or_with_dropped_spelling_are_not_verbatim),
    NAOS_ADD_TEST(the_checker_finds_inconsistent_verbatim_results),
    NAOS_ADD_TEST(only_children_of_a_share_lines_type_share_a_line),
    NAOS_ADD_TEST(judging_every_node_is_linear),
    NAOS_ADD_TEST(judging_every_node_of_deep_emphasis_is_linear),
    NAOS_ADD_TEST(out_of_memory_in_the_judgment_leaves_the_cache_consistent),
    NAOS_ADD_TEST(a_list_without_blank_lines_between_its_blocks_is_tight),
    NAOS_ADD_TEST(a_blank_line_between_items_makes_the_list_loose),
    NAOS_ADD_TEST(a_blank_line_between_the_children_of_an_item_makes_the_list_loose),
    NAOS_ADD_TEST(blank_lines_that_are_not_between_two_blocks_keep_the_list_tight),
    NAOS_ADD_TEST(blank_lines_in_a_nested_list_or_a_block_quote_do_not_count),
    NAOS_ADD_TEST(the_tight_override_wins_until_it_is_unset),
    NAOS_ADD_TEST(changes_of_the_items_and_their_children_invalidate_the_tightness),
    NAOS_ADD_TEST(the_tightness_of_a_list_in_a_block_quote_is_cached_and_checked),
    NAOS_ADD_TEST(deriving_the_tightness_is_linear)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(verbatim)
)
