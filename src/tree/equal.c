/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/* The semantic comparison of subtrees (amark_node_equal), with text normalization. */

#include <amark/node.h>

#include <amark/extension.h>

#include "tree/syntax_set.h"
#include "tree/tree.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Whether node is a text node (by its descriptor, the same in every syntax set). */
static bool is_text(const amark_node_t *node)
{
    return (node->desc == amark_builtin_types[AMARK_NODE_TEXT]);
}

/*
 * Whether a and b, without their children, have the same meaning: the same
 * descriptor, type data (the equal function of the type when it has one, also for
 * a data_size of 0; otherwise the data_size bytes) and literal.
 */
static bool same_node(const amark_node_t *a, const amark_node_t *b)
{
    const amark_node_type_desc_t *desc = a->desc;

    if (desc != b->desc)
    {
        return false;
    }

    if (desc->equal != NULL)
    {
        if (!desc->equal(a, amark_node_data_const(a), b, amark_node_data_const(b)))
        {
            return false;
        }
    }
    else if ((desc->data_size > 0)
             && (memcmp(amark_node_data_const(a), amark_node_data_const(b), desc->data_size)
                 != 0))
    {
        return false;
    }

    return amark_text_equal(a->literal, a->literal_length, b->literal, b->literal_length);
}

/* Skips node and its following siblings while they are empty text nodes: returns
 * the first sibling from node on that is not one (NULL when there is none). */
static const amark_node_t *skip_empty_text(const amark_node_t *node)
{
    while ((node != NULL) && is_text(node) && (node->literal_length == 0))
    {
        node = node->next;
    }

    return node;
}

/* Moves *node and *offset past the end of the text nodes that are exhausted
 * (offset at their length); *node becomes the sibling after the run of text nodes
 * (a node that is not text, or NULL) when the run has no more bytes. Returns
 * whether bytes remain in the run. */
static bool run_advance(const amark_node_t **node, size_t *offset)
{
    while (*offset == (*node)->literal_length)
    {
        *node = (*node)->next;
        *offset = 0;
        if ((*node == NULL) || !is_text(*node))
        {
            return false;
        }
    }

    return true;
}

/*
 * Compares the runs of adjacent text siblings that start at *a and *b (text nodes)
 * as two strings, with a cursor in each run. On equal runs, moves *a and *b to the
 * siblings after the runs (each a node that is not text, or NULL).
 */
static bool same_text_run(const amark_node_t **a, const amark_node_t **b)
{
    const amark_node_t *x = *a;
    const amark_node_t *y = *b;
    size_t x_offset = 0;
    size_t y_offset = 0;

    for (;;)
    {
        bool x_more = run_advance(&x, &x_offset);
        bool y_more = run_advance(&y, &y_offset);
        size_t count;

        if (!x_more || !y_more)
        {
            if (x_more != y_more)
            {
                return false;
            }

            *a = x;
            *b = y;
            return true;
        }

        count = x->literal_length - x_offset;
        if ((y->literal_length - y_offset) < count)
        {
            count = y->literal_length - y_offset;
        }

        if (memcmp(x->literal + x_offset, y->literal + y_offset, count) != 0)
        {
            return false;
        }

        x_offset += count;
        y_offset += count;
    }
}

bool amark_node_equal(const amark_node_t *a, const amark_node_t *b, uint32_t flags)
{

    bool normalize = ((flags & AMARK_EQUAL_NORMALIZE_TEXT) != 0);
    const amark_node_t *parent_a;
    const amark_node_t *parent_b;
    const amark_node_t *x;
    const amark_node_t *y;

    if ((a == NULL) || (b == NULL))
    {
        return false;
    }

    if (!same_node(a, b))
    {
        return false;
    }

    /* Walks the children of parent_a and parent_b side by side: x and y are the
     * next children to compare (NULL after the last one). The two walks go down and
     * up together, so parent_a is a exactly when parent_b is b. */
    parent_a = a;
    parent_b = b;
    x = a->first_child;
    y = b->first_child;
    for (;;)
    {
        if (normalize)
        {
            x = skip_empty_text(x);
            y = skip_empty_text(y);
        }

        if ((x == NULL) || (y == NULL))
        {
            if (x != y)
            {
                return false;
            }

            if (parent_a == a)
            {
                return true;
            }

            x = parent_a->next;
            y = parent_b->next;
            parent_a = parent_a->parent;
            parent_b = parent_b->parent;
            continue;
        }

        if (normalize && is_text(x) && is_text(y))
        {
            if (!same_text_run(&x, &y))
            {
                return false;
            }

            continue;
        }

        if (!same_node(x, y))
        {
            return false;
        }

        parent_a = x;
        parent_b = y;
        x = x->first_child;
        y = y->first_child;
    }
}
