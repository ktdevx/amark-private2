/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * CopyImport: the copy within the same document, the import from another
 * document and the import of the children of a node.
 *
 * Both make a new node for each node of the subtree, with the type, the
 * facets, the line range, the layout records, the content tokens, the dropped
 * marks and the extension data of its original. The caches of the verbatim
 * judgment are not copied (a new node starts without them), nor the user
 * pointer; a copied link_definition is put on the dirty list of the
 * definition index.
 *
 *   - The copy (5.6) shares the content objects: a copied leaf is one more
 *     owner of the content of its original, and a copied node with content
 *     tokens one more base of their base, so that the copy on write of
 *     Content (6.3) keeps the two apart. The lines are those of the document.
 *   - The import (5.7–5.9) looks the types up by name in the syntax set of the
 *     target, gathers the lines the subtrees refer to (line ranges, records,
 *     pieces of the content objects), adds them to the target as one new
 *     source with their ranges and line endings given
 *     (amark_line_table_append_lines), maps the line identifiers to the new
 *     ones and copies each content object once, so that two nodes sharing one
 *     in the source share its copy in the target.
 *
 * All or nothing: every check and every allocation is done on nodes that are
 * built but not committed (not in the ring, no reference counted), the lines
 * are added last (they cannot be taken back), and then the nodes are
 * committed and inserted, which allocates nothing. An import into the
 * document of the node is a copy.
 */
#include "tree/tree.h"

#include <amark/allocator.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ---- The state of one copy or import -------------------------------------------- */

/* Lines [first, end) of the source document; after the merge, the spans are
 * sorted and disjoint, and target_first is the identifier of line first in the
 * target. */
typedef struct line_span
{
    uint64_t first;
    uint64_t end;
    amark_line_id_t target_first;
} line_span_t;

/* A content object of the source document and its copy in the target. */
typedef struct content_pair
{
    amark_tree_content_t *from;
    amark_tree_content_t *to;
} content_pair_t;

typedef struct import
{
    amark_document_t *target;
    const amark_document_t *source;
    /* A copy within the document: the lines and the content objects are
     * shared, the types are those of the source. */
    bool same;
    /* The lines referred to (import only). */
    line_span_t *spans;
    size_t span_count;
    size_t span_capacity;
    /* The number of lines to add. */
    size_t line_count;
    /* The content objects referred to, sorted by address once gathered
     * (import only). */
    content_pair_t *contents;
    size_t content_count;
    size_t content_capacity;
    /* A link among the copied nodes. */
    bool has_link;
    /* The built copies of the top nodes, linked by next and prev. */
    amark_node_t *first_top;
    amark_node_t *last_top;
} import_t;

/* Makes room for needed elements of size bytes in *array of *capacity, by
 * doubling. On failure *array is unchanged. */
static amark_error_t grow(void **array, size_t *capacity, size_t needed, size_t size)
{
    size_t new_capacity;
    void *block;

    if (needed <= *capacity)
    {
        return AMARK_ERROR_NONE;
    }
    new_capacity = (*capacity == 0) ? 16 : *capacity;
    while (new_capacity < needed)
    {
        if (new_capacity > SIZE_MAX / 2 / size)
        {
            return AMARK_ERROR_OUT_OF_MEMORY;
        }
        new_capacity *= 2;
    }
    block = amark_realloc(*array, new_capacity * size);
    if (block == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    *array = block;
    *capacity = new_capacity;
    return AMARK_ERROR_NONE;
}

/* ---- Gathering the lines and the content objects -------------------------------- */

static amark_error_t push_span(import_t *import, uint64_t first, uint64_t count)
{
    void *array = import->spans;
    amark_error_t error;

    if (count == 0)
    {
        return AMARK_ERROR_NONE;
    }
    error = grow(&array, &import->span_capacity, import->span_count + 1, sizeof(line_span_t));
    import->spans = (line_span_t *)array;
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    import->spans[import->span_count].first = first;
    import->spans[import->span_count].end = first + count;
    import->spans[import->span_count].target_first = 0;
    import->span_count++;
    return AMARK_ERROR_NONE;
}

/* A line of a record of node: a span unless it lies in the line range of node,
 * where the records are (Layout), already gathered. */
static amark_error_t push_record_line(import_t *import, const amark_node_t *node,
                                      amark_line_id_t line)
{
    if ((node->line_count > 0) && (line >= node->line_first) &&
        ((uint64_t)line < (uint64_t)node->line_first + node->line_count))
    {
        return AMARK_ERROR_NONE;
    }
    return push_span(import, line, 1);
}

static amark_error_t push_content(import_t *import, amark_tree_content_t *content)
{
    void *array = import->contents;
    amark_error_t error;

    if (content == NULL)
    {
        return AMARK_ERROR_NONE;
    }
    error = grow(&array, &import->content_capacity, import->content_count + 1,
                 sizeof(content_pair_t));
    import->contents = (content_pair_t *)array;
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    import->contents[import->content_count].from = content;
    import->contents[import->content_count].to = NULL;
    import->content_count++;
    return AMARK_ERROR_NONE;
}

/* Gathers the lines of node (its line range and its records) and the content
 * objects it refers to (its content and the base of its content tokens). */
static amark_error_t gather_node(import_t *import, const amark_node_t *node)
{
    const amark_tree_records_t *records = node->records;
    amark_error_t error = push_span(import, node->line_first, node->line_count);
    size_t i;

    if (records != NULL)
    {
        for (i = 0; (error == AMARK_ERROR_NONE) && (i < records->margin_count); i++)
        {
            error = push_record_line(import, node, records->margins[i].line);
        }
        for (i = 0; (error == AMARK_ERROR_NONE) && (i < records->blank_line_count); i++)
        {
            error = push_record_line(import, node, records->blank_lines[i].line);
        }
        for (i = 0; (error == AMARK_ERROR_NONE) && (i < records->token_count); i++)
        {
            error = push_record_line(import, node, records->tokens[i].line);
        }
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = push_content(import, node->content);
    }
    if ((error == AMARK_ERROR_NONE) && (node->content_tokens != NULL))
    {
        error = push_content(import, node->content_tokens->base);
    }
    return error;
}

static int compare_spans(const void *a, const void *b)
{
    uint64_t x = ((const line_span_t *)a)->first;
    uint64_t y = ((const line_span_t *)b)->first;

    return (x > y) - (x < y);
}

static int compare_contents(const void *a, const void *b)
{
    uintptr_t x = (uintptr_t)((const content_pair_t *)a)->from;
    uintptr_t y = (uintptr_t)((const content_pair_t *)b)->from;

    return (x > y) - (x < y);
}

/*
 * Once every node is gathered: keeps each content object once, gathers the
 * lines of their pieces, merges the spans and gives them their identifiers in
 * the target, after its last line. AMARK_ERROR_LIMIT_EXCEEDED when the target
 * cannot hold that many more lines.
 */
static amark_error_t plan_lines(import_t *import)
{
    size_t count = 0;
    size_t i;
    uint64_t total = 0;
    amark_error_t error = AMARK_ERROR_NONE;

    if (import->content_count > 0)
    {
        qsort(import->contents, import->content_count, sizeof(content_pair_t), compare_contents);
        for (i = 0; i < import->content_count; i++)
        {
            if ((count == 0) || (import->contents[count - 1].from != import->contents[i].from))
            {
                import->contents[count++] = import->contents[i];
            }
        }
        import->content_count = count;
    }
    for (i = 0; (error == AMARK_ERROR_NONE) && (i < import->content_count); i++)
    {
        const amark_tree_content_t *content = import->contents[i].from;
        size_t k;

        for (k = 0; (error == AMARK_ERROR_NONE) && (k < content->piece_count); k++)
        {
            error = push_span(import, content->pieces[k].line, 1);
        }
    }
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    count = 0;
    if (import->span_count > 0)
    {
        qsort(import->spans, import->span_count, sizeof(line_span_t), compare_spans);
        for (i = 0; i < import->span_count; i++)
        {
            line_span_t *last = (count > 0) ? &import->spans[count - 1] : NULL;

            if ((last != NULL) && (import->spans[i].first <= last->end))
            {
                if (import->spans[i].end > last->end)
                {
                    last->end = import->spans[i].end;
                }
            }
            else
            {
                import->spans[count++] = import->spans[i];
            }
        }
        import->span_count = count;
    }
    for (i = 0; i < import->span_count; i++)
    {
        import->spans[i].target_first = (amark_line_id_t)(import->target->line_count + total);
        total += import->spans[i].end - import->spans[i].first;
        if (total > AMARK_TREE_MAX_LINES - import->target->line_count)
        {
            return AMARK_ERROR_LIMIT_EXCEEDED;
        }
    }
    import->line_count = (size_t)total;
    return AMARK_ERROR_NONE;
}

/* The identifier in the target of line of the source document. */
static amark_line_id_t map_line(const import_t *import, amark_line_id_t line)
{
    size_t low = 0;
    size_t high = import->span_count;

    if (import->same)
    {
        return line;
    }
    while (low < high)
    {
        size_t middle = low + (high - low) / 2;
        const line_span_t *span = &import->spans[middle];

        if (line < span->first)
        {
            high = middle;
        }
        else if (line >= span->end)
        {
            low = middle + 1;
        }
        else
        {
            return (amark_line_id_t)(span->target_first + (line - span->first));
        }
    }
    /* Not reached: every line referred to is in a span. */
    return line;
}

/* The content object of the target for content of the source document (the
 * same one for a copy; NULL for NULL). */
static amark_tree_content_t *map_content(const import_t *import, amark_tree_content_t *content)
{
    size_t low = 0;
    size_t high = import->content_count;

    if (import->same || (content == NULL))
    {
        return content;
    }
    while (low < high)
    {
        size_t middle = low + (high - low) / 2;
        uintptr_t key = (uintptr_t)import->contents[middle].from;

        if ((uintptr_t)content < key)
        {
            high = middle;
        }
        else if ((uintptr_t)content > key)
        {
            low = middle + 1;
        }
        else
        {
            return import->contents[middle].to;
        }
    }
    /* Not reached: every content object referred to is gathered. */
    return NULL;
}

/* ---- The copies of the lines and the content objects ----------------------------- */

/* The bytes of the gathered lines, joined without their line endings, in
 * *bytes (NULL when empty) of *length bytes, and their ranges in *ranges. */
static amark_error_t copy_lines(const import_t *import, char **bytes, size_t *length,
                                amark_tree_line_range_t **ranges)
{
    const amark_document_t *source = import->source;
    size_t total = 0;
    size_t offset = 0;
    size_t k = 0;
    size_t i;

    *bytes = NULL;
    *length = 0;
    *ranges = NULL;
    if (import->line_count > SIZE_MAX / sizeof(amark_tree_line_range_t))
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    *ranges = (amark_tree_line_range_t *)amark_malloc(import->line_count *
                                                      sizeof(amark_tree_line_range_t));
    if (*ranges == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    for (i = 0; i < import->span_count; i++)
    {
        uint64_t id;

        for (id = import->spans[i].first; id < import->spans[i].end; id++)
        {
            /* Each line is in the memory of its source, so the sum fits. */
            total += source->lines[id].length;
        }
    }
    if (total > 0)
    {
        *bytes = (char *)amark_malloc(total);
        if (*bytes == NULL)
        {
            amark_free(*ranges);
            *ranges = NULL;
            return AMARK_ERROR_OUT_OF_MEMORY;
        }
    }
    for (i = 0; i < import->span_count; i++)
    {
        uint64_t id;

        for (id = import->spans[i].first; id < import->spans[i].end; id++)
        {
            const amark_tree_line_t *line = &source->lines[id];

            if (line->length > 0)
            {
                memcpy(*bytes + offset, source->sources[line->source].data + line->offset,
                       line->length);
            }
            (*ranges)[k].offset = offset;
            (*ranges)[k].length = line->length;
            (*ranges)[k].eol = line->eol;
            offset += line->length;
            k++;
        }
    }
    *length = total;
    return AMARK_ERROR_NONE;
}

/* Frees a content object copied by copy_content, which no node counts. */
static void free_content(amark_tree_content_t *content)
{
    if (content != NULL)
    {
        amark_free(content->bytes);
        amark_free(content->pieces);
        amark_free(content);
    }
}

/* A copy of from for the target, its pieces on the lines of the target and no
 * owner nor base yet (the commit counts them). */
static amark_error_t copy_content(const import_t *import, const amark_tree_content_t *from,
                                  amark_tree_content_t **out)
{
    amark_tree_content_t *content;
    size_t i;

    *out = NULL;
    content = (amark_tree_content_t *)amark_calloc(1, sizeof(*content));
    if (content == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    content->state = from->state;
    content->length = from->length;
    content->line_count = from->line_count;
    if (from->capacity > 0)
    {
        content->bytes = (char *)amark_malloc(from->length + 1);
        if (content->bytes == NULL)
        {
            free_content(content);
            return AMARK_ERROR_OUT_OF_MEMORY;
        }
        memcpy(content->bytes, from->bytes, from->length);
        content->bytes[from->length] = '\0';
        content->capacity = from->length + 1;
    }
    if (from->piece_count > 0)
    {
        content->pieces =
            (amark_tree_piece_t *)amark_malloc(from->piece_count * sizeof(amark_tree_piece_t));
        if (content->pieces == NULL)
        {
            free_content(content);
            return AMARK_ERROR_OUT_OF_MEMORY;
        }
        for (i = 0; i < from->piece_count; i++)
        {
            content->pieces[i] = from->pieces[i];
            content->pieces[i].line = map_line(import, from->pieces[i].line);
        }
        content->piece_count = from->piece_count;
        content->piece_capacity = from->piece_count;
    }
    *out = content;
    return AMARK_ERROR_NONE;
}

/* ---- One node ---------------------------------------------------------------- */

/* Frees a node built by build_node that is not committed: it is not in the
 * ring and counts no reference to a content object, so only its memory goes
 * (deinit is up to the caller). */
static void free_uncommitted(amark_node_t *node)
{
    if (node->facets != NULL)
    {
        size_t i;

        for (i = 0; i < node->desc->facet_count; i++)
        {
            amark_free(node->facets[i].text);
        }
        amark_free(node->facets);
    }
    amark_tree_records_free(node->records);
    if (node->content_tokens != NULL)
    {
        amark_free(node->content_tokens->items);
        amark_free(node->content_tokens);
    }
    amark_free(node->data);
    amark_free(node);
}

/* An owned copy of count elements of size bytes at source in *out (NULL for
 * none). */
static amark_error_t copy_array(const void *source, size_t count, size_t size, void **out)
{
    *out = NULL;
    if (count == 0)
    {
        return AMARK_ERROR_NONE;
    }
    *out = amark_malloc(count * size);
    if (*out == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    memcpy(*out, source, count * size);
    return AMARK_ERROR_NONE;
}

/* Copies the stored values of the facets of source into node (not the derived
 * caches, which the getters derive again). */
static amark_error_t copy_facets(amark_node_t *node, const amark_node_t *source)
{
    size_t count = source->desc->facet_count;
    size_t i;

    if (count == 0)
    {
        return AMARK_ERROR_NONE;
    }
    node->facets = (amark_tree_facet_t *)amark_calloc(count, sizeof(amark_tree_facet_t));
    if (node->facets == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    for (i = 0; i < count; i++)
    {
        const amark_tree_facet_t *from = &source->facets[i];
        amark_tree_facet_t *to = &node->facets[i];

        to->state = from->state;
        to->number = from->number;
        if (from->text != NULL)
        {
            void *text = NULL;

            if (copy_array(from->text, from->length + 1, 1, &text) != AMARK_ERROR_NONE)
            {
                return AMARK_ERROR_OUT_OF_MEMORY;
            }
            to->text = (char *)text;
            to->length = from->length;
        }
    }
    return AMARK_ERROR_NONE;
}

/* Copies the layout records of source into node, each array with the
 * capacity of its count and its lines mapped to the target. */
static amark_error_t copy_records(const import_t *import, amark_node_t *node,
                                  const amark_node_t *source)
{
    const amark_tree_records_t *from = source->records;
    amark_tree_records_t *to;
    void *array = NULL;
    size_t i;

    if (from == NULL)
    {
        return AMARK_ERROR_NONE;
    }
    to = (amark_tree_records_t *)amark_calloc(1, sizeof(*to));
    if (to == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    node->records = to;
    if (copy_array(from->margins, from->margin_count, sizeof(amark_tree_margin_t), &array) !=
        AMARK_ERROR_NONE)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    to->margins = (amark_tree_margin_t *)array;
    to->margin_count = from->margin_count;
    to->margin_capacity = from->margin_count;
    if (copy_array(from->blank_lines, from->blank_line_count, sizeof(amark_tree_blank_line_t),
                   &array) != AMARK_ERROR_NONE)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    to->blank_lines = (amark_tree_blank_line_t *)array;
    to->blank_line_count = from->blank_line_count;
    to->blank_line_capacity = from->blank_line_count;
    if (copy_array(from->tokens, from->token_count, sizeof(amark_tree_layout_token_t), &array) !=
        AMARK_ERROR_NONE)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    to->tokens = (amark_tree_layout_token_t *)array;
    to->token_count = from->token_count;
    to->token_capacity = from->token_count;
    if (!import->same)
    {
        for (i = 0; i < to->margin_count; i++)
        {
            to->margins[i].line = map_line(import, to->margins[i].line);
        }
        for (i = 0; i < to->blank_line_count; i++)
        {
            to->blank_lines[i].line = map_line(import, to->blank_lines[i].line);
        }
        for (i = 0; i < to->token_count; i++)
        {
            to->tokens[i].line = map_line(import, to->tokens[i].line);
        }
    }
    return AMARK_ERROR_NONE;
}

/* Copies the content tokens of source into node, on the content object of
 * the target for their base (not counted: the commit counts it). */
static amark_error_t copy_content_tokens(const import_t *import, amark_node_t *node,
                                         const amark_node_t *source)
{
    const amark_tree_content_tokens_t *from = source->content_tokens;
    amark_tree_content_tokens_t *to;
    void *array = NULL;

    if ((from == NULL) || (from->count == 0))
    {
        return AMARK_ERROR_NONE;
    }
    to = (amark_tree_content_tokens_t *)amark_calloc(1, sizeof(*to));
    if (to == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    node->content_tokens = to;
    if (copy_array(from->items, from->count, sizeof(amark_tree_content_token_t), &array) !=
        AMARK_ERROR_NONE)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    to->items = (amark_tree_content_token_t *)array;
    to->count = from->count;
    to->capacity = from->count;
    to->base = map_content(import, from->base);
    return AMARK_ERROR_NONE;
}

/*
 * Builds in *out a copy of source of type (desc) for the target, not
 * committed: everything is allocated and the extension data copied, the
 * content and the base of the content tokens point to the content objects of
 * the target without counting them, and it is not in the ring. The caches
 * start empty (cache_flags 0, no remembered columns, no remembered leaf), the
 * relations, the user pointer and the definition index links are NULL. On
 * failure nothing is left allocated.
 */
static amark_error_t build_node(const import_t *import, const amark_node_t *source,
                                amark_node_type_t type, const amark_node_type_desc_t *desc,
                                amark_node_t **out)
{
    amark_node_t *node;
    amark_error_t error;

    *out = NULL;
    node = (amark_node_t *)amark_calloc(1, sizeof(*node));
    if (node == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    node->document = import->target;
    node->desc = desc;
    node->type = type;
    node->line_first = source->line_first;
    node->line_count = source->line_count;
    if (node->line_count > 0)
    {
        node->line_first = map_line(import, source->line_first);
    }
    node->dropped = source->dropped;
    node->content = map_content(import, source->content);
    error = copy_facets(node, source);
    if (error == AMARK_ERROR_NONE)
    {
        error = copy_records(import, node, source);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = copy_content_tokens(import, node, source);
    }
    if ((error == AMARK_ERROR_NONE) && (desc->data_size > 0))
    {
        node->data = amark_calloc(1, desc->data_size);
        if (node->data == NULL)
        {
            error = AMARK_ERROR_OUT_OF_MEMORY;
        }
        else if (desc->copy != NULL)
        {
            /* Last, so that nothing can fail once the data owns something. */
            error = desc->copy(node, node->data, source, source->data);
        }
        else
        {
            memcpy(node->data, source->data, desc->data_size);
        }
    }
    if (error != AMARK_ERROR_NONE)
    {
        free_uncommitted(node);
        return error;
    }
    *out = node;
    return AMARK_ERROR_NONE;
}

/* Frees the built, uncommitted subtree of top (the copy function ran on each
 * node, so deinit runs too), without recursion. */
static void free_built(amark_node_t *top)
{
    amark_node_t *current = top;

    for (;;)
    {
        amark_node_t *next;
        amark_node_t *parent;

        while (current->first_child != NULL)
        {
            current = current->first_child;
        }
        next = current->next;
        parent = current->parent;
        if (current->desc->deinit != NULL)
        {
            current->desc->deinit(current, current->data);
        }
        free_uncommitted(current);
        if (current == top)
        {
            return;
        }
        if (next != NULL)
        {
            current = next;
        }
        else
        {
            parent->first_child = NULL;
            current = parent;
        }
    }
}

/* Commits the built subtree of top: each node counts its references to the
 * content objects, joins the ring of the target, and a link_definition goes on
 * the dirty list of the definition index. Allocates nothing. */
static void commit_built(amark_node_t *top)
{
    amark_node_t *node = top;

    while (node != NULL)
    {
        if (node->content != NULL)
        {
            node->content->owners++;
        }
        if (node->content_tokens != NULL)
        {
            node->content_tokens->base->bases++;
        }
        amark_tree_node_link(node);
        amark_tree_def_index_mark(node);
        if (node->first_child != NULL)
        {
            node = node->first_child;
            continue;
        }
        while ((node != top) && (node->next == NULL))
        {
            node = node->parent;
        }
        node = (node == top) ? NULL : node->next;
    }
}

/* Appends child as the last child of parent (both built, not committed). */
static void append_built_child(amark_node_t *parent, amark_node_t *child)
{
    child->parent = parent;
    child->prev = parent->last_child;
    if (parent->last_child != NULL)
    {
        parent->last_child->next = child;
    }
    else
    {
        parent->first_child = child;
    }
    parent->last_child = child;
}

/* ---- The walk ---------------------------------------------------------------------- */

/* The next node after node in the preorder of the subtree of top, or NULL. */
static const amark_node_t *next_in_subtree(const amark_node_t *top, const amark_node_t *node)
{
    if (node->first_child != NULL)
    {
        return node->first_child;
    }
    while ((node != top) && (node->next == NULL))
    {
        node = node->parent;
    }
    return (node == top) ? NULL : node->next;
}

/* Whether two descriptors of the same name describe the same node shape: the
 * level, the flags, the size of the data and the kinds of the facets, so that
 * the stored values and the records of one fit the other. */
static bool same_shape(const amark_node_type_desc_t *a, const amark_node_type_desc_t *b)
{
    size_t i;

    if ((a->level != b->level) || (a->flags != b->flags) || (a->facet_count != b->facet_count) ||
        (a->data_size != b->data_size))
    {
        return false;
    }
    for (i = 0; i < a->facet_count; i++)
    {
        if ((a->facets[i].kind != b->facets[i].kind) || (a->facets[i].flags != b->facets[i].flags))
        {
            return false;
        }
    }
    return true;
}

/* The type of node in the target (5.9): its own type for a copy, else the
 * type of the same name in the syntax set of the target, of the same shape.
 * AMARK_ERROR_INVALID_ARGUMENT when there is none. */
static amark_error_t resolve_type(const import_t *import, const amark_node_t *node,
                                  amark_node_type_t *type, const amark_node_type_desc_t **desc)
{
    if (import->same)
    {
        *type = node->type;
        *desc = node->desc;
        return AMARK_ERROR_NONE;
    }
    if (!amark_syntax_set_find_node_type(import->target->set, node->desc->name, type))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    *desc = amark_syntax_set_node_type_desc(import->target->set, *type);
    if ((*desc == NULL) || ((*desc != node->desc) && !same_shape(*desc, node->desc)))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    return AMARK_ERROR_NONE;
}

/*
 * The checks and the gathering, before any node is built: every type is found
 * in the target, each child is accepted by its parent there, each top by
 * parent (the insertion parent, or NULL), and no link would get a link among
 * its descendants under parent. Gathers the lines and the content objects of
 * an import.
 */
static amark_error_t scan(import_t *import, const amark_node_t *first, bool siblings,
                          const amark_node_t *parent)
{
    const amark_node_t *top;
    const amark_node_t *ancestor;
    amark_error_t error = AMARK_ERROR_NONE;

    for (top = first; (error == AMARK_ERROR_NONE) && (top != NULL);
         top = siblings ? top->next : NULL)
    {
        const amark_node_t *node;

        for (node = top; (error == AMARK_ERROR_NONE) && (node != NULL);
             node = next_in_subtree(top, node))
        {
            amark_node_type_t type = AMARK_NODE_NONE;
            const amark_node_type_desc_t *desc = NULL;

            error = resolve_type(import, node, &type, &desc);
            if (error != AMARK_ERROR_NONE)
            {
                break;
            }
            if (type == AMARK_NODE_LINK)
            {
                import->has_link = true;
            }
            if (node == top)
            {
                if ((parent != NULL) &&
                    !amark_registry_can_contain(import->target->set, parent, parent->type, type))
                {
                    error = AMARK_ERROR_INVALID_ARGUMENT;
                }
            }
            else if (!import->same)
            {
                amark_node_type_t parent_type = AMARK_NODE_NONE;
                const amark_node_type_desc_t *parent_desc = NULL;

                /* Within the subtree the source set accepted each child; the
                 * target set must too. */
                error = resolve_type(import, node->parent, &parent_type, &parent_desc);
                if ((error == AMARK_ERROR_NONE) &&
                    !amark_registry_can_contain(import->target->set, NULL, parent_type, type))
                {
                    error = AMARK_ERROR_INVALID_ARGUMENT;
                }
            }
            if ((error == AMARK_ERROR_NONE) && !import->same)
            {
                error = gather_node(import, node);
            }
        }
    }
    if ((error == AMARK_ERROR_NONE) && (parent != NULL) && import->has_link)
    {
        for (ancestor = parent; ancestor != NULL; ancestor = ancestor->parent)
        {
            if (ancestor->type == AMARK_NODE_LINK)
            {
                return AMARK_ERROR_INVALID_ARGUMENT;
            }
        }
    }
    return error;
}

/* Builds the copy of source (its type found by scan) in *out. */
static amark_error_t build_one(const import_t *import, const amark_node_t *source,
                               amark_node_t **out)
{
    amark_node_type_t type = AMARK_NODE_NONE;
    const amark_node_type_desc_t *desc = NULL;
    amark_error_t error = resolve_type(import, source, &type, &desc);

    if (error != AMARK_ERROR_NONE)
    {
        *out = NULL;
        return error;
    }
    return build_node(import, source, type, desc, out);
}

/* Builds the copies of the subtrees, each top linked after the previous one
 * (import->first_top to import->last_top) as soon as it is built, so that a
 * failure leaves every built node under a top for discard. The subtrees are
 * walked in preorder without recursion; copy is the copy of source. */
static amark_error_t build(import_t *import, const amark_node_t *first, bool siblings)
{
    const amark_node_t *top;

    for (top = first; top != NULL; top = siblings ? top->next : NULL)
    {
        const amark_node_t *source = top;
        amark_node_t *current = NULL;
        amark_error_t error = build_one(import, top, &current);

        if (error != AMARK_ERROR_NONE)
        {
            return error;
        }
        current->prev = import->last_top;
        if (import->last_top != NULL)
        {
            import->last_top->next = current;
        }
        else
        {
            import->first_top = current;
        }
        import->last_top = current;
        for (;;)
        {
            const amark_node_t *next;
            amark_node_t *parent;
            amark_node_t *copy = NULL;

            if (source->first_child != NULL)
            {
                next = source->first_child;
                parent = current;
            }
            else
            {
                while ((source != top) && (source->next == NULL))
                {
                    source = source->parent;
                    current = current->parent;
                }
                if (source == top)
                {
                    break;
                }
                next = source->next;
                parent = current->parent;
            }
            error = build_one(import, next, &copy);
            if (error != AMARK_ERROR_NONE)
            {
                return error;
            }
            append_built_child(parent, copy);
            source = next;
            current = copy;
        }
    }
    return AMARK_ERROR_NONE;
}

/* Frees what a failed copy or import built: the copies of the nodes and of
 * the content objects. */
static void discard(import_t *import)
{
    amark_node_t *top = import->first_top;
    size_t i;

    while (top != NULL)
    {
        amark_node_t *next = top->next;

        top->next = NULL;
        free_built(top);
        top = next;
    }
    import->first_top = NULL;
    import->last_top = NULL;
    for (i = 0; i < import->content_count; i++)
    {
        free_content(import->contents[i].to);
        import->contents[i].to = NULL;
    }
}

/* Links the committed tops, in order, under parent before before (NULL: at the
 * end), and notifies one structural change. */
static void insert_tops(import_t *import, amark_node_t *parent, amark_node_t *before)
{
    amark_node_t *prev = (before != NULL) ? before->prev : parent->last_child;
    amark_node_t *top;

    for (top = import->first_top; top != NULL; top = top->next)
    {
        top->parent = parent;
    }
    import->first_top->prev = prev;
    if (prev != NULL)
    {
        prev->next = import->first_top;
    }
    else
    {
        parent->first_child = import->first_top;
    }
    import->last_top->next = before;
    if (before != NULL)
    {
        before->prev = import->last_top;
    }
    else
    {
        parent->last_child = import->last_top;
    }
    amark_tree_changed(parent, AMARK_TREE_CHANGE_STRUCTURE);
}

/*
 * Copies the subtree of first (and, with siblings, of each of its next
 * siblings) into target: a copy when target is the document of first, else an
 * import. With parent, the copies are inserted under parent before before;
 * otherwise the single copy goes to *out, detached.
 */
static amark_error_t copy_subtrees(amark_document_t *target, const amark_node_t *first,
                                   bool siblings, amark_node_t *parent, amark_node_t *before,
                                   amark_node_t **out)
{
    import_t import;
    char *bytes = NULL;
    size_t length = 0;
    amark_tree_line_range_t *ranges = NULL;
    amark_source_id_t id = 0;
    amark_error_t error;
    size_t i;

    memset(&import, 0, sizeof(import));
    import.target = target;
    import.source = first->document;
    import.same = (target == first->document);
    error = scan(&import, first, siblings, parent);
    if ((error == AMARK_ERROR_NONE) && !import.same)
    {
        error = plan_lines(&import);
    }
    for (i = 0; (error == AMARK_ERROR_NONE) && (i < import.content_count); i++)
    {
        error = copy_content(&import, import.contents[i].from, &import.contents[i].to);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = build(&import, first, siblings);
    }
    if ((error == AMARK_ERROR_NONE) && (import.line_count > 0))
    {
        error = copy_lines(&import, &bytes, &length, &ranges);
        if (error == AMARK_ERROR_NONE)
        {
            /* Last: the lines added cannot be taken back. */
            error = amark_line_table_append_lines(target, bytes, length, ranges,
                                                  import.line_count, &id);
        }
        amark_free(bytes);
        amark_free(ranges);
    }
    if (error != AMARK_ERROR_NONE)
    {
        discard(&import);
    }
    else
    {
        amark_node_t *top;

        /* The commit allocates nothing. */
        for (top = import.first_top; top != NULL; top = top->next)
        {
            commit_built(top);
        }
        if (parent != NULL)
        {
            insert_tops(&import, parent, before);
        }
        else
        {
            *out = import.first_top;
        }
    }
    amark_free(import.spans);
    amark_free(import.contents);
    return error;
}

/* ---- The public functions ---------------------------------------------------------- */

amark_error_t amark_node_copy(const amark_node_t *node, amark_node_t **out)
{
    if (out == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    *out = NULL;
    if ((node == NULL) || (node == node->document->root))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    return copy_subtrees(node->document, node, false, NULL, NULL, out);
}

amark_error_t amark_node_import(amark_document_t *target, const amark_node_t *node,
                                amark_node_t **out)
{
    if (out == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    *out = NULL;
    if ((target == NULL) || (node == NULL) || (node == node->document->root))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    return copy_subtrees(target, node, false, NULL, NULL, out);
}

amark_error_t amark_node_import_children(amark_node_t *target_parent, amark_node_t *before,
                                         const amark_node_t *source_parent)
{
    if ((target_parent == NULL) || (source_parent == NULL) ||
        ((before != NULL) && (before->parent != target_parent)))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    if (source_parent->first_child == NULL)
    {
        return AMARK_ERROR_NONE;
    }
    return copy_subtrees(target_parent->document, source_parent->first_child, true,
                         target_parent, before, NULL);
}
