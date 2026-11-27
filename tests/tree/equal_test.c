/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The tests of Equality (syntax-tree): the semantic comparison (13.1-13.3).
 * Trees that differ only in their notation (styles, layout, spelling) are
 * equal by default and unequal with AMARK_EQUAL_STYLE; the links of a
 * reference form compare by the destination and the title they resolve to in
 * their own documents; the extension data compares with the equal function of
 * its type, or byte for byte (12.13); AMARK_EQUAL_NORMALIZE_TEXT joins the
 * adjacent text nodes and ignores the empty ones; a deep tree compares without
 * recursion; and a failing allocation of a getter makes the result false
 * without leaving anything behind.
 */
#include "tree/tree.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <naos/naos.h>

#include "test_allocator.h"
#include "tree_builder.h"

/* ---- Helpers ------------------------------------------------------------------- */

#define CHECK_TREE_OK(document) \
    NAOS_CHECK_STR_EQ("none", amark_tree_violation_name(amark_tree_check((document), NULL)))

/* A document of the built-in syntax set with source as its one finished source
 * (none when source is NULL) and the nodes of sexpr. */
static amark_document_t *build(const char *source, const char *sexpr)
{
    amark_document_t *document = NULL;
    amark_source_id_t id = 0;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    if (source != NULL)
    {
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                            amark_document_add_source(document, source, strlen(source),
                                                      AMARK_SOURCE_FINISHED, &id));
    }
    AMARK_CHECK_TREE_BUILD(document, NULL, sexpr);
    return document;
}

static amark_node_t *root_of(amark_document_t *document)
{
    return amark_document_root(document);
}

/* Child index of node (NULL when it has fewer children). */
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

/* Checks the comparison of the roots of two documents in both directions. */
#define CHECK_EQUAL(expected, a, b, flags) \
    do { \
        NAOS_CHECK_INT_EQ((expected), amark_node_equal((a), (b), (flags))); \
        NAOS_CHECK_INT_EQ((expected), amark_node_equal((b), (a), (flags))); \
    } while (0)

/* ---- Notation ----------------------------------------------------------------- */

/* "*a* b" spelled with its tokens, and the same meaning written as "_a_ b". */
static const char asterisk_source[] = "# *a* b\n";
static const char asterisk_tree[] =
    "(document :lines (0 1)\n"
    "  (heading :level 1 :style (:form atx) :lines (0 1)\n"
    "    :tokens ((marker 0 0 1) (space 0 1 2)) :content laid ((0 2 7))\n"
    "    (emph :style (:delimiter \"*\")\n"
    "          :spelling ((delimiter 0 1 :role 1) (delimiter 2 3 :role 2))\n"
    "      (text :spelling ((text 1 2 :facet literal)) \"a\"))\n"
    "    (text :spelling ((text 3 5 :facet literal)) \" b\")))";

static const char underscore_source[] = "_a_ b\n=\n";
static const char underscore_tree[] =
    "(document :lines (0 2)\n"
    "  (heading :level 1 :style (:form setext) :lines (0 2)\n"
    "    :tokens ((marker 1 0 1)) :content laid ((0 0 5))\n"
    "    (emph :style (:delimiter \"_\")\n"
    "          :spelling ((delimiter 0 1 :role 1) (delimiter 2 3 :role 2))\n"
    "      (text :spelling ((text 1 2 :facet literal)) \"a\"))\n"
    "    (text :spelling ((text 3 5 :facet literal)) \" b\")))";

/* The same meaning without any style, layout or spelling. */
static const char plain_tree[] =
    "(heading :level 1 (emph (text \"a\")) (text \" b\"))";

NAOS_TEST(trees_that_differ_only_in_notation_are_equal_without_style)
{
    amark_document_t *asterisk = build(asterisk_source, asterisk_tree);
    amark_document_t *underscore = build(underscore_source, underscore_tree);
    amark_document_t *plain = build(NULL, plain_tree);

    CHECK_EQUAL(true, root_of(asterisk), root_of(underscore), 0);
    CHECK_EQUAL(true, root_of(asterisk), root_of(plain), 0);
    CHECK_EQUAL(true, root_of(asterisk), root_of(plain), AMARK_EQUAL_NORMALIZE_TEXT);
    /* The styles differ (the form of the heading, the delimiter of emph), or are
     * unset on one side. */
    CHECK_EQUAL(false, root_of(asterisk), root_of(underscore), AMARK_EQUAL_STYLE);
    CHECK_EQUAL(false, root_of(asterisk), root_of(plain), AMARK_EQUAL_STYLE);
    /* Subtrees compare too: the emph nodes differ only in their delimiter. */
    CHECK_EQUAL(true, child_at(child_at(root_of(asterisk), 0), 0),
                child_at(child_at(root_of(underscore), 0), 0), 0);
    CHECK_EQUAL(false, child_at(child_at(root_of(asterisk), 0), 0),
                child_at(child_at(root_of(underscore), 0), 0), AMARK_EQUAL_STYLE);
    /* A node equals itself, with every flag. */
    CHECK_EQUAL(true, root_of(asterisk), root_of(asterisk),
                AMARK_EQUAL_STYLE | AMARK_EQUAL_NORMALIZE_TEXT);
    CHECK_TREE_OK(asterisk);
    CHECK_TREE_OK(underscore);
    amark_document_destroy(asterisk);
    amark_document_destroy(underscore);
    amark_document_destroy(plain);
}

NAOS_TEST(the_same_styles_are_equal_with_style)
{
    amark_document_t *a = build(asterisk_source, asterisk_tree);
    amark_document_t *b = build(NULL,
                                "(heading :level 1 :style (:form atx)"
                                " (emph :style (:delimiter \"*\") (text \"a\")) (text \" b\"))");

    /* The layout and the spelling are not styles. */
    CHECK_EQUAL(true, root_of(a), root_of(b), AMARK_EQUAL_STYLE);
    amark_document_destroy(a);
    amark_document_destroy(b);
}

NAOS_TEST(types_values_and_children_must_match)
{
    amark_document_t *base = build(NULL, plain_tree);
    amark_document_t *level = build(NULL, "(heading :level 2 (emph (text \"a\")) (text \" b\"))");
    amark_document_t *literal = build(NULL, "(heading :level 1 (emph (text \"a\")) (text \" c\"))");
    amark_document_t *type = build(NULL, "(heading :level 1 (strong (text \"a\")) (text \" b\"))");
    amark_document_t *fewer = build(NULL, "(heading :level 1 (emph (text \"a\")))");
    amark_document_t *more =
        build(NULL, "(heading :level 1 (emph (text \"a\")) (text \" b\") (softbreak))");
    amark_document_t *deeper =
        build(NULL, "(heading :level 1 (emph (text \"a\") (text \"\")) (text \" b\"))");
    amark_document_t *block = build(NULL, "(paragraph (emph (text \"a\")) (text \" b\"))");
    amark_document_t *two = build(NULL, "(heading :level 1 (emph (text \"a\")) (text \" b\"))"
                                        "(thematic_break)");

    CHECK_EQUAL(false, root_of(base), root_of(level), 0);
    CHECK_EQUAL(false, root_of(base), root_of(literal), 0);
    CHECK_EQUAL(false, root_of(base), root_of(type), 0);
    CHECK_EQUAL(false, root_of(base), root_of(fewer), 0);
    CHECK_EQUAL(false, root_of(base), root_of(more), 0);
    CHECK_EQUAL(false, root_of(base), root_of(deeper), 0);
    CHECK_EQUAL(false, root_of(base), root_of(block), 0);
    CHECK_EQUAL(false, root_of(base), root_of(two), 0);
    /* The first children of base and two are equal. */
    CHECK_EQUAL(true, child_at(root_of(base), 0), child_at(root_of(two), 0), 0);
    /* The ignored parts: the user pointer. */
    amark_node_set_user_data(child_at(root_of(base), 0), (void *)base);
    CHECK_EQUAL(true, child_at(root_of(base), 0), child_at(root_of(two), 0), 0);
    /* NULL arguments. */
    NAOS_CHECK_FALSE(amark_node_equal(NULL, root_of(base), 0));
    NAOS_CHECK_FALSE(amark_node_equal(root_of(base), NULL, 0));
    NAOS_CHECK_FALSE(amark_node_equal(NULL, NULL, 0));
    amark_document_destroy(base);
    amark_document_destroy(level);
    amark_document_destroy(literal);
    amark_document_destroy(type);
    amark_document_destroy(fewer);
    amark_document_destroy(more);
    amark_document_destroy(deeper);
    amark_document_destroy(block);
    amark_document_destroy(two);
}

NAOS_TEST(content_facets_and_derived_values_compare_by_value)
{
    amark_document_t *laid = build("```c\nx\n```\n",
                                   "(document :lines (0 3)"
                                   " (code_block :info \"c\" :style (:form fenced) :lines (0 3)"
                                   "  :tokens ((marker 0 0 3) (text 0 3 4 :facet info)"
                                   "           (marker 2 0 3))"
                                   "  :content laid ((1 0 1))))");
    amark_document_t *free_ = build(NULL, "(code_block :info \"c\" \"x\\n\")");
    amark_document_t *other = build(NULL, "(code_block :info \"c\" \"y\\n\")");

    CHECK_EQUAL(true, root_of(laid), root_of(free_), 0);
    CHECK_EQUAL(false, root_of(laid), root_of(other), 0);
    amark_document_destroy(laid);
    amark_document_destroy(free_);
    amark_document_destroy(other);
}

/* ---- Reference links ---------------------------------------------------------- */

static const char reference_tree[] =
    "(paragraph (link :label \"x\" :style (:form full) (text \"a\")))"
    "(link_definition :label \"x\" :destination \"/u\" :title \"t\")";

NAOS_TEST(reference_links_compare_by_their_resolved_destination)
{
    amark_document_t *full = build(NULL, reference_tree);
    amark_document_t *inline_ =
        build(NULL, "(paragraph (link :destination \"/u\" :title \"t\" :style (:form inline)"
                    " (text \"a\")))"
                    "(link_definition :label \"x\" :destination \"/u\" :title \"t\")");
    amark_document_t *other_label =
        build(NULL, "(paragraph (link :label \"y\" :style (:form full) (text \"a\")))"
                    "(link_definition :label \"y\" :destination \"/u\" :title \"t\")");
    amark_document_t *other_destination =
        build(NULL, "(paragraph (link :label \"x\" :style (:form full) (text \"a\")))"
                    "(link_definition :label \"x\" :destination \"/v\" :title \"t\")");
    amark_document_t *other_title =
        build(NULL, "(paragraph (link :label \"x\" :style (:form full) (text \"a\")))"
                    "(link_definition :label \"x\" :destination \"/u\" :title \"T\")");
    amark_document_t *unresolved =
        build(NULL, "(paragraph (link :label \"x\" :style (:form full) (text \"a\")))"
                    "(link_definition :label \"z\" :destination \"/u\" :title \"t\")");
    amark_node_t *link = child_at(child_at(root_of(full), 0), 0);

    /* The paragraphs: each link resolves in its own document. */
    CHECK_EQUAL(true, child_at(root_of(full), 0), child_at(root_of(inline_), 0), 0);
    CHECK_EQUAL(true, child_at(root_of(full), 0), child_at(root_of(other_label), 0), 0);
    CHECK_EQUAL(false, child_at(root_of(full), 0), child_at(root_of(other_destination), 0), 0);
    CHECK_EQUAL(false, child_at(root_of(full), 0), child_at(root_of(other_title), 0), 0);
    CHECK_EQUAL(false, child_at(root_of(full), 0), child_at(root_of(unresolved), 0), 0);
    /* With the styles: the form and the label count. */
    CHECK_EQUAL(false, child_at(root_of(full), 0), child_at(root_of(inline_), 0),
                AMARK_EQUAL_STYLE);
    CHECK_EQUAL(false, child_at(root_of(full), 0), child_at(root_of(other_label), 0),
                AMARK_EQUAL_STYLE);
    /* The whole documents: the definitions differ in their label. */
    CHECK_EQUAL(true, root_of(full), root_of(inline_), 0);
    CHECK_EQUAL(false, root_of(full), root_of(other_label), 0);

    /* A copy of the link resolves in the same document, detached or not. */
    {
        amark_node_t *copy = NULL;

        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_copy(link, &copy));
        CHECK_EQUAL(true, link, copy, AMARK_EQUAL_STYLE);
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(amark_node_parent(link),
                                                                      copy));
        CHECK_EQUAL(true, link, copy, AMARK_EQUAL_STYLE);
    }
    CHECK_TREE_OK(full);
    amark_document_destroy(full);
    amark_document_destroy(inline_);
    amark_document_destroy(other_label);
    amark_document_destroy(other_destination);
    amark_document_destroy(other_title);
    amark_document_destroy(unresolved);
}

/* ---- Text normalization -------------------------------------------------------- */

NAOS_TEST(normalization_joins_adjacent_text_and_ignores_empty_text)
{
    amark_document_t *one = build(NULL, "(paragraph (text \"ab\") (emph (text \"c\")) (text \"d\"))");
    amark_document_t *split =
        build(NULL, "(paragraph (text \"a\") (text \"\") (text \"b\") (emph (text \"\") (text \"c\"))"
                    " (text \"d\") (text \"\"))");
    amark_document_t *empty_first =
        build(NULL, "(paragraph (text \"\") (text \"\") (text \"ab\") (emph (text \"c\") (text \"\"))"
                    " (text \"d\"))");
    amark_document_t *moved =
        build(NULL, "(paragraph (text \"a\") (emph (text \"c\")) (text \"bd\"))");
    amark_document_t *longer =
        build(NULL, "(paragraph (text \"a\") (text \"bx\") (emph (text \"c\")) (text \"d\"))");
    amark_document_t *shorter =
        build(NULL, "(paragraph (text \"a\") (emph (text \"c\")) (text \"d\"))");
    amark_document_t *without_emph = build(NULL, "(paragraph (text \"ab\") (text \"cd\"))");
    amark_document_t *empty_emph =
        build(NULL, "(paragraph (text \"ab\") (emph (text \"c\")) (text \"d\") (emph))");

    CHECK_EQUAL(false, root_of(one), root_of(split), 0);
    CHECK_EQUAL(false, root_of(one), root_of(empty_first), 0);
    CHECK_EQUAL(true, root_of(one), root_of(split), AMARK_EQUAL_NORMALIZE_TEXT);
    CHECK_EQUAL(true, root_of(one), root_of(empty_first), AMARK_EQUAL_NORMALIZE_TEXT);
    CHECK_EQUAL(true, root_of(split), root_of(empty_first),
                AMARK_EQUAL_NORMALIZE_TEXT | AMARK_EQUAL_STYLE);
    /* The runs are separated by the other nodes. */
    CHECK_EQUAL(false, root_of(one), root_of(moved), AMARK_EQUAL_NORMALIZE_TEXT);
    CHECK_EQUAL(false, root_of(one), root_of(longer), AMARK_EQUAL_NORMALIZE_TEXT);
    CHECK_EQUAL(false, root_of(one), root_of(shorter), AMARK_EQUAL_NORMALIZE_TEXT);
    CHECK_EQUAL(false, root_of(one), root_of(without_emph), AMARK_EQUAL_NORMALIZE_TEXT);
    /* Only text nodes are ignored when empty. */
    CHECK_EQUAL(false, root_of(one), root_of(empty_emph), AMARK_EQUAL_NORMALIZE_TEXT);
    /* A text node as the root of the comparison. */
    CHECK_EQUAL(true, child_at(child_at(root_of(one), 0), 0),
                child_at(child_at(root_of(empty_first), 0), 2), AMARK_EQUAL_NORMALIZE_TEXT);
    CHECK_EQUAL(false, child_at(child_at(root_of(one), 0), 0),
                child_at(child_at(root_of(split), 0), 0), AMARK_EQUAL_NORMALIZE_TEXT);
    amark_document_destroy(one);
    amark_document_destroy(split);
    amark_document_destroy(empty_first);
    amark_document_destroy(moved);
    amark_document_destroy(longer);
    amark_document_destroy(shorter);
    amark_document_destroy(without_emph);
    amark_document_destroy(empty_emph);
}

NAOS_TEST(normalization_reads_derived_literals)
{
    amark_document_t *spelled = build("ab\n",
                                      "(document :lines (0 1)"
                                      " (paragraph :lines (0 1) :content laid ((0 0 2))"
                                      "  (text :spelling ((text 0 1 :facet literal)) \"a\")"
                                      "  (text :spelling ((text 1 2 :facet literal)) \"b\")))");
    amark_document_t *one = build(NULL, "(paragraph (text \"ab\"))");

    CHECK_EQUAL(false, root_of(spelled), root_of(one), 0);
    CHECK_EQUAL(true, root_of(spelled), root_of(one), AMARK_EQUAL_NORMALIZE_TEXT);
    amark_document_destroy(spelled);
    amark_document_destroy(one);
}

/* ---- Extension data ------------------------------------------------------------ */

typedef struct note_data
{
    int value;
    int scratch; /* ignored by note_equal */
} note_data_t;

static size_t note_equal_calls;

static bool note_equal(const amark_node_t *a, const void *a_data, const amark_node_t *b,
                       const void *b_data)
{
    note_equal_calls++;
    if (amark_node_data((amark_node_t *)(uintptr_t)a) != a_data ||
        amark_node_data((amark_node_t *)(uintptr_t)b) != b_data)
    {
        return false;
    }
    return ((const note_data_t *)a_data)->value == ((const note_data_t *)b_data)->value;
}

static const amark_node_type_desc_t note_type = {
    sizeof(amark_node_type_desc_t), "test.note", AMARK_NODE_LEVEL_BLOCK, AMARK_CONTENT_BLOCK,
    AMARK_CONTENT_BLOCK, 0, NULL, 0, sizeof(note_data_t),
    NULL, NULL, NULL, NULL, note_equal, NULL, NULL};

/* A type without an equal function: its data compares byte for byte. */
static const amark_node_type_desc_t plain_type = {
    sizeof(amark_node_type_desc_t), "test.plain", AMARK_NODE_LEVEL_BLOCK, AMARK_CONTENT_BLOCK,
    AMARK_CONTENT_BLOCK, 0, NULL, 0, 8, NULL, NULL, NULL, NULL, NULL, NULL, NULL};

static const amark_node_type_desc_t *const note_types[] = {&note_type, &plain_type};
static const amark_extension_t note_ext = {
    sizeof(amark_extension_t), "test.notes", note_types, 2, NULL, 0, NULL};

/* A second extension frozen before test.notes, so that the types of the
 * notes get other identifiers in the second document. */
static const amark_node_type_desc_t first_type = {
    sizeof(amark_node_type_desc_t), "test.first", AMARK_NODE_LEVEL_BLOCK, AMARK_CONTENT_BLOCK,
    AMARK_CONTENT_BLOCK, 0, NULL, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
static const amark_node_type_desc_t *const first_types[] = {&first_type};
static const amark_extension_t first_ext = {
    sizeof(amark_extension_t), "test.first", first_types, 1, NULL, 0, NULL};

static amark_document_t *new_note_document(bool shifted, amark_node_type_t *note,
                                           amark_node_type_t *plain)
{
    amark_syntax_set_t *set = NULL;
    amark_document_t *document = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_create(&set));
    if (shifted)
    {
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &first_ext));
    }
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &note_ext));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_freeze(set));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(set, &document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_take_syntax_set(document, set));
    NAOS_REQUIRE_TRUE(amark_syntax_set_find_node_type(set, "test.note", note));
    NAOS_REQUIRE_TRUE(amark_syntax_set_find_node_type(set, "test.plain", plain));
    return document;
}

/* note(value) > plain(bytes) > paragraph(text) under the document node. */
static amark_node_t *add_notes(amark_document_t *document, amark_node_type_t note,
                               amark_node_type_t plain, int value, int scratch,
                               const char *bytes)
{
    amark_node_t *a = NULL;
    amark_node_t *b = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, note, &a));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, plain, &b));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(amark_document_root(document),
                                                                  a));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(a, b));
    ((note_data_t *)amark_node_data(a))->value = value;
    ((note_data_t *)amark_node_data(a))->scratch = scratch;
    memcpy(amark_node_data(b), bytes, 8);
    AMARK_CHECK_TREE_BUILD(document, b, "(paragraph (text \"x\"))");
    return a;
}

NAOS_TEST(extension_data_compares_with_the_equal_function_of_its_type)
{
    amark_node_type_t note1 = AMARK_NODE_NONE;
    amark_node_type_t plain1 = AMARK_NODE_NONE;
    amark_node_type_t note2 = AMARK_NODE_NONE;
    amark_node_type_t plain2 = AMARK_NODE_NONE;
    amark_document_t *d1 = new_note_document(false, &note1, &plain1);
    amark_document_t *d2 = new_note_document(true, &note2, &plain2);
    amark_node_t *a = add_notes(d1, note1, plain1, 1, 10, "abcdefgh");
    amark_node_t *same = add_notes(d2, note2, plain2, 1, 20, "abcdefgh");
    amark_node_t *other_value = add_notes(d2, note2, plain2, 2, 10, "abcdefgh");
    amark_node_t *other_bytes = add_notes(d2, note2, plain2, 1, 10, "abcdefgX");

    /* The types are the same by name, with other identifiers. */
    NAOS_REQUIRE_TRUE(note1 != note2);
    note_equal_calls = 0;
    /* scratch is ignored by the equal function. */
    CHECK_EQUAL(true, a, same, 0);
    NAOS_CHECK_UINT_EQ(2, note_equal_calls);
    CHECK_EQUAL(false, a, other_value, 0);
    /* The data of plain compares byte for byte. */
    CHECK_EQUAL(false, a, other_bytes, 0);
    CHECK_EQUAL(true, amark_node_first_child(a), amark_node_first_child(same), 0);
    CHECK_EQUAL(false, amark_node_first_child(a), amark_node_first_child(other_bytes), 0);
    /* A note and a plain node have different types. */
    CHECK_EQUAL(false, a, amark_node_first_child(a), 0);
    amark_document_destroy(d1);
    amark_document_destroy(d2);
}

/* ---- Deep trees ---------------------------------------------------------------- */

#define DEEP_LEVELS 100000u

/* A chain of DEEP_LEVELS block quotes around a paragraph with literal,
 * built from the bottom up. */
static amark_document_t *deep_document(const char *literal)
{
    amark_document_t *document = NULL;
    amark_node_t *paragraph = NULL;
    amark_node_t *text = NULL;
    amark_node_t *top;
    uint32_t i;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(document, AMARK_NODE_PARAGRAPH, &paragraph));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, AMARK_NODE_TEXT, &text));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_record_string(text, AMARK_TEXT_LITERAL, literal,
                                                 strlen(literal)));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(paragraph, text));
    top = paragraph;
    for (i = 0; i < DEEP_LEVELS; i++)
    {
        amark_node_t *quote = NULL;

        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                            amark_node_create(document, AMARK_NODE_BLOCK_QUOTE, &quote));
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(quote, top));
        top = quote;
    }
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_append_child(amark_document_root(document), top));
    return document;
}

NAOS_TEST(deep_trees_compare_without_recursion)
{
    amark_document_t *a = deep_document("x");
    amark_document_t *b = deep_document("x");
    amark_document_t *c = deep_document("y");

    CHECK_EQUAL(true, root_of(a), root_of(b), 0);
    CHECK_EQUAL(true, root_of(a), root_of(b), AMARK_EQUAL_STYLE | AMARK_EQUAL_NORMALIZE_TEXT);
    CHECK_EQUAL(false, root_of(a), root_of(c), 0);
    amark_document_destroy(a);
    amark_document_destroy(b);
    amark_document_destroy(c);
}

/* ---- Allocation failures --------------------------------------------------------- */

/* A full reference whose label and literals are spelled by tokens: reading
 * them derives values (allocated caches), and resolving the reference
 * updates the definition index. */
static const char spelled_reference_source[] = "[t][Foo] [foo]\n\n[foo]: /u \"T\"\n";
static const char spelled_reference_tree[] =
    "(document :lines (0 3)\n"
    "  (paragraph :lines (0 1) :content laid ((0 0 14))\n"
    "    (link :style (:form full) :spelling ((text 4 7 :facet label))\n"
    "      (text :spelling ((text 1 2 :facet literal)) \"t\"))\n"
    "    (text :spelling ((text 8 9 :facet literal)) \" \")\n"
    "    (link :label \"foo\" :style (:form shortcut)\n"
    "      (text :spelling ((text 10 13 :facet literal)) \"foo\")))\n"
    "  (link_definition :label \"foo\" :destination \"/u\" :title \"T\"))\n";
static const char inline_reference_tree[] =
    "(paragraph (link :destination \"/u\" :title \"T\" (text \"t\")) (text \" \")"
    " (link :destination \"/u\" :title \"T\" (text \"foo\")))"
    "(link_definition :label \"foo\" :destination \"/u\" :title \"T\")";

static amark_test_allocator_t oom_allocator;

NAOS_TEST(an_allocation_failure_of_a_getter_makes_the_result_false)
{
    size_t k;
    bool done = false;

    for (k = 1; !done && k < 1000; k++)
    {
        amark_document_t *spelled;
        amark_document_t *plain;
        size_t built_calls;
        size_t failures;
        bool result;
        bool again;
        amark_tree_violation_t violation;

        amark_test_allocator_init(&oom_allocator);
        amark_allocator_set(amark_test_allocator_activate(&oom_allocator));
        spelled = build(spelled_reference_source, spelled_reference_tree);
        plain = build(NULL, inline_reference_tree);
        built_calls = oom_allocator.calls;
        oom_allocator.fail_at = built_calls + k;
        result = amark_node_equal(root_of(spelled), root_of(plain), AMARK_EQUAL_NORMALIZE_TEXT);
        failures = oom_allocator.failures;
        oom_allocator.fail_at = 0;
        violation = amark_tree_check(spelled, NULL);
        /* Without a failure, the comparison succeeds after a failed one. */
        again = amark_node_equal(root_of(spelled), root_of(plain), AMARK_EQUAL_NORMALIZE_TEXT);
        amark_document_destroy(spelled);
        amark_document_destroy(plain);
        amark_allocator_set(NULL);

        NAOS_CHECK_UINT_EQ(0, oom_allocator.live_blocks);
        NAOS_CHECK_STR_EQ("none", amark_tree_violation_name(violation));
        NAOS_CHECK_TRUE(again);
        /* The comparison allocates (the first call, at least). */
        if (k == 1)
        {
            NAOS_CHECK_UINT_EQ(1, failures);
        }
        if (failures == 0)
        {
            NAOS_CHECK_TRUE(result);
            done = true;
        }
        else
        {
            NAOS_CHECK_FALSE(result);
        }
    }
    NAOS_CHECK_TRUE(done);
}

NAOS_SUITE(equal,
    NAOS_ADD_TEST(trees_that_differ_only_in_notation_are_equal_without_style),
    NAOS_ADD_TEST(the_same_styles_are_equal_with_style),
    NAOS_ADD_TEST(types_values_and_children_must_match),
    NAOS_ADD_TEST(content_facets_and_derived_values_compare_by_value),
    NAOS_ADD_TEST(reference_links_compare_by_their_resolved_destination),
    NAOS_ADD_TEST(normalization_joins_adjacent_text_and_ignores_empty_text),
    NAOS_ADD_TEST(normalization_reads_derived_literals),
    NAOS_ADD_TEST(extension_data_compares_with_the_equal_function_of_its_type),
    NAOS_ADD_TEST(deep_trees_compare_without_recursion),
    NAOS_ADD_TEST(an_allocation_failure_of_a_getter_makes_the_result_false)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(equal)
)
