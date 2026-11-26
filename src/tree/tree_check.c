/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/* The check of the tree invariants (amark_tree_check), for the tests and the fuzz
 * targets. */

#include "tree/tree.h"

#include <amark/document.h>
#include <amark/error.h>
#include <amark/node.h>

#include <stdbool.h>
#include <stddef.h>

/*
 * Checks the ring of all the nodes, from document->ring along ring_next: each
 * node belongs to document and the next node links back to it. Every ring_prev is
 * checked as the ring_prev of the node before it, so a walk whose checks all pass
 * comes back to the start (ring_next is one to one on the nodes walked) and stops.
 * Stores the number of nodes in *out_count.
 */
static const char *check_ring(const amark_document_t *document, size_t *out_count)
{
    const amark_node_t *node = document->ring;
    size_t count = 0;
    bool root_found = false;

    *out_count = 0;
    if (document->root == NULL)
    {
        return "the document has no root";
    }

    if (node == NULL)
    {
        return "the root is not in the ring";
    }

    do
    {
        if (node->document != document)
        {
            return "a node of the ring belongs to another document";
        }

        if ((node->ring_next == NULL) || (node->ring_next->ring_prev != node))
        {
            return "the ring links are not symmetric";
        }

        if (node == document->root)
        {
            root_found = true;
        }

        count++;
        node = node->ring_next;
    }
    while (node != document->ring);

    if (!root_found)
    {
        return "the root is not in the ring";
    }

    *out_count = count;
    return NULL;
}

/* Checks the links around node that it can check by itself: those of its parent
 * that end at it, those of its siblings, and the ends of its children. */
static const char *check_links(const amark_node_t *node)
{
    const amark_node_t *parent = node->parent;

    if (parent == NULL)
    {
        if ((node->prev != NULL) || (node->next != NULL))
        {
            return "the sibling links are not symmetric";
        }
    }
    else
    {
        if (node->prev == NULL)
        {
            if (parent->first_child != node)
            {
                return "the parent and child links are not symmetric";
            }
        }
        else if ((node->prev->next != node) || (node->prev->parent != parent))
        {
            return "the sibling links are not symmetric";
        }

        if (node->next == NULL)
        {
            if (parent->last_child != node)
            {
                return "the parent and child links are not symmetric";
            }
        }
        else if ((node->next->prev != node) || (node->next->parent != parent))
        {
            return "the sibling links are not symmetric";
        }
    }

    if ((node->first_child == NULL) != (node->last_child == NULL))
    {
        return "the parent and child links are not symmetric";
    }

    if ((node->first_child != NULL)
        && ((node->first_child->parent != node) || (node->first_child->prev != NULL)
            || (node->last_child->parent != node) || (node->last_child->next != NULL)))
    {
        return "the parent and child links are not symmetric";
    }

    return NULL;
}

/* Checks the tokens of node: sorted by (source, offset, tab_column_offset), each
 * within a registered source. Equal keys keep the order of addition, which leaves
 * no trace to check. */
static const char *check_tokens(const amark_document_t *document, const amark_node_t *node)
{
    size_t i;

    if (node->token_count > node->token_capacity)
    {
        return "a node has more tokens than its capacity";
    }

    if ((node->token_count != 0) && (node->tokens == NULL))
    {
        return "a node has tokens but no token array";
    }

    for (i = 0; i < node->token_count; i++)
    {
        const amark_token_record_t *token = &node->tokens[i];
        size_t source_length;

        if (token->source >= document->source_count)
        {
            return "a token refers to a source that is not registered";
        }

        /* Without an overflow: offset + length <= source_length. */
        source_length = document->sources[token->source].bytes.length;
        if ((token->offset > source_length) || (token->length > (source_length - token->offset)))
        {
            return "a token is out of the range of its source";
        }

        if (i > 0)
        {
            const amark_token_record_t *prev = &node->tokens[i - 1];

            if ((prev->source > token->source)
                || ((prev->source == token->source) && (prev->offset > token->offset))
                || ((prev->source == token->source) && (prev->offset == token->offset)
                    && (prev->tab_column_offset > token->tab_column_offset)))
            {
                return "the tokens of a node are not sorted";
            }
        }
    }

    return NULL;
}

/* Checks what each node of a subtree must satisfy: it belongs to document, is in
 * the ring (its ring neighbors link back to it), satisfies the containment rule
 * and the closure of AMARK_EDIT_DESCENDANTS with its parent, and has valid tokens. */
static const char *check_node(const amark_document_t *document, const amark_node_t *node)
{
    const amark_node_t *parent = node->parent;

    if (node->document != document)
    {
        return "a node of the tree belongs to another document";
    }

    if ((node->ring_next == NULL) || (node->ring_prev == NULL)
        || (node->ring_next->ring_prev != node) || (node->ring_prev->ring_next != node))
    {
        return "a node of the tree is not in the ring";
    }

    if (node->desc == NULL)
    {
        return "a node has no type descriptor";
    }

    if (parent != NULL)
    {
        if (!amark_node_allows_child(parent, node->desc))
        {
            return "a child violates the containment rule of its parent";
        }

        /* Every node with records has AMARK_EDIT_DESCENDANTS on all its ancestors:
         * by induction, it is enough that each parent of a node with records has
         * it (the parent then has records too). */
        if ((node->edits != 0) && ((parent->edits & AMARK_EDIT_DESCENDANTS) == 0))
        {
            return "an ancestor of a node with edit records lacks AMARK_EDIT_DESCENDANTS";
        }
    }

    return check_tokens(document, node);
}

/*
 * Checks the subtree of top, a node without a parent, in preorder without
 * recursion, and adds the number of its nodes to *visited. Each child is entered
 * only through a link that it links back to, so no node is visited twice; the
 * walk stops with an error rather than visit more than limit nodes in all.
 */
static const char *check_subtree(const amark_document_t *document, const amark_node_t *top,
                                 size_t limit, size_t *visited)
{
    const amark_node_t *node = top;

    for (;;)
    {
        const char *reason;

        if (*visited == limit)
        {
            return "a node of the tree is not in the ring";
        }

        (*visited)++;
        reason = check_node(document, node);
        if (reason != NULL)
        {
            return reason;
        }

        if (node->first_child != NULL)
        {
            if ((node->first_child->parent != node) || (node->first_child->prev != NULL))
            {
                return "the parent and child links are not symmetric";
            }

            node = node->first_child;
            continue;
        }

        /* Go to the next sibling of the nearest node, up to top, that has one. */
        while (node != top)
        {
            const amark_node_t *parent = node->parent;

            if (node->next != NULL)
            {
                if ((node->next->prev != node) || (node->next->parent != parent))
                {
                    return "the sibling links are not symmetric";
                }

                node = node->next;
                break;
            }

            if (parent->last_child != node)
            {
                return "the parent and child links are not symmetric";
            }

            node = parent;
        }

        if (node == top)
        {
            return NULL;
        }
    }
}

/* The checks, in the order: the ring, the links around each node of the ring, the
 * subtree of each node of the ring without a parent, and that these subtrees hold
 * every node of the ring. Each node is looked at a constant number of times. */
static const char *check_document(const amark_document_t *document)
{
    const amark_node_t *node;
    const char *reason;
    size_t count;
    size_t visited = 0;

    reason = check_ring(document, &count);
    if (reason != NULL)
    {
        return reason;
    }

    if ((document->root->parent != NULL) || (document->root->type != AMARK_NODE_DOCUMENT))
    {
        return "the root is not a document node without a parent";
    }

    node = document->ring;
    do
    {
        reason = check_links(node);
        if (reason != NULL)
        {
            return reason;
        }

        node = node->ring_next;
    }
    while (node != document->ring);

    do
    {
        if (node->parent == NULL)
        {
            reason = check_subtree(document, node, count, &visited);
            if (reason != NULL)
            {
                return reason;
            }
        }

        node = node->ring_next;
    }
    while (node != document->ring);

    if (visited != count)
    {
        return "a node of the ring is not in the subtree of a node without a parent";
    }

    return NULL;
}

amark_error_t amark_tree_check(const amark_document_t *document, const char **out_reason)
{
    const char *reason;

    if (out_reason != NULL)
    {
        *out_reason = NULL;
    }

    if (document == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    reason = check_document(document);
    if (reason == NULL)
    {
        return AMARK_ERROR_NONE;
    }

    if (out_reason != NULL)
    {
        *out_reason = reason;
    }

    return AMARK_ERROR_INVALID_STATE;
}
