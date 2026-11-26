/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/* Documents: creation and destruction, sources and their line index, positions,
 * edit tracking switch, syntax set ownership. */

#include <amark/document.h>

#include <amark/allocator.h>
#include <amark/error.h>
#include <amark/extension.h>
#include <amark/node.h>

#include "base/allocator_internal.h"
#include "base/buffer_internal.h"
#include "tree/syntax_set.h"
#include "tree/tree.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

amark_error_t amark_document_create(const amark_syntax_set_t *syntax,
                                    amark_document_t **out_document)
{
    amark_document_t *document;
    amark_error_t err;

    if (out_document == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    *out_document = NULL;
    if (syntax == NULL)
    {
        syntax = amark_syntax_set_core();
    }

    if (!amark_syntax_set_is_frozen(syntax))
    {
        return AMARK_ERROR_INVALID_STATE;
    }

    /* Zero-filled: no node, no source, no owned syntax set, tracking off, the
     * internal counters at 0. */
    document = amark_calloc(1, sizeof(*document));
    if (document == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }

    document->syntax = syntax;

    err = amark_node_create_with_desc(document, AMARK_NODE_DOCUMENT,
                                      amark_syntax_set_desc(syntax, AMARK_NODE_DOCUMENT),
                                      &document->root);
    if (err != AMARK_ERROR_NONE)
    {
        amark_free(document);
        return err;
    }

    *out_document = document;
    return AMARK_ERROR_NONE;
}

void amark_document_destroy(amark_document_t *document)
{
    size_t i;

    if (document == NULL)
    {
        return;
    }

    /* Every node, the root and the detached subtrees alike: each subtree of the
     * ring is freed from its top. Going up costs at most the size of the subtree
     * freed, so the whole is linear. */
    while (document->ring != NULL)
    {
        amark_node_t *top = document->ring;

        while (top->parent != NULL)
        {
            top = top->parent;
        }

        amark_node_destroy_subtree(top);
    }

    document->root = NULL;

    /* Then the sources. */
    for (i = 0; i < document->source_count; i++)
    {
        amark_buffer_deinit(&document->sources[i].bytes);
        amark_free(document->sources[i].line_starts);
    }

    amark_free(document->sources);

    /* Then the syntax set the document owns (NULL when it owns none), whose
     * descriptors the deinit functions of the nodes may still have used. */
    amark_syntax_set_destroy(document->owned_syntax);

    amark_free(document);
}

amark_node_t *amark_document_root(const amark_document_t *document)
{
    return (document != NULL) ? document->root : NULL;
}

const amark_syntax_set_t *amark_document_syntax_set(const amark_document_t *document)
{
    return (document != NULL) ? document->syntax : NULL;
}

void amark_document_set_edit_tracking(amark_document_t *document, bool enabled)
{
    if (document != NULL)
    {
        document->tracking = enabled;
    }
}

bool amark_document_edit_tracking(const amark_document_t *document)
{
    return (document != NULL) && document->tracking;
}

amark_error_t amark_document_take_syntax_set(amark_document_t *document,
                                             amark_syntax_set_t *set)
{
    /* Only the heap set the document itself uses, once: the static core set is
     * never freed, and a set the document does not use, or a second one, would be
     * freed while something else may still use it. */
    if ((document == NULL) || (set == NULL) || (set != document->syntax) || set->is_static
        || (document->owned_syntax != NULL))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    document->owned_syntax = set;

    return AMARK_ERROR_NONE;
}

/* --- Sources ------------------------------------------------------------------ */

/*
 * The line index of a source: line_starts[0] is 0 and each LF, CR or CRLF ends a
 * line, whose successor starts at the byte after it. A CR is counted when it is
 * read, as if it ended the line on its own; when an LF follows it (in the same
 * chunk or at the start of the next one, pending_cr), the start of the line after
 * the CR moves from the LF to the byte after the LF. The LF of a CRLF is thus on
 * the line of its CR.
 */

/* The number of line starts that the length bytes at data add to a source whose
 * last byte is a CR when pending_cr is true. */
static size_t count_line_starts(const char *data, size_t length, bool pending_cr)
{
    size_t count = 0;
    size_t i;
    bool after_cr = pending_cr;

    for (i = 0; i < length; i++)
    {
        if ((data[i] == '\n') && !after_cr)
        {
            count++;
        }
        else if (data[i] == '\r')
        {
            count++;
        }

        after_cr = (data[i] == '\r');
    }

    return count;
}

/* Makes room for additional more line starts in source. Leaves source as it was on
 * a failure (only the capacity may grow on a success). */
static amark_error_t reserve_line_starts(amark_source_t *source, size_t additional)
{
    size_t required;
    size_t new_capacity;
    size_t bytes;
    size_t *new_starts;

    if (!amark_size_add(source->line_count, additional, &required))
    {
        return AMARK_ERROR_OVERFLOW;
    }

    if (required <= source->line_capacity)
    {
        return AMARK_ERROR_NONE;
    }

    if (!amark_size_mul(source->line_capacity, 2, &new_capacity) || (new_capacity < required))
    {
        new_capacity = required;
    }

    if (!amark_size_mul(new_capacity, sizeof(size_t), &bytes))
    {
        return AMARK_ERROR_OVERFLOW;
    }

    new_starts = amark_realloc(source->line_starts, bytes);
    if (new_starts == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }

    source->line_starts = new_starts;
    source->line_capacity = new_capacity;

    return AMARK_ERROR_NONE;
}

/*
 * Appends the length bytes at data (not NULL when length is not 0, and possibly
 * within the bytes of source) to source and updates its line index. Reserves the
 * line starts before appending the bytes, so that a failure leaves the bytes, the
 * line index and pending_cr as they were.
 */
static amark_error_t source_append(amark_source_t *source, const char *data, size_t length)
{
    size_t old_length = source->bytes.length;
    size_t new_length;
    size_t i;
    const char *bytes;
    amark_error_t err;

    if (length == 0)
    {
        return AMARK_ERROR_NONE;
    }

    if (!amark_size_add(old_length, length, &new_length))
    {
        return AMARK_ERROR_OVERFLOW;
    }

    err = reserve_line_starts(source, count_line_starts(data, length, source->pending_cr));
    if (err != AMARK_ERROR_NONE)
    {
        return err;
    }

    /* amark_buffer_append finds data again when it lies in the bytes of the source
     * and the bytes move (a reservation before it would leave data dangling), and
     * leaves the bytes unchanged on a failure; the line starts are not written
     * yet, so the line index is unchanged too. */
    err = amark_buffer_append(&source->bytes, data, length);
    if (err != AMARK_ERROR_NONE)
    {
        return err;
    }

    /* Index the bytes as stored: data may have moved with them. */
    bytes = source->bytes.data;
    for (i = old_length; i < new_length; i++)
    {
        if (bytes[i] == '\n')
        {
            if (source->pending_cr)
            {
                /* The LF of a CRLF: the next line starts after it. */
                source->line_starts[source->line_count - 1] = i + 1;
            }
            else
            {
                source->line_starts[source->line_count] = i + 1;
                source->line_count++;
            }
        }
        else if (bytes[i] == '\r')
        {
            source->line_starts[source->line_count] = i + 1;
            source->line_count++;
        }

        source->pending_cr = (bytes[i] == '\r');
    }

    return AMARK_ERROR_NONE;
}

/* The source source of document, or NULL when it is not registered. */
static amark_source_t *find_source(const amark_document_t *document, amark_source_id_t source)
{
    if ((document == NULL) || (source >= document->source_count))
    {
        return NULL;
    }

    return &document->sources[source];
}

amark_error_t amark_document_add_source(amark_document_t *document, const char *data,
                                        size_t length, amark_source_id_t *out_source)
{
    amark_source_t source;
    amark_error_t err;

    if ((document == NULL) || (out_source == NULL) || ((data == NULL) && (length != 0)))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    /* The identifiers are amark_source_id_t (uint32_t). */
    if (document->source_count >= UINT32_MAX)
    {
        return AMARK_ERROR_OVERFLOW;
    }

    /* Room in the array first, so that the registration cannot fail. */
    if (document->source_count == document->source_capacity)
    {
        size_t new_capacity;
        size_t bytes;
        amark_source_t *new_sources;

        if (!amark_size_mul(document->source_capacity, 2, &new_capacity))
        {
            return AMARK_ERROR_OVERFLOW;
        }

        if (new_capacity == 0)
        {
            new_capacity = 4;
        }

        if (!amark_size_mul(new_capacity, sizeof(amark_source_t), &bytes))
        {
            return AMARK_ERROR_OVERFLOW;
        }

        new_sources = amark_realloc(document->sources, bytes);
        if (new_sources == NULL)
        {
            return AMARK_ERROR_OUT_OF_MEMORY;
        }

        document->sources = new_sources;
        document->source_capacity = new_capacity;
    }

    /* The copy, with its line index: one line, which starts at 0. */
    amark_buffer_init(&source.bytes);
    source.line_starts = NULL;
    source.line_count = 0;
    source.line_capacity = 0;
    source.pending_cr = false;

    err = reserve_line_starts(&source, 1);
    if (err == AMARK_ERROR_NONE)
    {
        source.line_starts[0] = 0;
        source.line_count = 1;
        err = source_append(&source, data, length);
    }

    if (err != AMARK_ERROR_NONE)
    {
        amark_buffer_deinit(&source.bytes);
        amark_free(source.line_starts);
        return err;
    }

    document->sources[document->source_count] = source;
    *out_source = (amark_source_id_t)document->source_count;
    document->source_count++;

    return AMARK_ERROR_NONE;
}

amark_error_t amark_document_append_source(amark_document_t *document,
                                           amark_source_id_t source, const char *data,
                                           size_t length)
{
    amark_source_t *record = find_source(document, source);

    if ((record == NULL) || ((data == NULL) && (length != 0)))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    return source_append(record, data, length);
}

size_t amark_document_source_count(const amark_document_t *document)
{
    return (document != NULL) ? document->source_count : 0;
}

const char *amark_document_source_data(const amark_document_t *document,
                                       amark_source_id_t source, size_t *out_length)
{
    const amark_source_t *record = find_source(document, source);

    if (record == NULL)
    {
        if (out_length != NULL)
        {
            *out_length = 0;
        }

        return NULL;
    }

    if (out_length != NULL)
    {
        *out_length = record->bytes.length;
    }

    return record->bytes.data;
}

/* --- Positions ---------------------------------------------------------------- */

amark_error_t amark_document_position(const amark_document_t *document,
                                      amark_source_id_t source, size_t offset,
                                      size_t *out_line, size_t *out_column)
{
    const amark_source_t *record = find_source(document, source);
    size_t low;
    size_t high;

    if ((record == NULL) || (out_line == NULL) || (out_column == NULL)
        || (offset > record->bytes.length))
    {
        if (out_line != NULL)
        {
            *out_line = 0;
        }

        if (out_column != NULL)
        {
            *out_column = 0;
        }

        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    /* The last line whose start is at most offset: line_starts[low] <= offset <
     * line_starts[high] (high may be line_count). line_starts[0] is 0. */
    low = 0;
    high = record->line_count;
    while ((high - low) > 1)
    {
        size_t middle = low + ((high - low) / 2);

        if (record->line_starts[middle] <= offset)
        {
            low = middle;
        }
        else
        {
            high = middle;
        }
    }

    *out_line = low + 1;
    *out_column = (offset - record->line_starts[low]) + 1;

    return AMARK_ERROR_NONE;
}
