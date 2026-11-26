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

NAOS_MAIN(
    NAOS_ADD_SUITE(text_helpers),
    NAOS_ADD_SUITE(literal),
    NAOS_ADD_SUITE(inline_text)
)
