/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * Content: the content objects, the materialization of placed content, the
 * content tokens, moving and splitting, and the replacement of the content.
 *
 * A leaf owns its content object (amark_node_t.content); the object counts its
 * owners and its bases separately and is freed when both reach 0. Laid content
 * grows by appending: each piece is checked against the line table (Layout's
 * record checks) and the order of the pieces, its bytes are appended (a virtual
 * LF before the first piece of a new line, a tab cut by a boundary as spaces),
 * and the leaf changes only once every allocation has succeeded. Appending
 * never changes the bytes already there. Free content is always a new object:
 * the previous one is released, never changed.
 */
#include "tree/tree.h"

#include <amark/allocator.h>

#include <string.h>

/* The first capacities of the bytes and of the pieces of a content object. */
#define BYTES_FIRST_CAPACITY 16u
#define PIECES_FIRST_CAPACITY 4u

/* ---- Content objects ----------------------------------------------------------------- */

static void content_free(amark_tree_content_t *content)
{
    amark_free(content->bytes);
    amark_free(content->pieces);
    amark_free(content);
}

void amark_tree_content_release(amark_tree_content_t *content, bool owner)
{
    if (content == NULL)
    {
        return;
    }
    if (owner)
    {
        content->owners--;
    }
    else
    {
        content->bases--;
    }
    if ((content->owners == 0) && (content->bases == 0))
    {
        content_free(content);
    }
}

/* Whether node is a block whose type has content. */
static bool is_content_leaf(const amark_node_t *node)
{
    return (node != NULL) && (node->desc->level == AMARK_NODE_LEVEL_BLOCK) &&
           ((node->desc->flags & AMARK_NODE_TYPE_CONTENT) != 0);
}

/*
 * Makes room for needed bytes (the terminating NUL included) in the bytes of
 * content, doubling the capacity. On failure the content is unchanged.
 */
static amark_error_t reserve_bytes(amark_tree_content_t *content, size_t needed)
{
    size_t capacity = content->capacity;
    char *grown;

    if (needed <= capacity)
    {
        return AMARK_ERROR_NONE;
    }
    if (capacity < BYTES_FIRST_CAPACITY)
    {
        capacity = BYTES_FIRST_CAPACITY;
    }
    while (capacity < needed)
    {
        capacity = (capacity > (SIZE_MAX / 2)) ? needed : capacity * 2;
    }
    grown = (char *)amark_realloc(content->bytes, capacity);
    if (grown == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    content->bytes = grown;
    content->capacity = capacity;
    return AMARK_ERROR_NONE;
}

/* Makes room for one more piece in content. On failure the content is
 * unchanged. Counts the pieces moved by a growth in the record steps. */
static amark_error_t reserve_piece(amark_document_t *document, amark_tree_content_t *content)
{
    size_t capacity;
    amark_tree_piece_t *grown;

    if (content->piece_count < content->piece_capacity)
    {
        return AMARK_ERROR_NONE;
    }
    capacity = (content->piece_capacity == 0) ? PIECES_FIRST_CAPACITY
                                              : content->piece_capacity * 2;
    if ((capacity < content->piece_capacity) || (capacity > (SIZE_MAX / sizeof(*grown))))
    {
        return AMARK_ERROR_OVERFLOW;
    }
    grown = (amark_tree_piece_t *)amark_realloc(content->pieces, capacity * sizeof(*grown));
    if (grown == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    content->pieces = grown;
    content->piece_capacity = capacity;
    document->stats.record_steps += content->piece_count;
    return AMARK_ERROR_NONE;
}

/* ---- Laid content --------------------------------------------------------------------- */

/*
 * The bytes a piece [start, end) of line becomes: lead spaces for the columns of
 * a tab cut by start, the bytes [body, body + body_length) of the line, then
 * trail spaces for the columns of a tab cut by end.
 */
typedef struct piece_shape
{
    uint32_t lead;
    size_t body;
    size_t body_length;
    uint32_t trail;
} piece_shape_t;

static amark_error_t shape_piece(const amark_document_t *document, amark_line_id_t line,
                                 uint32_t start, uint32_t end, piece_shape_t *shape)
{
    size_t start_byte = 0;
    size_t end_byte = 0;
    uint32_t start_offset = 0;
    uint32_t end_offset = 0;
    amark_error_t error;

    error = amark_document_column_to_byte(document, line, start, &start_byte, &start_offset);
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_document_column_to_byte(document, line, end, &end_byte, &end_offset);
    }
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    shape->lead = 0;
    shape->body = start_byte;
    shape->body_length = 0;
    shape->trail = end_offset;
    if (start_offset > 0)
    {
        uint32_t tab_column = start - start_offset;
        uint32_t tab_end = tab_column + (4u - (tab_column % 4u));

        if (end_byte == start_byte)
        {
            /* Both boundaries in the same tab. */
            shape->lead = end - start;
            shape->trail = 0;
            return AMARK_ERROR_NONE;
        }
        shape->lead = tab_end - start;
        shape->body = start_byte + 1;
    }
    shape->body_length = end_byte - shape->body;
    return AMARK_ERROR_NONE;
}

amark_error_t amark_block_add_content(amark_node_t *leaf, amark_line_id_t line,
                                      uint32_t column_start, uint32_t column_end)
{
    amark_document_t *document;
    amark_tree_content_t *content;
    amark_tree_piece_t *piece;
    const amark_tree_line_t *entry;
    const char *data;
    piece_shape_t shape;
    bool created = false;
    bool new_line;
    size_t added;
    char *out;
    amark_error_t error;

    if (!is_content_leaf(leaf))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    content = leaf->content;
    if ((content != NULL) && (content->state != AMARK_CONTENT_LAID))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    new_line = true;
    if ((content != NULL) && (content->piece_count > 0))
    {
        const amark_tree_piece_t *last = &content->pieces[content->piece_count - 1];

        if ((line < last->line) ||
            ((line == last->line) &&
             ((column_start < last->end) || (column_start == column_end))))
        {
            return AMARK_ERROR_INVALID_ARGUMENT;
        }
        new_line = (line != last->line);
    }
    error = amark_tree_check_record_columns(leaf, line, column_start, column_end);
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    document = leaf->document;
    error = shape_piece(document, line, column_start, column_end, &shape);
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    /* The bytes to append: the virtual LF of a new line after the first, then
     * the piece. A line is shorter than 4 GiB, so the sum cannot overflow. */
    added = (((content != NULL) && (content->piece_count > 0) && new_line) ? 1u : 0u) +
            (size_t)shape.lead + shape.body_length + (size_t)shape.trail;
    if (added > (AMARK_TREE_MAX_CONTENT - ((content != NULL) ? content->length : 0)))
    {
        return AMARK_ERROR_LIMIT_EXCEEDED;
    }

    if (content == NULL)
    {
        content = (amark_tree_content_t *)amark_calloc(1, sizeof(*content));
        if (content == NULL)
        {
            return AMARK_ERROR_OUT_OF_MEMORY;
        }
        content->state = AMARK_CONTENT_LAID;
        created = true;
    }
    error = reserve_bytes(content, content->length + added + 1);
    if (error == AMARK_ERROR_NONE)
    {
        error = reserve_piece(document, content);
    }
    if (error != AMARK_ERROR_NONE)
    {
        if (created)
        {
            content_free(content);
        }
        return error;
    }

    /* Commit. */
    out = content->bytes + content->length;
    if (content->piece_count > 0 && new_line)
    {
        *out++ = '\n';
    }
    piece = &content->pieces[content->piece_count];
    piece->line = line;
    piece->start = column_start;
    piece->end = column_end;
    piece->content_start = (size_t)(out - content->bytes);
    memset(out, ' ', shape.lead);
    out += shape.lead;
    entry = &document->lines[line];
    data = document->sources[entry->source].data;
    if (shape.body_length > 0)
    {
        memcpy(out, data + entry->offset + shape.body, shape.body_length);
        out += shape.body_length;
    }
    memset(out, ' ', shape.trail);
    out += shape.trail;
    *out = '\0';
    piece->content_length = (size_t)(out - content->bytes) - piece->content_start;
    content->length = (size_t)(out - content->bytes);
    if (new_line)
    {
        content->line_count++;
    }
    content->piece_count++;
    if (created)
    {
        content->owners = 1;
        leaf->content = content;
    }
    document->stats.record_steps++;
    amark_tree_changed(leaf, AMARK_TREE_CHANGE_RECORD);
    return AMARK_ERROR_NONE;
}

/* ---- Free content --------------------------------------------------------------------- */

amark_error_t amark_block_set_free_content(amark_node_t *leaf, const char *bytes, size_t length)
{
    amark_tree_content_t *content;
    size_t normalized = length;
    size_t lines = 0;
    size_t i;
    char *out;

    if (!is_content_leaf(leaf) || ((bytes == NULL) && (length > 0)))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    /* Each CRLF loses its CR. */
    for (i = 0; i + 1 < length; i++)
    {
        if ((bytes[i] == '\r') && (bytes[i + 1] == '\n'))
        {
            normalized--;
        }
    }
    if (normalized > AMARK_TREE_MAX_CONTENT)
    {
        return AMARK_ERROR_LIMIT_EXCEEDED;
    }
    content = (amark_tree_content_t *)amark_calloc(1, sizeof(*content));
    if (content == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    content->bytes = (char *)amark_malloc(normalized + 1);
    if (content->bytes == NULL)
    {
        content_free(content);
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    content->capacity = normalized + 1;
    out = content->bytes;
    for (i = 0; i < length; i++)
    {
        char c = bytes[i];

        if (c == '\r')
        {
            if ((i + 1 < length) && (bytes[i + 1] == '\n'))
            {
                continue;
            }
            c = '\n';
        }
        if (c == '\n')
        {
            lines++;
        }
        *out++ = c;
    }
    *out = '\0';
    content->length = normalized;
    content->line_count = (normalized == 0) ? 0 : lines + 1;
    content->state = AMARK_CONTENT_FREE;
    content->owners = 1;

    /* Commit: the previous content is released, never changed. */
    amark_tree_content_release(leaf->content, true);
    leaf->content = content;
    amark_tree_changed(leaf, AMARK_TREE_CHANGE_RECORD);
    return AMARK_ERROR_NONE;
}

/* ---- Readers -------------------------------------------------------------------------- */

amark_content_state_t amark_block_content_state(const amark_node_t *leaf)
{
    if ((leaf == NULL) || (leaf->content == NULL))
    {
        return AMARK_CONTENT_NONE;
    }
    return (amark_content_state_t)leaf->content->state;
}

amark_error_t amark_block_content_bytes(const amark_node_t *leaf, const char **bytes,
                                        size_t *length)
{
    if (!is_content_leaf(leaf) || (bytes == NULL) || (length == NULL))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    if ((leaf->content == NULL) || (leaf->content->bytes == NULL))
    {
        *bytes = "";
        *length = 0;
        return AMARK_ERROR_NONE;
    }
    *bytes = leaf->content->bytes;
    *length = leaf->content->length;
    return AMARK_ERROR_NONE;
}

size_t amark_block_content_piece_count(const amark_node_t *leaf)
{
    return ((leaf != NULL) && (leaf->content != NULL)) ? leaf->content->piece_count : 0;
}

amark_error_t amark_block_content_piece_at(const amark_node_t *leaf, size_t index,
                                           amark_content_piece_t *out)
{
    const amark_tree_piece_t *piece;

    if ((out == NULL) || (out->struct_size < sizeof(*out)) ||
        (index >= amark_block_content_piece_count(leaf)))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    piece = &leaf->content->pieces[index];
    out->line = piece->line;
    out->column_start = piece->start;
    out->column_end = piece->end;
    out->content_start = piece->content_start;
    out->content_length = piece->content_length;
    return AMARK_ERROR_NONE;
}
