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
 *
 * A node with content tokens counts as one base of the object its tokens are
 * on. Tokens are appended in the order of the content and checked against its
 * bytes; moving and splitting keep that order. The tokens of a node keep their
 * base when the leaf gets another content or the node moves to another leaf.
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

/* Drops the values derived from the tokens of node (its string caches). */
static void drop_derived(amark_node_t *node)
{
    size_t i;

    if (node->facets == NULL)
    {
        return;
    }
    for (i = 0; i < node->desc->facet_count; i++)
    {
        amark_free(node->facets[i].cache);
        node->facets[i].cache = NULL;
        node->facets[i].cache_length = 0;
    }
}

/* The change of the tokens of node: its derived values are dropped and the
 * change is notified. */
static void tokens_changed(amark_node_t *node)
{
    drop_derived(node);
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
        if (moved[i].facet > to->desc->facet_count)
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
