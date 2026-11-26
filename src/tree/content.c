/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * Content: the content objects, the materialization of placed content, the
 * content tokens, moving and splitting, the edits of the lines at the block
 * stage (moving, removing, trimming) and the copy of shared content.
 *
 * A leaf owns its content object (amark_node_t.content); the object counts its
 * owners and its bases separately and is freed when both reach 0. Laid content
 * grows by appending: each piece is checked against the line table (Layout's
 * record checks) and the order of the pieces, its bytes are appended (a virtual
 * LF before the first piece of a new line, a tab cut by a boundary as spaces),
 * and the leaf changes only once every allocation has succeeded. Appending
 * never changes the bytes already there. Free content is always a new object:
 * the previous one is released, never changed.
 *
 * A node with content tokens counts as one base of the object its tokens are
 * on. Tokens are appended in the order of the content and checked against its
 * bytes; moving and splitting keep that order. The tokens of a node keep their
 * base when the leaf gets another content or the node moves to another leaf.
 *
 * Copy on write: an object is copied before a leaf changes it only when it is
 * shared. Appending keeps the bytes already there, so it copies only for
 * another owner; moving, removing and trimming lines move the existing bytes,
 * so they copy for another owner or for any base (at the block stage the leaf
 * and its descendants have no tokens, so a base is another node). The copy
 * becomes the content of the leaf, the tokens of the leaf and its descendants on
 * the old object move to it, and the other nodes keep the old object
 * unchanged. Unshared content is changed in place, so recording line by line
 * stays linear in the records (14.6).
 */
#include "tree/tree.h"

#include <amark/allocator.h>
#include <amark/entity.h>
#include <amark/utf8.h>

#include <string.h>

/* The first capacities of the bytes and of the pieces of a content object, and
 * of the content tokens of a node. */
#define BYTES_FIRST_CAPACITY 16u
#define PIECES_FIRST_CAPACITY 4u
#define TOKENS_FIRST_CAPACITY 4u

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

/* The number of bytes of a piece of that shape. A line is shorter than 4 GiB, so
 * the sum cannot overflow. */
static size_t shape_length(const piece_shape_t *shape)
{
    return (size_t)shape->lead + shape->body_length + (size_t)shape->trail;
}

/* Writes the bytes of the piece of line of that shape at out and returns the end
 * of what it wrote (no NUL). */
static char *write_piece(const amark_document_t *document, amark_line_id_t line,
                         const piece_shape_t *shape, char *out)
{
    const amark_tree_line_t *entry = &document->lines[line];
    const char *data = document->sources[entry->source].data;

    memset(out, ' ', shape->lead);
    out += shape->lead;
    if (shape->body_length > 0)
    {
        memcpy(out, data + entry->offset + shape->body, shape->body_length);
        out += shape->body_length;
    }
    memset(out, ' ', shape->trail);
    return out + shape->trail;
}

/* ---- Copy on write ---------------------------------------------------------------------- */

/* Whether content must be copied before an edit that moves or changes its
 * existing bytes (moving, removing, trimming): another leaf owns it, or a node
 * has tokens on it. (Appending only needs a copy when another leaf owns it.) */
static bool shared_for_change(const amark_tree_content_t *content)
{
    return (content->owners >= 2) || (content->bases >= 1);
}

/*
 * Makes the laid content dst the count pieces of src from first on (count >= 1):
 * their bytes as they are in src (the virtual LFs between them included),
 * shifted to start at 0, and their line count. dst has the room for them; dst
 * may be src itself (the bytes and the pieces only move toward the start).
 */
static void keep_pieces(amark_tree_content_t *dst, const amark_tree_content_t *src, size_t first,
                        size_t count)
{
    size_t start = src->pieces[first].content_start;
    size_t end = src->pieces[first + count - 1].content_start +
                 src->pieces[first + count - 1].content_length;
    size_t lines = 0;
    size_t i;

    memmove(dst->bytes, src->bytes + start, end - start);
    memmove(dst->pieces, src->pieces + first, count * sizeof(*dst->pieces));
    for (i = 0; i < count; i++)
    {
        dst->pieces[i].content_start -= start;
        if ((i == 0) || (dst->pieces[i].line != dst->pieces[i - 1].line))
        {
            lines++;
        }
    }
    dst->length = end - start;
    dst->bytes[dst->length] = '\0';
    dst->piece_count = count;
    dst->line_count = lines;
    dst->state = AMARK_CONTENT_LAID;
}

/*
 * Allocates in *out a new laid object (no owner, no base yet) that holds the
 * count pieces of the laid content src from first on (count >= 1), with room for
 * extra_bytes more bytes and extra_pieces more pieces. The caller has checked
 * that the bytes stay below the limit of a content. Counts the pieces copied in
 * the record steps. On failure nothing is left allocated.
 */
static amark_error_t copy_pieces(amark_document_t *document, const amark_tree_content_t *src,
                                 size_t first, size_t count, size_t extra_bytes,
                                 size_t extra_pieces, amark_tree_content_t **out)
{
    amark_tree_content_t *copy;
    size_t start = src->pieces[first].content_start;
    size_t end = src->pieces[first + count - 1].content_start +
                 src->pieces[first + count - 1].content_length;
    size_t pieces;

    *out = NULL;
    if ((extra_pieces > (SIZE_MAX - count)) ||
        ((count + extra_pieces) > (SIZE_MAX / sizeof(*copy->pieces))))
    {
        return AMARK_ERROR_OVERFLOW;
    }
    pieces = count + extra_pieces;
    copy = (amark_tree_content_t *)amark_calloc(1, sizeof(*copy));
    if (copy == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    copy->capacity = (end - start) + extra_bytes + 1;
    copy->bytes = (char *)amark_malloc(copy->capacity);
    copy->pieces = (amark_tree_piece_t *)amark_malloc(pieces * sizeof(*copy->pieces));
    if ((copy->bytes == NULL) || (copy->pieces == NULL))
    {
        content_free(copy);
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    copy->piece_capacity = pieces;
    keep_pieces(copy, src, first, count);
    document->stats.record_steps += count;
    *out = copy;
    return AMARK_ERROR_NONE;
}

/*
 * Makes copy (a new object from copy_pieces) the content of leaf in place of
 * its old content: the tokens of the leaf and of its descendants on the old
 * content move to the copy, and the leaf releases the old content as an owner
 * (the other nodes keep it). Walks the subtree of the leaf without recursion.
 */
/* Moves the content tokens of leaf and of its descendants whose base is old to
 * the base copy (the same bytes). Walks the subtree without recursion. */
static void rebase_subtree(amark_node_t *leaf, amark_tree_content_t *old,
                           amark_tree_content_t *copy)
{
    amark_node_t *node = leaf;

    for (;;)
    {
        amark_tree_content_tokens_t *tokens = node->content_tokens;

        if ((tokens != NULL) && (tokens->base == old))
        {
            tokens->base = copy;
            copy->bases++;
            old->bases--;
        }
        if (node->first_child != NULL)
        {
            node = node->first_child;
            continue;
        }
        while ((node != leaf) && (node->next == NULL))
        {
            node = node->parent;
        }
        if (node == leaf)
        {
            break;
        }
        node = node->next;
    }
}

static void adopt_copy(amark_node_t *leaf, amark_tree_content_t *copy)
{
    amark_tree_content_t *old = leaf->content;

    rebase_subtree(leaf, old, copy);
    copy->owners = 1;
    leaf->content = copy;
    amark_tree_content_release(old, true);
    leaf->document->stats.content_copies++;
}

amark_error_t amark_block_add_content(amark_node_t *leaf, amark_line_id_t line,
                                      uint32_t column_start, uint32_t column_end)
{
    amark_document_t *document;
    amark_tree_content_t *content;
    amark_tree_content_t *target;
    amark_tree_content_t *copy = NULL;
    amark_tree_piece_t *piece;
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

        /* On the line of the last piece: after it, with columns, and only when
         * the last piece is not empty (an empty piece is the whole line). */
        if ((line < last->line) ||
            ((line == last->line) &&
             ((column_start < last->end) || (column_start == column_end) ||
              (last->start == last->end))))
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
     * the piece. */
    added = (((content != NULL) && (content->piece_count > 0) && new_line) ? 1u : 0u) +
            shape_length(&shape);
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
    else if (content->owners >= 2)
    {
        /* Appending keeps the bytes already there, so tokens on them do not
         * need a copy; another owner does. */
        error = copy_pieces(document, content, 0, content->piece_count, added, 1, &copy);
        if (error != AMARK_ERROR_NONE)
        {
            return error;
        }
    }
    target = (copy != NULL) ? copy : content;
    error = reserve_bytes(target, target->length + added + 1);
    if (error == AMARK_ERROR_NONE)
    {
        error = reserve_piece(document, target);
    }
    if (error != AMARK_ERROR_NONE)
    {
        if (created)
        {
            content_free(content);
        }
        if (copy != NULL)
        {
            content_free(copy);
        }
        return error;
    }

    /* Commit. */
    out = target->bytes + target->length;
    if ((target->piece_count > 0) && new_line)
    {
        *out++ = '\n';
    }
    piece = &target->pieces[target->piece_count];
    piece->line = line;
    piece->start = column_start;
    piece->end = column_end;
    piece->content_start = (size_t)(out - target->bytes);
    out = write_piece(document, line, &shape, out);
    *out = '\0';
    piece->content_length = (size_t)(out - target->bytes) - piece->content_start;
    target->length = (size_t)(out - target->bytes);
    if (new_line)
    {
        target->line_count++;
    }
    target->piece_count++;
    if (created)
    {
        content->owners = 1;
        leaf->content = content;
    }
    if (copy != NULL)
    {
        adopt_copy(leaf, copy);
    }
    document->stats.record_steps++;
    amark_tree_drop_content_derived(leaf);
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
    amark_tree_leaf_take_content(leaf, content);
    return AMARK_ERROR_NONE;
}

amark_error_t amark_tree_content_from_value(const char *value, size_t length,
                                            amark_tree_content_t **out)
{
    amark_tree_content_t *content;
    size_t normalized = length;
    size_t lines = 0;
    size_t i;
    char *bytes;

    *out = NULL;
    /* CRLF and CR become LF (a CONTENT facet without LINES is not normalized
     * by the setter): each CRLF loses its CR. */
    for (i = 0; i + 1 < length; i++)
    {
        if ((value[i] == '\r') && (value[i + 1] == '\n'))
        {
            normalized--;
        }
    }
    if (normalized > (AMARK_TREE_MAX_CONTENT + 1u))
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
    bytes = content->bytes;
    for (i = 0; i < length; i++)
    {
        char c = value[i];

        if (c == '\r')
        {
            if ((i + 1 < length) && (value[i + 1] == '\n'))
            {
                continue;
            }
            c = '\n';
        }
        *bytes++ = c;
    }
    /* The content is the value without its last LF. */
    content->length = normalized;
    if ((normalized > 0) && (content->bytes[normalized - 1] == '\n'))
    {
        content->length--;
    }
    if (content->length > AMARK_TREE_MAX_CONTENT)
    {
        content_free(content);
        return AMARK_ERROR_LIMIT_EXCEEDED;
    }
    content->bytes[content->length] = '\0';
    for (i = 0; i < content->length; i++)
    {
        lines += (content->bytes[i] == '\n') ? 1u : 0u;
    }
    content->line_count = (normalized == 0) ? 0 : lines + 1;
    content->state = AMARK_CONTENT_FREE;
    content->owners = 1;
    *out = content;
    return AMARK_ERROR_NONE;
}

/*
 * Removes the tokens of node that spell facet (not AMARK_FACET_NONE): its
 * layout tokens and its content tokens; the content tokens release their base
 * when none is left. Allocates nothing; drops the derived values of node.
 */
void amark_tree_remove_facet_tokens(amark_node_t *node, amark_facet_t facet)
{
    amark_tree_records_t *records = node->records;
    amark_tree_content_tokens_t *tokens = node->content_tokens;
    size_t kept = 0;
    size_t i;

    if (records != NULL)
    {
        for (i = 0; i < records->token_count; i++)
        {
            if (records->tokens[i].facet != facet)
            {
                records->tokens[kept++] = records->tokens[i];
            }
        }
        records->token_count = kept;
    }
    if (tokens != NULL)
    {
        kept = 0;
        for (i = 0; i < tokens->count; i++)
        {
            if (tokens->items[i].facet != facet)
            {
                tokens->items[kept++] = tokens->items[i];
            }
        }
        tokens->count = kept;
        if (kept == 0)
        {
            amark_tree_content_tokens_free(tokens);
            node->content_tokens = NULL;
        }
    }
    amark_tree_drop_derived(node, AMARK_FACET_NONE);
}

void amark_tree_leaf_take_content(amark_node_t *leaf, amark_tree_content_t *content)
{
    amark_tree_content_release(leaf->content, true);
    leaf->content = content;
    amark_tree_drop_content_derived(leaf);
    amark_tree_changed(leaf, AMARK_TREE_CHANGE_RECORD);
}

amark_error_t amark_tree_content_free_copy(const amark_tree_content_t *source,
                                           amark_tree_content_t **out)
{
    amark_tree_content_t *content;

    *out = NULL;
    content = (amark_tree_content_t *)amark_calloc(1, sizeof(*content));
    if (content == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    content->bytes = (char *)amark_malloc(source->length + 1);
    if (content->bytes == NULL)
    {
        content_free(content);
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    content->capacity = source->length + 1;
    if (source->length > 0)
    {
        memcpy(content->bytes, source->bytes, source->length);
    }
    content->bytes[source->length] = '\0';
    content->length = source->length;
    content->line_count = source->line_count;
    content->state = AMARK_CONTENT_FREE;
    content->owners = 1;
    *out = content;
    return AMARK_ERROR_NONE;
}

void amark_tree_leaf_replace_content(amark_node_t *leaf, amark_tree_content_t *content)
{
    amark_tree_content_t *old = leaf->content;

    if ((old != NULL) && (old->length == content->length) &&
        ((old->length == 0) || (memcmp(old->bytes, content->bytes, old->length) == 0)))
    {
        rebase_subtree(leaf, old, content);
    }
    amark_tree_leaf_take_content(leaf, content);
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

/* ---- Content tokens ------------------------------------------------------------------- */

void amark_tree_content_tokens_free(amark_tree_content_tokens_t *tokens)
{
    if (tokens == NULL)
    {
        return;
    }
    amark_tree_content_release(tokens->base, false);
    amark_free(tokens->items);
    amark_free(tokens);
}

/* The change of the tokens of node: its derived values are dropped (those of
 * its facets and, for a leaf, of its CONTENT facets, which its REPLACED tokens
 * change) and the change is notified. */
static void tokens_changed(amark_node_t *node)
{
    amark_tree_drop_derived(node, AMARK_FACET_NONE);
    amark_tree_changed(node, AMARK_TREE_CHANGE_RECORD);
}

/*
 * Makes room for extra more tokens in the tokens of node: *out receives the
 * token structure of node, allocated when node has none (*created tells), which
 * the caller stores in node once it commits. On failure node is unchanged and
 * nothing is left allocated. Counts the tokens moved by a growth in the record
 * steps.
 */
static amark_error_t reserve_tokens(amark_node_t *node, size_t extra,
                                    amark_tree_content_tokens_t **out, bool *created)
{
    amark_tree_content_tokens_t *tokens = node->content_tokens;
    amark_tree_content_token_t *grown;
    size_t needed;
    size_t capacity;

    *created = false;
    if (tokens == NULL)
    {
        tokens = (amark_tree_content_tokens_t *)amark_calloc(1, sizeof(*tokens));
        if (tokens == NULL)
        {
            return AMARK_ERROR_OUT_OF_MEMORY;
        }
        *created = true;
    }
    if (extra > (SIZE_MAX - tokens->count))
    {
        if (*created)
        {
            amark_free(tokens);
        }
        return AMARK_ERROR_OVERFLOW;
    }
    needed = tokens->count + extra;
    if (needed > tokens->capacity)
    {
        capacity = (tokens->capacity < TOKENS_FIRST_CAPACITY) ? TOKENS_FIRST_CAPACITY
                                                              : tokens->capacity;
        while (capacity < needed)
        {
            capacity = (capacity > (SIZE_MAX / 2)) ? needed : capacity * 2;
        }
        grown = NULL;
        if (capacity <= (SIZE_MAX / sizeof(*grown)))
        {
            grown = (amark_tree_content_token_t *)amark_realloc(tokens->items,
                                                                capacity * sizeof(*grown));
        }
        if (grown == NULL)
        {
            if (*created)
            {
                amark_free(tokens);
            }
            return (capacity <= (SIZE_MAX / sizeof(*grown))) ? AMARK_ERROR_OUT_OF_MEMORY
                                                             : AMARK_ERROR_OVERFLOW;
        }
        tokens->items = grown;
        tokens->capacity = capacity;
        node->document->stats.record_steps += tokens->count;
    }
    *out = tokens;
    return AMARK_ERROR_NONE;
}

static bool is_continuation(char c)
{
    return (((unsigned char)c) & 0xC0u) == 0x80u;
}

/*
 * Whether position (at most length) is a boundary of the characters of the
 * length bytes at bytes, decoded from the start with amark_utf8_decode (an
 * invalid sequence takes the bytes of its maximal subpart). A byte that is not a
 * continuation byte always starts a character, and a character is at most 4
 * bytes, so the decoding starts at most 3 bytes before position: constant time.
 */
static bool is_boundary(const char *bytes, size_t length, size_t position)
{
    size_t start = position;
    size_t steps = 0;

    if ((position == 0) || (position >= length))
    {
        return true;
    }
    while ((start > 0) && (steps < 3) && is_continuation(bytes[start]))
    {
        start--;
        steps++;
    }
    if ((start > 0) && is_continuation(bytes[start]))
    {
        /* The 3 bytes before position are continuation bytes too: no character
         * that holds position can start before it. */
        return true;
    }
    while (start < position)
    {
        start += amark_utf8_decode(bytes + start, length - start, NULL);
    }
    return start == position;
}

static bool is_ascii_punctuation(char c)
{
    unsigned char u = (unsigned char)c;

    return ((u >= 0x21u) && (u <= 0x2Fu)) || ((u >= 0x3Au) && (u <= 0x40u)) ||
           ((u >= 0x5Bu) && (u <= 0x60u)) || ((u >= 0x7Bu) && (u <= 0x7Eu));
}

/* Whether the bytes [start, end) (on character boundaries) are NUL bytes and
 * invalid UTF-8 sequences only. */
static bool all_replaced(const char *bytes, size_t start, size_t end)
{
    size_t position = start;

    while (position < end)
    {
        uint32_t codepoint = 0;
        size_t size;

        if (bytes[position] == '\0')
        {
            position++;
            continue;
        }
        size = amark_utf8_decode(bytes + position, end - position, &codepoint);
        if ((codepoint != AMARK_UTF8_REPLACEMENT) ||
            ((size == 3) && (memcmp(bytes + position, "\xEF\xBF\xBD", 3) == 0)))
        {
            return false;
        }
        position += size;
    }
    return true;
}

/*
 * Whether the bytes [start, end) of laid content are spaces that stand for the
 * columns of a tab cut by the start or the end of one piece: *ok receives the
 * answer. Finds the piece by binary search over the content positions.
 * AMARK_ERROR_OUT_OF_MEMORY when the index of the tabs of the line cannot be
 * built.
 */
static amark_error_t tab_remainder_ok(const amark_document_t *document,
                                      const amark_tree_content_t *content, size_t start,
                                      size_t end, bool *ok)
{
    const amark_tree_piece_t *piece;
    piece_shape_t shape;
    size_t low = 0;
    size_t high = content->piece_count;
    size_t piece_end;
    amark_error_t error;

    *ok = false;
    /* The last piece that starts at or before start. */
    while (low < high)
    {
        size_t middle = low + ((high - low) / 2);

        if (content->pieces[middle].content_start <= start)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    if (low == 0)
    {
        return AMARK_ERROR_NONE;
    }
    piece = &content->pieces[low - 1];
    piece_end = piece->content_start + piece->content_length;
    if (end > piece_end)
    {
        return AMARK_ERROR_NONE;
    }
    error = shape_piece(document, piece->line, piece->start, piece->end, &shape);
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    *ok = (end <= piece->content_start + shape.lead) || (start >= piece_end - shape.trail);
    return AMARK_ERROR_NONE;
}

/* Checks the bytes [start, end) of content for a token of kind: a nonempty
 * range within the content, on character boundaries, whose bytes match the
 * kind. AMARK_ERROR_INVALID_ARGUMENT otherwise. */
static amark_error_t check_token_bytes(const amark_document_t *document,
                                       const amark_tree_content_t *content, size_t start,
                                       size_t end, amark_spell_kind_t kind)
{
    const char *bytes = content->bytes;
    size_t i;
    uint32_t codepoints[2];
    size_t count = 0;
    bool ok = false;
    amark_error_t error;

    if ((kind < AMARK_SPELL_TEXT) || (kind > AMARK_SPELL_TAB_REMAINDER) || (start >= end) ||
        (end > content->length) || !is_boundary(bytes, content->length, start) ||
        !is_boundary(bytes, content->length, end))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    switch (kind)
    {
    case AMARK_SPELL_LINE_BREAK:
        /* A line holds no LF: every LF of laid content is a virtual one. */
        for (i = start; i < end; i++)
        {
            if (bytes[i] != '\n')
            {
                return AMARK_ERROR_INVALID_ARGUMENT;
            }
        }
        return AMARK_ERROR_NONE;
    case AMARK_SPELL_TAB_REMAINDER:
        if (content->state != AMARK_CONTENT_LAID)
        {
            return AMARK_ERROR_INVALID_ARGUMENT;
        }
        error = tab_remainder_ok(document, content, start, end, &ok);
        if (error != AMARK_ERROR_NONE)
        {
            return error;
        }
        return ok ? AMARK_ERROR_NONE : AMARK_ERROR_INVALID_ARGUMENT;
    case AMARK_SPELL_REPLACED:
        return all_replaced(bytes, start, end) ? AMARK_ERROR_NONE : AMARK_ERROR_INVALID_ARGUMENT;
    case AMARK_SPELL_ESCAPE:
        return ((end - start == 2) && (bytes[start] == '\\') &&
                is_ascii_punctuation(bytes[start + 1]))
                   ? AMARK_ERROR_NONE
                   : AMARK_ERROR_INVALID_ARGUMENT;
    case AMARK_SPELL_CHAR_REF:
        return amark_char_ref_decode(bytes + start, end - start, codepoints, &count)
                   ? AMARK_ERROR_NONE
                   : AMARK_ERROR_INVALID_ARGUMENT;
    default:
        return AMARK_ERROR_NONE;
    }
}

/* Whether node is leaf or an inline descendant of leaf (every node between them
 * is inline). */
static bool is_leaf_or_inline_of(const amark_node_t *node, const amark_node_t *leaf)
{
    const amark_node_t *current = node;

    while (current != leaf)
    {
        if ((current == NULL) || (current->desc->level != AMARK_NODE_LEVEL_INLINE))
        {
            return false;
        }
        current = current->parent;
    }
    return true;
}

amark_error_t amark_node_add_content_token(amark_node_t *node, const amark_node_t *leaf,
                                           const amark_content_token_t *token)
{
    amark_tree_content_tokens_t *tokens;
    amark_tree_content_token_t *item;
    amark_tree_content_t *content;
    bool created = false;
    amark_error_t error;

    if ((node == NULL) || (token == NULL) || (token->struct_size < sizeof(*token)) ||
        !is_content_leaf(leaf) || (leaf->content == NULL) || !is_leaf_or_inline_of(node, leaf) ||
        (token->facet > node->desc->facet_count))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    if (amark_tree_mixes_spelling(node, token->facet, false))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    content = leaf->content;
    tokens = node->content_tokens;
    if ((tokens != NULL) && (tokens->count > 0) &&
        ((tokens->base != content) || (token->start < tokens->items[tokens->count - 1].end)))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    error = check_token_bytes(node->document, content, token->start, token->end, token->kind);
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    error = reserve_tokens(node, 1, &tokens, &created);
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }

    /* Commit. The content is shorter than 4 GiB: the positions fit. */
    item = &tokens->items[tokens->count];
    item->start = (uint32_t)token->start;
    item->end = (uint32_t)token->end;
    item->kind = (uint8_t)token->kind;
    item->facet = (uint8_t)token->facet;
    item->role = token->role;
    if (tokens->count == 0)
    {
        tokens->base = content;
        content->bases++;
    }
    tokens->count++;
    node->content_tokens = tokens;
    node->document->stats.record_steps++;
    tokens_changed(node);
    return AMARK_ERROR_NONE;
}

size_t amark_node_content_token_count(const amark_node_t *node)
{
    return ((node != NULL) && (node->content_tokens != NULL)) ? node->content_tokens->count : 0;
}

amark_error_t amark_node_content_token_at(const amark_node_t *node, size_t index,
                                          amark_content_token_t *out)
{
    const amark_tree_content_token_t *item;

    if ((out == NULL) || (out->struct_size < sizeof(*out)) ||
        (index >= amark_node_content_token_count(node)))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    item = &node->content_tokens->items[index];
    out->start = item->start;
    out->end = item->end;
    out->kind = (amark_spell_kind_t)item->kind;
    out->facet = item->facet;
    out->role = item->role;
    return AMARK_ERROR_NONE;
}

amark_error_t amark_node_move_content_tokens(amark_node_t *from, amark_node_t *to, size_t first,
                                             size_t count)
{
    amark_tree_content_tokens_t *source;
    amark_tree_content_tokens_t *target;
    const amark_tree_content_token_t *moved;
    size_t target_count;
    size_t low;
    size_t high;
    size_t i;
    bool created = false;
    amark_error_t error;

    if ((from == NULL) || (to == NULL) || (from == to) || (from->document != to->document) ||
        ((to->desc->level != AMARK_NODE_LEVEL_INLINE) && !is_content_leaf(to)))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    source = from->content_tokens;
    if ((source == NULL) || (count == 0) || (first >= source->count) ||
        (count > (source->count - first)))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    target = to->content_tokens;
    target_count = (target != NULL) ? target->count : 0;
    if ((target_count > 0) && (target->base != source->base))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    moved = &source->items[first];
    for (i = 0; i < count; i++)
    {
        if ((moved[i].facet > to->desc->facet_count) ||
            amark_tree_mixes_spelling(to, moved[i].facet, false))
        {
            return AMARK_ERROR_INVALID_ARGUMENT;
        }
    }
    /* The place of the moved tokens among the tokens of to: the first token
     * that starts after the first moved one; the moved tokens must fit between
     * it and the one before. */
    low = 0;
    high = target_count;
    while (low < high)
    {
        size_t middle = low + ((high - low) / 2);

        if (target->items[middle].start < moved[0].start)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    if (((low > 0) && (target->items[low - 1].end > moved[0].start)) ||
        ((low < target_count) && (target->items[low].start < moved[count - 1].end)))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    error = reserve_tokens(to, count, &target, &created);
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }

    /* Commit. */
    memmove(&target->items[low + count], &target->items[low],
            (target->count - low) * sizeof(*target->items));
    memcpy(&target->items[low], moved, count * sizeof(*target->items));
    if (target->count == 0)
    {
        target->base = source->base;
        target->base->bases++;
    }
    target->count += count;
    to->content_tokens = target;
    memmove(&source->items[first], &source->items[first + count],
            (source->count - first - count) * sizeof(*source->items));
    source->count -= count;
    if (source->count == 0)
    {
        /* to holds the base now: the object stays. */
        amark_tree_content_release(source->base, false);
        source->base = NULL;
    }
    from->document->stats.record_steps += count;
    tokens_changed(from);
    tokens_changed(to);
    return AMARK_ERROR_NONE;
}

amark_error_t amark_node_split_content_token(amark_node_t *node, size_t index, size_t at)
{
    amark_tree_content_tokens_t *tokens;
    amark_tree_content_token_t item;
    bool created = false;
    amark_error_t error;

    if (index >= amark_node_content_token_count(node))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    tokens = node->content_tokens;
    item = tokens->items[index];
    if ((item.kind != AMARK_SPELL_TEXT) || (at <= item.start) || (at >= item.end) ||
        !is_boundary(tokens->base->bytes, tokens->base->length, at))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    error = reserve_tokens(node, 1, &tokens, &created);
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }

    /* Commit. */
    memmove(&tokens->items[index + 2], &tokens->items[index + 1],
            (tokens->count - index - 1) * sizeof(*tokens->items));
    tokens->items[index].end = (uint32_t)at;
    item.start = (uint32_t)at;
    tokens->items[index + 1] = item;
    tokens->count++;
    node->document->stats.record_steps++;
    tokens_changed(node);
    return AMARK_ERROR_NONE;
}

amark_error_t amark_node_content_base(const amark_node_t *node, const char **bytes,
                                     size_t *length, amark_content_state_t *state,
                                     bool *is_own_leaf_content)
{
    const amark_tree_content_t *base;
    const amark_node_t *leaf;

    if (node == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    base = (node->content_tokens != NULL) ? node->content_tokens->base : NULL;
    if (bytes != NULL)
    {
        *bytes = ((base != NULL) && (base->bytes != NULL)) ? base->bytes : "";
    }
    if (length != NULL)
    {
        *length = (base != NULL) ? base->length : 0;
    }
    if (state != NULL)
    {
        *state = (base != NULL) ? (amark_content_state_t)base->state : AMARK_CONTENT_NONE;
    }
    if (is_own_leaf_content != NULL)
    {
        /* The nearest leaf: the first block among node and its ancestors. */
        leaf = node;
        while ((leaf != NULL) && (leaf->desc->level == AMARK_NODE_LEVEL_INLINE))
        {
            leaf = leaf->parent;
        }
        *is_own_leaf_content = (base != NULL) && (leaf != NULL) && (leaf->content == base);
    }
    return AMARK_ERROR_NONE;
}

/* ---- Moving, removing and trimming lines (the block stage) ----------------------------- */

/* Whether leaf is at the block stage: a block with content that is not free,
 * without content tokens and without inline children (so neither it nor its
 * descendants have tokens on its content). */
static bool block_stage(const amark_node_t *leaf)
{
    const amark_node_t *child;

    if (!is_content_leaf(leaf) ||
        ((leaf->content != NULL) && (leaf->content->state != AMARK_CONTENT_LAID)) ||
        ((leaf->content_tokens != NULL) && (leaf->content_tokens->count > 0)))
    {
        return false;
    }
    for (child = leaf->first_child; child != NULL; child = child->next)
    {
        if (child->desc->level == AMARK_NODE_LEVEL_INLINE)
        {
            return false;
        }
    }
    return true;
}

/* Whether the count lines from first are at the start or at the end of the line
 * range of leaf (count >= 1), and no margin or blank line of the leaf is on
 * them. */
static bool edge_lines(const amark_node_t *leaf, amark_line_id_t first, uint32_t count)
{
    const amark_tree_records_t *records = leaf->records;
    size_t offset;
    size_t i;

    if ((count == 0) || (leaf->line_count == 0) || (first < leaf->line_first))
    {
        return false;
    }
    offset = (size_t)(first - leaf->line_first);
    if ((offset > leaf->line_count) || (count > (leaf->line_count - offset)) ||
        ((offset != 0) && ((offset + count) != leaf->line_count)))
    {
        return false;
    }
    if (records != NULL)
    {
        for (i = 0; i < records->margin_count; i++)
        {
            if ((records->margins[i].line - first) < count)
            {
                return false;
            }
        }
        for (i = 0; i < records->blank_line_count; i++)
        {
            if ((records->blank_lines[i].line - first) < count)
            {
                return false;
            }
        }
    }
    return true;
}

/* The index of the first piece of content on line or after it (binary search
 * over the pieces, which are in the order of the lines). */
static size_t first_piece_from(const amark_tree_content_t *content, size_t line)
{
    size_t low = 0;
    size_t high = content->piece_count;

    while (low < high)
    {
        size_t middle = low + ((high - low) / 2);

        if (content->pieces[middle].line < line)
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

/* The index of the first layout token of records on line or after it. */
static size_t first_token_from(const amark_tree_records_t *records, size_t line)
{
    size_t low = 0;
    size_t high = records->token_count;

    while (low < high)
    {
        size_t middle = low + ((high - low) / 2);

        if (records->tokens[middle].line < line)
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
 * Takes the count lines from first (checked by edge_lines) out of the leaf from
 * at the block stage: their layout tokens and content pieces go to to (a leaf
 * checked by the caller), or away when to is NULL. The content of from keeps
 * the other pieces, in place, or in a copy when it is shared (or it is released
 * when no piece is left); the content of to is a new object of the moved
 * pieces. Every allocation is made before anything changes.
 */
static amark_error_t take_lines(amark_node_t *from, amark_node_t *to, amark_line_id_t first,
                                uint32_t count)
{
    amark_document_t *document = from->document;
    amark_tree_content_t *content = from->content;
    amark_tree_records_t *records = from->records;
    amark_tree_records_t *to_records = NULL;
    amark_tree_content_t *to_content = NULL;
    amark_tree_content_t *copy = NULL;
    bool to_records_created = false;
    size_t end = (size_t)first + count;
    size_t piece_low = 0;
    size_t piece_high = 0;
    size_t piece_count = 0;
    size_t token_low = 0;
    size_t token_high = 0;
    size_t token_count = 0;
    size_t kept_first;
    size_t kept_count;
    size_t i;
    amark_error_t error = AMARK_ERROR_NONE;

    if (content != NULL)
    {
        piece_count = content->piece_count;
        piece_low = first_piece_from(content, first);
        piece_high = first_piece_from(content, end);
    }
    if (records != NULL)
    {
        token_count = records->token_count;
        token_low = first_token_from(records, first);
        token_high = first_token_from(records, end);
    }
    if (to != NULL)
    {
        for (i = token_low; i < token_high; i++)
        {
            if (records->tokens[i].facet > to->desc->facet_count)
            {
                return AMARK_ERROR_INVALID_ARGUMENT;
            }
        }
    }
    /* The pieces left: those before the lines, or those after them. */
    kept_first = (piece_low == 0) ? piece_high : 0;
    kept_count = piece_count - (piece_high - piece_low);

    if ((to != NULL) && (token_high > token_low))
    {
        error = amark_tree_reserve_layout_tokens(to, token_high - token_low, &to_records,
                                                 &to_records_created);
    }
    if ((error == AMARK_ERROR_NONE) && (to != NULL) && (piece_high > piece_low))
    {
        error = copy_pieces(document, content, piece_low, piece_high - piece_low, 0, 0,
                            &to_content);
    }
    if ((error == AMARK_ERROR_NONE) && (piece_high > piece_low) && (kept_count > 0) &&
        shared_for_change(content))
    {
        error = copy_pieces(document, content, kept_first, kept_count, 0, 0, &copy);
    }
    if (error != AMARK_ERROR_NONE)
    {
        if (to_records_created)
        {
            amark_tree_records_free(to_records);
        }
        if (to_content != NULL)
        {
            content_free(to_content);
        }
        return error;
    }

    /* Commit: to. */
    if (to != NULL)
    {
        if (to_records != NULL)
        {
            memcpy(to_records->tokens, &records->tokens[token_low],
                   (token_high - token_low) * sizeof(*to_records->tokens));
            to_records->token_count = token_high - token_low;
            to->records = to_records;
        }
        if (to_content != NULL)
        {
            to_content->owners = 1;
            to->content = to_content;
        }
        to->line_first = first;
        to->line_count = count;
    }
    /* from: its tokens, its content, its line range. */
    if (token_high > token_low)
    {
        memmove(&records->tokens[token_low], &records->tokens[token_high],
                (token_count - token_high) * sizeof(*records->tokens));
        records->token_count = token_count - (token_high - token_low);
    }
    if (piece_high > piece_low)
    {
        if (kept_count == 0)
        {
            from->content = NULL;
            amark_tree_content_release(content, true);
        }
        else if (copy != NULL)
        {
            adopt_copy(from, copy);
        }
        else
        {
            keep_pieces(content, content, kept_first, kept_count);
        }
    }
    if (first == from->line_first)
    {
        from->line_first += count;
    }
    from->line_count -= count;
    if (from->line_count == 0)
    {
        from->line_first = 0;
    }
    document->stats.record_steps += (token_high - token_low) + (piece_high - piece_low);
    amark_tree_drop_derived(from, AMARK_FACET_NONE);
    amark_tree_changed(from, AMARK_TREE_CHANGE_RECORD);
    if (to != NULL)
    {
        amark_tree_drop_derived(to, AMARK_FACET_NONE);
        amark_tree_changed(to, AMARK_TREE_CHANGE_RECORD);
    }
    return AMARK_ERROR_NONE;
}

/* Whether node has no record at all: no line range, no content, no layout
 * records and no content tokens. */
static bool unrecorded(const amark_node_t *node)
{
    const amark_tree_records_t *records = node->records;

    return (node->line_count == 0) && (node->content == NULL) &&
           ((node->content_tokens == NULL) || (node->content_tokens->count == 0)) &&
           ((records == NULL) || ((records->margin_count == 0) &&
                                  (records->blank_line_count == 0) &&
                                  (records->token_count == 0)));
}

amark_error_t amark_block_move_lines(amark_node_t *from, amark_node_t *to, amark_line_id_t first,
                                     uint32_t count)
{
    if (!block_stage(from) || !is_content_leaf(to) || (from == to) ||
        (from->document != to->document) || !unrecorded(to) || !edge_lines(from, first, count))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    return take_lines(from, to, first, count);
}

amark_error_t amark_block_remove_lines(amark_node_t *leaf, amark_line_id_t first, uint32_t count)
{
    if (!block_stage(leaf) || !edge_lines(leaf, first, count))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    return take_lines(leaf, NULL, first, count);
}

amark_error_t amark_block_trim_content_start(amark_node_t *leaf, uint32_t columns,
                                             amark_spell_kind_t kind, amark_facet_t facet,
                                             uint16_t role)
{
    amark_document_t *document;
    amark_tree_content_t *content;
    amark_tree_content_t *copy = NULL;
    amark_tree_content_t *target;
    const amark_tree_records_t *records;
    amark_tree_records_t *tokens = NULL;
    amark_tree_layout_token_t *slot;
    amark_tree_piece_t *piece;
    piece_shape_t shape;
    bool created = false;
    amark_line_id_t line;
    uint32_t start;
    uint32_t new_start;
    size_t old_length;
    size_t new_length;
    size_t index;
    size_t low;
    size_t high;
    size_t i;
    amark_error_t error;

    if (!block_stage(leaf) || (leaf->content == NULL) || (columns == 0) ||
        (kind < AMARK_SPELL_TEXT) || (kind > AMARK_SPELL_REPLACED) ||
        (facet > leaf->desc->facet_count))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    document = leaf->document;
    content = leaf->content;
    piece = &content->pieces[0];
    line = piece->line;
    start = piece->start;
    if (columns > (piece->end - start))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    new_start = start + columns;
    /* An empty piece is the whole of its line. */
    if ((new_start == piece->end) && (content->piece_count > 1) &&
        (content->pieces[1].line == line))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    error = amark_tree_check_record_columns(leaf, line, start, new_start);
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    /* The place of the token among the layout tokens (in the order of the lines
     * and the columns), without overlapping its neighbours. */
    records = leaf->records;
    low = 0;
    high = (records != NULL) ? records->token_count : 0;
    while (low < high)
    {
        size_t middle = low + ((high - low) / 2);
        const amark_tree_layout_token_t *other = &records->tokens[middle];

        if ((other->line < line) || ((other->line == line) && (other->start < start)))
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    index = low;
    if (((index > 0) && (records->tokens[index - 1].line == line) &&
         (records->tokens[index - 1].end > start)) ||
        ((records != NULL) && (index < records->token_count) &&
         (records->tokens[index].line == line) && (records->tokens[index].start < new_start)))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    error = shape_piece(document, line, new_start, piece->end, &shape);
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    /* The first piece starts the content. The new bytes of the piece are at
     * most 3 more than the old ones (the rest of a tab, for the tab itself). */
    old_length = piece->content_length;
    new_length = shape_length(&shape);
    if ((new_length > old_length) &&
        ((new_length - old_length) > (AMARK_TREE_MAX_CONTENT - content->length)))
    {
        return AMARK_ERROR_LIMIT_EXCEEDED;
    }

    error = amark_tree_reserve_layout_tokens(leaf, 1, &tokens, &created);
    if (error == AMARK_ERROR_NONE)
    {
        if (shared_for_change(content))
        {
            error = copy_pieces(document, content, 0, content->piece_count,
                                (new_length > old_length) ? new_length - old_length : 0, 0,
                                &copy);
        }
        else
        {
            error = reserve_bytes(content, content->length - old_length + new_length + 1);
        }
        if ((error != AMARK_ERROR_NONE) && created)
        {
            amark_tree_records_free(tokens);
        }
    }
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }

    /* Commit: the bytes after the piece shift, the piece is written again. */
    target = (copy != NULL) ? copy : content;
    memmove(target->bytes + new_length, target->bytes + old_length,
            target->length - old_length);
    (void)write_piece(document, line, &shape, target->bytes);
    target->length = target->length - old_length + new_length;
    target->bytes[target->length] = '\0';
    target->pieces[0].start = new_start;
    target->pieces[0].content_length = new_length;
    for (i = 1; i < target->piece_count; i++)
    {
        target->pieces[i].content_start =
            target->pieces[i].content_start - old_length + new_length;
    }
    memmove(&tokens->tokens[index + 1], &tokens->tokens[index],
            (tokens->token_count - index) * sizeof(*tokens->tokens));
    slot = &tokens->tokens[index];
    slot->line = line;
    slot->start = start;
    slot->end = new_start;
    slot->kind = (uint8_t)kind;
    slot->facet = (uint8_t)facet;
    slot->role = role;
    tokens->token_count++;
    leaf->records = tokens;
    if (copy != NULL)
    {
        adopt_copy(leaf, copy);
    }
    document->stats.record_steps++;
    amark_tree_drop_derived(leaf, AMARK_FACET_NONE);
    amark_tree_changed(leaf, AMARK_TREE_CHANGE_RECORD);
    return AMARK_ERROR_NONE;
}
