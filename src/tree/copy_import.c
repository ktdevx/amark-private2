/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * CopyImport: the copy, the import from another document, the import of the
 * children and the import of the lines.
 *
 * The copy within the same document (5.6) makes a new node for each node of
 * the subtree, with the type, the facets, the line range, the layout records,
 * the content tokens, the dropped marks and the extension data of its
 * original. The content objects are shared: a copied leaf is one more owner of
 * the content of its original, and a copied node with content tokens one more
 * base of their base, so that the copy on write of Content (6.3) keeps the
 * two apart. The caches of the verbatim judgment are not copied (a new node
 * starts without them) nor the definition index: a copied link_definition is
 * put on the dirty list.
 */
#include "tree/tree.h"

#include <amark/allocator.h>

#include <string.h>

/* ---- One node ---------------------------------------------------------------- */

/* Frees a node built by build_node that is not committed yet: it holds no
 * reference to a content object and is not in the ring, so only its memory
 * goes (the copy function has not run, or failed and owns nothing). */
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
 * capacity of its count. */
static amark_error_t copy_records(amark_node_t *node, const amark_node_t *source)
{
    const amark_tree_records_t *from = source->records;
    amark_tree_records_t *to;
    void *array = NULL;

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
    return AMARK_ERROR_NONE;
}

/* Copies the content tokens of source into node, without their base: the
 * commit sets it, so that an uncommitted node holds no reference. */
static amark_error_t copy_content_tokens(amark_node_t *node, const amark_node_t *source)
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
    return AMARK_ERROR_NONE;
}

/*
 * Builds in *out a copy of source, not committed: everything is allocated and
 * the extension data copied, but it refers to no content object and is not in
 * the ring. The caches start empty (cache_flags 0, no remembered columns, no
 * remembered leaf), the relations, the user pointer and the definition index
 * links are NULL. On failure nothing is left allocated.
 */
static amark_error_t build_node(const amark_node_t *source, amark_node_t **out)
{
    const amark_node_type_desc_t *desc = source->desc;
    amark_node_t *node;
    amark_error_t error;

    *out = NULL;
    node = (amark_node_t *)amark_calloc(1, sizeof(*node));
    if (node == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    node->document = source->document;
    node->desc = desc;
    node->type = source->type;
    node->line_first = source->line_first;
    node->line_count = source->line_count;
    node->dropped = source->dropped;
    error = copy_facets(node, source);
    if (error == AMARK_ERROR_NONE)
    {
        error = copy_records(node, source);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = copy_content_tokens(node, source);
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

/* Commits node, built from source: it shares the content and the base of the
 * content tokens of source, joins the ring of its document at its end, and a
 * link_definition goes on the dirty list of the definition index. Allocates
 * nothing. */
static void commit_node(amark_node_t *node, const amark_node_t *source)
{
    amark_document_t *document = node->document;

    if (source->content != NULL)
    {
        node->content = source->content;
        node->content->owners++;
    }
    if (node->content_tokens != NULL)
    {
        node->content_tokens->base = source->content_tokens->base;
        node->content_tokens->base->bases++;
    }
    node->ring_next = document->root;
    node->ring_prev = document->root->ring_prev;
    node->ring_prev->ring_next = node;
    document->root->ring_prev = node;
    document->node_count++;
    document->stats.nodes_created++;
    amark_tree_def_index_mark(node);
}

/* Appends child as the last child of parent (both new and detached; no change
 * is notified, as no node of the tree changes). */
static void append_new_child(amark_node_t *parent, amark_node_t *child)
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

/* ---- The copy ------------------------------------------------------------------ */

amark_error_t amark_node_copy(const amark_node_t *node, amark_node_t **out)
{
    const amark_node_t *source;
    amark_node_t *top = NULL;
    amark_node_t *current;
    amark_error_t error;

    if (out == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    *out = NULL;
    if ((node == NULL) || (node == node->document->root))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    error = build_node(node, &top);
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    commit_node(top, node);

    /* Preorder walk of the subtree of node without recursion; current is the
     * copy of source. */
    source = node;
    current = top;
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
            while ((source != node) && (source->next == NULL))
            {
                source = source->parent;
                current = current->parent;
            }
            if (source == node)
            {
                break;
            }
            next = source->next;
            parent = current->parent;
        }
        error = build_node(next, &copy);
        if (error != AMARK_ERROR_NONE)
        {
            /* Every node made so far is committed and under top. */
            amark_node_destroy(top);
            return error;
        }
        commit_node(copy, next);
        append_new_child(parent, copy);
        source = next;
        current = copy;
    }
    *out = top;
    return AMARK_ERROR_NONE;
}
