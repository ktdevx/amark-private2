/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * Verbatim: the spelling state, whether the original spelling can be output
 * and its cache, and the derivation of tightness.
 */
#include "tree/tree.h"

#include <amark/allocator.h>

#include <stdint.h>
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

/* ---- Subtree verbatim (11.4, 11.5, 7.8, 14.9) ---------------------------------------- */

/*
 * The judgment of a node is cached in its cache flags (AMARK_TREE_CACHE_*). It is
 * computed in postorder, children first, without recursion, and each node looks
 * only at its own records, at the cached flags of its children and at the
 * columns its children remembered: the cost of judging every node is
 * proportional to the nodes and the records (14.9).
 *
 *   - An inline node holds (AMARK_TREE_CACHE_VERBATIM) when it dropped no
 *     spelling, has content tokens, its children hold and nest in its range
 *     on the same base, and its tokens and the ranges of its children cover its
 *     range exactly once. Whether its base is the content of its leaf is decided
 *     when it is queried (the content of the leaf may change without notifying
 *     its descendants).
 *   - A block holds when it dropped no spelling, has a line range, its content
 *     (a leaf with content) can be output in its lines (11.3, from the cached
 *     flags of its inline children), and each line of its range is covered
 *     without gap or overlap, from a column c0, by its margin (a container), its
 *     layout tokens, its blank line, its content pieces and its block children.
 *     AMARK_TREE_CACHE_FULL_LINES tells that the coverage of each line reaches
 *     the end of the line; only the children of a SHARE_LINES type may end
 *     before it, where a sibling or a token of the parent goes on (7.8).
 *
 * The columns a parent needs of a child on a line: a container starts at its
 * margin; another block at the column it remembered (AMARK_TREE_CACHE_STARTS:
 * the first item of its line, for a pass-through block its first child, blank
 * line or token, for a leaf its first token or piece); a block ends at the end
 * of the line, or at the column it remembered (AMARK_TREE_CACHE_ENDS).
 */

/* Whether node is a block that can have block children (document, list,
 * containers). */
static bool has_block_children(const amark_node_t *node)
{
    return (node->desc->level == AMARK_NODE_LEVEL_BLOCK) &&
           ((node->desc->accepts & (AMARK_CONTENT_BLOCK | AMARK_CONTENT_LIST_ITEM)) != 0);
}

static bool is_container(const amark_node_t *node)
{
    return (node->desc->level == AMARK_NODE_LEVEL_BLOCK) &&
           ((node->desc->flags & AMARK_NODE_TYPE_MARGINS) != 0);
}

static bool cache_valid(const amark_node_t *node)
{
    return (node->cache_flags & AMARK_TREE_CACHE_VALID) != 0;
}

static bool cached_verbatim(const amark_node_t *node)
{
    return (node->cache_flags & (AMARK_TREE_CACHE_VALID | AMARK_TREE_CACHE_VERBATIM)) ==
           (AMARK_TREE_CACHE_VALID | AMARK_TREE_CACHE_VERBATIM);
}

/*
 * Whether the inline children of node (all judged) hold, nest within [start,
 * end) on content (children_nest), and cover that range exactly once together
 * with the tokens of node. Under the nesting order the tokens of node and the
 * ranges of its children do not overlap and lie within the range, and each child
 * covers its own range exactly once, so the coverage is exact when the lengths
 * add up to the range.
 */
static bool inline_children_cover(const amark_node_t *node, const amark_tree_content_t *content,
                                  size_t start, size_t end, size_t *steps)
{
    const amark_tree_content_tokens_t *own = node->content_tokens;
    size_t own_count = (own != NULL) ? own->count : 0;
    uint64_t covered = 0;
    const amark_node_t *child;
    size_t i;

    for (child = node->first_child; child != NULL; child = child->next)
    {
        const amark_tree_content_tokens_t *tokens = child->content_tokens;

        (*steps)++;
        if (!cached_verbatim(child) || (tokens == NULL) || (tokens->count == 0))
        {
            return false;
        }
        covered += (uint64_t)(tokens->items[tokens->count - 1].end - tokens->items[0].start);
    }
    for (i = 0; i < own_count; i++)
    {
        (*steps)++;
        covered += (uint64_t)(own->items[i].end - own->items[i].start);
    }
    if (!children_nest(node, content, start, end))
    {
        return false;
    }
    return covered == (uint64_t)(end - start);
}

static bool judge_inline(const amark_node_t *node, size_t *steps)
{
    const amark_tree_content_tokens_t *tokens = node->content_tokens;

    if ((node->dropped != 0) || (tokens == NULL) || (tokens->count == 0))
    {
        return false;
    }
    return inline_children_cover(node, tokens->base, tokens->items[0].start,
                                 tokens->items[tokens->count - 1].end, steps);
}

/* The content of a leaf can be output in its original lines (11.3), from the
 * cached flags of its inline children. A leaf without content (a fenced code
 * block without content lines) holds when nothing is spelled on a content. */
static bool judge_leaf_content(const amark_node_t *leaf, size_t *steps)
{
    const amark_tree_content_t *content = leaf->content;
    const amark_tree_content_tokens_t *own = leaf->content_tokens;

    if (content == NULL)
    {
        return (leaf->first_child == NULL) && ((own == NULL) || (own->count == 0));
    }
    if (content->state != AMARK_CONTENT_LAID)
    {
        return false;
    }
    if ((own != NULL) && (own->count > 0) && (own->base != content))
    {
        return false;
    }
    return inline_children_cover(leaf, content, 0, content->length, steps);
}

/* The number of start columns a judged block keeps before its end columns. */
static uint32_t start_column_count(const amark_node_t *block)
{
    return ((block->cache_flags & AMARK_TREE_CACHE_STARTS) != 0) ? block->line_count : 0;
}

/* The column where the judged child block starts on line (within its range). */
static uint32_t child_start(const amark_node_t *child, amark_line_id_t line)
{
    uint32_t index = line - child->line_first;

    if (is_container(child))
    {
        return child->records->margins[index].start;
    }
    return child->verbatim_columns[index];
}

/* The column where the judged child block ends on line (within its range). */
static uint32_t child_end(const amark_node_t *child, amark_line_id_t line, uint32_t width)
{
    if ((child->cache_flags & AMARK_TREE_CACHE_ENDS) == 0)
    {
        return width;
    }
    return child->verbatim_columns[start_column_count(child) + (line - child->line_first)];
}

/* The children of block (all judged) hold, lie in its range, and follow each
 * other in the order of their lines; a line is shared only under a SHARE_LINES
 * block, and only such a block has children whose lines end early. */
static bool block_children_in_order(const amark_node_t *block, size_t *steps)
{
    bool share = (block->desc->flags & AMARK_NODE_TYPE_SHARE_LINES) != 0;
    uint64_t range_end = (uint64_t)block->line_first + block->line_count;
    const amark_node_t *previous = NULL;
    const amark_node_t *child;

    for (child = block->first_child; child != NULL; child = child->next)
    {
        (*steps)++;
        if (!cached_verbatim(child) || (child->desc->level != AMARK_NODE_LEVEL_BLOCK) ||
            (child->line_first < block->line_first) ||
            ((uint64_t)child->line_first + child->line_count > range_end) ||
            (!share && ((child->cache_flags & AMARK_TREE_CACHE_FULL_LINES) == 0)))
        {
            return false;
        }
        if (previous != NULL)
        {
            amark_line_id_t previous_last = previous->line_first + previous->line_count - 1;

            if ((child->line_first < previous_last) ||
                ((child->line_first == previous_last) && !share))
            {
                return false;
            }
        }
        previous = child;
    }
    return true;
}

/* The judgment of the lines of a block, in progress. */
typedef struct line_judgment
{
    const amark_node_t *block;
    const amark_tree_records_t *records;
    const amark_tree_piece_t *pieces;
    size_t piece_count;
    size_t margin_index;
    size_t token_index;
    size_t blank_index;
    size_t piece_index;
    /* The first child whose lines are not all judged. */
    const amark_node_t *child;
    /* The columns: start columns (when starts), then end columns (when ends). */
    uint32_t *columns;
    bool starts;
    bool ends;
    bool full;
} line_judgment_t;

/* The kinds of the items that cover a line. */
enum
{
    ITEM_TOKEN,
    ITEM_BLANK,
    ITEM_PIECE,
    ITEM_CHILD,
    ITEM_KINDS
};

/* Switches to the end columns at line index (the lines before end at the end
 * of their lines). */
static amark_error_t start_end_columns(line_judgment_t *judgment, uint32_t index)
{
    const amark_node_t *block = judgment->block;
    const amark_tree_line_t *lines = block->document->lines;
    uint32_t count = block->line_count;
    size_t offset = judgment->starts ? count : 0;
    uint32_t *grown;
    uint32_t i;

    if ((size_t)count > (SIZE_MAX / sizeof(*grown)) - offset)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    grown = (uint32_t *)amark_realloc(judgment->columns, (offset + count) * sizeof(*grown));
    if (grown == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    for (i = 0; i < index; i++)
    {
        grown[offset + i] = lines[block->line_first + i].width;
    }
    judgment->columns = grown;
    judgment->ends = true;
    return AMARK_ERROR_NONE;
}

/*
 * Judges line index of the block: the items on the line must follow each other
 * from c0 (the margin of a container, otherwise the first item) without gap or
 * overlap. An empty item at the current column goes before a nonempty one.
 * Sets *holds, and remembers the columns of the line.
 */
static amark_error_t judge_line(line_judgment_t *judgment, uint32_t index, bool *holds,
                                size_t *steps)
{
    const amark_node_t *block = judgment->block;
    const amark_tree_records_t *records = judgment->records;
    amark_line_id_t line = block->line_first + index;
    uint32_t width = block->document->lines[line].width;
    const amark_node_t *child = judgment->child;
    bool placed = false;
    uint32_t c0 = 0;
    uint32_t position = 0;

    *holds = false;
    (*steps)++;
    if (is_container(block))
    {
        const amark_tree_margin_t *margin;

        if ((records == NULL) || (judgment->margin_index >= records->margin_count) ||
            (records->margins[judgment->margin_index].line != line))
        {
            return AMARK_ERROR_NONE;
        }
        margin = &records->margins[judgment->margin_index++];
        c0 = margin->start;
        position = margin->end;
        placed = true;
    }
    for (;;)
    {
        bool present[ITEM_KINDS] = {false, false, false, false};
        uint32_t starts[ITEM_KINDS] = {0, 0, 0, 0};
        uint32_t ends[ITEM_KINDS] = {0, 0, 0, 0};
        int chosen = -1;
        int kind;

        if ((records != NULL) && (judgment->token_index < records->token_count) &&
            (records->tokens[judgment->token_index].line == line))
        {
            present[ITEM_TOKEN] = true;
            starts[ITEM_TOKEN] = records->tokens[judgment->token_index].start;
            ends[ITEM_TOKEN] = records->tokens[judgment->token_index].end;
        }
        if ((records != NULL) && (judgment->blank_index < records->blank_line_count) &&
            (records->blank_lines[judgment->blank_index].line == line))
        {
            present[ITEM_BLANK] = true;
            starts[ITEM_BLANK] = records->blank_lines[judgment->blank_index].start;
            ends[ITEM_BLANK] = records->blank_lines[judgment->blank_index].end;
        }
        if ((judgment->piece_index < judgment->piece_count) &&
            (judgment->pieces[judgment->piece_index].line == line))
        {
            present[ITEM_PIECE] = true;
            starts[ITEM_PIECE] = judgment->pieces[judgment->piece_index].start;
            ends[ITEM_PIECE] = judgment->pieces[judgment->piece_index].end;
        }
        if ((child != NULL) && (child->line_first <= line))
        {
            present[ITEM_CHILD] = true;
            starts[ITEM_CHILD] = child_start(child, line);
            ends[ITEM_CHILD] = child_end(child, line, width);
        }
        if (!present[ITEM_TOKEN] && !present[ITEM_BLANK] && !present[ITEM_PIECE] &&
            !present[ITEM_CHILD])
        {
            break;
        }
        if (!placed)
        {
            /* Without a margin, c0 is the first column of the items. */
            for (kind = 0; kind < ITEM_KINDS; kind++)
            {
                if (present[kind] && (!placed || (starts[kind] < c0)))
                {
                    c0 = starts[kind];
                    placed = true;
                }
            }
            position = c0;
        }
        for (kind = 0; kind < ITEM_KINDS; kind++)
        {
            if (present[kind] && (starts[kind] == position) &&
                ((chosen < 0) || (ends[kind] == starts[kind])))
            {
                chosen = kind;
            }
        }
        if (chosen < 0)
        {
            return AMARK_ERROR_NONE; /* a gap or an overlap */
        }
        (*steps)++;
        position = ends[chosen];
        switch (chosen)
        {
        case ITEM_TOKEN:
            judgment->token_index++;
            break;
        case ITEM_BLANK:
            judgment->blank_index++;
            break;
        case ITEM_PIECE:
            judgment->piece_index++;
            break;
        default:
            child = child->next;
            break;
        }
    }
    if (!placed)
    {
        return AMARK_ERROR_NONE; /* nothing covers the line */
    }
    /* The children whose last line this is are done. */
    while ((judgment->child != NULL) &&
           (judgment->child->line_first + judgment->child->line_count - 1 == line) &&
           (judgment->child != child))
    {
        judgment->child = judgment->child->next;
    }
    if (judgment->starts)
    {
        judgment->columns[index] = c0;
    }
    if (position != width)
    {
        judgment->full = false;
        if (!judgment->ends)
        {
            amark_error_t error = start_end_columns(judgment, index);

            if (error != AMARK_ERROR_NONE)
            {
                return error;
            }
        }
    }
    if (judgment->ends)
    {
        judgment->columns[(judgment->starts ? block->line_count : 0) + index] = position;
    }
    *holds = true;
    return AMARK_ERROR_NONE;
}

/*
 * Judges the lines of block (with a line range; its children judged): *holds
 * and, when it holds, *full and *columns (the remembered columns, or NULL).
 * AMARK_ERROR_OUT_OF_MEMORY leaves nothing allocated.
 */
static amark_error_t judge_lines(const amark_node_t *block, bool *holds, bool *full,
                                 uint32_t **columns, bool *starts, bool *ends, size_t *steps)
{
    line_judgment_t judgment;
    const amark_tree_records_t *records = block->records;
    uint32_t index;
    amark_error_t error = AMARK_ERROR_NONE;
    bool line_holds = true;

    *holds = false;
    *columns = NULL;
    memset(&judgment, 0, sizeof(judgment));
    judgment.block = block;
    judgment.records = records;
    judgment.full = true;
    judgment.starts = !is_container(block);
    if (has_block_children(block))
    {
        if (!block_children_in_order(block, steps))
        {
            return AMARK_ERROR_NONE;
        }
        judgment.child = block->first_child;
    }
    if ((block->content != NULL) && (block->content->state == AMARK_CONTENT_LAID))
    {
        judgment.pieces = block->content->pieces;
        judgment.piece_count = block->content->piece_count;
    }
    if (judgment.starts)
    {
        judgment.columns =
            (uint32_t *)amark_malloc((size_t)block->line_count * sizeof(*judgment.columns));
        if (judgment.columns == NULL)
        {
            return AMARK_ERROR_OUT_OF_MEMORY;
        }
    }
    for (index = 0; (index < block->line_count) && line_holds; index++)
    {
        error = judge_line(&judgment, index, &line_holds, steps);
        if (error != AMARK_ERROR_NONE)
        {
            amark_free(judgment.columns);
            return error;
        }
    }
    /* Every record and child was on a line of the range. */
    if (!line_holds || (judgment.child != NULL) ||
        ((records != NULL) &&
         ((judgment.token_index != records->token_count) ||
          (judgment.blank_index != records->blank_line_count) ||
          (is_container(block) && (judgment.margin_index != records->margin_count)))) ||
        (judgment.piece_index != judgment.piece_count))
    {
        amark_free(judgment.columns);
        return AMARK_ERROR_NONE;
    }
    *holds = true;
    *full = judgment.full;
    *columns = judgment.columns;
    *starts = judgment.starts;
    *ends = judgment.ends;
    return AMARK_ERROR_NONE;
}

/* Judges node, whose children are judged, and caches the result. */
static amark_error_t judge_node(amark_node_t *node, size_t *steps)
{
    uint32_t flags = AMARK_TREE_CACHE_VALID;
    uint32_t *columns = NULL;

    if (node->desc->level != AMARK_NODE_LEVEL_BLOCK)
    {
        if (judge_inline(node, steps))
        {
            flags |= AMARK_TREE_CACHE_VERBATIM;
        }
    }
    else if ((node->dropped == 0) && (node->line_count > 0) &&
             (((node->desc->flags & AMARK_NODE_TYPE_CONTENT) != 0)
                  ? judge_leaf_content(node, steps)
                  : (has_block_children(node) || (node->first_child == NULL))))
    {
        bool holds = false;
        bool full = false;
        bool starts = false;
        bool ends = false;
        amark_error_t error = judge_lines(node, &holds, &full, &columns, &starts, &ends, steps);

        if (error != AMARK_ERROR_NONE)
        {
            return error;
        }
        if (holds)
        {
            flags |= AMARK_TREE_CACHE_VERBATIM;
            flags |= full ? AMARK_TREE_CACHE_FULL_LINES : 0u;
            flags |= starts ? AMARK_TREE_CACHE_STARTS : 0u;
            flags |= ends ? AMARK_TREE_CACHE_ENDS : 0u;
        }
    }
    amark_free(node->verbatim_columns);
    node->verbatim_columns = columns;
    /* The cached tightness is kept: it has its own validity. */
    node->cache_flags =
        flags | (node->cache_flags & (AMARK_TREE_CACHE_TIGHT | AMARK_TREE_CACHE_LOOSE));
    return AMARK_ERROR_NONE;
}

/* The first node of the postorder of the subtree of node that is not judged
 * yet, or node itself when it is judged (a judged node has judged children). */
static amark_node_t *first_unjudged(amark_node_t *node)
{
    while (!cache_valid(node) && (node->first_child != NULL))
    {
        node = node->first_child;
    }
    return node;
}

/* Judges the nodes of the subtree of top that are not judged, in postorder,
 * without recursion. On failure the nodes judged so far keep their results. */
static amark_error_t judge_subtree(amark_node_t *top)
{
    size_t *steps = &top->document->stats.verbatim_steps;
    amark_node_t *node = first_unjudged(top);

    for (;;)
    {
        (*steps)++;
        if (!cache_valid(node))
        {
            amark_error_t error = judge_node(node, steps);

            if (error != AMARK_ERROR_NONE)
            {
                return error;
            }
        }
        if (node == top)
        {
            return AMARK_ERROR_NONE;
        }
        node = (node->next != NULL) ? first_unjudged(node->next) : node->parent;
    }
}

/*
 * The leaf of the inline node: the nearest block among its ancestors (NULL when
 * there is none). The leaf changes only with the structure, so each inline node
 * remembers it for the structure generation of the document: the walk goes up
 * only to the first ancestor that remembers it (or to the leaf), and every node
 * on the way remembers it too. Finding the leaves of all the inline nodes takes
 * time proportional to them between two structural changes.
 */
static const amark_node_t *leaf_of(const amark_node_t *node)
{
    amark_document_t *document = node->document;
    uint64_t current = document->structure_generation + 1;
    amark_node_t *walk = (amark_node_t *)node;
    amark_node_t *leaf;

    /* Up to the first block or remembering inline node. */
    for (;;)
    {
        document->stats.verbatim_steps++;
        if (walk->leaf_memo_generation == current)
        {
            leaf = walk->leaf_memo;
            break;
        }
        if (walk->parent == NULL)
        {
            leaf = NULL;
            break;
        }
        if (walk->parent->desc->level == AMARK_NODE_LEVEL_BLOCK)
        {
            leaf = walk->parent;
            break;
        }
        walk = walk->parent;
    }
    /* The nodes from node to the stop remember it. */
    for (walk = (amark_node_t *)node; walk->leaf_memo_generation != current;
         walk = walk->parent)
    {
        document->stats.verbatim_steps++;
        walk->leaf_memo = leaf;
        walk->leaf_memo_generation = current;
        if ((walk->parent == NULL) || (walk->parent->desc->level == AMARK_NODE_LEVEL_BLOCK))
        {
            break;
        }
    }
    return leaf;
}

/* Whether the tokens of the inline node are on the laid content of its leaf. */
static bool on_own_leaf_content(const amark_node_t *node)
{
    const amark_node_t *leaf = leaf_of(node);

    return (leaf != NULL) && (leaf->content != NULL) &&
           (leaf->content->state == AMARK_CONTENT_LAID) && (node->content_tokens != NULL) &&
           (node->content_tokens->base == leaf->content);
}

amark_error_t amark_node_verbatim(const amark_node_t *node, bool *out)
{
    amark_error_t error;

    if ((node == NULL) || (out == NULL))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    *out = false;
    /* The cache is internal state: the judgment is a query (14.4: a document
     * is used by one thread at a time, queries included). */
    error = judge_subtree((amark_node_t *)node);
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    if (!cached_verbatim(node))
    {
        return AMARK_ERROR_NONE;
    }
    if (node->desc->level == AMARK_NODE_LEVEL_BLOCK)
    {
        *out = (node->cache_flags & AMARK_TREE_CACHE_FULL_LINES) != 0;
    }
    else
    {
        *out = on_own_leaf_content(node);
    }
    return AMARK_ERROR_NONE;
}

/* ---- Tightness (3.7) ------------------------------------------------------------- */

/* The index of the first blank line of records at or after line (count when
 * there is none). */
static size_t first_blank_from(const amark_tree_records_t *records, amark_line_id_t line,
                               size_t *steps)
{
    size_t low = 0;
    size_t high = records->blank_line_count;

    while (low < high)
    {
        size_t middle = low + (high - low) / 2;

        (*steps)++;
        if (records->blank_lines[middle].line < line)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    return low;
}

/*
 * Whether a blank line held by parent lies between two sibling children of
 * parent with a line range: after the last line of the earlier one and before
 * the first line of the next one. The blank lines are in the order of their
 * lines: a cursor goes forward over them while the children follow each other
 * in the order of the lines (each blank line and child is examined once); a
 * child placed before the cursor (a moved block) searches the blank lines
 * again from its own line.
 */
static bool blank_between_children(const amark_node_t *parent, size_t *steps)
{
    const amark_tree_records_t *records = parent->records;
    const amark_node_t *prev = NULL;
    const amark_node_t *child;
    size_t next = 0;

    if ((records == NULL) || (records->blank_line_count == 0))
    {
        return false;
    }
    for (child = parent->first_child; child != NULL; child = child->next)
    {
        (*steps)++;
        if (child->line_count == 0)
        {
            continue;
        }
        if (prev != NULL)
        {
            amark_line_id_t after = prev->line_first + prev->line_count;

            if ((next > 0) && (records->blank_lines[next - 1].line >= after))
            {
                next = first_blank_from(records, after, steps);
            }
            while ((next < records->blank_line_count) && (records->blank_lines[next].line < after))
            {
                (*steps)++;
                next++;
            }
            if ((next < records->blank_line_count) &&
                (records->blank_lines[next].line < child->line_first))
            {
                return true;
            }
        }
        prev = child;
    }
    return false;
}

bool amark_tree_list_loose(const amark_node_t *list, size_t *steps)
{
    const amark_node_t *child;

    (*steps)++;
    if (blank_between_children(list, steps))
    {
        return true;
    }
    for (child = list->first_child; child != NULL; child = child->next)
    {
        (*steps)++;
        if (blank_between_children(child, steps))
        {
            return true;
        }
    }
    return false;
}

bool amark_tree_derive_overridable(const amark_node_t *node, amark_facet_t facet,
                                   int64_t *number)
{
    /* The cache is internal state: the derivation is a query (14.4). */
    amark_node_t *list = (amark_node_t *)node;

    if ((node->type != AMARK_NODE_LIST) || (facet != AMARK_LIST_TIGHT))
    {
        return false;
    }
    if ((list->cache_flags & AMARK_TREE_CACHE_TIGHT) == 0)
    {
        bool loose = amark_tree_list_loose(list, &list->document->stats.tight_steps);

        list->cache_flags |= AMARK_TREE_CACHE_TIGHT | (loose ? AMARK_TREE_CACHE_LOOSE : 0u);
    }
    *number = ((list->cache_flags & AMARK_TREE_CACHE_LOOSE) != 0) ? 0 : 1;
    return true;
}
