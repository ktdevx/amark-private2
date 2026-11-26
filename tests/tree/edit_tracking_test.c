/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * Edit records (src/tree/edit_tracking.c) and the record of the creation
 * (src/tree/node.c).
 *
 * Suite edit_tracking (7.4, 7.5, 7.7, 7.8, 7.9, 7.10): with tracking off nothing is
 * recorded (creation, amark_edit_mark, amark_node_mark_edited); with tracking on a
 * new node gets AMARK_EDIT_CREATED, amark_edit_mark adds its bits to the node and
 * AMARK_EDIT_DESCENDANTS to every ancestor, stopping at the first ancestor that has
 * it already (counted by stat_propagation_steps), so that repeated edits under
 * the same ancestors cost no more steps; amark_node_mark_edited records
 * AMARK_EDIT_CONTENT; the bits are distinct and below the reserved 0x20.
 * The tree is linked through the internal structure (the structural editing comes
 * with structure_edit.c).
 */
#include "tree/tree.h"

#include <amark/allocator.h>
#include <amark/document.h>
#include <amark/error.h>
#include <amark/node.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

/* A tree built with tracking off: root > quote > paragraph > (first, second), and
 * root > other. */
typedef struct fixture
{
    amark_document_t *document;
    amark_node_t *root;
    amark_node_t *quote;
    amark_node_t *paragraph;
    amark_node_t *first;
    amark_node_t *second;
    amark_node_t *other;
} fixture_t;

static void build(fixture_t *f)
{
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &f->document));
    f->root = amark_document_root(f->document);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(f->document, AMARK_NODE_BLOCK_QUOTE, &f->quote));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(f->document, AMARK_NODE_PARAGRAPH, &f->paragraph));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(f->document, AMARK_NODE_TEXT, &f->first));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(f->document, AMARK_NODE_TEXT, &f->second));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(f->document, AMARK_NODE_THEMATIC_BREAK, &f->other));
    link_last(f->root, f->quote);
    link_last(f->quote, f->paragraph);
    link_last(f->paragraph, f->first);
    link_last(f->paragraph, f->second);
    link_last(f->root, f->other);
}

NAOS_TEST(the_bits_are_distinct_and_below_the_reserved_ones)
{
    static const uint32_t bits[] = { AMARK_EDIT_CONTENT, AMARK_EDIT_CHILDREN,
                                     AMARK_EDIT_DESCENDANTS, AMARK_EDIT_RELOCATED,
                                     AMARK_EDIT_CREATED };
    uint32_t all = 0;
    size_t i;

    for (i = 0; i < (sizeof(bits) / sizeof(bits[0])); i++)
    {
        NAOS_CHECK_UINT_EQ(0, all & bits[i]);
        NAOS_CHECK_TRUE((bits[i] & (bits[i] - 1)) == 0);
        all |= bits[i];
    }

    NAOS_CHECK_UINT_EQ(0x1Fu, all);
}

NAOS_TEST(nothing_is_recorded_while_tracking_is_off)
{
    fixture_t f;

    build(&f);

    NAOS_CHECK_FALSE(amark_document_edit_tracking(f.document));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.first));
    amark_edit_mark(f.first, AMARK_EDIT_CONTENT | AMARK_EDIT_CHILDREN);
    amark_node_mark_edited(f.second);
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.first));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.second));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.paragraph));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.quote));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.root));
    NAOS_CHECK_UINT_EQ(0, f.document->stat_propagation_steps);

    /* Turning tracking on and off again records nothing either. */
    amark_document_set_edit_tracking(f.document, true);
    amark_document_set_edit_tracking(f.document, false);
    amark_node_mark_edited(f.first);
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.first));

    amark_document_destroy(f.document);
}

NAOS_TEST(a_node_created_while_tracking_is_created)
{
    amark_document_t *document = NULL;
    amark_node_t *before = NULL;
    amark_node_t *during = NULL;
    amark_node_t *after = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, AMARK_NODE_TEXT, &before));
    amark_document_set_edit_tracking(document, true);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, AMARK_NODE_TEXT, &during));
    amark_document_set_edit_tracking(document, false);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, AMARK_NODE_TEXT, &after));

    NAOS_CHECK_UINT_EQ(0, amark_node_edits(before));
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_CREATED, amark_node_edits(during));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(after));
    /* The root, made with tracking off, and a detached node: nothing propagates. */
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(amark_document_root(document)));

    amark_document_destroy(document);
}

NAOS_TEST(a_mark_propagates_descendants_to_every_ancestor)
{
    fixture_t f;

    build(&f);
    amark_document_set_edit_tracking(f.document, true);

    amark_edit_mark(f.first, AMARK_EDIT_CONTENT);
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_CONTENT, amark_node_edits(f.first));
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_DESCENDANTS, amark_node_edits(f.paragraph));
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_DESCENDANTS, amark_node_edits(f.quote));
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_DESCENDANTS, amark_node_edits(f.root));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.second));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.other));
    NAOS_CHECK_UINT_EQ(3, f.document->stat_propagation_steps);

    /* The bits add up. */
    amark_edit_mark(f.first, AMARK_EDIT_RELOCATED);
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_CONTENT | AMARK_EDIT_RELOCATED, amark_node_edits(f.first));
    amark_edit_mark(f.paragraph, AMARK_EDIT_CHILDREN);
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_DESCENDANTS | AMARK_EDIT_CHILDREN,
                       amark_node_edits(f.paragraph));

    /* The other branch goes up to the root, which has the bit already. */
    amark_edit_mark(f.other, AMARK_EDIT_CONTENT);
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_CONTENT, amark_node_edits(f.other));
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_DESCENDANTS, amark_node_edits(f.root));

    amark_document_destroy(f.document);
}

NAOS_TEST(propagation_stops_at_an_ancestor_with_descendants)
{
    fixture_t f;
    size_t steps;

    build(&f);
    amark_document_set_edit_tracking(f.document, true);

    /* An ancestor that has the bit stops the walk, even when the ones above do not
     * (the walk does not look past it). */
    f.paragraph->edits = AMARK_EDIT_DESCENDANTS;
    amark_edit_mark(f.second, AMARK_EDIT_CONTENT);
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_CONTENT, amark_node_edits(f.second));
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_DESCENDANTS, amark_node_edits(f.paragraph));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.quote));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.root));
    NAOS_CHECK_UINT_EQ(0, f.document->stat_propagation_steps);

    /* Marking the paragraph itself takes it up to the root once; then the edits
     * under it take no step. */
    amark_edit_mark(f.paragraph, AMARK_EDIT_CHILDREN);
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_DESCENDANTS, amark_node_edits(f.quote));
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_DESCENDANTS, amark_node_edits(f.root));
    steps = f.document->stat_propagation_steps;
    NAOS_CHECK_UINT_EQ(2, steps);
    amark_edit_mark(f.first, AMARK_EDIT_CONTENT);
    amark_node_mark_edited(f.second);
    amark_node_mark_edited(f.first);
    NAOS_CHECK_UINT_EQ(steps, f.document->stat_propagation_steps);

    amark_document_destroy(f.document);
}

NAOS_TEST(no_bits_record_nothing)
{
    fixture_t f;

    build(&f);
    amark_document_set_edit_tracking(f.document, true);

    amark_edit_mark(f.first, 0);
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.first));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.paragraph));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.root));

    amark_document_destroy(f.document);
}

NAOS_TEST(mark_edited_records_content)
{
    fixture_t f;

    build(&f);
    amark_document_set_edit_tracking(f.document, true);

    amark_node_mark_edited(f.quote);
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_CONTENT, amark_node_edits(f.quote));
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_DESCENDANTS, amark_node_edits(f.root));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.paragraph));

    /* A detached node records on itself alone. */
    f.other->parent = NULL;
    f.quote->next = NULL;
    f.root->last_child = f.quote;
    amark_node_mark_edited(f.other);
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_CONTENT, amark_node_edits(f.other));

    amark_document_destroy(f.document);
}

NAOS_SUITE(edit_tracking,
    NAOS_ADD_TEST(the_bits_are_distinct_and_below_the_reserved_ones),
    NAOS_ADD_TEST(nothing_is_recorded_while_tracking_is_off),
    NAOS_ADD_TEST(a_node_created_while_tracking_is_created),
    NAOS_ADD_TEST(a_mark_propagates_descendants_to_every_ancestor),
    NAOS_ADD_TEST(propagation_stops_at_an_ancestor_with_descendants),
    NAOS_ADD_TEST(no_bits_record_nothing),
    NAOS_ADD_TEST(mark_edited_records_content))

NAOS_MAIN(
    NAOS_ADD_SUITE(edit_tracking)
)
