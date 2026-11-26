/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The tests of the flow of a parser building a tree and of the msgstr insertion
 * (syntax-tree, System Flows).
 *
 * The inline assembly flow (task 10, 3.3, 8.3, 8.5, 9.4): each test takes an
 * example of the CommonMark spec 0.31.2 and does, with the public API only (and
 * the tree builder of the tests for a container), what a parser does: it records
 * the content of the paragraphs (and moves the lines of a link reference
 * definition out of a paragraph), scans the inlines into text nodes (text runs
 * as TEXT tokens, delimiter runs as one DELIMITER token per character), matches
 * the delimiter runs by moving their tokens to emph and strong nodes, records
 * the brackets, the destinations, the titles and the labels of the links, and
 * the soft and hard line breaks. The expected trees and values are written by
 * hand from the HTML of each example. Every example checks that no record is
 * refused, that the tokens of the paragraph and of its inlines are on its
 * current content and cover each of its bytes exactly once, and that the values
 * derived from the tokens and the resolved destinations and titles are those of
 * the HTML. Each assembled tree also passes the lossless verification (10.1-10.3)
 * and every node of it has its whole spelling and is verbatim (11.1, 11.3, 11.4).
 *
 * The examples of INPUT.md (3.1, 3.3, 3.4), built with the tree builder: the
 * lossless verification, the judgments, the values, the positions and the dump.
 *
 * The msgstr insertion (INPUT.md 3.2, 8.6, 11.4): the msgid from the content of
 * a paragraph in a list in a block quote, the msgstr as a leaf with free content
 * in another document, and amark_block_replace_content: the paragraph keeps its
 * line range, the margins of its ancestors and the styles; its inlines keep the
 * bytes of the translator; it and its ancestors are no longer verbatim, the
 * other paragraph still is. Moving a paragraph from a block quote into an item
 * (11.4, 11.5): the ancestors at the destination have no margin on its line and
 * are not verbatim, the moved paragraph is.
 */
#include "tree/tree.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <naos/naos.h>

#include "test_check.h"
#include "tree_builder.h"

/* ---- Documents and blocks ---------------------------------------------------- */

/* A document of the built-in syntax set with one finished source holding
 * markdown, the document node covering all of its lines. */
static amark_document_t *new_document(const char *markdown)
{
    amark_document_t *document = NULL;
    amark_source_id_t source = 0;
    size_t lines;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_add_source(document, markdown, strlen(markdown),
                                                  AMARK_SOURCE_FINISHED, &source));
    lines = amark_document_line_count(document);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_block_set_lines(amark_document_root(document), 0,
                                              (uint32_t)lines));
    return document;
}

/* A new node of type appended to parent. */
static amark_node_t *add_node(amark_node_t *parent, amark_node_type_t type)
{
    amark_node_t *node = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(amark_node_document(parent), type, &node));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(parent, node));
    return node;
}

/* A new block of type appended to parent, on the count lines from first. */
static amark_node_t *add_block(amark_node_t *parent, amark_node_type_t type,
                               amark_line_id_t first, uint32_t count)
{
    amark_node_t *block = add_node(parent, type);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(block, first, count));
    return block;
}

static void add_content(amark_node_t *leaf, amark_line_id_t line, uint32_t start, uint32_t end)
{
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(leaf, line, start, end));
}

/* An empty line of the document, which belongs to block. */
static void add_blank_line(amark_node_t *block, amark_line_id_t line)
{
    amark_blank_line_t blank;

    memset(&blank, 0, sizeof(blank));
    blank.struct_size = sizeof(blank);
    blank.line = line;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_blank_line(block, &blank));
}

/* The block stage of a link reference definition: the line of paragraph moves
 * to a new link_definition placed before it (7.10). */
static amark_node_t *take_definition(amark_node_t *paragraph, amark_line_id_t line)
{
    amark_node_t *definition = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(amark_node_document(paragraph),
                                          AMARK_NODE_LINK_DEFINITION, &definition));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_before(paragraph, definition));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_move_lines(paragraph, definition, line, 1));
    return definition;
}

/* Destroys paragraph once every line of it went to link reference
 * definitions. */
static void drop_empty_paragraph(amark_node_t *paragraph)
{
    uint32_t count = 1;

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_lines(paragraph, NULL, &count));
    NAOS_CHECK_UINT_EQ(0, count);
    NAOS_CHECK_INT_EQ(AMARK_CONTENT_NONE, amark_block_content_state(paragraph));
    amark_node_destroy(paragraph);
}

/* ---- Inline scan ---------------------------------------------------------------- */

/* Records the token [start, end) of kind and facet on the content of leaf. */
static void token(amark_node_t *node, const amark_node_t *leaf, size_t start, size_t end,
                  amark_spell_kind_t kind, amark_facet_t facet)
{
    amark_content_token_t t;

    memset(&t, 0, sizeof(t));
    t.struct_size = sizeof(t);
    t.start = start;
    t.end = end;
    t.kind = kind;
    t.facet = facet;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_add_content_token(node, leaf, &t));
}

/* A text node appended to parent, spelled by the text [start, end) of leaf. */
static amark_node_t *scan_text(amark_node_t *parent, const amark_node_t *leaf, size_t start,
                               size_t end)
{
    amark_node_t *text = add_node(parent, AMARK_NODE_TEXT);

    token(text, leaf, start, end, AMARK_SPELL_TEXT, AMARK_TEXT_LITERAL);
    return text;
}

/* A delimiter run of count characters from start ("*", "[", ...), scanned as a
 * text node appended to parent with one DELIMITER token per character (the run
 * may be used in part: 6.2). */
static amark_node_t *scan_run(amark_node_t *parent, const amark_node_t *leaf, size_t start,
                              size_t count)
{
    amark_node_t *run = add_node(parent, AMARK_NODE_TEXT);
    size_t i;

    for (i = 0; i < count; i++)
    {
        token(run, leaf, start + i, start + i + 1, AMARK_SPELL_DELIMITER, AMARK_FACET_NONE);
    }
    return run;
}

/* A softbreak or linebreak appended to parent, spelled by the spaces
 * [space_start, break_start) (none when equal) and the line break at
 * break_start. */
static amark_node_t *scan_break(amark_node_t *parent, const amark_node_t *leaf,
                                amark_node_type_t type, size_t space_start, size_t break_start)
{
    amark_node_t *node = add_node(parent, type);

    if (space_start < break_start)
    {
        token(node, leaf, space_start, break_start, AMARK_SPELL_SPACE, AMARK_FACET_NONE);
    }
    token(node, leaf, break_start, break_start + 1, AMARK_SPELL_LINE_BREAK, AMARK_FACET_NONE);
    return node;
}

/* A text node of a run of delimiters left after the delimiter processing: its
 * literal is the bytes of its DELIMITER tokens, recorded (structural tokens do
 * not derive a value). */
static void finish_run(amark_node_t *run)
{
    const char *bytes = NULL;
    char literal[16];
    size_t count = amark_node_content_token_count(run);
    size_t i;

    NAOS_REQUIRE_TRUE(count < sizeof(literal));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_content_base(run, &bytes, NULL, NULL, NULL));
    for (i = 0; i < count; i++)
    {
        amark_content_token_t t;

        memset(&t, 0, sizeof(t));
        t.struct_size = sizeof(t);
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_content_token_at(run, i, &t));
        literal[i] = bytes[t.start];
    }
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_record_string(run, AMARK_TEXT_LITERAL, literal, count));
}

/*
 * The delimiter processing of one match of `*` runs: the last use tokens of
 * opener and the first use tokens of closer go to a new emph (use 1) or strong
 * (use 2) node placed after opener, which takes the nodes between the runs as
 * its children. A run left without tokens is destroyed.
 */
static amark_node_t *match_emphasis(amark_node_t *opener, amark_node_t *closer, size_t use)
{
    amark_node_t *emphasis = NULL;
    amark_node_t *child;
    size_t opener_count = amark_node_content_token_count(opener);

    NAOS_REQUIRE_TRUE((use == 1) || (use == 2));
    NAOS_REQUIRE_TRUE(opener_count >= use);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(amark_node_document(opener),
                                          use == 1 ? AMARK_NODE_EMPH : AMARK_NODE_STRONG,
                                          &emphasis));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_after(opener, emphasis));
    while ((child = amark_node_next(emphasis)) != closer)
    {
        NAOS_REQUIRE_NOT_NULL(child);
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(emphasis, child));
    }
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_move_content_tokens(opener, emphasis, opener_count - use, use));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_move_content_tokens(closer, emphasis, 0, use));
    /* AMARK_EMPH_DELIMITER and AMARK_STRONG_DELIMITER are the same facet. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_record_int(emphasis, AMARK_EMPH_DELIMITER,
                                            AMARK_EMPH_DELIMITER_ASTERISK));
    if (amark_node_content_token_count(opener) == 0)
    {
        amark_node_destroy(opener);
    }
    if (amark_node_content_token_count(closer) == 0)
    {
        amark_node_destroy(closer);
    }
    return emphasis;
}

/*
 * The `]` of the bracket run opener closes a link: a new link takes the token of
 * the `[` and the nodes after the bracket as its link text; the bracket node is
 * destroyed. The caller records the rest of the link (`]`, the destination, the
 * label) on it.
 */
static amark_node_t *close_bracket(amark_node_t *opener)
{
    amark_node_t *link = NULL;
    amark_node_t *child;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(amark_node_document(opener), AMARK_NODE_LINK, &link));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_after(opener, link));
    while ((child = amark_node_next(link)) != NULL)
    {
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(link, child));
    }
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_move_content_tokens(opener, link, 0, 1));
    NAOS_CHECK_UINT_EQ(0, amark_node_content_token_count(opener));
    amark_node_destroy(opener);
    return link;
}

static void record_int(amark_node_t *node, amark_facet_t facet, int64_t number)
{
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_record_int(node, facet, number));
}

/* ---- Checks ---------------------------------------------------------------------- */

#define CHECK_TREE_OK(document) \
    NAOS_CHECK_STR_EQ("none", amark_tree_violation_name(amark_tree_check((document), NULL)))

/* Checks that facet of node reads expected_literal, a string literal. */
#define CHECK_TEXT(node, facet, expected_literal) \
    do { \
        amark_value_t v_; \
        memset(&v_, 0, sizeof(v_)); \
        v_.struct_size = sizeof(v_); \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_get((node), (facet), &v_)); \
        NAOS_CHECK_INT_EQ(AMARK_VALUE_PRESENT, v_.state); \
        AMARK_CHECK_BYTES_EQ(v_.text, v_.length, expected_literal); \
    } while (0)

/* Checks that facet of node reads number. */
#define CHECK_NUMBER(node, facet, expected) \
    do { \
        amark_value_t v_; \
        memset(&v_, 0, sizeof(v_)); \
        v_.struct_size = sizeof(v_); \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_get((node), (facet), &v_)); \
        NAOS_CHECK_INT_EQ(AMARK_VALUE_PRESENT, v_.state); \
        NAOS_CHECK_INT_EQ((expected), v_.number); \
    } while (0)

/* Checks the content bytes of leaf against expected_literal. */
#define CHECK_CONTENT(leaf, expected_literal) \
    do { \
        const char *b_ = NULL; \
        size_t l_ = 0; \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_content_bytes((leaf), &b_, &l_)); \
        AMARK_CHECK_BYTES_EQ(b_, l_, expected_literal); \
    } while (0)

/* The child index of node (NULL when it has fewer children). */
static amark_node_t *child_at(const amark_node_t *node, size_t index)
{
    amark_node_t *child = amark_node_first_child(node);

    while ((child != NULL) && (index > 0))
    {
        child = amark_node_next(child);
        index--;
    }
    return child;
}

static void append_text(char *out, size_t size, size_t *used, const char *text)
{
    size_t length = strlen(text);

    if (*used + length < size)
    {
        memcpy(out + *used, text, length + 1);
        *used += length;
    }
}

/* The shape of the subtree of top: "(type (child ...) ...)" with the names of
 * the node types. */
static const char *shape(const amark_node_t *top, char *out, size_t size)
{
    const amark_node_t *node = top;
    size_t used = 0;

    out[0] = '\0';
    while (node != NULL)
    {
        const amark_node_t *child;

        append_text(out, size, &used, used == 0 ? "(" : " (");
        append_text(out, size, &used, amark_node_type_desc(node)->name);
        child = amark_node_first_child(node);
        if (child != NULL)
        {
            node = child;
            continue;
        }
        for (;;)
        {
            append_text(out, size, &used, ")");
            if (node == top)
            {
                return out;
            }
            if (amark_node_next(node) != NULL)
            {
                node = amark_node_next(node);
                break;
            }
            node = amark_node_parent(node);
        }
    }
    return out;
}

#define CHECK_SHAPE(top, expected) \
    do { \
        char shape_[512]; \
        NAOS_CHECK_STR_EQ((expected), shape((top), shape_, sizeof(shape_))); \
    } while (0)

/*
 * Checks the cover of the content of leaf: the content tokens of leaf and of
 * every inline descendant of it are on its current content (8.8), and each byte
 * of the content is in exactly one token (8.3).
 */
static void check_cover(const amark_node_t *leaf)
{
    unsigned char seen[256];
    const char *bytes = NULL;
    size_t length = 0;
    const amark_node_t *node = leaf;
    size_t first_bad;
    size_t i;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_content_bytes(leaf, &bytes, &length));
    NAOS_REQUIRE_TRUE(length > 0);
    NAOS_REQUIRE_TRUE(length <= sizeof(seen));
    memset(seen, 0, sizeof(seen));
    while (node != NULL)
    {
        size_t count = amark_node_content_token_count(node);

        if (count > 0)
        {
            const char *base = NULL;
            size_t base_length = 0;
            amark_content_state_t state = AMARK_CONTENT_NONE;
            bool own = false;

            NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                              amark_node_content_base(node, &base, &base_length, &state, &own));
            NAOS_CHECK_TRUE(own);
            NAOS_CHECK_INT_EQ(amark_block_content_state(leaf), state);
            NAOS_CHECK_UINT_EQ(length, base_length);
        }
        for (i = 0; i < count; i++)
        {
            amark_content_token_t t;
            size_t k;

            memset(&t, 0, sizeof(t));
            t.struct_size = sizeof(t);
            NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_content_token_at(node, i, &t));
            NAOS_REQUIRE_TRUE((t.start < t.end) && (t.end <= length));
            for (k = t.start; k < t.end; k++)
            {
                seen[k]++;
            }
        }
        if (amark_node_first_child(node) != NULL)
        {
            node = amark_node_first_child(node);
            continue;
        }
        while ((node != leaf) && (amark_node_next(node) == NULL))
        {
            node = amark_node_parent(node);
        }
        node = (node == leaf) ? NULL : amark_node_next(node);
    }
    /* The first byte not covered exactly once (length when there is none). */
    first_bad = length;
    for (i = 0; i < length; i++)
    {
        if (seen[i] != 1)
        {
            first_bad = i;
            break;
        }
    }
    NAOS_CHECK_UINT_EQ(length, first_bad);
}

/* Checks that the reference link (or image) resolves through the definition. */
static void check_resolves_to(amark_document_t *document, const amark_node_t *link,
                              const amark_node_t *definition)
{
    amark_value_t label;
    amark_node_t *found = NULL;

    memset(&label, 0, sizeof(label));
    label.struct_size = sizeof(label);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_get(link, AMARK_LINK_LABEL, &label));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_document_find_definition(document, label.text, label.length, &found));
    NAOS_CHECK_PTR_EQ(definition, found);
}

/* The lossless verification of the source of document (it has one: 0),
 * reporting into *report. */
static amark_error_t check_lossless(const amark_document_t *document,
                                    amark_lossless_report_t *report)
{
    memset(report, 0, sizeof(*report));
    report->struct_size = sizeof(*report);
    return amark_document_check_lossless(document, 0, report);
}

/* Whether the subtree of node can be output in its original spelling (false
 * when the query fails, which the check reports). */
static bool verbatim(const amark_node_t *node)
{
    bool out = false;

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_verbatim(node, &out));
    return out;
}

/* Whether the content of leaf can be output in its original lines. */
static bool content_verbatim(const amark_node_t *leaf)
{
    bool out = false;

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_content_verbatim(leaf, &out));
    return out;
}

/* The next node of the subtree of top in document order (NULL after the last). */
static const amark_node_t *next_in(const amark_node_t *top, const amark_node_t *node)
{
    if (amark_node_first_child(node) != NULL)
    {
        return amark_node_first_child(node);
    }
    while ((node != top) && (amark_node_next(node) == NULL))
    {
        node = amark_node_parent(node);
    }
    return (node == top) ? NULL : amark_node_next(node);
}

/*
 * Checks a tree just assembled as a parser does (the tree of an unedited
 * document): the internal checker finds nothing, the lossless verification
 * (10.1-10.3) finds every column of every line and every byte of every content
 * owned exactly once, and every node of the tree has its whole spelling and can
 * be output in its original spelling (11.1, 11.3, 11.4).
 */
static void check_assembled(amark_document_t *document)
{
    const amark_node_t *root = amark_document_root(document);
    const amark_node_t *node;
    amark_lossless_report_t report;
    amark_error_t error = check_lossless(document, &report);

    NAOS_CHECK_STR_EQ("none", amark_tree_violation_name(amark_tree_check(document, NULL)));
    NAOS_CHECK_STR_EQ(amark_error_string(AMARK_ERROR_NONE), amark_error_string(error));
    if (error == AMARK_ERROR_NOT_LOSSLESS)
    {
        /* Where, to tell the failure. */
        NAOS_CHECK_INT_EQ(0, (int)report.problem);
        NAOS_CHECK_UINT_EQ(AMARK_LINE_NONE, report.line);
        NAOS_CHECK_UINT_EQ(0, report.column);
        NAOS_CHECK_UINT_EQ(0, report.content_offset);
    }
    for (node = root; node != NULL; node = next_in(root, node))
    {
        amark_spelling_state_t state;

        memset(&state, 0, sizeof(state));
        state.struct_size = sizeof(state);
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_spelling_state(node, &state));
        NAOS_CHECK_TRUE(state.complete);
        NAOS_CHECK_FALSE(amark_node_is_new(node));
        NAOS_CHECK_TRUE(verbatim(node));
        if (amark_node_has_content(node))
        {
            NAOS_CHECK_TRUE(content_verbatim(node));
        }
    }
}

#define CHECK_ASSEMBLED(document) check_assembled(document)

/* ---- Emphasis ---------------------------------------------------------------------- */

/* Example 418: "*foo **bar *baz* bim** bop*"
 * <p><em>foo <strong>bar <em>baz</em> bim</strong> bop</em></p> */
NAOS_TEST(example_418_nested_emphasis)
{
    amark_document_t *document = new_document("*foo **bar *baz* bim** bop*\n");
    amark_node_t *paragraph = add_block(amark_document_root(document), AMARK_NODE_PARAGRAPH, 0, 1);
    amark_node_t *runs[6];
    amark_node_t *text[5];
    amark_node_t *inner;
    amark_node_t *strong;
    amark_node_t *outer;

    add_content(paragraph, 0, 0, 27);
    CHECK_CONTENT(paragraph, "*foo **bar *baz* bim** bop*");
    runs[0] = scan_run(paragraph, paragraph, 0, 1);
    text[0] = scan_text(paragraph, paragraph, 1, 5);
    runs[1] = scan_run(paragraph, paragraph, 5, 2);
    text[1] = scan_text(paragraph, paragraph, 7, 11);
    runs[2] = scan_run(paragraph, paragraph, 11, 1);
    text[2] = scan_text(paragraph, paragraph, 12, 15);
    runs[3] = scan_run(paragraph, paragraph, 15, 1);
    text[3] = scan_text(paragraph, paragraph, 16, 20);
    runs[4] = scan_run(paragraph, paragraph, 20, 2);
    text[4] = scan_text(paragraph, paragraph, 22, 26);
    runs[5] = scan_run(paragraph, paragraph, 26, 1);
    check_cover(paragraph);

    /* The closers from the left: "*" at 15, "**" at 20, "*" at 26. */
    inner = match_emphasis(runs[2], runs[3], 1);
    strong = match_emphasis(runs[1], runs[4], 2);
    outer = match_emphasis(runs[0], runs[5], 1);

    CHECK_SHAPE(paragraph,
                "(paragraph (emph (text) (strong (text) (emph (text)) (text)) (text)))");
    NAOS_CHECK_PTR_EQ(outer, child_at(paragraph, 0));
    NAOS_CHECK_PTR_EQ(strong, child_at(outer, 1));
    NAOS_CHECK_PTR_EQ(inner, child_at(strong, 1));
    CHECK_TEXT(text[0], AMARK_TEXT_LITERAL, "foo ");
    CHECK_TEXT(text[1], AMARK_TEXT_LITERAL, "bar ");
    CHECK_TEXT(text[2], AMARK_TEXT_LITERAL, "baz");
    CHECK_TEXT(text[3], AMARK_TEXT_LITERAL, " bim");
    CHECK_TEXT(text[4], AMARK_TEXT_LITERAL, " bop");
    CHECK_NUMBER(outer, AMARK_EMPH_DELIMITER, AMARK_EMPH_DELIMITER_ASTERISK);
    CHECK_NUMBER(strong, AMARK_STRONG_DELIMITER, AMARK_STRONG_DELIMITER_ASTERISK);
    NAOS_CHECK_UINT_EQ(2, amark_node_content_token_count(outer));
    NAOS_CHECK_UINT_EQ(4, amark_node_content_token_count(strong));
    NAOS_CHECK_UINT_EQ(2, amark_node_content_token_count(inner));
    check_cover(paragraph);
    CHECK_ASSEMBLED(document);
    amark_document_destroy(document);
}

/* Example 413: "***foo** bar*" -- the run "***" is used in part by each match.
 * <p><em><strong>foo</strong> bar</em></p> */
NAOS_TEST(example_413_a_delimiter_run_shared_by_two_matches)
{
    amark_document_t *document = new_document("***foo** bar*\n");
    amark_node_t *paragraph = add_block(amark_document_root(document), AMARK_NODE_PARAGRAPH, 0, 1);
    amark_node_t *opener;
    amark_node_t *foo;
    amark_node_t *closer;
    amark_node_t *bar;
    amark_node_t *last;
    amark_node_t *strong;
    amark_node_t *emph;
    amark_content_token_t t;

    add_content(paragraph, 0, 0, 13);
    opener = scan_run(paragraph, paragraph, 0, 3);
    foo = scan_text(paragraph, paragraph, 3, 6);
    closer = scan_run(paragraph, paragraph, 6, 2);
    bar = scan_text(paragraph, paragraph, 8, 12);
    last = scan_run(paragraph, paragraph, 12, 1);
    check_cover(paragraph);

    /* "**" closes with the last two of "***", then "*" with the first. */
    strong = match_emphasis(opener, closer, 2);
    NAOS_CHECK_UINT_EQ(1, amark_node_content_token_count(opener));
    emph = match_emphasis(opener, last, 1);

    CHECK_SHAPE(paragraph, "(paragraph (emph (strong (text)) (text)))");
    NAOS_CHECK_PTR_EQ(emph, child_at(paragraph, 0));
    NAOS_CHECK_PTR_EQ(strong, child_at(emph, 0));
    memset(&t, 0, sizeof(t));
    t.struct_size = sizeof(t);
    NAOS_CHECK_UINT_EQ(2, amark_node_content_token_count(emph));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_content_token_at(emph, 0, &t));
    NAOS_CHECK_UINT_EQ(0, t.start);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_content_token_at(emph, 1, &t));
    NAOS_CHECK_UINT_EQ(12, t.start);
    NAOS_CHECK_UINT_EQ(4, amark_node_content_token_count(strong));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_content_token_at(strong, 0, &t));
    NAOS_CHECK_UINT_EQ(1, t.start);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_content_token_at(strong, 3, &t));
    NAOS_CHECK_UINT_EQ(7, t.start);
    CHECK_TEXT(foo, AMARK_TEXT_LITERAL, "foo");
    CHECK_TEXT(bar, AMARK_TEXT_LITERAL, " bar");
    check_cover(paragraph);
    CHECK_ASSEMBLED(document);
    amark_document_destroy(document);
}

/* Example 444: "***foo**" -- the first "*" of the opener is left as text.
 * <p>*<strong>foo</strong></p> */
NAOS_TEST(example_444_an_opener_used_in_part_leaves_a_text)
{
    amark_document_t *document = new_document("***foo**\n");
    amark_node_t *paragraph = add_block(amark_document_root(document), AMARK_NODE_PARAGRAPH, 0, 1);
    amark_node_t *opener;
    amark_node_t *foo;
    amark_node_t *closer;
    amark_node_t *strong;

    add_content(paragraph, 0, 0, 8);
    opener = scan_run(paragraph, paragraph, 0, 3);
    foo = scan_text(paragraph, paragraph, 3, 6);
    closer = scan_run(paragraph, paragraph, 6, 2);
    strong = match_emphasis(opener, closer, 2);
    finish_run(opener);

    CHECK_SHAPE(paragraph, "(paragraph (text) (strong (text)))");
    NAOS_CHECK_PTR_EQ(opener, child_at(paragraph, 0));
    NAOS_CHECK_PTR_EQ(strong, child_at(paragraph, 1));
    CHECK_TEXT(opener, AMARK_TEXT_LITERAL, "*");
    CHECK_TEXT(foo, AMARK_TEXT_LITERAL, "foo");
    check_cover(paragraph);
    CHECK_ASSEMBLED(document);
    amark_document_destroy(document);
}

/* Example 443: "*foo**" -- the last "*" of the closer is left as text.
 * <p><em>foo</em>*</p> */
NAOS_TEST(example_443_a_closer_used_in_part_leaves_a_text)
{
    amark_document_t *document = new_document("*foo**\n");
    amark_node_t *paragraph = add_block(amark_document_root(document), AMARK_NODE_PARAGRAPH, 0, 1);
    amark_node_t *opener;
    amark_node_t *foo;
    amark_node_t *closer;
    amark_node_t *emph;

    add_content(paragraph, 0, 0, 6);
    opener = scan_run(paragraph, paragraph, 0, 1);
    foo = scan_text(paragraph, paragraph, 1, 4);
    closer = scan_run(paragraph, paragraph, 4, 2);
    emph = match_emphasis(opener, closer, 1);
    finish_run(closer);

    CHECK_SHAPE(paragraph, "(paragraph (emph (text)) (text))");
    NAOS_CHECK_PTR_EQ(emph, child_at(paragraph, 0));
    NAOS_CHECK_PTR_EQ(closer, child_at(paragraph, 1));
    CHECK_TEXT(foo, AMARK_TEXT_LITERAL, "foo");
    CHECK_TEXT(closer, AMARK_TEXT_LITERAL, "*");
    check_cover(paragraph);
    CHECK_ASSEMBLED(document);
    amark_document_destroy(document);
}

/* Example 405 in a block quote: "> *foo\n> bar*"
 * <blockquote><p><em>foo\nbar</em></p></blockquote> */
NAOS_TEST(example_405_emphasis_over_two_lines_of_a_block_quote)
{
    amark_document_t *document = new_document("> *foo\n> bar*\n");
    amark_node_t *quote;
    amark_node_t *paragraph;
    amark_node_t *opener;
    amark_node_t *foo;
    amark_node_t *softbreak;
    amark_node_t *bar;
    amark_node_t *closer;
    amark_node_t *emph;

    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(block_quote :lines (0 2)\n"
                           "  :margins ((0 0 2 present) (1 0 2 present))\n"
                           "  (paragraph :lines (0 2)))");
    quote = amark_node_first_child(amark_document_root(document));
    NAOS_REQUIRE_NOT_NULL(quote);
    paragraph = amark_node_first_child(quote);
    NAOS_REQUIRE_NOT_NULL(paragraph);
    add_content(paragraph, 0, 2, 6);
    add_content(paragraph, 1, 2, 6);
    CHECK_CONTENT(paragraph, "*foo\nbar*");
    opener = scan_run(paragraph, paragraph, 0, 1);
    foo = scan_text(paragraph, paragraph, 1, 4);
    softbreak = scan_break(paragraph, paragraph, AMARK_NODE_SOFTBREAK, 4, 4);
    bar = scan_text(paragraph, paragraph, 5, 8);
    closer = scan_run(paragraph, paragraph, 8, 1);
    emph = match_emphasis(opener, closer, 1);

    CHECK_SHAPE(quote, "(block_quote (paragraph (emph (text) (softbreak) (text))))");
    NAOS_CHECK_PTR_EQ(emph, child_at(paragraph, 0));
    NAOS_CHECK_PTR_EQ(softbreak, child_at(emph, 1));
    CHECK_TEXT(foo, AMARK_TEXT_LITERAL, "foo");
    CHECK_TEXT(bar, AMARK_TEXT_LITERAL, "bar");
    check_cover(paragraph);
    CHECK_ASSEMBLED(document);
    amark_document_destroy(document);
}

/* Example 405 in a block quote whose second line is a lazy continuation (the
 * laziness of examples 232 and 233): "> *foo\nbar*"
 * <blockquote><p><em>foo\nbar</em></p></blockquote> */
NAOS_TEST(example_405_emphasis_over_a_lazy_line_of_a_block_quote)
{
    amark_document_t *document = new_document("> *foo\nbar*\n");
    amark_node_t *quote;
    amark_node_t *paragraph;
    amark_node_t *opener;
    amark_node_t *foo;
    amark_node_t *bar;
    amark_node_t *closer;
    amark_margin_t margin;

    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(block_quote :lines (0 2)\n"
                           "  :margins ((0 0 2 present) (1 0 0 lazy))\n"
                           "  (paragraph :lines (0 2)))");
    quote = amark_node_first_child(amark_document_root(document));
    NAOS_REQUIRE_NOT_NULL(quote);
    paragraph = amark_node_first_child(quote);
    NAOS_REQUIRE_NOT_NULL(paragraph);
    memset(&margin, 0, sizeof(margin));
    margin.struct_size = sizeof(margin);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_margin_at(quote, 1, &margin));
    NAOS_CHECK_INT_EQ(AMARK_MARGIN_LAZY, margin.state);
    add_content(paragraph, 0, 2, 6);
    add_content(paragraph, 1, 0, 4);
    CHECK_CONTENT(paragraph, "*foo\nbar*");
    opener = scan_run(paragraph, paragraph, 0, 1);
    foo = scan_text(paragraph, paragraph, 1, 4);
    (void)scan_break(paragraph, paragraph, AMARK_NODE_SOFTBREAK, 4, 4);
    bar = scan_text(paragraph, paragraph, 5, 8);
    closer = scan_run(paragraph, paragraph, 8, 1);
    (void)match_emphasis(opener, closer, 1);

    CHECK_SHAPE(quote, "(block_quote (paragraph (emph (text) (softbreak) (text))))");
    CHECK_TEXT(foo, AMARK_TEXT_LITERAL, "foo");
    CHECK_TEXT(bar, AMARK_TEXT_LITERAL, "bar");
    check_cover(paragraph);
    CHECK_ASSEMBLED(document);
    amark_document_destroy(document);
}

/* ---- Code spans ------------------------------------------------------------------------ */

/* Example 329: "`` foo ` bar ``"
 * <p><code>foo ` bar</code></p> */
NAOS_TEST(example_329_a_code_span_with_its_padding)
{
    amark_document_t *document = new_document("`` foo ` bar ``\n");
    amark_node_t *paragraph = add_block(amark_document_root(document), AMARK_NODE_PARAGRAPH, 0, 1);
    amark_node_t *code;

    add_content(paragraph, 0, 0, 15);
    code = add_node(paragraph, AMARK_NODE_CODE);
    token(code, paragraph, 0, 2, AMARK_SPELL_DELIMITER, AMARK_FACET_NONE);
    /* The spaces stripped from both ends are structural. */
    token(code, paragraph, 2, 3, AMARK_SPELL_SPACE, AMARK_FACET_NONE);
    token(code, paragraph, 3, 12, AMARK_SPELL_TEXT, AMARK_CODE_LITERAL);
    token(code, paragraph, 12, 13, AMARK_SPELL_SPACE, AMARK_FACET_NONE);
    token(code, paragraph, 13, 15, AMARK_SPELL_DELIMITER, AMARK_FACET_NONE);
    record_int(code, AMARK_CODE_BACKTICKS, 2);
    record_int(code, AMARK_CODE_PADDED, 1);

    CHECK_SHAPE(paragraph, "(paragraph (code))");
    CHECK_TEXT(code, AMARK_CODE_LITERAL, "foo ` bar");
    CHECK_NUMBER(code, AMARK_CODE_BACKTICKS, 2);
    check_cover(paragraph);
    CHECK_ASSEMBLED(document);
    amark_document_destroy(document);
}

/* Example 337: "`foo   bar \nbaz`" -- the line break becomes a space.
 * <p><code>foo   bar  baz</code></p> */
NAOS_TEST(example_337_a_code_span_over_two_lines)
{
    amark_document_t *document = new_document("`foo   bar \nbaz`\n");
    amark_node_t *paragraph = add_block(amark_document_root(document), AMARK_NODE_PARAGRAPH, 0, 2);
    amark_node_t *code;

    add_content(paragraph, 0, 0, 11);
    add_content(paragraph, 1, 0, 4);
    CHECK_CONTENT(paragraph, "`foo   bar \nbaz`");
    code = add_node(paragraph, AMARK_NODE_CODE);
    token(code, paragraph, 0, 1, AMARK_SPELL_DELIMITER, AMARK_FACET_NONE);
    token(code, paragraph, 1, 11, AMARK_SPELL_TEXT, AMARK_CODE_LITERAL);
    token(code, paragraph, 11, 12, AMARK_SPELL_LINE_BREAK, AMARK_CODE_LITERAL);
    token(code, paragraph, 12, 15, AMARK_SPELL_TEXT, AMARK_CODE_LITERAL);
    token(code, paragraph, 15, 16, AMARK_SPELL_DELIMITER, AMARK_FACET_NONE);
    record_int(code, AMARK_CODE_BACKTICKS, 1);

    CHECK_SHAPE(paragraph, "(paragraph (code))");
    CHECK_TEXT(code, AMARK_CODE_LITERAL, "foo   bar  baz");
    check_cover(paragraph);
    CHECK_ASSEMBLED(document);
    amark_document_destroy(document);
}

/* ---- Escapes and character references ------------------------------------------------- */

/* Example 515: "[link \[bar](/uri)"
 * <p><a href="/uri">link [bar</a></p> */
NAOS_TEST(example_515_an_escape_in_a_link_text)
{
    amark_document_t *document = new_document("[link \\[bar](/uri)\n");
    amark_node_t *paragraph = add_block(amark_document_root(document), AMARK_NODE_PARAGRAPH, 0, 1);
    amark_node_t *bracket;
    amark_node_t *text;
    amark_node_t *link;

    add_content(paragraph, 0, 0, 18);
    bracket = scan_run(paragraph, paragraph, 0, 1);
    text = add_node(paragraph, AMARK_NODE_TEXT);
    token(text, paragraph, 1, 6, AMARK_SPELL_TEXT, AMARK_TEXT_LITERAL);
    token(text, paragraph, 6, 8, AMARK_SPELL_ESCAPE, AMARK_TEXT_LITERAL);
    token(text, paragraph, 8, 11, AMARK_SPELL_TEXT, AMARK_TEXT_LITERAL);
    link = close_bracket(bracket);
    token(link, paragraph, 11, 13, AMARK_SPELL_DELIMITER, AMARK_FACET_NONE);
    token(link, paragraph, 13, 17, AMARK_SPELL_TEXT, AMARK_LINK_DESTINATION);
    token(link, paragraph, 17, 18, AMARK_SPELL_DELIMITER, AMARK_FACET_NONE);
    record_int(link, AMARK_LINK_FORM, AMARK_LINK_FORM_INLINE);

    CHECK_SHAPE(paragraph, "(paragraph (link (text)))");
    CHECK_TEXT(text, AMARK_TEXT_LITERAL, "link [bar");
    CHECK_TEXT(link, AMARK_LINK_DESTINATION, "/uri");
    CHECK_TEXT(link, AMARK_LINK_TITLE, "");
    check_cover(paragraph);
    CHECK_ASSEMBLED(document);
    amark_document_destroy(document);
}

/* Example 26: "&#35; &#1234; &#992; &#0;"
 * <p># &#x4D2; &#x3E0; &#xFFFD;</p> (as characters) */
NAOS_TEST(example_26_numeric_character_references)
{
    amark_document_t *document = new_document("&#35; &#1234; &#992; &#0;\n");
    amark_node_t *paragraph = add_block(amark_document_root(document), AMARK_NODE_PARAGRAPH, 0, 1);
    amark_node_t *text;

    add_content(paragraph, 0, 0, 25);
    text = add_node(paragraph, AMARK_NODE_TEXT);
    token(text, paragraph, 0, 5, AMARK_SPELL_CHAR_REF, AMARK_TEXT_LITERAL);
    token(text, paragraph, 5, 6, AMARK_SPELL_TEXT, AMARK_TEXT_LITERAL);
    token(text, paragraph, 6, 13, AMARK_SPELL_CHAR_REF, AMARK_TEXT_LITERAL);
    token(text, paragraph, 13, 14, AMARK_SPELL_TEXT, AMARK_TEXT_LITERAL);
    token(text, paragraph, 14, 20, AMARK_SPELL_CHAR_REF, AMARK_TEXT_LITERAL);
    token(text, paragraph, 20, 21, AMARK_SPELL_TEXT, AMARK_TEXT_LITERAL);
    token(text, paragraph, 21, 25, AMARK_SPELL_CHAR_REF, AMARK_TEXT_LITERAL);

    CHECK_SHAPE(paragraph, "(paragraph (text))");
    CHECK_TEXT(text, AMARK_TEXT_LITERAL, "# \xD3\x92 \xCF\xA0 \xEF\xBF\xBD");
    check_cover(paragraph);
    CHECK_ASSEMBLED(document);
    amark_document_destroy(document);
}

/* Example 25: named references on three lines.
 * <p>&nbsp; &amp; &copy; &AElig; &Dcaron;\n&frac34; &HilbertSpace; &DifferentialD;\n
 * &ClockwiseContourIntegral; &ngE;</p> (as characters) */
NAOS_TEST(example_25_named_character_references_over_three_lines)
{
    static const size_t refs[][2] = {
        {0, 6},   {7, 12},  {13, 19}, {20, 27}, {28, 36},  /* line 1 */
        {37, 45}, {46, 60}, {61, 76},                      /* line 2 */
        {77, 103}, {104, 109},                             /* line 3 */
    };
    static const size_t lines[][2] = {{0, 5}, {5, 8}, {8, 10}};
    amark_document_t *document =
        new_document("&nbsp; &amp; &copy; &AElig; &Dcaron;\n"
                     "&frac34; &HilbertSpace; &DifferentialD;\n"
                     "&ClockwiseContourIntegral; &ngE;\n");
    amark_node_t *paragraph = add_block(amark_document_root(document), AMARK_NODE_PARAGRAPH, 0, 3);
    amark_node_t *texts[3];
    size_t line;

    add_content(paragraph, 0, 0, 36);
    add_content(paragraph, 1, 0, 39);
    add_content(paragraph, 2, 0, 32);
    for (line = 0; line < 3; line++)
    {
        size_t i;

        texts[line] = add_node(paragraph, AMARK_NODE_TEXT);
        for (i = lines[line][0]; i < lines[line][1]; i++)
        {
            if (i > lines[line][0])
            {
                token(texts[line], paragraph, refs[i - 1][1], refs[i][0], AMARK_SPELL_TEXT,
                      AMARK_TEXT_LITERAL);
            }
            token(texts[line], paragraph, refs[i][0], refs[i][1], AMARK_SPELL_CHAR_REF,
                  AMARK_TEXT_LITERAL);
        }
        if (line < 2)
        {
            (void)scan_break(paragraph, paragraph, AMARK_NODE_SOFTBREAK,
                             refs[lines[line][1] - 1][1], refs[lines[line][1] - 1][1]);
        }
    }

    CHECK_SHAPE(paragraph, "(paragraph (text) (softbreak) (text) (softbreak) (text))");
    /* U+00A0 & U+00A9 U+00C6 U+010E */
    CHECK_TEXT(texts[0], AMARK_TEXT_LITERAL, "\xC2\xA0 & \xC2\xA9 \xC3\x86 \xC4\x8E");
    /* U+00BE U+210B U+2146 */
    CHECK_TEXT(texts[1], AMARK_TEXT_LITERAL, "\xC2\xBE \xE2\x84\x8B \xE2\x85\x86");
    /* U+2232, U+2267 U+0338 */
    CHECK_TEXT(texts[2], AMARK_TEXT_LITERAL, "\xE2\x88\xB2 \xE2\x89\xA7\xCC\xB8");
    check_cover(paragraph);
    CHECK_ASSEMBLED(document);
    amark_document_destroy(document);
}

/* ---- Inline links ------------------------------------------------------------------------- */

/* Example 22: "[foo](/bar\* "ti\*tle")"
 * <p><a href="/bar*" title="ti*tle">foo</a></p> */
NAOS_TEST(example_22_escapes_in_the_destination_and_the_title)
{
    amark_document_t *document = new_document("[foo](/bar\\* \"ti\\*tle\")\n");
    amark_node_t *paragraph = add_block(amark_document_root(document), AMARK_NODE_PARAGRAPH, 0, 1);
    amark_node_t *bracket;
    amark_node_t *foo;
    amark_node_t *link;

    add_content(paragraph, 0, 0, 23);
    bracket = scan_run(paragraph, paragraph, 0, 1);
    foo = scan_text(paragraph, paragraph, 1, 4);
    link = close_bracket(bracket);
    token(link, paragraph, 4, 6, AMARK_SPELL_DELIMITER, AMARK_FACET_NONE);
    token(link, paragraph, 6, 10, AMARK_SPELL_TEXT, AMARK_LINK_DESTINATION);
    token(link, paragraph, 10, 12, AMARK_SPELL_ESCAPE, AMARK_LINK_DESTINATION);
    token(link, paragraph, 12, 13, AMARK_SPELL_SPACE, AMARK_FACET_NONE);
    token(link, paragraph, 13, 14, AMARK_SPELL_DELIMITER, AMARK_LINK_TITLE_QUOTE);
    token(link, paragraph, 14, 16, AMARK_SPELL_TEXT, AMARK_LINK_TITLE);
    token(link, paragraph, 16, 18, AMARK_SPELL_ESCAPE, AMARK_LINK_TITLE);
    token(link, paragraph, 18, 21, AMARK_SPELL_TEXT, AMARK_LINK_TITLE);
    token(link, paragraph, 21, 22, AMARK_SPELL_DELIMITER, AMARK_LINK_TITLE_QUOTE);
    token(link, paragraph, 22, 23, AMARK_SPELL_DELIMITER, AMARK_FACET_NONE);
    record_int(link, AMARK_LINK_FORM, AMARK_LINK_FORM_INLINE);
    record_int(link, AMARK_LINK_TITLE_QUOTE, AMARK_LINK_TITLE_QUOTE_DOUBLE);

    CHECK_SHAPE(paragraph, "(paragraph (link (text)))");
    CHECK_TEXT(foo, AMARK_TEXT_LITERAL, "foo");
    CHECK_TEXT(link, AMARK_LINK_DESTINATION, "/bar*");
    CHECK_TEXT(link, AMARK_LINK_TITLE, "ti*tle");
    check_cover(paragraph);
    CHECK_ASSEMBLED(document);
    amark_document_destroy(document);
}

/* Example 505: three inline links on three lines, one per title quote.
 * <p><a href="/url" title="title">link</a>\n(twice more)</p> */
NAOS_TEST(example_505_inline_links_with_each_title_quote)
{
    static const int64_t quotes[] = {AMARK_LINK_TITLE_QUOTE_DOUBLE, AMARK_LINK_TITLE_QUOTE_SINGLE,
                                     AMARK_LINK_TITLE_QUOTE_PAREN};
    amark_document_t *document = new_document("[link](/url \"title\")\n"
                                              "[link](/url 'title')\n"
                                              "[link](/url (title))\n");
    amark_node_t *paragraph = add_block(amark_document_root(document), AMARK_NODE_PARAGRAPH, 0, 3);
    amark_node_t *links[3];
    amark_node_t *texts[3];
    size_t line;

    for (line = 0; line < 3; line++)
    {
        add_content(paragraph, (amark_line_id_t)line, 0, 20);
    }
    for (line = 0; line < 3; line++)
    {
        size_t at = 21 * line;
        amark_node_t *bracket = scan_run(paragraph, paragraph, at, 1);

        texts[line] = scan_text(paragraph, paragraph, at + 1, at + 5);
        links[line] = close_bracket(bracket);
        token(links[line], paragraph, at + 5, at + 7, AMARK_SPELL_DELIMITER, AMARK_FACET_NONE);
        token(links[line], paragraph, at + 7, at + 11, AMARK_SPELL_TEXT, AMARK_LINK_DESTINATION);
        token(links[line], paragraph, at + 11, at + 12, AMARK_SPELL_SPACE, AMARK_FACET_NONE);
        token(links[line], paragraph, at + 12, at + 13, AMARK_SPELL_DELIMITER,
              AMARK_LINK_TITLE_QUOTE);
        token(links[line], paragraph, at + 13, at + 18, AMARK_SPELL_TEXT, AMARK_LINK_TITLE);
        token(links[line], paragraph, at + 18, at + 19, AMARK_SPELL_DELIMITER,
              AMARK_LINK_TITLE_QUOTE);
        token(links[line], paragraph, at + 19, at + 20, AMARK_SPELL_DELIMITER, AMARK_FACET_NONE);
        record_int(links[line], AMARK_LINK_FORM, AMARK_LINK_FORM_INLINE);
        record_int(links[line], AMARK_LINK_TITLE_QUOTE, quotes[line]);
        if (line < 2)
        {
            (void)scan_break(paragraph, paragraph, AMARK_NODE_SOFTBREAK, at + 20, at + 20);
        }
    }

    CHECK_SHAPE(paragraph, "(paragraph (link (text)) (softbreak) (link (text)) (softbreak) "
                           "(link (text)))");
    for (line = 0; line < 3; line++)
    {
        CHECK_TEXT(texts[line], AMARK_TEXT_LITERAL, "link");
        CHECK_TEXT(links[line], AMARK_LINK_DESTINATION, "/url");
        CHECK_TEXT(links[line], AMARK_LINK_TITLE, "title");
        CHECK_NUMBER(links[line], AMARK_LINK_TITLE_QUOTE, quotes[line]);
    }
    check_cover(paragraph);
    CHECK_ASSEMBLED(document);
    amark_document_destroy(document);
}

/* ---- Reference links ---------------------------------------------------------------------- */

/* Spells the definition "[label]: destination" of one line (the label from 1 to
 * label_end, the destination from label_end + 3 to destination_end) and, when
 * title_end is not 0, ` "title"` up to title_end. */
static void spell_definition(amark_node_t *definition, size_t label_end, size_t destination_end,
                             size_t title_end)
{
    token(definition, definition, 0, 1, AMARK_SPELL_DELIMITER, AMARK_FACET_NONE);
    token(definition, definition, 1, label_end, AMARK_SPELL_TEXT, AMARK_LINK_DEFINITION_LABEL);
    token(definition, definition, label_end, label_end + 2, AMARK_SPELL_DELIMITER,
          AMARK_FACET_NONE);
    token(definition, definition, label_end + 2, label_end + 3, AMARK_SPELL_SPACE,
          AMARK_FACET_NONE);
    token(definition, definition, label_end + 3, destination_end, AMARK_SPELL_TEXT,
          AMARK_LINK_DEFINITION_DESTINATION);
    if (title_end != 0)
    {
        token(definition, definition, destination_end, destination_end + 1, AMARK_SPELL_SPACE,
              AMARK_FACET_NONE);
        token(definition, definition, destination_end + 1, destination_end + 2,
              AMARK_SPELL_DELIMITER, AMARK_LINK_DEFINITION_TITLE_QUOTE);
        token(definition, definition, destination_end + 2, title_end - 1, AMARK_SPELL_TEXT,
              AMARK_LINK_DEFINITION_TITLE);
        token(definition, definition, title_end - 1, title_end, AMARK_SPELL_DELIMITER,
              AMARK_LINK_DEFINITION_TITLE_QUOTE);
        record_int(definition, AMARK_LINK_DEFINITION_TITLE_QUOTE,
                   AMARK_LINK_DEFINITION_TITLE_QUOTE_DOUBLE);
    }
}

/* Example 527: "[foo][bar]\n\n[bar]: /url "title""
 * <p><a href="/url" title="title">foo</a></p> */
NAOS_TEST(example_527_a_full_reference_link)
{
    amark_document_t *document = new_document("[foo][bar]\n\n[bar]: /url \"title\"\n");
    amark_node_t *root = amark_document_root(document);
    amark_node_t *paragraph = add_block(root, AMARK_NODE_PARAGRAPH, 0, 1);
    amark_node_t *rest;
    amark_node_t *definition;
    amark_node_t *bracket;
    amark_node_t *foo;
    amark_node_t *link;

    /* The block stage. */
    add_content(paragraph, 0, 0, 10);
    add_blank_line(root, 1);
    rest = add_block(root, AMARK_NODE_PARAGRAPH, 2, 1);
    add_content(rest, 2, 0, 19);
    definition = take_definition(rest, 2);
    drop_empty_paragraph(rest);
    CHECK_CONTENT(definition, "[bar]: /url \"title\"");
    spell_definition(definition, 4, 11, 19);

    /* The inlines. */
    bracket = scan_run(paragraph, paragraph, 0, 1);
    foo = scan_text(paragraph, paragraph, 1, 4);
    link = close_bracket(bracket);
    token(link, paragraph, 4, 5, AMARK_SPELL_DELIMITER, AMARK_FACET_NONE);
    token(link, paragraph, 5, 6, AMARK_SPELL_DELIMITER, AMARK_FACET_NONE);
    token(link, paragraph, 6, 9, AMARK_SPELL_TEXT, AMARK_LINK_LABEL);
    token(link, paragraph, 9, 10, AMARK_SPELL_DELIMITER, AMARK_FACET_NONE);
    record_int(link, AMARK_LINK_FORM, AMARK_LINK_FORM_FULL);

    CHECK_SHAPE(root, "(document (paragraph (link (text))) (link_definition))");
    CHECK_TEXT(definition, AMARK_LINK_DEFINITION_LABEL, "bar");
    CHECK_TEXT(definition, AMARK_LINK_DEFINITION_DESTINATION, "/url");
    CHECK_TEXT(definition, AMARK_LINK_DEFINITION_TITLE, "title");
    CHECK_TEXT(foo, AMARK_TEXT_LITERAL, "foo");
    CHECK_TEXT(link, AMARK_LINK_LABEL, "bar");
    CHECK_TEXT(link, AMARK_LINK_DESTINATION, "/url");
    CHECK_TEXT(link, AMARK_LINK_TITLE, "title");
    check_resolves_to(document, link, definition);
    check_cover(paragraph);
    check_cover(definition);
    CHECK_ASSEMBLED(document);
    amark_document_destroy(document);
}

/* Example 561: "[Foo]\n\n[foo]: /url "title"" -- a shortcut, matched case
 * insensitively.
 * <p><a href="/url" title="title">Foo</a></p> */
NAOS_TEST(example_561_a_shortcut_reference_link)
{
    amark_document_t *document = new_document("[Foo]\n\n[foo]: /url \"title\"\n");
    amark_node_t *root = amark_document_root(document);
    amark_node_t *paragraph = add_block(root, AMARK_NODE_PARAGRAPH, 0, 1);
    amark_node_t *rest;
    amark_node_t *definition;
    amark_node_t *bracket;
    amark_node_t *foo;
    amark_node_t *link;

    add_content(paragraph, 0, 0, 5);
    add_blank_line(root, 1);
    rest = add_block(root, AMARK_NODE_PARAGRAPH, 2, 1);
    add_content(rest, 2, 0, 19);
    definition = take_definition(rest, 2);
    drop_empty_paragraph(rest);
    spell_definition(definition, 4, 11, 19);

    bracket = scan_run(paragraph, paragraph, 0, 1);
    foo = scan_text(paragraph, paragraph, 1, 4);
    link = close_bracket(bracket);
    token(link, paragraph, 4, 5, AMARK_SPELL_DELIMITER, AMARK_FACET_NONE);
    record_int(link, AMARK_LINK_FORM, AMARK_LINK_FORM_SHORTCUT);
    /* The label of a shortcut is its link text: recorded, without tokens. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_record_string(link, AMARK_LINK_LABEL, "Foo", 3));

    CHECK_SHAPE(root, "(document (paragraph (link (text))) (link_definition))");
    CHECK_TEXT(foo, AMARK_TEXT_LITERAL, "Foo");
    CHECK_TEXT(link, AMARK_LINK_LABEL, "Foo");
    CHECK_TEXT(link, AMARK_LINK_DESTINATION, "/url");
    CHECK_TEXT(link, AMARK_LINK_TITLE, "title");
    check_resolves_to(document, link, definition);
    check_cover(paragraph);
    check_cover(definition);
    CHECK_ASSEMBLED(document);
    amark_document_destroy(document);
}

/* Example 570: "[foo][bar][baz]\n\n[baz]: /url1\n[bar]: /url2" -- a full
 * reference, then a shortcut; the two definitions come from one paragraph.
 * <p><a href="/url2">foo</a><a href="/url1">baz</a></p> */
NAOS_TEST(example_570_a_full_and_a_shortcut_reference_link)
{
    amark_document_t *document =
        new_document("[foo][bar][baz]\n\n[baz]: /url1\n[bar]: /url2\n");
    amark_node_t *root = amark_document_root(document);
    amark_node_t *paragraph = add_block(root, AMARK_NODE_PARAGRAPH, 0, 1);
    amark_node_t *rest;
    amark_node_t *baz_definition;
    amark_node_t *bar_definition;
    amark_node_t *bracket;
    amark_node_t *foo;
    amark_node_t *baz;
    amark_node_t *full;
    amark_node_t *shortcut;

    add_content(paragraph, 0, 0, 15);
    add_blank_line(root, 1);
    rest = add_block(root, AMARK_NODE_PARAGRAPH, 2, 2);
    add_content(rest, 2, 0, 12);
    add_content(rest, 3, 0, 12);
    CHECK_CONTENT(rest, "[baz]: /url1\n[bar]: /url2");
    baz_definition = take_definition(rest, 2);
    CHECK_CONTENT(rest, "[bar]: /url2");
    bar_definition = take_definition(rest, 3);
    drop_empty_paragraph(rest);
    spell_definition(baz_definition, 4, 12, 0);
    spell_definition(bar_definition, 4, 12, 0);

    bracket = scan_run(paragraph, paragraph, 0, 1);
    foo = scan_text(paragraph, paragraph, 1, 4);
    full = close_bracket(bracket);
    token(full, paragraph, 4, 5, AMARK_SPELL_DELIMITER, AMARK_FACET_NONE);
    token(full, paragraph, 5, 6, AMARK_SPELL_DELIMITER, AMARK_FACET_NONE);
    token(full, paragraph, 6, 9, AMARK_SPELL_TEXT, AMARK_LINK_LABEL);
    token(full, paragraph, 9, 10, AMARK_SPELL_DELIMITER, AMARK_FACET_NONE);
    record_int(full, AMARK_LINK_FORM, AMARK_LINK_FORM_FULL);
    bracket = scan_run(paragraph, paragraph, 10, 1);
    baz = scan_text(paragraph, paragraph, 11, 14);
    shortcut = close_bracket(bracket);
    token(shortcut, paragraph, 14, 15, AMARK_SPELL_DELIMITER, AMARK_FACET_NONE);
    record_int(shortcut, AMARK_LINK_FORM, AMARK_LINK_FORM_SHORTCUT);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_record_string(shortcut, AMARK_LINK_LABEL, "baz", 3));

    CHECK_SHAPE(root, "(document (paragraph (link (text)) (link (text))) (link_definition) "
                      "(link_definition))");
    NAOS_CHECK_PTR_EQ(baz_definition, child_at(root, 1));
    NAOS_CHECK_PTR_EQ(bar_definition, child_at(root, 2));
    CHECK_TEXT(foo, AMARK_TEXT_LITERAL, "foo");
    CHECK_TEXT(baz, AMARK_TEXT_LITERAL, "baz");
    CHECK_TEXT(full, AMARK_LINK_LABEL, "bar");
    CHECK_TEXT(full, AMARK_LINK_DESTINATION, "/url2");
    CHECK_TEXT(full, AMARK_LINK_TITLE, "");
    CHECK_TEXT(shortcut, AMARK_LINK_DESTINATION, "/url1");
    CHECK_TEXT(shortcut, AMARK_LINK_TITLE, "");
    check_resolves_to(document, full, bar_definition);
    check_resolves_to(document, shortcut, baz_definition);
    check_cover(paragraph);
    check_cover(baz_definition);
    check_cover(bar_definition);
    CHECK_ASSEMBLED(document);
    amark_document_destroy(document);
}

/* ---- Line breaks --------------------------------------------------------------------------- */

/* Example 633: "foo  \nbaz" -- a hard line break of spaces.
 * <p>foo<br />\nbaz</p> */
NAOS_TEST(example_633_a_hard_line_break_of_spaces)
{
    amark_document_t *document = new_document("foo  \nbaz\n");
    amark_node_t *paragraph = add_block(amark_document_root(document), AMARK_NODE_PARAGRAPH, 0, 2);
    amark_node_t *foo;
    amark_node_t *linebreak;
    amark_node_t *baz;

    add_content(paragraph, 0, 0, 5);
    add_content(paragraph, 1, 0, 3);
    CHECK_CONTENT(paragraph, "foo  \nbaz");
    foo = scan_text(paragraph, paragraph, 0, 3);
    linebreak = scan_break(paragraph, paragraph, AMARK_NODE_LINEBREAK, 3, 5);
    record_int(linebreak, AMARK_LINEBREAK_FORM, AMARK_LINEBREAK_FORM_SPACES);
    baz = scan_text(paragraph, paragraph, 6, 9);

    CHECK_SHAPE(paragraph, "(paragraph (text) (linebreak) (text))");
    CHECK_TEXT(foo, AMARK_TEXT_LITERAL, "foo");
    CHECK_TEXT(baz, AMARK_TEXT_LITERAL, "baz");
    CHECK_NUMBER(linebreak, AMARK_LINEBREAK_FORM, AMARK_LINEBREAK_FORM_SPACES);
    check_cover(paragraph);
    CHECK_ASSEMBLED(document);
    amark_document_destroy(document);
}

/* Example 634: "foo\\\nbaz" -- a hard line break of a backslash.
 * <p>foo<br />\nbaz</p> */
NAOS_TEST(example_634_a_hard_line_break_of_a_backslash)
{
    amark_document_t *document = new_document("foo\\\nbaz\n");
    amark_node_t *paragraph = add_block(amark_document_root(document), AMARK_NODE_PARAGRAPH, 0, 2);
    amark_node_t *foo;
    amark_node_t *linebreak;
    amark_node_t *baz;

    add_content(paragraph, 0, 0, 4);
    add_content(paragraph, 1, 0, 3);
    CHECK_CONTENT(paragraph, "foo\\\nbaz");
    foo = scan_text(paragraph, paragraph, 0, 3);
    linebreak = add_node(paragraph, AMARK_NODE_LINEBREAK);
    token(linebreak, paragraph, 3, 4, AMARK_SPELL_MARKER, AMARK_FACET_NONE);
    token(linebreak, paragraph, 4, 5, AMARK_SPELL_LINE_BREAK, AMARK_FACET_NONE);
    record_int(linebreak, AMARK_LINEBREAK_FORM, AMARK_LINEBREAK_FORM_BACKSLASH);
    baz = scan_text(paragraph, paragraph, 5, 8);

    CHECK_SHAPE(paragraph, "(paragraph (text) (linebreak) (text))");
    CHECK_TEXT(foo, AMARK_TEXT_LITERAL, "foo");
    CHECK_TEXT(baz, AMARK_TEXT_LITERAL, "baz");
    CHECK_NUMBER(linebreak, AMARK_LINEBREAK_FORM, AMARK_LINEBREAK_FORM_BACKSLASH);
    check_cover(paragraph);
    CHECK_ASSEMBLED(document);
    amark_document_destroy(document);
}

/* Example 649: "foo \n baz" -- a soft line break; the space at the end of the
 * first line is spelled by the softbreak, the space at the start of the second
 * is the own spelling of the paragraph.
 * <p>foo\nbaz</p> */
NAOS_TEST(example_649_a_soft_line_break_between_spaces)
{
    amark_document_t *document = new_document("foo \n baz\n");
    amark_node_t *paragraph = add_block(amark_document_root(document), AMARK_NODE_PARAGRAPH, 0, 2);
    amark_node_t *foo;
    amark_node_t *baz;
    amark_layout_token_t indent;

    add_content(paragraph, 0, 0, 4);
    memset(&indent, 0, sizeof(indent));
    indent.struct_size = sizeof(indent);
    indent.line = 1;
    indent.column_start = 0;
    indent.column_end = 1;
    indent.kind = AMARK_SPELL_SPACE;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_token(paragraph, &indent));
    add_content(paragraph, 1, 1, 4);
    CHECK_CONTENT(paragraph, "foo \nbaz");
    foo = scan_text(paragraph, paragraph, 0, 3);
    (void)scan_break(paragraph, paragraph, AMARK_NODE_SOFTBREAK, 3, 4);
    baz = scan_text(paragraph, paragraph, 5, 8);

    CHECK_SHAPE(paragraph, "(paragraph (text) (softbreak) (text))");
    CHECK_TEXT(foo, AMARK_TEXT_LITERAL, "foo");
    CHECK_TEXT(baz, AMARK_TEXT_LITERAL, "baz");
    check_cover(paragraph);
    CHECK_ASSEMBLED(document);
    amark_document_destroy(document);
}

NAOS_SUITE(inline_assembly,
    NAOS_ADD_TEST(example_418_nested_emphasis),
    NAOS_ADD_TEST(example_413_a_delimiter_run_shared_by_two_matches),
    NAOS_ADD_TEST(example_444_an_opener_used_in_part_leaves_a_text),
    NAOS_ADD_TEST(example_443_a_closer_used_in_part_leaves_a_text),
    NAOS_ADD_TEST(example_405_emphasis_over_two_lines_of_a_block_quote),
    NAOS_ADD_TEST(example_405_emphasis_over_a_lazy_line_of_a_block_quote),
    NAOS_ADD_TEST(example_329_a_code_span_with_its_padding),
    NAOS_ADD_TEST(example_337_a_code_span_over_two_lines),
    NAOS_ADD_TEST(example_515_an_escape_in_a_link_text),
    NAOS_ADD_TEST(example_26_numeric_character_references),
    NAOS_ADD_TEST(example_25_named_character_references_over_three_lines),
    NAOS_ADD_TEST(example_22_escapes_in_the_destination_and_the_title),
    NAOS_ADD_TEST(example_505_inline_links_with_each_title_quote),
    NAOS_ADD_TEST(example_527_a_full_reference_link),
    NAOS_ADD_TEST(example_561_a_shortcut_reference_link),
    NAOS_ADD_TEST(example_570_a_full_and_a_shortcut_reference_link),
    NAOS_ADD_TEST(example_633_a_hard_line_break_of_spaces),
    NAOS_ADD_TEST(example_634_a_hard_line_break_of_a_backslash),
    NAOS_ADD_TEST(example_649_a_soft_line_break_between_spaces)
)

/* ---- Checks of the examples of INPUT.md ------------------------------------------------- */

#define ALL_FLAGS \
    (AMARK_DUMP_STYLE | AMARK_DUMP_LAYOUT | AMARK_DUMP_SPELLING | AMARK_DUMP_POSITIONS | \
     AMARK_DUMP_LINES)

/* Checks that facet of node reads the boolean expected. */
#define CHECK_BOOL(node, facet, expected) CHECK_NUMBER((node), (facet), (expected) ? 1 : 0)

/* Checks the margin of container on line: present or lazy, its columns and
 * whether it is the first line of an item. */
static void check_margin(const amark_node_t *container, amark_line_id_t line,
                         amark_margin_state_t state, uint32_t start, uint32_t end, bool first)
{
    amark_margin_t margin;
    bool found = false;

    memset(&margin, 0, sizeof(margin));
    margin.struct_size = sizeof(margin);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_find_margin(container, line, &margin, &found));
    NAOS_CHECK_TRUE(found);
    NAOS_CHECK_INT_EQ(state, margin.state);
    NAOS_CHECK_UINT_EQ(start, margin.column_start);
    NAOS_CHECK_UINT_EQ(end, margin.column_end);
    NAOS_CHECK_UINT_EQ(first ? AMARK_MARGIN_FIRST : 0u, margin.flags & AMARK_MARGIN_FIRST);
}

/* Whether container has a margin on line (false for a node that is not a
 * container). */
static bool has_margin(const amark_node_t *container, amark_line_id_t line)
{
    bool found = true;

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_find_margin(container, line, NULL, &found));
    return found;
}

/* Checks the source range of node: its offsets in the source and its start
 * and end (lines from 1, columns as amark_source_range_t tells). */
static void check_range(const amark_node_t *node, size_t start_offset, size_t end_offset,
                        uint32_t start_line, uint32_t start_column, uint32_t end_line,
                        uint32_t end_column)
{
    amark_source_range_t range;
    bool has_position = false;

    memset(&range, 0, sizeof(range));
    range.struct_size = sizeof(range);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_source_range(node, &range, &has_position));
    NAOS_CHECK_TRUE(has_position);
    NAOS_CHECK_UINT_EQ(0, range.source);
    NAOS_CHECK_UINT_EQ(start_offset, range.start_offset);
    NAOS_CHECK_UINT_EQ(end_offset, range.end_offset);
    NAOS_CHECK_UINT_EQ(start_line, range.start_line);
    NAOS_CHECK_UINT_EQ(start_column, range.start_column);
    NAOS_CHECK_UINT_EQ(end_line, range.end_line);
    NAOS_CHECK_UINT_EQ(end_column, range.end_column);
}

/* Whether node has a source position. */
static bool has_position(const amark_node_t *node)
{
    amark_source_range_t range;
    bool out = true;

    memset(&range, 0, sizeof(range));
    range.struct_size = sizeof(range);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_source_range(node, &range, &out));
    return out;
}

/* ---- INPUT.md 3.1: a paragraph in a list in a block quote ---------------------------------- */

static const char input_3_1_source[] = "> - one *two\n>   three* four\n";

/* The layout layer and the spelling layer of 3.1 (the roles of the delimiters of
 * the emphasis: 1 the opener, 2 the closer). */
static const char input_3_1_tree[] =
    "(block_quote :lines (0 2) :margins ((0 0 2 present) (1 0 2 present))\n"
    "  (list :type bullet :tight true :style (:bullet \"-\") :lines (0 2)\n"
    "    (item :lines (0 2) :margins ((0 2 4 present first) (1 2 4 present))\n"
    "      (paragraph :lines (0 2) :content laid ((0 4 12) (1 4 15))\n"
    "        (text :spelling ((text 0 4 :facet literal)))\n"
    "        (emph :style (:delimiter \"*\")\n"
    "              :spelling ((delimiter 4 5 :role 1) (delimiter 14 15 :role 2))\n"
    "          (text :spelling ((text 5 8 :facet literal)))\n"
    "          (softbreak :spelling ((line_break 8 9)))\n"
    "          (text :spelling ((text 9 14 :facet literal))))\n"
    "        (text :spelling ((text 15 20 :facet literal)))))))";

/* The dump of 3.1 with every flag (the notation of INPUT.md 3.1). */
static const char input_3_1_dump[] =
    "(document :lines (0 2) :pos (1 1 2 15)"
    " :line_table ((0 1 0 12 lf 12) (0 2 13 15 lf 15))\n"
    "  (block_quote :lines (0 2) :margins ((0 0 2 present) (1 0 2 present)) :pos (1 1 2 15)\n"
    "    (list :type bullet :start 1 :tight true :style (:bullet \"-\") :lines (0 2)"
    " :pos (1 3 2 15)\n"
    "      (item :lines (0 2) :margins ((0 2 4 present first) (1 2 4 present))"
    " :pos (1 3 2 15)\n"
    "        (paragraph :lines (0 2) :content laid ((0 4 12) (1 4 15)) :pos (1 5 2 15)\n"
    "          (text :spelling ((text 0 4 :facet literal)) :pos (1 5 1 8) \"one \")\n"
    "          (emph :style (:delimiter \"*\")"
    " :spelling ((delimiter 4 5 :role 1) (delimiter 14 15 :role 2)) :pos (1 9 2 10)\n"
    "            (text :spelling ((text 5 8 :facet literal)) :pos (1 10 1 12) \"two\")\n"
    "            (softbreak :spelling ((line_break 8 9)) :pos (1 13 1 13))\n"
    "            (text :spelling ((text 9 14 :facet literal)) :pos (2 5 2 9) \"three\"))\n"
    "          (text :spelling ((text 15 20 :facet literal)) :pos (2 11 2 15) \" four\"))))))\n";

NAOS_TEST(input_3_1_a_paragraph_in_a_list_in_a_block_quote)
{
    amark_document_t *document = new_document(input_3_1_source);
    amark_node_t *root = amark_document_root(document);
    amark_node_t *quote;
    amark_node_t *list;
    amark_node_t *item;
    amark_node_t *paragraph;
    amark_node_t *emph;

    AMARK_CHECK_TREE_BUILD(document, NULL, input_3_1_tree);
    quote = amark_node_first_child(root);
    NAOS_REQUIRE_NOT_NULL(quote);
    list = amark_node_first_child(quote);
    NAOS_REQUIRE_NOT_NULL(list);
    item = amark_node_first_child(list);
    NAOS_REQUIRE_NOT_NULL(item);
    paragraph = amark_node_first_child(item);
    NAOS_REQUIRE_NOT_NULL(paragraph);
    CHECK_SHAPE(root, "(document (block_quote (list (item (paragraph (text) (emph (text) "
                      "(softbreak) (text)) (text))))))");
    emph = child_at(paragraph, 1);

    /* Lossless, every node complete and verbatim. */
    CHECK_ASSEMBLED(document);
    /* The division of the lines: [0,2) the block quote, [2,4) the item, [4,end)
     * the paragraph. */
    check_margin(quote, 0, AMARK_MARGIN_PRESENT, 0, 2, false);
    check_margin(quote, 1, AMARK_MARGIN_PRESENT, 0, 2, false);
    check_margin(item, 0, AMARK_MARGIN_PRESENT, 2, 4, true);
    check_margin(item, 1, AMARK_MARGIN_PRESENT, 2, 4, false);
    /* The content: "one *two" + the virtual "\n" + "three* four". */
    NAOS_CHECK_INT_EQ(AMARK_CONTENT_LAID, amark_block_content_state(paragraph));
    CHECK_CONTENT(paragraph, "one *two\nthree* four");
    check_cover(paragraph);

    /* The values. */
    CHECK_NUMBER(list, AMARK_LIST_TYPE, AMARK_LIST_TYPE_BULLET);
    CHECK_NUMBER(list, AMARK_LIST_BULLET, AMARK_LIST_BULLET_DASH);
    CHECK_BOOL(list, AMARK_LIST_TIGHT, true);
    CHECK_TEXT(child_at(paragraph, 0), AMARK_TEXT_LITERAL, "one ");
    CHECK_NUMBER(emph, AMARK_EMPH_DELIMITER, AMARK_EMPH_DELIMITER_ASTERISK);
    CHECK_TEXT(child_at(emph, 0), AMARK_TEXT_LITERAL, "two");
    CHECK_TEXT(child_at(emph, 2), AMARK_TEXT_LITERAL, "three");
    CHECK_TEXT(child_at(paragraph, 2), AMARK_TEXT_LITERAL, " four");

    /* The positions: the paragraph from "one" to the end of the second line,
     * the emphasis from its "*" on the first line to its "*" on the second. */
    check_range(quote, 0, 28, 1, 1, 2, 15);
    check_range(paragraph, 4, 28, 1, 5, 2, 15);
    check_range(emph, 8, 23, 1, 9, 2, 10);
    check_range(child_at(emph, 1), 12, 13, 1, 13, 1, 13);

    /* The dump. */
    AMARK_CHECK_TREE_DUMP(root, ALL_FLAGS, input_3_1_dump);
    amark_document_destroy(document);
}

/* ---- INPUT.md 3.3: a partial tab ------------------------------------------------------------ */

/*
 * `>\t\tfoo` (CommonMark example 7): the first tab takes the columns [1,4), the
 * second [4,8). [0,2) is the margin of the block quote (its first tab column is
 * the optional space after ">"), [2,6) the 4-column indentation of the code
 * block, [6,11) its content, whose two first columns are the rest of the
 * second tab, spelled as a tab_remainder.
 */
static const char input_3_3_tree[] =
    "(block_quote :lines (0 1) :margins ((0 0 2 present))\n"
    "  (code_block :style (:form indented)\n"
    "    :lines (0 1)\n"
    "    :tokens ((space 0 2 6))\n"
    "    :content laid ((0 6 11))\n"
    "    :spelling ((tab_remainder 0 2) (text 2 5))))";

static const char input_3_3_dump[] =
    "(document :lines (0 1) :pos (1 1 1 6) :line_table ((0 1 0 6 none 11))\n"
    "  (block_quote :lines (0 1) :margins ((0 0 2 present)) :pos (1 1 1 6)\n"
    "    (code_block :info \"\" :style (:form indented) :lines (0 1) :tokens ((space 0 2 6))"
    " :content laid ((0 6 11)) :spelling ((tab_remainder 0 2) (text 2 5)) :pos (1 2 1 6)"
    " \"  foo\\n\")))\n";

NAOS_TEST(input_3_3_a_partial_tab)
{
    amark_document_t *document = new_document(">\t\tfoo");
    amark_node_t *root = amark_document_root(document);
    amark_node_t *quote;
    amark_node_t *code;
    amark_layout_token_t indent;

    AMARK_CHECK_TREE_BUILD(document, NULL, input_3_3_tree);
    quote = amark_node_first_child(root);
    NAOS_REQUIRE_NOT_NULL(quote);
    code = amark_node_first_child(quote);
    NAOS_REQUIRE_NOT_NULL(code);
    CHECK_SHAPE(root, "(document (block_quote (code_block)))");

    /* Lossless: both tabs are divided by columns, without a special token. */
    CHECK_ASSEMBLED(document);
    check_margin(quote, 0, AMARK_MARGIN_PRESENT, 0, 2, false);
    NAOS_CHECK_UINT_EQ(1, amark_block_token_count(code));
    memset(&indent, 0, sizeof(indent));
    indent.struct_size = sizeof(indent);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_token_at(code, 0, &indent));
    NAOS_CHECK_UINT_EQ(2, indent.column_start);
    NAOS_CHECK_UINT_EQ(6, indent.column_end);

    /* The rest of the second tab is two spaces of the content: the content is
     * "  foo", and the literal, its lines each ended by a line break,
     * "  foo\n". */
    CHECK_CONTENT(code, "  foo");
    check_cover(code);
    CHECK_TEXT(code, AMARK_CODE_BLOCK_LITERAL, "  foo\n");
    CHECK_TEXT(code, AMARK_CODE_BLOCK_INFO, "");
    CHECK_NUMBER(code, AMARK_CODE_BLOCK_FORM, AMARK_CODE_BLOCK_FORM_INDENTED);

    /* The positions: the code block starts in the first tab (byte 1), the
     * column 2 being inside it. */
    check_range(quote, 0, 6, 1, 1, 1, 6);
    check_range(code, 1, 6, 1, 2, 1, 6);

    AMARK_CHECK_TREE_DUMP(root, ALL_FLAGS, input_3_3_dump);
    amark_document_destroy(document);
}

/* ---- INPUT.md 3.4: lazy continuation -------------------------------------------------------- */

static const char input_3_4_tree[] =
    "(block_quote :lines (0 2) :margins ((0 0 2 present) (1 0 0 lazy))\n"
    "  (paragraph :lines (0 2) :content laid ((0 2 3) (1 0 1))\n"
    "    (text :spelling ((text 0 1 :facet literal)))\n"
    "    (softbreak :spelling ((line_break 1 2)))\n"
    "    (text :spelling ((text 2 3 :facet literal)))))";

static const char input_3_4_dump[] =
    "(document :lines (0 2) :pos (1 1 2 1) :line_table ((0 1 0 3 lf 3) (0 2 4 1 lf 1))\n"
    "  (block_quote :lines (0 2) :margins ((0 0 2 present) (1 0 0 lazy)) :pos (1 1 2 1)\n"
    "    (paragraph :lines (0 2) :content laid ((0 2 3) (1 0 1)) :pos (1 3 2 1)\n"
    "      (text :spelling ((text 0 1 :facet literal)) :pos (1 3 1 3) \"a\")\n"
    "      (softbreak :spelling ((line_break 1 2)) :pos (1 4 1 4))\n"
    "      (text :spelling ((text 2 3 :facet literal)) :pos (2 1 2 1) \"b\"))))\n";

NAOS_TEST(input_3_4_lazy_continuation)
{
    amark_document_t *document = new_document("> a\nb\n");
    amark_node_t *root = amark_document_root(document);
    amark_node_t *quote;
    amark_node_t *paragraph;

    AMARK_CHECK_TREE_BUILD(document, NULL, input_3_4_tree);
    quote = amark_node_first_child(root);
    NAOS_REQUIRE_NOT_NULL(quote);
    paragraph = amark_node_first_child(quote);
    NAOS_REQUIRE_NOT_NULL(paragraph);
    CHECK_SHAPE(root, "(document (block_quote (paragraph (text) (softbreak) (text))))");

    /* Lossless: the lazy margin has no columns and adds nothing to the
     * division of L1; the block quote with it is still verbatim. */
    CHECK_ASSEMBLED(document);
    check_margin(quote, 0, AMARK_MARGIN_PRESENT, 0, 2, false);
    check_margin(quote, 1, AMARK_MARGIN_LAZY, 0, 0, false);

    CHECK_CONTENT(paragraph, "a\nb");
    check_cover(paragraph);
    CHECK_TEXT(child_at(paragraph, 0), AMARK_TEXT_LITERAL, "a");
    CHECK_TEXT(child_at(paragraph, 2), AMARK_TEXT_LITERAL, "b");

    /* The positions: L1 starts at offset 4; the virtual "\n" is the LF of L0. */
    check_range(quote, 0, 5, 1, 1, 2, 1);
    check_range(paragraph, 2, 5, 1, 3, 2, 1);
    check_range(child_at(paragraph, 1), 3, 4, 1, 4, 1, 4);
    check_range(child_at(paragraph, 2), 4, 5, 2, 1, 2, 1);

    AMARK_CHECK_TREE_DUMP(root, ALL_FLAGS, input_3_4_dump);
    amark_document_destroy(document);
}

NAOS_SUITE(input_examples,
    NAOS_ADD_TEST(input_3_1_a_paragraph_in_a_list_in_a_block_quote),
    NAOS_ADD_TEST(input_3_3_a_partial_tab),
    NAOS_ADD_TEST(input_3_4_lazy_continuation)
)

/* ---- The msgstr insertion (INPUT.md 3.2; 8.6, 11.4) ------------------------------------------ */

/* INPUT.md 3.1 with a second item: "five". */
static const char msgstr_target_source[] = "> - one *two\n>   three* four\n> - five\n";

static const char msgstr_target_tree[] =
    "(block_quote :lines (0 3)\n"
    "  :margins ((0 0 2 present) (1 0 2 present) (2 0 2 present))\n"
    "  (list :type bullet :tight true :style (:bullet \"-\") :lines (0 3)\n"
    "    (item :lines (0 2) :margins ((0 2 4 present first) (1 2 4 present))\n"
    "      (paragraph :lines (0 2) :content laid ((0 4 12) (1 4 15))\n"
    "        (text :spelling ((text 0 4 :facet literal)))\n"
    "        (emph :style (:delimiter \"*\")\n"
    "              :spelling ((delimiter 4 5 :role 1) (delimiter 14 15 :role 2))\n"
    "          (text :spelling ((text 5 8 :facet literal)))\n"
    "          (softbreak :spelling ((line_break 8 9)))\n"
    "          (text :spelling ((text 9 14 :facet literal))))\n"
    "        (text :spelling ((text 15 20 :facet literal)))))\n"
    "    (item :lines (2 1) :margins ((2 2 4 present first))\n"
    "      (paragraph :lines (2 1) :content laid ((2 4 8))\n"
    "        (text :spelling ((text 0 4 :facet literal)))))))";

/* The msgstr of the translator, "いち *に さん* よん", in UTF-8: "いち " is
 * [0,7), "*" [7,8), "に さん" [8,18), "*" [18,19) and " よん" [19,26). */
#define ICHI "\xE3\x81\x84\xE3\x81\xA1"
#define NI "\xE3\x81\xAB"
#define SAN "\xE3\x81\x95\xE3\x82\x93"
#define YON "\xE3\x82\x88\xE3\x82\x93"
#define MSGSTR ICHI " *" NI " " SAN "* " YON

/* The msgstr parsed as an inline fragment: a temporary leaf with free content
 * and its inline CST (the delimiters of the translator spell the emphasis). */
static const char msgstr_fragment_tree[] =
    "(paragraph :content free \"" MSGSTR "\"\n"
    "  (text :spelling ((text 0 7 :facet literal)) \"" ICHI " \")\n"
    "  (emph :style (:delimiter \"*\")\n"
    "        :spelling ((delimiter 7 8 :role 1) (delimiter 18 19 :role 2))\n"
    "    (text :spelling ((text 8 18 :facet literal)) \"" NI " " SAN "\"))\n"
    "  (text :spelling ((text 19 26 :facet literal)) \" " YON "\"))";

/* The replaced paragraph: its line range, the free content and the inlines of
 * the fragment with their spelling. */
static const char msgstr_replaced_dump[] =
    "(paragraph :lines (0 2) :content free \"" MSGSTR "\"\n"
    "  (text :spelling ((text 0 7 :facet literal)) \"" ICHI " \")\n"
    "  (emph :style (:delimiter \"*\")"
    " :spelling ((delimiter 7 8 :role 1) (delimiter 18 19 :role 2))\n"
    "    (text :spelling ((text 8 18 :facet literal)) \"" NI " " SAN "\"))\n"
    "  (text :spelling ((text 19 26 :facet literal)) \" " YON "\"))\n";

#define MSGSTR_FLAGS (AMARK_DUMP_STYLE | AMARK_DUMP_LAYOUT | AMARK_DUMP_SPELLING)

/* The msgid of leaf: its content with each line break a space and each run of
 * spaces one space (INPUT.md 3.2). */
static void make_msgid(const amark_node_t *leaf, char *out, size_t size)
{
    const char *bytes = NULL;
    size_t length = 0;
    size_t used = 0;
    size_t i;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_content_bytes(leaf, &bytes, &length));
    NAOS_REQUIRE_TRUE(length < size);
    for (i = 0; i < length; i++)
    {
        char c = (bytes[i] == '\n') ? ' ' : bytes[i];

        if ((c == ' ') && (used > 0) && (out[used - 1] == ' '))
        {
            continue;
        }
        out[used++] = c;
    }
    out[used] = '\0';
}

/* A dump of node with flags (to be freed with free()). */
static char *dump_of(const amark_node_t *node, uint32_t flags)
{
    amark_buffer_t *buffer = NULL;
    char *text = NULL;

    if (amark_buffer_create(&buffer) != AMARK_ERROR_NONE)
    {
        return NULL;
    }
    if (amark_node_dump(node, flags, buffer) == AMARK_ERROR_NONE)
    {
        size_t length = amark_buffer_length(buffer);

        text = malloc(length + 1);
        if (text != NULL)
        {
            memcpy(text, amark_buffer_data(buffer), length);
            text[length] = '\0';
        }
    }
    amark_buffer_destroy(buffer);
    return text;
}

NAOS_TEST(msgstr_replaces_the_content_of_a_paragraph_in_a_list_in_a_block_quote)
{
    amark_document_t *document = new_document(msgstr_target_source);
    amark_document_t *fragment_document = NULL;
    amark_node_t *root = amark_document_root(document);
    amark_node_t *quote;
    amark_node_t *list;
    amark_node_t *item;
    amark_node_t *paragraph;
    amark_node_t *other_item;
    amark_node_t *other;
    amark_node_t *fragment;
    amark_node_t *emph;
    amark_node_t *node;
    amark_lossless_report_t report;
    amark_spelling_state_t state;
    char msgid[64];
    char *fragment_before;
    char *other_before;
    amark_line_id_t first = 0;
    uint32_t count = 0;
    const char *bytes = NULL;
    size_t length = 0;
    size_t i;

    AMARK_CHECK_TREE_BUILD(document, NULL, msgstr_target_tree);
    quote = amark_node_first_child(root);
    NAOS_REQUIRE_NOT_NULL(quote);
    list = amark_node_first_child(quote);
    NAOS_REQUIRE_NOT_NULL(list);
    item = child_at(list, 0);
    NAOS_REQUIRE_NOT_NULL(item);
    other_item = child_at(list, 1);
    NAOS_REQUIRE_NOT_NULL(other_item);
    paragraph = amark_node_first_child(item);
    NAOS_REQUIRE_NOT_NULL(paragraph);
    other = amark_node_first_child(other_item);
    NAOS_REQUIRE_NOT_NULL(other);
    /* The unedited document is lossless and verbatim everywhere. */
    CHECK_ASSEMBLED(document);
    other_before = dump_of(other, ALL_FLAGS);
    NAOS_REQUIRE_NOT_NULL(other_before);

    /* 1. The msgid, from the content of the paragraph. */
    make_msgid(paragraph, msgid, sizeof(msgid));
    NAOS_CHECK_STR_EQ("one *two three* four", msgid);

    /* 2. The msgstr parsed as an inline fragment in another document. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &fragment_document));
    AMARK_CHECK_TREE_BUILD(fragment_document, NULL, msgstr_fragment_tree);
    fragment = amark_node_first_child(amark_document_root(fragment_document));
    NAOS_REQUIRE_NOT_NULL(fragment);
    NAOS_CHECK_INT_EQ(AMARK_CONTENT_FREE, amark_block_content_state(fragment));
    CHECK_CONTENT(fragment, MSGSTR);
    check_cover(fragment);
    fragment_before = dump_of(fragment, ALL_FLAGS);
    NAOS_REQUIRE_NOT_NULL(fragment_before);

    /* 3. The content and the inlines of the paragraph are replaced. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_replace_content(paragraph, fragment));
    NAOS_CHECK_STR_EQ("none", amark_tree_violation_name(amark_tree_check(document, NULL)));

    /* The line range, the margins of the ancestors and the styles are kept. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_lines(paragraph, &first, &count));
    NAOS_CHECK_UINT_EQ(0, first);
    NAOS_CHECK_UINT_EQ(2, count);
    NAOS_CHECK_UINT_EQ(3, amark_block_margin_count(quote));
    check_margin(quote, 0, AMARK_MARGIN_PRESENT, 0, 2, false);
    check_margin(quote, 1, AMARK_MARGIN_PRESENT, 0, 2, false);
    NAOS_CHECK_UINT_EQ(2, amark_block_margin_count(item));
    check_margin(item, 0, AMARK_MARGIN_PRESENT, 2, 4, true);
    check_margin(item, 1, AMARK_MARGIN_PRESENT, 2, 4, false);
    CHECK_NUMBER(list, AMARK_LIST_BULLET, AMARK_LIST_BULLET_DASH);
    CHECK_BOOL(list, AMARK_LIST_TIGHT, true);
    memset(&state, 0, sizeof(state));
    state.struct_size = sizeof(state);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_spelling_state(paragraph, &state));
    NAOS_CHECK_TRUE(state.complete);
    NAOS_CHECK_FALSE(amark_node_is_new(paragraph));
    /* A block keeps its position from its line range. */
    check_range(paragraph, 0, 28, 1, 1, 2, 15);

    /* The content is free content holding the bytes of the translator, and the
     * inlines keep their spelling on it: "*" is the byte the translator wrote. */
    NAOS_CHECK_INT_EQ(AMARK_CONTENT_FREE, amark_block_content_state(paragraph));
    NAOS_CHECK_UINT_EQ(0, amark_block_content_piece_count(paragraph));
    CHECK_CONTENT(paragraph, MSGSTR);
    CHECK_SHAPE(paragraph, "(paragraph (text) (emph (text)) (text))");
    check_cover(paragraph);
    emph = child_at(paragraph, 1);
    NAOS_CHECK_UINT_EQ(2, amark_node_content_token_count(emph));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_content_bytes(paragraph, &bytes, &length));
    for (i = 0; i < 2; i++)
    {
        amark_content_token_t t;

        memset(&t, 0, sizeof(t));
        t.struct_size = sizeof(t);
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_content_token_at(emph, i, &t));
        NAOS_CHECK_INT_EQ(AMARK_SPELL_DELIMITER, t.kind);
        NAOS_CHECK_UINT_EQ(t.start + 1, t.end);
        NAOS_CHECK_TRUE((t.end <= length) && (bytes[t.start] == '*'));
    }
    CHECK_TEXT(child_at(paragraph, 0), AMARK_TEXT_LITERAL, ICHI " ");
    CHECK_TEXT(child_at(emph, 0), AMARK_TEXT_LITERAL, NI " " SAN);
    CHECK_TEXT(child_at(paragraph, 2), AMARK_TEXT_LITERAL, " " YON);
    CHECK_NUMBER(emph, AMARK_EMPH_DELIMITER, AMARK_EMPH_DELIMITER_ASTERISK);
    AMARK_CHECK_TREE_DUMP(paragraph, MSGSTR_FLAGS, msgstr_replaced_dump);
    for (node = amark_node_first_child(paragraph); node != NULL;
         node = (amark_node_t *)next_in(paragraph, node))
    {
        memset(&state, 0, sizeof(state));
        state.struct_size = sizeof(state);
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_spelling_state(node, &state));
        NAOS_CHECK_TRUE(state.has_spelling);
        NAOS_CHECK_TRUE(state.complete);
        /* Free content is on no line: no position, not in the original lines. */
        NAOS_CHECK_FALSE(has_position(node));
        NAOS_CHECK_FALSE(verbatim(node));
    }

    /* The judgments: the paragraph and its ancestors cannot be output in their
     * original spelling; the other paragraph (and its item) still can. */
    NAOS_CHECK_FALSE(content_verbatim(paragraph));
    NAOS_CHECK_FALSE(verbatim(paragraph));
    NAOS_CHECK_FALSE(verbatim(item));
    NAOS_CHECK_FALSE(verbatim(list));
    NAOS_CHECK_FALSE(verbatim(quote));
    NAOS_CHECK_FALSE(verbatim(root));
    NAOS_CHECK_TRUE(verbatim(other_item));
    NAOS_CHECK_TRUE(verbatim(other));
    NAOS_CHECK_TRUE(content_verbatim(other));
    AMARK_CHECK_TREE_DUMP(other, ALL_FLAGS, other_before);

    /* The source bytes of the old content [4,end) of L0 and L1 have no owner
     * any more: the first gap is at column 4 of L0. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NOT_LOSSLESS, check_lossless(document, &report));
    NAOS_CHECK_INT_EQ(AMARK_LOSSLESS_GAP, report.problem);
    NAOS_CHECK_UINT_EQ(0, report.line);
    NAOS_CHECK_UINT_EQ(4, report.column);
    NAOS_CHECK_PTR_EQ(NULL, report.leaf);

    /* The fragment is not changed, and the target does not depend on it. */
    AMARK_CHECK_TREE_DUMP(fragment, ALL_FLAGS, fragment_before);
    NAOS_CHECK_STR_EQ("none",
                      amark_tree_violation_name(amark_tree_check(fragment_document, NULL)));
    amark_document_destroy(fragment_document);
    AMARK_CHECK_TREE_DUMP(paragraph, MSGSTR_FLAGS, msgstr_replaced_dump);
    check_cover(paragraph);
    NAOS_CHECK_TRUE(verbatim(other_item));
    NAOS_CHECK_FALSE(verbatim(root));
    CHECK_TREE_OK(document);
    free(fragment_before);
    free(other_before);
    amark_document_destroy(document);
}

/* ---- Moving a block (11.4, 11.5) ------------------------------------------------------------ */

/* L0 "> quoted", L1 "- item": a paragraph in a block quote, and a list. */
static const char move_tree[] =
    "(block_quote :lines (0 1) :margins ((0 0 2 present))\n"
    "  (paragraph :lines (0 1) :content laid ((0 2 8))\n"
    "    (text :spelling ((text 0 6 :facet literal)))))\n"
    "(list :type bullet :tight true :style (:bullet \"-\") :lines (1 1)\n"
    "  (item :lines (1 1) :margins ((1 0 2 present first))\n"
    "    (paragraph :lines (1 1) :content laid ((1 2 6))\n"
    "      (text :spelling ((text 0 4 :facet literal))))))";

NAOS_TEST(a_paragraph_moved_from_a_block_quote_into_an_item_keeps_its_own_spelling)
{
    amark_document_t *document = new_document("> quoted\n- item\n");
    amark_node_t *root = amark_document_root(document);
    amark_node_t *quote;
    amark_node_t *list;
    amark_node_t *item;
    amark_node_t *moved;
    amark_node_t *stayed;
    amark_node_t *ancestor;
    amark_lossless_report_t report;
    char *moved_before;

    AMARK_CHECK_TREE_BUILD(document, NULL, move_tree);
    quote = child_at(root, 0);
    NAOS_REQUIRE_NOT_NULL(quote);
    list = child_at(root, 1);
    NAOS_REQUIRE_NOT_NULL(list);
    item = amark_node_first_child(list);
    NAOS_REQUIRE_NOT_NULL(item);
    moved = amark_node_first_child(quote);
    NAOS_REQUIRE_NOT_NULL(moved);
    stayed = amark_node_first_child(item);
    NAOS_REQUIRE_NOT_NULL(stayed);
    CHECK_ASSEMBLED(document);
    NAOS_CHECK_TRUE(has_margin(quote, 0));
    moved_before = dump_of(moved, ALL_FLAGS);
    NAOS_REQUIRE_NOT_NULL(moved_before);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(item, moved));
    CHECK_SHAPE(root, "(document (block_quote) (list (item (paragraph (text)) "
                      "(paragraph (text)))))");
    CHECK_TREE_OK(document);

    /* The moved paragraph keeps its own spelling, its content in its original
     * lines, its value and its position. */
    NAOS_CHECK_TRUE(verbatim(moved));
    NAOS_CHECK_TRUE(content_verbatim(moved));
    NAOS_CHECK_TRUE(verbatim(amark_node_first_child(moved)));
    CHECK_TEXT(amark_node_first_child(moved), AMARK_TEXT_LITERAL, "quoted");
    check_range(moved, 2, 8, 1, 3, 1, 8);
    AMARK_CHECK_TREE_DUMP(moved, ALL_FLAGS, moved_before);

    /* No ancestor at its destination has a margin on its line (11.5): the item
     * has one on its own line only, the list and the document are not
     * containers. */
    for (ancestor = amark_node_parent(moved); ancestor != NULL;
         ancestor = amark_node_parent(ancestor))
    {
        NAOS_CHECK_FALSE(has_margin(ancestor, 0));
    }
    NAOS_CHECK_TRUE(has_margin(item, 1));
    /* The margin of the block quote stays with the block quote. */
    NAOS_CHECK_TRUE(has_margin(quote, 0));

    /* The ancestors at the destination cannot be output in their original
     * spelling; the paragraph that was in the item still can. */
    NAOS_CHECK_FALSE(verbatim(item));
    NAOS_CHECK_FALSE(verbatim(list));
    NAOS_CHECK_FALSE(verbatim(root));
    NAOS_CHECK_TRUE(verbatim(stayed));
    /* The block quote left without its paragraph does not cover its line. */
    NAOS_CHECK_FALSE(verbatim(quote));

    /* Every byte still has exactly one owner: the margin of the block quote and
     * the content of the moved paragraph. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, check_lossless(document, &report));

    /* Moving it back restores the original spelling of every node. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(quote, moved));
    CHECK_ASSEMBLED(document);
    free(moved_before);
    amark_document_destroy(document);
}

NAOS_SUITE(msgstr_insertion,
    NAOS_ADD_TEST(msgstr_replaces_the_content_of_a_paragraph_in_a_list_in_a_block_quote),
    NAOS_ADD_TEST(a_paragraph_moved_from_a_block_quote_into_an_item_keeps_its_own_spelling)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(inline_assembly),
    NAOS_ADD_SUITE(input_examples),
    NAOS_ADD_SUITE(msgstr_insertion)
)
