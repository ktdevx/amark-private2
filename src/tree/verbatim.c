/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * Verbatim: the spelling state, whether the original spelling can be output
 * and its cache, and the derivation of tightness.
 */
#include "tree/tree.h"

#include <amark/allocator.h>

#include <string.h>

/* ---- Spelling state and new nodes ---------------------------------------------- */

/* Whether node has a record other than its line range: a margin, a blank line, a
 * layout token or laid content. */
static bool has_records(const amark_node_t *node)
{
    const amark_tree_records_t *records = node->records;

    if ((records != NULL) && ((records->margin_count > 0) || (records->blank_line_count > 0) ||
                              (records->token_count > 0)))
    {
        return true;
    }
    return (node->content != NULL) && (node->content->state == AMARK_CONTENT_LAID);
}

static bool has_content_tokens(const amark_node_t *node)
{
    return (node->content_tokens != NULL) && (node->content_tokens->count > 0);
}

amark_error_t amark_node_spelling_state(const amark_node_t *node, amark_spelling_state_t *out)
{
    if ((node == NULL) || (out == NULL) || (out->struct_size < sizeof(*out)))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    out->has_spelling = has_records(node) || has_content_tokens(node);
    out->complete = (node->dropped == 0);
    out->dropped_facets = node->dropped & ~(uint32_t)AMARK_TREE_DROPPED_STRUCTURE;
    out->structure_dropped = ((node->dropped & AMARK_TREE_DROPPED_STRUCTURE) != 0);
    return AMARK_ERROR_NONE;
}

bool amark_node_is_new(const amark_node_t *node)
{
    return (node != NULL) && (node->line_count == 0) && !has_records(node) &&
           !has_content_tokens(node) && ((node->dropped & AMARK_TREE_DROPPED_STRUCTURE) == 0);
}

/* ---- Walking the nodes under a leaf ---------------------------------------------- */

/* The next node after node in the preorder of the subtree of top (NULL after the
 * last one). */
static const amark_node_t *next_under(const amark_node_t *node, const amark_node_t *top)
{
    if (node->first_child != NULL)
    {
        return node->first_child;
    }
    while (node != top)
    {
        if (node->next != NULL)
        {
            return node->next;
        }
        node = node->parent;
    }
    return NULL;
}

/* ---- Content verbatim ------------------------------------------------------------ */

/* Whether node is a block whose type has content. */
static bool is_content_leaf(const amark_node_t *node)
{
    return (node != NULL) && (node->desc->level == AMARK_NODE_LEVEL_BLOCK) &&
           ((node->desc->flags & AMARK_NODE_TYPE_CONTENT) != 0);
}

/*
 * The nesting order at node (with tokens on content, or leaf itself): the range
 * of the tokens of each child lies within [start, end), the range of node (the
 * whole content for the leaf), the ranges of the children follow each other in
 * the order of the children without overlap, and no token of node overlaps the
 * range of a child. A child without tokens, or with tokens on another content
 * object, breaks it. In time proportional to the children and the tokens of
 * node.
 */
static bool children_nest(const amark_node_t *node, const amark_tree_content_t *content,
                          size_t start, size_t end)
{
    const amark_tree_content_tokens_t *own = node->content_tokens;
    size_t own_count = (own != NULL) ? own->count : 0;
    size_t index = 0;
    size_t previous_end = start;
    const amark_node_t *child;

    for (child = node->first_child; child != NULL; child = child->next)
    {
        const amark_tree_content_tokens_t *tokens = child->content_tokens;
        size_t child_start;
        size_t child_end;

        if ((tokens == NULL) || (tokens->count == 0) || (tokens->base != content))
        {
            return false;
        }
        child_start = tokens->items[0].start;
        child_end = tokens->items[tokens->count - 1].end;
        if ((child_start < previous_end) || (child_end > end))
        {
            return false;
        }
        /* The tokens of node are in order: skip those before the child; the next
         * one must start after it. */
        while ((index < own_count) && (own->items[index].end <= child_start))
        {
            index++;
        }
        if ((index < own_count) && (own->items[index].start < child_end))
        {
            return false;
        }
        previous_end = child_end;
    }
    return true;
}

/*
 * The coverage under the nesting order: when children_nest holds at every node
 * under the leaf, the tokens of two different nodes never overlap (the tokens of
 * a node lie within its range, the ranges of siblings are disjoint, and the
 * tokens of an ancestor are outside the ranges of its children, so outside the
 * range of every descendant), and the tokens of one node never overlap (an
 * invariant of the records). All are within the content, so they cover it
 * exactly once when the sum of their lengths is its length. Hence the judgment
 * needs no array of counts and allocates nothing.
 */
amark_error_t amark_block_content_verbatim(const amark_node_t *leaf, bool *out)
{
    const amark_tree_content_t *content;
    const amark_node_t *node;
    uint64_t covered = 0;

    if ((out == NULL) || !is_content_leaf(leaf))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    *out = false;
    content = leaf->content;
    if ((content == NULL) || (content->state != AMARK_CONTENT_LAID))
    {
        return AMARK_ERROR_NONE;
    }
    for (node = leaf; node != NULL; node = next_under(node, leaf))
    {
        const amark_tree_content_tokens_t *tokens = node->content_tokens;
        size_t count = (tokens != NULL) ? tokens->count : 0;
        size_t start = 0;
        size_t end = content->length;
        size_t i;

        if (node->dropped != 0)
        {
            return AMARK_ERROR_NONE;
        }
        if (count > 0)
        {
            if (tokens->base != content)
            {
                return AMARK_ERROR_NONE;
            }
            for (i = 0; i < count; i++)
            {
                covered += (uint64_t)(tokens->items[i].end - tokens->items[i].start);
            }
            if (node != leaf)
            {
                start = tokens->items[0].start;
                end = tokens->items[count - 1].end;
            }
        }
        else if (node != leaf)
        {
            /* An inline node without tokens (a new node). */
            return AMARK_ERROR_NONE;
        }
        if (!children_nest(node, content, start, end))
        {
            return AMARK_ERROR_NONE;
        }
    }
    *out = (covered == (uint64_t)content->length);
    return AMARK_ERROR_NONE;
}

/* ---- Content coverage --------------------------------------------------------------- */

void amark_tree_cover_scratch_free(amark_tree_cover_scratch_t *scratch)
{
    if (scratch == NULL)
    {
        return;
    }
    amark_free(scratch->counts);
    scratch->counts = NULL;
    scratch->capacity = 0;
}

/* Makes room for needed counts in scratch (its old counts are not kept). */
static amark_error_t reserve_counts(amark_tree_cover_scratch_t *scratch, size_t needed)
{
    int32_t *grown;

    if (needed <= scratch->capacity)
    {
        return AMARK_ERROR_NONE;
    }
    if (needed > SIZE_MAX / sizeof(*grown))
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    grown = (int32_t *)amark_malloc(needed * sizeof(*grown));
    if (grown == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    amark_free(scratch->counts);
    scratch->counts = grown;
    scratch->capacity = needed;
    return AMARK_ERROR_NONE;
}

/*
 * The counts are a difference array over the positions of the content: +1 at
 * the start and -1 at the end of each token on the content, so that the running
 * sum at a position is the number of tokens that cover it. A position is
 * covered by at most one token of each node (the tokens of a node do not
 * overlap), so the counts stay far below the range of int32_t.
 */
amark_error_t amark_tree_content_cover(const amark_node_t *leaf,
                                       amark_tree_cover_scratch_t *scratch,
                                       amark_tree_cover_t *out)
{
    const amark_tree_content_t *content = leaf->content;
    size_t length = (content != NULL) ? content->length : 0;
    const amark_node_t *foreign = NULL;
    size_t foreign_offset = 0;
    const amark_node_t *node;
    int32_t *counts;
    int32_t running = 0;
    size_t position;
    amark_error_t error;

    error = reserve_counts(scratch, length + 1);
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    counts = scratch->counts;
    memset(counts, 0, (length + 1) * sizeof(*counts));
    for (node = leaf; node != NULL; node = next_under(node, leaf))
    {
        const amark_tree_content_tokens_t *tokens = node->content_tokens;
        size_t count = (tokens != NULL) ? tokens->count : 0;
        size_t i;

        if (count == 0)
        {
            continue;
        }
        if ((content == NULL) || (tokens->base != content))
        {
            /* The first foreign token of a node is its earliest one. */
            if ((foreign == NULL) || (tokens->items[0].start < foreign_offset))
            {
                foreign = node;
                foreign_offset = tokens->items[0].start;
            }
            continue;
        }
        for (i = 0; i < count; i++)
        {
            counts[tokens->items[i].start]++;
            counts[tokens->items[i].end]--;
        }
    }

    out->covered = false;
    out->node = NULL;
    for (position = 0; position < length; position++)
    {
        running += counts[position];
        if (running != 1)
        {
            break;
        }
    }
    if ((foreign != NULL) && ((position == length) || (foreign_offset <= position)))
    {
        out->problem = AMARK_TREE_COVER_FOREIGN;
        out->offset = foreign_offset;
        out->node = foreign;
    }
    else if (position < length)
    {
        out->problem = (running == 0) ? AMARK_TREE_COVER_GAP : AMARK_TREE_COVER_OVERLAP;
        out->offset = position;
    }
    else
    {
        out->covered = true;
        out->problem = AMARK_TREE_COVER_NONE;
        out->offset = 0;
    }
    return AMARK_ERROR_NONE;
}
