/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * Position: the source ranges (2.9-2.11).
 *
 *   - A block: on the first line of its line range, the leftmost column of the
 *     records of the block and its descendants; on the last line, the
 *     rightmost. Each record array is in the order of the lines, so the
 *     records of a line are found by binary search.
 *   - An inline node: the earliest start and the latest end of the content
 *     tokens of the node and its descendants on one base, mapped through the
 *     pieces of that laid content to bytes of the lines.
 *
 * The walks are iterative (no recursion) and allocate nothing; only the
 * conversions of columns to bytes may build the index of the tabs of a line.
 */
#include "tree/tree.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- Helpers ------------------------------------------------------------------- */

/* The next node of the preorder walk of the subtree of top after node; the
 * children of node are skipped unless descend. NULL at the end. */
static const amark_node_t *walk_next(const amark_node_t *node, const amark_node_t *top,
                                     bool descend)
{
    if (descend && node->first_child != NULL)
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

/* The first index of a record array (ordered by line) whose line is at least
 * line: a binary search over the records, each with a line field. */
#define DEFINE_LOWER_BOUND(name, type)                                    \
    static size_t name(const type *items, size_t count, amark_line_id_t line) \
    {                                                                     \
        size_t low = 0;                                                   \
        size_t high = count;                                              \
        while (low < high)                                                \
        {                                                                 \
            size_t middle = low + (high - low) / 2;                       \
            if (items[middle].line < line)                                \
            {                                                             \
                low = middle + 1;                                         \
            }                                                             \
            else                                                          \
            {                                                             \
                high = middle;                                            \
            }                                                             \
        }                                                                 \
        return low;                                                       \
    }

DEFINE_LOWER_BOUND(margin_lower_bound, amark_tree_margin_t)
DEFINE_LOWER_BOUND(blank_lower_bound, amark_tree_blank_line_t)
DEFINE_LOWER_BOUND(token_lower_bound, amark_tree_layout_token_t)
DEFINE_LOWER_BOUND(piece_lower_bound, amark_tree_piece_t)

/* The leftmost column on the first line and the rightmost on the last line of
 * the records seen so far. */
typedef struct extent
{
    amark_line_id_t first;
    amark_line_id_t last;
    bool has_left;
    bool has_right;
    uint32_t left;
    uint32_t right;
} extent_t;

static void extend_left(extent_t *extent, uint32_t column)
{
    if (!extent->has_left || column < extent->left)
    {
        extent->left = column;
        extent->has_left = true;
    }
}

static void extend_right(extent_t *extent, uint32_t column)
{
    if (!extent->has_right || column > extent->right)
    {
        extent->right = column;
        extent->has_right = true;
    }
}

/* Extends extent with the records of type on its first line (their first record
 * there is the leftmost) and on its last line (their last record there is the
 * rightmost: the records of a line do not overlap and are in column order). */
#define EXTEND_WITH(extent, items, count, lower_bound)                              \
    do                                                                              \
    {                                                                               \
        size_t at_ = lower_bound((items), (count), (extent)->first);                \
        if (at_ < (count) && (items)[at_].line == (extent)->first)                  \
        {                                                                           \
            extend_left((extent), (items)[at_].start);                              \
        }                                                                           \
        at_ = lower_bound((items), (count), (extent)->last + 1u);                   \
        if (at_ > 0 && (items)[at_ - 1].line == (extent)->last)                     \
        {                                                                           \
            extend_right((extent), (items)[at_ - 1].end);                           \
        }                                                                           \
    } while (0)

/* Extends extent with the records of the block node. */
static void extend_with_block(extent_t *extent, const amark_node_t *node)
{
    const amark_tree_records_t *records = node->records;
    const amark_tree_content_t *content = node->content;

    if (records != NULL)
    {
        EXTEND_WITH(extent, records->margins, records->margin_count, margin_lower_bound);
        EXTEND_WITH(extent, records->blank_lines, records->blank_line_count, blank_lower_bound);
        EXTEND_WITH(extent, records->tokens, records->token_count, token_lower_bound);
    }
    if (content != NULL && content->state == AMARK_CONTENT_LAID)
    {
        EXTEND_WITH(extent, content->pieces, content->piece_count, piece_lower_bound);
    }
}

/* A byte range [start, end) of a line, as the positions in the line (a line
 * ending counts after the bytes of the line). */
typedef struct line_span
{
    amark_line_id_t line;
    size_t start;
    size_t end;
} line_span_t;

/* The bytes [*start, *end) of line covering the columns [column_start,
 * column_end): a column inside a tab is that tab. */
static amark_error_t columns_to_bytes(const amark_document_t *document, amark_line_id_t line,
                                      uint32_t column_start, uint32_t column_end, size_t *start,
                                      size_t *end)
{
    uint32_t tab_offset = 0;
    amark_error_t error;

    error = amark_document_column_to_byte(document, line, column_start, start, NULL);
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_document_column_to_byte(document, line, column_end, end, &tab_offset);
    }
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    if (tab_offset > 0)
    {
        (*end)++;
    }
    if (*end < *start)
    {
        *end = *start;
    }
    return AMARK_ERROR_NONE;
}

/* Writes the range of start to end (on lines of one source) to *out. */
static void write_range(const amark_document_t *document, const line_span_t *start,
                        const line_span_t *end, amark_source_range_t *out)
{
    const amark_tree_line_t *first = &document->lines[start->line];
    const amark_tree_line_t *last = &document->lines[end->line];

    out->source = first->source;
    out->start_offset = first->offset + start->start;
    out->end_offset = last->offset + end->end;
    out->start_line = first->number;
    out->start_column = (uint32_t)(start->start + 1);
    out->end_line = last->number;
    out->end_column = (uint32_t)end->end;
}

/* ---- Blocks -------------------------------------------------------------------- */

static amark_error_t block_range(const amark_node_t *block, amark_source_range_t *out,
                                 bool *has_position)
{
    const amark_document_t *document = block->document;
    const amark_node_t *node = block;
    line_span_t start = {0, 0, 0};
    line_span_t end = {0, 0, 0};
    size_t unused;
    extent_t extent;
    amark_error_t error;

    if (block->line_count == 0)
    {
        *has_position = false;
        return AMARK_ERROR_NONE;
    }
    extent.first = block->line_first;
    extent.last = block->line_first + block->line_count - 1u;
    /* The range stays in the source of its first line. */
    while (document->lines[extent.last].source != document->lines[extent.first].source)
    {
        extent.last--;
    }
    extent.has_left = false;
    extent.has_right = false;
    extent.left = 0;
    extent.right = 0;
    while (node != NULL)
    {
        /* Inline nodes have no layout records: their subtrees are skipped. */
        bool is_block = node->desc->level == AMARK_NODE_LEVEL_BLOCK;

        if (is_block)
        {
            extend_with_block(&extent, node);
        }
        node = walk_next(node, block, is_block);
    }
    if (!extent.has_left)
    {
        extent.left = 0;
    }
    if (!extent.has_right)
    {
        extent.right = document->lines[extent.last].width;
    }
    start.line = extent.first;
    end.line = extent.last;
    error = columns_to_bytes(document, extent.first, extent.left, extent.left, &start.start,
                             &unused);
    if (error == AMARK_ERROR_NONE)
    {
        error = columns_to_bytes(document, extent.last, extent.right, extent.right, &unused,
                                 &end.end);
    }
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    if (extent.first == extent.last && end.end < start.start)
    {
        end.end = start.start;
    }
    write_range(document, &start, &end, out);
    *has_position = true;
    return AMARK_ERROR_NONE;
}

/* ---- Inline nodes ---------------------------------------------------------------- */

/* The index of the piece of content that holds position (or, past its end, whose
 * virtual LF it is): the last piece whose content start is at most position. */
static size_t piece_at(const amark_tree_content_t *content, size_t position)
{
    size_t low = 0;
    size_t high = content->piece_count;

    while (low < high)
    {
        size_t middle = low + (high - low) / 2;

        if (content->pieces[middle].content_start <= position)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    return low > 0 ? low - 1 : 0;
}

/* The bytes of the line of the byte at position of the laid content: the byte
 * of the line it copies (a tab for its expanded spaces), or the line ending of
 * the line of the piece before a virtual LF. *index receives the piece. */
static amark_error_t map_byte(const amark_document_t *document,
                              const amark_tree_content_t *content, size_t position,
                              size_t *index, line_span_t *out)
{
    const amark_tree_piece_t *piece;
    const amark_tree_line_t *line;
    size_t offset;
    size_t first = 0;
    size_t last = 0;
    size_t byte;
    uint32_t first_tab_offset = 0;
    uint32_t last_tab_offset = 0;
    amark_error_t error;

    *index = piece_at(content, position);
    piece = &content->pieces[*index];
    line = &document->lines[piece->line];
    out->line = piece->line;
    offset = position - piece->content_start;
    if (offset >= piece->content_length)
    {
        /* The virtual LF after the piece: the line ending of its line. */
        out->start = line->length;
        out->end = line->length + amark_line_table_eol_length(line->eol);
        return AMARK_ERROR_NONE;
    }
    error = amark_document_column_to_byte(document, piece->line, piece->start, &first,
                                          &first_tab_offset);
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_document_column_to_byte(document, piece->line, piece->end, &last,
                                              &last_tab_offset);
    }
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    if (first_tab_offset > 0)
    {
        /* The piece starts with the columns of the tab at first after its start,
         * as spaces. */
        uint32_t tab_column = piece->start - first_tab_offset;
        size_t lead = (size_t)(4u - tab_column % 4u - first_tab_offset);

        if (first == last || offset < lead)
        {
            byte = first;
        }
        else
        {
            byte = first + 1 + (offset - lead);
        }
    }
    else
    {
        byte = first + offset;
    }
    if (last_tab_offset > 0 && byte > last)
    {
        /* The piece ends with the columns of the tab at last before its end. */
        byte = last;
    }
    out->start = byte;
    out->end = byte + 1;
    return AMARK_ERROR_NONE;
}

static amark_error_t inline_range(const amark_node_t *top, amark_source_range_t *out,
                                  bool *has_position)
{
    const amark_document_t *document = top->document;
    const amark_tree_content_t *base = NULL;
    const amark_node_t *node;
    size_t earliest = 0;
    size_t latest = 0;
    size_t start_index = 0;
    size_t end_index = 0;
    line_span_t start = {0, 0, 0};
    line_span_t end = {0, 0, 0};
    amark_error_t error;

    for (node = top; node != NULL; node = walk_next(node, top, true))
    {
        const amark_tree_content_tokens_t *tokens = node->content_tokens;

        if (tokens == NULL || tokens->count == 0)
        {
            continue;
        }
        if (base == NULL)
        {
            /* The base of the first token in document order. */
            base = tokens->base;
            earliest = tokens->items[0].start;
            latest = tokens->items[tokens->count - 1].end;
        }
        else if (tokens->base == base)
        {
            if (tokens->items[0].start < earliest)
            {
                earliest = tokens->items[0].start;
            }
            if (tokens->items[tokens->count - 1].end > latest)
            {
                latest = tokens->items[tokens->count - 1].end;
            }
        }
    }
    if (base == NULL || base->state != AMARK_CONTENT_LAID || base->piece_count == 0)
    {
        *has_position = false;
        return AMARK_ERROR_NONE;
    }
    error = map_byte(document, base, earliest, &start_index, &start);
    if (error == AMARK_ERROR_NONE)
    {
        error = map_byte(document, base, latest - 1, &end_index, &end);
    }
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    if (document->lines[end.line].source != document->lines[start.line].source)
    {
        /* The range stays in the source of its start: it ends with the last
         * piece of that source. */
        const amark_tree_piece_t *piece;
        size_t unused;

        while (document->lines[base->pieces[end_index].line].source !=
               document->lines[start.line].source)
        {
            end_index--;
        }
        piece = &base->pieces[end_index];
        end.line = piece->line;
        error = columns_to_bytes(document, piece->line, piece->start, piece->end, &unused,
                                 &end.end);
        if (error != AMARK_ERROR_NONE)
        {
            return error;
        }
        if (end.line == start.line && end.end < start.start)
        {
            end.end = start.start;
        }
    }
    write_range(document, &start, &end, out);
    *has_position = true;
    return AMARK_ERROR_NONE;
}

/* ---- Public functions ------------------------------------------------------------ */

amark_error_t amark_node_source_range(const amark_node_t *node, amark_source_range_t *out,
                                      bool *has_position)
{
    if (node == NULL || out == NULL || has_position == NULL ||
        out->struct_size < sizeof(amark_source_range_t))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    if (node->desc->level == AMARK_NODE_LEVEL_BLOCK)
    {
        return block_range(node, out, has_position);
    }
    return inline_range(node, out, has_position);
}
