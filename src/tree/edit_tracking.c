/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/* Edit records and their propagation to the ancestors (amark_edit_mark). */

#include <amark/node.h>

#include "tree/tree.h"

#include <stdint.h>

void amark_edit_mark(amark_node_t *node, uint32_t bits)
{
    amark_document_t *document = node->document;
    amark_node_t *ancestor;

    if (!document->tracking || (bits == 0))
    {
        return;
    }

    node->edits |= bits;

    /* Every ancestor of a node with AMARK_EDIT_DESCENDANTS has it already, so the
     * walk stops at the first one that has it: over a sequence of edits, each
     * ancestor gets the bit once. */
    for (ancestor = node->parent;
         (ancestor != NULL) && ((ancestor->edits & AMARK_EDIT_DESCENDANTS) == 0);
         ancestor = ancestor->parent)
    {
        ancestor->edits |= AMARK_EDIT_DESCENDANTS;
        document->stat_propagation_steps++;
    }
}

uint32_t amark_node_edits(const amark_node_t *node)
{
    return (node != NULL) ? node->edits : 0;
}

void amark_node_mark_edited(amark_node_t *node)
{
    if (node != NULL)
    {
        amark_edit_mark(node, AMARK_EDIT_CONTENT);
    }
}
