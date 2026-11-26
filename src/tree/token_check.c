/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/* The lossless check of the syntax tokens (amark_document_check_tokens). */

#include <amark/document.h>

#include <amark/allocator.h>
#include <amark/error.h>
#include <amark/node.h>
#include <amark/token.h>

#include "base/allocator_internal.h"
#include "tree/tree.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The columns of a tab stop. */
#define TAB_WIDTH 4u

/* A token of the check and the node that owns it. */
typedef struct check_entry
{
    const amark_token_record_t *token;
    const amark_node_t *owner;
} check_entry_t;

/* The state of the scan: the bytes of the source, the next byte that no token has
 * covered yet, the column of that byte and the owner of the last token. */
typedef struct check_scan
{
    const unsigned char *bytes;
    size_t length;
    size_t bom_end;
    size_t position;
    size_t column;
    const amark_node_t *previous_owner;
} check_scan_t;

/* The first problem found. */
typedef struct check_problem
{
    amark_token_problem_t problem;
    size_t offset;
    const amark_node_t *node;
} check_problem_t;

/* Writes the empty report (no problem) when report is not NULL. */
static void clear_report(amark_token_report_t *report)
{
    if (report != NULL)
    {
        report->problem = AMARK_TOKEN_PROBLEM_NONE;
        report->offset = 0;
        report->node = NULL;
    }
}

/* The next node of the tree after node in preorder, NULL after the last one. */
static const amark_node_t *next_in_preorder(const amark_node_t *node)
{
    if (node->first_child != NULL)
    {
        return node->first_child;
    }

    while (node != NULL)
    {
        if (node->next != NULL)
        {
            return node->next;
        }

        node = node->parent;
    }

    return NULL;
}

/* Whether record is a token of the check: it refers to source and covers at least
 * one byte (zero-length tokens, such as the lazy continuations, cover nothing). */
static bool is_checked(const amark_token_record_t *record, amark_source_id_t source)
{
    return ((record->source == source) && (record->length != 0));
}

/*
 * Visits the nodes reachable from the root of document in preorder, without
 * recursion, and counts their checked tokens; when entries is not NULL, also
 * stores them in it, each node's in the order of its tokens.
 */
static size_t collect(const amark_document_t *document, amark_source_id_t source,
                      check_entry_t *entries)
{
    const amark_node_t *node;
    size_t count = 0;

    for (node = document->root; node != NULL; node = next_in_preorder(node))
    {
        size_t i;

        for (i = 0; i < node->token_count; i++)
        {
            if (!is_checked(&node->tokens[i], source))
            {
                continue;
            }

            if (entries != NULL)
            {
                entries[count].token = &node->tokens[i];
                entries[count].owner = node;
            }

            count++;
        }
    }

    return count;
}

/* Whether a comes after b in the order of the check (offset, tab_column_offset). */
static bool entry_after(const check_entry_t *a, const check_entry_t *b)
{
    if (a->token->offset != b->token->offset)
    {
        return (a->token->offset > b->token->offset);
    }

    return (a->token->tab_column_offset > b->token->tab_column_offset);
}

/*
 * Sorts the count entries stably, with a bottom-up merge sort that uses scratch
 * (count entries) and no recursion, in O(count log count). Returns the array that
 * holds the result (entries or scratch).
 */
static check_entry_t *sort_entries(check_entry_t *entries, check_entry_t *scratch, size_t count)
{
    check_entry_t *from = entries;
    check_entry_t *to = scratch;
    size_t width = 1;

    while (width < count)
    {
        size_t start = 0;

        while (start < count)
        {
            size_t middle = ((count - start) > width) ? (start + width) : count;
            size_t end = ((count - middle) > width) ? (middle + width) : count;
            size_t left = start;
            size_t right = middle;
            size_t out = start;

            /* On equal keys the left run first, so the sort is stable. */
            while ((left < middle) && (right < end))
            {
                if (entry_after(&from[left], &from[right]))
                {
                    to[out++] = from[right++];
                }
                else
                {
                    to[out++] = from[left++];
                }
            }

            while (left < middle)
            {
                to[out++] = from[left++];
            }

            while (right < end)
            {
                to[out++] = from[right++];
            }

            start = end;
        }

        {
            check_entry_t *swap = from;

            from = to;
            to = swap;
        }

        /* The runs of the next pass are twice as long (without overflowing). */
        width = (width > (count / 2)) ? count : (width * 2);
    }

    return from;
}

/* Moves the scan over the byte at its position, which a token covers, updating the
 * column: a BOM takes no column, a line ending starts a new line, a tab goes to the
 * next tab stop and any other byte takes one column. */
static void advance_byte(check_scan_t *scan)
{
    unsigned char byte = scan->bytes[scan->position];

    if (scan->position < scan->bom_end)
    {
        /* Line 1 starts after the BOM, which takes no column. */
        scan->position++;
        return;
    }

    if ((byte == '\n') || (byte == '\r'))
    {
        scan->column = 0;
    }
    else if (byte == '\t')
    {
        scan->column += TAB_WIDTH - (scan->column % TAB_WIDTH);
    }
    else
    {
        scan->column++;
    }

    scan->position++;
}

/* Records a problem in *out_problem and returns true (for a short return). */
static bool found(check_problem_t *out_problem, amark_token_problem_t problem, size_t offset,
                  const amark_node_t *node)
{
    out_problem->problem = problem;
    out_problem->offset = offset;
    out_problem->node = node;

    return true;
}

/*
 * Checks the group[0..count) of tokens that start at the position of the scan, at
 * least one of which is a partial tab: all of them are partial tabs of that tab,
 * whose columns follow each other from 0 and add up to the width of the tab at the
 * column of the scan. Moves the scan over the tab when they do.
 */
static bool check_partial_tab(check_scan_t *scan, const check_entry_t *group, size_t count,
                              check_problem_t *out_problem)
{
    size_t width = TAB_WIDTH - (scan->column % TAB_WIDTH);
    size_t columns = 0;
    size_t i;

    for (i = 0; i < count; i++)
    {
        if ((group[i].token->flags & AMARK_TOKEN_FLAG_PARTIAL_TAB) == 0)
        {
            /* A regular token covers the same tab. */
            return found(out_problem, AMARK_TOKEN_PROBLEM_TAB_COLUMNS, scan->position,
                         group[i].owner);
        }
    }

    for (i = 0; i < count; i++)
    {
        if (group[i].token->tab_column_offset != columns)
        {
            return found(out_problem, AMARK_TOKEN_PROBLEM_TAB_COLUMNS, scan->position,
                         group[i].owner);
        }

        columns += group[i].token->tab_columns;
    }

    if (columns != width)
    {
        return found(out_problem, AMARK_TOKEN_PROBLEM_TAB_COLUMNS, scan->position,
                     group[count - 1].owner);
    }

    advance_byte(scan);
    scan->previous_owner = group[count - 1].owner;

    return false;
}

/*
 * Scans the count sorted entries over the source and stores the first problem in
 * *out_problem; returns whether there is one.
 */
static bool scan_entries(check_scan_t *scan, const check_entry_t *entries, size_t count,
                         check_problem_t *out_problem)
{
    size_t i = 0;

    while (i < count)
    {
        const check_entry_t *entry = &entries[i];
        size_t offset = entry->token->offset;
        size_t group_end;
        size_t end;
        bool partial = false;

        if (offset > scan->position)
        {
            return found(out_problem, AMARK_TOKEN_PROBLEM_GAP, scan->position,
                         scan->previous_owner);
        }

        if (offset < scan->position)
        {
            /* The byte at offset is already covered: by a regular token when this
             * is a partial tab (the partial tabs of one tab form one group). */
            if ((entry->token->flags & AMARK_TOKEN_FLAG_PARTIAL_TAB) != 0)
            {
                return found(out_problem, AMARK_TOKEN_PROBLEM_TAB_COLUMNS, offset,
                             entry->owner);
            }

            return found(out_problem, AMARK_TOKEN_PROBLEM_OVERLAP, offset, entry->owner);
        }

        /* The tokens that start at the position. */
        for (group_end = i; group_end < count; group_end++)
        {
            if (entries[group_end].token->offset != offset)
            {
                break;
            }

            if ((entries[group_end].token->flags & AMARK_TOKEN_FLAG_PARTIAL_TAB) != 0)
            {
                partial = true;
            }
        }

        if (partial)
        {
            if (check_partial_tab(scan, &entries[i], group_end - i, out_problem))
            {
                return true;
            }

            i = group_end;
            continue;
        }

        if ((group_end - i) > 1)
        {
            return found(out_problem, AMARK_TOKEN_PROBLEM_OVERLAP, offset,
                         entries[i + 1].owner);
        }

        end = offset + entry->token->length;
        while (scan->position < end)
        {
            advance_byte(scan);
        }

        scan->previous_owner = entry->owner;
        i++;
    }

    if (scan->position < scan->length)
    {
        return found(out_problem, AMARK_TOKEN_PROBLEM_GAP, scan->position,
                     scan->previous_owner);
    }

    return false;
}

amark_error_t amark_document_check_tokens(const amark_document_t *document,
                                          amark_source_id_t source,
                                          amark_token_report_t *out_report)
{
    const amark_source_t *record;
    check_entry_t *entries = NULL;
    const check_entry_t *sorted = NULL;
    check_scan_t scan;
    check_problem_t problem;
    size_t count;
    bool has_problem;

    if ((out_report != NULL) && (out_report->struct_size < sizeof(amark_token_report_t)))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    clear_report(out_report);
    if ((document == NULL) || (source >= document->source_count))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    record = &document->sources[source];

    /* The work area: the entries and the scratch of the sort, in one block. */
    count = collect(document, source, NULL);
    if (count != 0)
    {
        size_t bytes;

        if (!amark_size_mul(count, 2 * sizeof(check_entry_t), &bytes))
        {
            return AMARK_ERROR_OVERFLOW;
        }

        entries = amark_malloc(bytes);
        if (entries == NULL)
        {
            return AMARK_ERROR_OUT_OF_MEMORY;
        }

        (void)collect(document, source, entries);
        sorted = sort_entries(entries, entries + count, count);
    }

    scan.bytes = (const unsigned char *)record->bytes.data;
    scan.length = record->bytes.length;
    scan.bom_end = 0;
    if ((scan.length >= 3) && (scan.bytes[0] == 0xEFu) && (scan.bytes[1] == 0xBBu)
        && (scan.bytes[2] == 0xBFu))
    {
        scan.bom_end = 3;
    }

    scan.position = 0;
    scan.column = 0;
    scan.previous_owner = NULL;

    has_problem = scan_entries(&scan, sorted, count, &problem);
    amark_free(entries);

    if (!has_problem)
    {
        return AMARK_ERROR_NONE;
    }

    if (out_report != NULL)
    {
        out_report->problem = problem.problem;
        out_report->offset = problem.offset;
        out_report->node = problem.node;
    }

    return AMARK_ERROR_NOT_LOSSLESS;
}
