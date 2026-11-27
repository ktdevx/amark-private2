/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * Layout: the line ranges, the margins, the blank lines, the leaf spellings
 * and the checks of the records. (Moving, removing and trimming the lines of a
 * leaf rebuild its content: they are in content.c.)
 *
 * Each record array is kept in its order (margins and blank lines by line, one
 * per line; tokens by line and column without overlap) by refusing an append
 * out of order, so that an append is constant time amortized (14.6) and a
 * lookup by line is a binary search (11.2). A record is checked against the
 * line table (the line, the width, the character boundaries) and the line range
 * of its node before the record arrays are allocated (the check may build the
 * index of the tabs of the line, an internal cache of the line table), and the
 * node changes only once every allocation has succeeded.
 */
#include "tree/tree.h"

#include <amark/allocator.h>
#include <amark/utf8.h>

#include <string.h>

/* The first capacity of a record array. */
#define RECORDS_FIRST_CAPACITY 4u

/* ---- Records ---------------------------------------------------------------------- */

void amark_tree_records_free(amark_tree_records_t *records)
{
    if (records == NULL)
    {
        return;
    }
    amark_free(records->margins);
    amark_free(records->blank_lines);
    amark_free(records->tokens);
    amark_free(records);
}

/* The record structure of node in *out, allocated when node has none yet;
 * *created tells whether it was. The node is not changed: the caller stores it
 * once the record is added, or frees it (forget_records) on failure. */
static amark_error_t open_records(const amark_node_t *node, amark_tree_records_t **out,
                                  bool *created)
{
    *created = false;
    *out = node->records;
    if (*out == NULL)
    {
        *out = (amark_tree_records_t *)amark_calloc(1, sizeof(**out));
        if (*out == NULL)
        {
            return AMARK_ERROR_OUT_OF_MEMORY;
        }
        *created = true;
    }
    return AMARK_ERROR_NONE;
}

static void forget_records(amark_tree_records_t *records, bool created)
{
    if (created)
    {
        amark_tree_records_free(records);
    }
}

/*
 * Makes room for one more record in the array items of count records and
 * *capacity slots of size bytes: *grown receives the array (items itself when
 * there is room, else the grown array, and *capacity its new capacity). On
 * failure items and *capacity are unchanged. Counts the records moved by a
 * growth in the record steps of document.
 */
static amark_error_t grow(amark_document_t *document, void *items, size_t count,
                          size_t *capacity, size_t size, void **grown)
{
    size_t new_capacity;

    *grown = items;
    if (count < *capacity)
    {
        return AMARK_ERROR_NONE;
    }
    new_capacity = (*capacity == 0) ? RECORDS_FIRST_CAPACITY : *capacity * 2;
    if ((new_capacity < *capacity) || (new_capacity > (SIZE_MAX / size)))
    {
        return AMARK_ERROR_OVERFLOW;
    }
    *grown = amark_realloc(items, new_capacity * size);
    if (*grown == NULL)
    {
        *grown = items;
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    *capacity = new_capacity;
    document->stats.record_steps += count;
    return AMARK_ERROR_NONE;
}

amark_error_t amark_tree_reserve_layout_tokens(amark_node_t *node, size_t extra,
                                               amark_tree_records_t **out, bool *created)
{
    amark_tree_records_t *records = NULL;
    amark_tree_layout_token_t *grown;
    size_t needed;
    size_t capacity;
    amark_error_t error;

    error = open_records(node, &records, created);
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    if (extra > (SIZE_MAX - records->token_count))
    {
        forget_records(records, *created);
        return AMARK_ERROR_OVERFLOW;
    }
    needed = records->token_count + extra;
    if (needed > records->token_capacity)
    {
        capacity = (records->token_capacity < RECORDS_FIRST_CAPACITY) ? RECORDS_FIRST_CAPACITY
                                                                      : records->token_capacity;
        while (capacity < needed)
        {
            capacity = (capacity > (SIZE_MAX / 2)) ? needed : capacity * 2;
        }
        if (capacity > (SIZE_MAX / sizeof(*grown)))
        {
            forget_records(records, *created);
            return AMARK_ERROR_OVERFLOW;
        }
        grown = (amark_tree_layout_token_t *)amark_realloc(records->tokens,
                                                           capacity * sizeof(*grown));
        if (grown == NULL)
        {
            forget_records(records, *created);
            return AMARK_ERROR_OUT_OF_MEMORY;
        }
        records->tokens = grown;
        records->token_capacity = capacity;
        node->document->stats.record_steps += records->token_count;
    }
    *out = records;
    return AMARK_ERROR_NONE;
}

/* ---- Checks ------------------------------------------------------------------------- */

/* Whether line is in the line range of node. */
static bool in_range(const amark_node_t *node, amark_line_id_t line)
{
    return (node->line_count > 0) && (line >= node->line_first) &&
           ((line - node->line_first) < node->line_count);
}

/* Whether node is a block that can have block children (the document, a list,
 * a container). */
static bool has_block_children(const amark_node_t *node)
{
    return (node->desc->level == AMARK_NODE_LEVEL_BLOCK) &&
           ((node->desc->accepts & (AMARK_CONTENT_BLOCK | AMARK_CONTENT_LIST_ITEM)) != 0);
}

/* The bytes of line (without its line ending). */
static const char *line_bytes(const amark_document_t *document, amark_line_id_t line)
{
    const amark_tree_line_t *entry = &document->lines[line];
    const char *data = document->sources[entry->source].data;

    return (data != NULL) ? data + entry->offset : "";
}

/* Whether byte b of the length bytes at text is a boundary of the characters
 * decoded from the start (amark_utf8_decode, with the maximal subparts of the
 * invalid sequences). Every byte that is not a continuation byte starts a
 * character, so only the three bytes before b need to be looked at. */
static bool is_char_boundary(const char *text, size_t length, size_t b)
{
    size_t start;
    size_t back;

    if ((b == 0) || (b >= length) || ((((unsigned char)text[b]) & 0xC0u) != 0x80u))
    {
        return true;
    }
    for (back = 1; (back <= 3) && (back <= b); back++)
    {
        start = b - back;
        if ((((unsigned char)text[start]) & 0xC0u) != 0x80u)
        {
            return (start + amark_utf8_decode(text + start, length - start, NULL)) <= b;
        }
    }
    /* Only continuation bytes before b: each is a character of its own. */
    return true;
}

/*
 * Checks that column is within the width of line and is not inside a character
 * other than a tab, and gives the byte of the column (the byte of the tab when
 * the column is inside one) in *byte and whether it is inside a tab in
 * *in_tab. AMARK_ERROR_INVALID_ARGUMENT otherwise; AMARK_ERROR_OUT_OF_MEMORY
 * when the index of the tabs of the line cannot be built.
 */
static amark_error_t check_column(const amark_document_t *document, amark_line_id_t line,
                                  uint32_t column, size_t *byte, bool *in_tab)
{
    uint32_t tab_offset = 0;
    amark_error_t error;

    if (column > document->lines[line].width)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    error = amark_document_column_to_byte(document, line, column, byte, &tab_offset);
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    *in_tab = (tab_offset != 0);
    if (!*in_tab &&
        !is_char_boundary(line_bytes(document, line), document->lines[line].length, *byte))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    return AMARK_ERROR_NONE;
}

/*
 * Checks a record of node on line with the columns [start, end): the line is in
 * the line table and in the line range of node, start <= end <= the width of
 * the line, and both boundaries are boundaries of characters or inside tabs.
 * *start_byte receives the byte of start, and *start_in_tab whether it is
 * inside a tab.
 */
static amark_error_t check_record(const amark_node_t *node, amark_line_id_t line, uint32_t start,
                                  uint32_t end, size_t *start_byte, bool *start_in_tab)
{
    const amark_document_t *document = node->document;
    size_t end_byte = 0;
    bool end_in_tab = false;
    amark_error_t error;

    if ((line >= document->line_count) || !in_range(node, line) || (start > end))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    error = check_column(document, line, start, start_byte, start_in_tab);
    if (error == AMARK_ERROR_NONE)
    {
        error = check_column(document, line, end, &end_byte, &end_in_tab);
    }
    return error;
}

amark_error_t amark_tree_check_record_columns(const amark_node_t *node, amark_line_id_t line,
                                              uint32_t start, uint32_t end)
{
    size_t start_byte = 0;
    bool start_in_tab = false;

    return check_record(node, line, start, end, &start_byte, &start_in_tab);
}

/* Whether the bytes of line from byte (or from inside the tab at byte) to its
 * end are spaces and tabs only. */
static bool blank_from(const amark_document_t *document, amark_line_id_t line, size_t byte)
{
    const char *text = line_bytes(document, line);
    size_t length = document->lines[line].length;
    size_t i;

    for (i = byte; i < length; i++)
    {
        if ((text[i] != ' ') && (text[i] != '\t'))
        {
            return false;
        }
    }
    return true;
}

/* ---- Line ranges -------------------------------------------------------------------- */

amark_error_t amark_block_set_lines(amark_node_t *block, amark_line_id_t first, uint32_t count)
{
    const amark_tree_records_t *records;

    if ((block == NULL) || (block->desc->level != AMARK_NODE_LEVEL_BLOCK))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    if (count > 0)
    {
        if ((first >= block->document->line_count) ||
            (count > (block->document->line_count - first)))
        {
            return AMARK_ERROR_INVALID_ARGUMENT;
        }
    }
    else
    {
        first = 0;
    }
    /* The records are in the order of the lines: the first and the last of
     * each array bound them all. */
    records = block->records;
    if (records != NULL)
    {
        amark_line_id_t low = AMARK_LINE_NONE;
        amark_line_id_t high = 0;
        bool any = false;

        if (records->margin_count > 0)
        {
            any = true;
            low = (records->margins[0].line < low) ? records->margins[0].line : low;
            high = (records->margins[records->margin_count - 1].line > high)
                       ? records->margins[records->margin_count - 1].line
                       : high;
        }
        if (records->blank_line_count > 0)
        {
            any = true;
            low = (records->blank_lines[0].line < low) ? records->blank_lines[0].line : low;
            high = (records->blank_lines[records->blank_line_count - 1].line > high)
                       ? records->blank_lines[records->blank_line_count - 1].line
                       : high;
        }
        if (records->token_count > 0)
        {
            any = true;
            low = (records->tokens[0].line < low) ? records->tokens[0].line : low;
            high = (records->tokens[records->token_count - 1].line > high)
                       ? records->tokens[records->token_count - 1].line
                       : high;
        }
        if (any && ((count == 0) || (low < first) || ((high - first) >= count)))
        {
            return AMARK_ERROR_INVALID_ARGUMENT;
        }
    }
    /* The pieces of the content are in the order of the lines too. */
    if ((block->content != NULL) && (block->content->piece_count > 0))
    {
        const amark_tree_content_t *content = block->content;
        amark_line_id_t low = content->pieces[0].line;
        amark_line_id_t high = content->pieces[content->piece_count - 1].line;

        if ((count == 0) || (low < first) || ((high - first) >= count))
        {
            return AMARK_ERROR_INVALID_ARGUMENT;
        }
    }
    if ((block->line_count == count) && ((count == 0) || (block->line_first == first)))
    {
        return AMARK_ERROR_NONE;
    }

    block->line_first = first;
    block->line_count = count;
    amark_tree_changed(block, AMARK_TREE_CHANGE_RECORD);
    return AMARK_ERROR_NONE;
}

amark_error_t amark_block_lines(const amark_node_t *block, amark_line_id_t *first,
                                uint32_t *count)
{
    if ((block == NULL) || (block->desc->level != AMARK_NODE_LEVEL_BLOCK))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    if (first != NULL)
    {
        *first = (block->line_count > 0) ? block->line_first : AMARK_LINE_NONE;
    }
    if (count != NULL)
    {
        *count = block->line_count;
    }
    return AMARK_ERROR_NONE;
}

/* ---- Margins ------------------------------------------------------------------------ */

amark_error_t amark_block_add_margin(amark_node_t *container, const amark_margin_t *margin)
{
    const amark_tree_records_t *records;
    amark_tree_margin_t *slot;
    amark_tree_records_t *target = NULL;
    bool created = false;
    void *grown = NULL;
    size_t start_byte = 0;
    bool start_in_tab = false;
    amark_error_t error;

    if ((container == NULL) || (margin == NULL) || (margin->struct_size < sizeof(*margin)) ||
        (container->desc->level != AMARK_NODE_LEVEL_BLOCK) ||
        ((container->desc->flags & AMARK_NODE_TYPE_MARGINS) == 0) ||
        ((margin->state != AMARK_MARGIN_PRESENT) && (margin->state != AMARK_MARGIN_LAZY)) ||
        ((margin->flags & ~(uint32_t)AMARK_MARGIN_FIRST) != 0))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    records = container->records;
    if ((records != NULL) && (records->margin_count > 0) &&
        (margin->line <= records->margins[records->margin_count - 1].line))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    if ((margin->state == AMARK_MARGIN_LAZY) && (margin->column_start != margin->column_end))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    error = check_record(container, margin->line, margin->column_start, margin->column_end,
                         &start_byte, &start_in_tab);
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    /* A present margin without columns is the margin of a blank line. */
    if ((margin->state == AMARK_MARGIN_PRESENT) && (margin->column_start == margin->column_end) &&
        !blank_from(container->document, margin->line, start_byte))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    error = open_records(container, &target, &created);
    if (error == AMARK_ERROR_NONE)
    {
        error = grow(container->document, target->margins, target->margin_count,
                     &target->margin_capacity, sizeof(target->margins[0]), &grown);
        target->margins = (amark_tree_margin_t *)grown;
    }
    if (error != AMARK_ERROR_NONE)
    {
        forget_records(target, created);
        return error;
    }
    container->records = target;
    slot = &target->margins[target->margin_count++];
    slot->line = margin->line;
    slot->start = margin->column_start;
    slot->end = margin->column_end;
    slot->state = (uint8_t)margin->state;
    slot->flags = (uint8_t)margin->flags;
    slot->role = margin->role;
    container->document->stats.record_steps++;
    amark_tree_changed(container, AMARK_TREE_CHANGE_RECORD);
    return AMARK_ERROR_NONE;
}

/* The index of the margin of container on line in *index, or false. */
static bool search_margin(const amark_node_t *container, amark_line_id_t line, size_t *index)
{
    const amark_tree_records_t *records = container->records;
    size_t low = 0;
    size_t high;

    if ((records == NULL) || ((container->desc->flags & AMARK_NODE_TYPE_MARGINS) == 0))
    {
        return false;
    }
    high = records->margin_count;
    while (low < high)
    {
        size_t middle = low + (high - low) / 2;

        if (records->margins[middle].line < line)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    if ((low < records->margin_count) && (records->margins[low].line == line))
    {
        *index = low;
        return true;
    }
    return false;
}

static void read_margin(const amark_tree_margin_t *slot, amark_margin_t *out)
{
    out->line = slot->line;
    out->state = (amark_margin_state_t)slot->state;
    out->flags = slot->flags;
    out->column_start = slot->start;
    out->column_end = slot->end;
    out->role = slot->role;
}

amark_error_t amark_block_find_margin(const amark_node_t *container, amark_line_id_t line,
                                      amark_margin_t *out, bool *found)
{
    size_t index = 0;

    if ((container == NULL) || (found == NULL) ||
        ((out != NULL) && (out->struct_size < sizeof(*out))))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    *found = search_margin(container, line, &index);
    if (*found && (out != NULL))
    {
        read_margin(&container->records->margins[index], out);
    }
    return AMARK_ERROR_NONE;
}

amark_error_t amark_block_discard_margin(amark_node_t *container, amark_line_id_t line)
{
    amark_tree_records_t *records;
    size_t index = 0;

    if ((container == NULL) || (container->desc->level != AMARK_NODE_LEVEL_BLOCK) ||
        !search_margin(container, line, &index))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    records = container->records;
    memmove(&records->margins[index], &records->margins[index + 1],
            (records->margin_count - index - 1) * sizeof(records->margins[0]));
    records->margin_count--;
    container->dropped |= AMARK_TREE_DROPPED_STRUCTURE;
    amark_tree_changed(container, AMARK_TREE_CHANGE_RECORD);
    return AMARK_ERROR_NONE;
}

size_t amark_block_margin_count(const amark_node_t *container)
{
    return ((container != NULL) && (container->records != NULL))
               ? container->records->margin_count
               : 0;
}

amark_error_t amark_block_margin_at(const amark_node_t *container, size_t index,
                                    amark_margin_t *out)
{
    if ((out == NULL) || (out->struct_size < sizeof(*out)) ||
        (index >= amark_block_margin_count(container)))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    read_margin(&container->records->margins[index], out);
    return AMARK_ERROR_NONE;
}

/* ---- Blank lines -------------------------------------------------------------------- */

amark_error_t amark_block_add_blank_line(amark_node_t *block, const amark_blank_line_t *blank)
{
    const amark_tree_records_t *records;
    amark_tree_blank_line_t *slot;
    amark_tree_records_t *target = NULL;
    bool created = false;
    void *grown = NULL;
    size_t start_byte = 0;
    bool start_in_tab = false;
    amark_error_t error;

    if ((block == NULL) || (blank == NULL) || (blank->struct_size < sizeof(*blank)) ||
        !has_block_children(block))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    records = block->records;
    if ((records != NULL) && (records->blank_line_count > 0) &&
        (blank->line <= records->blank_lines[records->blank_line_count - 1].line))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    error = check_record(block, blank->line, blank->column_start, blank->column_end,
                         &start_byte, &start_in_tab);
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    if ((blank->column_end != block->document->lines[blank->line].width) ||
        !blank_from(block->document, blank->line, start_byte))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    error = open_records(block, &target, &created);
    if (error == AMARK_ERROR_NONE)
    {
        error = grow(block->document, target->blank_lines, target->blank_line_count,
                     &target->blank_line_capacity, sizeof(target->blank_lines[0]), &grown);
        target->blank_lines = (amark_tree_blank_line_t *)grown;
    }
    if (error != AMARK_ERROR_NONE)
    {
        forget_records(target, created);
        return error;
    }
    block->records = target;
    slot = &target->blank_lines[target->blank_line_count++];
    slot->line = blank->line;
    slot->start = blank->column_start;
    slot->end = blank->column_end;
    block->document->stats.record_steps++;
    amark_tree_changed(block, AMARK_TREE_CHANGE_RECORD);
    return AMARK_ERROR_NONE;
}

size_t amark_block_blank_line_count(const amark_node_t *block)
{
    return ((block != NULL) && (block->records != NULL)) ? block->records->blank_line_count : 0;
}

amark_error_t amark_block_blank_line_at(const amark_node_t *block, size_t index,
                                        amark_blank_line_t *out)
{
    const amark_tree_blank_line_t *slot;

    if ((out == NULL) || (out->struct_size < sizeof(*out)) ||
        (index >= amark_block_blank_line_count(block)))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    slot = &block->records->blank_lines[index];
    out->line = slot->line;
    out->column_start = slot->start;
    out->column_end = slot->end;
    return AMARK_ERROR_NONE;
}

/* ---- Layout tokens ------------------------------------------------------------------ */

amark_error_t amark_block_add_token(amark_node_t *block, const amark_layout_token_t *token)
{
    const amark_tree_records_t *records;
    amark_tree_layout_token_t *slot;
    amark_tree_records_t *target = NULL;
    bool created = false;
    void *grown = NULL;
    size_t start_byte = 0;
    bool start_in_tab = false;
    amark_error_t error;

    if ((block == NULL) || (token == NULL) || (token->struct_size < sizeof(*token)) ||
        (block->desc->level != AMARK_NODE_LEVEL_BLOCK) || (token->kind < AMARK_SPELL_TEXT) ||
        (token->kind > AMARK_SPELL_REPLACED) || (token->facet > block->desc->facet_count) ||
        (token->column_start >= token->column_end) ||
        amark_tree_mixes_spelling(block, token->facet, true) ||
        amark_tree_facet_dropped(block, token->facet))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    records = block->records;
    if ((records != NULL) && (records->token_count > 0))
    {
        const amark_tree_layout_token_t *last = &records->tokens[records->token_count - 1];

        if ((token->line < last->line) ||
            ((token->line == last->line) && (token->column_start < last->end)))
        {
            return AMARK_ERROR_INVALID_ARGUMENT;
        }
    }
    error = check_record(block, token->line, token->column_start, token->column_end,
                         &start_byte, &start_in_tab);
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    if ((token->facet != AMARK_FACET_NONE) &&
        !amark_tree_layout_bytes_ok(block->document, token->line, token->column_start,
                                    token->column_end, token->kind))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    error = open_records(block, &target, &created);
    if (error == AMARK_ERROR_NONE)
    {
        error = grow(block->document, target->tokens, target->token_count,
                     &target->token_capacity, sizeof(target->tokens[0]), &grown);
        target->tokens = (amark_tree_layout_token_t *)grown;
    }
    if (error != AMARK_ERROR_NONE)
    {
        forget_records(target, created);
        return error;
    }
    block->records = target;
    slot = &target->tokens[target->token_count++];
    slot->line = token->line;
    slot->start = token->column_start;
    slot->end = token->column_end;
    slot->kind = (uint8_t)token->kind;
    slot->facet = (uint8_t)token->facet;
    slot->role = token->role;
    block->document->stats.record_steps++;
    if (token->facet != AMARK_FACET_NONE)
    {
        amark_tree_drop_derived(block, token->facet);
    }
    amark_tree_changed(block, AMARK_TREE_CHANGE_RECORD);
    return AMARK_ERROR_NONE;
}

size_t amark_block_token_count(const amark_node_t *block)
{
    return ((block != NULL) && (block->records != NULL)) ? block->records->token_count : 0;
}

amark_error_t amark_block_token_at(const amark_node_t *block, size_t index,
                                   amark_layout_token_t *out)
{
    const amark_tree_layout_token_t *slot;

    if ((out == NULL) || (out->struct_size < sizeof(*out)) ||
        (index >= amark_block_token_count(block)))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    slot = &block->records->tokens[index];
    out->line = slot->line;
    out->column_start = slot->start;
    out->column_end = slot->end;
    out->kind = (amark_spell_kind_t)slot->kind;
    out->facet = slot->facet;
    out->role = slot->role;
    return AMARK_ERROR_NONE;
}
