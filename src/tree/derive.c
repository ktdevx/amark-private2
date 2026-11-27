/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * Facets: the values derived from the tokens (the decoding of each kind and the
 * facet flags), the values of the CONTENT facets derived from the content of
 * the leaf, and the string cache.
 *
 * A STRING facet spelled by tokens (layout tokens of the block, or content
 * tokens of the node, never both) takes the value of its tokens, concatenated
 * in their order and decoded by kind:
 *   TEXT, TAB_REMAINDER   the bytes;
 *   ESCAPE                the character after the backslash (RAW: the bytes);
 *   CHAR_REF              amark_char_ref_decode (RAW: the bytes);
 *   REPLACED              U+FFFD for each replaced unit (a NUL byte, an invalid
 *                         sequence);
 *   LINE_BREAK            LF for each line break (FOLD_LINE_BREAKS: a space);
 *   MARKER, SPACE, DELIMITER nothing.
 * A LINES facet whose value is not empty ends with LF. A CONTENT facet (the
 * literals of code_block and html_block) always takes the bytes of the content
 * of its leaf, with the ranges of the REPLACED tokens of the leaf as U+FFFD,
 * and one LF more when the content has a line.
 *
 * The derivation writes to a sink: a buffer, for the value that the getter
 * caches in the facet (amark_tree_facet_t.cache), or a comparison with the
 * cache, for the internal check (which allocates nothing). The cache is dropped
 * whenever the tokens of the node or the content of the leaf change.
 */
#include "tree/tree.h"

#include "base/buffer_internal.h"

#include <amark/allocator.h>
#include <amark/buffer.h>
#include <amark/entity.h>
#include <amark/utf8.h>

#include <string.h>

/* ---- Sinks ------------------------------------------------------------------- */

/*
 * Where a derivation writes: appended to buffer, or, when buffer is NULL,
 * compared with the expected bytes from position on (differs is then set at
 * the first difference). total counts the bytes written and last is the last
 * one, for the final LF of a LINES facet. error keeps the first failure of the
 * buffer; nothing is written after it.
 */
typedef struct sink
{
    amark_buffer_t *buffer;
    const char *expected;
    size_t expected_length;
    size_t position;
    bool differs;
    size_t total;
    char last;
    amark_error_t error;
} sink_t;

static void emit(sink_t *sink, const char *bytes, size_t length)
{
    if ((length == 0) || (sink->error != AMARK_ERROR_NONE) || sink->differs)
    {
        return;
    }
    if (sink->buffer != NULL)
    {
        sink->error = amark_buffer_append(sink->buffer, bytes, length);
        if (sink->error != AMARK_ERROR_NONE)
        {
            return;
        }
    }
    else
    {
        if ((length > (sink->expected_length - sink->position)) ||
            (memcmp(sink->expected + sink->position, bytes, length) != 0))
        {
            sink->differs = true;
            return;
        }
        sink->position += length;
    }
    sink->total += length;
    sink->last = bytes[length - 1];
}

static void emit_repeat(sink_t *sink, char c, size_t count)
{
    size_t i;

    for (i = 0; i < count; i++)
    {
        emit(sink, &c, 1);
    }
}

static void emit_codepoint(sink_t *sink, uint32_t codepoint)
{
    char out[AMARK_UTF8_MAX_LENGTH];

    emit(sink, out, amark_utf8_encode(codepoint, out));
}

/* ---- Decoding ------------------------------------------------------------------ */

/* Writes the replaced bytes: U+FFFD for each NUL byte and each invalid sequence
 * (the maximal subpart of amark_utf8_decode). A valid character, which a
 * content token cannot hold, is written as it is. */
static void emit_replaced(sink_t *sink, const char *bytes, size_t length)
{
    size_t position = 0;

    while (position < length)
    {
        uint32_t codepoint = 0;
        size_t size;

        if (bytes[position] == '\0')
        {
            emit_codepoint(sink, AMARK_UTF8_REPLACEMENT);
            position++;
            continue;
        }
        size = amark_utf8_decode(bytes + position, length - position, &codepoint);
        if (codepoint == AMARK_UTF8_REPLACEMENT)
        {
            emit_codepoint(sink, AMARK_UTF8_REPLACEMENT);
        }
        else
        {
            emit(sink, bytes + position, size);
        }
        position += size;
    }
}

/* Writes the contribution of the bytes of a token of kind to a facet with the
 * flags (AMARK_FACET_*). */
static void emit_token(sink_t *sink, amark_spell_kind_t kind, uint32_t flags, const char *bytes,
                       size_t length)
{
    bool raw = (flags & AMARK_FACET_RAW) != 0;
    uint32_t codepoints[2];
    size_t count = 0;
    size_t i;

    switch (kind)
    {
    case AMARK_SPELL_TEXT:
    case AMARK_SPELL_TAB_REMAINDER:
        emit(sink, bytes, length);
        break;
    case AMARK_SPELL_ESCAPE:
        if (!raw && (length >= 2) && (bytes[0] == '\\'))
        {
            emit(sink, bytes + 1, length - 1);
        }
        else
        {
            emit(sink, bytes, length);
        }
        break;
    case AMARK_SPELL_CHAR_REF:
        if (!raw && amark_char_ref_decode(bytes, length, codepoints, &count))
        {
            for (i = 0; i < count; i++)
            {
                emit_codepoint(sink, codepoints[i]);
            }
        }
        else
        {
            emit(sink, bytes, length);
        }
        break;
    case AMARK_SPELL_REPLACED:
        emit_replaced(sink, bytes, length);
        break;
    case AMARK_SPELL_LINE_BREAK:
        emit_repeat(sink, ((flags & AMARK_FACET_FOLD_LINE_BREAKS) != 0) ? ' ' : '\n', length);
        break;
    case AMARK_SPELL_MARKER:
    case AMARK_SPELL_SPACE:
    case AMARK_SPELL_DELIMITER:
    default:
        break;
    }
}

/*
 * The byte of line that holds column, and the columns of a tab before column
 * when it is inside one (amark_document_column_to_byte without the index of the
 * tabs, so that the check allocates nothing): a walk of the line when it has a
 * tab. column is at most the width of the line (checked by the record).
 */
static void column_to_byte(const amark_tree_line_t *entry, const char *data, uint32_t column,
                           size_t *byte, uint32_t *tab_offset)
{
    uint64_t at = 0;
    size_t i;

    *tab_offset = 0;
    if (!entry->has_tab)
    {
        *byte = column;
        return;
    }
    for (i = 0; i < entry->length; i++)
    {
        uint64_t width = (data[i] == '\t') ? (4u - (at % 4u)) : 1u;

        if (at + width > column)
        {
            *byte = i;
            *tab_offset = (uint32_t)(column - at);
            return;
        }
        at += width;
    }
    *byte = entry->length;
}

bool amark_tree_layout_bytes_ok(const amark_document_t *document, amark_line_id_t line,
                                uint32_t start, uint32_t end, amark_spell_kind_t kind)
{
    const amark_tree_line_t *entry = &document->lines[line];
    const char *data = document->sources[entry->source].data + entry->offset;
    size_t start_byte = 0;
    size_t end_byte = 0;
    uint32_t start_offset = 0;
    uint32_t end_offset = 0;
    size_t body;
    bool whole = (kind == AMARK_SPELL_ESCAPE) || (kind == AMARK_SPELL_CHAR_REF) ||
                 (kind == AMARK_SPELL_REPLACED);

    column_to_byte(entry, data, start, &start_byte, &start_offset);
    column_to_byte(entry, data, end, &end_byte, &end_offset);
    if (whole && ((start_offset > 0) || (end_offset > 0)))
    {
        return false;
    }
    body = (start_offset > 0) ? start_byte + 1 : start_byte;
    if (body >= end_byte)
    {
        /* Only the columns of a cut tab: spaces. */
        return !whole;
    }
    return amark_tree_spelled_bytes_ok(kind, data + body, end_byte - body);
}

/* Writes the contribution of a layout token: the bytes of its columns (a tab
 * cut by a boundary as spaces for its columns in the range), decoded by kind. */
static void emit_layout_token(sink_t *sink, const amark_document_t *document,
                              const amark_tree_layout_token_t *token, uint32_t flags)
{
    const amark_tree_line_t *entry = &document->lines[token->line];
    const char *data = document->sources[entry->source].data + entry->offset;
    size_t start_byte = 0;
    size_t end_byte = 0;
    uint32_t start_offset = 0;
    uint32_t end_offset = 0;
    size_t body;
    uint32_t lead = 0;
    bool spaces = (token->kind == AMARK_SPELL_TEXT) || (token->kind == AMARK_SPELL_ESCAPE) ||
                  (token->kind == AMARK_SPELL_CHAR_REF) ||
                  (token->kind == AMARK_SPELL_REPLACED);

    column_to_byte(entry, data, token->start, &start_byte, &start_offset);
    column_to_byte(entry, data, token->end, &end_byte, &end_offset);
    body = start_byte;
    if (start_offset > 0)
    {
        uint32_t tab_column = token->start - start_offset;
        uint32_t tab_end = tab_column + (4u - (tab_column % 4u));

        if (end_byte == start_byte)
        {
            /* Both boundaries in the same tab. */
            if (spaces)
            {
                emit_repeat(sink, ' ', token->end - token->start);
            }
            return;
        }
        lead = tab_end - token->start;
        body = start_byte + 1;
    }
    if (spaces)
    {
        emit_repeat(sink, ' ', lead);
    }
    emit_token(sink, (amark_spell_kind_t)token->kind, flags, data + body, end_byte - body);
    if (spaces)
    {
        emit_repeat(sink, ' ', end_offset);
    }
}

/* ---- Derivations ------------------------------------------------------------------ */

/* How the value of a facet is derived. */
typedef enum derivation
{
    DERIVE_NONE,
    DERIVE_TOKENS,
    DERIVE_CONTENT
} derivation_t;

bool amark_tree_layout_spells(const amark_node_t *node, amark_facet_t facet)
{
    const amark_tree_records_t *records = node->records;
    size_t i;

    if ((records == NULL) || (facet == AMARK_FACET_NONE))
    {
        return false;
    }
    for (i = 0; i < records->token_count; i++)
    {
        if (records->tokens[i].facet == facet)
        {
            return true;
        }
    }
    return false;
}

bool amark_tree_content_spells(const amark_node_t *node, amark_facet_t facet)
{
    const amark_tree_content_tokens_t *tokens = node->content_tokens;
    size_t i;

    if ((tokens == NULL) || (facet == AMARK_FACET_NONE))
    {
        return false;
    }
    for (i = 0; i < tokens->count; i++)
    {
        if (tokens->items[i].facet == facet)
        {
            return true;
        }
    }
    return false;
}

bool amark_tree_mixes_spelling(const amark_node_t *node, amark_facet_t facet, bool layout)
{
    const amark_facet_desc_t *desc;

    if (facet == AMARK_FACET_NONE)
    {
        return false;
    }
    desc = amark_node_type_facet(node->desc, facet);
    if ((desc == NULL) || (desc->kind != AMARK_FACET_STRING))
    {
        return false;
    }
    return layout ? amark_tree_content_spells(node, facet) : amark_tree_layout_spells(node, facet);
}

bool amark_tree_facet_dropped(const amark_node_t *node, amark_facet_t facet)
{
    return (facet != AMARK_FACET_NONE) && ((node->dropped & (1u << facet)) != 0);
}

static derivation_t derivation_of(const amark_node_t *node, amark_facet_t facet,
                                  const amark_facet_desc_t *desc)
{
    if ((desc->flags & AMARK_FACET_CONTENT) != 0)
    {
        return DERIVE_CONTENT;
    }
    if ((desc->kind == AMARK_FACET_STRING) &&
        (amark_tree_layout_spells(node, facet) || amark_tree_content_spells(node, facet)))
    {
        return DERIVE_TOKENS;
    }
    return DERIVE_NONE;
}

/* Writes the value of facet derived from the tokens of node. */
static void derive_tokens(sink_t *sink, const amark_node_t *node, amark_facet_t facet,
                          uint32_t flags)
{
    const amark_tree_records_t *records = node->records;
    const amark_tree_content_tokens_t *tokens = node->content_tokens;
    size_t i;

    if (records != NULL)
    {
        for (i = 0; i < records->token_count; i++)
        {
            if (records->tokens[i].facet == facet)
            {
                emit_layout_token(sink, node->document, &records->tokens[i], flags);
            }
        }
    }
    if (tokens != NULL)
    {
        for (i = 0; i < tokens->count; i++)
        {
            const amark_tree_content_token_t *item = &tokens->items[i];

            if (item->facet == facet)
            {
                emit_token(sink, (amark_spell_kind_t)item->kind, flags,
                           tokens->base->bytes + item->start, item->end - item->start);
            }
        }
    }
    if (((flags & AMARK_FACET_LINES) != 0) && (sink->total > 0) && (sink->last != '\n'))
    {
        emit(sink, "\n", 1);
    }
}

/* Writes the value of a CONTENT facet of leaf: its content, the ranges of its
 * REPLACED tokens on it as U+FFFD, then LF when it has a line. */
static void derive_content(sink_t *sink, const amark_node_t *leaf)
{
    const amark_tree_content_t *content = leaf->content;
    const amark_tree_content_tokens_t *tokens = leaf->content_tokens;
    size_t position = 0;
    size_t i;

    if (content == NULL)
    {
        return;
    }
    if (content->bytes == NULL)
    {
        /* Empty content: no byte, maybe one empty line. */
        if (content->line_count >= 1)
        {
            emit(sink, "\n", 1);
        }
        return;
    }
    if ((tokens != NULL) && (tokens->base == content))
    {
        for (i = 0; i < tokens->count; i++)
        {
            const amark_tree_content_token_t *item = &tokens->items[i];

            if (item->kind == AMARK_SPELL_REPLACED)
            {
                emit(sink, content->bytes + position, item->start - position);
                emit_replaced(sink, content->bytes + item->start, item->end - item->start);
                position = item->end;
            }
        }
    }
    emit(sink, content->bytes + position, content->length - position);
    if (content->line_count >= 1)
    {
        emit(sink, "\n", 1);
    }
}

static void derive(sink_t *sink, const amark_node_t *node, amark_facet_t facet,
                   const amark_facet_desc_t *desc, derivation_t how)
{
    if (how == DERIVE_CONTENT)
    {
        derive_content(sink, node);
    }
    else
    {
        derive_tokens(sink, node, facet, desc->flags);
    }
}

amark_error_t amark_tree_derive(const amark_node_t *node, amark_facet_t facet,
                                const amark_facet_desc_t *desc, bool *derived, const char **text,
                                size_t *length)
{
    /* The cache is not part of the value of the node: the getter fills it. */
    amark_tree_facet_t *slot = (amark_tree_facet_t *)&node->facets[facet - 1];
    derivation_t how = derivation_of(node, facet, desc);
    amark_buffer_t buffer;
    sink_t sink;
    char *built = NULL;
    size_t built_length = 0;
    amark_error_t error;

    *derived = how != DERIVE_NONE;
    if (how == DERIVE_NONE)
    {
        return AMARK_ERROR_NONE;
    }
    if (slot->cache == NULL)
    {
        amark_buffer_init(&buffer);
        memset(&sink, 0, sizeof(sink));
        sink.buffer = &buffer;
        derive(&sink, node, facet, desc, how);
        error = sink.error;
        if (error == AMARK_ERROR_NONE)
        {
            error = amark_buffer_detach(&buffer, &built, &built_length);
        }
        amark_buffer_deinit(&buffer);
        if (error != AMARK_ERROR_NONE)
        {
            return error;
        }
        slot->cache = built;
        slot->cache_length = built_length;
    }
    *text = slot->cache;
    *length = slot->cache_length;
    return AMARK_ERROR_NONE;
}

bool amark_tree_derived_cache_ok(const amark_node_t *node, amark_facet_t facet)
{
    const amark_facet_desc_t *desc = &node->desc->facets[facet - 1];
    const amark_tree_facet_t *slot = &node->facets[facet - 1];
    derivation_t how;
    sink_t sink;

    if (slot->cache == NULL)
    {
        return true;
    }
    how = derivation_of(node, facet, desc);
    if (how == DERIVE_NONE)
    {
        return false;
    }
    memset(&sink, 0, sizeof(sink));
    sink.expected = slot->cache;
    sink.expected_length = slot->cache_length;
    derive(&sink, node, facet, desc, how);
    return !sink.differs && (sink.position == slot->cache_length);
}

/* ---- Dropping ---------------------------------------------------------------------- */

static void drop_one(amark_tree_facet_t *slot)
{
    amark_free(slot->cache);
    slot->cache = NULL;
    slot->cache_length = 0;
}

void amark_tree_drop_derived(amark_node_t *node, amark_facet_t facet)
{
    size_t i;

    if (node->facets == NULL)
    {
        return;
    }
    if (facet != AMARK_FACET_NONE)
    {
        if (facet <= node->desc->facet_count)
        {
            drop_one(&node->facets[facet - 1]);
        }
        return;
    }
    for (i = 0; i < node->desc->facet_count; i++)
    {
        drop_one(&node->facets[i]);
    }
}

void amark_tree_drop_content_derived(amark_node_t *leaf)
{
    size_t i;

    if (leaf->facets == NULL)
    {
        return;
    }
    for (i = 0; i < leaf->desc->facet_count; i++)
    {
        if ((leaf->desc->facets[i].flags & AMARK_FACET_CONTENT) != 0)
        {
            drop_one(&leaf->facets[i]);
        }
    }
}
