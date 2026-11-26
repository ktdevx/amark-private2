/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/* The source span of a node (amark_node_source_span). */

#include <amark/node.h>

#include <amark/document.h>
#include <amark/error.h>

#include "tree/tree.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* The next node of the subtree of root after node in preorder, NULL after the
 * last one. Never leaves the subtree (the siblings and the parent of root). */
static const amark_node_t *next_in_subtree(const amark_node_t *root, const amark_node_t *node)
{
    if (node->first_child != NULL)
    {
        return node->first_child;
    }

    while (node != root)
    {
        if (node->next != NULL)
        {
            return node->next;
        }

        node = node->parent;
    }

    return NULL;
}

/*
 * Finds the source of the first token of the subtree of root (in preorder, then
 * in the order of the tokens of each node) and the range [start, end) the tokens
 * of that source cover. Zero-length tokens count: they mark a position. Returns
 * false when the subtree has no token. Takes time linear in the nodes and the
 * tokens of the subtree, without recursion.
 */
static bool find_span(const amark_node_t *root, amark_source_id_t *out_source, size_t *out_start,
                      size_t *out_end)
{
    const amark_node_t *node;
    bool found = false;
    amark_source_id_t source = 0;
    size_t start = 0;
    size_t end = 0;

    for (node = root; node != NULL; node = next_in_subtree(root, node))
    {
        size_t i;

        for (i = 0; i < node->token_count; i++)
        {
            const amark_token_record_t *record = &node->tokens[i];

            /* The range was checked against the source when the token was added,
             * so offset + length fits in size_t. */
            size_t token_end = record->offset + record->length;

            if (!found)
            {
                found = true;
                source = record->source;
                start = record->offset;
                end = token_end;
                continue;
            }

            if (record->source != source)
            {
                continue;
            }

            if (record->offset < start)
            {
                start = record->offset;
            }

            if (token_end > end)
            {
                end = token_end;
            }
        }
    }

    *out_source = source;
    *out_start = start;
    *out_end = end;

    return found;
}

bool amark_node_source_span(const amark_node_t *node, amark_source_span_t *out_span)
{
    amark_source_span_t span;
    amark_source_id_t source;
    size_t start;
    size_t end;

    if ((node == NULL) || (out_span == NULL)
        || (out_span->struct_size < sizeof(amark_source_span_t)))
    {
        return false;
    }

    if (!find_span(node, &source, &start, &end))
    {
        return false;
    }

    /* Writes the fields of version 1 and keeps the struct_size of the caller. */
    memset(&span, 0, sizeof(span));
    span.struct_size = out_span->struct_size;
    span.source = source;
    span.start_offset = start;
    span.end_offset = end;
    if ((amark_document_position(node->document, source, start, &span.start_line,
                                 &span.start_column) != AMARK_ERROR_NONE)
        || (amark_document_position(node->document, source, end, &span.end_line,
                                    &span.end_column) != AMARK_ERROR_NONE))
    {
        return false;
    }

    memcpy(out_span, &span, sizeof(span));

    return true;
}
