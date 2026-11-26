/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * Literals and the setting of inline text (src/tree/literal.c), through the public
 * API; the internal string helpers are called directly, and the internal
 * amark_tree_check checks the invariants after the operations.
 *
 * Suite text_helpers (5.2, 5.3, 5.4, 5.5): the common check (NUL, invalid UTF-8,
 * NULL with a length), the line endings (LF, CR, CRLF), the blank lines (only
 * between two line endings), the normalization to LF and the added final LF.
 * Suite literal (2.3, 5.1, 5.2, 5.3, 5.4, 5.5, 5.8, 7.1, 7.6): the default "", the
 * copy, the rejections of each literal mode (line endings for text and code, blank
 * lines for html_inline, a type without a literal, the empty code) leave the
 * literal unchanged, the multi-line normalization of code_block and html_block,
 * AMARK_EDIT_CONTENT only for another value and only with tracking on, the tokens
 * kept, and the out-of-memory sweep with the literal unchanged on each failure.
 * Suite inline_text (5.7, 7.1, 7.2, 7.4): the text nodes and soft breaks of each
 * line ending form, leading, trailing and consecutive line endings, the empty
 * string, the replaced children destroyed, the records, the rejections (a target
 * that cannot contain text, an invalid string) with the children unchanged, the
 * tokens of the target kept, and the out-of-memory sweep.
 * Suite attr (2.2, 2.6, 5.1, 5.2, 5.3, 5.4, 5.6, 5.8, 7.1, 7.6; src/tree/attr.c):
 * the defaults, the boundaries (levels 0, 1, 6, 7; starts -1, 0, 999999999,
 * 1000000000; labels of 999 and 1000 code points, also multi-byte; escaped and raw
 * brackets, "\\["; only whitespace), the line endings of info and destinations,
 * the blank lines of titles and labels and their normalization to LF, the type
 * mismatches, AMARK_EDIT_CONTENT only for another value, the tokens kept, and the
 * out-of-memory sweep of the string setters.
 */
#include "tree/tree.h"

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

/* Expects the check of document to pass. */
#define CHECK_VALID(document) \
    do { \
        const char *check_reason_ = "unset"; \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_tree_check((document), &check_reason_)); \
        NAOS_CHECK_NULL(check_reason_); \
    } while (0)

/* Expects the literal of node to be the string literal expected. */
#define CHECK_LITERAL(node, expected) \
    do { \
        size_t literal_length_ = 99; \
        const char *literal_ = amark_node_literal((node), &literal_length_); \
        NAOS_CHECK_UINT_EQ(sizeof(expected) - 1, literal_length_); \
        NAOS_CHECK_INT_EQ(0, memcmp((expected), literal_, sizeof(expected))); \
    } while (0)

/* Sets the literal of node to the string literal text. */
#define SET(node, text) amark_node_set_literal((node), (text), sizeof(text) - 1)

/* Sets the inline text of node to the string literal text. */
#define SET_INLINE(node, text) amark_node_set_inline_text((node), (text), sizeof(text) - 1)

/* A document with a paragraph under the root, and a detached node of each literal
 * type. */
typedef struct fixture
{
    amark_document_t *document;
    amark_node_t *paragraph;
    amark_node_t *text;
    amark_node_t *code;
    amark_node_t *html_inline;
    amark_node_t *code_block;
    amark_node_t *html_block;
} fixture_t;

static void build(fixture_t *f)
{
    amark_document_t *document;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &f->document));
    document = f->document;
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(document, AMARK_NODE_PARAGRAPH, &f->paragraph));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_append_child(amark_document_root(document), f->paragraph));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, AMARK_NODE_TEXT, &f->text));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(f->paragraph, f->text));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, AMARK_NODE_CODE, &f->code));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(document, AMARK_NODE_HTML_INLINE, &f->html_inline));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(document, AMARK_NODE_CODE_BLOCK, &f->code_block));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(document, AMARK_NODE_HTML_BLOCK, &f->html_block));
}

/* --- text_helpers -------------------------------------------------------------------- */

NAOS_TEST(the_common_check_rejects_nul_invalid_utf8_and_null_with_a_length)
{
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_text_check(NULL, 0));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_text_check("", 0));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_text_check("a\xC3\xA9\r\n", 5));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_text_check(NULL, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_text_check("a\0b", 3));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_text_check("a\xFF", 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_text_check("\xC3", 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_text_check("\xED\xA0\x80", 3));
}

NAOS_TEST(line_endings_are_lf_cr_and_crlf)
{
    NAOS_CHECK_FALSE(amark_text_has_line_ending(NULL, 0));
    NAOS_CHECK_FALSE(amark_text_has_line_ending("a \tb", 4));
    NAOS_CHECK_TRUE(amark_text_has_line_ending("a\nb", 3));
    NAOS_CHECK_TRUE(amark_text_has_line_ending("a\rb", 3));
    NAOS_CHECK_TRUE(amark_text_has_line_ending("a\r\nb", 4));
}

NAOS_TEST(a_blank_line_lies_between_two_line_endings)
{
    NAOS_CHECK_FALSE(amark_text_has_blank_line(NULL, 0));
    NAOS_CHECK_FALSE(amark_text_has_blank_line("a\nb\nc", 5));
    NAOS_CHECK_FALSE(amark_text_has_blank_line("\na", 2));
    NAOS_CHECK_FALSE(amark_text_has_blank_line("  \na", 4));
    NAOS_CHECK_FALSE(amark_text_has_blank_line("a\n  ", 4));
    NAOS_CHECK_FALSE(amark_text_has_blank_line("a\r\nb", 4));
    NAOS_CHECK_FALSE(amark_text_has_blank_line("a\n x\nb", 6));
    NAOS_CHECK_TRUE(amark_text_has_blank_line("a\n\nb", 4));
    NAOS_CHECK_TRUE(amark_text_has_blank_line("\n\n", 2));
    NAOS_CHECK_TRUE(amark_text_has_blank_line("a\n \t\nb", 6));
    NAOS_CHECK_TRUE(amark_text_has_blank_line("a\r\n\r\nb", 6));
    NAOS_CHECK_TRUE(amark_text_has_blank_line("a\r\rb", 4));
    NAOS_CHECK_TRUE(amark_text_has_blank_line("a\n\r\nb", 5));
}

NAOS_TEST(normalizing_gives_lf_and_the_final_lf)
{
    char *text = NULL;
    size_t length = 99;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_text_normalize("a\r\nb\rc\nd", 8, false, &text, &length));
    NAOS_CHECK_UINT_EQ(7, length);
    NAOS_CHECK_STR_EQ("a\nb\nc\nd", text);
    amark_free(text);

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_text_normalize("a\r", 2, true, &text, &length));
    NAOS_CHECK_UINT_EQ(2, length);
    NAOS_CHECK_STR_EQ("a\n", text);
    amark_free(text);

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_text_normalize("ab", 2, true, &text, &length));
    NAOS_CHECK_UINT_EQ(3, length);
    NAOS_CHECK_STR_EQ("ab\n", text);
    amark_free(text);

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_text_normalize("", 0, true, &text, &length));
    NAOS_CHECK_NULL(text);
    NAOS_CHECK_UINT_EQ(0, length);

    NAOS_CHECK_TRUE(amark_text_equal(NULL, 0, "", 0));
    NAOS_CHECK_TRUE(amark_text_equal("ab", 2, "ab", 2));
    NAOS_CHECK_FALSE(amark_text_equal("ab", 2, "ac", 2));
    NAOS_CHECK_FALSE(amark_text_equal("ab", 2, "abc", 3));
}

NAOS_SUITE(text_helpers,
    NAOS_ADD_TEST(the_common_check_rejects_nul_invalid_utf8_and_null_with_a_length),
    NAOS_ADD_TEST(line_endings_are_lf_cr_and_crlf),
    NAOS_ADD_TEST(a_blank_line_lies_between_two_line_endings),
    NAOS_ADD_TEST(normalizing_gives_lf_and_the_final_lf))

/* --- literal ------------------------------------------------------------------------- */

NAOS_TEST(a_literal_is_empty_by_default_and_a_copy_once_set)
{
    fixture_t f;
    char text[] = "caf\xC3\xA9";

    build(&f);
    CHECK_LITERAL(f.text, "");
    NAOS_CHECK_STR_EQ("", amark_node_literal(f.text, NULL));
    NAOS_CHECK_STR_EQ("", amark_node_literal(NULL, NULL));
    NAOS_CHECK_STR_EQ("", amark_node_literal(f.paragraph, NULL));

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_literal(f.text, text, sizeof(text) - 1));
    text[0] = 'X';
    CHECK_LITERAL(f.text, "caf\xC3\xA9");

    /* NULL with length 0 is "". */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_literal(f.text, NULL, 0));
    CHECK_LITERAL(f.text, "");
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET(f.html_inline, "<a>"));
    CHECK_LITERAL(f.html_inline, "<a>");

    CHECK_VALID(f.document);
    amark_document_destroy(f.document);
}

NAOS_TEST(invalid_strings_and_arguments_are_rejected)
{
    fixture_t f;

    build(&f);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, SET(f.text, "keep"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, SET(f.text, "a\0b"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, SET(f.text, "a\xFF"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, SET(f.text, "\xE3\x81"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_literal(f.text, NULL, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, SET(f.code_block, "a\0b\n"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_literal(NULL, "a", 1));
    CHECK_LITERAL(f.text, "keep");
    CHECK_LITERAL(f.code_block, "");

    CHECK_VALID(f.document);
    amark_document_destroy(f.document);
}

NAOS_TEST(line_literals_reject_line_endings)
{
    fixture_t f;

    build(&f);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, SET(f.text, "t"));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, SET(f.code, "c"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, SET(f.text, "a\nb"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, SET(f.text, "a\rb"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, SET(f.text, "a\r\n"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, SET(f.code, "\na"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, SET(f.code, "a\r"));
    CHECK_LITERAL(f.text, "t");
    CHECK_LITERAL(f.code, "c");

    /* Spaces and tabs are not line endings. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET(f.text, " a\tb "));
    CHECK_LITERAL(f.text, " a\tb ");

    CHECK_VALID(f.document);
    amark_document_destroy(f.document);
}

NAOS_TEST(span_literals_reject_blank_lines_and_normalize_to_lf)
{
    fixture_t f;

    build(&f);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, SET(f.html_inline, "<b>"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, SET(f.html_inline, "<a\n\nb>"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, SET(f.html_inline, "<a\n \t\nb>"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, SET(f.html_inline, "<a\r\n\r\nb>"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, SET(f.html_inline, "<a\r\rb>"));
    CHECK_LITERAL(f.html_inline, "<b>");

    /* Line endings become LF; no final LF is added. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET(f.html_inline, "<a\r\nb\rc\nd>"));
    CHECK_LITERAL(f.html_inline, "<a\nb\nc\nd>");
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET(f.html_inline, "<a\r"));
    CHECK_LITERAL(f.html_inline, "<a\n");

    CHECK_VALID(f.document);
    amark_document_destroy(f.document);
}

NAOS_TEST(lines_literals_normalize_to_lf_and_end_with_lf)
{
    fixture_t f;

    build(&f);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET(f.code_block, "a\r\nb\rc\nd"));
    CHECK_LITERAL(f.code_block, "a\nb\nc\nd\n");
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET(f.code_block, "x\r\n"));
    CHECK_LITERAL(f.code_block, "x\n");
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET(f.code_block, "a\n\n\nb"));
    CHECK_LITERAL(f.code_block, "a\n\n\nb\n");
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET(f.code_block, "\r"));
    CHECK_LITERAL(f.code_block, "\n");
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET(f.code_block, ""));
    CHECK_LITERAL(f.code_block, "");

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET(f.html_block, "<div>\r\n</div>"));
    CHECK_LITERAL(f.html_block, "<div>\n</div>\n");

    CHECK_VALID(f.document);
    amark_document_destroy(f.document);
}

NAOS_TEST(the_empty_code_and_types_without_a_literal_are_rejected)
{
    fixture_t f;
    amark_node_t *emph;

    build(&f);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, SET(f.code, "x"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, SET(f.code, ""));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_literal(f.code, NULL, 0));
    CHECK_LITERAL(f.code, "x");

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(f.document, AMARK_NODE_EMPH, &emph));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, SET(f.paragraph, "p"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, SET(emph, "e"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, SET(emph, ""));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      SET(amark_document_root(f.document), "r"));
    CHECK_LITERAL(f.paragraph, "");
    CHECK_LITERAL(emph, "");

    CHECK_VALID(f.document);
    amark_document_destroy(f.document);
}

NAOS_TEST(only_another_value_records_content_while_tracking)
{
    fixture_t f;

    build(&f);

    /* Tracking off: nothing is recorded. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, SET(f.text, "a"));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, SET(f.code_block, "x\n"));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.text));

    amark_document_set_edit_tracking(f.document, true);

    /* The same value, also after the normalization, and rejections record
     * nothing. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET(f.text, "a"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET(f.code_block, "x\r\n"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET(f.code_block, "x"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, SET(f.text, "b\n"));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.text));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.code_block));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.paragraph));

    /* Another value records CONTENT and DESCENDANTS on the ancestors. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET(f.text, "b"));
    CHECK_LITERAL(f.text, "b");
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_CONTENT, amark_node_edits(f.text));
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_DESCENDANTS, amark_node_edits(f.paragraph));
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_DESCENDANTS,
                       amark_node_edits(amark_document_root(f.document)));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET(f.code_block, "y"));
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_CONTENT, amark_node_edits(f.code_block));

    /* "" over "" records nothing; clearing a literal is another value. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET(f.html_inline, ""));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.html_inline));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, SET(f.html_inline, "<i>"));
    amark_document_set_edit_tracking(f.document, false);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, SET(f.html_block, "z"));
    amark_document_set_edit_tracking(f.document, true);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_literal(f.html_block, NULL, 0));
    CHECK_LITERAL(f.html_block, "");
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_CONTENT, amark_node_edits(f.html_block));

    CHECK_VALID(f.document);
    amark_document_destroy(f.document);
}

NAOS_TEST(setting_a_literal_keeps_the_tokens)
{
    fixture_t f;
    amark_source_id_t source;
    amark_token_t token;
    amark_token_t got;

    build(&f);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_add_source(f.document, "abc", 3, &source));
    memset(&token, 0, sizeof(token));
    token.struct_size = sizeof(token);
    token.kind = AMARK_TOKEN_TEXT;
    token.source = source;
    token.offset = 0;
    token.length = 3;
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_add_token(f.text, &token));

    amark_document_set_edit_tracking(f.document, true);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET(f.text, "changed"));
    NAOS_CHECK_UINT_EQ(1, amark_node_token_count(f.text));
    memset(&got, 0, sizeof(got));
    got.struct_size = sizeof(got);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_token_at(f.text, 0, &got));
    NAOS_CHECK_UINT_EQ(0, got.offset);
    NAOS_CHECK_UINT_EQ(3, got.length);

    CHECK_VALID(f.document);
    amark_document_destroy(f.document);
}

/* Sets a code_block literal over an existing one; a failure must leave it as it
 * was. */
static amark_error_t set_literal_scenario(void *context)
{
    bool *unchanged = context;
    amark_document_t *document = NULL;
    amark_node_t *node = NULL;
    amark_error_t err;

    err = amark_document_create(NULL, &document);
    if (err == AMARK_ERROR_NONE)
    {
        err = amark_node_create(document, AMARK_NODE_CODE_BLOCK, &node);
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = SET(node, "old\r\n");
    }

    if (err == AMARK_ERROR_NONE)
    {
        size_t length;
        const char *literal;

        amark_document_set_edit_tracking(document, true);
        err = SET(node, "new\rline");
        literal = amark_node_literal(node, &length);
        if (err == AMARK_ERROR_OUT_OF_MEMORY)
        {
            if ((length != 4) || (memcmp(literal, "old\n", 5) != 0)
                || (amark_node_edits(node) != 0))
            {
                *unchanged = false;
            }
        }
        else if ((err == AMARK_ERROR_NONE)
                 && ((length != 9) || (memcmp(literal, "new\nline\n", 10) != 0)
                     || (amark_node_edits(node) != AMARK_EDIT_CONTENT)))
        {
            *unchanged = false;
        }

        if (amark_tree_check(document, NULL) != AMARK_ERROR_NONE)
        {
            *unchanged = false;
        }
    }

    amark_document_destroy(document);

    return err;
}

NAOS_TEST(setting_a_literal_passes_the_out_of_memory_sweep)
{
    bool unchanged = true;

    AMARK_CHECK_OOM_SWEEP(set_literal_scenario, &unchanged);
    NAOS_CHECK_TRUE(unchanged);
}

NAOS_SUITE(literal,
    NAOS_SET_TEARDOWN(restore),
    NAOS_ADD_TEST(a_literal_is_empty_by_default_and_a_copy_once_set),
    NAOS_ADD_TEST(invalid_strings_and_arguments_are_rejected),
    NAOS_ADD_TEST(line_literals_reject_line_endings),
    NAOS_ADD_TEST(span_literals_reject_blank_lines_and_normalize_to_lf),
    NAOS_ADD_TEST(lines_literals_normalize_to_lf_and_end_with_lf),
    NAOS_ADD_TEST(the_empty_code_and_types_without_a_literal_are_rejected),
    NAOS_ADD_TEST(only_another_value_records_content_while_tracking),
    NAOS_ADD_TEST(setting_a_literal_keeps_the_tokens),
    NAOS_ADD_TEST(setting_a_literal_passes_the_out_of_memory_sweep))

/* --- inline_text --------------------------------------------------------------------- */

/*
 * Whether the children of node are, in order, the nodes that pattern describes: 't'
 * a text node whose literal is the next string of texts, 's' a soft break (no
 * literal, no child).
 */
static bool children_are(const amark_node_t *node, const char *pattern, const char *const *texts)
{
    const amark_node_t *child = amark_node_first_child(node);
    size_t i;

    for (i = 0; pattern[i] != '\0'; i++, child = amark_node_next(child))
    {
        if ((child == NULL) || (amark_node_parent(child) != node)
            || (amark_node_first_child(child) != NULL))
        {
            return false;
        }

        if (pattern[i] == 't')
        {
            size_t length;
            const char *literal = amark_node_literal(child, &length);

            if ((amark_node_get_type(child) != AMARK_NODE_TEXT) || (length != strlen(*texts))
                || (memcmp(literal, *texts, length + 1) != 0))
            {
                return false;
            }

            texts++;
        }
        else if ((amark_node_get_type(child) != AMARK_NODE_SOFTBREAK)
                 || (amark_node_literal(child, NULL)[0] != '\0'))
        {
            return false;
        }
    }

    return child == NULL;
}

NAOS_TEST(line_endings_split_the_text_into_text_nodes_and_soft_breaks)
{
    fixture_t f;

    build(&f);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET_INLINE(f.paragraph, "a\nb\r\nc\rd"));
    NAOS_CHECK_TRUE(children_are(f.paragraph, "tststst",
                                 (const char *const[]){ "a", "b", "c", "d" }));
    CHECK_VALID(f.document);

    /* Leading, trailing and consecutive line endings are soft breaks; no empty text
     * node is created. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET_INLINE(f.paragraph, "\nx y\n\r\n\r"));
    NAOS_CHECK_TRUE(children_are(f.paragraph, "stsss", (const char *const[]){ "x y" }));
    CHECK_VALID(f.document);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET_INLINE(f.paragraph, "\r\n"));
    NAOS_CHECK_TRUE(children_are(f.paragraph, "s", NULL));

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET_INLINE(f.paragraph, "caf\xC3\xA9"));
    NAOS_CHECK_TRUE(children_are(f.paragraph, "t", (const char *const[]){ "caf\xC3\xA9" }));

    /* The empty string removes the children. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET_INLINE(f.paragraph, ""));
    NAOS_CHECK_NULL(amark_node_first_child(f.paragraph));
    NAOS_CHECK_NULL(amark_node_last_child(f.paragraph));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_inline_text(f.paragraph, NULL, 0));
    NAOS_CHECK_NULL(amark_node_first_child(f.paragraph));

    CHECK_VALID(f.document);
    amark_document_destroy(f.document);
}

NAOS_TEST(the_old_children_and_their_subtrees_are_destroyed)
{
    fixture_t f;
    amark_node_t *emph;
    amark_node_t *inner;

    build(&f);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(f.document, AMARK_NODE_EMPH, &emph));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(f.document, AMARK_NODE_TEXT, &inner));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(emph, inner));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(f.paragraph, emph));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, SET(inner, "inner"));

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET_INLINE(emph, "e\nf"));
    NAOS_CHECK_TRUE(children_are(emph, "tst", (const char *const[]){ "e", "f" }));
    CHECK_VALID(f.document);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET_INLINE(f.paragraph, "new"));
    NAOS_CHECK_TRUE(children_are(f.paragraph, "t", (const char *const[]){ "new" }));

    /* The ring holds only the nodes of the tree and the detached fixture nodes:
     * the check counts them against the subtrees. */
    CHECK_VALID(f.document);
    amark_document_destroy(f.document);
}

NAOS_TEST(headings_and_links_take_inline_text)
{
    fixture_t f;
    amark_node_t *heading;
    amark_node_t *link;

    build(&f);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(f.document, AMARK_NODE_HEADING, &heading));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(f.document, AMARK_NODE_LINK, &link));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET_INLINE(heading, "h\ni"));
    NAOS_CHECK_TRUE(children_are(heading, "tst", (const char *const[]){ "h", "i" }));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET_INLINE(link, "l"));
    NAOS_CHECK_TRUE(children_are(link, "t", (const char *const[]){ "l" }));

    CHECK_VALID(f.document);
    amark_document_destroy(f.document);
}

NAOS_TEST(setting_inline_text_records_children_and_created)
{
    fixture_t f;
    amark_node_t *child;
    amark_node_t *empty;

    build(&f);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(f.document, AMARK_NODE_PARAGRAPH, &empty));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_append_child(amark_document_root(f.document), empty));

    /* Tracking off: no record. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET_INLINE(f.paragraph, "a\nb"));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.paragraph));
    for (child = amark_node_first_child(f.paragraph); child != NULL;
         child = amark_node_next(child))
    {
        NAOS_CHECK_UINT_EQ(0, amark_node_edits(child));
    }

    amark_document_set_edit_tracking(f.document, true);

    /* Nothing replaced with nothing records nothing. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET_INLINE(empty, ""));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(empty));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(amark_document_root(f.document)));

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET_INLINE(f.paragraph, "c\nd"));
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_CHILDREN | AMARK_EDIT_DESCENDANTS,
                       amark_node_edits(f.paragraph));
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_DESCENDANTS,
                       amark_node_edits(amark_document_root(f.document)));
    for (child = amark_node_first_child(f.paragraph); child != NULL;
         child = amark_node_next(child))
    {
        NAOS_CHECK_UINT_EQ(AMARK_EDIT_CREATED, amark_node_edits(child));
    }

    /* Removing the children records CHILDREN. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET_INLINE(f.paragraph, "x"));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, SET_INLINE(f.paragraph, ""));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, SET_INLINE(empty, "y"));
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_CHILDREN | AMARK_EDIT_DESCENDANTS, amark_node_edits(empty));

    CHECK_VALID(f.document);
    amark_document_destroy(f.document);
}

NAOS_TEST(a_rejected_inline_text_leaves_the_children)
{
    fixture_t f;
    amark_node_t *thematic_break;

    build(&f);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, SET(f.text, "keep"));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(f.document, AMARK_NODE_THEMATIC_BREAK,
                                          &thematic_break));

    /* Targets that cannot contain text. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      SET_INLINE(amark_document_root(f.document), "a"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, SET_INLINE(f.text, "a"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, SET_INLINE(f.code_block, "a"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, SET_INLINE(thematic_break, "a"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_inline_text(NULL, "a", 1));
    NAOS_CHECK_PTR_EQ(f.paragraph, amark_node_first_child(amark_document_root(f.document)));

    /* Invalid strings. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, SET_INLINE(f.paragraph, "a\nb\0c"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, SET_INLINE(f.paragraph, "a\n\xC0\xAF"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_set_inline_text(f.paragraph, NULL, 2));
    NAOS_CHECK_TRUE(children_are(f.paragraph, "t", (const char *const[]){ "keep" }));
    NAOS_CHECK_PTR_EQ(f.text, amark_node_first_child(f.paragraph));

    CHECK_VALID(f.document);
    amark_document_destroy(f.document);
}

NAOS_TEST(setting_inline_text_keeps_the_tokens_of_the_target)
{
    fixture_t f;
    amark_source_id_t source;
    amark_token_t token;

    build(&f);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_add_source(f.document, "abc\n", 4, &source));
    memset(&token, 0, sizeof(token));
    token.struct_size = sizeof(token);
    token.kind = AMARK_TOKEN_LINE_ENDING;
    token.source = source;
    token.offset = 3;
    token.length = 1;
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_add_token(f.paragraph, &token));

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET_INLINE(f.paragraph, "x\ny"));
    NAOS_CHECK_UINT_EQ(1, amark_node_token_count(f.paragraph));

    CHECK_VALID(f.document);
    amark_document_destroy(f.document);
}

/* Replaces the children of a paragraph that has one; a failure must leave the
 * child, its literal and the records as they were. */
static amark_error_t inline_text_scenario(void *context)
{
    bool *unchanged = context;
    amark_document_t *document = NULL;
    amark_node_t *paragraph = NULL;
    amark_node_t *text = NULL;
    amark_error_t err;

    err = amark_document_create(NULL, &document);
    if (err == AMARK_ERROR_NONE)
    {
        err = amark_node_create(document, AMARK_NODE_PARAGRAPH, &paragraph);
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = amark_node_append_child(amark_document_root(document), paragraph);
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = amark_node_create(document, AMARK_NODE_TEXT, &text);
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = amark_node_append_child(paragraph, text);
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = SET(text, "old");
    }

    if (err == AMARK_ERROR_NONE)
    {
        amark_document_set_edit_tracking(document, true);
        err = SET_INLINE(paragraph, "a\r\nb\nc");
        if (err == AMARK_ERROR_OUT_OF_MEMORY)
        {
            if (!children_are(paragraph, "t", (const char *const[]){ "old" })
                || (amark_node_first_child(paragraph) != text)
                || (amark_node_edits(paragraph) != 0) || (amark_node_edits(text) != 0))
            {
                *unchanged = false;
            }
        }
        else if ((err == AMARK_ERROR_NONE)
                 && (!children_are(paragraph, "tstst", (const char *const[]){ "a", "b", "c" })
                     || (amark_node_edits(paragraph)
                         != (AMARK_EDIT_CHILDREN | AMARK_EDIT_DESCENDANTS))))
        {
            *unchanged = false;
        }

        if (amark_tree_check(document, NULL) != AMARK_ERROR_NONE)
        {
            *unchanged = false;
        }
    }

    amark_document_destroy(document);

    return err;
}

NAOS_TEST(setting_inline_text_passes_the_out_of_memory_sweep)
{
    bool unchanged = true;

    AMARK_CHECK_OOM_SWEEP(inline_text_scenario, &unchanged);
    NAOS_CHECK_TRUE(unchanged);
}

NAOS_SUITE(inline_text,
    NAOS_SET_TEARDOWN(restore),
    NAOS_ADD_TEST(line_endings_split_the_text_into_text_nodes_and_soft_breaks),
    NAOS_ADD_TEST(the_old_children_and_their_subtrees_are_destroyed),
    NAOS_ADD_TEST(headings_and_links_take_inline_text),
    NAOS_ADD_TEST(setting_inline_text_records_children_and_created),
    NAOS_ADD_TEST(a_rejected_inline_text_leaves_the_children),
    NAOS_ADD_TEST(setting_inline_text_keeps_the_tokens_of_the_target),
    NAOS_ADD_TEST(setting_inline_text_passes_the_out_of_memory_sweep))

/* --- attr ---------------------------------------------------------------------------- */

/* Expects the string attribute that getter returns for node to be the string literal
 * expected. */
#define CHECK_ATTR(getter, node, expected) \
    do { \
        size_t attr_length_ = 99; \
        const char *attr_ = getter((node), &attr_length_); \
        NAOS_CHECK_UINT_EQ(sizeof(expected) - 1, attr_length_); \
        NAOS_CHECK_INT_EQ(0, memcmp((expected), attr_, sizeof(expected))); \
    } while (0)

/* Sets a string attribute of node with setter to the string literal text. */
#define SET_ATTR(setter, node, text) setter((node), (text), sizeof(text) - 1)

/* A document with a node of each type that has attributes: a heading, a list, a
 * code block and a link reference definition under the root, a link and an image in
 * a paragraph under the root. */
typedef struct attr_fixture
{
    amark_document_t *document;
    amark_node_t *heading;
    amark_node_t *list;
    amark_node_t *code_block;
    amark_node_t *definition;
    amark_node_t *paragraph;
    amark_node_t *link;
    amark_node_t *image;
} attr_fixture_t;

/* Creates a node of type and appends it to parent. */
static amark_node_t *append_new(amark_document_t *document, amark_node_t *parent,
                                amark_node_type_t type)
{
    amark_node_t *node = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, type, &node));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(parent, node));

    return node;
}

static void build_attr(attr_fixture_t *f)
{
    amark_node_t *root;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &f->document));
    root = amark_document_root(f->document);
    f->heading = append_new(f->document, root, AMARK_NODE_HEADING);
    f->list = append_new(f->document, root, AMARK_NODE_LIST);
    f->code_block = append_new(f->document, root, AMARK_NODE_CODE_BLOCK);
    f->definition = append_new(f->document, root, AMARK_NODE_LINK_DEFINITION);
    f->paragraph = append_new(f->document, root, AMARK_NODE_PARAGRAPH);
    f->link = append_new(f->document, f->paragraph, AMARK_NODE_LINK);
    f->image = append_new(f->document, f->paragraph, AMARK_NODE_IMAGE);
}

/* Writes to buffer count copies of the unit (of unit_length bytes) followed by tail
 * (of tail_length bytes), and returns the length. */
static size_t repeat(char *buffer, const char *unit, size_t unit_length, size_t count,
                     const char *tail, size_t tail_length)
{
    size_t length = 0;
    size_t i;

    for (i = 0; i < count; i++)
    {
        memcpy(buffer + length, unit, unit_length);
        length += unit_length;
    }

    memcpy(buffer + length, tail, tail_length);

    return length + tail_length;
}

NAOS_TEST(attributes_have_their_defaults)
{
    attr_fixture_t f;

    build_attr(&f);
    NAOS_CHECK_INT_EQ(1, amark_node_heading_level(f.heading));
    NAOS_CHECK_INT_EQ(AMARK_LIST_BULLET, amark_node_list_type(f.list));
    NAOS_CHECK_INT_EQ(1, amark_node_list_start(f.list));
    NAOS_CHECK_TRUE(amark_node_list_tight(f.list));
    CHECK_ATTR(amark_node_code_info, f.code_block, "");
    CHECK_ATTR(amark_node_link_destination, f.link, "");
    CHECK_ATTR(amark_node_link_title, f.image, "");
    CHECK_ATTR(amark_node_link_label, f.definition, "");
    CHECK_ATTR(amark_node_link_destination, f.definition, "");
    NAOS_CHECK_STR_EQ("", amark_node_link_title(f.definition, NULL));

    CHECK_VALID(f.document);
    amark_document_destroy(f.document);
}

NAOS_TEST(heading_levels_are_1_to_6)
{
    attr_fixture_t f;

    build_attr(&f);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_heading_level(f.heading, 6));
    NAOS_CHECK_INT_EQ(6, amark_node_heading_level(f.heading));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_heading_level(f.heading, 7));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_heading_level(f.heading, 0));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_heading_level(f.heading, -1));
    NAOS_CHECK_INT_EQ(6, amark_node_heading_level(f.heading));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_heading_level(f.heading, 1));
    NAOS_CHECK_INT_EQ(1, amark_node_heading_level(f.heading));

    CHECK_VALID(f.document);
    amark_document_destroy(f.document);
}

NAOS_TEST(list_types_starts_and_tightness_are_checked)
{
    attr_fixture_t f;

    build_attr(&f);

    /* The type is bullet or ordered. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_list_type(f.list, AMARK_LIST_ORDERED));
    NAOS_CHECK_INT_EQ(AMARK_LIST_ORDERED, amark_node_list_type(f.list));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_set_list_type(f.list, AMARK_LIST_NONE));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_set_list_type(f.list, (amark_list_type_t)3));
    NAOS_CHECK_INT_EQ(AMARK_LIST_ORDERED, amark_node_list_type(f.list));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_list_type(f.list, AMARK_LIST_BULLET));
    NAOS_CHECK_INT_EQ(AMARK_LIST_BULLET, amark_node_list_type(f.list));

    /* The start is 0-999999999, kept also for a bullet list. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_list_start(f.list, 0));
    NAOS_CHECK_INT_EQ(0, amark_node_list_start(f.list));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_list_start(f.list, 999999999L));
    NAOS_CHECK_INT_EQ(999999999L, amark_node_list_start(f.list));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_list_start(f.list, -1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_set_list_start(f.list, 1000000000L));
    NAOS_CHECK_INT_EQ(999999999L, amark_node_list_start(f.list));

    /* The tightness. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_list_tight(f.list, false));
    NAOS_CHECK_FALSE(amark_node_list_tight(f.list));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_list_tight(f.list, true));
    NAOS_CHECK_TRUE(amark_node_list_tight(f.list));

    CHECK_VALID(f.document);
    amark_document_destroy(f.document);
}

NAOS_TEST(a_type_mismatch_gets_defaults_and_rejects_setting)
{
    attr_fixture_t f;

    build_attr(&f);

    /* Getters: the defaults on another type and on NULL. */
    NAOS_CHECK_INT_EQ(0, amark_node_heading_level(f.list));
    NAOS_CHECK_INT_EQ(0, amark_node_heading_level(NULL));
    NAOS_CHECK_INT_EQ(AMARK_LIST_NONE, amark_node_list_type(f.heading));
    NAOS_CHECK_INT_EQ(AMARK_LIST_NONE, amark_node_list_type(NULL));
    NAOS_CHECK_INT_EQ(0, amark_node_list_start(f.paragraph));
    NAOS_CHECK_FALSE(amark_node_list_tight(f.paragraph));
    NAOS_CHECK_FALSE(amark_node_list_tight(NULL));
    CHECK_ATTR(amark_node_code_info, f.link, "");
    CHECK_ATTR(amark_node_code_info, NULL, "");
    CHECK_ATTR(amark_node_link_destination, f.code_block, "");
    CHECK_ATTR(amark_node_link_title, f.heading, "");
    CHECK_ATTR(amark_node_link_label, f.link, "");
    CHECK_ATTR(amark_node_link_label, NULL, "");
    NAOS_CHECK_STR_EQ("", amark_node_link_label(f.image, NULL));

    /* Setters: INVALID_ARGUMENT and nothing changes. */
    amark_document_set_edit_tracking(f.document, true);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_heading_level(f.list, 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_heading_level(NULL, 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_set_list_type(f.heading, AMARK_LIST_ORDERED));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_list_start(f.heading, 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_list_tight(f.heading, false));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_list_tight(NULL, false));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      SET_ATTR(amark_node_set_code_info, f.link, "c"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      SET_ATTR(amark_node_set_link_destination, f.code_block, "/u"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      SET_ATTR(amark_node_set_link_title, f.paragraph, "t"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      SET_ATTR(amark_node_set_link_label, f.link, "x"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      SET_ATTR(amark_node_set_link_label, f.image, "x"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      SET_ATTR(amark_node_set_link_label, NULL, "x"));
    NAOS_CHECK_INT_EQ(1, amark_node_list_start(f.list));
    NAOS_CHECK_INT_EQ(1, amark_node_heading_level(f.heading));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.list));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.link));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(amark_document_root(f.document)));

    CHECK_VALID(f.document);
    amark_document_destroy(f.document);
}

NAOS_TEST(strings_pass_the_common_check_and_are_copied)
{
    attr_fixture_t f;
    char text[] = "/caf\xC3\xA9";

    build_attr(&f);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_set_link_destination(f.link, text, sizeof(text) - 1));
    text[0] = 'X';
    CHECK_ATTR(amark_node_link_destination, f.link, "/caf\xC3\xA9");

    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      SET_ATTR(amark_node_set_link_destination, f.link, "a\0b"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      SET_ATTR(amark_node_set_link_title, f.link, "a\xFF"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      SET_ATTR(amark_node_set_code_info, f.code_block, "\xE3\x81"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      SET_ATTR(amark_node_set_link_label, f.definition, "a\0b"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_set_link_title(f.link, NULL, 1));
    CHECK_ATTR(amark_node_link_destination, f.link, "/caf\xC3\xA9");
    CHECK_ATTR(amark_node_link_title, f.link, "");

    /* NULL with length 0 is "". */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_link_destination(f.link, NULL, 0));
    CHECK_ATTR(amark_node_link_destination, f.link, "");

    /* The three strings of a link reference definition are separate. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET_ATTR(amark_node_set_link_label, f.definition, "L"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      SET_ATTR(amark_node_set_link_destination, f.definition, "/d"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET_ATTR(amark_node_set_link_title, f.definition, "T"));
    CHECK_ATTR(amark_node_link_label, f.definition, "L");
    CHECK_ATTR(amark_node_link_destination, f.definition, "/d");
    CHECK_ATTR(amark_node_link_title, f.definition, "T");

    CHECK_VALID(f.document);
    amark_document_destroy(f.document);
}

NAOS_TEST(info_and_destinations_reject_line_endings)
{
    attr_fixture_t f;

    build_attr(&f);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        SET_ATTR(amark_node_set_code_info, f.code_block, "c info"));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        SET_ATTR(amark_node_set_link_destination, f.image, "/i"));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        SET_ATTR(amark_node_set_link_destination, f.definition, "/d"));

    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      SET_ATTR(amark_node_set_code_info, f.code_block, "a\nb"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      SET_ATTR(amark_node_set_code_info, f.code_block, "a\r"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      SET_ATTR(amark_node_set_link_destination, f.image, "a\r\nb"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      SET_ATTR(amark_node_set_link_destination, f.definition, "\n"));
    CHECK_ATTR(amark_node_code_info, f.code_block, "c info");
    CHECK_ATTR(amark_node_link_destination, f.image, "/i");
    CHECK_ATTR(amark_node_link_destination, f.definition, "/d");

    CHECK_VALID(f.document);
    amark_document_destroy(f.document);
}

NAOS_TEST(titles_reject_blank_lines_and_normalize_to_lf)
{
    attr_fixture_t f;

    build_attr(&f);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      SET_ATTR(amark_node_set_link_title, f.link, "a\r\nb\rc\nd"));
    CHECK_ATTR(amark_node_link_title, f.link, "a\nb\nc\nd");
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      SET_ATTR(amark_node_set_link_title, f.definition, "x\r\n"));
    CHECK_ATTR(amark_node_link_title, f.definition, "x\n");

    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      SET_ATTR(amark_node_set_link_title, f.link, "a\n\nb"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      SET_ATTR(amark_node_set_link_title, f.link, "a\n \t\nb"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      SET_ATTR(amark_node_set_link_title, f.image, "a\r\n\r\nb"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      SET_ATTR(amark_node_set_link_title, f.definition, "a\r\rb"));
    CHECK_ATTR(amark_node_link_title, f.link, "a\nb\nc\nd");
    CHECK_ATTR(amark_node_link_title, f.image, "");
    CHECK_ATTR(amark_node_link_title, f.definition, "x\n");

    CHECK_VALID(f.document);
    amark_document_destroy(f.document);
}

NAOS_TEST(labels_have_1_to_999_code_points)
{
    attr_fixture_t f;
    static char buffer[4096];
    size_t length;
    size_t got;

    build_attr(&f);

    /* 999 and 1000 ASCII code points. */
    length = repeat(buffer, "a", 1, 999, "", 0);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_link_label(f.definition, buffer, length));
    amark_node_link_label(f.definition, &got);
    NAOS_CHECK_UINT_EQ(999, got);
    length = repeat(buffer, "b", 1, 1000, "", 0);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_set_link_label(f.definition, buffer, length));

    /* 999 and 1000 code points of 3 bytes: code points, not bytes, are counted. */
    length = repeat(buffer, "\xE3\x81\x82", 3, 999, "", 0);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_link_label(f.definition, buffer, length));
    amark_node_link_label(f.definition, &got);
    NAOS_CHECK_UINT_EQ(2997, got);
    length = repeat(buffer, "\xE3\x81\x82", 3, 1000, "", 0);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_set_link_label(f.definition, buffer, length));

    /* A 4-byte code point among them. */
    length = repeat(buffer, "a", 1, 998, "\xF0\x9F\x98\x80", 4);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_link_label(f.definition, buffer, length));
    length = repeat(buffer, "a", 1, 999, "\xF0\x9F\x98\x80", 4);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_set_link_label(f.definition, buffer, length));

    /* An escape is two code points; CRLF is one line ending (kept as LF). */
    length = repeat(buffer, "a", 1, 997, "\\[", 2);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_link_label(f.definition, buffer, length));
    length = repeat(buffer, "a", 1, 998, "\\[", 2);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_set_link_label(f.definition, buffer, length));
    length = repeat(buffer, "a", 1, 998, "\r\n", 2);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_link_label(f.definition, buffer, length));
    amark_node_link_label(f.definition, &got);
    NAOS_CHECK_UINT_EQ(999, got);
    length = repeat(buffer, "a", 1, 999, "\r\n", 2);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_set_link_label(f.definition, buffer, length));

    /* The empty label. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_set_link_label(f.definition, NULL, 0));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      SET_ATTR(amark_node_set_link_label, f.definition, ""));
    amark_node_link_label(f.definition, &got);
    NAOS_CHECK_UINT_EQ(999, got);

    CHECK_VALID(f.document);
    amark_document_destroy(f.document);
}

NAOS_TEST(labels_reject_unescaped_brackets_blank_lines_and_only_whitespace)
{
    attr_fixture_t f;

    build_attr(&f);

    /* Escaped brackets are accepted, raw ones are not. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      SET_ATTR(amark_node_set_link_label, f.definition, "a\\[b\\]c"));
    CHECK_ATTR(amark_node_link_label, f.definition, "a\\[b\\]c");
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      SET_ATTR(amark_node_set_link_label, f.definition, "a[b"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      SET_ATTR(amark_node_set_link_label, f.definition, "a]b"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      SET_ATTR(amark_node_set_link_label, f.definition, "["));

    /* "\\[" is an escaped backslash and a raw bracket; "\\\[" escapes both. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      SET_ATTR(amark_node_set_link_label, f.definition, "a\\\\[b"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      SET_ATTR(amark_node_set_link_label, f.definition, "a\\\\]"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      SET_ATTR(amark_node_set_link_label, f.definition, "a\\\\\\[b"));
    CHECK_ATTR(amark_node_link_label, f.definition, "a\\\\\\[b");

    /* A backslash before a character that is not punctuation is not an escape. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      SET_ATTR(amark_node_set_link_label, f.definition, "\\a]"));

    /* A trailing unpaired backslash would escape the closing "]": "\\", "a\\" and
     * three backslashes are rejected, two (an escaped backslash) are accepted. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      SET_ATTR(amark_node_set_link_label, f.definition, "\\"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      SET_ATTR(amark_node_set_link_label, f.definition, "a\\"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      SET_ATTR(amark_node_set_link_label, f.definition, "a\\\\\\"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      SET_ATTR(amark_node_set_link_label, f.definition, "a\\\\"));
    CHECK_ATTR(amark_node_link_label, f.definition, "a\\\\");

    /* Only spaces, tabs and line endings. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      SET_ATTR(amark_node_set_link_label, f.definition, " "));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      SET_ATTR(amark_node_set_link_label, f.definition, " \t\n "));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      SET_ATTR(amark_node_set_link_label, f.definition, "\r\n"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      SET_ATTR(amark_node_set_link_label, f.definition, " \t x "));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      SET_ATTR(amark_node_set_link_label, f.definition, " \\! "));

    /* Blank lines are rejected; line endings are normalized to LF. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      SET_ATTR(amark_node_set_link_label, f.definition, "a\n\nb"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      SET_ATTR(amark_node_set_link_label, f.definition, "a\r\n \r\nb"));
    CHECK_ATTR(amark_node_link_label, f.definition, " \\! ");
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      SET_ATTR(amark_node_set_link_label, f.definition, "a\r\nb\rc"));
    CHECK_ATTR(amark_node_link_label, f.definition, "a\nb\nc");

    CHECK_VALID(f.document);
    amark_document_destroy(f.document);
}

NAOS_TEST(only_another_attribute_value_records_content_while_tracking)
{
    attr_fixture_t f;
    amark_node_t *root;

    build_attr(&f);
    root = amark_document_root(f.document);

    /* Tracking off: nothing is recorded. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_heading_level(f.heading, 2));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, SET_ATTR(amark_node_set_link_title, f.link, "t\nu"));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.heading));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.link));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(root));

    amark_document_set_edit_tracking(f.document, true);

    /* The same values (also after the normalization) and rejections record
     * nothing. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_heading_level(f.heading, 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_list_type(f.list, AMARK_LIST_BULLET));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_list_start(f.list, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_list_tight(f.list, true));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET_ATTR(amark_node_set_code_info, f.code_block, ""));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_code_info(f.code_block, NULL, 0));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET_ATTR(amark_node_set_link_title, f.link, "t\r\nu"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET_ATTR(amark_node_set_link_title, f.link, "t\ru"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_heading_level(f.heading, 7));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      SET_ATTR(amark_node_set_link_label, f.definition, "["));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      SET_ATTR(amark_node_set_link_destination, f.image, "a\nb"));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.heading));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.list));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.code_block));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.definition));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.link));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.image));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(root));

    /* Another value of each attribute records CONTENT, and DESCENDANTS on the
     * ancestors. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_heading_level(f.heading, 3));
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_CONTENT, amark_node_edits(f.heading));
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_DESCENDANTS, amark_node_edits(root));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_list_tight(f.list, false));
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_CONTENT, amark_node_edits(f.list));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      SET_ATTR(amark_node_set_link_destination, f.image, "/x"));
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_CONTENT, amark_node_edits(f.image));
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_DESCENDANTS, amark_node_edits(f.paragraph));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.link));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET_ATTR(amark_node_set_link_label, f.definition, "L"));
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_CONTENT, amark_node_edits(f.definition));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET_ATTR(amark_node_set_code_info, f.code_block, "c"));
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_CONTENT, amark_node_edits(f.code_block));

    CHECK_VALID(f.document);
    amark_document_destroy(f.document);
}

/* Records content for each other value of the list attributes and the title. */
NAOS_TEST(each_attribute_change_records_content)
{
    attr_fixture_t f;
    amark_node_t *other_list;

    build_attr(&f);
    other_list = append_new(f.document, amark_document_root(f.document), AMARK_NODE_LIST);
    amark_document_set_edit_tracking(f.document, true);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_list_type(f.list, AMARK_LIST_ORDERED));
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_CONTENT, amark_node_edits(f.list));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_list_start(other_list, 5));
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_CONTENT, amark_node_edits(other_list));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET_ATTR(amark_node_set_link_title, f.link, "t"));
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_CONTENT, amark_node_edits(f.link));

    CHECK_VALID(f.document);
    amark_document_destroy(f.document);
}

NAOS_TEST(setting_attributes_keeps_the_tokens)
{
    attr_fixture_t f;
    amark_source_id_t source;
    amark_token_t token;
    amark_token_t got;

    build_attr(&f);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_add_source(f.document, "# a\n[l]: /d", 11, &source));
    memset(&token, 0, sizeof(token));
    token.struct_size = sizeof(token);
    token.kind = AMARK_TOKEN_MARKER;
    token.source = source;
    token.offset = 0;
    token.length = 1;
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_add_token(f.heading, &token));
    token.kind = AMARK_TOKEN_TEXT;
    token.offset = 9;
    token.length = 2;
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_add_token(f.definition, &token));

    amark_document_set_edit_tracking(f.document, true);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_heading_level(f.heading, 4));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      SET_ATTR(amark_node_set_link_destination, f.definition, "/other"));
    NAOS_CHECK_UINT_EQ(1, amark_node_token_count(f.heading));
    NAOS_CHECK_UINT_EQ(1, amark_node_token_count(f.definition));
    memset(&got, 0, sizeof(got));
    got.struct_size = sizeof(got);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_token_at(f.definition, 0, &got));
    NAOS_CHECK_UINT_EQ(9, got.offset);
    NAOS_CHECK_UINT_EQ(2, got.length);

    CHECK_VALID(f.document);
    amark_document_destroy(f.document);
}

/* Sets each string attribute of a link reference definition over an existing value;
 * a failure must leave all of them as they were, with no record. */
static amark_error_t set_attr_scenario(void *context)
{
    bool *unchanged = context;
    amark_document_t *document = NULL;
    amark_node_t *node = NULL;
    amark_node_t *code_block = NULL;
    amark_error_t err;

    err = amark_document_create(NULL, &document);
    if (err == AMARK_ERROR_NONE)
    {
        err = amark_node_create(document, AMARK_NODE_LINK_DEFINITION, &node);
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = amark_node_create(document, AMARK_NODE_CODE_BLOCK, &code_block);
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = SET_ATTR(amark_node_set_link_label, node, "old");
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = SET_ATTR(amark_node_set_link_destination, node, "/old");
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = SET_ATTR(amark_node_set_code_info, code_block, "old");
    }

    if (err == AMARK_ERROR_NONE)
    {
        amark_document_set_edit_tracking(document, true);
        err = SET_ATTR(amark_node_set_link_label, node, "new\r\nlabel");
        if (err == AMARK_ERROR_NONE)
        {
            err = SET_ATTR(amark_node_set_link_destination, node, "/new");
        }

        if (err == AMARK_ERROR_NONE)
        {
            err = SET_ATTR(amark_node_set_link_title, node, "new\rtitle");
        }

        if (err == AMARK_ERROR_NONE)
        {
            err = SET_ATTR(amark_node_set_code_info, code_block, "new");
        }

        if (err == AMARK_ERROR_OUT_OF_MEMORY)
        {
            /* Each setter either succeeded fully or changed nothing. */
            size_t length;
            const char *label = amark_node_link_label(node, &length);
            bool label_old = (length == 3) && (memcmp(label, "old", 4) == 0);
            bool label_new = (length == 9) && (memcmp(label, "new\nlabel", 10) == 0);

            if ((!label_old && !label_new)
                || (label_old && (amark_node_edits(node) != 0))
                || (label_new && (amark_node_edits(node) != AMARK_EDIT_CONTENT)))
            {
                *unchanged = false;
            }

            if (amark_node_edits(code_block) != 0)
            {
                *unchanged = false;
            }
        }
        else if (err == AMARK_ERROR_NONE)
        {
            size_t length;
            const char *title = amark_node_link_title(node, &length);

            if ((length != 9) || (memcmp(title, "new\ntitle", 10) != 0)
                || (strcmp(amark_node_link_destination(node, NULL), "/new") != 0)
                || (strcmp(amark_node_code_info(code_block, NULL), "new") != 0)
                || (amark_node_edits(node) != AMARK_EDIT_CONTENT)
                || (amark_node_edits(code_block) != AMARK_EDIT_CONTENT))
            {
                *unchanged = false;
            }
        }

        if (amark_tree_check(document, NULL) != AMARK_ERROR_NONE)
        {
            *unchanged = false;
        }
    }

    amark_document_destroy(document);

    return err;
}

NAOS_TEST(setting_string_attributes_passes_the_out_of_memory_sweep)
{
    bool unchanged = true;

    AMARK_CHECK_OOM_SWEEP(set_attr_scenario, &unchanged);
    NAOS_CHECK_TRUE(unchanged);
}

NAOS_SUITE(attr,
    NAOS_SET_TEARDOWN(restore),
    NAOS_ADD_TEST(attributes_have_their_defaults),
    NAOS_ADD_TEST(heading_levels_are_1_to_6),
    NAOS_ADD_TEST(list_types_starts_and_tightness_are_checked),
    NAOS_ADD_TEST(a_type_mismatch_gets_defaults_and_rejects_setting),
    NAOS_ADD_TEST(strings_pass_the_common_check_and_are_copied),
    NAOS_ADD_TEST(info_and_destinations_reject_line_endings),
    NAOS_ADD_TEST(titles_reject_blank_lines_and_normalize_to_lf),
    NAOS_ADD_TEST(labels_have_1_to_999_code_points),
    NAOS_ADD_TEST(labels_reject_unescaped_brackets_blank_lines_and_only_whitespace),
    NAOS_ADD_TEST(only_another_attribute_value_records_content_while_tracking),
    NAOS_ADD_TEST(each_attribute_change_records_content),
    NAOS_ADD_TEST(setting_attributes_keeps_the_tokens),
    NAOS_ADD_TEST(setting_string_attributes_passes_the_out_of_memory_sweep))

NAOS_MAIN(
    NAOS_ADD_SUITE(text_helpers),
    NAOS_ADD_SUITE(literal),
    NAOS_ADD_SUITE(inline_text),
    NAOS_ADD_SUITE(attr)
)
