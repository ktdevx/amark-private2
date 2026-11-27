/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The tests of the tree builder of the tests (TestSupport, 13.8): the trees of
 * INPUT.md 3.1, 3.3 and 3.4 built from S-expressions in the format of the dump,
 * read back with the public API (values, styles, line ranges, margins, blank
 * lines, layout tokens, content and content tokens); the dropped marks, the
 * check of the line table, free content and building under another parent; the
 * errors with their positions, the release of the nodes created by a failed
 * build, and the allocation failures.
 */
#include "tree/tree.h"

#include <stddef.h>
#include <stdint.h>
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

#define CHECK_LINES(node, first_, count_) \
    do { \
        amark_line_id_t f_ = 0; \
        uint32_t c_ = 0; \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_lines((node), &f_, &c_)); \
        NAOS_CHECK_UINT_EQ((first_), f_); \
        NAOS_CHECK_UINT_EQ((count_), c_); \
    } while (0)

#define CHECK_MARGIN(node, index, line_, state_, flags_, start_, end_, role_) \
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
        NAOS_CHECK_UINT_EQ((role_), m_.role); \
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

#define CHECK_LAYOUT_TOKEN(node, index, kind_, line_, start_, end_, facet_, role_) \
    do { \
        amark_layout_token_t t_; \
        memset(&t_, 0, sizeof(t_)); \
        t_.struct_size = sizeof(t_); \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_token_at((node), (index), &t_)); \
        NAOS_CHECK_INT_EQ((kind_), t_.kind); \
        NAOS_CHECK_UINT_EQ((line_), t_.line); \
        NAOS_CHECK_UINT_EQ((start_), t_.column_start); \
        NAOS_CHECK_UINT_EQ((end_), t_.column_end); \
        NAOS_CHECK_UINT_EQ((facet_), t_.facet); \
        NAOS_CHECK_UINT_EQ((role_), t_.role); \
    } while (0)

#define CHECK_PIECE(node, index, line_, start_, end_) \
    do { \
        amark_content_piece_t p_; \
        memset(&p_, 0, sizeof(p_)); \
        p_.struct_size = sizeof(p_); \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_content_piece_at((node), (index), &p_)); \
        NAOS_CHECK_UINT_EQ((line_), p_.line); \
        NAOS_CHECK_UINT_EQ((start_), p_.column_start); \
        NAOS_CHECK_UINT_EQ((end_), p_.column_end); \
    } while (0)

#define CHECK_CONTENT_TOKEN(node, index, kind_, start_, end_, facet_, role_) \
    do { \
        amark_content_token_t t_; \
        memset(&t_, 0, sizeof(t_)); \
        t_.struct_size = sizeof(t_); \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_content_token_at((node), (index), &t_)); \
        NAOS_CHECK_INT_EQ((kind_), t_.kind); \
        NAOS_CHECK_UINT_EQ((start_), t_.start); \
        NAOS_CHECK_UINT_EQ((end_), t_.end); \
        NAOS_CHECK_UINT_EQ((facet_), t_.facet); \
        NAOS_CHECK_UINT_EQ((role_), t_.role); \
    } while (0)

/* Checks the number of a facet. */
#define CHECK_NUMBER(node, facet, state_, number_) \
    do { \
        amark_value_t v_; \
        memset(&v_, 0, sizeof(v_)); \
        v_.struct_size = sizeof(v_); \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_get((node), (facet), &v_)); \
        NAOS_CHECK_INT_EQ((state_), v_.state); \
        NAOS_CHECK_INT_EQ((number_), v_.number); \
    } while (0)

/* Checks the string of a facet against a literal. */
#define CHECK_STRING(node, facet, expected_literal) \
    do { \
        amark_value_t v_; \
        memset(&v_, 0, sizeof(v_)); \
        v_.struct_size = sizeof(v_); \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_get((node), (facet), &v_)); \
        NAOS_CHECK_INT_EQ(AMARK_VALUE_PRESENT, v_.state); \
        NAOS_CHECK_MEM_EQ(v_.text, v_.length, "" expected_literal, \
                          sizeof("" expected_literal) - 1); \
    } while (0)

/* Checks the bytes of the content of a leaf. */
#define CHECK_CONTENT(node, state_, expected_literal) \
    do { \
        const char *b_ = NULL; \
        size_t l_ = 0; \
        NAOS_CHECK_INT_EQ((state_), amark_block_content_state(node)); \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_content_bytes((node), &b_, &l_)); \
        NAOS_CHECK_MEM_EQ(b_, l_, "" expected_literal, sizeof("" expected_literal) - 1); \
    } while (0)

/* Builds sexpr and checks that it fails with error, with a reason that begins
 * with the position prefix (such as "line 2, column 5:") and holds detail. The
 * document then has only the nodes it had before. */
static void check_build_fails(amark_document_t *document, amark_node_t *parent,
                              const char *sexpr, amark_error_t error, const char *prefix,
                              const char *detail)
{
    char *reason = NULL;
    size_t nodes = document->node_count;

    NAOS_CHECK_INT_EQ(error, amark_test_tree_build(document, parent, sexpr, &reason));
    NAOS_REQUIRE_NOT_NULL(reason);
    if (strncmp(reason, prefix, strlen(prefix)) != 0)
    {
        NAOS_CHECK_STR_EQ(prefix, reason);
    }
    if (strstr(reason, detail) == NULL)
    {
        NAOS_CHECK_STR_EQ(detail, reason);
    }
    NAOS_CHECK_UINT_EQ(nodes, document->node_count);
    amark_test_tree_free_reason(reason);
    CHECK_TREE_OK(document);
}

/* ---- INPUT.md 3.1: a paragraph in a list in a block quote --------------------------- */

static const char input_3_1_source[] = "> - one *two\n>   three* four\n";

static const char input_3_1_tree[] =
    "(document\n"
    "  :lines (0 2)\n"
    "  :line_table ((0 1 0 12 lf 12) (0 2 13 15 lf 15))\n"
    "  (block_quote\n"
    "    :lines (0 2)\n"
    "    :margins ((0 0 2 present) (1 0 2 present))   ; \"> \" and \"> \"\n"
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

NAOS_TEST(input_3_1_builds_with_its_layout_and_spelling)
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
    NAOS_CHECK_UINT_EQ(11, document->node_count);
    CHECK_TREE_OK(document);

    CHECK_LINES(root, 0, 2);
    quote = amark_node_first_child(root);
    NAOS_REQUIRE_NOT_NULL(quote);
    NAOS_CHECK_UINT_EQ(AMARK_NODE_BLOCK_QUOTE, amark_node_get_type(quote));
    NAOS_CHECK_PTR_EQ(NULL, amark_node_next(quote));
    CHECK_LINES(quote, 0, 2);
    NAOS_CHECK_UINT_EQ(2, amark_block_margin_count(quote));
    CHECK_MARGIN(quote, 0, 0, AMARK_MARGIN_PRESENT, 0, 0, 2, 0);
    CHECK_MARGIN(quote, 1, 1, AMARK_MARGIN_PRESENT, 0, 0, 2, 0);

    list = amark_node_first_child(quote);
    NAOS_REQUIRE_NOT_NULL(list);
    NAOS_CHECK_UINT_EQ(AMARK_NODE_LIST, amark_node_get_type(list));
    CHECK_NUMBER(list, AMARK_LIST_TYPE, AMARK_VALUE_PRESENT, AMARK_LIST_TYPE_BULLET);
    CHECK_NUMBER(list, AMARK_LIST_START, AMARK_VALUE_PRESENT, 1);
    CHECK_NUMBER(list, AMARK_LIST_TIGHT, AMARK_VALUE_PRESENT, 1);
    CHECK_NUMBER(list, AMARK_LIST_BULLET, AMARK_VALUE_PRESENT, AMARK_LIST_BULLET_DASH);
    CHECK_NUMBER(list, AMARK_LIST_DELIMITER, AMARK_VALUE_UNSET, 0);
    CHECK_LINES(list, 0, 2);
    NAOS_CHECK_UINT_EQ(0, amark_block_margin_count(list));

    item = amark_node_first_child(list);
    NAOS_REQUIRE_NOT_NULL(item);
    NAOS_CHECK_UINT_EQ(AMARK_NODE_ITEM, amark_node_get_type(item));
    NAOS_CHECK_UINT_EQ(2, amark_block_margin_count(item));
    CHECK_MARGIN(item, 0, 0, AMARK_MARGIN_PRESENT, AMARK_MARGIN_FIRST, 2, 4, 0);
    CHECK_MARGIN(item, 1, 1, AMARK_MARGIN_PRESENT, 0, 2, 4, 0);

    paragraph = amark_node_first_child(item);
    NAOS_REQUIRE_NOT_NULL(paragraph);
    NAOS_CHECK_UINT_EQ(AMARK_NODE_PARAGRAPH, amark_node_get_type(paragraph));
    CHECK_LINES(paragraph, 0, 2);
    /* The content: "one *two" + the virtual "\n" + "three* four" (20 bytes). */
    CHECK_CONTENT(paragraph, AMARK_CONTENT_LAID, "one *two\nthree* four");
    NAOS_CHECK_UINT_EQ(2, amark_block_content_piece_count(paragraph));
    CHECK_PIECE(paragraph, 0, 0, 4, 12);
    CHECK_PIECE(paragraph, 1, 1, 4, 15);
    NAOS_CHECK_UINT_EQ(0, amark_node_content_token_count(paragraph));

    /* The spelling on the content. */
    NAOS_CHECK_UINT_EQ(AMARK_NODE_TEXT, amark_node_get_type(child_at(paragraph, 0)));
    CHECK_CONTENT_TOKEN(child_at(paragraph, 0), 0, AMARK_SPELL_TEXT, 0, 4, AMARK_TEXT_LITERAL, 0);
    CHECK_STRING(child_at(paragraph, 0), AMARK_TEXT_LITERAL, "one ");
    emph = child_at(paragraph, 1);
    NAOS_REQUIRE_NOT_NULL(emph);
    NAOS_CHECK_UINT_EQ(AMARK_NODE_EMPH, amark_node_get_type(emph));
    CHECK_NUMBER(emph, AMARK_EMPH_DELIMITER, AMARK_VALUE_PRESENT, AMARK_EMPH_DELIMITER_ASTERISK);
    NAOS_CHECK_UINT_EQ(2, amark_node_content_token_count(emph));
    CHECK_CONTENT_TOKEN(emph, 0, AMARK_SPELL_DELIMITER, 4, 5, AMARK_FACET_NONE, 1);
    CHECK_CONTENT_TOKEN(emph, 1, AMARK_SPELL_DELIMITER, 14, 15, AMARK_FACET_NONE, 2);
    CHECK_STRING(child_at(emph, 0), AMARK_TEXT_LITERAL, "two");
    NAOS_CHECK_UINT_EQ(AMARK_NODE_SOFTBREAK, amark_node_get_type(child_at(emph, 1)));
    CHECK_CONTENT_TOKEN(child_at(emph, 1), 0, AMARK_SPELL_LINE_BREAK, 8, 9, AMARK_FACET_NONE, 0);
    CHECK_STRING(child_at(emph, 2), AMARK_TEXT_LITERAL, "three");
    NAOS_CHECK_PTR_EQ(NULL, child_at(emph, 3));
    CHECK_STRING(child_at(paragraph, 2), AMARK_TEXT_LITERAL, " four");
    NAOS_CHECK_PTR_EQ(NULL, child_at(paragraph, 3));

    amark_document_destroy(document);
}

/* ---- INPUT.md 3.3: a partial tab ----------------------------------------------------- */

NAOS_TEST(input_3_3_builds_with_a_tab_shared_by_columns)
{
    /* ">\t\tfoo": the margin [0,2) ends inside the first tab, the indentation
     * [2,6) of the code block inside the second; the content starts with the 2
     * columns left of the second tab, as spaces. */
    amark_document_t *document = new_document(">\t\tfoo");
    amark_node_t *quote;
    amark_node_t *code;

    NAOS_REQUIRE_NOT_NULL(document);
    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(block_quote :lines (0 1) :margins ((0 0 2 present))\n"
                           "  (code_block :style (:form indented)\n"
                           "    :lines (0 1)\n"
                           "    :tokens ((space 0 2 6))\n"
                           "    :content laid ((0 6 11))\n"
                           "    :spelling ((tab_remainder 0 2) (text 2 5))\n"
                           "    :info \"\"\n"
                           "    \"  foo\\n\"))");
    CHECK_TREE_OK(document);

    quote = amark_node_first_child(amark_document_root(document));
    NAOS_REQUIRE_NOT_NULL(quote);
    CHECK_MARGIN(quote, 0, 0, AMARK_MARGIN_PRESENT, 0, 0, 2, 0);
    code = amark_node_first_child(quote);
    NAOS_REQUIRE_NOT_NULL(code);
    NAOS_CHECK_UINT_EQ(AMARK_NODE_CODE_BLOCK, amark_node_get_type(code));
    CHECK_NUMBER(code, AMARK_CODE_BLOCK_FORM, AMARK_VALUE_PRESENT, AMARK_CODE_BLOCK_FORM_INDENTED);
    CHECK_NUMBER(code, AMARK_CODE_BLOCK_FENCE_CHAR, AMARK_VALUE_UNSET, 0);
    NAOS_CHECK_UINT_EQ(1, amark_block_token_count(code));
    CHECK_LAYOUT_TOKEN(code, 0, AMARK_SPELL_SPACE, 0, 2, 6, AMARK_FACET_NONE, 0);
    CHECK_CONTENT(code, AMARK_CONTENT_LAID, "  foo");
    CHECK_PIECE(code, 0, 0, 6, 11);
    NAOS_CHECK_UINT_EQ(2, amark_node_content_token_count(code));
    CHECK_CONTENT_TOKEN(code, 0, AMARK_SPELL_TAB_REMAINDER, 0, 2, AMARK_FACET_NONE, 0);
    CHECK_CONTENT_TOKEN(code, 1, AMARK_SPELL_TEXT, 2, 5, AMARK_FACET_NONE, 0);
    /* The literal is the content of the leaf, not a recorded value. */
    CHECK_STRING(code, AMARK_CODE_BLOCK_LITERAL, "  foo\n");
    CHECK_STRING(code, AMARK_CODE_BLOCK_INFO, "");

    amark_document_destroy(document);
}

/* ---- INPUT.md 3.4: lazy continuation ------------------------------------------------- */

NAOS_TEST(input_3_4_builds_with_a_lazy_margin)
{
    amark_document_t *document = new_document("> a\nb\n");
    amark_node_t *quote;
    amark_node_t *paragraph;

    NAOS_REQUIRE_NOT_NULL(document);
    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(document :lines (0 2)\n"
                           "  (block_quote\n"
                           "    :lines (0 2)\n"
                           "    :margins ((0 0 2 present) (1 0 0 lazy))  ; L1 left it out\n"
                           "    (paragraph :lines (0 2) :content laid ((0 2 3) (1 0 1))\n"
                           "      (text :spelling ((text 0 1 :facet literal)) \"a\")\n"
                           "      (softbreak :spelling ((line_break 1 2)))\n"
                           "      (text :spelling ((text 2 3 :facet literal)) \"b\"))))");
    CHECK_TREE_OK(document);

    quote = amark_node_first_child(amark_document_root(document));
    NAOS_REQUIRE_NOT_NULL(quote);
    NAOS_CHECK_UINT_EQ(2, amark_block_margin_count(quote));
    CHECK_MARGIN(quote, 0, 0, AMARK_MARGIN_PRESENT, 0, 0, 2, 0);
    CHECK_MARGIN(quote, 1, 1, AMARK_MARGIN_LAZY, 0, 0, 0, 0);
    paragraph = amark_node_first_child(quote);
    NAOS_REQUIRE_NOT_NULL(paragraph);
    CHECK_CONTENT(paragraph, AMARK_CONTENT_LAID, "a\nb");
    CHECK_PIECE(paragraph, 0, 0, 2, 3);
    CHECK_PIECE(paragraph, 1, 1, 0, 1);
    CHECK_STRING(child_at(paragraph, 0), AMARK_TEXT_LITERAL, "a");
    CHECK_CONTENT_TOKEN(child_at(paragraph, 1), 0, AMARK_SPELL_LINE_BREAK, 1, 2, AMARK_FACET_NONE,
                        0);
    CHECK_STRING(child_at(paragraph, 2), AMARK_TEXT_LITERAL, "b");

    amark_document_destroy(document);
}

/* ---- Other records ---------------------------------------------------------------- */

NAOS_TEST(blank_lines_tokens_roles_and_free_content_are_recorded)
{
    /* L0 "# a #", L1 "  ", L2 "```", L3 "x", L4 "```". */
    amark_document_t *document = new_document("# a #\n  \n```\nx\n```\n");
    amark_node_t *root;
    amark_node_t *heading;
    amark_node_t *code;
    amark_node_t *paragraph;

    NAOS_REQUIRE_NOT_NULL(document);
    root = amark_document_root(document);
    AMARK_CHECK_TREE_BUILD(
        document, NULL,
        "(document :lines (0 5) :blanks ((1 0 2))\n"
        "  (heading :level 1 :style (:form atx :closing true)\n"
        "    :lines (0 1)\n"
        "    :tokens ((marker 0 0 1 :role 1) (space 0 1 2) (marker 0 4 5 :role 2))\n"
        "    :content laid ((0 2 3))\n"
        "    (text :spelling ((text 0 1 :facet literal)) \"a\"))\n"
        "  (code_block :info \"\" :style (:form fenced :fence_char \"`\" :fence_length 3)\n"
        "    :lines (2 3)\n"
        "    :tokens ((marker 2 0 3) (marker 4 0 3))\n"
        "    :content laid ((3 0 1))\n"
        "    \"x\\n\")\n"
        "  (paragraph :content free \"p\\nq\" :pos (0 0 0 0)\n"
        "    (text \"p\") (softbreak) (text \"q\")))");
    CHECK_TREE_OK(document);

    CHECK_LINES(root, 0, 5);
    NAOS_CHECK_UINT_EQ(1, amark_block_blank_line_count(root));
    CHECK_BLANK(root, 0, 1, 0, 2);

    heading = child_at(root, 0);
    NAOS_REQUIRE_NOT_NULL(heading);
    CHECK_NUMBER(heading, AMARK_HEADING_LEVEL, AMARK_VALUE_PRESENT, 1);
    CHECK_NUMBER(heading, AMARK_HEADING_FORM, AMARK_VALUE_PRESENT, AMARK_HEADING_FORM_ATX);
    CHECK_NUMBER(heading, AMARK_HEADING_CLOSING, AMARK_VALUE_PRESENT, 1);
    NAOS_CHECK_UINT_EQ(3, amark_block_token_count(heading));
    CHECK_LAYOUT_TOKEN(heading, 0, AMARK_SPELL_MARKER, 0, 0, 1, AMARK_FACET_NONE, 1);
    CHECK_LAYOUT_TOKEN(heading, 1, AMARK_SPELL_SPACE, 0, 1, 2, AMARK_FACET_NONE, 0);
    CHECK_LAYOUT_TOKEN(heading, 2, AMARK_SPELL_MARKER, 0, 4, 5, AMARK_FACET_NONE, 2);
    CHECK_CONTENT(heading, AMARK_CONTENT_LAID, "a");

    code = child_at(root, 1);
    NAOS_REQUIRE_NOT_NULL(code);
    CHECK_NUMBER(code, AMARK_CODE_BLOCK_FORM, AMARK_VALUE_PRESENT, AMARK_CODE_BLOCK_FORM_FENCED);
    CHECK_NUMBER(code, AMARK_CODE_BLOCK_FENCE_CHAR, AMARK_VALUE_PRESENT,
                 AMARK_CODE_BLOCK_FENCE_CHAR_BACKTICK);
    CHECK_NUMBER(code, AMARK_CODE_BLOCK_FENCE_LENGTH, AMARK_VALUE_PRESENT, 3);
    CHECK_LINES(code, 2, 3);
    NAOS_CHECK_UINT_EQ(2, amark_block_token_count(code));
    CHECK_LAYOUT_TOKEN(code, 1, AMARK_SPELL_MARKER, 4, 0, 3, AMARK_FACET_NONE, 0);
    CHECK_CONTENT(code, AMARK_CONTENT_LAID, "x");
    CHECK_STRING(code, AMARK_CODE_BLOCK_LITERAL, "x\n");

    /* Free content, and values recorded without spelling. */
    paragraph = child_at(root, 2);
    NAOS_REQUIRE_NOT_NULL(paragraph);
    CHECK_CONTENT(paragraph, AMARK_CONTENT_FREE, "p\nq");
    CHECK_LINES(paragraph, AMARK_LINE_NONE, 0);
    CHECK_STRING(child_at(paragraph, 0), AMARK_TEXT_LITERAL, "p");
    NAOS_CHECK_UINT_EQ(0, amark_node_content_token_count(child_at(paragraph, 0)));
    NAOS_CHECK_UINT_EQ(AMARK_NODE_SOFTBREAK, amark_node_get_type(child_at(paragraph, 1)));
    CHECK_STRING(child_at(paragraph, 2), AMARK_TEXT_LITERAL, "q");

    amark_document_destroy(document);
}

NAOS_TEST(a_code_block_without_content_records_its_literal_as_free_content)
{
    amark_document_t *document = new_document("");
    amark_node_t *code;

    NAOS_REQUIRE_NOT_NULL(document);
    AMARK_CHECK_TREE_BUILD(document, NULL, "(code_block :info \"c\" \"a\\tb\\n\\n\")");
    code = amark_node_first_child(amark_document_root(document));
    NAOS_REQUIRE_NOT_NULL(code);
    CHECK_STRING(code, AMARK_CODE_BLOCK_INFO, "c");
    CHECK_STRING(code, AMARK_CODE_BLOCK_LITERAL, "a\tb\n\n");
    CHECK_CONTENT(code, AMARK_CONTENT_FREE, "a\tb\n");
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(strings_take_their_escapes)
{
    amark_document_t *document = new_document("");
    amark_node_t *html;

    NAOS_REQUIRE_NOT_NULL(document);
    AMARK_CHECK_TREE_BUILD(
        document, NULL,
        "(paragraph (html_inline \"<a \\\"q\\\" \\\\ \\x41\\xc3\\xa9\\r\\t>\"))");
    html = amark_node_first_child(amark_node_first_child(amark_document_root(document)));
    NAOS_REQUIRE_NOT_NULL(html);
    CHECK_STRING(html, AMARK_HTML_INLINE_LITERAL, "<a \"q\" \\ A\xc3\xa9\r\t>");
    amark_document_destroy(document);
}

NAOS_TEST(dropped_marks_are_set)
{
    amark_document_t *document = new_document("");
    amark_node_t *paragraph;
    amark_node_t *link;
    amark_node_t *code;

    NAOS_REQUIRE_NOT_NULL(document);
    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(paragraph :dropped (structure)\n"
                           "  (link :destination \"/u\" :style (:form inline)\n"
                           "        :dropped (destination title)))\n"
                           "(code_block :info \"\" :dropped (structure literal) \"x\\n\")");
    CHECK_TREE_OK(document);
    paragraph = child_at(amark_document_root(document), 0);
    NAOS_REQUIRE_NOT_NULL(paragraph);
    NAOS_CHECK_UINT_EQ(AMARK_TREE_DROPPED_STRUCTURE, paragraph->dropped);
    link = amark_node_first_child(paragraph);
    NAOS_REQUIRE_NOT_NULL(link);
    NAOS_CHECK_UINT_EQ((1u << AMARK_LINK_DESTINATION) | (1u << AMARK_LINK_TITLE), link->dropped);
    CHECK_STRING(link, AMARK_LINK_DESTINATION, "/u");
    CHECK_NUMBER(link, AMARK_LINK_FORM, AMARK_VALUE_PRESENT, AMARK_LINK_FORM_INLINE);
    code = child_at(amark_document_root(document), 1);
    NAOS_REQUIRE_NOT_NULL(code);
    NAOS_CHECK_UINT_EQ(AMARK_TREE_DROPPED_STRUCTURE | (1u << AMARK_CODE_BLOCK_LITERAL),
                       code->dropped);
    CHECK_STRING(code, AMARK_CODE_BLOCK_LITERAL, "x\n");
    amark_document_destroy(document);
}

NAOS_TEST(nodes_are_appended_to_the_given_parent_in_order)
{
    amark_document_t *document = new_document("");
    amark_node_t *paragraph;

    NAOS_REQUIRE_NOT_NULL(document);
    AMARK_CHECK_TREE_BUILD(document, NULL, "(paragraph (text \"a\"))");
    paragraph = amark_node_first_child(amark_document_root(document));
    NAOS_REQUIRE_NOT_NULL(paragraph);
    AMARK_CHECK_TREE_BUILD(document, paragraph,
                           "(emph (text \"b\")) (code \"c\" :style (:backticks 2 :padded false))");
    NAOS_CHECK_UINT_EQ(AMARK_NODE_TEXT, amark_node_get_type(child_at(paragraph, 0)));
    NAOS_CHECK_UINT_EQ(AMARK_NODE_EMPH, amark_node_get_type(child_at(paragraph, 1)));
    NAOS_CHECK_UINT_EQ(AMARK_NODE_CODE, amark_node_get_type(child_at(paragraph, 2)));
    CHECK_STRING(child_at(paragraph, 2), AMARK_CODE_LITERAL, "c");
    CHECK_NUMBER(child_at(paragraph, 2), AMARK_CODE_BACKTICKS, AMARK_VALUE_PRESENT, 2);
    CHECK_NUMBER(child_at(paragraph, 2), AMARK_CODE_PADDED, AMARK_VALUE_PRESENT, 0);
    CHECK_STRING(amark_node_first_child(child_at(paragraph, 1)), AMARK_TEXT_LITERAL, "b");
    NAOS_CHECK_PTR_EQ(NULL, child_at(paragraph, 3));
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

/* ---- Errors ------------------------------------------------------------------------ */

NAOS_TEST(syntax_errors_report_their_positions)
{
    amark_document_t *document = new_document("a\n");

    NAOS_REQUIRE_NOT_NULL(document);
    check_build_fails(document, NULL, "", AMARK_ERROR_INVALID_ARGUMENT, "line 1, column 1:",
                      "node");
    check_build_fails(document, NULL, "(paragraph\n  (text \"a\")", AMARK_ERROR_INVALID_ARGUMENT,
                      "line 1, column 1:", "unclosed");
    check_build_fails(document, NULL, "(paragraph))", AMARK_ERROR_INVALID_ARGUMENT,
                      "line 1, column 12:", "unexpected )");
    check_build_fails(document, NULL, "(paragraph\n  (text \"a))", AMARK_ERROR_INVALID_ARGUMENT,
                      "line 2, column 9:", "unterminated string");
    check_build_fails(document, NULL, "(text \"\\q\")", AMARK_ERROR_INVALID_ARGUMENT,
                      "line 1, column 8:", "escape");
    check_build_fails(document, NULL, "(text \"\\x4\")", AMARK_ERROR_INVALID_ARGUMENT,
                      "line 1, column 8:", "escape");
    check_build_fails(document, NULL, "paragraph", AMARK_ERROR_INVALID_ARGUMENT,
                      "line 1, column 1:", "node");
    check_build_fails(document, NULL, "(:lines)", AMARK_ERROR_INVALID_ARGUMENT,
                      "line 1, column 2:", "type name");
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(semantic_errors_report_their_positions)
{
    amark_document_t *document = new_document("a\n");

    NAOS_REQUIRE_NOT_NULL(document);
    check_build_fails(document, NULL, "(paragraph)\n(nothing)", AMARK_ERROR_INVALID_ARGUMENT,
                      "line 2, column 2:", "unknown node type");
    check_build_fails(document, NULL, "(list :colour 1)", AMARK_ERROR_INVALID_ARGUMENT,
                      "line 1, column 7:", "colour");
    check_build_fails(document, NULL, "(list :type numbered)", AMARK_ERROR_INVALID_ARGUMENT,
                      "line 1, column 13:", "numbered");
    check_build_fails(document, NULL, "(list :start \"1\")", AMARK_ERROR_INVALID_ARGUMENT,
                      "line 1, column 14:", "integer");
    check_build_fails(document, NULL, "(list :tight yes)", AMARK_ERROR_INVALID_ARGUMENT,
                      "line 1, column 14:", "true or false");
    check_build_fails(document, NULL, "(list :bullet \"-\")", AMARK_ERROR_INVALID_ARGUMENT,
                      "line 1, column 7:", "style");
    check_build_fails(document, NULL, "(list :style (:type bullet))",
                      AMARK_ERROR_INVALID_ARGUMENT, "line 1, column 15:", "not a style");
    check_build_fails(document, NULL, "(list :start 99999999999999999999)",
                      AMARK_ERROR_INVALID_ARGUMENT, "line 1, column 14:", "integer");
    check_build_fails(document, NULL, "(paragraph :lines (0))", AMARK_ERROR_INVALID_ARGUMENT,
                      "line 1, column 19:", "(first count)");
    check_build_fails(document, NULL, "(paragraph :content (0 0 1))",
                      AMARK_ERROR_INVALID_ARGUMENT, "line 1, column 21:", "laid or free");
    check_build_fails(document, NULL, "(paragraph (text :spelling ((bold 0 1))))",
                      AMARK_ERROR_INVALID_ARGUMENT, "line 1, column 30:", "token kind");
    check_build_fails(document, NULL, "(block_quote :margins ((0 0 1 present second)))",
                      AMARK_ERROR_INVALID_ARGUMENT, "line 1, column 39:", "second");
    check_build_fails(document, NULL, "(paragraph :dropped (structure size))",
                      AMARK_ERROR_INVALID_ARGUMENT, "line 1, column 32:", "size");
    check_build_fails(document, NULL, "(text \"a\" \"b\")", AMARK_ERROR_INVALID_ARGUMENT,
                      "line 1, column 11:", "literal");
    check_build_fails(document, NULL, "(paragraph :lines (0 1) :lines (0 1))",
                      AMARK_ERROR_INVALID_ARGUMENT, "line 1, column 25:", "twice");
    check_build_fails(document, NULL, "(list :type bullet :type ordered)",
                      AMARK_ERROR_INVALID_ARGUMENT, "line 1, column 20:", "twice");
    check_build_fails(document, NULL, "(paragraph :lines)", AMARK_ERROR_INVALID_ARGUMENT,
                      "line 1, column 12:", "value");
    check_build_fails(document, NULL, "(paragraph x)", AMARK_ERROR_INVALID_ARGUMENT,
                      "line 1, column 12:", "x");
    check_build_fails(document, NULL, "(paragraph (document))", AMARK_ERROR_INVALID_ARGUMENT,
                      "line 1, column 13:", "document");
    check_build_fails(document, NULL, "(document :line_table ((0 1 0 1 crlf 1)))",
                      AMARK_ERROR_INVALID_ARGUMENT, "line 1, column 24:", "line table");
    check_build_fails(document, NULL, "(document :line_table ())",
                      AMARK_ERROR_INVALID_ARGUMENT, "line 1, column 23:", "line table");
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(refused_records_report_their_positions_and_free_the_nodes)
{
    amark_document_t *document = new_document("> a\nb\n");
    amark_node_t *root;

    NAOS_REQUIRE_NOT_NULL(document);
    root = amark_document_root(document);
    /* A margin beyond the width of the line, deep in the tree. */
    check_build_fails(document, NULL,
                      "(paragraph)\n"
                      "(block_quote :lines (0 2)\n"
                      "  (paragraph :lines (0 1))\n"
                      "  (block_quote :lines (0 2) :margins ((0 0 9 present))))",
                      AMARK_ERROR_INVALID_ARGUMENT, "line 4, column 39:", "margin");
    /* A line outside the line table. */
    check_build_fails(document, NULL, "(paragraph :lines (5 1))", AMARK_ERROR_INVALID_ARGUMENT,
                      "line 1, column 19:", "lines");
    /* A child the parent cannot contain. */
    check_build_fails(document, NULL, "(paragraph (paragraph))", AMARK_ERROR_INVALID_ARGUMENT,
                      "line 1, column 12:", "append");
    /* A content token on a leaf without content. */
    check_build_fails(document, NULL, "(paragraph (text :spelling ((text 0 1)) \"a\"))",
                      AMARK_ERROR_INVALID_ARGUMENT, "line 1, column 29:", "content token");
    /* A content token of a kind that does not match its bytes. */
    check_build_fails(document, NULL,
                      "(paragraph :content free \"ab\" (softbreak :spelling ((line_break 0 1))))",
                      AMARK_ERROR_INVALID_ARGUMENT, "line 1, column 53:", "content token");
    /* A value the facet refuses. */
    check_build_fails(document, NULL, "(heading :level 7)", AMARK_ERROR_INVALID_ARGUMENT,
                      "line 1, column 17:", "level");
    /* A literal that differs from the value derived from its spelling. */
    check_build_fails(document, NULL,
                      "(paragraph :content free \"ab\" (text :spelling ((text 0 2 :facet literal)) "
                      "\"xy\"))",
                      AMARK_ERROR_INVALID_ARGUMENT, "line 1, column 75:", "differs");
    /* A literal of a code block that differs from its content. */
    check_build_fails(document, NULL,
                      "(code_block :lines (0 1) :content laid ((0 2 3)) \"b\\n\")",
                      AMARK_ERROR_INVALID_ARGUMENT, "line 1, column 50:", "differs");
    NAOS_CHECK_PTR_EQ(NULL, amark_node_first_child(root));
    CHECK_LINES(root, AMARK_LINE_NONE, 0);
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(a_failed_document_keeps_the_document_node_without_children)
{
    amark_document_t *document = new_document("> a\nb\n");
    amark_node_t *root;
    amark_node_t *quote = NULL;

    NAOS_REQUIRE_NOT_NULL(document);
    root = amark_document_root(document);
    check_build_fails(document, NULL,
                      "(document :lines (0 2)\n"
                      "  (paragraph :lines (0 2))\n"
                      "  (paragraph :lines (3 1)))",
                      AMARK_ERROR_INVALID_ARGUMENT, "line 3, column 21:", "lines");
    NAOS_CHECK_PTR_EQ(NULL, amark_node_first_child(root));
    /* The records of the document node come after its children. */
    CHECK_LINES(root, AMARK_LINE_NONE, 0);
    /* A document node under another node. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, AMARK_NODE_BLOCK_QUOTE,
                                                            &quote));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(root, quote));
    check_build_fails(document, quote, "(document)", AMARK_ERROR_INVALID_ARGUMENT,
                      "line 1, column 2:", "document");
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(bad_arguments_are_refused)
{
    amark_document_t *document = new_document("");
    amark_document_t *other = new_document("");
    amark_node_t *foreign = NULL;
    char *reason = NULL;

    NAOS_REQUIRE_NOT_NULL(document);
    NAOS_REQUIRE_NOT_NULL(other);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_test_tree_build(NULL, NULL, "(paragraph)", &reason));
    NAOS_CHECK_NOT_NULL(reason);
    amark_test_tree_free_reason(reason);
    reason = NULL;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_test_tree_build(document, NULL, NULL, &reason));
    amark_test_tree_free_reason(reason);
    reason = NULL;
    /* A parent of another document. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_create(other, AMARK_NODE_PARAGRAPH, &foreign));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_test_tree_build(document, foreign, "(text \"a\")", &reason));
    amark_test_tree_free_reason(reason);
    /* reason may be NULL. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_test_tree_build(document, NULL, "(", NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_test_tree_build(document, NULL, "(paragraph)", NULL));
    amark_test_tree_free_reason(NULL);
    amark_node_destroy(foreign);
    amark_document_destroy(other);
    amark_document_destroy(document);
}

/* ---- Allocation failures ---------------------------------------------------------- */

static amark_error_t oom_scenario(void *context)
{
    amark_document_t *document = NULL;
    amark_source_id_t source = 0;
    char *reason = NULL;
    amark_error_t error;

    (void)context;
    error = amark_document_create(NULL, &document);
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    error = amark_document_add_source(document, input_3_1_source, strlen(input_3_1_source),
                                      AMARK_SOURCE_FINISHED, &source);
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_test_tree_build(document, NULL, input_3_1_tree, &reason);
        amark_test_tree_free_reason(reason);
        /* A failed build leaves only the document node. */
        if ((error != AMARK_ERROR_NONE) &&
            ((document->node_count != 1) ||
             (amark_node_first_child(amark_document_root(document)) != NULL)))
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

NAOS_TEST(out_of_memory_frees_the_nodes_and_leaks_nothing)
{
    AMARK_CHECK_OOM_SWEEP(oom_scenario, NULL);
}

NAOS_SUITE(tree_builder,
    NAOS_ADD_TEST(input_3_1_builds_with_its_layout_and_spelling),
    NAOS_ADD_TEST(input_3_3_builds_with_a_tab_shared_by_columns),
    NAOS_ADD_TEST(input_3_4_builds_with_a_lazy_margin),
    NAOS_ADD_TEST(blank_lines_tokens_roles_and_free_content_are_recorded),
    NAOS_ADD_TEST(a_code_block_without_content_records_its_literal_as_free_content),
    NAOS_ADD_TEST(strings_take_their_escapes),
    NAOS_ADD_TEST(dropped_marks_are_set),
    NAOS_ADD_TEST(nodes_are_appended_to_the_given_parent_in_order),
    NAOS_ADD_TEST(syntax_errors_report_their_positions),
    NAOS_ADD_TEST(semantic_errors_report_their_positions),
    NAOS_ADD_TEST(refused_records_report_their_positions_and_free_the_nodes),
    NAOS_ADD_TEST(a_failed_document_keeps_the_document_node_without_children),
    NAOS_ADD_TEST(bad_arguments_are_refused),
    NAOS_ADD_TEST(out_of_memory_frees_the_nodes_and_leaks_nothing)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(tree_builder)
)
