/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The check of the tree invariants (src/tree/tree_check.c), the containment rule
 * on descriptors (amark_node_allows_child, src/tree/structure_edit.c) and the
 * internal counters of a document.
 *
 * Suite tree_check (4.5, 7.5): valid trees pass (an empty document, a tree with
 * detached subtrees, sorted tokens within their sources, edit records closed over
 * the ancestors, a 100 000 deep chain); each invariant broken directly through the
 * internal structs is reported with its own reason (sibling and parent links, a
 * node without a parent that has siblings, containment, AMARK_EDIT_DESCENDANTS,
 * token order, range and source, ring symmetry, a node missing from the ring, a
 * node of the ring outside every subtree, nodes of another document); a NULL
 * document is an invalid argument. Each test repairs what it broke before the
 * document is destroyed.
 * Suite allows_child (4.5): amark_node_allows_child agrees with the public
 * amark_node_can_contain on every pair of the built-in types and of extension
 * types, one of which overrides the rule with can_contain.
 * Suite counters: a new document has both counters at 0.
 */
#include "tree/tree.h"

#include <amark/allocator.h>
#include <amark/document.h>
#include <amark/error.h>
#include <amark/extension.h>
#include <amark/node.h>

#include "tree/syntax_set.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <naos/naos.h>

/* Links child, a detached node, as the last child of parent. Records nothing. */
static void link_last(amark_node_t *parent, amark_node_t *child)
{
    child->parent = parent;
    child->prev = parent->last_child;
    child->next = NULL;
    if (parent->last_child != NULL)
    {
        parent->last_child->next = child;
    }
    else
    {
        parent->first_child = child;
    }

    parent->last_child = child;
}

/* Unlinks child, the last child of its parent. */
static void unlink_last(amark_node_t *child)
{
    amark_node_t *parent = child->parent;

    parent->last_child = child->prev;
    if (child->prev != NULL)
    {
        child->prev->next = NULL;
    }
    else
    {
        parent->first_child = NULL;
    }

    child->parent = NULL;
    child->prev = NULL;
}

/* Gives node a copy of the count tokens at tokens (node has none before). */
static void set_tokens(amark_node_t *node, const amark_token_record_t *tokens, size_t count)
{
    node->tokens = amark_malloc(count * sizeof(*tokens));
    NAOS_REQUIRE_NOT_NULL(node->tokens);
    memcpy(node->tokens, tokens, count * sizeof(*tokens));
    node->token_count = count;
    node->token_capacity = count;
}

/* Expects the check of document to fail with reason. */
#define CHECK_VIOLATION(document, expected_reason) \
    do { \
        const char *check_reason_ = NULL; \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_STATE, \
                          amark_tree_check((document), &check_reason_)); \
        NAOS_REQUIRE_NOT_NULL(check_reason_); \
        NAOS_CHECK_STR_EQ((expected_reason), check_reason_); \
    } while (0)

/* Expects the check of document to pass. */
#define CHECK_VALID(document) \
    do { \
        const char *check_reason_ = "unset"; \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_tree_check((document), &check_reason_)); \
        NAOS_CHECK_NULL(check_reason_); \
    } while (0)

#define REASON_SIBLING "the sibling links are not symmetric"
#define REASON_PARENT_CHILD "the parent and child links are not symmetric"
#define REASON_CONTAINMENT "a child violates the containment rule of its parent"
#define REASON_DESCENDANTS \
    "an ancestor of a node with edit records lacks AMARK_EDIT_DESCENDANTS"
#define REASON_UNSORTED "the tokens of a node are not sorted"
#define REASON_RANGE "a token is out of the range of its source"
#define REASON_SOURCE "a token refers to a source that is not registered"
#define REASON_RING "the ring links are not symmetric"
#define REASON_NOT_IN_RING "a node of the tree is not in the ring"
#define REASON_UNREACHABLE "a node of the ring is not in the subtree of a node without a parent"
#define REASON_RING_DOCUMENT "a node of the ring belongs to another document"
#define REASON_TREE_DOCUMENT "a node of the tree belongs to another document"

/*
 * root > quote > paragraph > (first, emph > second), and a detached subtree
 * detached > detached_text, made in this order (the order of the ring), with
 * tracking off. Sources: 0 is "hello world" (11 bytes), 1 is "second".
 */
typedef struct fixture
{
    amark_document_t *document;
    amark_node_t *root;
    amark_node_t *quote;
    amark_node_t *paragraph;
    amark_node_t *first;
    amark_node_t *emph;
    amark_node_t *second;
    amark_node_t *detached;
    amark_node_t *detached_text;
} fixture_t;

static amark_node_t *create(amark_document_t *document, amark_node_type_t type)
{
    amark_node_t *node = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, type, &node));
    return node;
}

static void build(fixture_t *f)
{
    amark_source_id_t source;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &f->document));
    f->root = amark_document_root(f->document);
    f->quote = create(f->document, AMARK_NODE_BLOCK_QUOTE);
    f->paragraph = create(f->document, AMARK_NODE_PARAGRAPH);
    f->first = create(f->document, AMARK_NODE_TEXT);
    f->emph = create(f->document, AMARK_NODE_EMPH);
    f->second = create(f->document, AMARK_NODE_TEXT);
    f->detached = create(f->document, AMARK_NODE_PARAGRAPH);
    f->detached_text = create(f->document, AMARK_NODE_TEXT);
    link_last(f->root, f->quote);
    link_last(f->quote, f->paragraph);
    link_last(f->paragraph, f->first);
    link_last(f->paragraph, f->emph);
    link_last(f->emph, f->second);
    link_last(f->detached, f->detached_text);

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_add_source(f->document, "hello world", 11, &source));
    NAOS_REQUIRE_UINT_EQ(0, source);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_add_source(f->document, "second", 6, &source));
    NAOS_REQUIRE_UINT_EQ(1, source);
}

static amark_token_record_t token(uint32_t source, size_t offset, size_t length,
                                  uint8_t tab_column_offset)
{
    amark_token_record_t record;

    memset(&record, 0, sizeof(record));
    record.source = source;
    record.offset = offset;
    record.length = length;
    record.tab_column_offset = tab_column_offset;
    return record;
}

/* --- Valid trees ----------------------------------------------------------------- */

NAOS_TEST(an_empty_document_is_valid)
{
    amark_document_t *document = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    CHECK_VALID(document);
    amark_document_destroy(document);
}

NAOS_TEST(a_tree_with_detached_subtrees_is_valid)
{
    fixture_t f;
    amark_node_t *lone;

    build(&f);
    CHECK_VALID(f.document);

    /* A lone detached node too, and the reason may be omitted. */
    lone = create(f.document, AMARK_NODE_THEMATIC_BREAK);
    NAOS_CHECK_NOT_NULL(lone);
    CHECK_VALID(f.document);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_tree_check(f.document, NULL));

    amark_document_destroy(f.document);
}

NAOS_TEST(sorted_tokens_within_their_sources_are_valid)
{
    fixture_t f;
    amark_token_record_t tokens[6];

    build(&f);

    /* Sorted by source, then offset, then tab_column_offset; equal keys are
     * allowed (the order of addition); a token may end at the end of its source
     * or be empty there. */
    tokens[0] = token(0, 0, 5, 0);
    tokens[1] = token(0, 5, 1, 0);
    tokens[2] = token(0, 5, 1, 2);
    tokens[3] = token(0, 5, 1, 2);
    tokens[4] = token(0, 6, 5, 0);
    tokens[5] = token(1, 6, 0, 0);
    set_tokens(f.first, tokens, 6);
    tokens[0] = token(1, 0, 6, 0);
    set_tokens(f.detached_text, tokens, 1);
    CHECK_VALID(f.document);

    amark_document_destroy(f.document);
}

NAOS_TEST(edit_records_closed_over_the_ancestors_are_valid)
{
    fixture_t f;
    amark_node_t *created;

    build(&f);
    amark_document_set_edit_tracking(f.document, true);

    amark_edit_mark(f.second, AMARK_EDIT_CONTENT);
    amark_edit_mark(f.detached_text, AMARK_EDIT_CONTENT);
    CHECK_VALID(f.document);

    /* A detached node with records alone, and a node with AMARK_EDIT_DESCENDANTS
     * under ancestors that have it. */
    created = create(f.document, AMARK_NODE_TEXT);
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_CREATED, created->edits);
    CHECK_VALID(f.document);

    amark_document_destroy(f.document);
}

NAOS_TEST(a_deep_chain_is_checked_without_recursion)
{
    amark_document_t *document = NULL;
    amark_node_t *parent;
    size_t i;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    parent = amark_document_root(document);
    for (i = 0; i < 100000; i++)
    {
        amark_node_t *quote = create(document, AMARK_NODE_BLOCK_QUOTE);

        link_last(parent, quote);
        parent = quote;
    }

    CHECK_VALID(document);
    amark_document_destroy(document);
}

NAOS_TEST(a_null_document_is_an_invalid_argument)
{
    const char *reason = "unset";

    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_tree_check(NULL, &reason));
    NAOS_CHECK_NULL(reason);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_tree_check(NULL, NULL));
}

/* --- Links ----------------------------------------------------------------------- */

NAOS_TEST(asymmetric_sibling_links_are_detected)
{
    fixture_t f;

    build(&f);

    /* first->next is emph, but emph->prev does not link back. */
    f.emph->prev = NULL;
    CHECK_VIOLATION(f.document, REASON_SIBLING);
    f.emph->prev = f.first;

    /* emph->prev is first, but first->next does not link back. */
    f.first->next = f.second;
    CHECK_VIOLATION(f.document, REASON_SIBLING);
    f.first->next = f.emph;

    CHECK_VALID(f.document);
    amark_document_destroy(f.document);
}

NAOS_TEST(a_node_without_a_parent_with_siblings_is_detected)
{
    fixture_t f;
    amark_node_t *lone;

    build(&f);
    lone = create(f.document, AMARK_NODE_PARAGRAPH);

    f.detached->next = lone;
    CHECK_VIOLATION(f.document, REASON_SIBLING);
    f.detached->next = NULL;

    amark_document_destroy(f.document);
}

NAOS_TEST(a_wrong_first_or_last_child_is_detected)
{
    fixture_t f;

    build(&f);

    f.paragraph->first_child = f.emph;
    CHECK_VIOLATION(f.document, REASON_PARENT_CHILD);
    f.paragraph->first_child = f.first;

    f.paragraph->last_child = f.first;
    CHECK_VIOLATION(f.document, REASON_PARENT_CHILD);
    f.paragraph->last_child = f.emph;

    /* Only one of the ends set. */
    f.emph->first_child = NULL;
    CHECK_VIOLATION(f.document, REASON_PARENT_CHILD);
    f.emph->first_child = f.second;

    /* In a detached subtree too. */
    f.detached->last_child = NULL;
    CHECK_VIOLATION(f.document, REASON_PARENT_CHILD);
    f.detached->last_child = f.detached_text;

    CHECK_VALID(f.document);
    amark_document_destroy(f.document);
}

NAOS_TEST(a_wrong_parent_is_detected)
{
    fixture_t f;

    build(&f);

    f.first->parent = f.quote;
    CHECK_VIOLATION(f.document, REASON_PARENT_CHILD);
    f.first->parent = f.paragraph;

    f.detached_text->parent = f.paragraph;
    CHECK_VIOLATION(f.document, REASON_PARENT_CHILD);
    f.detached_text->parent = f.detached;

    CHECK_VALID(f.document);
    amark_document_destroy(f.document);
}

/* --- Containment and edit records ------------------------------------------------ */

NAOS_TEST(a_containment_violation_is_detected)
{
    fixture_t f;
    amark_node_t *text;
    amark_node_t *list;

    build(&f);

    /* A text directly under the root. */
    text = create(f.document, AMARK_NODE_TEXT);
    link_last(f.root, text);
    CHECK_VIOLATION(f.document, REASON_CONTAINMENT);
    unlink_last(text);

    /* A paragraph in a list, in a detached subtree. */
    list = create(f.document, AMARK_NODE_LIST);
    link_last(list, create(f.document, AMARK_NODE_PARAGRAPH));
    CHECK_VIOLATION(f.document, REASON_CONTAINMENT);
    unlink_last(list->first_child);

    CHECK_VALID(f.document);
    amark_document_destroy(f.document);
}

NAOS_TEST(a_missing_descendants_record_is_detected)
{
    fixture_t f;
    amark_node_t *created;

    build(&f);
    amark_document_set_edit_tracking(f.document, true);
    amark_edit_mark(f.second, AMARK_EDIT_CONTENT);
    CHECK_VALID(f.document);

    /* An ancestor of a node with AMARK_EDIT_DESCENDANTS lacks it. */
    f.quote->edits = 0;
    CHECK_VIOLATION(f.document, REASON_DESCENDANTS);
    f.quote->edits = AMARK_EDIT_DESCENDANTS;

    /* The root lacks it. */
    f.root->edits = AMARK_EDIT_CONTENT;
    CHECK_VIOLATION(f.document, REASON_DESCENDANTS);
    f.root->edits = AMARK_EDIT_DESCENDANTS;

    /* A created node linked without the propagation, in a detached subtree. */
    created = create(f.document, AMARK_NODE_TEXT);
    link_last(f.detached, created);
    CHECK_VIOLATION(f.document, REASON_DESCENDANTS);
    f.detached->edits = AMARK_EDIT_DESCENDANTS;

    CHECK_VALID(f.document);
    amark_document_destroy(f.document);
}

/* --- Tokens ---------------------------------------------------------------------- */

NAOS_TEST(unsorted_tokens_are_detected)
{
    fixture_t f;
    amark_token_record_t tokens[2];

    build(&f);
    tokens[0] = token(0, 0, 0, 0);
    tokens[1] = token(0, 0, 0, 0);
    set_tokens(f.second, tokens, 2);
    CHECK_VALID(f.document);

    /* By offset. */
    f.second->tokens[0] = token(0, 5, 1, 0);
    f.second->tokens[1] = token(0, 2, 1, 0);
    CHECK_VIOLATION(f.document, REASON_UNSORTED);

    /* By source, before the offset. */
    f.second->tokens[0] = token(1, 0, 1, 0);
    f.second->tokens[1] = token(0, 5, 1, 0);
    CHECK_VIOLATION(f.document, REASON_UNSORTED);

    /* By tab_column_offset. */
    f.second->tokens[0] = token(0, 3, 1, 2);
    f.second->tokens[1] = token(0, 3, 1, 1);
    CHECK_VIOLATION(f.document, REASON_UNSORTED);

    f.second->tokens[1] = token(0, 3, 1, 2);
    CHECK_VALID(f.document);
    amark_document_destroy(f.document);
}

NAOS_TEST(a_token_out_of_its_source_is_detected)
{
    fixture_t f;
    amark_token_record_t tokens[1];

    build(&f);
    tokens[0] = token(0, 11, 0, 0);
    set_tokens(f.detached_text, tokens, 1);
    CHECK_VALID(f.document);

    /* Past the end of the source. */
    f.detached_text->tokens[0] = token(0, 10, 2, 0);
    CHECK_VIOLATION(f.document, REASON_RANGE);
    f.detached_text->tokens[0] = token(0, 12, 0, 0);
    CHECK_VIOLATION(f.document, REASON_RANGE);
    f.detached_text->tokens[0] = token(1, 0, 7, 0);
    CHECK_VIOLATION(f.document, REASON_RANGE);

    /* offset + length overflows. */
    f.detached_text->tokens[0] = token(0, SIZE_MAX, 2, 0);
    CHECK_VIOLATION(f.document, REASON_RANGE);
    f.detached_text->tokens[0] = token(0, 1, SIZE_MAX, 0);
    CHECK_VIOLATION(f.document, REASON_RANGE);

    /* A source that is not registered. */
    f.detached_text->tokens[0] = token(2, 0, 0, 0);
    CHECK_VIOLATION(f.document, REASON_SOURCE);
    f.detached_text->tokens[0] = token(UINT32_MAX, 0, 0, 0);
    CHECK_VIOLATION(f.document, REASON_SOURCE);

    amark_document_destroy(f.document);
}

/* --- The ring and the documents -------------------------------------------------- */

NAOS_TEST(asymmetric_ring_links_are_detected)
{
    fixture_t f;
    amark_node_t *saved;

    build(&f);

    /* paragraph->ring_next is first, whose ring_prev does not link back. */
    saved = f.first->ring_prev;
    f.first->ring_prev = f.root;
    CHECK_VIOLATION(f.document, REASON_RING);
    f.first->ring_prev = saved;

    /* A NULL ring link. */
    saved = f.emph->ring_next;
    f.emph->ring_next = NULL;
    CHECK_VIOLATION(f.document, REASON_RING);
    f.emph->ring_next = saved;

    CHECK_VALID(f.document);
    amark_document_destroy(f.document);
}

NAOS_TEST(a_node_missing_from_the_ring_is_detected)
{
    fixture_t f;
    amark_node_t *ring_prev;
    amark_node_t *ring_next;

    build(&f);

    /* Splice first out of the ring, keeping its own links. */
    ring_prev = f.first->ring_prev;
    ring_next = f.first->ring_next;
    ring_prev->ring_next = ring_next;
    ring_next->ring_prev = ring_prev;
    CHECK_VIOLATION(f.document, REASON_NOT_IN_RING);

    /* With its own links cleared, as the ring removal does. */
    f.first->ring_prev = NULL;
    f.first->ring_next = NULL;
    CHECK_VIOLATION(f.document, REASON_NOT_IN_RING);

    f.first->ring_prev = ring_prev;
    f.first->ring_next = ring_next;
    ring_prev->ring_next = f.first;
    ring_next->ring_prev = f.first;
    CHECK_VALID(f.document);

    /* The same in a detached subtree. */
    ring_prev = f.detached_text->ring_prev;
    ring_next = f.detached_text->ring_next;
    ring_prev->ring_next = ring_next;
    ring_next->ring_prev = ring_prev;
    CHECK_VIOLATION(f.document, REASON_NOT_IN_RING);
    ring_prev->ring_next = f.detached_text;
    ring_next->ring_prev = f.detached_text;

    CHECK_VALID(f.document);
    amark_document_destroy(f.document);
}

NAOS_TEST(a_node_of_the_ring_outside_every_subtree_is_detected)
{
    fixture_t f;
    amark_node_t *a;
    amark_node_t *b;

    build(&f);

    /* Two nodes whose sibling links form a cycle under paragraph, which does not
     * list them: every link around each of them is symmetric, but no subtree
     * reaches them. */
    a = create(f.document, AMARK_NODE_TEXT);
    b = create(f.document, AMARK_NODE_TEXT);
    a->parent = f.paragraph;
    b->parent = f.paragraph;
    a->prev = b;
    a->next = b;
    b->prev = a;
    b->next = a;
    CHECK_VIOLATION(f.document, REASON_UNREACHABLE);

    a->parent = NULL;
    b->parent = NULL;
    a->prev = NULL;
    a->next = NULL;
    b->prev = NULL;
    b->next = NULL;
    CHECK_VALID(f.document);
    amark_document_destroy(f.document);
}

NAOS_TEST(nodes_of_another_document_are_detected)
{
    fixture_t f;
    amark_document_t *other = NULL;
    amark_node_t *foreign;

    build(&f);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &other));

    /* A node of the ring that says it belongs to another document. */
    f.second->document = other;
    CHECK_VIOLATION(f.document, REASON_RING_DOCUMENT);
    f.second->document = f.document;

    /* A node of another document linked into the tree. */
    foreign = create(other, AMARK_NODE_TEXT);
    link_last(f.paragraph, foreign);
    CHECK_VIOLATION(f.document, REASON_TREE_DOCUMENT);
    unlink_last(foreign);

    CHECK_VALID(f.document);
    CHECK_VALID(other);
    amark_document_destroy(other);
    amark_document_destroy(f.document);
}

/* --- amark_node_allows_child ----------------------------------------------------- */

/* test.box accepts nothing by its classes but overrides the rule: it holds the
 * inline types with a line literal (text, code) and other boxes. */
static bool box_can_contain(const amark_node_type_desc_t *self,
                            const amark_node_type_desc_t *child)
{
    return (child == self)
           || ((child->level == AMARK_NODE_LEVEL_INLINE)
               && (child->literal_mode == AMARK_LITERAL_LINE));
}

static const amark_node_type_desc_t box_type = {
    sizeof(amark_node_type_desc_t), "test.box", AMARK_NODE_LEVEL_BLOCK, AMARK_CONTENT_BLOCK, 0,
    AMARK_LITERAL_NONE, 0, box_can_contain, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

static const amark_node_type_desc_t mark_type = {
    sizeof(amark_node_type_desc_t), "test.mark", AMARK_NODE_LEVEL_INLINE, AMARK_CONTENT_INLINE,
    AMARK_CONTENT_INLINE, AMARK_LITERAL_NONE, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

static const amark_node_type_desc_t *const test_types[] = { &box_type, &mark_type };

static const amark_extension_t test_extension = {
    sizeof(amark_extension_t), "test", test_types, 2, NULL, 0, NULL
};

#define TYPE_COUNT (AMARK_BUILTIN_TYPE_COUNT + 2u)

/* The identifier of the i-th type (0-based) of the set: the built-in types, then
 * the two of test_extension. */
static amark_node_type_t type_at(size_t i)
{
    if (i < AMARK_BUILTIN_TYPE_COUNT)
    {
        return (amark_node_type_t)(i + 1);
    }

    return (amark_node_type_t)(AMARK_NODE_DYNAMIC_FIRST + (i - AMARK_BUILTIN_TYPE_COUNT));
}

NAOS_TEST(allows_child_agrees_with_can_contain)
{
    amark_syntax_set_t *set = NULL;
    amark_document_t *document = NULL;
    amark_node_t *parents[TYPE_COUNT];
    size_t allowed = 0;
    size_t i;
    size_t j;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_create(&set));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &test_extension));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_freeze(set));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(set, &document));
    NAOS_REQUIRE_PTR_EQ(&box_type, amark_syntax_set_desc(set, type_at(AMARK_BUILTIN_TYPE_COUNT)));

    parents[0] = amark_document_root(document);
    for (i = 1; i < TYPE_COUNT; i++)
    {
        parents[i] = create(document, type_at(i));
    }

    for (i = 0; i < TYPE_COUNT; i++)
    {
        for (j = 0; j < TYPE_COUNT; j++)
        {
            const amark_node_type_desc_t *child_desc = amark_syntax_set_desc(set, type_at(j));
            bool allows;

            NAOS_REQUIRE_NOT_NULL(child_desc);
            allows = amark_node_allows_child(parents[i], child_desc);
            NAOS_CHECK_TRUE(allows == amark_node_can_contain(parents[i], type_at(j)));
            if (allows)
            {
                allowed++;
            }
        }
    }

    NAOS_CHECK_UINT_GT(allowed, 0);

    /* The override and the classes of the extension types. */
    NAOS_CHECK_TRUE(amark_node_allows_child(parents[AMARK_BUILTIN_TYPE_COUNT],
                                            amark_builtin_types[AMARK_NODE_TEXT]));
    NAOS_CHECK_TRUE(amark_node_allows_child(parents[AMARK_BUILTIN_TYPE_COUNT], &box_type));
    NAOS_CHECK_FALSE(amark_node_allows_child(parents[AMARK_BUILTIN_TYPE_COUNT],
                                             amark_builtin_types[AMARK_NODE_PARAGRAPH]));
    NAOS_CHECK_FALSE(amark_node_allows_child(parents[AMARK_BUILTIN_TYPE_COUNT], &mark_type));
    NAOS_CHECK_TRUE(amark_node_allows_child(parents[0], &box_type));
    NAOS_CHECK_TRUE(amark_node_allows_child(parents[AMARK_NODE_PARAGRAPH - 1], &mark_type));
    NAOS_CHECK_FALSE(amark_node_allows_child(parents[AMARK_NODE_TEXT - 1], &mark_type));

    /* The check uses the same rule: a paragraph in a box is a violation, a text
     * is not. */
    link_last(parents[AMARK_BUILTIN_TYPE_COUNT], parents[AMARK_NODE_TEXT - 1]);
    CHECK_VALID(document);
    link_last(parents[AMARK_BUILTIN_TYPE_COUNT], parents[AMARK_NODE_PARAGRAPH - 1]);
    CHECK_VIOLATION(document, REASON_CONTAINMENT);
    unlink_last(parents[AMARK_NODE_PARAGRAPH - 1]);
    CHECK_VALID(document);

    amark_document_destroy(document);
    amark_syntax_set_destroy(set);
}

/* --- Counters -------------------------------------------------------------------- */

NAOS_TEST(the_counters_start_at_zero)
{
    amark_document_t *document = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    NAOS_CHECK_UINT_EQ(0, document->stat_propagation_steps);
    NAOS_CHECK_UINT_EQ(0, document->stat_token_moves);
    amark_document_destroy(document);
}

NAOS_SUITE(tree_check,
    NAOS_ADD_TEST(an_empty_document_is_valid),
    NAOS_ADD_TEST(a_tree_with_detached_subtrees_is_valid),
    NAOS_ADD_TEST(sorted_tokens_within_their_sources_are_valid),
    NAOS_ADD_TEST(edit_records_closed_over_the_ancestors_are_valid),
    NAOS_ADD_TEST(a_deep_chain_is_checked_without_recursion),
    NAOS_ADD_TEST(a_null_document_is_an_invalid_argument),
    NAOS_ADD_TEST(asymmetric_sibling_links_are_detected),
    NAOS_ADD_TEST(a_node_without_a_parent_with_siblings_is_detected),
    NAOS_ADD_TEST(a_wrong_first_or_last_child_is_detected),
    NAOS_ADD_TEST(a_wrong_parent_is_detected),
    NAOS_ADD_TEST(a_containment_violation_is_detected),
    NAOS_ADD_TEST(a_missing_descendants_record_is_detected),
    NAOS_ADD_TEST(unsorted_tokens_are_detected),
    NAOS_ADD_TEST(a_token_out_of_its_source_is_detected),
    NAOS_ADD_TEST(asymmetric_ring_links_are_detected),
    NAOS_ADD_TEST(a_node_missing_from_the_ring_is_detected),
    NAOS_ADD_TEST(a_node_of_the_ring_outside_every_subtree_is_detected),
    NAOS_ADD_TEST(nodes_of_another_document_are_detected))

NAOS_SUITE(allows_child,
    NAOS_ADD_TEST(allows_child_agrees_with_can_contain))

NAOS_SUITE(counters,
    NAOS_ADD_TEST(the_counters_start_at_zero))

NAOS_MAIN(
    NAOS_ADD_SUITE(tree_check),
    NAOS_ADD_SUITE(allows_child),
    NAOS_ADD_SUITE(counters)
)
