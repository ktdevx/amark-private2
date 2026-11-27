/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * LosslessCheck: the division of each line and the coverage of the content
 * (10.1-10.5, 14.7).
 *
 *   1. The division of the lines. Two walks of the tree (the nodes connected to
 *      the document node; the inline subtrees hold no layout record) gather the
 *      records on the lines of the source: the first counts them per line, the
 *      second places them, so that the records of each line are together
 *      (a counting sort by line). Each line is then checked with a difference
 *      array over its columns, and the boundaries of its records with a mark
 *      per column, both in one scratch array.
 *   2. The coverage of the contents: a third walk calls amark_tree_content_cover
 *      (Verbatim) for each block with content, with the same scratch.
 *
 * The scratch is allocated once, for the widest line and the longest content.
 * Nothing is recursive and the tree is never changed.
 */
#include "tree/tree.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <amark/allocator.h>
#include <amark/utf8.h>

/* A record on a line of the source: its columns [start, end), and whether it
 * refers to a line of no column (a blank line or a content piece does). */
typedef struct lossless_record
{
    uint32_t start;
    uint32_t end;
    bool refers;
} lossless_record_t;

/* The state of the check. */
typedef struct lossless_check
{
    const amark_document_t *document;
    const amark_tree_source_t *source;
    /* offsets[k] is the first record of the line k of the source (k from 0);
     * offsets[line_count] is the number of records. During the first walk,
     * offsets[k + 1] counts the records of line k; during the second walk,
     * offsets[k] is where the next record of line k goes. */
    size_t *offsets;
    lossless_record_t *records;
    /* The longest content of the leaves (for the scratch). */
    size_t longest_content;
    size_t *steps;
} lossless_check_t;

/* The next node of the preorder walk of the tree of root after node; the
 * children of node are skipped unless descend. NULL at the end. */
static const amark_node_t *walk_next(const amark_node_t *node, const amark_node_t *root,
                                     bool descend)
{
    if (descend && node->first_child != NULL)
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

static bool is_block(const amark_node_t *node)
{
    return node->desc->level == AMARK_NODE_LEVEL_BLOCK;
}

/* Gives the index in the source of line in *index, or false when line is on
 * another source. */
static bool line_index(const lossless_check_t *check, amark_line_id_t line, size_t *index)
{
    const amark_tree_line_t *entry = &check->document->lines[line];

    if (&check->document->sources[entry->source] != check->source)
    {
        return false;
    }
    *index = (size_t)entry->number - 1u;
    return true;
}

/* One record on line: counted (place false) or placed (place true). */
static void visit_record(lossless_check_t *check, amark_line_id_t line, uint32_t start,
                         uint32_t end, bool refers, bool place)
{
    size_t index;

    (*check->steps)++;
    if (!line_index(check, line, &index))
    {
        return;
    }
    if (!place)
    {
        check->offsets[index + 1]++;
        return;
    }
    check->records[check->offsets[index]].start = start;
    check->records[check->offsets[index]].end = end;
    check->records[check->offsets[index]].refers = refers;
    check->offsets[index]++;
}

/* Counts or places the records of the block node. */
static void visit_block(lossless_check_t *check, const amark_node_t *node, bool place)
{
    const amark_tree_records_t *records = node->records;
    const amark_tree_content_t *content = node->content;
    size_t i;

    if (records != NULL)
    {
        for (i = 0; i < records->margin_count; i++)
        {
            const amark_tree_margin_t *margin = &records->margins[i];

            /* A lazy margin has no columns. */
            if (margin->state != AMARK_MARGIN_LAZY)
            {
                visit_record(check, margin->line, margin->start, margin->end, false, place);
            }
        }
        for (i = 0; i < records->blank_line_count; i++)
        {
            const amark_tree_blank_line_t *blank = &records->blank_lines[i];

            visit_record(check, blank->line, blank->start, blank->end, true, place);
        }
        for (i = 0; i < records->token_count; i++)
        {
            const amark_tree_layout_token_t *token = &records->tokens[i];

            visit_record(check, token->line, token->start, token->end, false, place);
        }
    }
    if (content != NULL)
    {
        if (content->state == AMARK_CONTENT_LAID)
        {
            for (i = 0; i < content->piece_count; i++)
            {
                const amark_tree_piece_t *piece = &content->pieces[i];

                visit_record(check, piece->line, piece->start, piece->end, true, place);
            }
        }
        if (content->length > check->longest_content)
        {
            check->longest_content = content->length;
        }
    }
}

/* Walks the tree of root and counts or places the records of its blocks. */
static void walk_records(lossless_check_t *check, const amark_node_t *root, bool place)
{
    const amark_node_t *node = root;

    while (node != NULL)
    {
        bool block = is_block(node);

        (*check->steps)++;
        if (block)
        {
            visit_block(check, node, place);
        }
        node = walk_next(node, root, block);
    }
}

/* Makes room for needed counts in scratch (its old counts are not kept). The
 * array is the one amark_tree_content_cover grows and
 * amark_tree_cover_scratch_free frees. */
static amark_error_t reserve_scratch(amark_tree_cover_scratch_t *scratch, size_t needed)
{
    int32_t *grown;

    if (needed <= scratch->capacity)
    {
        return AMARK_ERROR_NONE;
    }
    if (needed > SIZE_MAX / sizeof(*grown))
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    grown = (int32_t *)amark_malloc(needed * sizeof(*grown));
    if (grown == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    amark_free(scratch->counts);
    scratch->counts = grown;
    scratch->capacity = needed;
    return AMARK_ERROR_NONE;
}

/* The first inconsistency of the division of the lines. */
typedef struct line_problem
{
    amark_lossless_problem_t problem;
    amark_line_id_t line;
    uint32_t column;
} line_problem_t;

/*
 * Checks one column of a line: a record boundary at it (marked) must not be
 * inside a character other than a tab (boundary false), and, before the width,
 * the running count of the records that cover it must be 1. Returns false and
 * fills *out at the first problem.
 */
static bool check_column(const int32_t *diff, const int32_t *marks, uint32_t column,
                         uint32_t width, bool boundary, int32_t *running, line_problem_t *out)
{
    if (marks[column] != 0 && !boundary)
    {
        out->problem = AMARK_LOSSLESS_SPLIT_CHARACTER;
        out->column = column;
        return false;
    }
    if (column < width)
    {
        *running += diff[column];
        if (*running != 1)
        {
            out->problem = (*running == 0) ? AMARK_LOSSLESS_GAP : AMARK_LOSSLESS_OVERLAP;
            out->column = column;
            return false;
        }
    }
    return true;
}

/*
 * Checks the line of identifier id with the records [first, last) of check,
 * using counts (at least 2 * (width + 1) entries) for the difference array and
 * the boundary marks. Returns false and fills *out at the first problem.
 */
static bool check_line(lossless_check_t *check, amark_line_id_t id, size_t first, size_t last,
                       int32_t *counts, line_problem_t *out)
{
    const amark_tree_line_t *line = &check->document->lines[id];
    const char *text = check->document->sources[line->source].data + line->offset;
    uint32_t width = line->width;
    int32_t *diff = counts;
    int32_t *marks = counts + (size_t)width + 1u;
    int32_t running = 0;
    bool referred = false;
    uint32_t column = 0;
    size_t byte = 0;
    size_t i;

    out->line = id;
    memset(counts, 0, (2u * ((size_t)width + 1u)) * sizeof(*counts));
    for (i = first; i < last; i++)
    {
        const lossless_record_t *record = &check->records[i];

        diff[record->start]++;
        diff[record->end]--;
        marks[record->start] = 1;
        marks[record->end] = 1;
        referred = referred || record->refers;
    }
    *check->steps += 1u + (size_t)width + line->length;
    if (width == 0 && !referred)
    {
        out->problem = AMARK_LOSSLESS_GAP;
        out->column = 0;
        return false;
    }
    /* The columns in order, with whether each one is a boundary of
     * characters: a tab spans up to 4 columns, all boundaries (a tab is
     * divided by columns); another character spans one column per byte, the
     * first one only being a boundary. */
    while (byte < line->length)
    {
        uint32_t span;
        uint32_t k;
        bool is_tab = text[byte] == '\t';

        if (is_tab)
        {
            span = 4u - column % 4u;
            byte++;
        }
        else
        {
            span = (uint32_t)amark_utf8_decode(text + byte, line->length - byte, NULL);
            byte += span;
        }
        for (k = 0; k < span; k++)
        {
            if (!check_column(diff, marks, column, width, is_tab || k == 0, &running, out))
            {
                return false;
            }
            column++;
        }
    }
    return check_column(diff, marks, width, width, true, &running, out);
}

/* Fills report, if not NULL, with the problem of a line. */
static void report_line(amark_lossless_report_t *report, const line_problem_t *problem)
{
    if (report != NULL)
    {
        report->problem = problem->problem;
        report->line = problem->line;
        report->column = problem->column;
        report->leaf = NULL;
        report->content_offset = 0;
    }
}

/* Checks the division of the lines of the source (records placed). */
static bool check_lines(lossless_check_t *check, int32_t *counts, line_problem_t *out)
{
    size_t k;
    size_t first = 0;

    for (k = 0; k < check->source->line_count; k++)
    {
        /* After the second walk, offsets[k] is the end of the records of line
         * k, which is where the records of line k + 1 start. */
        size_t last = check->offsets[k];

        if (!check_line(check, check->source->lines[k], first, last, counts, out))
        {
            return false;
        }
        first = last;
    }
    return true;
}

/* Checks the coverage of the content of each block with content, in document
 * order. Fills report at the first problem and returns AMARK_ERROR_NOT_LOSSLESS. */
static amark_error_t check_contents(lossless_check_t *check, const amark_node_t *root,
                                    amark_tree_cover_scratch_t *scratch,
                                    amark_lossless_report_t *report)
{
    const amark_node_t *node = root;

    while (node != NULL)
    {
        bool block = is_block(node);

        (*check->steps)++;
        if (block && (node->desc->flags & AMARK_NODE_TYPE_CONTENT) != 0)
        {
            amark_tree_cover_t cover;
            const amark_node_t *under;
            amark_error_t error = amark_tree_content_cover(node, scratch, &cover);

            if (error != AMARK_ERROR_NONE)
            {
                return error;
            }
            /* The cost of the coverage: its bytes, and the nodes and tokens
             * under the leaf. */
            *check->steps += (node->content != NULL) ? node->content->length : 0u;
            for (under = node; under != NULL; under = walk_next(under, node, true))
            {
                (*check->steps)++;
                if (under->content_tokens != NULL)
                {
                    *check->steps += under->content_tokens->count;
                }
            }
            if (!cover.covered)
            {
                if (report != NULL)
                {
                    report->problem = (cover.problem == AMARK_TREE_COVER_GAP)
                                          ? AMARK_LOSSLESS_GAP
                                          : (cover.problem == AMARK_TREE_COVER_OVERLAP)
                                                ? AMARK_LOSSLESS_OVERLAP
                                                : AMARK_LOSSLESS_FOREIGN_TOKEN;
                    report->line = AMARK_LINE_NONE;
                    report->column = 0;
                    report->leaf = node;
                    report->content_offset = cover.offset;
                }
                return AMARK_ERROR_NOT_LOSSLESS;
            }
        }
        node = walk_next(node, root, block);
    }
    return AMARK_ERROR_NONE;
}

/* The widest line of the source. */
static uint32_t widest_line(const lossless_check_t *check)
{
    uint32_t widest = 0;
    size_t k;

    for (k = 0; k < check->source->line_count; k++)
    {
        uint32_t width = check->document->lines[check->source->lines[k]].width;

        if (width > widest)
        {
            widest = width;
        }
    }
    return widest;
}

amark_error_t amark_document_check_lossless(const amark_document_t *document,
                                            amark_source_id_t source,
                                            amark_lossless_report_t *report)
{
    lossless_check_t check;
    amark_tree_cover_scratch_t scratch;
    line_problem_t problem;
    const amark_node_t *root;
    size_t line_count;
    size_t record_count;
    size_t needed;
    size_t k;
    amark_error_t error = AMARK_ERROR_NONE;

    if (document == NULL || source >= document->source_count ||
        (report != NULL && report->struct_size < sizeof(amark_lossless_report_t)))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    root = document->root;
    memset(&check, 0, sizeof(check));
    memset(&scratch, 0, sizeof(scratch));
    check.document = document;
    check.source = &document->sources[source];
    /* The counters are internal statistics: the document is not changed. */
    check.steps = &root->document->stats.lossless_steps;
    line_count = check.source->line_count;

    /* 1. Count the records per line, then place them. */
    check.offsets = (size_t *)amark_calloc(line_count + 1u, sizeof(size_t));
    if (check.offsets == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    walk_records(&check, root, false);
    for (k = 0; k < line_count; k++)
    {
        check.offsets[k + 1] += check.offsets[k];
    }
    record_count = check.offsets[line_count];
    if (record_count > 0)
    {
        check.records =
            (lossless_record_t *)amark_calloc(record_count, sizeof(lossless_record_t));
        if (check.records == NULL)
        {
            error = AMARK_ERROR_OUT_OF_MEMORY;
            goto done;
        }
        walk_records(&check, root, true);
    }

    /* The scratch, once: the difference array and the marks of the widest
     * line, or the counts of the longest content. */
    needed = 2u * ((size_t)widest_line(&check) + 1u);
    if (check.longest_content + 1u > needed)
    {
        needed = check.longest_content + 1u;
    }
    error = reserve_scratch(&scratch, needed);
    if (error != AMARK_ERROR_NONE)
    {
        goto done;
    }

    /* 2. The division of the lines, then the coverage of the contents. */
    if (!check_lines(&check, scratch.counts, &problem))
    {
        report_line(report, &problem);
        error = AMARK_ERROR_NOT_LOSSLESS;
        goto done;
    }
    error = check_contents(&check, root, &scratch, report);

done:
    amark_tree_cover_scratch_free(&scratch);
    amark_free(check.records);
    amark_free(check.offsets);
    return error;
}
