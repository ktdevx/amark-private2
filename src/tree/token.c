/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/* Syntax tokens: checks, sorted insertion, removal, transfer and access. */

#include <amark/token.h>

#include <amark/allocator.h>
#include <amark/document.h>
#include <amark/error.h>
#include <amark/node.h>

#include "base/allocator_internal.h"
#include "tree/tree.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The flags amark_node_add_token knows. */
#define KNOWN_FLAGS (AMARK_TOKEN_FLAG_CONTAINER_PREFIX | AMARK_TOKEN_FLAG_PARTIAL_TAB)

/* The columns of a tab (tab stops of 4 columns). */
#define TAB_WIDTH 4u

/* The capacity of the first token array of a node. */
#define FIRST_CAPACITY 4u

/* --- Checks ------------------------------------------------------------------- */

/* Whether the length bytes at bytes are only spaces and tabs. */
static bool only_spaces(const unsigned char *bytes, size_t length)
{
    size_t i;

    for (i = 0; i < length; i++)
    {
        if ((bytes[i] != ' ') && (bytes[i] != '\t'))
        {
            return false;
        }
    }

    return true;
}

/* Whether the length bytes at bytes, at offset in their source, fit kind. */
static bool bytes_fit_kind(int kind, const unsigned char *bytes, size_t offset, size_t length)
{
    switch (kind)
    {
    case AMARK_TOKEN_TEXT:
        return true;
    case AMARK_TOKEN_MARKER:
    case AMARK_TOKEN_REPLACED:
        return (length >= 1);
    case AMARK_TOKEN_SPACE:
        return only_spaces(bytes, length);
    case AMARK_TOKEN_LINE_ENDING:
        if (length == 1)
        {
            return ((bytes[0] == '\n') || (bytes[0] == '\r'));
        }

        return ((length == 2) && (bytes[0] == '\r') && (bytes[1] == '\n'));
    case AMARK_TOKEN_ESCAPE:
        return ((length == 2) && (bytes[0] == '\\'));
    case AMARK_TOKEN_CHAR_REF:
        return ((length >= 2) && (bytes[0] == '&') && (bytes[length - 1] == ';'));
    case AMARK_TOKEN_BOM:
        return ((offset == 0) && (length == 3) && (bytes[0] == 0xEFu) && (bytes[1] == 0xBBu)
                && (bytes[2] == 0xBFu));
    case AMARK_TOKEN_LAZY_CONTINUATION:
        return (length == 0);
    default:
        return false;
    }
}

/*
 * Checks token for a node of document and, when it is valid, stores its record in
 * *out_record. Without AMARK_TOKEN_FLAG_PARTIAL_TAB the tab fields must be 0, so
 * that every valid token fits the record (tab fields of 8 bits).
 */
static amark_error_t check_token(const amark_document_t *document, const amark_token_t *token,
                                 amark_token_record_t *out_record)
{
    const amark_source_t *source;
    const unsigned char *bytes;
    int kind;

    if ((token == NULL) || (token->struct_size < sizeof(amark_token_t)))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    kind = (int)token->kind;
    if ((kind < (int)AMARK_TOKEN_TEXT) || (kind > (int)AMARK_TOKEN_LAZY_CONTINUATION)
        || ((token->flags & ~(uint32_t)KNOWN_FLAGS) != 0))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    /* The source and the range (offset + length may not fit in size_t). */
    if (token->source >= document->source_count)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    source = &document->sources[token->source];
    if ((token->offset > source->bytes.length)
        || (token->length > (source->bytes.length - token->offset)))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    /* The checks read no byte of a zero-length token (the data of an empty source
     * may be NULL). */
    bytes = NULL;
    if (token->length != 0)
    {
        bytes = (const unsigned char *)source->bytes.data + token->offset;
    }

    if (!bytes_fit_kind(kind, bytes, token->offset, token->length))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    /* One tab, some of its columns. */
    if ((token->flags & AMARK_TOKEN_FLAG_PARTIAL_TAB) != 0)
    {
        if ((token->length != 1) || (bytes[0] != '\t') || (token->tab_columns < 1)
            || (token->tab_columns > TAB_WIDTH)
            || (token->tab_column_offset > (TAB_WIDTH - token->tab_columns)))
        {
            return AMARK_ERROR_INVALID_ARGUMENT;
        }
    }
    else if ((token->tab_column_offset != 0) || (token->tab_columns != 0))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    out_record->offset = token->offset;
    out_record->length = token->length;
    out_record->source = token->source;
    out_record->role = token->role;
    out_record->kind = (uint8_t)kind;
    out_record->flags = (uint8_t)token->flags;
    out_record->tab_column_offset = (uint8_t)token->tab_column_offset;
    out_record->tab_columns = (uint8_t)token->tab_columns;

    return AMARK_ERROR_NONE;
}

/* --- Order -------------------------------------------------------------------- */

/* Whether a comes after b in the order of the tokens (source, offset,
 * tab_column_offset); equal keys keep the order of addition. */
static bool record_after(const amark_token_record_t *a, const amark_token_record_t *b)
{
    if (a->source != b->source)
    {
        return (a->source > b->source);
    }

    if (a->offset != b->offset)
    {
        return (a->offset > b->offset);
    }

    return (a->tab_column_offset > b->tab_column_offset);
}

/* The index at which record goes in the tokens of node: after every token that
 * does not come after it (an upper bound, so equal keys keep their order). */
static size_t insertion_index(const amark_node_t *node, const amark_token_record_t *record)
{
    size_t low = 0;
    size_t high = node->token_count;

    while (low < high)
    {
        size_t middle = low + ((high - low) / 2);

        if (record_after(&node->tokens[middle], record))
        {
            high = middle;
        }
        else
        {
            low = middle + 1;
        }
    }

    return low;
}

/* Makes room for additional more tokens in node. Leaves node as it was on a
 * failure (only the capacity may grow on a success). */
static amark_error_t reserve_tokens(amark_node_t *node, size_t additional)
{
    size_t required;
    size_t new_capacity;
    size_t bytes;
    amark_token_record_t *new_tokens;

    if (!amark_size_add(node->token_count, additional, &required))
    {
        return AMARK_ERROR_OVERFLOW;
    }

    if (required <= node->token_capacity)
    {
        return AMARK_ERROR_NONE;
    }

    if (!amark_size_mul(node->token_capacity, 2, &new_capacity) || (new_capacity < required))
    {
        new_capacity = required;
    }

    if (new_capacity < FIRST_CAPACITY)
    {
        new_capacity = FIRST_CAPACITY;
    }

    if (!amark_size_mul(new_capacity, sizeof(amark_token_record_t), &bytes))
    {
        return AMARK_ERROR_OVERFLOW;
    }

    new_tokens = amark_realloc(node->tokens, bytes);
    if (new_tokens == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }

    node->tokens = new_tokens;
    node->token_capacity = new_capacity;

    return AMARK_ERROR_NONE;
}

/* --- Public functions ----------------------------------------------------------- */

size_t amark_node_token_count(const amark_node_t *node)
{
    return (node == NULL) ? 0 : node->token_count;
}

amark_error_t amark_node_token_at(const amark_node_t *node, size_t index, amark_token_t *out_token)
{
    const amark_token_record_t *record;
    amark_token_t token;

    if ((node == NULL) || (out_token == NULL) || (out_token->struct_size < sizeof(amark_token_t))
        || (index >= node->token_count))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    /* Writes the fields of version 1 and keeps the struct_size of the caller. */
    record = &node->tokens[index];
    memset(&token, 0, sizeof(token));
    token.struct_size = out_token->struct_size;
    token.kind = (amark_token_kind_t)record->kind;
    token.role = record->role;
    token.flags = record->flags;
    token.source = record->source;
    token.offset = record->offset;
    token.length = record->length;
    token.tab_column_offset = record->tab_column_offset;
    token.tab_columns = record->tab_columns;
    memcpy(out_token, &token, sizeof(token));

    return AMARK_ERROR_NONE;
}

amark_error_t amark_node_add_token(amark_node_t *node, const amark_token_t *token)
{
    amark_token_record_t record;
    size_t index;
    amark_error_t err;

    if (node == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    err = check_token(node->document, token, &record);
    if (err != AMARK_ERROR_NONE)
    {
        return err;
    }

    err = reserve_tokens(node, 1);
    if (err != AMARK_ERROR_NONE)
    {
        return err;
    }

    /* In source order the token goes at the end without a search. */
    if ((node->token_count == 0) || !record_after(&node->tokens[node->token_count - 1], &record))
    {
        index = node->token_count;
    }
    else
    {
        index = insertion_index(node, &record);
        memmove(&node->tokens[index + 1], &node->tokens[index],
                (node->token_count - index) * sizeof(amark_token_record_t));
        node->document->stat_token_moves += node->token_count - index;
    }

    node->tokens[index] = record;
    node->token_count++;

    return AMARK_ERROR_NONE;
}

amark_error_t amark_node_remove_token(amark_node_t *node, size_t index)
{
    if ((node == NULL) || (index >= node->token_count))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    memmove(&node->tokens[index], &node->tokens[index + 1],
            (node->token_count - index - 1) * sizeof(amark_token_record_t));
    node->token_count--;

    return AMARK_ERROR_NONE;
}

amark_error_t amark_node_transfer_tokens(amark_node_t *from, size_t first, size_t count,
                                         amark_node_t *to)
{
    const amark_token_record_t *moved;
    size_t end;
    size_t i;
    size_t j;
    size_t k;
    amark_error_t err;

    if ((from == NULL) || (to == NULL) || (from->document != to->document)
        || !amark_size_add(first, count, &end) || (end > from->token_count))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    /* Moving tokens to their own node, at their sorted places, changes nothing. */
    if ((count == 0) || (from == to))
    {
        return AMARK_ERROR_NONE;
    }

    /* The only allocation, before any change. */
    err = reserve_tokens(to, count);
    if (err != AMARK_ERROR_NONE)
    {
        return err;
    }

    /* Merges the sorted range into to from the back; on equal keys the moved tokens
     * go after the tokens of to, as if added after them. */
    moved = &from->tokens[first];
    i = to->token_count;
    j = count;
    k = to->token_count + count;
    while (j > 0)
    {
        if ((i > 0) && record_after(&to->tokens[i - 1], &moved[j - 1]))
        {
            to->tokens[k - 1] = to->tokens[i - 1];
            i--;
        }
        else
        {
            to->tokens[k - 1] = moved[j - 1];
            j--;
        }

        k--;
    }

    to->document->stat_token_moves += to->token_count - i;
    to->token_count += count;

    /* Closes the gap in from. */
    memmove(&from->tokens[first], &from->tokens[end],
            (from->token_count - end) * sizeof(amark_token_record_t));
    from->token_count -= count;

    return AMARK_ERROR_NONE;
}

const char *amark_token_kind_name(amark_token_kind_t kind)
{
    switch ((int)kind)
    {
    case AMARK_TOKEN_TEXT:
        return "text";
    case AMARK_TOKEN_MARKER:
        return "marker";
    case AMARK_TOKEN_SPACE:
        return "space";
    case AMARK_TOKEN_LINE_ENDING:
        return "line_ending";
    case AMARK_TOKEN_ESCAPE:
        return "escape";
    case AMARK_TOKEN_CHAR_REF:
        return "char_ref";
    case AMARK_TOKEN_REPLACED:
        return "replaced";
    case AMARK_TOKEN_BOM:
        return "bom";
    case AMARK_TOKEN_LAZY_CONTINUATION:
        return "lazy_continuation";
    default:
        return "unknown";
    }
}
