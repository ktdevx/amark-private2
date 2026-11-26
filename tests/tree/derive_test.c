/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The tests of Facets (derived values) (syntax-tree): the value of a STRING
 * facet derived from the tokens that spell it, decoded by kind (escapes,
 * character references, replaced bytes, virtual line breaks, tab remainders;
 * the structural tokens left out), with the RAW, FOLD_LINE_BREAKS and LINES
 * flags; the CONTENT facets (the literals of code_block and html_block)
 * always derived from the content of the leaf; the order of the getter
 * (derived before set or recorded); the cache of the derived value and its
 * drop when the tokens or the content change; the refusal of a facet spelled
 * by both layout and content tokens; the internal check of the caches and the
 * allocation failures (3.3). The dropping of the spelling by the edits: a set
 * value or style, and an unset style, drop only the tokens of their facet
 * (6.7, 6.8); the dropping of the spelling of a facet and of a node, which
 * keeps the values (the tight of a list as an override) and makes laid
 * content free (6.13); the round trip of the
 * literals of the CONTENT facets (8.7); the setting of the inline text (6.12).
 */
#include "tree/tree.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <naos/naos.h>

#include "test_allocator.h"
#include "test_check.h"

/* ---- A test extension type with a LINES facet --------------------------------- */

#define NOTE_BODY 1u /* V STRING, LINES */
#define NOTE_TYPE AMARK_NODE_DYNAMIC_FIRST

static const amark_facet_desc_t note_facets[1] = {
    {sizeof(amark_facet_desc_t), "body", AMARK_FACET_VALUE, AMARK_FACET_STRING,
     AMARK_FACET_LINES, 0, 0, NULL, 0, 0, NULL, NULL, NULL, NULL},
};

static const amark_node_type_desc_t note_type = {
    sizeof(amark_node_type_desc_t), "test.note", AMARK_NODE_LEVEL_BLOCK, AMARK_CONTENT_BLOCK,
    0, 0, note_facets, 1, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
static const amark_node_type_desc_t *const note_types[] = {&note_type};
static const amark_extension_t note_extension = {
    sizeof(amark_extension_t), "test.derive", note_types, 1, NULL, 0, NULL};

/* ---- Helpers ------------------------------------------------------------------- */

/* A document of the built-in syntax set (or with note_extension) with one
 * finished source of length bytes. */
static amark_document_t *new_document_n(const char *text, size_t length, bool note)
{
    amark_document_t *document = NULL;
    amark_syntax_set_t *set = NULL;
    amark_source_id_t source = 0;

    if (note)
    {
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_create(&set));
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &note_extension));
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_freeze(set));
    }
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(set, &document));
    if (set != NULL)
    {
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_take_syntax_set(document, set));
    }
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_add_source(document, text, length, AMARK_SOURCE_FINISHED,
                                                  &source));
    return document;
}

#define NEW_DOCUMENT(literal) new_document_n(("" literal), sizeof("" literal) - 1, false)

static amark_node_t *new_node(amark_document_t *document, amark_node_type_t type)
{
    amark_node_t *node = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, type, &node));
    NAOS_REQUIRE_NOT_NULL(node);
    return node;
}

/* A block of type with the line range [first, first + count). */
static amark_node_t *new_block(amark_document_t *document, amark_node_type_t type,
                               amark_line_id_t first, uint32_t count)
{
    amark_node_t *node = new_node(document, type);

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(node, first, count));
    return node;
}

/* A node of type appended to parent. */
static amark_node_t *new_child(amark_node_t *parent, amark_node_type_t type)
{
    amark_node_t *node = new_node(amark_node_document(parent), type);

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(parent, node));
    return node;
}

static void add_content(amark_node_t *leaf, amark_line_id_t line, uint32_t start, uint32_t end)
{
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(leaf, line, start, end));
}

static amark_error_t try_token(amark_node_t *node, const amark_node_t *leaf, size_t start,
                               size_t end, amark_spell_kind_t kind, amark_facet_t facet)
{
    amark_content_token_t token;

    memset(&token, 0, sizeof(token));
    token.struct_size = sizeof(token);
    token.start = start;
    token.end = end;
    token.kind = kind;
    token.facet = facet;
    return amark_node_add_content_token(node, leaf, &token);
}

static void token(amark_node_t *node, const amark_node_t *leaf, size_t start, size_t end,
                  amark_spell_kind_t kind, amark_facet_t facet)
{
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, try_token(node, leaf, start, end, kind, facet));
}

static amark_error_t try_layout_token(amark_node_t *block, amark_line_id_t line, uint32_t start,
                                      uint32_t end, amark_spell_kind_t kind, amark_facet_t facet)
{
    amark_layout_token_t token;

    memset(&token, 0, sizeof(token));
    token.struct_size = sizeof(token);
    token.line = line;
    token.column_start = start;
    token.column_end = end;
    token.kind = kind;
    token.facet = facet;
    return amark_block_add_token(block, &token);
}

static void layout_token(amark_node_t *block, amark_line_id_t line, uint32_t start, uint32_t end,
                         amark_spell_kind_t kind, amark_facet_t facet)
{
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, try_layout_token(block, line, start, end, kind, facet));
}

static amark_value_t get(const amark_node_t *node, amark_facet_t facet)
{
    amark_value_t value;

    memset(&value, 0xA5, sizeof(value));
    value.struct_size = sizeof(value);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_get(node, facet, &value));
    return value;
}

/* Checks a PRESENT string, terminated by NUL. */
#define CHECK_TEXT(node, facet, expected_literal) \
    do { \
        amark_value_t v_ = get((node), (facet)); \
        NAOS_CHECK_INT_EQ(AMARK_VALUE_PRESENT, v_.state); \
        NAOS_REQUIRE_NOT_NULL(v_.text); \
        AMARK_CHECK_BYTES_EQ(v_.text, v_.length, expected_literal); \
        NAOS_CHECK_INT_EQ(0, v_.text[v_.length]); \
    } while (0)

#define CHECK_TREE_OK(document) \
    NAOS_CHECK_STR_EQ("none", amark_tree_violation_name(amark_tree_check((document), NULL)))

/* The cache of facet of node. */
#define CACHE(node, facet) ((node)->facets[(facet) - 1].cache)

/* ---- Decoding the tokens --------------------------------------------------------- */

NAOS_TEST(escapes_and_character_references_are_decoded)
{
    /* a\*b &amp; &#35; &#x41;&NotEqualTilde; */
    amark_document_t *document =
        NEW_DOCUMENT("a\\*b &amp; &#35; &#x41;&NotEqualTilde;\n");
    amark_node_t *paragraph = new_block(document, AMARK_NODE_PARAGRAPH, 0, 1);
    amark_node_t *text;

    add_content(paragraph, 0, 0, 38);
    text = new_child(paragraph, AMARK_NODE_TEXT);
    token(text, paragraph, 0, 1, AMARK_SPELL_TEXT, AMARK_TEXT_LITERAL);
    token(text, paragraph, 1, 3, AMARK_SPELL_ESCAPE, AMARK_TEXT_LITERAL);
    token(text, paragraph, 3, 5, AMARK_SPELL_TEXT, AMARK_TEXT_LITERAL);
    token(text, paragraph, 5, 10, AMARK_SPELL_CHAR_REF, AMARK_TEXT_LITERAL);
    token(text, paragraph, 10, 11, AMARK_SPELL_TEXT, AMARK_TEXT_LITERAL);
    token(text, paragraph, 11, 16, AMARK_SPELL_CHAR_REF, AMARK_TEXT_LITERAL);
    token(text, paragraph, 16, 17, AMARK_SPELL_TEXT, AMARK_TEXT_LITERAL);
    token(text, paragraph, 17, 23, AMARK_SPELL_CHAR_REF, AMARK_TEXT_LITERAL);
    token(text, paragraph, 23, 38, AMARK_SPELL_CHAR_REF, AMARK_TEXT_LITERAL);
    /* &NotEqualTilde; is U+2242 U+0338. */
    CHECK_TEXT(text, AMARK_TEXT_LITERAL, "a*b & # A\xE2\x89\x82\xCC\xB8");
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(replaced_bytes_become_one_replacement_character_per_unit)
{
    /* NUL, then two invalid bytes (two units), then a truncated sequence (one
     * unit). */
    amark_document_t *document = NEW_DOCUMENT("a\0b\xFF\xFE" "c\xE3\x81" "d\n");
    amark_node_t *paragraph = new_block(document, AMARK_NODE_PARAGRAPH, 0, 1);
    amark_node_t *text;

    add_content(paragraph, 0, 0, 9);
    text = new_child(paragraph, AMARK_NODE_TEXT);
    token(text, paragraph, 0, 1, AMARK_SPELL_TEXT, AMARK_TEXT_LITERAL);
    token(text, paragraph, 1, 2, AMARK_SPELL_REPLACED, AMARK_TEXT_LITERAL);
    token(text, paragraph, 2, 3, AMARK_SPELL_TEXT, AMARK_TEXT_LITERAL);
    token(text, paragraph, 3, 5, AMARK_SPELL_REPLACED, AMARK_TEXT_LITERAL);
    token(text, paragraph, 5, 6, AMARK_SPELL_TEXT, AMARK_TEXT_LITERAL);
    token(text, paragraph, 6, 8, AMARK_SPELL_REPLACED, AMARK_TEXT_LITERAL);
    token(text, paragraph, 8, 9, AMARK_SPELL_TEXT, AMARK_TEXT_LITERAL);
    CHECK_TEXT(text, AMARK_TEXT_LITERAL,
               "a\xEF\xBF\xBD" "b\xEF\xBF\xBD\xEF\xBF\xBD" "c\xEF\xBF\xBD" "d");
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(code_span_line_breaks_fold_and_structural_tokens_are_left_out)
{
    /* ` a
     * b ` <x
     * y> */
    amark_document_t *document = NEW_DOCUMENT("` a\nb ` <x\ny>\n");
    amark_node_t *paragraph = new_block(document, AMARK_NODE_PARAGRAPH, 0, 3);
    amark_node_t *code;
    amark_node_t *html;

    add_content(paragraph, 0, 0, 3);
    add_content(paragraph, 1, 0, 6);
    add_content(paragraph, 2, 0, 2);
    /* "` a\nb ` <x\ny>" */
    code = new_child(paragraph, AMARK_NODE_CODE);
    token(code, paragraph, 0, 1, AMARK_SPELL_DELIMITER, AMARK_FACET_NONE);
    token(code, paragraph, 1, 2, AMARK_SPELL_SPACE, AMARK_CODE_LITERAL);
    token(code, paragraph, 2, 3, AMARK_SPELL_TEXT, AMARK_CODE_LITERAL);
    token(code, paragraph, 3, 4, AMARK_SPELL_LINE_BREAK, AMARK_CODE_LITERAL);
    token(code, paragraph, 4, 5, AMARK_SPELL_TEXT, AMARK_CODE_LITERAL);
    token(code, paragraph, 5, 6, AMARK_SPELL_SPACE, AMARK_CODE_LITERAL);
    token(code, paragraph, 6, 7, AMARK_SPELL_DELIMITER, AMARK_FACET_NONE);
    CHECK_TEXT(code, AMARK_CODE_LITERAL, "a b");
    /* html_inline is RAW and does not fold: the line break stays LF. */
    html = new_child(paragraph, AMARK_NODE_HTML_INLINE);
    token(html, paragraph, 8, 10, AMARK_SPELL_TEXT, AMARK_HTML_INLINE_LITERAL);
    token(html, paragraph, 10, 11, AMARK_SPELL_LINE_BREAK, AMARK_HTML_INLINE_LITERAL);
    token(html, paragraph, 11, 13, AMARK_SPELL_TEXT, AMARK_HTML_INLINE_LITERAL);
    CHECK_TEXT(html, AMARK_HTML_INLINE_LITERAL, "<x\ny>");
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(a_raw_label_keeps_its_escapes_and_references)
{
    /* [a\]&amp;b]: /u\_&amp; */
    amark_document_t *document = NEW_DOCUMENT("[a\\]&amp;b]: /u\\_&amp;\n");
    amark_node_t *definition = new_block(document, AMARK_NODE_LINK_DEFINITION, 0, 1);
    amark_node_t *paragraph;
    amark_node_t *link;

    add_content(definition, 0, 0, 22);
    token(definition, definition, 0, 1, AMARK_SPELL_DELIMITER, AMARK_FACET_NONE);
    token(definition, definition, 1, 2, AMARK_SPELL_TEXT, AMARK_LINK_DEFINITION_LABEL);
    token(definition, definition, 2, 4, AMARK_SPELL_ESCAPE, AMARK_LINK_DEFINITION_LABEL);
    token(definition, definition, 4, 9, AMARK_SPELL_CHAR_REF, AMARK_LINK_DEFINITION_LABEL);
    token(definition, definition, 9, 10, AMARK_SPELL_TEXT, AMARK_LINK_DEFINITION_LABEL);
    token(definition, definition, 10, 12, AMARK_SPELL_DELIMITER, AMARK_FACET_NONE);
    token(definition, definition, 12, 13, AMARK_SPELL_SPACE, AMARK_FACET_NONE);
    token(definition, definition, 13, 15, AMARK_SPELL_TEXT, AMARK_LINK_DEFINITION_DESTINATION);
    token(definition, definition, 15, 17, AMARK_SPELL_ESCAPE, AMARK_LINK_DEFINITION_DESTINATION);
    token(definition, definition, 17, 22, AMARK_SPELL_CHAR_REF,
          AMARK_LINK_DEFINITION_DESTINATION);
    CHECK_TEXT(definition, AMARK_LINK_DEFINITION_LABEL, "a\\]&amp;b");
    CHECK_TEXT(definition, AMARK_LINK_DEFINITION_DESTINATION, "/u_&");

    /* The label of a full reference link: [x][a\]] */
    paragraph = new_block(document, AMARK_NODE_PARAGRAPH, 0, 1);
    add_content(paragraph, 0, 0, 11);
    link = new_child(paragraph, AMARK_NODE_LINK);
    token(link, paragraph, 0, 1, AMARK_SPELL_DELIMITER, AMARK_FACET_NONE);
    token(link, paragraph, 2, 4, AMARK_SPELL_ESCAPE, AMARK_LINK_LABEL);
    token(link, paragraph, 4, 9, AMARK_SPELL_CHAR_REF, AMARK_LINK_LABEL);
    CHECK_TEXT(link, AMARK_LINK_LABEL, "\\]&amp;");
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(layout_tokens_are_decoded_in_the_order_of_the_lines)
{
    /* ```c&amp;\+ x
     * a
     *
     * ``` */
    amark_document_t *document = NEW_DOCUMENT("```c&amp;\\+ x\na\n\n```\n");
    amark_node_t *block = new_block(document, AMARK_NODE_CODE_BLOCK, 0, 4);

    layout_token(block, 0, 0, 3, AMARK_SPELL_MARKER, AMARK_FACET_NONE);
    layout_token(block, 0, 3, 4, AMARK_SPELL_TEXT, AMARK_CODE_BLOCK_INFO);
    layout_token(block, 0, 4, 9, AMARK_SPELL_CHAR_REF, AMARK_CODE_BLOCK_INFO);
    layout_token(block, 0, 9, 11, AMARK_SPELL_ESCAPE, AMARK_CODE_BLOCK_INFO);
    layout_token(block, 0, 11, 12, AMARK_SPELL_SPACE, AMARK_FACET_NONE);
    layout_token(block, 0, 12, 13, AMARK_SPELL_TEXT, AMARK_CODE_BLOCK_INFO);
    layout_token(block, 3, 0, 3, AMARK_SPELL_MARKER, AMARK_FACET_NONE);
    CHECK_TEXT(block, AMARK_CODE_BLOCK_INFO, "c&+x");
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(a_lines_facet_derived_from_tokens_ends_with_a_line_feed)
{
    amark_document_t *document = new_document_n("ab\tc\n", 5, true);
    amark_node_t *note = new_block(document, NOTE_TYPE, 0, 1);

    CHECK_TEXT(note, NOTE_BODY, "");
    /* The tab spans the columns [2, 4). */
    layout_token(note, 0, 1, 2, AMARK_SPELL_TEXT, NOTE_BODY);
    CHECK_TEXT(note, NOTE_BODY, "b\n");
    layout_token(note, 0, 2, 5, AMARK_SPELL_TEXT, NOTE_BODY);
    CHECK_TEXT(note, NOTE_BODY, "b\tc\n");
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(a_layout_token_cut_inside_a_tab_gives_spaces)
{
    /* ">\tx": the tab spans columns [1, 4). */
    amark_document_t *document = new_document_n(">\tx\n", 4, true);
    amark_node_t *note = new_block(document, NOTE_TYPE, 0, 1);

    layout_token(note, 0, 2, 5, AMARK_SPELL_TEXT, NOTE_BODY);
    CHECK_TEXT(note, NOTE_BODY, "  x\n");
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

/* ---- The order of the getter ---------------------------------------------------- */

NAOS_TEST(the_derived_value_comes_before_the_recorded_value)
{
    amark_document_t *document = NEW_DOCUMENT("ab\n");
    amark_node_t *paragraph = new_block(document, AMARK_NODE_PARAGRAPH, 0, 1);
    amark_node_t *text;

    add_content(paragraph, 0, 0, 2);
    text = new_child(paragraph, AMARK_NODE_TEXT);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_record_string(text, AMARK_TEXT_LITERAL, "zz", 2));
    CHECK_TEXT(text, AMARK_TEXT_LITERAL, "zz");
    token(text, paragraph, 0, 2, AMARK_SPELL_TEXT, AMARK_TEXT_LITERAL);
    CHECK_TEXT(text, AMARK_TEXT_LITERAL, "ab");
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(only_structural_tokens_derive_the_empty_string)
{
    amark_document_t *document = NEW_DOCUMENT("``\n");
    amark_node_t *paragraph = new_block(document, AMARK_NODE_PARAGRAPH, 0, 1);
    amark_node_t *text;

    add_content(paragraph, 0, 0, 2);
    text = new_child(paragraph, AMARK_NODE_TEXT);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_record_string(text, AMARK_TEXT_LITERAL, "zz", 2));
    token(text, paragraph, 0, 2, AMARK_SPELL_DELIMITER, AMARK_TEXT_LITERAL);
    CHECK_TEXT(text, AMARK_TEXT_LITERAL, "");
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

/* ---- CONTENT facets ------------------------------------------------------------------ */

NAOS_TEST(a_code_block_ending_with_an_empty_line_keeps_it)
{
    /* ```
     * a
     *
     * ``` */
    amark_document_t *document = NEW_DOCUMENT("```\na\n\n```\n");
    amark_node_t *block = new_block(document, AMARK_NODE_CODE_BLOCK, 0, 4);
    amark_node_t *empty = new_node(document, AMARK_NODE_CODE_BLOCK);
    amark_node_t *one = new_block(document, AMARK_NODE_CODE_BLOCK, 2, 1);

    CHECK_TEXT(block, AMARK_CODE_BLOCK_LITERAL, "");
    add_content(block, 1, 0, 1);
    CHECK_TEXT(block, AMARK_CODE_BLOCK_LITERAL, "a\n");
    add_content(block, 2, 0, 0);
    CHECK_TEXT(block, AMARK_CODE_BLOCK_LITERAL, "a\n\n");
    /* No content: the empty string; one empty line: one LF. */
    CHECK_TEXT(empty, AMARK_CODE_BLOCK_LITERAL, "");
    add_content(one, 2, 0, 0);
    CHECK_TEXT(one, AMARK_CODE_BLOCK_LITERAL, "\n");
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(input_3_3_the_literal_of_a_code_block_in_a_partial_tab)
{
    /* >\t\tfoo */
    amark_document_t *document = NEW_DOCUMENT(">\t\tfoo\n");
    amark_node_t *quote = new_block(document, AMARK_NODE_BLOCK_QUOTE, 0, 1);
    amark_node_t *block;
    amark_margin_t margin;

    memset(&margin, 0, sizeof(margin));
    margin.struct_size = sizeof(margin);
    margin.line = 0;
    margin.state = AMARK_MARGIN_PRESENT;
    margin.column_start = 0;
    margin.column_end = 2;
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_add_margin(quote, &margin));
    block = new_block(document, AMARK_NODE_CODE_BLOCK, 0, 1);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(quote, block));
    layout_token(block, 0, 2, 6, AMARK_SPELL_SPACE, AMARK_FACET_NONE);
    add_content(block, 0, 6, 11);
    token(block, block, 0, 2, AMARK_SPELL_TAB_REMAINDER, AMARK_CODE_BLOCK_LITERAL);
    token(block, block, 2, 5, AMARK_SPELL_TEXT, AMARK_CODE_BLOCK_LITERAL);
    CHECK_TEXT(block, AMARK_CODE_BLOCK_LITERAL, "  foo\n");
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(replaced_ranges_of_the_content_become_replacement_characters)
{
    amark_document_t *document = NEW_DOCUMENT("<a\0b\xFF>\n");
    amark_node_t *html = new_block(document, AMARK_NODE_HTML_BLOCK, 0, 1);

    add_content(html, 0, 0, 6);
    token(html, html, 2, 3, AMARK_SPELL_REPLACED, AMARK_HTML_BLOCK_LITERAL);
    token(html, html, 4, 5, AMARK_SPELL_REPLACED, AMARK_FACET_NONE);
    CHECK_TEXT(html, AMARK_HTML_BLOCK_LITERAL, "<a\xEF\xBF\xBD" "b\xEF\xBF\xBD>\n");
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(a_content_facet_is_derived_from_the_content_whatever_was_recorded)
{
    amark_document_t *document = NEW_DOCUMENT("x\n");
    amark_node_t *block = new_block(document, AMARK_NODE_CODE_BLOCK, 0, 1);

    add_content(block, 0, 0, 1);
    CHECK_TEXT(block, AMARK_CODE_BLOCK_LITERAL, "x\n");
    /* Free content replaces the value. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_set_free_content(block, "p\r\nq", 4));
    CHECK_TEXT(block, AMARK_CODE_BLOCK_LITERAL, "p\nq\n");
    /* Setting the literal makes new free content; the facet keeps no value. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_set_string(block, AMARK_CODE_BLOCK_LITERAL, "a\n\n", 3));
    CHECK_TEXT(block, AMARK_CODE_BLOCK_LITERAL, "a\n\n");
    NAOS_CHECK_INT_EQ(AMARK_CONTENT_FREE, amark_block_content_state(block));
    NAOS_CHECK_INT_EQ(AMARK_TREE_FACET_NONE, block->facets[AMARK_CODE_BLOCK_LITERAL - 1].state);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_set_string(block, AMARK_CODE_BLOCK_LITERAL, "\n", 1));
    CHECK_TEXT(block, AMARK_CODE_BLOCK_LITERAL, "\n");
    NAOS_CHECK_UINT_EQ(1, block->content->line_count);
    NAOS_CHECK_UINT_EQ(0, block->content->length);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_set_string(block, AMARK_CODE_BLOCK_LITERAL, "", 0));
    CHECK_TEXT(block, AMARK_CODE_BLOCK_LITERAL, "");
    NAOS_CHECK_UINT_EQ(0, block->content->line_count);
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(setting_a_content_facet_removes_its_tokens_and_marks_it_dropped)
{
    /* >\t\tfoo (INPUT.md 3.3) */
    amark_document_t *document = NEW_DOCUMENT(">\t\tfoo\n\0x\n");
    amark_node_t *block = new_block(document, AMARK_NODE_CODE_BLOCK, 0, 1);
    amark_node_t *html = new_block(document, AMARK_NODE_HTML_BLOCK, 1, 1);
    amark_tree_content_t *old;

    layout_token(block, 0, 2, 6, AMARK_SPELL_SPACE, AMARK_FACET_NONE);
    add_content(block, 0, 6, 11);
    token(block, block, 0, 2, AMARK_SPELL_TAB_REMAINDER, AMARK_CODE_BLOCK_LITERAL);
    token(block, block, 2, 5, AMARK_SPELL_TEXT, AMARK_CODE_BLOCK_LITERAL);
    CHECK_TEXT(block, AMARK_CODE_BLOCK_LITERAL, "  foo\n");
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_set_string(block, AMARK_CODE_BLOCK_LITERAL, "bar", 3));
    CHECK_TEXT(block, AMARK_CODE_BLOCK_LITERAL, "bar\n");
    NAOS_CHECK_UINT_EQ(0, amark_node_content_token_count(block));
    NAOS_CHECK_NULL(block->content_tokens);
    NAOS_CHECK_TRUE((block->dropped & (1u << AMARK_CODE_BLOCK_LITERAL)) != 0);
    /* The own spelling of the leaf stays. */
    NAOS_CHECK_UINT_EQ(1, amark_block_token_count(block));
    CHECK_TREE_OK(document);

    /* A token of no facet stays, on the old content; a record keeps the
     * tokens of the facet. */
    add_content(html, 1, 0, 2);
    token(html, html, 0, 1, AMARK_SPELL_REPLACED, AMARK_FACET_NONE);
    token(html, html, 1, 2, AMARK_SPELL_TEXT, AMARK_HTML_BLOCK_LITERAL);
    old = html->content;
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_record_string(html, AMARK_HTML_BLOCK_LITERAL, "<p>", 3));
    NAOS_CHECK_UINT_EQ(2, amark_node_content_token_count(html));
    NAOS_CHECK_UINT_EQ(0, html->dropped);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_set_string(html, AMARK_HTML_BLOCK_LITERAL, "<q>", 3));
    NAOS_CHECK_UINT_EQ(1, amark_node_content_token_count(html));
    NAOS_CHECK_TRUE(html->content_tokens->base == old);
    NAOS_CHECK_TRUE(html->content != old);
    CHECK_TEXT(html, AMARK_HTML_BLOCK_LITERAL, "<q>\n");
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(the_value_of_a_content_facet_is_normalized_to_lf)
{
    amark_tree_content_t *content = NULL;

    /* As for a CONTENT facet without LINES, which the setter does not
     * normalize. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_tree_content_from_value("a\r\nb\rc", 6, &content));
    AMARK_CHECK_BYTES_EQ(content->bytes, content->length, "a\nb\nc");
    NAOS_CHECK_UINT_EQ(3, content->line_count);
    NAOS_CHECK_INT_EQ(AMARK_CONTENT_FREE, content->state);
    amark_tree_content_release(content, true);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_tree_content_from_value("a\r\n", 3, &content));
    AMARK_CHECK_BYTES_EQ(content->bytes, content->length, "a");
    NAOS_CHECK_UINT_EQ(1, content->line_count);
    amark_tree_content_release(content, true);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_tree_content_from_value("\r", 1, &content));
    NAOS_CHECK_UINT_EQ(0, content->length);
    NAOS_CHECK_UINT_EQ(1, content->line_count);
    amark_tree_content_release(content, true);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_tree_content_from_value("x\r\r", 3, &content));
    AMARK_CHECK_BYTES_EQ(content->bytes, content->length, "x\n");
    NAOS_CHECK_UINT_EQ(2, content->line_count);
    amark_tree_content_release(content, true);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_tree_content_from_value("", 0, &content));
    NAOS_CHECK_UINT_EQ(0, content->length);
    NAOS_CHECK_UINT_EQ(0, content->line_count);
    amark_tree_content_release(content, true);
}

/* ---- The cache -------------------------------------------------------------------- */

NAOS_TEST(the_cache_is_dropped_when_the_tokens_change)
{
    amark_document_t *document = NEW_DOCUMENT("ab cd\n");
    amark_node_t *paragraph = new_block(document, AMARK_NODE_PARAGRAPH, 0, 1);
    amark_node_t *text;
    amark_node_t *other;
    const char *first;
    amark_value_t value;

    add_content(paragraph, 0, 0, 5);
    text = new_child(paragraph, AMARK_NODE_TEXT);
    other = new_child(paragraph, AMARK_NODE_TEXT);
    token(text, paragraph, 0, 2, AMARK_SPELL_TEXT, AMARK_TEXT_LITERAL);
    CHECK_TEXT(text, AMARK_TEXT_LITERAL, "ab");
    first = CACHE(text, AMARK_TEXT_LITERAL);
    NAOS_REQUIRE_NOT_NULL(first);
    /* The second get returns the cache. */
    value = get(text, AMARK_TEXT_LITERAL);
    NAOS_CHECK_PTR_EQ(first, value.text);
    CHECK_TREE_OK(document);

    /* Adding a token drops it. */
    token(text, paragraph, 2, 5, AMARK_SPELL_TEXT, AMARK_TEXT_LITERAL);
    NAOS_CHECK_NULL(CACHE(text, AMARK_TEXT_LITERAL));
    CHECK_TEXT(text, AMARK_TEXT_LITERAL, "ab cd");
    /* Splitting drops it (the value stays). */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_split_content_token(text, 1, 3));
    NAOS_CHECK_NULL(CACHE(text, AMARK_TEXT_LITERAL));
    CHECK_TEXT(text, AMARK_TEXT_LITERAL, "ab cd");
    /* Moving drops it on both nodes. */
    CHECK_TEXT(other, AMARK_TEXT_LITERAL, "");
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_move_content_tokens(text, other, 2, 1));
    NAOS_CHECK_NULL(CACHE(text, AMARK_TEXT_LITERAL));
    NAOS_CHECK_NULL(CACHE(other, AMARK_TEXT_LITERAL));
    CHECK_TEXT(text, AMARK_TEXT_LITERAL, "ab ");
    CHECK_TEXT(other, AMARK_TEXT_LITERAL, "cd");
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(the_cache_is_dropped_when_the_layout_tokens_or_the_content_change)
{
    amark_document_t *document = NEW_DOCUMENT("```ab\nx\ny\n```\n");
    amark_node_t *block = new_block(document, AMARK_NODE_CODE_BLOCK, 0, 4);

    layout_token(block, 0, 0, 3, AMARK_SPELL_MARKER, AMARK_FACET_NONE);
    layout_token(block, 0, 3, 4, AMARK_SPELL_TEXT, AMARK_CODE_BLOCK_INFO);
    CHECK_TEXT(block, AMARK_CODE_BLOCK_INFO, "a");
    layout_token(block, 0, 4, 5, AMARK_SPELL_TEXT, AMARK_CODE_BLOCK_INFO);
    NAOS_CHECK_NULL(CACHE(block, AMARK_CODE_BLOCK_INFO));
    CHECK_TEXT(block, AMARK_CODE_BLOCK_INFO, "ab");

    add_content(block, 1, 0, 1);
    CHECK_TEXT(block, AMARK_CODE_BLOCK_LITERAL, "x\n");
    NAOS_REQUIRE_NOT_NULL(CACHE(block, AMARK_CODE_BLOCK_LITERAL));
    add_content(block, 2, 0, 1);
    NAOS_CHECK_NULL(CACHE(block, AMARK_CODE_BLOCK_LITERAL));
    CHECK_TEXT(block, AMARK_CODE_BLOCK_LITERAL, "x\ny\n");
    /* Removing a line drops it too. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_remove_lines(block, 3, 1));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_remove_lines(block, 2, 1));
    CHECK_TEXT(block, AMARK_CODE_BLOCK_LITERAL, "x\n");
    /* A token of the leaf drops the literal (a replaced range would change it). */
    token(block, block, 0, 1, AMARK_SPELL_TEXT, AMARK_CODE_BLOCK_LITERAL);
    NAOS_CHECK_NULL(CACHE(block, AMARK_CODE_BLOCK_LITERAL));
    CHECK_TEXT(block, AMARK_CODE_BLOCK_LITERAL, "x\n");
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_set_free_content(block, "z", 1));
    NAOS_CHECK_NULL(CACHE(block, AMARK_CODE_BLOCK_LITERAL));
    CHECK_TEXT(block, AMARK_CODE_BLOCK_LITERAL, "z\n");
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

/* ---- Mixed tokens ------------------------------------------------------------------- */

NAOS_TEST(a_string_facet_is_not_spelled_by_layout_and_content_tokens_both)
{
    amark_document_t *document = NEW_DOCUMENT("```ab\nx\n```\n");
    amark_node_t *block = new_block(document, AMARK_NODE_CODE_BLOCK, 0, 3);
    amark_node_t *other = new_block(document, AMARK_NODE_CODE_BLOCK, 0, 3);

    layout_token(block, 0, 3, 4, AMARK_SPELL_TEXT, AMARK_CODE_BLOCK_INFO);
    add_content(block, 1, 0, 1);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      try_token(block, block, 0, 1, AMARK_SPELL_TEXT, AMARK_CODE_BLOCK_INFO));
    NAOS_CHECK_UINT_EQ(0, amark_node_content_token_count(block));

    add_content(other, 1, 0, 1);
    token(other, other, 0, 1, AMARK_SPELL_TEXT, AMARK_CODE_BLOCK_INFO);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      try_layout_token(other, 0, 3, 4, AMARK_SPELL_TEXT, AMARK_CODE_BLOCK_INFO));
    NAOS_CHECK_UINT_EQ(0, amark_block_token_count(other));
    /* Another facet, or a structural token, is fine. */
    layout_token(other, 0, 0, 3, AMARK_SPELL_MARKER, AMARK_FACET_NONE);
    CHECK_TEXT(other, AMARK_CODE_BLOCK_INFO, "x");
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

/* ---- The internal check ------------------------------------------------------------- */

NAOS_TEST(the_check_finds_a_stale_cache_and_a_stored_content_value)
{
    amark_document_t *document = NEW_DOCUMENT("ab\n");
    amark_node_t *paragraph = new_block(document, AMARK_NODE_PARAGRAPH, 0, 1);
    amark_node_t *block = new_block(document, AMARK_NODE_CODE_BLOCK, 0, 1);
    amark_node_t *text;
    amark_tree_check_report_t report;
    amark_tree_facet_t *slot;
    char saved;

    add_content(paragraph, 0, 0, 2);
    text = new_child(paragraph, AMARK_NODE_TEXT);
    token(text, paragraph, 0, 2, AMARK_SPELL_TEXT, AMARK_TEXT_LITERAL);
    CHECK_TEXT(text, AMARK_TEXT_LITERAL, "ab");
    CHECK_TREE_OK(document);

    /* A cache that differs from the derivation. */
    slot = &text->facets[AMARK_TEXT_LITERAL - 1];
    saved = slot->cache[1];
    slot->cache[1] = 'x';
    NAOS_CHECK_INT_EQ(AMARK_TREE_VIOLATION_FACETS, amark_tree_check(document, &report));
    NAOS_CHECK_TRUE(report.node == text);
    slot->cache[1] = saved;
    CHECK_TREE_OK(document);

    /* A cache of a facet that has nothing to derive from. */
    add_content(block, 0, 0, 2);
    CHECK_TEXT(block, AMARK_CODE_BLOCK_INFO, "");
    NAOS_CHECK_NULL(CACHE(block, AMARK_CODE_BLOCK_INFO));
    CHECK_TEXT(block, AMARK_CODE_BLOCK_LITERAL, "ab\n");
    CHECK_TREE_OK(document);
    slot = &block->facets[AMARK_CODE_BLOCK_INFO - 1];
    slot->cache = (char *)"";
    NAOS_CHECK_INT_EQ(AMARK_TREE_VIOLATION_FACETS, amark_tree_check(document, &report));
    NAOS_CHECK_TRUE(report.node == block);
    slot->cache = NULL;
    CHECK_TREE_OK(document);

    /* A token spells a facet whose spelling is dropped. */
    text->dropped |= 1u << AMARK_TEXT_LITERAL;
    NAOS_CHECK_INT_EQ(AMARK_TREE_VIOLATION_FACETS, amark_tree_check(document, &report));
    NAOS_CHECK_TRUE(report.node == text);
    text->dropped = 0;
    CHECK_TREE_OK(document);

    /* A CONTENT facet holds no stored value. */
    slot = &block->facets[AMARK_CODE_BLOCK_LITERAL - 1];
    slot->state = AMARK_TREE_FACET_RECORDED;
    slot->text = (char *)"";
    NAOS_CHECK_INT_EQ(AMARK_TREE_VIOLATION_FACETS, amark_tree_check(document, &report));
    NAOS_CHECK_TRUE(report.node == block);
    slot->state = AMARK_TREE_FACET_NONE;
    slot->text = NULL;
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

/* ---- Allocation failures ----------------------------------------------------------- */

/* Derives values under allocation failures: a failed get returns
 * AMARK_ERROR_OUT_OF_MEMORY and leaves no cache; the tree stays valid. */
static amark_error_t derive_oom_scenario(void *context)
{
    static const char input[] = "a&amp;\\*\n```\nx\n```\n";
    amark_document_t *document = NULL;
    amark_node_t *paragraph = NULL;
    amark_node_t *text = NULL;
    amark_node_t *block = NULL;
    amark_source_id_t source = 0;
    amark_content_token_t token_record;
    amark_value_t value;
    amark_error_t error;
    size_t i;

    (void)context;
    error = amark_document_create(NULL, &document);
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    error = amark_document_add_source(document, input, sizeof(input) - 1, AMARK_SOURCE_FINISHED,
                                      &source);
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_create(document, AMARK_NODE_PARAGRAPH, &paragraph);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_block_set_lines(paragraph, 0, 1);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_block_add_content(paragraph, 0, 0, 8);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_create(document, AMARK_NODE_TEXT, &text);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_append_child(paragraph, text);
    }
    for (i = 0; (error == AMARK_ERROR_NONE) && (i < 3); i++)
    {
        static const size_t bounds[4] = {0, 1, 6, 8};
        static const amark_spell_kind_t kinds[3] = {AMARK_SPELL_TEXT, AMARK_SPELL_CHAR_REF,
                                                    AMARK_SPELL_ESCAPE};

        memset(&token_record, 0, sizeof(token_record));
        token_record.struct_size = sizeof(token_record);
        token_record.start = bounds[i];
        token_record.end = bounds[i + 1];
        token_record.kind = kinds[i];
        token_record.facet = AMARK_TEXT_LITERAL;
        error = amark_node_add_content_token(text, paragraph, &token_record);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_create(document, AMARK_NODE_CODE_BLOCK, &block);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_block_set_lines(block, 1, 3);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_block_add_content(block, 2, 0, 1);
    }
    /* Derive each value twice: the second get reads the cache. */
    for (i = 0; (error == AMARK_ERROR_NONE) && (i < 4); i++)
    {
        const amark_node_t *node = ((i % 2) == 0) ? text : block;
        amark_facet_t facet = ((i % 2) == 0) ? AMARK_TEXT_LITERAL : AMARK_CODE_BLOCK_LITERAL;

        value.struct_size = sizeof(value);
        error = amark_node_get(node, facet, &value);
        if ((error != AMARK_ERROR_NONE) && (node->facets[facet - 1].cache != NULL))
        {
            error = AMARK_ERROR_INVALID_STATE;
        }
        if ((error == AMARK_ERROR_NONE) &&
            (((i % 2) == 0) ? ((value.length != 3) || (memcmp(value.text, "a&*", 3) != 0))
                            : ((value.length != 2) || (memcmp(value.text, "x\n", 2) != 0))))
        {
            error = AMARK_ERROR_INVALID_STATE;
        }
    }
    if ((error == AMARK_ERROR_NONE) || (error == AMARK_ERROR_OUT_OF_MEMORY))
    {
        if (amark_tree_check(document, NULL) != AMARK_TREE_VIOLATION_NONE)
        {
            error = AMARK_ERROR_INVALID_STATE;
        }
    }
    amark_document_destroy(document);
    return error;
}

NAOS_TEST(out_of_memory_in_a_derivation_leaves_no_cache)
{
    AMARK_CHECK_OOM_SWEEP(derive_oom_scenario, NULL);
}

/* Sets a CONTENT facet under allocation failures: the leaf keeps its content. */
static amark_error_t set_content_oom_scenario(void *context)
{
    amark_document_t *document = NULL;
    amark_node_t *block = NULL;
    amark_source_id_t source = 0;
    amark_content_token_t token_record;
    amark_value_t value;
    amark_error_t error;

    (void)context;
    error = amark_document_create(NULL, &document);
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_document_add_source(document, "old\n", 4, AMARK_SOURCE_FINISHED, &source);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_create(document, AMARK_NODE_CODE_BLOCK, &block);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_block_set_lines(block, 0, 1);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_block_add_content(block, 0, 0, 3);
    }
    if (error == AMARK_ERROR_NONE)
    {
        memset(&token_record, 0, sizeof(token_record));
        token_record.struct_size = sizeof(token_record);
        token_record.start = 0;
        token_record.end = 3;
        token_record.kind = AMARK_SPELL_TEXT;
        token_record.facet = AMARK_CODE_BLOCK_LITERAL;
        error = amark_node_add_content_token(block, block, &token_record);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_set_string(block, AMARK_CODE_BLOCK_LITERAL, "a\r\nb", 4);
        if ((error != AMARK_ERROR_NONE) &&
            ((block->content == NULL) || (block->content->length != 3) ||
             (amark_node_content_token_count(block) != 1) || (block->dropped != 0)))
        {
            error = AMARK_ERROR_INVALID_STATE;
        }
        if ((error == AMARK_ERROR_NONE) && ((amark_node_content_token_count(block) != 0) ||
                                            (block->content_tokens != NULL)))
        {
            error = AMARK_ERROR_INVALID_STATE;
        }
    }
    if (error == AMARK_ERROR_NONE)
    {
        value.struct_size = sizeof(value);
        error = amark_node_get(block, AMARK_CODE_BLOCK_LITERAL, &value);
        if ((error == AMARK_ERROR_NONE) &&
            ((value.length != 4) || (memcmp(value.text, "a\nb\n", 4) != 0)))
        {
            error = AMARK_ERROR_INVALID_STATE;
        }
    }
    if ((document != NULL) && (amark_tree_check(document, NULL) != AMARK_TREE_VIOLATION_NONE))
    {
        error = AMARK_ERROR_INVALID_STATE;
    }
    amark_document_destroy(document);
    return error;
}

NAOS_TEST(out_of_memory_in_setting_a_content_facet_changes_nothing)
{
    AMARK_CHECK_OOM_SWEEP(set_content_oom_scenario, NULL);
}

/* ---- Dropping the spelling (6.7, 6.8, 6.9, 6.12, 6.13, 8.7) ------------------------- */

/* The tokens of a facet of node (layout and content tokens). */
static size_t facet_tokens(const amark_node_t *node, amark_facet_t facet)
{
    size_t count = 0;
    size_t i;

    if (node->records != NULL)
    {
        for (i = 0; i < node->records->token_count; i++)
        {
            count += (node->records->tokens[i].facet == facet) ? 1u : 0u;
        }
    }
    if (node->content_tokens != NULL)
    {
        for (i = 0; i < node->content_tokens->count; i++)
        {
            count += (node->content_tokens->items[i].facet == facet) ? 1u : 0u;
        }
    }
    return count;
}

#define DROPPED(node, facet) (((node)->dropped & (1u << (facet))) != 0)

/*
 * [a *b*](/u "t") c
 * 0123456789012345678
 * A paragraph whose content is the line, with a link (its brackets,
 * parentheses, destination, title and title quotes), a text "a " and an emph
 * (its delimiters and a text "b") in the link, and a text " c" after it.
 */
typedef struct link_fixture
{
    amark_document_t *document;
    amark_node_t *paragraph;
    amark_node_t *link;
    amark_node_t *inner;
    amark_node_t *emph;
    amark_node_t *emphasized;
    amark_node_t *after;
} link_fixture_t;

static link_fixture_t new_link_fixture(void)
{
    link_fixture_t f;

    f.document = NEW_DOCUMENT("[a *b*](/u \"t\") c\n");
    f.paragraph = new_block(f.document, AMARK_NODE_PARAGRAPH, 0, 1);
    add_content(f.paragraph, 0, 0, 17);
    f.link = new_child(f.paragraph, AMARK_NODE_LINK);
    token(f.link, f.paragraph, 0, 1, AMARK_SPELL_DELIMITER, AMARK_FACET_NONE);
    f.inner = new_child(f.link, AMARK_NODE_TEXT);
    token(f.inner, f.paragraph, 1, 3, AMARK_SPELL_TEXT, AMARK_TEXT_LITERAL);
    f.emph = new_child(f.link, AMARK_NODE_EMPH);
    token(f.emph, f.paragraph, 3, 4, AMARK_SPELL_DELIMITER, AMARK_EMPH_DELIMITER);
    f.emphasized = new_child(f.emph, AMARK_NODE_TEXT);
    token(f.emphasized, f.paragraph, 4, 5, AMARK_SPELL_TEXT, AMARK_TEXT_LITERAL);
    token(f.emph, f.paragraph, 5, 6, AMARK_SPELL_DELIMITER, AMARK_EMPH_DELIMITER);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_record_int(f.emph, AMARK_EMPH_DELIMITER,
                                                                AMARK_EMPH_DELIMITER_ASTERISK));
    token(f.link, f.paragraph, 6, 7, AMARK_SPELL_DELIMITER, AMARK_FACET_NONE);
    token(f.link, f.paragraph, 7, 8, AMARK_SPELL_DELIMITER, AMARK_FACET_NONE);
    token(f.link, f.paragraph, 8, 10, AMARK_SPELL_TEXT, AMARK_LINK_DESTINATION);
    token(f.link, f.paragraph, 10, 11, AMARK_SPELL_SPACE, AMARK_FACET_NONE);
    token(f.link, f.paragraph, 11, 12, AMARK_SPELL_DELIMITER, AMARK_LINK_TITLE_QUOTE);
    token(f.link, f.paragraph, 12, 13, AMARK_SPELL_TEXT, AMARK_LINK_TITLE);
    token(f.link, f.paragraph, 13, 14, AMARK_SPELL_DELIMITER, AMARK_LINK_TITLE_QUOTE);
    token(f.link, f.paragraph, 14, 15, AMARK_SPELL_DELIMITER, AMARK_FACET_NONE);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_record_int(f.link, AMARK_LINK_FORM, AMARK_LINK_FORM_INLINE));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_record_int(f.link, AMARK_LINK_TITLE_QUOTE,
                                              AMARK_LINK_TITLE_QUOTE_DOUBLE));
    f.after = new_child(f.paragraph, AMARK_NODE_TEXT);
    token(f.after, f.paragraph, 15, 17, AMARK_SPELL_TEXT, AMARK_TEXT_LITERAL);
    return f;
}

NAOS_TEST(changing_a_link_destination_keeps_the_other_spelling)
{
    link_fixture_t f = new_link_fixture();

    CHECK_TEXT(f.link, AMARK_LINK_DESTINATION, "/u");
    CHECK_TEXT(f.link, AMARK_LINK_TITLE, "t");
    NAOS_CHECK_UINT_EQ(9, amark_node_content_token_count(f.link));

    /* The same value keeps the spelling (6.9). */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_set_string(f.link, AMARK_LINK_DESTINATION, "/u", 2));
    NAOS_CHECK_UINT_EQ(9, amark_node_content_token_count(f.link));
    NAOS_CHECK_UINT_EQ(0, f.link->dropped);

    /* A new destination drops only the tokens of the destination (6.7). */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_set_string(f.link, AMARK_LINK_DESTINATION, "/v", 2));
    CHECK_TEXT(f.link, AMARK_LINK_DESTINATION, "/v");
    NAOS_CHECK_UINT_EQ(8, amark_node_content_token_count(f.link));
    NAOS_CHECK_UINT_EQ(0, facet_tokens(f.link, AMARK_LINK_DESTINATION));
    NAOS_CHECK_UINT_EQ(5, facet_tokens(f.link, AMARK_FACET_NONE));
    NAOS_CHECK_UINT_EQ(2, facet_tokens(f.link, AMARK_LINK_TITLE_QUOTE));
    NAOS_CHECK_UINT_EQ(1, facet_tokens(f.link, AMARK_LINK_TITLE));
    NAOS_CHECK_UINT_EQ(1u << AMARK_LINK_DESTINATION, f.link->dropped);
    CHECK_TEXT(f.link, AMARK_LINK_TITLE, "t");
    /* The link text and the neighbours keep their spelling. */
    NAOS_CHECK_UINT_EQ(1, amark_node_content_token_count(f.inner));
    NAOS_CHECK_UINT_EQ(2, amark_node_content_token_count(f.emph));
    NAOS_CHECK_UINT_EQ(1, amark_node_content_token_count(f.emphasized));
    NAOS_CHECK_UINT_EQ(1, amark_node_content_token_count(f.after));
    NAOS_CHECK_UINT_EQ(0, f.inner->dropped | f.emph->dropped | f.after->dropped);
    NAOS_CHECK_INT_EQ(AMARK_CONTENT_LAID, amark_block_content_state(f.paragraph));
    CHECK_TREE_OK(f.document);

    /* The title derived from its tokens: the same value keeps them. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_string(f.link, AMARK_LINK_TITLE, "t", 1));
    NAOS_CHECK_UINT_EQ(1, facet_tokens(f.link, AMARK_LINK_TITLE));

    /* A new style drops only the tokens of the style, and keeps the title
     * (6.8). */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_set_int(f.link, AMARK_LINK_TITLE_QUOTE,
                                           AMARK_LINK_TITLE_QUOTE_SINGLE));
    NAOS_CHECK_UINT_EQ(0, facet_tokens(f.link, AMARK_LINK_TITLE_QUOTE));
    NAOS_CHECK_UINT_EQ(1, facet_tokens(f.link, AMARK_LINK_TITLE));
    NAOS_CHECK_TRUE(DROPPED(f.link, AMARK_LINK_TITLE_QUOTE));
    CHECK_TEXT(f.link, AMARK_LINK_TITLE, "t");

    /* Unsetting a style drops its spelling. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_unset(f.emph, AMARK_EMPH_DELIMITER));
    NAOS_CHECK_UINT_EQ(0, amark_node_content_token_count(f.emph));
    NAOS_CHECK_NULL(f.emph->content_tokens);
    NAOS_CHECK_UINT_EQ(1u << AMARK_EMPH_DELIMITER, f.emph->dropped);
    NAOS_CHECK_UINT_EQ(1, amark_node_content_token_count(f.emphasized));
    /* A record keeps the spelling (6.14). */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_record_string(f.link, AMARK_LINK_TITLE, "x", 1));
    NAOS_CHECK_UINT_EQ(1, facet_tokens(f.link, AMARK_LINK_TITLE));
    NAOS_CHECK_FALSE(DROPPED(f.link, AMARK_LINK_TITLE));
    CHECK_TREE_OK(f.document);
    amark_document_destroy(f.document);
}

NAOS_TEST(setting_a_layout_spelled_facet_drops_only_its_layout_tokens)
{
    /* ```py
     * x
     * ``` */
    amark_document_t *document = NEW_DOCUMENT("```py\nx\n```\n");
    amark_node_t *block = new_block(document, AMARK_NODE_CODE_BLOCK, 0, 3);

    layout_token(block, 0, 0, 3, AMARK_SPELL_DELIMITER, AMARK_CODE_BLOCK_FENCE_CHAR);
    layout_token(block, 0, 3, 5, AMARK_SPELL_TEXT, AMARK_CODE_BLOCK_INFO);
    layout_token(block, 2, 0, 3, AMARK_SPELL_DELIMITER, AMARK_CODE_BLOCK_FENCE_CHAR);
    add_content(block, 1, 0, 1);
    CHECK_TEXT(block, AMARK_CODE_BLOCK_INFO, "py");
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_set_string(block, AMARK_CODE_BLOCK_INFO, "c", 1));
    CHECK_TEXT(block, AMARK_CODE_BLOCK_INFO, "c");
    NAOS_CHECK_UINT_EQ(2, amark_block_token_count(block));
    NAOS_CHECK_UINT_EQ(2, facet_tokens(block, AMARK_CODE_BLOCK_FENCE_CHAR));
    NAOS_CHECK_UINT_EQ(1u << AMARK_CODE_BLOCK_INFO, block->dropped);
    /* The content keeps its pieces. */
    NAOS_CHECK_INT_EQ(AMARK_CONTENT_LAID, amark_block_content_state(block));
    CHECK_TEXT(block, AMARK_CODE_BLOCK_LITERAL, "x\n");
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(literals_of_content_facets_round_trip)
{
    static const char *const values[] = {"", "\n", "a", "a\n\n"};
    static const char *const expected[] = {"", "\n", "a\n", "a\n\n"};
    static const size_t lines[] = {0, 1, 1, 2};
    amark_document_t *document = NEW_DOCUMENT("```\nold\n```\n");
    amark_node_t *block = new_block(document, AMARK_NODE_CODE_BLOCK, 0, 3);
    amark_node_t *html = new_block(document, AMARK_NODE_HTML_BLOCK, 1, 1);
    size_t i;

    layout_token(block, 0, 0, 3, AMARK_SPELL_DELIMITER, AMARK_CODE_BLOCK_FENCE_CHAR);
    add_content(block, 1, 0, 3);
    add_content(html, 1, 0, 3);
    for (i = 0; i < 4; i++)
    {
        amark_value_t value;

        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                            amark_node_set_string(block, AMARK_CODE_BLOCK_LITERAL, values[i],
                                                  strlen(values[i])));
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                            amark_node_set_string(html, AMARK_HTML_BLOCK_LITERAL, values[i],
                                                  strlen(values[i])));
        value = get(block, AMARK_CODE_BLOCK_LITERAL);
        NAOS_CHECK_STR_EQ(expected[i], value.text);
        NAOS_CHECK_UINT_EQ(strlen(expected[i]), value.length);
        value = get(html, AMARK_HTML_BLOCK_LITERAL);
        NAOS_CHECK_STR_EQ(expected[i], value.text);
        NAOS_CHECK_UINT_EQ(lines[i], block->content->line_count);
        NAOS_CHECK_UINT_EQ(lines[i], html->content->line_count);
        NAOS_CHECK_INT_EQ(AMARK_CONTENT_FREE, amark_block_content_state(block));
        NAOS_CHECK_UINT_EQ(0, amark_node_content_token_count(block));
        /* The fence line stays; the literal is marked as dropped. */
        NAOS_CHECK_UINT_EQ(1, amark_block_token_count(block));
        NAOS_CHECK_TRUE(DROPPED(block, AMARK_CODE_BLOCK_LITERAL));
        CHECK_TREE_OK(document);
    }
    /* Each value read back sets the same content again. */
    for (i = 0; i < 4; i++)
    {
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                            amark_node_set_string(block, AMARK_CODE_BLOCK_LITERAL, expected[i],
                                                  strlen(expected[i])));
        NAOS_CHECK_STR_EQ(expected[i], get(block, AMARK_CODE_BLOCK_LITERAL).text);
        NAOS_CHECK_UINT_EQ(lines[i], block->content->line_count);
    }
    amark_document_destroy(document);
}

NAOS_TEST(discarding_the_spelling_of_a_facet_keeps_its_value)
{
    link_fixture_t f = new_link_fixture();
    amark_document_t *document = NEW_DOCUMENT("<a\0b>\n");
    amark_node_t *html = new_block(document, AMARK_NODE_HTML_BLOCK, 0, 1);
    amark_tree_content_t *old;
    size_t steps;

    /* A STRING facet derived from its tokens keeps the value as set. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_discard_facet_spelling(f.link, AMARK_LINK_TITLE));
    CHECK_TEXT(f.link, AMARK_LINK_TITLE, "t");
    NAOS_CHECK_INT_EQ(AMARK_TREE_FACET_SET, f.link->facets[AMARK_LINK_TITLE - 1].state);
    NAOS_CHECK_UINT_EQ(0, facet_tokens(f.link, AMARK_LINK_TITLE));
    NAOS_CHECK_UINT_EQ(2, facet_tokens(f.link, AMARK_LINK_TITLE_QUOTE));
    NAOS_CHECK_UINT_EQ(1, facet_tokens(f.link, AMARK_LINK_DESTINATION));
    NAOS_CHECK_UINT_EQ(1u << AMARK_LINK_TITLE, f.link->dropped);
    /* A style keeps its recorded value. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_discard_facet_spelling(f.emph, AMARK_EMPH_DELIMITER));
    NAOS_CHECK_UINT_EQ(0, amark_node_content_token_count(f.emph));
    NAOS_CHECK_INT_EQ(AMARK_EMPH_DELIMITER_ASTERISK, get(f.emph, AMARK_EMPH_DELIMITER).number);
    /* Again: nothing left to drop. */
    steps = f.document->stats.invalidation_steps;
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_discard_facet_spelling(f.emph, AMARK_EMPH_DELIMITER));
    NAOS_CHECK_UINT_EQ(steps, f.document->stats.invalidation_steps);
    /* Refused. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_discard_facet_spelling(NULL, AMARK_LINK_TITLE));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_discard_facet_spelling(f.link, AMARK_FACET_NONE));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_discard_facet_spelling(f.link, 7));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_discard_facet_spelling(f.paragraph, 1));
    CHECK_TREE_OK(f.document);
    amark_document_destroy(f.document);

    /* A CONTENT facet keeps its value in new free content. */
    add_content(html, 0, 0, 5);
    token(html, html, 2, 3, AMARK_SPELL_REPLACED, AMARK_HTML_BLOCK_LITERAL);
    old = html->content;
    CHECK_TEXT(html, AMARK_HTML_BLOCK_LITERAL, "<a\xEF\xBF\xBD" "b>\n");
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_discard_facet_spelling(html, AMARK_HTML_BLOCK_LITERAL));
    NAOS_CHECK_TRUE(html->content != old);
    NAOS_CHECK_INT_EQ(AMARK_CONTENT_FREE, amark_block_content_state(html));
    NAOS_CHECK_UINT_EQ(0, amark_node_content_token_count(html));
    CHECK_TEXT(html, AMARK_HTML_BLOCK_LITERAL, "<a\xEF\xBF\xBD" "b>\n");
    NAOS_CHECK_TRUE(DROPPED(html, AMARK_HTML_BLOCK_LITERAL));
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(discarding_all_spelling_keeps_the_values_and_frees_the_content)
{
    link_fixture_t f = new_link_fixture();
    amark_tree_content_t *old = f.paragraph->content;
    const char *bytes = NULL;
    size_t length = 0;
    amark_content_state_t state = AMARK_CONTENT_NONE;
    bool own = false;

    /* The link: its derived values become set values, its tokens go, its
     * children keep theirs. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_discard_spelling(f.link));
    NAOS_CHECK_NULL(f.link->content_tokens);
    CHECK_TEXT(f.link, AMARK_LINK_DESTINATION, "/u");
    CHECK_TEXT(f.link, AMARK_LINK_TITLE, "t");
    NAOS_CHECK_INT_EQ(AMARK_TREE_FACET_SET, f.link->facets[AMARK_LINK_DESTINATION - 1].state);
    NAOS_CHECK_INT_EQ(AMARK_LINK_TITLE_QUOTE_DOUBLE, get(f.link, AMARK_LINK_TITLE_QUOTE).number);
    NAOS_CHECK_INT_EQ(AMARK_LINK_FORM_INLINE, get(f.link, AMARK_LINK_FORM).number);
    /* The structure and the facets that had tokens are marked, not the
     * others (form, label, angle). */
    NAOS_CHECK_UINT_EQ(AMARK_TREE_DROPPED_STRUCTURE | (1u << AMARK_LINK_DESTINATION) |
                           (1u << AMARK_LINK_TITLE) | (1u << AMARK_LINK_TITLE_QUOTE),
                       f.link->dropped);
    NAOS_CHECK_UINT_EQ(1, amark_node_content_token_count(f.inner));
    NAOS_CHECK_UINT_EQ(2, amark_node_content_token_count(f.emph));
    CHECK_TREE_OK(f.document);

    /* The paragraph: its laid content becomes free content with the same bytes,
     * and the tokens of its descendants move to it. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_discard_spelling(f.paragraph));
    NAOS_CHECK_TRUE(f.paragraph->content != old);
    NAOS_CHECK_INT_EQ(AMARK_CONTENT_FREE, amark_block_content_state(f.paragraph));
    NAOS_CHECK_UINT_EQ(0, amark_block_content_piece_count(f.paragraph));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_block_content_bytes(f.paragraph, &bytes, &length));
    AMARK_CHECK_BYTES_EQ(bytes, length, "[a *b*](/u \"t\") c");
    NAOS_CHECK_UINT_EQ(1, f.paragraph->content->line_count);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_content_base(f.emphasized, &bytes, &length, &state, &own));
    NAOS_CHECK_INT_EQ(AMARK_CONTENT_FREE, state);
    NAOS_CHECK_TRUE(own);
    NAOS_CHECK_TRUE(f.after->content_tokens->base == f.paragraph->content);
    CHECK_TEXT(f.after, AMARK_TEXT_LITERAL, " c");
    CHECK_TEXT(f.emphasized, AMARK_TEXT_LITERAL, "b");
    /* The line range stays. */
    NAOS_CHECK_UINT_EQ(1, f.paragraph->line_count);
    NAOS_CHECK_UINT_EQ(AMARK_TREE_DROPPED_STRUCTURE, f.paragraph->dropped);
    CHECK_TREE_OK(f.document);
    amark_document_destroy(f.document);
}

NAOS_TEST(discarding_all_spelling_of_containers_and_code_blocks)
{
    /* > ```
     * > a\0
     * >
     * (the code block has no closing fence) */
    amark_document_t *document = NEW_DOCUMENT("> ```\n> a\0\n>\n");
    amark_node_t *quote = new_block(document, AMARK_NODE_BLOCK_QUOTE, 0, 3);
    amark_node_t *block = new_block(document, AMARK_NODE_CODE_BLOCK, 0, 2);
    amark_margin_t margin;
    amark_blank_line_t blank;
    uint32_t line;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(quote, block));
    for (line = 0; line < 3; line++)
    {
        memset(&margin, 0, sizeof(margin));
        margin.struct_size = sizeof(margin);
        margin.line = line;
        margin.state = AMARK_MARGIN_PRESENT;
        margin.column_start = 0;
        margin.column_end = (line < 2) ? 2 : 1;
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_add_margin(quote, &margin));
    }
    memset(&blank, 0, sizeof(blank));
    blank.struct_size = sizeof(blank);
    blank.line = 2;
    blank.column_start = 1;
    blank.column_end = 1;
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_add_blank_line(quote, &blank));
    layout_token(block, 0, 2, 5, AMARK_SPELL_DELIMITER, AMARK_CODE_BLOCK_FENCE_CHAR);
    add_content(block, 1, 2, 4);
    token(block, block, 1, 2, AMARK_SPELL_REPLACED, AMARK_CODE_BLOCK_LITERAL);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_record_int(block, AMARK_CODE_BLOCK_FORM,
                                              AMARK_CODE_BLOCK_FORM_FENCED));
    CHECK_TEXT(block, AMARK_CODE_BLOCK_LITERAL, "a\xEF\xBF\xBD\n");

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_discard_spelling(quote));
    NAOS_CHECK_UINT_EQ(0, amark_block_margin_count(quote));
    NAOS_CHECK_UINT_EQ(0, amark_block_blank_line_count(quote));
    NAOS_CHECK_UINT_EQ(3, quote->line_count);
    NAOS_CHECK_UINT_EQ(AMARK_TREE_DROPPED_STRUCTURE, quote->dropped);
    /* The child keeps its spelling. */
    NAOS_CHECK_UINT_EQ(1, amark_block_token_count(block));
    NAOS_CHECK_INT_EQ(AMARK_CONTENT_LAID, amark_block_content_state(block));
    CHECK_TREE_OK(document);

    /* The code block keeps its literal (the replaced byte as U+FFFD) in free
     * content, and its style. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_discard_spelling(block));
    NAOS_CHECK_UINT_EQ(0, amark_block_token_count(block));
    NAOS_CHECK_UINT_EQ(0, amark_node_content_token_count(block));
    NAOS_CHECK_INT_EQ(AMARK_CONTENT_FREE, amark_block_content_state(block));
    CHECK_TEXT(block, AMARK_CODE_BLOCK_LITERAL, "a\xEF\xBF\xBD\n");
    NAOS_CHECK_INT_EQ(AMARK_CODE_BLOCK_FORM_FENCED, get(block, AMARK_CODE_BLOCK_FORM).number);
    NAOS_CHECK_UINT_EQ(AMARK_TREE_DROPPED_STRUCTURE | (1u << AMARK_CODE_BLOCK_LITERAL) |
                           (1u << AMARK_CODE_BLOCK_FENCE_CHAR),
                       block->dropped);
    NAOS_CHECK_FALSE(DROPPED(block, AMARK_CODE_BLOCK_FORM));
    CHECK_TREE_OK(document);

    /* Again: nothing changes but the marks, which are already there. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_discard_spelling(block));
    CHECK_TEXT(block, AMARK_CODE_BLOCK_LITERAL, "a\xEF\xBF\xBD\n");
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_discard_spelling(NULL));
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(discarding_all_spelling_of_a_list_or_its_item_keeps_tight)
{
    /* - a
     *
     * - b
     * The tight value of a list is derived from the blank lines that dropping
     * the spelling removes: it becomes an override with the value it had.
     * Until Verbatim derives tight (12.3) the derived value is the default
     * (tight); 12.3 must make this test check a loose list. */
    amark_document_t *document = NEW_DOCUMENT("- a\n\n- b\n");
    amark_node_t *list = new_block(document, AMARK_NODE_LIST, 0, 3);
    amark_node_t *item = new_block(document, AMARK_NODE_ITEM, 0, 2);
    amark_node_t *other = new_block(document, AMARK_NODE_LIST, 2, 1);
    amark_node_t *paragraph = new_block(document, AMARK_NODE_PARAGRAPH, 0, 1);
    amark_blank_line_t blank;
    int64_t tight = get(list, AMARK_LIST_TIGHT).number;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(list, item));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(item, paragraph));
    memset(&blank, 0, sizeof(blank));
    blank.struct_size = sizeof(blank);
    blank.line = 1;
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_add_blank_line(item, &blank));

    /* A paragraph in the item leaves the list alone. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_discard_spelling(paragraph));
    NAOS_CHECK_INT_EQ(AMARK_TREE_FACET_NONE, list->facets[AMARK_LIST_TIGHT - 1].state);
    /* An item of the list: the parent list takes the override. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_discard_spelling(item));
    NAOS_CHECK_UINT_EQ(0, amark_block_blank_line_count(item));
    NAOS_CHECK_INT_EQ(AMARK_TREE_FACET_SET, list->facets[AMARK_LIST_TIGHT - 1].state);
    NAOS_CHECK_INT_EQ(tight, get(list, AMARK_LIST_TIGHT).number);
    NAOS_CHECK_UINT_EQ(0, list->dropped);
    /* A list itself. */
    tight = get(other, AMARK_LIST_TIGHT).number;
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_discard_spelling(other));
    NAOS_CHECK_INT_EQ(AMARK_TREE_FACET_SET, other->facets[AMARK_LIST_TIGHT - 1].state);
    NAOS_CHECK_INT_EQ(tight, get(other, AMARK_LIST_TIGHT).number);
    NAOS_CHECK_UINT_EQ(AMARK_TREE_DROPPED_STRUCTURE, other->dropped);
    /* An existing override is kept as it is. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_int(list, AMARK_LIST_TIGHT, 0));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_discard_spelling(list));
    NAOS_CHECK_INT_EQ(0, get(list, AMARK_LIST_TIGHT).number);
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

/* The types of the children of node, as a string ("t" text, "s" softbreak,
 * "?" other), and checks that the text children have the literals in order. */
static void check_inline_children(const amark_node_t *node, const char *types,
                                  const char *const *literals)
{
    const amark_node_t *child;
    size_t i = 0;
    size_t text = 0;

    for (child = node->first_child; child != NULL; child = child->next, i++)
    {
        NAOS_REQUIRE_TRUE(types[i] != '\0');
        if (types[i] == 't')
        {
            NAOS_REQUIRE_INT_EQ(AMARK_NODE_TEXT, child->type);
            NAOS_CHECK_STR_EQ(literals[text], get(child, AMARK_TEXT_LITERAL).text);
            NAOS_CHECK_INT_EQ(AMARK_TREE_FACET_SET,
                              child->facets[AMARK_TEXT_LITERAL - 1].state);
            text++;
        }
        else
        {
            NAOS_REQUIRE_INT_EQ(AMARK_NODE_SOFTBREAK, child->type);
        }
        NAOS_CHECK_NULL(child->content_tokens);
        NAOS_CHECK_UINT_EQ(0, child->dropped);
    }
    NAOS_CHECK_INT_EQ('\0', types[i]);
}

NAOS_TEST(set_inline_text_places_texts_and_softbreaks)
{
    static const char *const abcd[] = {"a", "b", "c", "d"};
    static const char *const one[] = {"x y"};
    link_fixture_t f = new_link_fixture();
    amark_node_t *heading = new_node(f.document, AMARK_NODE_HEADING);
    amark_node_t *block = new_node(f.document, AMARK_NODE_CODE_BLOCK);
    size_t nodes = f.document->node_count;
    const char *bytes = NULL;
    size_t length = 0;

    /* The children of the paragraph (the link, its 3 descendants, the text after)
     * are destroyed; 4 texts and 4 softbreaks take their place. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_set_inline_text(f.paragraph, "a\r\nb\rc\n\nd", 9));
    check_inline_children(f.paragraph, "tststsst", abcd);
    NAOS_CHECK_UINT_EQ(nodes - 5 + 8, f.document->node_count);
    /* The paragraph keeps its content and its line range. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_block_content_bytes(f.paragraph, &bytes, &length));
    AMARK_CHECK_BYTES_EQ(bytes, length, "[a *b*](/u \"t\") c");
    CHECK_TREE_OK(f.document);

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_inline_text(heading, "x y", 3));
    check_inline_children(heading, "t", one);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_inline_text(heading, "\n", 1));
    check_inline_children(heading, "s", NULL);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_inline_text(heading, NULL, 0));
    check_inline_children(heading, "", NULL);

    /* Refused, leaving the children. */
    nodes = f.document->node_count;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_set_inline_text(f.paragraph, "a\0b", 3));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_set_inline_text(f.paragraph, "a\xFF", 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_set_inline_text(f.paragraph, NULL, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_inline_text(NULL, "a", 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_inline_text(block, "a", 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_set_inline_text(f.document->root, "a", 1));
    check_inline_children(f.paragraph, "tststsst", abcd);
    NAOS_CHECK_UINT_EQ(nodes, f.document->node_count);
    CHECK_TREE_OK(f.document);
    amark_document_destroy(f.document);
}

/* The facet edits under allocation failures: a failure changes nothing. */
static amark_error_t discard_oom_scenario(void *context)
{
    static const char source[] = "[a](/u \"t\")\n```\nx\0\n```\n";
    amark_document_t *document = NULL;
    amark_node_t *paragraph = NULL;
    amark_node_t *link = NULL;
    amark_node_t *text = NULL;
    amark_node_t *block = NULL;
    amark_source_id_t id = 0;
    amark_content_token_t record;
    amark_layout_token_t layout;
    static const struct
    {
        int on_block;
        size_t start, end;
        amark_spell_kind_t kind;
        amark_facet_t facet;
    } tokens[] = {
        {0, 0, 1, AMARK_SPELL_DELIMITER, AMARK_FACET_NONE},
        {0, 2, 4, AMARK_SPELL_DELIMITER, AMARK_FACET_NONE},
        {0, 4, 6, AMARK_SPELL_TEXT, AMARK_LINK_DESTINATION},
        {0, 7, 8, AMARK_SPELL_DELIMITER, AMARK_LINK_TITLE_QUOTE},
        {0, 8, 9, AMARK_SPELL_TEXT, AMARK_LINK_TITLE},
        {0, 9, 10, AMARK_SPELL_DELIMITER, AMARK_LINK_TITLE_QUOTE},
        {0, 10, 11, AMARK_SPELL_DELIMITER, AMARK_FACET_NONE},
        {1, 1, 2, AMARK_SPELL_REPLACED, AMARK_CODE_BLOCK_LITERAL},
    };
    size_t i;
    amark_error_t error;

    (void)context;
    error = amark_document_create(NULL, &document);
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_document_add_source(document, source, sizeof(source) - 1,
                                          AMARK_SOURCE_FINISHED, &id);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_create(document, AMARK_NODE_PARAGRAPH, &paragraph);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_append_child(document->root, paragraph);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_block_set_lines(paragraph, 0, 1);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_block_add_content(paragraph, 0, 0, 11);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_create(document, AMARK_NODE_LINK, &link);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_append_child(paragraph, link);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_create(document, AMARK_NODE_TEXT, &text);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_append_child(link, text);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_create(document, AMARK_NODE_CODE_BLOCK, &block);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_append_child(document->root, block);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_block_set_lines(block, 1, 3);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_block_add_content(block, 2, 0, 2);
    }
    if (error == AMARK_ERROR_NONE)
    {
        memset(&layout, 0, sizeof(layout));
        layout.struct_size = sizeof(layout);
        layout.line = 1;
        layout.column_start = 0;
        layout.column_end = 3;
        layout.kind = AMARK_SPELL_DELIMITER;
        layout.facet = AMARK_CODE_BLOCK_FENCE_CHAR;
        error = amark_block_add_token(block, &layout);
    }
    memset(&record, 0, sizeof(record));
    record.struct_size = sizeof(record);
    record.start = 1;
    record.end = 2;
    record.kind = AMARK_SPELL_TEXT;
    record.facet = AMARK_TEXT_LITERAL;
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_add_content_token(text, paragraph, &record);
    }
    for (i = 0; (error == AMARK_ERROR_NONE) && (i < sizeof(tokens) / sizeof(tokens[0])); i++)
    {
        record.start = tokens[i].start;
        record.end = tokens[i].end;
        record.kind = tokens[i].kind;
        record.facet = tokens[i].facet;
        error = amark_node_add_content_token(tokens[i].on_block ? block : link,
                                             tokens[i].on_block ? block : paragraph, &record);
    }

    /* Each edit fails without a change, or succeeds. */
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_set_string(link, AMARK_LINK_DESTINATION, "/v", 2);
        if ((error != AMARK_ERROR_NONE) &&
            ((amark_node_content_token_count(link) != 7) || (link->dropped != 0)))
        {
            error = AMARK_ERROR_INVALID_STATE;
        }
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_discard_facet_spelling(link, AMARK_LINK_TITLE);
        if ((error != AMARK_ERROR_NONE) &&
            ((amark_node_content_token_count(link) != 6) ||
             (link->facets[AMARK_LINK_TITLE - 1].state != AMARK_TREE_FACET_NONE) ||
             (link->dropped != (1u << AMARK_LINK_DESTINATION))))
        {
            error = AMARK_ERROR_INVALID_STATE;
        }
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_discard_facet_spelling(block, AMARK_CODE_BLOCK_LITERAL);
        if ((error != AMARK_ERROR_NONE) &&
            ((amark_node_content_token_count(block) != 1) ||
             (amark_block_content_state(block) != AMARK_CONTENT_LAID) || (block->dropped != 0)))
        {
            error = AMARK_ERROR_INVALID_STATE;
        }
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_discard_spelling(link);
        if ((error != AMARK_ERROR_NONE) &&
            ((amark_node_content_token_count(link) != 5) ||
             (link->facets[AMARK_LINK_DESTINATION - 1].state != AMARK_TREE_FACET_SET) ||
             (link->dropped != ((1u << AMARK_LINK_DESTINATION) | (1u << AMARK_LINK_TITLE)))))
        {
            error = AMARK_ERROR_INVALID_STATE;
        }
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_discard_spelling(paragraph);
        if ((error != AMARK_ERROR_NONE) &&
            ((amark_block_content_state(paragraph) != AMARK_CONTENT_LAID) ||
             (paragraph->dropped != 0) || (text->content_tokens->base != paragraph->content)))
        {
            error = AMARK_ERROR_INVALID_STATE;
        }
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_set_inline_text(paragraph, "p\nq", 3);
        if ((error != AMARK_ERROR_NONE) &&
            ((paragraph->first_child != link) || (link->next != NULL)))
        {
            error = AMARK_ERROR_INVALID_STATE;
        }
    }
    if ((error == AMARK_ERROR_NONE) &&
        ((paragraph->first_child == NULL) || (paragraph->first_child->next == NULL) ||
         (paragraph->last_child->type != AMARK_NODE_TEXT)))
    {
        error = AMARK_ERROR_INVALID_STATE;
    }
    if ((document != NULL) && (amark_tree_check(document, NULL) != AMARK_TREE_VIOLATION_NONE))
    {
        error = AMARK_ERROR_INVALID_STATE;
    }
    amark_document_destroy(document);
    return error;
}

NAOS_TEST(out_of_memory_in_dropping_the_spelling_changes_nothing)
{
    AMARK_CHECK_OOM_SWEEP(discard_oom_scenario, NULL);
}

NAOS_SUITE(derive,
    NAOS_ADD_TEST(escapes_and_character_references_are_decoded),
    NAOS_ADD_TEST(replaced_bytes_become_one_replacement_character_per_unit),
    NAOS_ADD_TEST(code_span_line_breaks_fold_and_structural_tokens_are_left_out),
    NAOS_ADD_TEST(a_raw_label_keeps_its_escapes_and_references),
    NAOS_ADD_TEST(layout_tokens_are_decoded_in_the_order_of_the_lines),
    NAOS_ADD_TEST(a_lines_facet_derived_from_tokens_ends_with_a_line_feed),
    NAOS_ADD_TEST(a_layout_token_cut_inside_a_tab_gives_spaces),
    NAOS_ADD_TEST(the_derived_value_comes_before_the_recorded_value),
    NAOS_ADD_TEST(only_structural_tokens_derive_the_empty_string),
    NAOS_ADD_TEST(a_code_block_ending_with_an_empty_line_keeps_it),
    NAOS_ADD_TEST(input_3_3_the_literal_of_a_code_block_in_a_partial_tab),
    NAOS_ADD_TEST(replaced_ranges_of_the_content_become_replacement_characters),
    NAOS_ADD_TEST(a_content_facet_is_derived_from_the_content_whatever_was_recorded),
    NAOS_ADD_TEST(setting_a_content_facet_removes_its_tokens_and_marks_it_dropped),
    NAOS_ADD_TEST(the_value_of_a_content_facet_is_normalized_to_lf),
    NAOS_ADD_TEST(the_cache_is_dropped_when_the_tokens_change),
    NAOS_ADD_TEST(the_cache_is_dropped_when_the_layout_tokens_or_the_content_change),
    NAOS_ADD_TEST(a_string_facet_is_not_spelled_by_layout_and_content_tokens_both),
    NAOS_ADD_TEST(the_check_finds_a_stale_cache_and_a_stored_content_value),
    NAOS_ADD_TEST(out_of_memory_in_a_derivation_leaves_no_cache),
    NAOS_ADD_TEST(out_of_memory_in_setting_a_content_facet_changes_nothing),
    NAOS_ADD_TEST(changing_a_link_destination_keeps_the_other_spelling),
    NAOS_ADD_TEST(setting_a_layout_spelled_facet_drops_only_its_layout_tokens),
    NAOS_ADD_TEST(literals_of_content_facets_round_trip),
    NAOS_ADD_TEST(discarding_the_spelling_of_a_facet_keeps_its_value),
    NAOS_ADD_TEST(discarding_all_spelling_keeps_the_values_and_frees_the_content),
    NAOS_ADD_TEST(discarding_all_spelling_of_containers_and_code_blocks),
    NAOS_ADD_TEST(discarding_all_spelling_of_a_list_or_its_item_keeps_tight),
    NAOS_ADD_TEST(set_inline_text_places_texts_and_softbreaks),
    NAOS_ADD_TEST(out_of_memory_in_dropping_the_spelling_changes_nothing)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(derive)
)
