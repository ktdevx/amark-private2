/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * LineTable: the sources, their division into lines, the incremental
 * completion, the BOM, the line endings, the columns and the bytes.
 *
 * Each source keeps the state of its unfinished line (amark_tree_source_t), so
 * that an append looks only at the new bytes (and at a CR left waiting at the
 * end). Every change goes in two passes over the same state: a dry scan on a
 * copy of the state counts the lines the bytes complete, so that all the
 * memory is reserved before anything changes (1.7); then the same scan on the
 * real state records them. Both passes run the same code, so they agree.
 */
#include "tree/tree.h"

#include <string.h>

#include <amark/allocator.h>

static const char bom[3] = {'\xEF', '\xBB', '\xBF'};

/* ---- Arrays --------------------------------------------------------------------- */

/* Grows array (of *capacity elements of size bytes) to hold at least needed
 * elements, doubling, and returns the block in *out (array itself when it is
 * large enough). On failure nothing changes. */
static amark_error_t reserve(void *array, size_t *capacity, size_t needed, size_t size,
                             void **out)
{
    size_t grown;
    void *block;

    *out = array;
    if (needed <= *capacity)
    {
        return AMARK_ERROR_NONE;
    }
    grown = *capacity < 8 ? 8 : *capacity;
    while (grown < needed)
    {
        if (grown > SIZE_MAX / 2)
        {
            grown = needed;
            break;
        }
        grown *= 2;
    }
    if (grown > SIZE_MAX / size)
    {
        return AMARK_ERROR_OVERFLOW;
    }
    block = amark_realloc(array, grown * size);
    if (block == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    *out = block;
    *capacity = grown;
    return AMARK_ERROR_NONE;
}

/* ---- The division into lines ----------------------------------------------------- */

/* The state of the division of one source (the fields of amark_tree_source_t
 * that a scan changes). */
typedef struct scan_state
{
    bool bom_known;
    bool has_bom;
    size_t line_start;
    size_t scanned;
    uint64_t width;
} scan_state_t;

/* Where the completed lines go: counted (dry scan) or recorded (commit). */
typedef struct line_sink
{
    /* NULL for a dry scan. */
    amark_document_t *document;
    amark_tree_source_t *source;
    amark_source_id_t id;
    /* The lines completed, and whether one of them is wider than UINT32_MAX. */
    size_t count;
    bool too_wide;
} line_sink_t;

static void emit(line_sink_t *sink, const scan_state_t *state, size_t end, amark_eol_t eol)
{
    amark_document_t *document = sink->document;
    amark_tree_line_t *line;
    amark_line_id_t id;

    sink->count++;
    if (state->width > UINT32_MAX)
    {
        sink->too_wide = true;
    }
    if (document == NULL)
    {
        return;
    }
    /* The commit: the memory is reserved. */
    id = (amark_line_id_t)document->line_count;
    line = &document->lines[document->line_count++];
    line->source = sink->id;
    line->number = (uint32_t)(sink->source->line_count + 1);
    line->offset = state->line_start;
    line->length = end - state->line_start;
    line->eol = (uint8_t)eol;
    line->width = (uint32_t)state->width;
    sink->source->lines[sink->source->line_count++] = id;
    if (document->default_eol == 0 && eol != AMARK_EOL_NONE)
    {
        document->default_eol = (uint8_t)eol;
    }
}

/*
 * Divides data[0, length) into lines from state, the end of the input included
 * when finish is true, and gives each completed line to sink. A CR at the end of
 * the data waits for the next byte, unless the input ends.
 */
static void scan(const char *data, size_t length, bool finish, scan_state_t *state,
                 line_sink_t *sink)
{
    if (!state->bom_known)
    {
        size_t n = length < sizeof(bom) ? length : sizeof(bom);

        bool prefix = n == 0 || memcmp(data, bom, n) == 0;

        /* While the bytes are a strict prefix of the BOM, wait. */
        if (prefix && n < sizeof(bom) && !finish)
        {
            return;
        }
        state->bom_known = true;
        state->has_bom = prefix && n == sizeof(bom);
        state->line_start = state->has_bom ? sizeof(bom) : 0;
        state->scanned = state->line_start;
        state->width = 0;
    }
    while (state->scanned < length)
    {
        char c = data[state->scanned];

        if (c == '\n' || c == '\r')
        {
            size_t end = state->scanned;
            size_t next = end + 1;
            amark_eol_t eol = AMARK_EOL_LF;

            if (c == '\r')
            {
                if (next == length && !finish)
                {
                    return;
                }
                eol = AMARK_EOL_CR;
                if (next < length && data[next] == '\n')
                {
                    eol = AMARK_EOL_CRLF;
                    next++;
                }
            }
            emit(sink, state, end, eol);
            state->line_start = next;
            state->scanned = next;
            state->width = 0;
            continue;
        }
        state->width += c == '\t' ? 4 - state->width % 4 : 1;
        state->scanned++;
    }
    if (finish && state->line_start < length)
    {
        emit(sink, state, length, AMARK_EOL_NONE);
        state->line_start = length;
        state->width = 0;
    }
}

static scan_state_t state_of(const amark_tree_source_t *source)
{
    scan_state_t state;

    state.bom_known = source->bom_known;
    state.has_bom = source->has_bom;
    state.line_start = source->line_start;
    state.scanned = source->scanned;
    state.width = source->width;
    return state;
}

/*
 * Appends the length bytes at bytes to source (identifier id) and records the
 * lines they complete, the end of the input included when finish is true. All
 * the memory is reserved first: on failure the source keeps its bytes, its
 * lines and its state (only its capacities may have grown).
 */
static amark_error_t feed(amark_document_t *document, amark_tree_source_t *source,
                          amark_source_id_t id, const char *bytes, size_t length, bool finish)
{
    line_sink_t sink;
    scan_state_t state;
    amark_error_t error;
    void *block;
    size_t total;

    if (length > SIZE_MAX - source->length)
    {
        return AMARK_ERROR_OVERFLOW;
    }
    total = source->length + length;
    error = reserve(source->data, &source->capacity, total, 1, &block);
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    source->data = (char *)block;
    if (length > 0)
    {
        /* Past the length: not part of the source until the commit. */
        memcpy(source->data + source->length, bytes, length);
    }

    /* The dry scan. */
    memset(&sink, 0, sizeof(sink));
    state = state_of(source);
    scan(source->data, total, finish, &state, &sink);
    if (sink.too_wide || sink.count > AMARK_TREE_MAX_LINES - document->line_count)
    {
        return AMARK_ERROR_LIMIT_EXCEEDED;
    }
    error = reserve(document->lines, &document->line_capacity,
                    document->line_count + sink.count, sizeof(amark_tree_line_t), &block);
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    document->lines = (amark_tree_line_t *)block;
    error = reserve(source->lines, &source->line_capacity, source->line_count + sink.count,
                    sizeof(amark_line_id_t), &block);
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    source->lines = (amark_line_id_t *)block;

    /* The commit. */
    sink.document = document;
    sink.source = source;
    sink.id = id;
    sink.count = 0;
    state = state_of(source);
    scan(source->data, total, finish, &state, &sink);
    source->length = total;
    source->bom_known = state.bom_known;
    source->has_bom = state.has_bom;
    source->line_start = state.line_start;
    source->scanned = state.scanned;
    source->width = state.width;
    source->finished = finish;
    return AMARK_ERROR_NONE;
}

static void source_deinit(amark_tree_source_t *source)
{
    amark_free(source->data);
    amark_free(source->lines);
}

/* The source id of document that can be appended to, or an error. */
static amark_error_t open_source(amark_document_t *document, amark_source_id_t source)
{
    if (document == NULL || source >= document->source_count)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    if (document->sources[source].finished)
    {
        return AMARK_ERROR_INVALID_STATE;
    }
    return AMARK_ERROR_NONE;
}

/* ---- Internal functions ------------------------------------------------------------ */

void amark_line_table_deinit(amark_document_t *document)
{
    size_t i;

    for (i = 0; i < document->source_count; i++)
    {
        source_deinit(&document->sources[i]);
    }
    amark_free(document->sources);
    amark_free(document->lines);
    document->sources = NULL;
    document->source_count = 0;
    document->source_capacity = 0;
    document->lines = NULL;
    document->line_count = 0;
    document->line_capacity = 0;
    document->default_eol = 0;
}

size_t amark_line_table_eol_length(uint8_t eol)
{
    switch (eol)
    {
    case AMARK_EOL_LF:
    case AMARK_EOL_CR:
        return 1;
    case AMARK_EOL_CRLF:
        return 2;
    default:
        return 0;
    }
}

/* ---- Public functions ------------------------------------------------------------- */

amark_error_t amark_document_add_source(amark_document_t *document, const char *bytes,
                                        size_t length, uint32_t flags, amark_source_id_t *out)
{
    amark_tree_source_t source;
    amark_error_t error;
    void *block;

    if (document == NULL || out == NULL || (bytes == NULL && length > 0) ||
        (flags & ~(uint32_t)AMARK_SOURCE_FINISHED) != 0)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    if (document->source_count >= UINT32_MAX)
    {
        return AMARK_ERROR_LIMIT_EXCEEDED;
    }
    error = reserve(document->sources, &document->source_capacity, document->source_count + 1,
                    sizeof(amark_tree_source_t), &block);
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    document->sources = (amark_tree_source_t *)block;
    memset(&source, 0, sizeof(source));
    error = feed(document, &source, (amark_source_id_t)document->source_count, bytes, length,
                 (flags & AMARK_SOURCE_FINISHED) != 0);
    if (error != AMARK_ERROR_NONE)
    {
        source_deinit(&source);
        return error;
    }
    *out = (amark_source_id_t)document->source_count;
    document->sources[document->source_count++] = source;
    return AMARK_ERROR_NONE;
}

amark_error_t amark_document_append_source(amark_document_t *document, amark_source_id_t source,
                                           const char *bytes, size_t length)
{
    amark_error_t error;

    if (bytes == NULL && length > 0)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    error = open_source(document, source);
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    return feed(document, &document->sources[source], source, bytes, length, false);
}

amark_error_t amark_document_finish_source(amark_document_t *document, amark_source_id_t source)
{
    amark_error_t error = open_source(document, source);

    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    return feed(document, &document->sources[source], source, NULL, 0, true);
}

size_t amark_document_source_count(const amark_document_t *document)
{
    return document != NULL ? document->source_count : 0;
}

amark_error_t amark_document_source_data(const amark_document_t *document,
                                         amark_source_id_t source, const char **data,
                                         size_t *length, bool *has_bom)
{
    const amark_tree_source_t *entry;

    if (document == NULL || source >= document->source_count)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    entry = &document->sources[source];
    if (data != NULL)
    {
        *data = entry->data != NULL ? entry->data : "";
    }
    if (length != NULL)
    {
        *length = entry->length;
    }
    if (has_bom != NULL)
    {
        *has_bom = entry->has_bom;
    }
    return AMARK_ERROR_NONE;
}

size_t amark_document_line_count(const amark_document_t *document)
{
    return document != NULL ? document->line_count : 0;
}

amark_error_t amark_document_line(const amark_document_t *document, amark_line_id_t line,
                                  amark_line_info_t *out)
{
    const amark_tree_line_t *entry;

    if (document == NULL || out == NULL || out->struct_size < sizeof(amark_line_info_t) ||
        line >= document->line_count)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    entry = &document->lines[line];
    out->source = entry->source;
    out->number = entry->number;
    out->offset = entry->offset;
    out->length = entry->length;
    out->eol = (amark_eol_t)entry->eol;
    out->width = entry->width;
    return AMARK_ERROR_NONE;
}

amark_eol_t amark_document_default_eol(const amark_document_t *document)
{
    if (document == NULL || document->default_eol == 0)
    {
        return AMARK_EOL_LF;
    }
    return (amark_eol_t)document->default_eol;
}
