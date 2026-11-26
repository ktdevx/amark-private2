/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * Structural editing (src/tree/structure_edit.c): insertion, moves, replacement,
 * unlinking and the public destruction, through the public API. The internal
 * amark_tree_check checks the invariants after every operation, and the internal
 * counters show the cost of the cycle check.
 *
 * Suite structure_edit (1.6, 1.7, 4.2, 4.3, 4.4, 4.5, 10.3): the four positions;
 * moves within a parent, between parents and of subtrees; operations on the node
 * itself do nothing; replacement (by a detached node, by a node of the tree, by a
 * sibling, by a descendant); each rejection (another document, the root, a sibling
 * or an old node without a parent, the containment rule and its override, a
 * cycle, a childless container into itself, NULL) leaves every link and record
 * unchanged; unlinking and reinserting a detached node; destroying an attached
 * node, a detached one, the root (nothing) and NULL; inserting a node without
 * children walks no ancestor (stat_cycle_steps stays 0, which 7.3 checks on a deep
 * chain), while a node with children walks from the new parent to the root.
 * Suite structure_records (7.2, 7.3, 7.5, 7.7): the old and the new parent get
 * AMARK_EDIT_CHILDREN, the inserted node AMARK_EDIT_RELOCATED unless it is
 * AMARK_EDIT_CREATED, and the ancestors AMARK_EDIT_DESCENDANTS; with tracking off
 * nothing is recorded, but a subtree that already has records still gives
 * AMARK_EDIT_DESCENDANTS to its new ancestors.
 */
#include "tree/tree.h"

#include <amark/document.h>
#include <amark/error.h>
#include <amark/extension.h>
#include <amark/node.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <naos/naos.h>

/* Expects the check of document to pass. */
#define CHECK_VALID(document) \
    do { \
        const char *check_reason_ = "unset"; \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_tree_check((document), &check_reason_)); \
        NAOS_CHECK_NULL(check_reason_); \
    } while (0)

static amark_node_t *create(amark_document_t *document, amark_node_type_t type)
{
    amark_node_t *node = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, type, &node));
    return node;
}

static void append(amark_node_t *parent, amark_node_t *child)
{
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(parent, child));
}

/*
 * The fixture, built with tracking off:
 *   root > quote > paragraph > (first, emph > inner)
 *   root > second_paragraph > third
 *   root > rule
 * and two detached subtrees: loose_paragraph > fourth, and loose_text.
 */
#define NODE_COUNT 12

typedef struct fixture
{
    amark_document_t *document;
    union
    {
        struct
        {
            amark_node_t *root;
            amark_node_t *quote;
            amark_node_t *paragraph;
            amark_node_t *first;
            amark_node_t *emph;
            amark_node_t *inner;
            amark_node_t *second_paragraph;
            amark_node_t *third;
            amark_node_t *rule;
            amark_node_t *loose_paragraph;
            amark_node_t *fourth;
            amark_node_t *loose_text;
        } n;
        amark_node_t *all[NODE_COUNT];
    } u;
} fixture_t;

static void build(fixture_t *f)
{
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &f->document));
    f->u.n.root = amark_document_root(f->document);
    f->u.n.quote = create(f->document, AMARK_NODE_BLOCK_QUOTE);
    f->u.n.paragraph = create(f->document, AMARK_NODE_PARAGRAPH);
    f->u.n.first = create(f->document, AMARK_NODE_TEXT);
    f->u.n.emph = create(f->document, AMARK_NODE_EMPH);
    f->u.n.inner = create(f->document, AMARK_NODE_TEXT);
    f->u.n.second_paragraph = create(f->document, AMARK_NODE_PARAGRAPH);
    f->u.n.third = create(f->document, AMARK_NODE_TEXT);
    f->u.n.rule = create(f->document, AMARK_NODE_THEMATIC_BREAK);
    f->u.n.loose_paragraph = create(f->document, AMARK_NODE_PARAGRAPH);
    f->u.n.fourth = create(f->document, AMARK_NODE_TEXT);
    f->u.n.loose_text = create(f->document, AMARK_NODE_TEXT);

    append(f->u.n.root, f->u.n.quote);
    append(f->u.n.quote, f->u.n.paragraph);
    append(f->u.n.paragraph, f->u.n.first);
    append(f->u.n.paragraph, f->u.n.emph);
    append(f->u.n.emph, f->u.n.inner);
    append(f->u.n.root, f->u.n.second_paragraph);
    append(f->u.n.second_paragraph, f->u.n.third);
    append(f->u.n.root, f->u.n.rule);
    append(f->u.n.loose_paragraph, f->u.n.fourth);
    CHECK_VALID(f->document);
}

/* The links and the records of a node. */
typedef struct node_state
{
    amark_node_t *parent;
    amark_node_t *first_child;
    amark_node_t *last_child;
    amark_node_t *prev;
    amark_node_t *next;
    uint32_t edits;
} node_state_t;

typedef struct snapshot
{
    node_state_t nodes[NODE_COUNT];
} snapshot_t;

static void take_snapshot(const fixture_t *f, snapshot_t *s)
{
    size_t i;

    memset(s, 0, sizeof(*s));
    for (i = 0; i < NODE_COUNT; i++)
    {
        const amark_node_t *node = f->u.all[i];

        s->nodes[i].parent = amark_node_parent(node);
        s->nodes[i].first_child = amark_node_first_child(node);
        s->nodes[i].last_child = amark_node_last_child(node);
        s->nodes[i].prev = amark_node_previous(node);
        s->nodes[i].next = amark_node_next(node);
        s->nodes[i].edits = amark_node_edits(node);
    }
}

/* Expects the links and records of the nodes of f to be those of before. */
#define CHECK_UNCHANGED(f, before) \
    do { \
        snapshot_t after_; \
        take_snapshot((f), &after_); \
        NAOS_CHECK_MEM_EQ(&(before), sizeof(before), &after_, sizeof(after_)); \
    } while (0)

/* Whether the children of parent are, in order, the count nodes of expected (each
 * with parent as its parent), followed both ways. */
static bool children_are(const amark_node_t *parent, amark_node_t *const *expected, size_t count)
{
    const amark_node_t *child = amark_node_first_child(parent);
    size_t i;

    for (i = 0; i < count; i++)
    {
        if ((child != expected[i]) || (amark_node_parent(child) != parent))
        {
            return false;
        }

        child = amark_node_next(child);
    }

    if (child != NULL)
    {
        return false;
    }

    child = amark_node_last_child(parent);
    for (i = count; i > 0; i--)
    {
        if (child != expected[i - 1])
        {
            return false;
        }

        child = amark_node_previous(child);
    }

    return child == NULL;
}

#define CHILDREN_ARE(parent, ...) \
    children_are((parent), (amark_node_t *const[]){ __VA_ARGS__ }, \
                 sizeof((amark_node_t *const[]){ __VA_ARGS__ }) / sizeof(amark_node_t *))

#define HAS_NO_CHILDREN(parent) \
    ((amark_node_first_child(parent) == NULL) && (amark_node_last_child(parent) == NULL))

static bool is_detached(const amark_node_t *node)
{
    return (amark_node_parent(node) == NULL) && (amark_node_next(node) == NULL)
           && (amark_node_previous(node) == NULL);
}

/* The number of nodes in the ring of document. */
static size_t ring_count(const amark_document_t *document)
{
    const amark_node_t *node = document->ring;
    size_t count = 0;

    if (node == NULL)
    {
        return 0;
    }

    do
    {
        count++;
        node = node->ring_next;
    } while (node != document->ring);

    return count;
}

/* --- Suite structure_edit ---------------------------------------------------------- */

NAOS_TEST(the_four_positions)
{
    fixture_t f;
    amark_node_t *after = NULL;
    amark_node_t *front = NULL;
    amark_node_t *back = NULL;

    build(&f);
    after = create(f.document, AMARK_NODE_TEXT);
    front = create(f.document, AMARK_NODE_SOFTBREAK);
    back = create(f.document, AMARK_NODE_CODE);

    /* Before the first child, after the last one, then in the middle. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_before(f.u.n.first, f.u.n.loose_text));
    NAOS_CHECK_TRUE(CHILDREN_ARE(f.u.n.paragraph, f.u.n.loose_text, f.u.n.first, f.u.n.emph));
    CHECK_VALID(f.document);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_after(f.u.n.emph, after));
    NAOS_CHECK_TRUE(CHILDREN_ARE(f.u.n.paragraph, f.u.n.loose_text, f.u.n.first, f.u.n.emph,
                                 after));
    CHECK_VALID(f.document);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_prepend_child(f.u.n.paragraph, front));
    NAOS_CHECK_TRUE(CHILDREN_ARE(f.u.n.paragraph, front, f.u.n.loose_text, f.u.n.first,
                                 f.u.n.emph, after));
    CHECK_VALID(f.document);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(f.u.n.paragraph, back));
    NAOS_CHECK_TRUE(CHILDREN_ARE(f.u.n.paragraph, front, f.u.n.loose_text, f.u.n.first,
                                 f.u.n.emph, after, back));
    CHECK_VALID(f.document);

    /* insert_after in the middle, insert_before in the middle. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_after(f.u.n.first, f.u.n.fourth));
    NAOS_CHECK_TRUE(CHILDREN_ARE(f.u.n.paragraph, front, f.u.n.loose_text, f.u.n.first,
                                 f.u.n.fourth, f.u.n.emph, after, back));
    NAOS_CHECK_TRUE(HAS_NO_CHILDREN(f.u.n.loose_paragraph));
    CHECK_VALID(f.document);

    /* Into an empty parent, both ways. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_prepend_child(f.u.n.loose_paragraph, f.u.n.fourth));
    NAOS_CHECK_TRUE(CHILDREN_ARE(f.u.n.loose_paragraph, f.u.n.fourth));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_unlink(f.u.n.fourth));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_append_child(f.u.n.loose_paragraph, f.u.n.fourth));
    NAOS_CHECK_TRUE(CHILDREN_ARE(f.u.n.loose_paragraph, f.u.n.fourth));
    CHECK_VALID(f.document);

    amark_document_destroy(f.document);
}

NAOS_TEST(inserting_a_node_of_the_tree_moves_it)
{
    fixture_t f;

    build(&f);

    /* Between parents: the old parent is left empty. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(f.u.n.paragraph, f.u.n.third));
    NAOS_CHECK_TRUE(CHILDREN_ARE(f.u.n.paragraph, f.u.n.first, f.u.n.emph, f.u.n.third));
    NAOS_CHECK_TRUE(HAS_NO_CHILDREN(f.u.n.second_paragraph));
    CHECK_VALID(f.document);

    /* Within a parent: reorderings, including to the place next to itself. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_before(f.u.n.first, f.u.n.third));
    NAOS_CHECK_TRUE(CHILDREN_ARE(f.u.n.paragraph, f.u.n.third, f.u.n.first, f.u.n.emph));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_after(f.u.n.emph, f.u.n.third));
    NAOS_CHECK_TRUE(CHILDREN_ARE(f.u.n.paragraph, f.u.n.first, f.u.n.emph, f.u.n.third));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_after(f.u.n.first, f.u.n.emph));
    NAOS_CHECK_TRUE(CHILDREN_ARE(f.u.n.paragraph, f.u.n.first, f.u.n.emph, f.u.n.third));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_prepend_child(f.u.n.paragraph, f.u.n.first));
    NAOS_CHECK_TRUE(CHILDREN_ARE(f.u.n.paragraph, f.u.n.first, f.u.n.emph, f.u.n.third));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(f.u.n.paragraph, f.u.n.first));
    NAOS_CHECK_TRUE(CHILDREN_ARE(f.u.n.paragraph, f.u.n.emph, f.u.n.third, f.u.n.first));
    CHECK_VALID(f.document);

    /* A subtree moves with its descendants: the quote goes after the rule, then
     * the emph into the second paragraph. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_after(f.u.n.rule, f.u.n.quote));
    NAOS_CHECK_TRUE(CHILDREN_ARE(f.u.n.root, f.u.n.second_paragraph, f.u.n.rule, f.u.n.quote));
    NAOS_CHECK_TRUE(CHILDREN_ARE(f.u.n.quote, f.u.n.paragraph));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_append_child(f.u.n.second_paragraph, f.u.n.emph));
    NAOS_CHECK_TRUE(CHILDREN_ARE(f.u.n.second_paragraph, f.u.n.emph));
    NAOS_CHECK_TRUE(CHILDREN_ARE(f.u.n.emph, f.u.n.inner));
    NAOS_CHECK_TRUE(CHILDREN_ARE(f.u.n.paragraph, f.u.n.third, f.u.n.first));
    CHECK_VALID(f.document);

    amark_document_destroy(f.document);
}

NAOS_TEST(using_the_node_itself_does_nothing)
{
    fixture_t f;
    snapshot_t before;

    build(&f);
    amark_document_set_edit_tracking(f.document, true);
    take_snapshot(&f, &before);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_before(f.u.n.first, f.u.n.first));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_after(f.u.n.emph, f.u.n.emph));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_replace(f.u.n.quote, f.u.n.quote));
    CHECK_UNCHANGED(&f, before);
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.u.n.paragraph));
    CHECK_VALID(f.document);

    amark_document_destroy(f.document);
}

NAOS_TEST(replace_puts_the_new_node_in_place_of_the_old)
{
    fixture_t f;

    build(&f);

    /* By a detached node. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_replace(f.u.n.first, f.u.n.loose_text));
    NAOS_CHECK_TRUE(CHILDREN_ARE(f.u.n.paragraph, f.u.n.loose_text, f.u.n.emph));
    NAOS_CHECK_TRUE(is_detached(f.u.n.first));
    CHECK_VALID(f.document);

    /* By a node of another parent, which moves. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_replace(f.u.n.loose_text, f.u.n.third));
    NAOS_CHECK_TRUE(CHILDREN_ARE(f.u.n.paragraph, f.u.n.third, f.u.n.emph));
    NAOS_CHECK_TRUE(HAS_NO_CHILDREN(f.u.n.second_paragraph));
    NAOS_CHECK_TRUE(is_detached(f.u.n.loose_text));
    CHECK_VALID(f.document);

    /* By its next sibling, then by its previous one. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(f.u.n.paragraph, f.u.n.first));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_replace(f.u.n.third, f.u.n.emph));
    NAOS_CHECK_TRUE(CHILDREN_ARE(f.u.n.paragraph, f.u.n.emph, f.u.n.first));
    NAOS_CHECK_TRUE(is_detached(f.u.n.third));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_replace(f.u.n.first, f.u.n.emph));
    NAOS_CHECK_TRUE(CHILDREN_ARE(f.u.n.paragraph, f.u.n.emph));
    NAOS_CHECK_TRUE(is_detached(f.u.n.first));
    CHECK_VALID(f.document);

    /* By one of its descendants: the old node leaves without it. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_replace(f.u.n.emph, f.u.n.inner));
    NAOS_CHECK_TRUE(CHILDREN_ARE(f.u.n.paragraph, f.u.n.inner));
    NAOS_CHECK_TRUE(is_detached(f.u.n.emph));
    NAOS_CHECK_TRUE(HAS_NO_CHILDREN(f.u.n.emph));
    CHECK_VALID(f.document);

    /* A whole subtree in place of a top-level block. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_replace(f.u.n.rule, f.u.n.loose_paragraph));
    NAOS_CHECK_TRUE(CHILDREN_ARE(f.u.n.root, f.u.n.quote, f.u.n.second_paragraph,
                                 f.u.n.loose_paragraph));
    NAOS_CHECK_TRUE(CHILDREN_ARE(f.u.n.loose_paragraph, f.u.n.fourth));
    NAOS_CHECK_TRUE(is_detached(f.u.n.rule));
    CHECK_VALID(f.document);

    amark_document_destroy(f.document);
}

/* Expects op to be an invalid argument that changes nothing in f. */
#define CHECK_REJECTED(f, before, op) \
    do { \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, (op)); \
        CHECK_UNCHANGED((f), (before)); \
        CHECK_VALID((f)->document); \
    } while (0)

NAOS_TEST(a_rejected_operation_changes_nothing)
{
    fixture_t f;
    snapshot_t before;
    amark_document_t *other = NULL;
    amark_node_t *foreign = NULL;
    amark_node_t *foreign_paragraph = NULL;

    build(&f);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &other));
    foreign = create(other, AMARK_NODE_TEXT);
    foreign_paragraph = create(other, AMARK_NODE_PARAGRAPH);
    append(amark_document_root(other), foreign_paragraph);

    /* With tracking on, so that the records must not change either. */
    amark_document_set_edit_tracking(f.document, true);
    take_snapshot(&f, &before);

    /* 1: a node of another document, either way round. */
    CHECK_REJECTED(&f, before, amark_node_append_child(f.u.n.paragraph, foreign));
    CHECK_REJECTED(&f, before, amark_node_prepend_child(f.u.n.paragraph, foreign));
    CHECK_REJECTED(&f, before, amark_node_insert_before(f.u.n.first, foreign));
    CHECK_REJECTED(&f, before, amark_node_insert_after(f.u.n.first, foreign));
    CHECK_REJECTED(&f, before, amark_node_replace(f.u.n.first, foreign));
    CHECK_REJECTED(&f, before, amark_node_append_child(foreign_paragraph, f.u.n.first));
    CHECK_REJECTED(&f, before, amark_node_replace(foreign_paragraph, f.u.n.quote));
    NAOS_CHECK_TRUE(CHILDREN_ARE(amark_document_root(other), foreign_paragraph));
    NAOS_CHECK_TRUE(is_detached(foreign));

    /* 2: the root, and a sibling or an old node without a parent (the root, a
     * detached node). */
    CHECK_REJECTED(&f, before, amark_node_append_child(f.u.n.quote, f.u.n.root));
    CHECK_REJECTED(&f, before, amark_node_prepend_child(f.u.n.quote, f.u.n.root));
    CHECK_REJECTED(&f, before, amark_node_insert_before(f.u.n.quote, f.u.n.root));
    CHECK_REJECTED(&f, before, amark_node_insert_after(f.u.n.rule, f.u.n.root));
    CHECK_REJECTED(&f, before, amark_node_replace(f.u.n.quote, f.u.n.root));
    CHECK_REJECTED(&f, before, amark_node_unlink(f.u.n.root));
    CHECK_REJECTED(&f, before, amark_node_insert_before(f.u.n.root, f.u.n.rule));
    CHECK_REJECTED(&f, before, amark_node_insert_after(f.u.n.root, f.u.n.rule));
    CHECK_REJECTED(&f, before, amark_node_insert_before(f.u.n.loose_text, f.u.n.first));
    CHECK_REJECTED(&f, before, amark_node_insert_after(f.u.n.loose_paragraph, f.u.n.rule));
    CHECK_REJECTED(&f, before, amark_node_insert_before(f.u.n.loose_text, f.u.n.loose_text));
    CHECK_REJECTED(&f, before, amark_node_replace(f.u.n.root, f.u.n.loose_paragraph));
    CHECK_REJECTED(&f, before, amark_node_replace(f.u.n.loose_text, f.u.n.first));
    CHECK_REJECTED(&f, before, amark_node_replace(f.u.n.loose_paragraph, f.u.n.rule));

    /* 3: the containment rule. */
    CHECK_REJECTED(&f, before, amark_node_append_child(f.u.n.paragraph, f.u.n.loose_paragraph));
    CHECK_REJECTED(&f, before, amark_node_append_child(f.u.n.first, f.u.n.loose_text));
    CHECK_REJECTED(&f, before, amark_node_prepend_child(f.u.n.rule, f.u.n.loose_paragraph));
    CHECK_REJECTED(&f, before, amark_node_insert_before(f.u.n.quote, f.u.n.loose_text));
    CHECK_REJECTED(&f, before, amark_node_insert_after(f.u.n.first, f.u.n.rule));
    CHECK_REJECTED(&f, before, amark_node_replace(f.u.n.first, f.u.n.second_paragraph));
    CHECK_REJECTED(&f, before, amark_node_replace(f.u.n.rule, f.u.n.third));

    /* 4: a cycle: a node with children into itself, under its own descendant,
     * next to its own child (a childless container into itself is in
     * a_childless_container_cannot_contain_itself). */
    CHECK_REJECTED(&f, before, amark_node_append_child(f.u.n.emph, f.u.n.emph));
    CHECK_REJECTED(&f, before, amark_node_prepend_child(f.u.n.quote, f.u.n.quote));
    CHECK_REJECTED(&f, before, amark_node_insert_before(f.u.n.inner, f.u.n.emph));
    CHECK_REJECTED(&f, before, amark_node_insert_after(f.u.n.inner, f.u.n.emph));
    CHECK_REJECTED(&f, before, amark_node_replace(f.u.n.inner, f.u.n.emph));

    /* NULL. */
    CHECK_REJECTED(&f, before, amark_node_append_child(NULL, f.u.n.loose_text));
    CHECK_REJECTED(&f, before, amark_node_append_child(f.u.n.paragraph, NULL));
    CHECK_REJECTED(&f, before, amark_node_prepend_child(NULL, f.u.n.loose_text));
    CHECK_REJECTED(&f, before, amark_node_prepend_child(f.u.n.paragraph, NULL));
    CHECK_REJECTED(&f, before, amark_node_insert_before(NULL, f.u.n.loose_text));
    CHECK_REJECTED(&f, before, amark_node_insert_before(f.u.n.first, NULL));
    CHECK_REJECTED(&f, before, amark_node_insert_after(NULL, f.u.n.loose_text));
    CHECK_REJECTED(&f, before, amark_node_insert_after(f.u.n.first, NULL));
    CHECK_REJECTED(&f, before, amark_node_replace(NULL, f.u.n.loose_text));
    CHECK_REJECTED(&f, before, amark_node_replace(f.u.n.first, NULL));
    CHECK_REJECTED(&f, before, amark_node_unlink(NULL));

    amark_document_destroy(other);
    amark_document_destroy(f.document);
}

NAOS_TEST(a_childless_container_cannot_contain_itself)
{
    fixture_t f;
    snapshot_t before;
    amark_node_t *attached = NULL;
    amark_node_t *detached = NULL;

    build(&f);
    attached = create(f.document, AMARK_NODE_BLOCK_QUOTE);
    detached = create(f.document, AMARK_NODE_BLOCK_QUOTE);
    append(f.u.n.root, attached);
    amark_document_set_edit_tracking(f.document, true);
    take_snapshot(&f, &before);
    NAOS_CHECK_UINT_EQ(0, f.document->stat_cycle_steps);

    /* An empty block quote accepts blocks and is one: only the check that the new
     * parent is not the node itself stops it, in constant time (no ancestor is
     * walked). */
    CHECK_REJECTED(&f, before, amark_node_append_child(attached, attached));
    CHECK_REJECTED(&f, before, amark_node_prepend_child(attached, attached));
    CHECK_REJECTED(&f, before, amark_node_append_child(detached, detached));
    CHECK_REJECTED(&f, before, amark_node_prepend_child(detached, detached));
    NAOS_CHECK_TRUE(HAS_NO_CHILDREN(attached));
    NAOS_CHECK_TRUE(HAS_NO_CHILDREN(detached));
    NAOS_CHECK_PTR_EQ(f.u.n.root, amark_node_parent(attached));
    NAOS_CHECK_PTR_EQ(attached, amark_node_last_child(f.u.n.root));
    NAOS_CHECK_TRUE(is_detached(detached));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(attached));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(detached));
    NAOS_CHECK_UINT_EQ(0, f.document->stat_cycle_steps);

    amark_document_destroy(f.document);
}

NAOS_TEST(a_cycle_through_a_deeper_ancestor_is_rejected)
{
    amark_document_t *document = NULL;
    amark_node_t *outer = NULL;
    amark_node_t *middle = NULL;
    amark_node_t *bottom = NULL;
    amark_node_t *paragraph = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    outer = create(document, AMARK_NODE_BLOCK_QUOTE);
    middle = create(document, AMARK_NODE_BLOCK_QUOTE);
    bottom = create(document, AMARK_NODE_BLOCK_QUOTE);
    paragraph = create(document, AMARK_NODE_PARAGRAPH);
    append(amark_document_root(document), outer);
    append(outer, middle);
    append(middle, bottom);
    append(bottom, paragraph);

    /* outer is the grandparent of the new parent, and a detached subtree too. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_append_child(bottom, outer));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_insert_before(paragraph, outer));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_replace(paragraph, middle));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_unlink(outer));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_prepend_child(bottom, outer));
    NAOS_CHECK_TRUE(CHILDREN_ARE(outer, middle));
    NAOS_CHECK_TRUE(CHILDREN_ARE(middle, bottom));
    NAOS_CHECK_TRUE(CHILDREN_ARE(bottom, paragraph));
    CHECK_VALID(document);

    /* Moving a descendant up is no cycle. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(outer, bottom));
    NAOS_CHECK_TRUE(CHILDREN_ARE(outer, middle, bottom));
    CHECK_VALID(document);

    amark_document_destroy(document);
}

/* test.box accepts nothing by its classes but overrides the rule: it holds texts
 * only. */
static bool box_can_contain(const amark_node_type_desc_t *self,
                            const amark_node_type_desc_t *child)
{
    (void)self;
    return strcmp(child->name, "text") == 0;
}

static const amark_node_type_desc_t box_type = {
    sizeof(amark_node_type_desc_t), "test.box", AMARK_NODE_LEVEL_BLOCK, AMARK_CONTENT_BLOCK,
    AMARK_CONTENT_INLINE, AMARK_LITERAL_NONE, 0, box_can_contain, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL
};

static const amark_node_type_desc_t *const test_types[] = { &box_type };

static const amark_extension_t test_extension = {
    sizeof(amark_extension_t), "test", test_types, 1, NULL, 0, NULL
};

NAOS_TEST(the_override_of_the_containment_rule_comes_first)
{
    amark_syntax_set_t *set = NULL;
    amark_document_t *document = NULL;
    amark_node_t *box = NULL;
    amark_node_t *text = NULL;
    amark_node_t *emph = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_create(&set));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &test_extension));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_freeze(set));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(set, &document));
    box = create(document, AMARK_NODE_DYNAMIC_FIRST);
    text = create(document, AMARK_NODE_TEXT);
    emph = create(document, AMARK_NODE_EMPH);

    /* The box accepts inline content by its classes, but its can_contain allows
     * texts only. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_append_child(amark_document_root(document), box));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(box, text));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_append_child(box, emph));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_insert_after(text, emph));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_replace(text, emph));
    NAOS_CHECK_TRUE(CHILDREN_ARE(box, text));
    NAOS_CHECK_TRUE(is_detached(emph));
    CHECK_VALID(document);

    amark_document_destroy(document);
    amark_syntax_set_destroy(set);
}

NAOS_TEST(an_unlinked_node_stays_and_can_be_inserted_again)
{
    fixture_t f;
    size_t nodes;

    build(&f);
    nodes = ring_count(f.document);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_unlink(f.u.n.emph));
    NAOS_CHECK_TRUE(is_detached(f.u.n.emph));
    NAOS_CHECK_TRUE(CHILDREN_ARE(f.u.n.emph, f.u.n.inner));
    NAOS_CHECK_TRUE(CHILDREN_ARE(f.u.n.paragraph, f.u.n.first));
    NAOS_CHECK_PTR_EQ(f.document, amark_node_document(f.u.n.emph));
    NAOS_CHECK_UINT_EQ(nodes, ring_count(f.document));
    CHECK_VALID(f.document);

    /* Unlinking a detached node does nothing; unlinking the only child empties the
     * parent; the first and the last child. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_unlink(f.u.n.emph));
    NAOS_CHECK_TRUE(is_detached(f.u.n.emph));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_unlink(f.u.n.third));
    NAOS_CHECK_TRUE(HAS_NO_CHILDREN(f.u.n.second_paragraph));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_unlink(f.u.n.quote));
    NAOS_CHECK_TRUE(CHILDREN_ARE(f.u.n.root, f.u.n.second_paragraph, f.u.n.rule));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_unlink(f.u.n.rule));
    NAOS_CHECK_TRUE(CHILDREN_ARE(f.u.n.root, f.u.n.second_paragraph));
    CHECK_VALID(f.document);

    /* Inserted again, elsewhere, with their subtrees. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_append_child(f.u.n.second_paragraph, f.u.n.emph));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_prepend_child(f.u.n.emph, f.u.n.third));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_insert_before(f.u.n.second_paragraph, f.u.n.rule));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_insert_after(f.u.n.second_paragraph, f.u.n.quote));
    NAOS_CHECK_TRUE(CHILDREN_ARE(f.u.n.root, f.u.n.rule, f.u.n.second_paragraph, f.u.n.quote));
    NAOS_CHECK_TRUE(CHILDREN_ARE(f.u.n.second_paragraph, f.u.n.emph));
    NAOS_CHECK_TRUE(CHILDREN_ARE(f.u.n.emph, f.u.n.third, f.u.n.inner));
    NAOS_CHECK_TRUE(CHILDREN_ARE(f.u.n.quote, f.u.n.paragraph));
    NAOS_CHECK_UINT_EQ(nodes, ring_count(f.document));
    CHECK_VALID(f.document);

    amark_document_destroy(f.document);
}

NAOS_TEST(destroy_unlinks_and_frees_the_subtree)
{
    fixture_t f;
    size_t nodes;

    build(&f);
    nodes = ring_count(f.document);

    /* An attached subtree (emph > inner). */
    amark_node_destroy(f.u.n.emph);
    NAOS_CHECK_TRUE(CHILDREN_ARE(f.u.n.paragraph, f.u.n.first));
    NAOS_CHECK_UINT_EQ(nodes - 2, ring_count(f.document));
    CHECK_VALID(f.document);

    /* A detached subtree, a node in the middle, then a whole branch. */
    amark_node_destroy(f.u.n.loose_paragraph);
    NAOS_CHECK_UINT_EQ(nodes - 4, ring_count(f.document));
    amark_node_destroy(f.u.n.second_paragraph);
    NAOS_CHECK_TRUE(CHILDREN_ARE(f.u.n.root, f.u.n.quote, f.u.n.rule));
    amark_node_destroy(f.u.n.quote);
    NAOS_CHECK_TRUE(CHILDREN_ARE(f.u.n.root, f.u.n.rule));
    NAOS_CHECK_UINT_EQ(nodes - 9, ring_count(f.document));
    CHECK_VALID(f.document);

    /* The root and NULL: nothing. */
    amark_node_destroy(f.u.n.root);
    amark_node_destroy(NULL);
    NAOS_CHECK_PTR_EQ(f.u.n.root, amark_document_root(f.document));
    NAOS_CHECK_TRUE(CHILDREN_ARE(f.u.n.root, f.u.n.rule));
    NAOS_CHECK_UINT_EQ(nodes - 9, ring_count(f.document));
    CHECK_VALID(f.document);

    amark_document_destroy(f.document);
}

#define CHAIN_DEPTH 1000u

NAOS_TEST(only_a_node_with_children_walks_the_ancestors)
{
    amark_document_t *document = NULL;
    amark_node_t *bottom = NULL;
    amark_node_t *node = NULL;
    amark_node_t *quote = NULL;
    amark_node_t *paragraph = NULL;
    size_t i;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    NAOS_CHECK_UINT_EQ(0, document->stat_cycle_steps);

    /* A chain of empty quotes: each insertion is of a node without children. */
    bottom = amark_document_root(document);
    for (i = 0; i < CHAIN_DEPTH; i++)
    {
        node = create(document, AMARK_NODE_BLOCK_QUOTE);
        append(bottom, node);
        bottom = node;
    }

    NAOS_CHECK_UINT_EQ(0, document->stat_cycle_steps);

    /* Leaves and empty containers at the bottom, in every position, and moves of
     * them: still no step. */
    paragraph = create(document, AMARK_NODE_PARAGRAPH);
    quote = create(document, AMARK_NODE_BLOCK_QUOTE);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(bottom, paragraph));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_before(paragraph, quote));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_after(paragraph, quote));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_prepend_child(bottom, quote));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_replace(quote, create(document, AMARK_NODE_THEMATIC_BREAK)));
    NAOS_CHECK_UINT_EQ(0, document->stat_cycle_steps);

    /* A node with a child walks from the new parent to the root: the bottom quote,
     * the CHAIN_DEPTH - 1 above it and the root. */
    append(quote, create(document, AMARK_NODE_PARAGRAPH));
    NAOS_CHECK_UINT_EQ(0, document->stat_cycle_steps);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(bottom, quote));
    NAOS_CHECK_UINT_EQ(CHAIN_DEPTH + 1, document->stat_cycle_steps);
    CHECK_VALID(document);

    amark_document_destroy(document);
}

NAOS_SUITE(structure_edit,
    NAOS_ADD_TEST(the_four_positions),
    NAOS_ADD_TEST(inserting_a_node_of_the_tree_moves_it),
    NAOS_ADD_TEST(using_the_node_itself_does_nothing),
    NAOS_ADD_TEST(replace_puts_the_new_node_in_place_of_the_old),
    NAOS_ADD_TEST(a_rejected_operation_changes_nothing),
    NAOS_ADD_TEST(a_childless_container_cannot_contain_itself),
    NAOS_ADD_TEST(a_cycle_through_a_deeper_ancestor_is_rejected),
    NAOS_ADD_TEST(the_override_of_the_containment_rule_comes_first),
    NAOS_ADD_TEST(an_unlinked_node_stays_and_can_be_inserted_again),
    NAOS_ADD_TEST(destroy_unlinks_and_frees_the_subtree),
    NAOS_ADD_TEST(only_a_node_with_children_walks_the_ancestors))

/* --- Suite structure_records ------------------------------------------------------- */

#define D AMARK_EDIT_DESCENDANTS
#define C AMARK_EDIT_CHILDREN
#define R AMARK_EDIT_RELOCATED

NAOS_TEST(a_move_records_both_parents_and_the_node)
{
    fixture_t f;

    build(&f);
    amark_document_set_edit_tracking(f.document, true);

    /* third: second_paragraph -> paragraph. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(f.u.n.paragraph, f.u.n.third));
    NAOS_CHECK_UINT_EQ(R, amark_node_edits(f.u.n.third));
    NAOS_CHECK_UINT_EQ(C | D, amark_node_edits(f.u.n.paragraph));
    NAOS_CHECK_UINT_EQ(C, amark_node_edits(f.u.n.second_paragraph));
    NAOS_CHECK_UINT_EQ(D, amark_node_edits(f.u.n.quote));
    NAOS_CHECK_UINT_EQ(D, amark_node_edits(f.u.n.root));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.u.n.first));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.u.n.emph));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.u.n.inner));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.u.n.rule));
    CHECK_VALID(f.document);

    /* A reordering within the root: the moved node only, not its descendants. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_after(f.u.n.rule, f.u.n.quote));
    NAOS_CHECK_UINT_EQ(R | D, amark_node_edits(f.u.n.quote));
    NAOS_CHECK_UINT_EQ(C | D, amark_node_edits(f.u.n.root));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.u.n.first));
    CHECK_VALID(f.document);

    amark_document_destroy(f.document);
}

NAOS_TEST(a_new_node_is_created_not_relocated)
{
    fixture_t f;
    amark_node_t *node = NULL;

    build(&f);
    amark_document_set_edit_tracking(f.document, true);
    node = create(f.document, AMARK_NODE_TEXT);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(f.u.n.second_paragraph, node));
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_CREATED, amark_node_edits(node));
    NAOS_CHECK_UINT_EQ(C | D, amark_node_edits(f.u.n.second_paragraph));
    NAOS_CHECK_UINT_EQ(D, amark_node_edits(f.u.n.root));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.u.n.third));
    CHECK_VALID(f.document);

    /* Moved again, it stays created only. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_before(f.u.n.first, node));
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_CREATED, amark_node_edits(node));
    NAOS_CHECK_UINT_EQ(C | D, amark_node_edits(f.u.n.paragraph));
    NAOS_CHECK_UINT_EQ(D, amark_node_edits(f.u.n.quote));
    CHECK_VALID(f.document);

    amark_document_destroy(f.document);
}

NAOS_TEST(unlink_replace_and_destroy_record_the_parent)
{
    fixture_t f;

    build(&f);
    amark_document_set_edit_tracking(f.document, true);

    /* An unlinked node keeps no new record of its own; a reinserted one is
     * relocated. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_unlink(f.u.n.first));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.u.n.first));
    NAOS_CHECK_UINT_EQ(C, amark_node_edits(f.u.n.paragraph));
    NAOS_CHECK_UINT_EQ(D, amark_node_edits(f.u.n.quote));
    NAOS_CHECK_UINT_EQ(D, amark_node_edits(f.u.n.root));
    CHECK_VALID(f.document);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_append_child(f.u.n.second_paragraph, f.u.n.first));
    NAOS_CHECK_UINT_EQ(R, amark_node_edits(f.u.n.first));
    NAOS_CHECK_UINT_EQ(C | D, amark_node_edits(f.u.n.second_paragraph));
    CHECK_VALID(f.document);

    /* replace: the old node leaves without a record, the new one is relocated. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_replace(f.u.n.rule, f.u.n.loose_paragraph));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.u.n.rule));
    NAOS_CHECK_UINT_EQ(R, amark_node_edits(f.u.n.loose_paragraph));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.u.n.fourth));
    NAOS_CHECK_UINT_EQ(C | D, amark_node_edits(f.u.n.root));
    CHECK_VALID(f.document);

    /* destroy: the parent. */
    amark_node_destroy(f.u.n.inner);
    NAOS_CHECK_UINT_EQ(C, amark_node_edits(f.u.n.emph));
    NAOS_CHECK_UINT_EQ(C | D, amark_node_edits(f.u.n.paragraph));
    CHECK_VALID(f.document);

    amark_document_destroy(f.document);
}

NAOS_TEST(nothing_is_recorded_while_tracking_is_off)
{
    fixture_t f;
    size_t i;

    build(&f);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(f.u.n.paragraph, f.u.n.third));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_after(f.u.n.rule, f.u.n.quote));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_replace(f.u.n.first, f.u.n.loose_text));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_prepend_child(f.u.n.root, f.u.n.loose_paragraph));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_unlink(f.u.n.emph));
    amark_node_destroy(f.u.n.second_paragraph);

    for (i = 0; i < NODE_COUNT; i++)
    {
        if (f.u.all[i] != f.u.n.second_paragraph)
        {
            NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.u.all[i]));
        }
    }

    NAOS_CHECK_UINT_EQ(0, f.document->stat_propagation_steps);
    CHECK_VALID(f.document);

    amark_document_destroy(f.document);
}

NAOS_TEST(records_reach_the_new_ancestors_even_while_tracking_is_off)
{
    fixture_t f;
    amark_node_t *created = NULL;

    build(&f);

    /* Records made while tracking was on: a new node, and an edit inside the
     * detached loose_paragraph. */
    amark_document_set_edit_tracking(f.document, true);
    created = create(f.document, AMARK_NODE_TEXT);
    amark_node_mark_edited(f.u.n.fourth);
    NAOS_CHECK_UINT_EQ(D, amark_node_edits(f.u.n.loose_paragraph));
    amark_document_set_edit_tracking(f.document, false);

    /* Inserted with tracking off: no new record, but the new ancestors get
     * AMARK_EDIT_DESCENDANTS (the check would fail otherwise). */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(f.u.n.emph, created));
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_CREATED, amark_node_edits(created));
    NAOS_CHECK_UINT_EQ(D, amark_node_edits(f.u.n.emph));
    NAOS_CHECK_UINT_EQ(D, amark_node_edits(f.u.n.paragraph));
    NAOS_CHECK_UINT_EQ(D, amark_node_edits(f.u.n.quote));
    NAOS_CHECK_UINT_EQ(D, amark_node_edits(f.u.n.root));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.u.n.second_paragraph));
    CHECK_VALID(f.document);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_replace(f.u.n.third, f.u.n.fourth));
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_CONTENT, amark_node_edits(f.u.n.fourth));
    NAOS_CHECK_UINT_EQ(D, amark_node_edits(f.u.n.second_paragraph));
    CHECK_VALID(f.document);

    /* A subtree whose root has DESCENDANTS only, moved between branches. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(f.u.n.root, f.u.n.loose_paragraph));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(f.u.n.loose_paragraph, f.u.n.emph));
    NAOS_CHECK_UINT_EQ(D, amark_node_edits(f.u.n.loose_paragraph));
    NAOS_CHECK_UINT_EQ(D, amark_node_edits(f.u.n.emph));
    CHECK_VALID(f.document);

    amark_document_destroy(f.document);
}

NAOS_SUITE(structure_records,
    NAOS_ADD_TEST(a_move_records_both_parents_and_the_node),
    NAOS_ADD_TEST(a_new_node_is_created_not_relocated),
    NAOS_ADD_TEST(unlink_replace_and_destroy_record_the_parent),
    NAOS_ADD_TEST(nothing_is_recorded_while_tracking_is_off),
    NAOS_ADD_TEST(records_reach_the_new_ancestors_even_while_tracking_is_off))

NAOS_MAIN(
    NAOS_ADD_SUITE(structure_edit),
    NAOS_ADD_SUITE(structure_records)
)
