/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/* Structural editing: insertion, unlinking, replacement, public destruction,
 * containment rule, cycle check. */

#include <amark/node.h>

#include <amark/document.h>
#include <amark/error.h>
#include <amark/extension.h>

#include "tree/tree.h"

#include <stdbool.h>
#include <stddef.h>

/* --- Containment rule ------------------------------------------------------------ */

bool amark_node_allows_child(const amark_node_t *parent, const amark_node_type_desc_t *child_desc)
{
    if (parent->desc->can_contain != NULL)
    {
        return parent->desc->can_contain(parent->desc, child_desc);
    }

    return (parent->desc->accepts & child_desc->provides) != 0;
}

/* --- Links and records ------------------------------------------------------------ */

/* Detaches node, which has a parent, and records AMARK_EDIT_CHILDREN on the
 * parent. */
static void detach(amark_node_t *node)
{
    amark_node_t *parent = node->parent;

    if (node->prev != NULL)
    {
        node->prev->next = node->next;
    }
    else
    {
        parent->first_child = node->next;
    }

    if (node->next != NULL)
    {
        node->next->prev = node->prev;
    }
    else
    {
        parent->last_child = node->prev;
    }

    node->parent = NULL;
    node->prev = NULL;
    node->next = NULL;
    amark_edit_mark(parent, AMARK_EDIT_CHILDREN);
}

void amark_node_link_child(amark_node_t *parent, amark_node_t *before, amark_node_t *node)
{
    node->parent = parent;
    node->next = before;
    if (before != NULL)
    {
        node->prev = before->prev;
        before->prev = node;
    }
    else
    {
        node->prev = parent->last_child;
        parent->last_child = node;
    }

    if (node->prev != NULL)
    {
        node->prev->next = node;
    }
    else
    {
        parent->first_child = node;
    }

    amark_edit_mark(parent, AMARK_EDIT_CHILDREN);
    if ((node->edits & AMARK_EDIT_CREATED) == 0)
    {
        amark_edit_mark(node, AMARK_EDIT_RELOCATED);
    }

    /* With tracking on, the marks above have done it already (and this stops at
     * once); with tracking off, the records the subtree had still reach the new
     * ancestors. */
    if (node->edits != 0)
    {
        amark_edit_propagate_descendants(node);
    }
}

/* --- Checks ----------------------------------------------------------------------- */

/* Checks 1 and the first half of 2: node belongs to the document of anchor and is
 * not the root. */
static amark_error_t check_node(const amark_node_t *anchor, const amark_node_t *node)
{
    if ((anchor->document != node->document) || (node == node->document->root))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    return AMARK_ERROR_NONE;
}

/*
 * Checks 3 and 4 for node as a child of parent: the containment rule, and that node
 * is not parent or one of its ancestors. Only a node with children can be an
 * ancestor of parent other than parent itself, so the ancestors are walked only
 * then (each step counted by stat_cycle_steps): inserting a node without children
 * takes constant time whatever the depth.
 */
static amark_error_t check_placement(const amark_node_t *parent, const amark_node_t *node)
{
    const amark_node_t *ancestor;

    if (!amark_node_allows_child(parent, node->desc) || (parent == node))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    if (node->first_child == NULL)
    {
        return AMARK_ERROR_NONE;
    }

    for (ancestor = parent; ancestor != NULL; ancestor = ancestor->parent)
    {
        parent->document->stat_cycle_steps++;
        if (ancestor == node)
        {
            return AMARK_ERROR_INVALID_ARGUMENT;
        }
    }

    return AMARK_ERROR_NONE;
}

/* The checks of an insertion next to sibling. */
static amark_error_t check_sibling_insertion(const amark_node_t *sibling, const amark_node_t *node)
{
    amark_error_t err;

    if ((sibling == NULL) || (node == NULL))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    err = check_node(sibling, node);
    if (err != AMARK_ERROR_NONE)
    {
        return err;
    }

    if (sibling->parent == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    return check_placement(sibling->parent, node);
}

/* The checks of an insertion as a child of parent. */
static amark_error_t check_child_insertion(const amark_node_t *parent, const amark_node_t *node)
{
    amark_error_t err;

    if ((parent == NULL) || (node == NULL))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    err = check_node(parent, node);
    if (err != AMARK_ERROR_NONE)
    {
        return err;
    }

    return check_placement(parent, node);
}

/* --- Public editing --------------------------------------------------------------- */

amark_error_t amark_node_unlink(amark_node_t *node)
{
    if ((node == NULL) || (node == node->document->root))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    if (node->parent != NULL)
    {
        detach(node);
    }

    return AMARK_ERROR_NONE;
}

void amark_node_destroy(amark_node_t *node)
{
    if ((node == NULL) || (node == node->document->root))
    {
        return;
    }

    if (node->parent != NULL)
    {
        detach(node);
    }

    amark_node_destroy_subtree(node);
}

amark_error_t amark_node_insert_before(amark_node_t *sibling, amark_node_t *node)
{
    amark_error_t err;

    err = check_sibling_insertion(sibling, node);
    if ((err != AMARK_ERROR_NONE) || (sibling == node))
    {
        return err;
    }

    /* node is not an ancestor of sibling, so taking it out leaves sibling where it
     * is. */
    if (node->parent != NULL)
    {
        detach(node);
    }

    amark_node_link_child(sibling->parent, sibling, node);
    return AMARK_ERROR_NONE;
}

amark_error_t amark_node_insert_after(amark_node_t *sibling, amark_node_t *node)
{
    amark_error_t err;

    err = check_sibling_insertion(sibling, node);
    if ((err != AMARK_ERROR_NONE) || (sibling == node))
    {
        return err;
    }

    /* The next sibling is read after node is taken out (node may be it). */
    if (node->parent != NULL)
    {
        detach(node);
    }

    amark_node_link_child(sibling->parent, sibling->next, node);
    return AMARK_ERROR_NONE;
}

amark_error_t amark_node_prepend_child(amark_node_t *parent, amark_node_t *node)
{
    amark_error_t err;

    err = check_child_insertion(parent, node);
    if (err != AMARK_ERROR_NONE)
    {
        return err;
    }

    /* The first child is read after node is taken out (node may be it). */
    if (node->parent != NULL)
    {
        detach(node);
    }

    amark_node_link_child(parent, parent->first_child, node);
    return AMARK_ERROR_NONE;
}

amark_error_t amark_node_append_child(amark_node_t *parent, amark_node_t *node)
{
    amark_error_t err;

    err = check_child_insertion(parent, node);
    if (err != AMARK_ERROR_NONE)
    {
        return err;
    }

    if (node->parent != NULL)
    {
        detach(node);
    }

    amark_node_link_child(parent, NULL, node);
    return AMARK_ERROR_NONE;
}

amark_error_t amark_node_replace(amark_node_t *old_node, amark_node_t *new_node)
{
    amark_node_t *parent;
    amark_node_t *before;
    amark_error_t err;

    /* The same checks as an insertion before old_node: a detached old_node (the
     * root included) has no parent. */
    err = check_sibling_insertion(old_node, new_node);
    if ((err != AMARK_ERROR_NONE) || (old_node == new_node))
    {
        return err;
    }

    /* new_node is not an ancestor of old_node, so taking it out (from anywhere,
     * the subtree of old_node included) leaves old_node under the same parent;
     * the place of old_node is read after that (new_node may be next to it). */
    if (new_node->parent != NULL)
    {
        detach(new_node);
    }

    parent = old_node->parent;
    before = old_node->next;
    detach(old_node);
    amark_node_link_child(parent, before, new_node);
    return AMARK_ERROR_NONE;
}
