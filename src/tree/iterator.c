/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * Iterator: the traversal with enter and exit events (4.2-4.4).
 *
 * The iterator holds only its root, the current node and the current event;
 * each step computes the next event from the links of the tree at that
 * moment, so edits of the current node's content and children, and of the
 * nodes other than the current node and its ancestors, are followed (4.3).
 * The walk uses no recursion and no stack.
 */
#include "tree/tree.h"

#include <amark/allocator.h>

#include <stdbool.h>
#include <stddef.h>

struct amark_iter
{
    /* The root of the traversal: it is never left. */
    amark_node_t *root;
    /* The current node and event; node is NULL before the first event. */
    amark_node_t *node;
    amark_event_t event;
};

/* Whether the type of node can have children (it accepts block, list item or
 * inline children): such a node has an enter and an exit event. */
static bool can_have_children(const amark_node_t *node)
{
    return node->desc->accepts != 0;
}

/* Whether node is root or one of its descendants. */
static bool in_subtree(const amark_node_t *root, const amark_node_t *node)
{
    while (node != NULL)
    {
        if (node == root)
        {
            return true;
        }
        node = node->parent;
    }
    return false;
}

amark_error_t amark_iter_create(amark_node_t *root, amark_iter_t **out)
{
    amark_iter_t *iter;

    if (out == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    *out = NULL;
    if (root == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    iter = (amark_iter_t *)amark_malloc(sizeof(*iter));
    if (iter == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    iter->root = root;
    iter->node = NULL;
    iter->event = AMARK_EVENT_DONE;
    *out = iter;
    return AMARK_ERROR_NONE;
}

void amark_iter_destroy(amark_iter_t *iter)
{
    amark_free(iter);
}

amark_event_t amark_iter_next(amark_iter_t *iter)
{
    amark_node_t *node;

    if (iter == NULL)
    {
        return AMARK_EVENT_DONE;
    }
    node = iter->node;
    if (node == NULL)
    {
        /* The first event. */
        iter->node = iter->root;
        iter->event = AMARK_EVENT_ENTER;
        return iter->event;
    }
    if (iter->event == AMARK_EVENT_DONE)
    {
        return AMARK_EVENT_DONE;
    }
    if (iter->event == AMARK_EVENT_ENTER && can_have_children(node))
    {
        /* Into the children as they are now, or straight to the exit. */
        if (node->first_child != NULL)
        {
            iter->node = node->first_child;
            iter->event = AMARK_EVENT_ENTER;
        }
        else
        {
            iter->event = AMARK_EVENT_EXIT;
        }
        return iter->event;
    }
    /* The subtree of node is over: its next sibling, else the exit of its
     * parent, without leaving the root. A node no longer under the root (not
     * supported) ends the traversal when its ancestors run out. */
    if (node == iter->root || (node->next == NULL && node->parent == NULL))
    {
        iter->node = iter->root;
        iter->event = AMARK_EVENT_DONE;
    }
    else if (node->next != NULL)
    {
        iter->node = node->next;
        iter->event = AMARK_EVENT_ENTER;
    }
    else
    {
        iter->node = node->parent;
        iter->event = AMARK_EVENT_EXIT;
    }
    return iter->event;
}

amark_node_t *amark_iter_node(const amark_iter_t *iter)
{
    return iter != NULL ? iter->node : NULL;
}

amark_error_t amark_iter_reset(amark_iter_t *iter, amark_node_t *node, amark_event_t event)
{
    if (iter == NULL || node == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    if (event != AMARK_EVENT_ENTER && (event != AMARK_EVENT_EXIT || !can_have_children(node)))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    if (!in_subtree(iter->root, node))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    iter->node = node;
    iter->event = event;
    return AMARK_ERROR_NONE;
}
