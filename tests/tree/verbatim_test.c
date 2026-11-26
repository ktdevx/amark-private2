/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The tests of Verbatim (syntax-tree): the spelling state of a node (11.1), the
 * new nodes (11.6), whether the content of a leaf can be output in its original
 * lines (11.3), and the coverage of a content by its tokens, the internal check
 * that the lossless check (13.2) reuses.
 */
#include "tree/tree.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <naos/naos.h>

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
    NAOS_ADD_TEST(cover_reuses_and_grows_its_scratch)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(verbatim)
)
