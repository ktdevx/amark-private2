/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The tests of the out-of-memory sweep (syntax-tree; 1.7, 14.2; design, Error
 * Handling and Testing Strategy, Robustness).
 *
 * Every operation that changes a document (records, sets, sets through the
 * on_change hooks of the built-in types, the drops of spelling, the line
 * operations, the replacement of content, the copy, the import, the import of
 * children and the lookup of the definition index) is swept with
 * AMARK_CHECK_OOM_SWEEP: each run fails the n-th allocation, for n = 1, 2, ...
 * until a run in which nothing fails. A run builds its two documents (the test
 * allocator of the sweep is global, so no block may come from outside the run),
 * dumps them with every flag, runs the operation and, when the operation fails,
 * checks that
 *   - the internal checker passes on both documents;
 *   - the dumps with every flag are those taken before the call;
 *   - the documents hold as many nodes as before;
 * and the sweep checks that every failing run returns AMARK_ERROR_OUT_OF_MEMORY
 * and frees everything. The injected failure is the n-th allocation only, so
 * the "before" dump is taken before it (a failure injected while building or
 * dumping ends the run with that failure) and the "after" dump after it.
 * A run in which the operation succeeds checks the documents and that the
 * operation changed the dump (except the lookups, which change nothing).
 */
#include "tree/tree.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <naos/naos.h>

#include "test_allocator.h"
#include "test_check.h"
#include "tree_builder.h"

#define ALL_FLAGS \
    (AMARK_DUMP_STYLE | AMARK_DUMP_LAYOUT | AMARK_DUMP_SPELLING | AMARK_DUMP_POSITIONS | \
     AMARK_DUMP_LINES)

#define CHECK_TREE_OK(document) \
    NAOS_CHECK_STR_EQ("none", amark_tree_violation_name(amark_tree_check((document), NULL)))

/* ---- The documents of the sweep ------------------------------------------------- */

/*
 * The main document:
 *   L0-L1  INPUT.md 3.1: a paragraph with inlines in an item of a list in a
 *          block quote (root child 0)
 *   L2     an ATX heading with its own markers (root child 1)
 *   L3     a blank line of the document node
 *   L4-L6  a fenced code block (root child 2)
 *   L7     a paragraph with a full and a shortcut reference (root child 3)
 *   L8-L9  a paragraph with laid content and no inline yet (root child 4),
 *          then an empty paragraph without lines (root child 5), the target of
 *          the move of lines
 *   L10    a paragraph whose laid content is one empty line (root child 6)
 * and, without layout, a setext heading (7), an indented code block (8) and
 * two link reference definitions (9 "foo", 10 "bar"). L11 "> z" is left for
 * a new block quote. The document node has 4 layout tokens on L12 (6 spaces)
 * and 4 blank lines (L3, L13-L15), and the texts "one " and "three" 4 content
 * tokens each: their arrays are full (4 is the first capacity), so one more
 * record grows them. L16 is left for a new blank line.
 */
static const char main_source[] = "> - one *two\n>   three* four\n# a #\n  \n```\nx\n```\n"
                                  "[t][Foo] [foo]\np1\np2\n\n> z\n      \n\n\n\n\n";

static const char main_tree[] =
    "(document :lines (0 17) :blanks ((3 0 2) (13 0 0) (14 0 0) (15 0 0))\n"
    "  :tokens ((space 12 0 1) (space 12 1 2) (space 12 2 3) (space 12 3 4))\n"
    "  (block_quote :lines (0 2) :margins ((0 0 2 present) (1 0 2 present))\n"
    "    (list :type bullet :tight true :style (:bullet \"-\") :lines (0 2)\n"
    "      (item :lines (0 2) :margins ((0 2 4 present first) (1 2 4 present))\n"
    "        (paragraph :lines (0 2) :content laid ((0 4 12) (1 4 15))\n"
    "          (text :spelling ((text 0 1 :facet literal) (text 1 2 :facet literal)\n"
    "                           (text 2 3 :facet literal) (text 3 4 :facet literal)))\n"
    "          (emph :style (:delimiter \"*\")\n"
    "                :spelling ((delimiter 4 5 :role 1) (delimiter 14 15 :role 2))\n"
    "            (text :spelling ((text 5 8 :facet literal)))\n"
    "            (softbreak :spelling ((line_break 8 9)))\n"
    "            (text :spelling ((text 9 10 :facet literal) (text 10 11 :facet literal)\n"
    "                             (text 11 12 :facet literal) (text 12 14 :facet literal))))\n"
    "          (text :spelling ((text 15 20 :facet literal)))))))\n"
    "  (heading :level 1 :style (:form atx :closing true) :lines (2 1)\n"
    "    :tokens ((marker 2 0 1 :role 1) (space 2 1 2) (marker 2 4 5 :role 2))\n"
    "    :content laid ((2 2 3))\n"
    "    (text :spelling ((text 0 1 :facet literal))))\n"
    "  (code_block :info \"\" :style (:form fenced :fence_char \"`\" :fence_length 3)\n"
    "    :lines (4 3) :tokens ((marker 4 0 3) (marker 6 0 3)) :content laid ((5 0 1)))\n"
    "  (paragraph :lines (7 1) :content laid ((7 0 14))\n"
    "    (link :style (:form full) :spelling ((text 4 7 :facet label))\n"
    "      (text :spelling ((text 1 2 :facet literal))))\n"
    "    (text :spelling ((text 8 9 :facet literal)))\n"
    "    (link :label \"foo\" :style (:form shortcut)\n"
    "      (text :spelling ((text 10 13 :facet literal)))))\n"
    "  (paragraph :lines (8 2) :content laid ((8 0 2) (9 0 2)))\n"
    "  (paragraph)\n"
    "  (paragraph :lines (10 1) :content laid ((10 0 0)))\n"
    "  (heading :level 1 :style (:form setext))\n"
    "  (code_block :style (:form indented) \"y\\n\")\n"
    "  (link_definition :label \"foo\" :destination \"/u\" :title \"T\")\n"
    "  (link_definition :label \"bar\" :destination \"/b\"))\n";

/* The other document, whose source is not finished (more bytes can be
 * appended): a paragraph with laid content and inlines (root child
 * 0), the source of the replacements and of the imports, and a leaf with free
 * content and no inline (root child 1). */
static const char other_source[] = "q *r*\n";

static const char other_tree[] =
    "(document :lines (0 1)\n"
    "  (paragraph :lines (0 1) :content laid ((0 0 5))\n"
    "    (text :spelling ((text 0 2 :facet literal)))\n"
    "    (emph :style (:delimiter \"*\")\n"
    "          :spelling ((delimiter 2 3 :role 1) (delimiter 4 5 :role 2))\n"
    "      (text :spelling ((text 3 4 :facet literal)))))\n"
    "  (paragraph :content free \"c\\nd\"))\n";

/* A document of definitions only: the dump reads no reference, so the
 * definitions are still on the dirty list of the index when the lookup runs. */
static const char definitions_tree[] =
    "(document\n"
    "  (link_definition :label \"foo\" :destination \"/1\")\n"
    "  (block_quote (link_definition :label \"FOO\" :destination \"/2\"))\n"
    "  (link_definition :label \"bar\" :destination \"/3\"))\n";

/* The documents of a run. */
typedef struct fixture
{
    amark_document_t *main;
    amark_document_t *other;
} fixture_t;

/* The node at path under the document node of document: each character is
 * the index of a child, 0-9 then a for 10 ("" is the document node); NULL when
 * there is none. */
static amark_node_t *at(amark_document_t *document, const char *path)
{
    amark_node_t *node = amark_document_root(document);

    for (; (*path != '\0') && (node != NULL); path++)
    {
        int index = (*path >= 'a') ? (*path - 'a' + 10) : (*path - '0');

        node = amark_node_first_child(node);
        while ((index-- > 0) && (node != NULL))
        {
            node = amark_node_next(node);
        }
    }
    return node;
}

/* A new document holding source (none when NULL), added with flags, with tree
 * built in it. */
static amark_error_t new_document(const char *source, uint32_t flags, const char *tree,
                                  amark_document_t **out)
{
    amark_document_t *document = NULL;
    amark_source_id_t id = 0;
    amark_error_t error = amark_document_create(NULL, &document);

    if ((error == AMARK_ERROR_NONE) && (source != NULL))
    {
        error = amark_document_add_source(document, source, strlen(source), flags, &id);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_test_tree_build(document, NULL, tree, NULL);
    }
    if (error != AMARK_ERROR_NONE)
    {
        amark_document_destroy(document);
        document = NULL;
    }
    *out = document;
    return error;
}

/* ---- The cases ---------------------------------------------------------------------- */

typedef amark_error_t (*operation_t)(fixture_t *f);

typedef struct oom_case
{
    const char *name;
    /* The tree of the main document: main_tree (with main_source) when NULL,
     * else this tree in a document without source. */
    const char *tree;
    operation_t operation;
    /* Whether the operation changes the dump when it succeeds. */
    bool changes;
    /* Whether the operation allocates: the sweep then fails it at least once;
     * otherwise it never fails. */
    bool allocates;
} oom_case_t;

/* ---- The diagnostics ---------------------------------------------------------------- */

/* The first broken run of the current sweep, kept with the C library (not the
 * allocator of Amark) so that it outlives the run. */
static struct
{
    size_t run;          /* the runs of the current sweep so far */
    size_t failed_calls; /* the runs whose operation returned OUT_OF_MEMORY */
    size_t broken_run;   /* the first broken run, 0 if none */
    const char *what;    /* what broke */
    char *before;        /* the dump before the call, when the dumps differ */
    char *after;         /* the dump after the call */
} diag;

static char *copy_text(const amark_buffer_t *buffer)
{
    size_t length = amark_buffer_length(buffer);
    char *text = (char *)malloc(length + 1);

    if (text != NULL)
    {
        memcpy(text, amark_buffer_data(buffer), length);
        text[length] = '\0';
    }
    return text;
}

/* Records the first broken run; returns AMARK_ERROR_INVALID_STATE, which the
 * sweep reports as a run with the wrong error. */
static amark_error_t broken(const char *what, const amark_buffer_t *before,
                            const amark_buffer_t *after)
{
    if (diag.broken_run == 0)
    {
        diag.broken_run = diag.run;
        diag.what = what;
        if ((before != NULL) && (after != NULL))
        {
            diag.before = copy_text(before);
            diag.after = copy_text(after);
        }
    }
    return AMARK_ERROR_INVALID_STATE;
}

static void reset_diag(void)
{
    free(diag.before);
    free(diag.after);
    memset(&diag, 0, sizeof(diag));
}

/* ---- The scenario -------------------------------------------------------------------- */

static amark_error_t dump_documents(const fixture_t *f, amark_buffer_t *main_dump,
                                    amark_buffer_t *other_dump)
{
    amark_error_t error = amark_node_dump(amark_document_root(f->main), ALL_FLAGS, main_dump);

    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_dump(amark_document_root(f->other), ALL_FLAGS, other_dump);
    }
    return error;
}

static bool same_dump(const amark_buffer_t *a, const amark_buffer_t *b)
{
    return (amark_buffer_length(a) == amark_buffer_length(b)) &&
           (memcmp(amark_buffer_data(a), amark_buffer_data(b), amark_buffer_length(a)) == 0);
}

static bool documents_ok(const fixture_t *f)
{
    return (amark_tree_check(f->main, NULL) == AMARK_TREE_VIOLATION_NONE) &&
           (amark_tree_check(f->other, NULL) == AMARK_TREE_VIOLATION_NONE);
}

/* Compares the state after a call with the state before it. */
static amark_error_t compare(const fixture_t *f, amark_buffer_t *const before[2],
                             amark_buffer_t *const after[2], size_t main_nodes, size_t other_nodes,
                             bool expect_same)
{
    amark_error_t error;

    if (!documents_ok(f))
    {
        return broken("the internal checker fails", NULL, NULL);
    }
    /* The injected failure is used up: the dumps cannot fail here. */
    error = dump_documents(f, after[0], after[1]);
    if (error != AMARK_ERROR_NONE)
    {
        return expect_same ? broken("the dump after the call fails", NULL, NULL) : error;
    }
    if (expect_same)
    {
        if (!same_dump(before[0], after[0]))
        {
            return broken("the dump of the main document changed", before[0], after[0]);
        }
        if (!same_dump(before[1], after[1]))
        {
            return broken("the dump of the other document changed", before[1], after[1]);
        }
        if ((f->main->node_count != main_nodes) || (f->other->node_count != other_nodes))
        {
            return broken("the node count changed", NULL, NULL);
        }
    }
    return AMARK_ERROR_NONE;
}

static amark_error_t case_scenario(void *context)
{
    const oom_case_t *c = (const oom_case_t *)context;
    fixture_t f = {NULL, NULL};
    amark_buffer_t *before[2] = {NULL, NULL};
    amark_buffer_t *after[2] = {NULL, NULL};
    size_t main_nodes = 0;
    size_t other_nodes = 0;
    amark_error_t error;
    size_t i;

    diag.run++;
    error = (c->tree == NULL)
                ? new_document(main_source, AMARK_SOURCE_FINISHED, main_tree, &f.main)
                : new_document(NULL, AMARK_SOURCE_FINISHED, c->tree, &f.main);
    if (error == AMARK_ERROR_NONE)
    {
        error = new_document(other_source, 0, other_tree, &f.other);
    }
    for (i = 0; (i < 2) && (error == AMARK_ERROR_NONE); i++)
    {
        error = amark_buffer_create(&before[i]);
        if (error == AMARK_ERROR_NONE)
        {
            error = amark_buffer_create(&after[i]);
        }
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = dump_documents(&f, before[0], before[1]);
    }
    if (error == AMARK_ERROR_NONE)
    {
        main_nodes = f.main->node_count;
        other_nodes = f.other->node_count;
        error = c->operation(&f);
        if (error == AMARK_ERROR_OUT_OF_MEMORY)
        {
            diag.failed_calls++;
            if (compare(&f, before, after, main_nodes, other_nodes, true) != AMARK_ERROR_NONE)
            {
                error = AMARK_ERROR_INVALID_STATE;
            }
        }
        else if (error == AMARK_ERROR_NONE)
        {
            error = compare(&f, before, after, main_nodes, other_nodes, false);
            if ((error == AMARK_ERROR_NONE) && c->changes && same_dump(before[0], after[0]) &&
                same_dump(before[1], after[1]))
            {
                error = broken("the operation succeeded without a change", NULL, NULL);
            }
        }
    }
    for (i = 0; i < 2; i++)
    {
        amark_buffer_destroy(before[i]);
        amark_buffer_destroy(after[i]);
    }
    amark_document_destroy(f.other);
    amark_document_destroy(f.main);
    return error;
}

/* Sweeps each case and reports the first broken run with its dumps. */
static void sweep(const oom_case_t *cases, size_t count)
{
    size_t i;

    for (i = 0; i < count; i++)
    {
        NAOS_TRACE_PUSH("case: %s", cases[i].name);
        reset_diag();
        AMARK_CHECK_OOM_SWEEP(case_scenario, (void *)&cases[i]);
        NAOS_NOTE("run %lu: %s", (unsigned long)diag.broken_run,
                  diag.what != NULL ? diag.what : "");
        NAOS_CHECK_UINT_EQ(0, diag.broken_run);
        if (diag.before != NULL)
        {
            NAOS_CHECK_STR_EQ(diag.before, diag.after);
        }
        /* The operation itself met failures (not only the building) when it
         * allocates, and none when it does not. */
        NAOS_CHECK_INT_EQ(cases[i].allocates, diag.failed_calls > 0);
        NAOS_TRACE_POP();
    }
    reset_diag();
}

/* Appends node, a new detached node, to the document node of document;
 * destroys it when that fails. */
static amark_error_t append_new(amark_document_t *document, amark_node_t *node)
{
    amark_error_t error = amark_node_append_child(amark_document_root(document), node);

    if (error != AMARK_ERROR_NONE)
    {
        amark_node_destroy(node);
    }
    return error;
}

/* ---- Records ----------------------------------------------------------------------- */

static amark_error_t record_derived_literal(fixture_t *f)
{
    return amark_node_record_string(at(f->main, "00000"), AMARK_TEXT_LITERAL, "ONE ", 4);
}

static amark_error_t record_list_start(fixture_t *f)
{
    return amark_node_record_int(at(f->main, "00"), AMARK_LIST_START, 7);
}

static amark_error_t record_definition_label(fixture_t *f)
{
    return amark_node_record_string(at(f->main, "9"), AMARK_LINK_DEFINITION_LABEL, "Baz", 3);
}

/* What a parser records for "> z" (L11): a new block quote with its lines and
 * margin, a paragraph with its lines and content, and a text with its token.
 * On failure the new subtree goes away again. */
static amark_error_t record_new_block_quote(fixture_t *f)
{
    amark_node_t *quote = NULL;
    amark_node_t *paragraph = NULL;
    amark_node_t *text = NULL;
    amark_margin_t margin;
    amark_content_token_t token;
    amark_error_t error = amark_node_create(f->main, AMARK_NODE_BLOCK_QUOTE, &quote);

    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    memset(&margin, 0, sizeof(margin));
    margin.struct_size = sizeof(margin);
    margin.line = 11;
    margin.state = AMARK_MARGIN_PRESENT;
    margin.column_start = 0;
    margin.column_end = 2;
    memset(&token, 0, sizeof(token));
    token.struct_size = sizeof(token);
    token.start = 0;
    token.end = 1;
    token.kind = AMARK_SPELL_TEXT;
    token.facet = AMARK_TEXT_LITERAL;
    error = amark_node_append_child(amark_document_root(f->main), quote);
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_block_set_lines(quote, 11, 1);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_block_add_margin(quote, &margin);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_create(f->main, AMARK_NODE_PARAGRAPH, &paragraph);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_append_child(quote, paragraph);
        if (error != AMARK_ERROR_NONE)
        {
            amark_node_destroy(paragraph);
        }
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_block_set_lines(paragraph, 11, 1);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_block_add_content(paragraph, 11, 2, 3);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_create(f->main, AMARK_NODE_TEXT, &text);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_append_child(paragraph, text);
        if (error != AMARK_ERROR_NONE)
        {
            amark_node_destroy(text);
        }
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_add_content_token(text, paragraph, &token);
    }
    if (error != AMARK_ERROR_NONE)
    {
        amark_node_destroy(quote);
    }
    return error;
}

/* A fifth layout token of the document node: its array grows. */
static amark_error_t record_layout_token(fixture_t *f)
{
    amark_layout_token_t token;

    memset(&token, 0, sizeof(token));
    token.struct_size = sizeof(token);
    token.line = 12;
    token.column_start = 4;
    token.column_end = 5;
    token.kind = AMARK_SPELL_SPACE;
    return amark_block_add_token(amark_document_root(f->main), &token);
}

/* A fifth blank line of the document node (the empty L16): its array grows. */
static amark_error_t record_blank_line(fixture_t *f)
{
    amark_blank_line_t blank;

    memset(&blank, 0, sizeof(blank));
    blank.struct_size = sizeof(blank);
    blank.line = 16;
    return amark_block_add_blank_line(amark_document_root(f->main), &blank);
}

static amark_error_t record_free_content(fixture_t *f)
{
    return amark_block_set_free_content(at(f->main, "4"), "new\r\ntext", 9);
}

/* The last token of "three", "ee" [12,14), becomes "e" and "e": a fifth token,
 * the array grows. */
static amark_error_t split_content_token(fixture_t *f)
{
    return amark_node_split_content_token(at(f->main, "000012"), 3, 13);
}

/* The opening "*" of the emphasis goes to the text "one ", after its 4
 * tokens: the array grows. */
static amark_error_t move_content_token(fixture_t *f)
{
    return amark_node_move_content_tokens(at(f->main, "00001"), at(f->main, "00000"), 0, 1);
}

static amark_error_t discard_margin(fixture_t *f)
{
    return amark_block_discard_margin(at(f->main, "0"), 1);
}

/* More bytes of the unfinished source of the other document: new lines in
 * the line table. */
static amark_error_t append_source(fixture_t *f)
{
    return amark_document_append_source(f->other, 0, "s\nt\n", 4);
}

static const oom_case_t record_cases[] = {
    {"record a literal derived from tokens", NULL, record_derived_literal, false, true},
    {"record the start of a list", NULL, record_list_start, true, false},
    {"record the label of a definition", NULL, record_definition_label, true, true},
    {"record a new block quote with a paragraph", NULL, record_new_block_quote, true, true},
    {"record a layout token", NULL, record_layout_token, true, true},
    {"record a blank line", NULL, record_blank_line, true, true},
    {"set free content", NULL, record_free_content, true, true},
    {"split a content token", NULL, split_content_token, true, true},
    {"move a content token", NULL, move_content_token, true, true},
    {"discard a margin", NULL, discard_margin, true, false},
    {"append to a source", NULL, append_source, true, true},
};

NAOS_TEST(records)
{
    sweep(record_cases, sizeof(record_cases) / sizeof(record_cases[0]));
}

/* ---- Sets ------------------------------------------------------------------------------- */

static amark_error_t set_spelled_literal(fixture_t *f)
{
    return amark_node_set_string(at(f->main, "000010"), AMARK_TEXT_LITERAL, "TWO", 3);
}

static amark_error_t set_style(fixture_t *f)
{
    return amark_node_set_int(at(f->main, "00001"), AMARK_EMPH_DELIMITER,
                              AMARK_EMPH_DELIMITER_UNDERSCORE);
}

static amark_error_t unset_style(fixture_t *f)
{
    return amark_node_unset(at(f->main, "00001"), AMARK_EMPH_DELIMITER);
}

static amark_error_t set_code_literal(fixture_t *f)
{
    return amark_node_set_string(at(f->main, "2"), AMARK_CODE_BLOCK_LITERAL, "a\r\nb\n", 5);
}

static amark_error_t set_definition_label(fixture_t *f)
{
    return amark_node_set_string(at(f->main, "a"), AMARK_LINK_DEFINITION_LABEL, "Foo", 3);
}

static amark_error_t set_inline_text(fixture_t *f)
{
    return amark_node_set_inline_text(at(f->main, "0000"), "x\ny\r\nz", 6);
}

static const oom_case_t set_cases[] = {
    {"set a literal spelled by a token", NULL, set_spelled_literal, true, true},
    {"set a style", NULL, set_style, true, false},
    {"unset a style", NULL, unset_style, true, false},
    {"set the literal of a code block", NULL, set_code_literal, true, true},
    {"set the label of a definition", NULL, set_definition_label, true, true},
    {"set the inline text of a paragraph", NULL, set_inline_text, true, true},
};

NAOS_TEST(sets)
{
    sweep(set_cases, sizeof(set_cases) / sizeof(set_cases[0]));
}

/* ---- Sets with the on_change hooks ------------------------------------------------- */

static amark_error_t set_list_type(fixture_t *f)
{
    return amark_node_set_int(at(f->main, "00"), AMARK_LIST_TYPE, AMARK_LIST_TYPE_ORDERED);
}

static amark_error_t set_setext_level(fixture_t *f)
{
    return amark_node_set_int(at(f->main, "7"), AMARK_HEADING_LEVEL, 3);
}

static amark_error_t set_indented_info(fixture_t *f)
{
    return amark_node_set_string(at(f->main, "8"), AMARK_CODE_BLOCK_INFO, "c", 1);
}

static amark_error_t set_full_reference_destination(fixture_t *f)
{
    return amark_node_set_string(at(f->main, "30"), AMARK_LINK_DESTINATION, "/new", 4);
}

static amark_error_t set_shortcut_reference_title(fixture_t *f)
{
    return amark_node_set_string(at(f->main, "32"), AMARK_LINK_TITLE, "U", 1);
}

static const oom_case_t hook_cases[] = {
    {"set the type of a list (its items lose their margins)", NULL, set_list_type, true, false},
    {"set the level of a setext heading to 3 (it becomes ATX)", NULL, set_setext_level, true,
     false},
    {"set the info of an indented code block (it becomes fenced)", NULL, set_indented_info,
     true, true},
    {"set the destination of a full reference (it becomes inline)", NULL,
     set_full_reference_destination, true, true},
    {"set the title of a shortcut reference (it becomes inline)", NULL,
     set_shortcut_reference_title, true, true},
};

NAOS_TEST(sets_with_hooks)
{
    sweep(hook_cases, sizeof(hook_cases) / sizeof(hook_cases[0]));
}

/* ---- Drops of spelling --------------------------------------------------------------- */

#define DISCARD_SPELLING(name, path) \
    static amark_error_t name(fixture_t *f) \
    { \
        return amark_node_discard_spelling(at(f->main, path)); \
    }

DISCARD_SPELLING(discard_paragraph, "0000")
DISCARD_SPELLING(discard_item, "000")
DISCARD_SPELLING(discard_list, "00")
DISCARD_SPELLING(discard_block_quote, "0")
DISCARD_SPELLING(discard_emph, "00001")
DISCARD_SPELLING(discard_text, "00000")
DISCARD_SPELLING(discard_heading, "1")
DISCARD_SPELLING(discard_code_block, "2")
DISCARD_SPELLING(discard_full_reference, "30")
DISCARD_SPELLING(discard_raw_paragraph, "4")
DISCARD_SPELLING(discard_empty_paragraph, "6")
DISCARD_SPELLING(discard_document, "")

static amark_error_t discard_literal_spelling(fixture_t *f)
{
    return amark_node_discard_facet_spelling(at(f->main, "000012"), AMARK_TEXT_LITERAL);
}

static amark_error_t discard_code_literal_spelling(fixture_t *f)
{
    return amark_node_discard_facet_spelling(at(f->main, "2"), AMARK_CODE_BLOCK_LITERAL);
}

static amark_error_t discard_label_spelling(fixture_t *f)
{
    return amark_node_discard_facet_spelling(at(f->main, "30"), AMARK_LINK_LABEL);
}

static const oom_case_t discard_cases[] = {
    {"drop the spelling of a paragraph with inlines", NULL, discard_paragraph, true, true},
    {"drop the spelling of an item", NULL, discard_item, true, false},
    {"drop the spelling of a list", NULL, discard_list, true, false},
    {"drop the spelling of a block quote", NULL, discard_block_quote, true, false},
    {"drop the spelling of an emphasis", NULL, discard_emph, true, false},
    {"drop the spelling of a text", NULL, discard_text, true, true},
    {"drop the spelling of an ATX heading", NULL, discard_heading, true, true},
    {"drop the spelling of a fenced code block", NULL, discard_code_block, true, true},
    {"drop the spelling of a full reference", NULL, discard_full_reference, true, true},
    {"drop the spelling of a paragraph without inlines", NULL, discard_raw_paragraph, true, true},
    {"drop the spelling of an empty paragraph (no line left)", NULL, discard_empty_paragraph,
     true, true},
    {"drop the spelling of the document node", NULL, discard_document, true, false},
    {"drop the spelling of a literal", NULL, discard_literal_spelling, true, true},
    {"drop the spelling of the literal of a code block", NULL, discard_code_literal_spelling,
     true, true},
    {"drop the spelling of a label", NULL, discard_label_spelling, true, true},
};

NAOS_TEST(drops_of_spelling)
{
    sweep(discard_cases, sizeof(discard_cases) / sizeof(discard_cases[0]));
}

/* ---- Line operations ------------------------------------------------------------------ */

static amark_error_t move_last_line(fixture_t *f)
{
    return amark_block_move_lines(at(f->main, "4"), at(f->main, "5"), 9, 1);
}

static amark_error_t remove_first_line(fixture_t *f)
{
    return amark_block_remove_lines(at(f->main, "4"), 8, 1);
}

static amark_error_t remove_every_line(fixture_t *f)
{
    return amark_block_remove_lines(at(f->main, "4"), 8, 2);
}

static amark_error_t trim_content_start(fixture_t *f)
{
    return amark_block_trim_content_start(at(f->main, "4"), 1, AMARK_SPELL_MARKER,
                                          AMARK_FACET_NONE, 0);
}

/* The lines of a copy: the copy shares the content, which the operation
 * copies before it changes it. */
static amark_error_t remove_line_of_shared_content(fixture_t *f)
{
    amark_node_t *copy = NULL;
    amark_error_t error = amark_node_copy(at(f->main, "4"), &copy);

    if (error == AMARK_ERROR_NONE)
    {
        error = amark_block_remove_lines(copy, 9, 1);
        amark_node_destroy(copy);
    }
    return error;
}

static const oom_case_t line_cases[] = {
    {"move the last line to another paragraph", NULL, move_last_line, true, true},
    {"remove the first line", NULL, remove_first_line, true, false},
    {"remove every line", NULL, remove_every_line, true, false},
    {"trim the start of the content", NULL, trim_content_start, true, true},
    {"remove a line of shared content", NULL, remove_line_of_shared_content, false, true},
};

NAOS_TEST(line_operations)
{
    sweep(line_cases, sizeof(line_cases) / sizeof(line_cases[0]));
}

/* ---- Replacement of content ---------------------------------------------------------- */

static amark_error_t replace_from_other_document(fixture_t *f)
{
    return amark_block_replace_content(at(f->main, "0000"), at(f->other, "0"));
}

static amark_error_t replace_from_same_document(fixture_t *f)
{
    return amark_block_replace_content(at(f->main, "0000"), at(f->main, "3"));
}

static amark_error_t replace_with_itself(fixture_t *f)
{
    return amark_block_replace_content(at(f->main, "3"), at(f->main, "3"));
}

/* A leaf with its own notation: the markers of the ATX heading stay. */
static amark_error_t replace_heading(fixture_t *f)
{
    amark_node_t *heading = at(f->main, "1");
    amark_error_t error = amark_block_replace_content(heading, at(f->other, "0"));

    if ((error == AMARK_ERROR_NONE) &&
        ((amark_block_token_count(heading) != 3) ||
         (amark_block_content_state(heading) != AMARK_CONTENT_FREE)))
    {
        return broken("the heading lost its markers", NULL, NULL);
    }
    return error;
}

static amark_error_t replace_code_block(fixture_t *f)
{
    return amark_block_replace_content(at(f->main, "2"), at(f->other, "1"));
}

static const oom_case_t replace_cases[] = {
    {"replace the content from another document", NULL, replace_from_other_document, true, true},
    {"replace the content from the same document", NULL, replace_from_same_document, true, true},
    {"replace the content with itself", NULL, replace_with_itself, true, true},
    {"replace the content of an ATX heading", NULL, replace_heading, true, true},
    {"replace the content of a fenced code block", NULL, replace_code_block, true, true},
};

NAOS_TEST(replacements_of_content)
{
    sweep(replace_cases, sizeof(replace_cases) / sizeof(replace_cases[0]));
}

/* ---- Copy, import and import of children --------------------------------------------- */

static amark_error_t copy_and_append(amark_document_t *document, const char *path)
{
    amark_node_t *copy = NULL;
    amark_error_t error = amark_node_copy(at(document, path), &copy);

    return (error == AMARK_ERROR_NONE) ? append_new(document, copy) : error;
}

static amark_error_t copy_block_quote(fixture_t *f)
{
    return copy_and_append(f->main, "0");
}

static amark_error_t copy_references(fixture_t *f)
{
    return copy_and_append(f->main, "3");
}

static amark_error_t copy_definition(fixture_t *f)
{
    return copy_and_append(f->main, "9");
}

static amark_error_t import_and_append(amark_document_t *target, amark_document_t *source,
                                       const char *path)
{
    amark_node_t *imported = NULL;
    amark_error_t error = amark_node_import(target, at(source, path), &imported);

    return (error == AMARK_ERROR_NONE) ? append_new(target, imported) : error;
}

static amark_error_t import_paragraph(fixture_t *f)
{
    return import_and_append(f->main, f->other, "0");
}

static amark_error_t import_block_quote(fixture_t *f)
{
    return import_and_append(f->other, f->main, "0");
}

static amark_error_t import_definition(fixture_t *f)
{
    return import_and_append(f->other, f->main, "9");
}

static amark_error_t import_children_from_other(fixture_t *f)
{
    return amark_node_import_children(amark_document_root(f->main), at(f->main, "1"),
                                      amark_document_root(f->other));
}

static amark_error_t import_children_of_main(fixture_t *f)
{
    return amark_node_import_children(amark_document_root(f->other), NULL,
                                      amark_document_root(f->main));
}

static amark_error_t import_children_of_same_document(fixture_t *f)
{
    return amark_node_import_children(amark_document_root(f->main), NULL, at(f->main, "0"));
}

static const oom_case_t copy_cases[] = {
    {"copy a block quote", NULL, copy_block_quote, true, true},
    {"copy a paragraph with references", NULL, copy_references, true, true},
    {"copy a definition", NULL, copy_definition, true, true},
    {"import a paragraph", NULL, import_paragraph, true, true},
    {"import a block quote", NULL, import_block_quote, true, true},
    {"import a definition", NULL, import_definition, true, true},
    {"import the children of a document", NULL, import_children_from_other, true, true},
    {"import every child of the main document", NULL, import_children_of_main, true, true},
    {"import children from the same document", NULL, import_children_of_same_document, true, true},
};

NAOS_TEST(copies_and_imports)
{
    sweep(copy_cases, sizeof(copy_cases) / sizeof(copy_cases[0]));
}

/* ---- Lookups of the definition index -------------------------------------------------- */

static amark_error_t lookup_definitions(fixture_t *f)
{
    amark_node_t *found = NULL;
    amark_error_t error = amark_document_find_definition(f->main, "Foo", 3, &found);

    if ((error == AMARK_ERROR_NONE) && (found != at(f->main, "0")))
    {
        return broken("the lookup of foo found another definition", NULL, NULL);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_document_find_definition(f->main, "BAR", 3, &found);
    }
    if ((error == AMARK_ERROR_NONE) && (found != at(f->main, "2")))
    {
        return broken("the lookup of bar found another definition", NULL, NULL);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_document_find_definition(f->main, "none", 4, &found);
    }
    if ((error == AMARK_ERROR_NONE) && (found != NULL))
    {
        return broken("the lookup of none found a definition", NULL, NULL);
    }
    return error;
}

/* After a refused lookup, the same lookups succeed and find the same
 * definitions; the result of the refused one is returned. */
static amark_error_t lookup_dirty_definitions(fixture_t *f)
{
    amark_error_t error = lookup_definitions(f);

    if ((error == AMARK_ERROR_OUT_OF_MEMORY) && (lookup_definitions(f) != AMARK_ERROR_NONE))
    {
        return broken("the lookups after a refused one fail", NULL, NULL);
    }
    return error;
}

/* Whether facet of node reads as the string expected. */
static bool reads(const amark_node_t *node, amark_facet_t facet, const char *expected)
{
    amark_value_t value;

    value.struct_size = sizeof(value);
    return (amark_node_get(node, facet, &value) == AMARK_ERROR_NONE) &&
           (value.state == AMARK_VALUE_PRESENT) && (value.length == strlen(expected)) &&
           (memcmp(value.text, expected, value.length) == 0);
}

/* The lookups through the values of the references, after a change of the
 * order of the definitions. */
static amark_error_t lookup_through_references(fixture_t *f)
{
    amark_value_t value;
    amark_error_t error = amark_node_insert_before(at(f->main, "0"), at(f->main, "a"));

    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    /* The move allocates nothing; a refused lookup leaves the moved
     * definition, so it goes back before the result is returned. */
    value.struct_size = sizeof(value);
    error = amark_node_get(at(f->main, "40"), AMARK_LINK_DESTINATION, &value);
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_get(at(f->main, "42"), AMARK_LINK_TITLE, &value);
    }
    if (amark_node_insert_after(at(f->main, "a"), at(f->main, "0")) != AMARK_ERROR_NONE)
    {
        return broken("the definition cannot go back", NULL, NULL);
    }
    /* After a refused lookup, the references resolve as before. */
    if ((error == AMARK_ERROR_OUT_OF_MEMORY) &&
        (!reads(at(f->main, "30"), AMARK_LINK_DESTINATION, "/u") ||
         !reads(at(f->main, "32"), AMARK_LINK_TITLE, "T")))
    {
        return broken("the references resolve otherwise after a refused lookup", NULL, NULL);
    }
    return error;
}

static const oom_case_t lookup_cases[] = {
    {"look up labels with dirty definitions", definitions_tree, lookup_dirty_definitions, false,
     true},
    {"look up through the values of references", NULL, lookup_through_references, false, true},
};

NAOS_TEST(lookups_of_the_index)
{
    sweep(lookup_cases, sizeof(lookup_cases) / sizeof(lookup_cases[0]));
}

/* ---- Empty free content without a line (14.3) ------------------------------------------ */

/* Dropping the spelling of a paragraph whose laid content is one empty line
 * gives empty free content without a line; the checker refuses a line on it,
 * which only the value "\n" of a CONTENT facet may have. */
NAOS_TEST(empty_free_content_has_no_line_unless_it_is_a_content_value)
{
    amark_document_t *document = NULL;
    amark_node_t *paragraph;
    amark_node_t *code;
    amark_tree_check_report_t report;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        new_document(main_source, AMARK_SOURCE_FINISHED, main_tree, &document));
    paragraph = at(document, "6");
    NAOS_REQUIRE_NOT_NULL(paragraph);
    NAOS_REQUIRE_NOT_NULL(paragraph->content);
    NAOS_CHECK_INT_EQ(AMARK_CONTENT_LAID, amark_block_content_state(paragraph));
    NAOS_CHECK_UINT_EQ(1, paragraph->content->line_count);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_discard_spelling(paragraph));
    NAOS_REQUIRE_NOT_NULL(paragraph->content);
    NAOS_CHECK_INT_EQ(AMARK_CONTENT_FREE, amark_block_content_state(paragraph));
    NAOS_CHECK_UINT_EQ(0, paragraph->content->length);
    NAOS_CHECK_UINT_EQ(0, paragraph->content->line_count);
    CHECK_TREE_OK(document);

    /* The same content with a line is a violation on a paragraph... */
    paragraph->content->line_count = 1;
    memset(&report, 0, sizeof(report));
    NAOS_CHECK_STR_EQ("content", amark_tree_violation_name(amark_tree_check(document, &report)));
    NAOS_CHECK_PTR_EQ(paragraph, report.node);
    NAOS_CHECK_STR_EQ("empty free content has a line on a type without a CONTENT facet",
                      report.message != NULL ? report.message : "");
    paragraph->content->line_count = 0;
    CHECK_TREE_OK(document);

    /* ... and the value "\n" on a code block. */
    code = at(document, "8");
    NAOS_REQUIRE_NOT_NULL(code);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_set_string(code, AMARK_CODE_BLOCK_LITERAL, "\n", 1));
    NAOS_REQUIRE_NOT_NULL(code->content);
    NAOS_CHECK_UINT_EQ(0, code->content->length);
    NAOS_CHECK_UINT_EQ(1, code->content->line_count);
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_SUITE(oom,
    NAOS_ADD_TEST(records),
    NAOS_ADD_TEST(sets),
    NAOS_ADD_TEST(sets_with_hooks),
    NAOS_ADD_TEST(drops_of_spelling),
    NAOS_ADD_TEST(line_operations),
    NAOS_ADD_TEST(replacements_of_content),
    NAOS_ADD_TEST(copies_and_imports),
    NAOS_ADD_TEST(lookups_of_the_index)
)

NAOS_SUITE(empty_free_content,
    NAOS_ADD_TEST(empty_free_content_has_no_line_unless_it_is_a_content_value)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(oom),
    NAOS_ADD_SUITE(empty_free_content)
)
