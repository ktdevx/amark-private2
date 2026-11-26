/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/* The traversal of a subtree with enter and exit events. */

#include <amark/iterator.h>

#include <amark/allocator.h>
#include <amark/error.h>
#include <amark/node.h>

#include "tree/tree.h"

#include <stdbool.h>
#include <stddef.h>

/* An iterator: the root, the current node and the current event, nothing else.
 * Before the first event, node is NULL and event AMARK_EVENT_NONE; at the end,
 * node is NULL and event AMARK_EVENT_DONE. */
struct amark_iter
{
    amark_node_t *root;
    amark_node_t *node;
    amark_event_t event;
};

amark_error_t amark_iter_create(amark_node_t *root, amark_iter_t **out_iter)
{
    amark_iter_t *iter;

    if (out_iter == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    *out_iter = NULL;
    if (root == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    iter = amark_malloc(sizeof(*iter));
    if (iter == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }

    iter->root = root;
    iter->node = NULL;
    iter->event = AMARK_EVENT_NONE;
    *out_iter = iter;

    return AMARK_ERROR_NONE;
}

void amark_iter_destroy(amark_iter_t *iter)
{
    amark_free(iter);
}

/* Moves iter to (node, event). */
static amark_event_t iter_move(amark_iter_t *iter, amark_node_t *node, amark_event_t event)
{
    iter->node = node;
    iter->event = event;

    return event;
}

amark_event_t amark_iter_next(amark_iter_t *iter)
{
    amark_node_t *node;

    if (iter == NULL)
    {
        return AMARK_EVENT_DONE;
    }

    node = iter->node;
    switch (iter->event)
    {
    case AMARK_EVENT_NONE:
        return iter_move(iter, iter->root, AMARK_EVENT_ENTER);
    case AMARK_EVENT_ENTER:
        if (amark_node_can_have_children(node))
        {
            if (node->first_child != NULL)
            {
                return iter_move(iter, node->first_child, AMARK_EVENT_ENTER);
            }

            return iter_move(iter, node, AMARK_EVENT_EXIT);
        }

        break;
    case AMARK_EVENT_EXIT:
        break;
    case AMARK_EVENT_DONE:
    default:
        return AMARK_EVENT_DONE;
    }

    /* An exit, or the enter of a leaf: the next sibling, else the exit of the
     * parent, never past the root. */
    if (node == iter->root)
    {
        return iter_move(iter, NULL, AMARK_EVENT_DONE);
    }

    if (node->next != NULL)
    {
        return iter_move(iter, node->next, AMARK_EVENT_ENTER);
    }

    if (node->parent != NULL)
    {
        return iter_move(iter, node->parent, AMARK_EVENT_EXIT);
    }

    return iter_move(iter, NULL, AMARK_EVENT_DONE);
}

amark_node_t *amark_iter_node(const amark_iter_t *iter)
{
    return (iter != NULL) ? iter->node : NULL;
}

amark_event_t amark_iter_event(const amark_iter_t *iter)
{
    return (iter != NULL) ? iter->event : AMARK_EVENT_NONE;
}

amark_node_t *amark_iter_root(const amark_iter_t *iter)
{
    return (iter != NULL) ? iter->root : NULL;
}

/* Returns whether node is root or one of its descendants. */
static bool iter_in_subtree(const amark_node_t *root, const amark_node_t *node)
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

amark_error_t amark_iter_reset(amark_iter_t *iter, amark_node_t *node, amark_event_t event)
{
    if ((iter == NULL) || (node == NULL))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    if ((event != AMARK_EVENT_ENTER) && (event != AMARK_EVENT_EXIT))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    if ((event == AMARK_EVENT_EXIT) && !amark_node_can_have_children(node))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    if (!iter_in_subtree(iter->root, node))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    iter->node = node;
    iter->event = event;

    return AMARK_ERROR_NONE;
}
