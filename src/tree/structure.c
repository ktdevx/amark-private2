/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * NodeCore: the insertion, unlinking and replacement of nodes, and the
 * containment and cycle checks.
 */
#include "tree/tree.h"

/* Where a node is placed. */
typedef enum place
{
    PLACE_BEFORE,
    PLACE_AFTER,
    PLACE_FIRST,
    PLACE_LAST
} place_t;

/* Whether the subtree of node holds a link (node included). The walk follows
 * the links, without recursion or stack. */
static bool subtree_has_link(amark_node_t *node)
{
    amark_document_t *document = node->document;
    amark_node_t *current = node;

    for (;;)
    {
        document->stats.structure_steps++;
        if (current->type == AMARK_NODE_LINK)
        {
            return true;
        }
        if (current->first_child != NULL)
        {
            current = current->first_child;
            continue;
        }
        while (current != node && current->next == NULL)
        {
            current = current->parent;
        }
        if (current == node)
        {
            return false;
        }
        current = current->next;
    }
}

/*
 * Checks that node, of the same document, can become a child of parent:
 * node is not the document node, parent accepts its type, node is neither
 * parent nor one of its ancestors, and no link would get a link among its
 * descendants (a link among parent and its ancestors, and a link in the
 * subtree of node). The walk goes from parent up to the top of its tree, so it
 * is proportional to the depth of the insertion point; the subtree of node is
 * walked only under a link.
 */
static amark_error_t check_place(amark_node_t *parent, amark_node_t *node)
{
    amark_document_t *document = node->document;
    amark_node_t *ancestor;
    bool under_link = false;

    if (node == document->root ||
        !amark_registry_can_contain(document->set, parent, parent->type, node->type))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    for (ancestor = parent; ancestor != NULL; ancestor = ancestor->parent)
    {
        document->stats.structure_steps++;
        if (ancestor == node)
        {
            return AMARK_ERROR_INVALID_ARGUMENT;
        }
        if (ancestor->type == AMARK_NODE_LINK)
        {
            under_link = true;
        }
    }
    if (under_link && subtree_has_link(node))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    return AMARK_ERROR_NONE;
}

/* Links node, which has no parent, under parent between prev and next (adjacent
 * children of parent, or NULL at the ends), and notifies the change. */
static void link_between(amark_node_t *parent, amark_node_t *prev, amark_node_t *next,
                         amark_node_t *node)
{
    node->parent = parent;
    node->prev = prev;
    node->next = next;
    if (prev != NULL)
    {
        prev->next = node;
    }
    else
    {
        parent->first_child = node;
    }
    if (next != NULL)
    {
        next->prev = node;
    }
    else
    {
        parent->last_child = node;
    }
    /* The moved node itself is recomputed too; then the new place. */
    node->cache_flags &= ~AMARK_TREE_CACHE_VALID;
    amark_tree_changed(parent, AMARK_TREE_CHANGE_STRUCTURE);
}

/* Places node relative to target: before or after the anchor target, or as the
 * first or the last child of the parent target. */
static amark_error_t place_node(amark_node_t *target, place_t where, amark_node_t *node)
{
    amark_node_t *parent;
    amark_node_t *prev;
    amark_node_t *next;
    amark_error_t error;

    if (target == NULL || node == NULL || target->document != node->document)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    if (where == PLACE_BEFORE || where == PLACE_AFTER)
    {
        parent = target->parent;
        if (parent == NULL)
        {
            return AMARK_ERROR_INVALID_ARGUMENT;
        }
    }
    else
    {
        parent = target;
    }
    error = check_place(parent, node);
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    if (node == target)
    {
        /* Before or after itself: already there. */
        return AMARK_ERROR_NONE;
    }

    /* Commit: remove node from its place, then find the neighbours, which may
     * have changed when node was next to the anchor. */
    amark_tree_node_detach(node);
    switch (where)
    {
    case PLACE_BEFORE:
        prev = target->prev;
        next = target;
        break;
    case PLACE_AFTER:
        prev = target;
        next = target->next;
        break;
    case PLACE_FIRST:
        prev = NULL;
        next = parent->first_child;
        break;
    default:
        prev = parent->last_child;
        next = NULL;
        break;
    }
    link_between(parent, prev, next, node);
    return AMARK_ERROR_NONE;
}

amark_error_t amark_node_unlink(amark_node_t *node)
{
    if (node == NULL || node == node->document->root)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    amark_tree_node_detach(node);
    return AMARK_ERROR_NONE;
}

amark_error_t amark_node_insert_before(amark_node_t *anchor, amark_node_t *node)
{
    return place_node(anchor, PLACE_BEFORE, node);
}

amark_error_t amark_node_insert_after(amark_node_t *anchor, amark_node_t *node)
{
    return place_node(anchor, PLACE_AFTER, node);
}

amark_error_t amark_node_prepend_child(amark_node_t *parent, amark_node_t *node)
{
    return place_node(parent, PLACE_FIRST, node);
}

amark_error_t amark_node_append_child(amark_node_t *parent, amark_node_t *node)
{
    return place_node(parent, PLACE_LAST, node);
}

amark_error_t amark_node_replace(amark_node_t *old_node, amark_node_t *new_node)
{
    amark_node_t *parent;
    amark_node_t *prev;
    amark_node_t *next;
    amark_error_t error;

    if (old_node == NULL || new_node == NULL || old_node->document != new_node->document)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    parent = old_node->parent;
    if (parent == NULL)
    {
        /* The document node, or a node without a position. */
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    error = check_place(parent, new_node);
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    if (new_node == old_node)
    {
        return AMARK_ERROR_NONE;
    }

    /* Commit: remove new_node from its place (maybe next to old_node, or under
     * it), then take the place of old_node. */
    amark_tree_node_detach(new_node);
    prev = old_node->prev;
    next = old_node->next;
    old_node->parent = NULL;
    old_node->prev = NULL;
    old_node->next = NULL;
    link_between(parent, prev, next, new_node);
    return AMARK_ERROR_NONE;
}
