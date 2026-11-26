/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The tests of DefinitionIndex (syntax-tree).
 *
 * The label normalization (9.1),
 * which strips the leading and trailing whitespace of CommonMark labels (space,
 * tab, LF, CR), collapses each internal run of it to one space and applies the
 * full case fold (amark_unicode_case_fold) to every code point; the other
 * Unicode spaces (a full-width space, a no-break space) and the form feed are
 * ordinary characters. The result is appended to the output buffer, which keeps
 * its content on failure.
 *
 * The definition index and the resolution of the reference links (9.2-9.6,
 * 14.8): the definitions are looked up by their normalized label; only those
 * attached to the document node are candidates, and among several the first in
 * document order wins. The operations that may change a label only put the
 * definition on the dirty list, which the next lookup processes; the result is
 * kept while the structure generation is the same. The destination and the
 * title of a reference link or image (full, collapsed, shortcut) are those of
 * the definition of its label, or UNRESOLVED without one.
 */
#include "tree/tree.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <naos/naos.h>

#include "test_allocator.h"

/* ---- Helpers ------------------------------------------------------------------ */

/* Normalizes the NUL-terminated label into a new buffer and returns whether the
 * result is expected (also NUL-terminated). */
static int normalizes_to(const char *label, const char *expected)
{
    amark_buffer_t *out = NULL;
    int same;

    if (amark_buffer_create(&out) != AMARK_ERROR_NONE)
    {
        return 0;
    }
    same = (amark_label_normalize(label, strlen(label), out) == AMARK_ERROR_NONE) &&
           (amark_buffer_length(out) == strlen(expected)) &&
           (memcmp(amark_buffer_data(out), expected, strlen(expected)) == 0);
    amark_buffer_destroy(out);
    return same;
}

/* ---- Whitespace ----------------------------------------------------------------- */

NAOS_TEST(ascii_labels_are_folded_and_their_spaces_collapsed)
{
    NAOS_CHECK_TRUE(normalizes_to("Foo", "foo"));
    NAOS_CHECK_TRUE(normalizes_to("  Foo   BAR  ", "foo bar"));
    NAOS_CHECK_TRUE(normalizes_to("\tfoo\t \tbar\t", "foo bar"));
    NAOS_CHECK_TRUE(normalizes_to("a b c", "a b c"));
    NAOS_CHECK_TRUE(normalizes_to("[Foo*bar\\]]", "[foo*bar\\]]"));
}

NAOS_TEST(full_width_space_and_no_break_space_are_ordinary_characters)
{
    /* U+3000 IDEOGRAPHIC SPACE around and between the words: kept as is. */
    NAOS_CHECK_TRUE(normalizes_to("\xE3\x80\x80" "Foo" "\xE3\x80\x80\xE3\x80\x80" "Bar"
                                  "\xE3\x80\x80",
                                  "\xE3\x80\x80" "foo" "\xE3\x80\x80\xE3\x80\x80" "bar"
                                  "\xE3\x80\x80"));
    /* The ASCII spaces around a full-width space are stripped; it stays. */
    NAOS_CHECK_TRUE(normalizes_to(" \xE3\x80\x80 ", "\xE3\x80\x80"));
    /* U+00A0 NO-BREAK SPACE and U+2003 EM SPACE are kept; the runs of ASCII
     * spaces between them each become one space. */
    NAOS_CHECK_TRUE(normalizes_to("A  \xC2\xA0\t\xE2\x80\x83  B", "a \xC2\xA0 \xE2\x80\x83 b"));
    /* A label of full-width letters: U+FF21 FULLWIDTH A folds to U+FF41. */
    NAOS_CHECK_TRUE(normalizes_to("\xEF\xBC\xA1\xE3\x80\x80\xEF\xBC\xA2",
                                  "\xEF\xBD\x81\xE3\x80\x80\xEF\xBD\x82"));
}

NAOS_TEST(line_breaks_are_whitespace)
{
    NAOS_CHECK_TRUE(normalizes_to("foo\nbar", "foo bar"));
    NAOS_CHECK_TRUE(normalizes_to("foo\r\nbar", "foo bar"));
    NAOS_CHECK_TRUE(normalizes_to("foo\rbar", "foo bar"));
    NAOS_CHECK_TRUE(normalizes_to("foo \n\t  Bar", "foo bar"));
    NAOS_CHECK_TRUE(normalizes_to("\nFoo\r\n", "foo"));
    NAOS_CHECK_TRUE(normalizes_to("a\r\r\nb", "a b"));
    NAOS_CHECK_TRUE(normalizes_to("\r\n\r\nFoo \r\n\t bar\n\r", "foo bar"));
    NAOS_CHECK_TRUE(normalizes_to("foo\n\nbar", "foo bar"));
}

NAOS_TEST(characters_outside_the_whitespace_definition_are_kept)
{
    /* U+000C (form feed), U+000B (vertical tab), U+0085 (next line) and
     * U+200B (zero width space) are not whitespace of a label. */
    NAOS_CHECK_TRUE(normalizes_to("a\fb", "a\fb"));
    NAOS_CHECK_TRUE(normalizes_to(" \f ", "\f"));
    NAOS_CHECK_TRUE(normalizes_to("a\vb", "a\vb"));
    NAOS_CHECK_TRUE(normalizes_to("\xC2\x85" "a", "\xC2\x85" "a"));
    NAOS_CHECK_TRUE(normalizes_to("\xE2\x80\x8B" "a", "\xE2\x80\x8B" "a"));
}

NAOS_TEST(a_label_of_whitespace_only_normalizes_to_empty)
{
    amark_buffer_t *out = NULL;

    NAOS_CHECK_TRUE(normalizes_to("", ""));
    NAOS_CHECK_TRUE(normalizes_to(" ", ""));
    NAOS_CHECK_TRUE(normalizes_to(" \t\r\n \r\n\t ", ""));

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_create(&out));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_label_normalize(NULL, 0, out));
    NAOS_CHECK_UINT_EQ(0, amark_buffer_length(out));
    amark_buffer_destroy(out);
}

/* ---- Case fold ------------------------------------------------------------------ */

NAOS_TEST(sharp_s_and_capital_sharp_s_fold_to_ss)
{
    NAOS_CHECK_TRUE(normalizes_to("\xC3\x9F", "ss"));         /* U+00DF */
    NAOS_CHECK_TRUE(normalizes_to("\xE1\xBA\x9E", "ss"));     /* U+1E9E */
    NAOS_CHECK_TRUE(normalizes_to("Stra\xC3\x9F" "e", "strasse"));
    NAOS_CHECK_TRUE(normalizes_to("STRA\xE1\xBA\x9E" "E", "strasse"));
    NAOS_CHECK_TRUE(normalizes_to("STRASSE", "strasse"));
}

NAOS_TEST(folds_to_several_code_points_are_written_in_full)
{
    /* U+0130 LATIN CAPITAL LETTER I WITH DOT ABOVE -> U+0069 U+0307. */
    NAOS_CHECK_TRUE(normalizes_to("\xC4\xB0", "i\xCC\x87"));
    /* U+FB03 LATIN SMALL LIGATURE FFI -> "ffi". */
    NAOS_CHECK_TRUE(normalizes_to("o\xEF\xAC\x83" "ce", "office"));
    /* U+0390 -> U+03B9 U+0308 U+0301 (three code points). */
    NAOS_CHECK_TRUE(normalizes_to("\xCE\x90", "\xCE\xB9\xCC\x88\xCC\x81"));
    /* U+0149 -> U+02BC U+006E, between whitespace. */
    NAOS_CHECK_TRUE(normalizes_to(" \xC5\x89  \xC5\x89 ", "\xCA\xBC" "n \xCA\xBC" "n"));
}

NAOS_TEST(final_sigma_and_sigma_fold_alike)
{
    /* U+0391 U+03A3 and U+03B1 U+03C2 both give U+03B1 U+03C3. */
    NAOS_CHECK_TRUE(normalizes_to("\xCE\x91\xCE\xA3", "\xCE\xB1\xCF\x83"));
    NAOS_CHECK_TRUE(normalizes_to("\xCE\xB1\xCF\x82", "\xCE\xB1\xCF\x83"));
}

NAOS_TEST(invalid_utf8_becomes_replacement_and_nul_is_kept)
{
    static const char label[] = {'A', '\0', ' ', '\xFF', ' ', '\xE3', '\x80'};
    static const char expected[] = {'a', '\0', ' ', '\xEF', '\xBF', '\xBD', ' ',
                                    '\xEF', '\xBF', '\xBD'};
    amark_buffer_t *out = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_create(&out));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_label_normalize(label, sizeof(label), out));
    NAOS_CHECK_UINT_EQ(sizeof(expected), amark_buffer_length(out));
    NAOS_CHECK_TRUE(memcmp(amark_buffer_data(out), expected, sizeof(expected)) == 0);
    amark_buffer_destroy(out);
}

/* ---- The output ----------------------------------------------------------------- */

NAOS_TEST(the_result_is_appended_to_the_buffer)
{
    amark_buffer_t *out = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_create(&out));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_cstr(out, "key:"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_label_normalize(" A  B ", 6, out));
    NAOS_CHECK_STR_EQ("key:a b", amark_buffer_data(out));
    /* Only length bytes are read. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_label_normalize("CdEF", 2, out));
    NAOS_CHECK_STR_EQ("key:a bcd", amark_buffer_data(out));
    amark_buffer_destroy(out);
}

NAOS_TEST(invalid_arguments_leave_the_buffer_unchanged)
{
    amark_buffer_t *out = NULL;

    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_label_normalize("a", 1, NULL));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_create(&out));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_cstr(out, "x"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_label_normalize(NULL, 1, out));
    NAOS_CHECK_STR_EQ("x", amark_buffer_data(out));
    amark_buffer_destroy(out);
}

/* Normalizes a long label whose fold is longer than itself (U+0130 takes two
 * bytes and folds to three) into a buffer that already holds "x", so that the
 * buffer grows both before and during the normalization: on failure the buffer
 * still holds "x". */
static amark_error_t normalize_oom_scenario(void *context)
{
    static const char unit[] = "\xE1\xBA\x9E \xC4\xB0\xC4\xB0\xC4\xB0\xC4\xB0\n";
    char label[64 * (sizeof(unit) - 1)];
    amark_buffer_t *out = NULL;
    amark_error_t error;
    size_t i;

    (void)context;
    for (i = 0; i < 64; i++)
    {
        memcpy(label + i * (sizeof(unit) - 1), unit, sizeof(unit) - 1);
    }
    error = amark_buffer_create(&out);
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_buffer_append_byte(out, 'x');
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_label_normalize(label, sizeof(label), out);
        if (error != AMARK_ERROR_NONE)
        {
            if ((amark_buffer_length(out) != 1) || (amark_buffer_data(out)[0] != 'x'))
            {
                error = AMARK_ERROR_INVALID_STATE;
            }
        }
        else if ((amark_buffer_length(out) != 1 + 64 * 16 - 1) ||
                 (memcmp(amark_buffer_data(out), "xss i\xCC\x87i\xCC\x87i\xCC\x87i\xCC\x87 ss",
                         19) != 0) ||
                 (amark_buffer_data(out)[1 + 64 * 16 - 2] != '\x87'))
        {
            error = AMARK_ERROR_INVALID_STATE;
        }
    }
    amark_buffer_destroy(out);
    return error;
}

NAOS_TEST(out_of_memory_leaves_the_buffer_unchanged)
{
    amark_test_oom_result_t result =
        amark_test_oom_sweep(amark_allocator_set, normalize_oom_scenario, NULL,
                             AMARK_TEST_OOM_MAX_ITERATIONS);

    NAOS_CHECK_TRUE(amark_test_oom_check(result, "normalize_oom_scenario", __FILE__, __LINE__));
    /* The creation, the "x", the reservation and a growth during the loop. */
    NAOS_CHECK_UINT_EQ(4, result.iterations);
}

/* ---- The definition index: helpers -------------------------------------------- */

/* Checks that document keeps the internal invariants (amark_tree_check). */
static void check_tree_ok(amark_document_t *document)
{
    amark_tree_check_report_t report;

    NAOS_CHECK_STR_EQ("none", amark_tree_violation_name(amark_tree_check(document, &report)));
    NAOS_CHECK_NULL(report.message);
}

static amark_document_t *new_document(void)
{
    amark_document_t *document = NULL;

    return amark_document_create(NULL, &document) == AMARK_ERROR_NONE ? document : NULL;
}

/* A new node of type appended to parent (none when parent is NULL). */
static amark_node_t *new_node(amark_document_t *document, amark_node_t *parent,
                              amark_node_type_t type)
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

static amark_error_t record(amark_node_t *node, amark_facet_t facet, const char *text)
{
    return amark_node_record_string(node, facet, text, strlen(text));
}

/* A new link_definition under parent (none for NULL) with the recorded label,
 * destination and title. */
static amark_node_t *new_definition(amark_document_t *document, amark_node_t *parent,
                                    const char *label, const char *destination,
                                    const char *title)
{
    amark_node_t *definition = new_node(document, parent, AMARK_NODE_LINK_DEFINITION);

    if ((definition == NULL) ||
        (record(definition, AMARK_LINK_DEFINITION_LABEL, label) != AMARK_ERROR_NONE) ||
        (record(definition, AMARK_LINK_DEFINITION_DESTINATION, destination) !=
         AMARK_ERROR_NONE) ||
        (record(definition, AMARK_LINK_DEFINITION_TITLE, title) != AMARK_ERROR_NONE))
    {
        return NULL;
    }
    return definition;
}

/* A new paragraph under parent holding a new link (or image) of form with the
 * recorded label; returns the link. */
static amark_node_t *new_reference(amark_document_t *document, amark_node_t *parent,
                                   amark_node_type_t type, int64_t form, const char *label)
{
    amark_node_t *paragraph = new_node(document, parent, AMARK_NODE_PARAGRAPH);
    amark_node_t *link = new_node(document, paragraph, type);

    if ((link == NULL) || (amark_node_record_int(link, AMARK_LINK_FORM, form) != AMARK_ERROR_NONE) ||
        (record(link, AMARK_LINK_LABEL, label) != AMARK_ERROR_NONE))
    {
        return NULL;
    }
    return link;
}

/* The definition found for label (NULL when there is none or the lookup fails). */
static amark_node_t *find(amark_document_t *document, const char *label)
{
    amark_node_t *found = (amark_node_t *)&found;

    if (amark_document_find_definition(document, label, strlen(label), &found) !=
        AMARK_ERROR_NONE)
    {
        return NULL;
    }
    return found;
}

static amark_value_t get(const amark_node_t *node, amark_facet_t facet)
{
    amark_value_t value;

    memset(&value, 0, sizeof(value));
    value.struct_size = sizeof(value);
    if (amark_node_get(node, facet, &value) != AMARK_ERROR_NONE)
    {
        value.state = (amark_value_state_t)0;
    }
    return value;
}

/* Whether facet of node reads as the PRESENT string expected. */
static int reads(const amark_node_t *node, amark_facet_t facet, const char *expected)
{
    amark_value_t value = get(node, facet);

    return (value.state == AMARK_VALUE_PRESENT) && (value.text != NULL) &&
           (value.length == strlen(expected)) && (strcmp(value.text, expected) == 0);
}

/* Whether facet of node reads as UNRESOLVED (no number, no text). */
static int unresolved(const amark_node_t *node, amark_facet_t facet)
{
    amark_value_t value = get(node, facet);

    return (value.state == AMARK_VALUE_UNRESOLVED) && (value.number == 0) &&
           (value.text == NULL) && (value.length == 0);
}

/* Adds a TEXT content token spelling the label on [start, end) of the content
 * of definition. */
static amark_error_t add_label_token(amark_node_t *definition, size_t start, size_t end)
{
    amark_content_token_t token;

    memset(&token, 0, sizeof(token));
    token.struct_size = sizeof(token);
    token.start = start;
    token.end = end;
    token.kind = AMARK_SPELL_TEXT;
    token.facet = AMARK_LINK_DEFINITION_LABEL;
    return amark_node_add_content_token(definition, definition, &token);
}

/* ---- The definition index: lookups ------------------------------------------------ */

NAOS_TEST(the_first_definition_in_document_order_is_returned)
{
    amark_document_t *document = new_document();
    amark_node_t *root = amark_document_root(document);
    amark_node_t *quote;
    amark_node_t *inner;
    amark_node_t *outer;
    amark_node_t *earliest;

    NAOS_REQUIRE_TRUE(document != NULL);
    /* Created in reverse order: the order of creation does not matter. */
    outer = new_definition(document, NULL, "FOO", "/outer", "");
    quote = new_node(document, root, AMARK_NODE_BLOCK_QUOTE);
    inner = new_definition(document, quote, "Foo", "/inner", "");
    NAOS_REQUIRE_TRUE((outer != NULL) && (inner != NULL));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(root, outer));

    /* [Foo] in the quote comes before [FOO] after it. */
    NAOS_CHECK_PTR_EQ(inner, find(document, "foo"));
    NAOS_CHECK_PTR_EQ(inner, find(document, "  FOO "));
    /* The same result again (cached). */
    NAOS_CHECK_PTR_EQ(inner, find(document, "fOO"));

    /* A definition placed before the quote wins (the structure changed). */
    earliest = new_definition(document, NULL, "foo", "/earliest", "");
    NAOS_REQUIRE_TRUE(earliest != NULL);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_prepend_child(root, earliest));
    NAOS_CHECK_PTR_EQ(earliest, find(document, "foo"));

    /* Siblings under one parent, and an ancestor's sibling: move the outer one
     * first, then the inner one before it inside a new quote. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_before(earliest, outer));
    NAOS_CHECK_PTR_EQ(outer, find(document, "foo"));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_before(outer, quote));
    NAOS_CHECK_PTR_EQ(inner, find(document, "foo"));

    /* Another label is not a candidate. */
    NAOS_CHECK_NULL(find(document, "bar"));
    check_tree_ok(document);
    amark_document_destroy(document);
}

NAOS_TEST(definitions_deep_in_the_tree_are_ordered_by_their_ancestors)
{
    amark_document_t *document = new_document();
    amark_node_t *root = amark_document_root(document);
    amark_node_t *list;
    amark_node_t *first_item;
    amark_node_t *second_item;
    amark_node_t *quote;
    amark_node_t *deep;
    amark_node_t *shallow;

    NAOS_REQUIRE_TRUE(document != NULL);
    list = new_node(document, root, AMARK_NODE_LIST);
    first_item = new_node(document, list, AMARK_NODE_ITEM);
    second_item = new_node(document, list, AMARK_NODE_ITEM);
    quote = new_node(document, first_item, AMARK_NODE_BLOCK_QUOTE);
    deep = new_definition(document, quote, "x", "/deep", "");
    shallow = new_definition(document, second_item, "X", "/shallow", "");
    NAOS_REQUIRE_TRUE((deep != NULL) && (shallow != NULL));

    NAOS_CHECK_PTR_EQ(deep, find(document, "x"));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(list, first_item));
    NAOS_CHECK_PTR_EQ(shallow, find(document, "x"));
    /* A paragraph between does not matter; the deeper one is first again. */
    NAOS_REQUIRE_TRUE(new_node(document, second_item, AMARK_NODE_PARAGRAPH) != NULL);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_prepend_child(root, quote));
    NAOS_CHECK_PTR_EQ(deep, find(document, "x"));
    check_tree_ok(document);
    amark_document_destroy(document);
}

NAOS_TEST(detached_definitions_are_not_candidates)
{
    amark_document_t *document = new_document();
    amark_node_t *root = amark_document_root(document);
    amark_node_t *quote;
    amark_node_t *first;
    amark_node_t *second;
    amark_node_t *loose;

    NAOS_REQUIRE_TRUE(document != NULL);
    quote = new_node(document, root, AMARK_NODE_BLOCK_QUOTE);
    first = new_definition(document, quote, "a", "/1", "");
    second = new_definition(document, root, "A", "/2", "");
    /* Never attached. */
    loose = new_definition(document, NULL, "a", "/loose", "");
    NAOS_REQUIRE_TRUE((first != NULL) && (second != NULL) && (loose != NULL));
    NAOS_CHECK_PTR_EQ(first, find(document, "a"));

    /* The quote, with the first definition in it, is detached. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_unlink(quote));
    NAOS_CHECK_PTR_EQ(second, find(document, "a"));
    /* The second one too: no candidate left. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_unlink(second));
    NAOS_CHECK_NULL(find(document, "a"));
    /* A detached tree that holds a definition is not attached either. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(quote, second));
    NAOS_CHECK_NULL(find(document, "a"));
    /* Attached again. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(root, quote));
    NAOS_CHECK_PTR_EQ(first, find(document, "a"));

    /* A destroyed definition leaves the index. */
    amark_node_destroy(first);
    NAOS_CHECK_PTR_EQ(second, find(document, "a"));
    check_tree_ok(document);
    amark_node_destroy(quote);
    amark_node_destroy(loose);
    NAOS_CHECK_NULL(find(document, "a"));
    check_tree_ok(document);
    amark_document_destroy(document);
}

NAOS_TEST(a_destroyed_dirty_definition_leaves_the_dirty_list)
{
    amark_document_t *document = new_document();
    amark_node_t *root = amark_document_root(document);
    amark_node_t *first;
    amark_node_t *second;
    amark_node_t *third;

    NAOS_REQUIRE_TRUE(document != NULL);
    first = new_definition(document, root, "a", "/1", "");
    second = new_definition(document, root, "b", "/2", "");
    third = new_definition(document, root, "c", "/3", "");
    NAOS_REQUIRE_TRUE((first != NULL) && (second != NULL) && (third != NULL));
    /* Recorded but not looked up yet: all three wait on the dirty list. */
    NAOS_CHECK_TRUE(first->def_dirty && second->def_dirty && third->def_dirty);
    NAOS_CHECK_UINT_EQ(3, document->def_index.dirty_count);
    check_tree_ok(document);
    amark_node_destroy(second);
    NAOS_CHECK_UINT_EQ(2, document->def_index.dirty_count);
    check_tree_ok(document);
    NAOS_CHECK_PTR_EQ(third, find(document, "c"));
    NAOS_CHECK_FALSE(first->def_dirty || third->def_dirty);
    NAOS_CHECK_UINT_EQ(0, document->def_index.dirty_count);
    NAOS_CHECK_NULL(find(document, "b"));
    check_tree_ok(document);
    amark_document_destroy(document);
}

/* ---- The definition index: label changes ----------------------------------------- */

NAOS_TEST(a_label_change_is_seen_by_the_next_lookup)
{
    amark_document_t *document = new_document();
    amark_node_t *root = amark_document_root(document);
    amark_node_t *first;
    amark_node_t *second;

    NAOS_REQUIRE_TRUE(document != NULL);
    first = new_definition(document, root, "one", "/1", "");
    second = new_definition(document, root, "two", "/2", "");
    NAOS_REQUIRE_TRUE((first != NULL) && (second != NULL));
    NAOS_CHECK_PTR_EQ(first, find(document, "one"));
    NAOS_CHECK_PTR_EQ(second, find(document, "two"));

    /* Set: the first one becomes "two" and wins over the second one. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_set_string(first, AMARK_LINK_DEFINITION_LABEL, "Two", 3));
    NAOS_CHECK_TRUE(first->def_dirty);
    NAOS_CHECK_PTR_EQ(first, find(document, "two"));
    NAOS_CHECK_NULL(find(document, "one"));
    /* Record. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, record(first, AMARK_LINK_DEFINITION_LABEL, "three"));
    NAOS_CHECK_PTR_EQ(second, find(document, "two"));
    NAOS_CHECK_PTR_EQ(first, find(document, "THREE"));
    check_tree_ok(document);
    amark_document_destroy(document);
}

NAOS_TEST(label_tokens_and_dropped_spelling_are_seen_by_the_next_lookup)
{
    static const char text[] = "[Foo bar]: /url";
    amark_document_t *document = new_document();
    amark_node_t *root = amark_document_root(document);
    amark_node_t *definition;
    amark_node_t *other;

    NAOS_REQUIRE_TRUE(document != NULL);
    definition = new_definition(document, root, "recorded", "/url", "");
    NAOS_REQUIRE_TRUE(definition != NULL);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_block_set_free_content(definition, text, sizeof(text) - 1));
    NAOS_CHECK_PTR_EQ(definition, find(document, "recorded"));

    /* A token record: the label is now derived from "Foo". */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, add_label_token(definition, 1, 4));
    NAOS_CHECK_TRUE(definition->def_dirty);
    NAOS_CHECK_NULL(find(document, "recorded"));
    NAOS_CHECK_PTR_EQ(definition, find(document, "foo"));
    /* Another token: "Foo bar" (the space is spelled too). */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, add_label_token(definition, 4, 8));
    NAOS_CHECK_PTR_EQ(definition, find(document, "foo  BAR"));
    NAOS_CHECK_NULL(find(document, "foo"));
    /* A split keeps the label. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_split_content_token(definition, 1, 5));
    NAOS_CHECK_TRUE(definition->def_dirty);
    NAOS_CHECK_PTR_EQ(definition, find(document, "foo bar"));
    check_tree_ok(document);

    /* A move of the tokens to another definition on the same content: the
     * label of the first is the recorded one again, the other gets "Foo bar". */
    other = new_node(document, NULL, AMARK_NODE_LINK_DEFINITION);
    NAOS_REQUIRE_TRUE(other != NULL);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(root, other));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_move_content_tokens(definition, other, 0, 3));
    NAOS_CHECK_TRUE(definition->def_dirty && other->def_dirty);
    NAOS_CHECK_PTR_EQ(definition, find(document, "recorded"));
    NAOS_CHECK_PTR_EQ(other, find(document, "foo bar"));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_move_content_tokens(other, definition, 0, 3));
    NAOS_CHECK_PTR_EQ(definition, find(document, "foo bar"));
    NAOS_CHECK_NULL(find(document, "recorded"));
    check_tree_ok(document);

    /* Dropping the spelling of the label keeps its value as the set value. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_discard_facet_spelling(definition,
                                                          AMARK_LINK_DEFINITION_LABEL));
    NAOS_CHECK_TRUE(definition->def_dirty);
    NAOS_CHECK_PTR_EQ(definition, find(document, "foo bar"));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_discard_spelling(definition));
    NAOS_CHECK_PTR_EQ(definition, find(document, "foo bar"));
    check_tree_ok(document);
    amark_document_destroy(document);
}

NAOS_TEST(definitions_without_a_label_are_not_indexed)
{
    amark_document_t *document = new_document();
    amark_node_t *root = amark_document_root(document);
    amark_node_t *definition;
    amark_node_t *found = (amark_node_t *)&found;

    NAOS_REQUIRE_TRUE(document != NULL);
    definition = new_node(document, root, AMARK_NODE_LINK_DEFINITION);
    NAOS_REQUIRE_TRUE(definition != NULL);
    /* The label is "" (its default value): nothing to find. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_find_definition(document, "", 0, &found));
    NAOS_CHECK_NULL(found);
    NAOS_CHECK_NULL(find(document, " \t\n"));
    NAOS_CHECK_NULL(definition->def_group);
    NAOS_CHECK_FALSE(definition->def_dirty);
    check_tree_ok(document);
    amark_document_destroy(document);
}

NAOS_TEST(find_definition_checks_its_arguments)
{
    amark_document_t *document = new_document();
    amark_node_t *found = (amark_node_t *)&found;

    NAOS_REQUIRE_TRUE(document != NULL);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_document_find_definition(NULL, "a", 1, &found));
    NAOS_CHECK_NULL(found);
    found = (amark_node_t *)&found;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_document_find_definition(document, NULL, 1, &found));
    NAOS_CHECK_NULL(found);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_document_find_definition(document, "a", 1, NULL));
    found = (amark_node_t *)&found;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_find_definition(document, NULL, 0, &found));
    NAOS_CHECK_NULL(found);
    amark_document_destroy(document);
}

/* ---- Reference links ------------------------------------------------------------- */

NAOS_TEST(reference_links_and_images_resolve_through_the_index)
{
    static const int64_t forms[] = {AMARK_LINK_FORM_FULL, AMARK_LINK_FORM_COLLAPSED,
                                    AMARK_LINK_FORM_SHORTCUT};
    amark_document_t *document = new_document();
    amark_node_t *root = amark_document_root(document);
    size_t i;

    NAOS_REQUIRE_TRUE(document != NULL);
    NAOS_REQUIRE_TRUE(new_definition(document, root, "Foo Bar", "/url", "the title") != NULL);
    for (i = 0; i < 3; i++)
    {
        amark_node_t *link =
            new_reference(document, root, AMARK_NODE_LINK, forms[i], "foo\n  BAR");
        amark_node_t *image =
            new_reference(document, root, AMARK_NODE_IMAGE, forms[i], "FOO bar");

        NAOS_REQUIRE_TRUE((link != NULL) && (image != NULL));
        NAOS_CHECK_TRUE(reads(link, AMARK_LINK_DESTINATION, "/url"));
        NAOS_CHECK_TRUE(reads(link, AMARK_LINK_TITLE, "the title"));
        NAOS_CHECK_TRUE(reads(link, AMARK_LINK_LABEL, "foo\n  BAR"));
        NAOS_CHECK_TRUE(reads(image, AMARK_IMAGE_DESTINATION, "/url"));
        NAOS_CHECK_TRUE(reads(image, AMARK_IMAGE_TITLE, "the title"));
        NAOS_CHECK_TRUE(reads(image, AMARK_IMAGE_LABEL, "FOO bar"));
    }
    check_tree_ok(document);
    amark_document_destroy(document);
}

NAOS_TEST(other_forms_read_their_own_values)
{
    amark_document_t *document = new_document();
    amark_node_t *root = amark_document_root(document);
    amark_node_t *inline_link;
    amark_node_t *autolink;
    amark_node_t *no_form;

    NAOS_REQUIRE_TRUE(document != NULL);
    NAOS_REQUIRE_TRUE(new_definition(document, root, "a", "/definition", "t") != NULL);
    inline_link = new_reference(document, root, AMARK_NODE_LINK, AMARK_LINK_FORM_INLINE, "a");
    autolink = new_reference(document, root, AMARK_NODE_LINK, AMARK_LINK_FORM_AUTOLINK, "a");
    no_form = new_node(document, new_node(document, root, AMARK_NODE_PARAGRAPH), AMARK_NODE_LINK);
    NAOS_REQUIRE_TRUE((inline_link != NULL) && (autolink != NULL) && (no_form != NULL));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, record(inline_link, AMARK_LINK_DESTINATION, "/own"));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, record(no_form, AMARK_LINK_LABEL, "a"));
    NAOS_CHECK_TRUE(reads(inline_link, AMARK_LINK_DESTINATION, "/own"));
    NAOS_CHECK_TRUE(reads(inline_link, AMARK_LINK_TITLE, ""));
    NAOS_CHECK_TRUE(reads(autolink, AMARK_LINK_DESTINATION, ""));
    NAOS_CHECK_TRUE(reads(no_form, AMARK_LINK_DESTINATION, ""));
    amark_document_destroy(document);
}

NAOS_TEST(a_reference_without_a_definition_is_unresolved)
{
    amark_document_t *document = new_document();
    amark_node_t *root = amark_document_root(document);
    amark_node_t *link;
    amark_node_t *definition;

    NAOS_REQUIRE_TRUE(document != NULL);
    link = new_reference(document, root, AMARK_NODE_LINK, AMARK_LINK_FORM_FULL, "missing");
    NAOS_REQUIRE_TRUE(link != NULL);
    NAOS_CHECK_TRUE(unresolved(link, AMARK_LINK_DESTINATION));
    NAOS_CHECK_TRUE(unresolved(link, AMARK_LINK_TITLE));
    /* The label is still there. */
    NAOS_CHECK_TRUE(reads(link, AMARK_LINK_LABEL, "missing"));
    /* An empty label resolves to nothing either. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, record(link, AMARK_LINK_LABEL, ""));
    NAOS_CHECK_TRUE(unresolved(link, AMARK_LINK_DESTINATION));

    /* A detached definition does not resolve it; an attached one does. */
    definition = new_definition(document, NULL, "Missing", "/found", "");
    NAOS_REQUIRE_TRUE(definition != NULL);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, record(link, AMARK_LINK_LABEL, "missing"));
    NAOS_CHECK_TRUE(unresolved(link, AMARK_LINK_DESTINATION));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(root, definition));
    NAOS_CHECK_TRUE(reads(link, AMARK_LINK_DESTINATION, "/found"));
    NAOS_CHECK_TRUE(reads(link, AMARK_LINK_TITLE, ""));
    check_tree_ok(document);
    amark_document_destroy(document);
}

NAOS_TEST(setting_the_label_of_a_reference_resolves_the_new_label)
{
    amark_document_t *document = new_document();
    amark_node_t *root = amark_document_root(document);
    amark_node_t *link;
    amark_node_t *first;

    NAOS_REQUIRE_TRUE(document != NULL);
    first = new_definition(document, root, "first", "/1", "one");
    NAOS_REQUIRE_TRUE(first != NULL);
    NAOS_REQUIRE_TRUE(new_definition(document, root, "second", "/2", "two") != NULL);
    link = new_reference(document, root, AMARK_NODE_LINK, AMARK_LINK_FORM_SHORTCUT, "first");
    NAOS_REQUIRE_TRUE(link != NULL);
    NAOS_CHECK_TRUE(reads(link, AMARK_LINK_DESTINATION, "/1"));

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_set_string(link, AMARK_LINK_LABEL, "SECOND", 6));
    NAOS_CHECK_TRUE(reads(link, AMARK_LINK_LABEL, "SECOND"));
    NAOS_CHECK_TRUE(reads(link, AMARK_LINK_DESTINATION, "/2"));
    NAOS_CHECK_TRUE(reads(link, AMARK_LINK_TITLE, "two"));
    /* The definition changes: the link follows. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_set_string(first, AMARK_LINK_DEFINITION_LABEL, "second", 6));
    NAOS_CHECK_TRUE(reads(link, AMARK_LINK_DESTINATION, "/1"));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_set_string(first, AMARK_LINK_DEFINITION_DESTINATION, "/new",
                                              4));
    NAOS_CHECK_TRUE(reads(link, AMARK_LINK_DESTINATION, "/new"));
    check_tree_ok(document);
    amark_document_destroy(document);
}

/* ---- The cached results ------------------------------------------------------------ */

NAOS_TEST(the_result_is_kept_while_the_structure_is_unchanged)
{
    amark_document_t *document = new_document();
    amark_node_t *root = amark_document_root(document);
    amark_node_t *first;
    amark_node_t *second;
    size_t steps;

    NAOS_REQUIRE_TRUE(document != NULL);
    first = new_definition(document, root, "a", "/1", "");
    second = new_definition(document, root, "a", "/2", "");
    NAOS_REQUIRE_TRUE((first != NULL) && (second != NULL));
    NAOS_CHECK_PTR_EQ(first, find(document, "a"));
    /* Kept: no step. */
    steps = document->stats.index_steps;
    NAOS_CHECK_PTR_EQ(first, find(document, "A"));
    NAOS_CHECK_UINT_EQ(steps, document->stats.index_steps);
    /* A change of a value does not change the structure, nor the result. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, record(first, AMARK_LINK_DEFINITION_DESTINATION, "/x"));
    NAOS_CHECK_PTR_EQ(first, find(document, "a"));

    /* A structural change, even elsewhere, decides again. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_before(first, second));
    NAOS_CHECK_PTR_EQ(second, find(document, "a"));
    NAOS_CHECK_TRUE(document->stats.index_steps > steps);
    /* A label change of a candidate decides again too. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, record(second, AMARK_LINK_DEFINITION_LABEL, "b"));
    NAOS_CHECK_PTR_EQ(first, find(document, "a"));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, record(second, AMARK_LINK_DEFINITION_LABEL, "a"));
    NAOS_CHECK_PTR_EQ(second, find(document, "a"));
    check_tree_ok(document);
    amark_document_destroy(document);
}

/* ---- Complexity ---------------------------------------------------------------------- */

/* The index steps of the first lookup, after a structural change, of the only
 * definition of a document whose tree is depth block quotes deep, with many
 * other nodes around. */
static size_t lookup_steps(size_t depth, size_t others)
{
    amark_document_t *document = new_document();
    amark_node_t *parent;
    amark_node_t *definition;
    amark_node_t *spare;
    size_t i;
    size_t steps = (size_t)-1;

    if (document == NULL)
    {
        return steps;
    }
    parent = amark_document_root(document);
    for (i = 0; i < others; i++)
    {
        if (new_definition(document, parent, "other", "/o", "") == NULL)
        {
            goto done;
        }
    }
    for (i = 0; i < depth; i++)
    {
        parent = new_node(document, parent, AMARK_NODE_BLOCK_QUOTE);
        if (parent == NULL)
        {
            goto done;
        }
    }
    definition = new_definition(document, parent, "label", "/url", "");
    spare = new_node(document, amark_document_root(document), AMARK_NODE_PARAGRAPH);
    if ((definition == NULL) || (spare == NULL) || (find(document, "other") == NULL) ||
        (find(document, "label") != definition))
    {
        goto done;
    }
    /* A structural change: the result is decided again. */
    amark_node_destroy(spare);
    steps = document->stats.index_steps;
    if (find(document, "label") != definition)
    {
        steps = (size_t)-1;
        goto done;
    }
    steps = document->stats.index_steps - steps;
done:
    amark_document_destroy(document);
    return steps;
}

NAOS_TEST(a_lookup_of_one_definition_is_proportional_to_the_depth)
{
    size_t shallow = lookup_steps(100, 10);
    size_t deep = lookup_steps(1000, 10);
    size_t crowded = lookup_steps(100, 500);

    NAOS_CHECK_TRUE(shallow != (size_t)-1);
    NAOS_CHECK_TRUE(deep != (size_t)-1);
    /* One step per ancestor, and a few more. */
    NAOS_CHECK_TRUE((shallow >= 100) && (shallow <= 110));
    NAOS_CHECK_TRUE((deep >= 1000) && (deep <= 1010));
    /* The other definitions and nodes do not count. */
    NAOS_CHECK_UINT_EQ(shallow, crowded);
}

/* ---- The internal check -------------------------------------------------------------- */

NAOS_TEST(the_check_finds_a_broken_index)
{
    amark_document_t *document = new_document();
    amark_node_t *root = amark_document_root(document);
    amark_node_t *first;
    amark_node_t *second;
    amark_tree_check_report_t report;
    amark_tree_def_group_t *group;

    NAOS_REQUIRE_TRUE(document != NULL);
    first = new_definition(document, root, "a", "/1", "");
    second = new_definition(document, root, "b", "/2", "");
    NAOS_REQUIRE_TRUE((first != NULL) && (second != NULL));
    NAOS_REQUIRE_PTR_EQ(first, find(document, "a"));
    NAOS_REQUIRE_PTR_EQ(second, find(document, "b"));
    check_tree_ok(document);

    /* A label that changed without the dirty mark: the key is stale. */
    group = first->def_group;
    NAOS_REQUIRE_TRUE(group != NULL);
    group->key[0] = 'z';
    NAOS_CHECK_STR_EQ("index", amark_tree_violation_name(amark_tree_check(document, &report)));
    NAOS_CHECK_PTR_EQ(first, report.node);
    group->key[0] = 'a';
    check_tree_ok(document);

    /* A member that points to another group. */
    first->def_group = second->def_group;
    NAOS_CHECK_STR_EQ("index", amark_tree_violation_name(amark_tree_check(document, &report)));
    first->def_group = group;
    /* A dirty mark off the dirty list. */
    first->def_dirty = true;
    NAOS_CHECK_STR_EQ("index", amark_tree_violation_name(amark_tree_check(document, &report)));
    first->def_dirty = false;
    /* A cached result that is not a member. */
    group->cached = second;
    group->cached_valid = true;
    NAOS_CHECK_STR_EQ("index", amark_tree_violation_name(amark_tree_check(document, &report)));
    group->cached = first;
    check_tree_ok(document);
    amark_document_destroy(document);
}

/* ---- Out of memory ------------------------------------------------------------------- */

/* Looks label up; when the lookup runs out of memory, checks that it wrote NULL
 * and left the document consistent, and that the same lookup succeeds again
 * (the sweep fails one allocation only). Returns the error of the first
 * lookup, or AMARK_ERROR_INVALID_STATE for a wrong result. */
static amark_error_t find_and_retry(amark_document_t *document, const char *label,
                                    const amark_node_t *expected)
{
    amark_node_t *found = (amark_node_t *)&found;
    amark_error_t error = amark_document_find_definition(document, label, strlen(label), &found);

    if (error == AMARK_ERROR_OUT_OF_MEMORY)
    {
        if ((found != NULL) || (amark_tree_check(document, NULL) != AMARK_TREE_VIOLATION_NONE) ||
            (amark_document_find_definition(document, label, strlen(label), &found) !=
             AMARK_ERROR_NONE))
        {
            return AMARK_ERROR_INVALID_STATE;
        }
    }
    else if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    if ((found != expected) || (amark_tree_check(document, NULL) != AMARK_TREE_VIOLATION_NONE))
    {
        return AMARK_ERROR_INVALID_STATE;
    }
    return error;
}

/* The same for the destination of a reference link, expected to read as the
 * PRESENT string expected. */
static amark_error_t get_and_retry(const amark_node_t *link, const char *expected)
{
    amark_value_t value;
    amark_error_t error;

    memset(&value, 0, sizeof(value));
    value.struct_size = sizeof(value);
    error = amark_node_get(link, AMARK_LINK_DESTINATION, &value);
    if (error == AMARK_ERROR_OUT_OF_MEMORY)
    {
        if (amark_node_get(link, AMARK_LINK_DESTINATION, &value) != AMARK_ERROR_NONE)
        {
            return AMARK_ERROR_INVALID_STATE;
        }
    }
    else if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    if ((value.state != AMARK_VALUE_PRESENT) || (strcmp(value.text, expected) != 0))
    {
        return AMARK_ERROR_INVALID_STATE;
    }
    return error;
}

/* Builds 40 definitions (half with labels derived from tokens, so that the
 * lookup derives them; the others with 20 distinct labels, so that the table
 * grows), looks them up and reads a reference link. Each failure returns
 * OUT_OF_MEMORY after checking that the operation can be done again. */
static amark_error_t index_oom_scenario(void *context)
{
    static const char text[] = "[Foo]: /url";
    amark_document_t *document = NULL;
    amark_node_t *root;
    amark_node_t *definitions[40];
    amark_node_t *link = NULL;
    amark_error_t error;
    amark_error_t failure = AMARK_ERROR_NONE;
    size_t i;

    (void)context;
    error = amark_document_create(NULL, &document);
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    root = amark_document_root(document);
    for (i = 0; (i < 40) && (error == AMARK_ERROR_NONE); i++)
    {
        char label[16];
        int length;

        error = amark_node_create(document, AMARK_NODE_LINK_DEFINITION, &definitions[i]);
        if (error == AMARK_ERROR_NONE)
        {
            error = amark_node_append_child(root, definitions[i]);
        }
        if ((error == AMARK_ERROR_NONE) && (i % 2 == 0))
        {
            error = amark_block_set_free_content(definitions[i], text, sizeof(text) - 1);
            if (error == AMARK_ERROR_NONE)
            {
                error = add_label_token(definitions[i], 1, 4);
            }
        }
        else if (error == AMARK_ERROR_NONE)
        {
            length = snprintf(label, sizeof(label), "Label %u", (unsigned)i);
            error = amark_node_record_string(definitions[i], AMARK_LINK_DEFINITION_LABEL, label,
                                             (size_t)length);
        }
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_create(document, AMARK_NODE_LINK, &link);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_record_int(link, AMARK_LINK_FORM, AMARK_LINK_FORM_SHORTCUT);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_record_string(link, AMARK_LINK_LABEL, "FOO", 3);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_record_string(definitions[2], AMARK_LINK_DEFINITION_DESTINATION,
                                         "/two", 4);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = find_and_retry(document, "label 7", definitions[7]);
        if (error == AMARK_ERROR_OUT_OF_MEMORY)
        {
            failure = error;
            error = AMARK_ERROR_NONE;
        }
    }
    if (error == AMARK_ERROR_NONE)
    {
        /* Another structure: the order of the 20 definitions of "foo" is decided
         * again; the first is now definitions[2]. */
        error = amark_node_append_child(root, definitions[0]);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = get_and_retry(link, "/two");
        if (error == AMARK_ERROR_OUT_OF_MEMORY)
        {
            failure = error;
            error = AMARK_ERROR_NONE;
        }
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = find_and_retry(document, "foo", definitions[2]);
    }
    if ((error == AMARK_ERROR_NONE) && (failure != AMARK_ERROR_NONE))
    {
        error = failure;
    }
    amark_document_destroy(document);
    return error;
}

NAOS_TEST(out_of_memory_in_the_index_leaves_the_document_consistent)
{
    amark_test_oom_result_t result =
        amark_test_oom_sweep(amark_allocator_set, index_oom_scenario, NULL,
                             AMARK_TEST_OOM_MAX_ITERATIONS);

    NAOS_CHECK_TRUE(amark_test_oom_check(result, "index_oom_scenario", __FILE__, __LINE__));
}

NAOS_SUITE(label_normalize,
    NAOS_ADD_TEST(ascii_labels_are_folded_and_their_spaces_collapsed),
    NAOS_ADD_TEST(full_width_space_and_no_break_space_are_ordinary_characters),
    NAOS_ADD_TEST(line_breaks_are_whitespace),
    NAOS_ADD_TEST(characters_outside_the_whitespace_definition_are_kept),
    NAOS_ADD_TEST(a_label_of_whitespace_only_normalizes_to_empty),
    NAOS_ADD_TEST(sharp_s_and_capital_sharp_s_fold_to_ss),
    NAOS_ADD_TEST(folds_to_several_code_points_are_written_in_full),
    NAOS_ADD_TEST(final_sigma_and_sigma_fold_alike),
    NAOS_ADD_TEST(invalid_utf8_becomes_replacement_and_nul_is_kept),
    NAOS_ADD_TEST(the_result_is_appended_to_the_buffer),
    NAOS_ADD_TEST(invalid_arguments_leave_the_buffer_unchanged),
    NAOS_ADD_TEST(out_of_memory_leaves_the_buffer_unchanged)
)

NAOS_SUITE(definition_index,
    NAOS_ADD_TEST(the_first_definition_in_document_order_is_returned),
    NAOS_ADD_TEST(definitions_deep_in_the_tree_are_ordered_by_their_ancestors),
    NAOS_ADD_TEST(detached_definitions_are_not_candidates),
    NAOS_ADD_TEST(a_destroyed_dirty_definition_leaves_the_dirty_list),
    NAOS_ADD_TEST(a_label_change_is_seen_by_the_next_lookup),
    NAOS_ADD_TEST(label_tokens_and_dropped_spelling_are_seen_by_the_next_lookup),
    NAOS_ADD_TEST(definitions_without_a_label_are_not_indexed),
    NAOS_ADD_TEST(find_definition_checks_its_arguments),
    NAOS_ADD_TEST(reference_links_and_images_resolve_through_the_index),
    NAOS_ADD_TEST(other_forms_read_their_own_values),
    NAOS_ADD_TEST(a_reference_without_a_definition_is_unresolved),
    NAOS_ADD_TEST(setting_the_label_of_a_reference_resolves_the_new_label),
    NAOS_ADD_TEST(the_result_is_kept_while_the_structure_is_unchanged),
    NAOS_ADD_TEST(a_lookup_of_one_definition_is_proportional_to_the_depth),
    NAOS_ADD_TEST(the_check_finds_a_broken_index),
    NAOS_ADD_TEST(out_of_memory_in_the_index_leaves_the_document_consistent)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(label_normalize),
    NAOS_ADD_SUITE(definition_index)
)
