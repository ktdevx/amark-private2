/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/* Copies and imports from another document (source copies and identifier
 * remapping). */

#include <amark/node.h>

#include <amark/allocator.h>
#include <amark/document.h>
#include <amark/error.h>
#include <amark/extension.h>

#include "base/allocator_internal.h"
#include "tree/tree.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* An identifier of source_map for a source that no token of the copy refers to. */
#define SOURCE_UNUSED UINT32_MAX

/*
 * A copy of subtrees of the document from into the document to. When they are
 * the same document, the types and the sources stay as they are; otherwise every
 * type is found in the syntax set of to by its descriptor, and the sources the
 * tokens refer to are copied into to (source_map).
 */
typedef struct copy_context
{
    amark_document_t *to;
    const amark_document_t *from;
    /* The last descriptor found in the syntax set of to and its type (a cache of
     * amark_syntax_set_node_type_of, a linear search). */
    const amark_node_type_desc_t *cached_desc;
    amark_node_type_t cached_type;
    /* For each source of from (from->source_count entries), its identifier in to,
     * or SOURCE_UNUSED; NULL for the same document or a document without sources. */
    uint32_t *source_map;
    /* The number of sources of to before the copy, to remove the added ones. */
    size_t to_source_count;
} copy_context_t;

static amark_error_t context_init(copy_context_t *context, amark_document_t *to,
                                  const amark_document_t *from)
{
    context->to = to;
    context->from = from;
    context->cached_desc = NULL;
    context->cached_type = AMARK_NODE_NONE;
    context->source_map = NULL;
    context->to_source_count = to->source_count;

    if ((to != from) && (from->source_count != 0))
    {
        size_t bytes;
        size_t i;

        if (!amark_size_mul(from->source_count, sizeof(uint32_t), &bytes))
        {
            return AMARK_ERROR_OVERFLOW;
        }

        context->source_map = amark_malloc(bytes);
        if (context->source_map == NULL)
        {
            return AMARK_ERROR_OUT_OF_MEMORY;
        }

        for (i = 0; i < from->source_count; i++)
        {
            context->source_map[i] = SOURCE_UNUSED;
        }
    }

    return AMARK_ERROR_NONE;
}

static void context_deinit(copy_context_t *context)
{
    amark_free(context->source_map);
    context->source_map = NULL;
}

/* Removes the sources the copy added to the destination (none for the same
 * document). */
static void context_rollback(copy_context_t *context)
{
    amark_document_truncate_sources(context->to, context->to_source_count);
}

/* The node after current in the preorder of the subtree of top, or NULL. */
static const amark_node_t *next_in_subtree(const amark_node_t *current, const amark_node_t *top)
{
    if (current->first_child != NULL)
    {
        return current->first_child;
    }

    while ((current != top) && (current->next == NULL))
    {
        current = current->parent;
    }

    return (current == top) ? NULL : current->next;
}

/* Finds in *out_type the type of node in the destination: its own type in the same
 * document, else the type of its descriptor in the syntax set of the destination.
 * Returns false when that set does not hold the descriptor. */
static bool map_type(copy_context_t *context, const amark_node_t *node,
                     amark_node_type_t *out_type)
{
    if (context->to == context->from)
    {
        *out_type = node->type;
        return true;
    }

    if (node->desc != context->cached_desc)
    {
        amark_node_type_t type;

        if (!amark_syntax_set_node_type_of(context->to->syntax, node->desc, &type))
        {
            return false;
        }

        context->cached_desc = node->desc;
        context->cached_type = type;
    }

    *out_type = context->cached_type;
    return true;
}

/* Checks that every type of the subtree of node is in the destination:
 * AMARK_ERROR_INVALID_ARGUMENT otherwise. */
static amark_error_t check_types(copy_context_t *context, const amark_node_t *node)
{
    const amark_node_t *current;

    for (current = node; current != NULL; current = next_in_subtree(current, node))
    {
        amark_node_type_t type;

        if (!map_type(context, current, &type))
        {
            return AMARK_ERROR_INVALID_ARGUMENT;
        }
    }

    return AMARK_ERROR_NONE;
}

/* Marks in the source map the sources the tokens of the subtree of node refer to
 * (nothing for the same document). */
static void mark_sources(copy_context_t *context, const amark_node_t *node)
{
    const amark_node_t *current;

    if (context->source_map == NULL)
    {
        return;
    }

    for (current = node; current != NULL; current = next_in_subtree(current, node))
    {
        size_t i;

        for (i = 0; i < current->token_count; i++)
        {
            context->source_map[current->tokens[i].source] = 0;
        }
    }
}

/*
 * Copies the marked sources into the destination, in the order of their
 * identifiers, and writes their new identifiers in the source map. The map thus
 * keeps the order of the identifiers, and so the order of the tokens of every
 * node. On failure removes the sources it added.
 */
static amark_error_t copy_sources(copy_context_t *context)
{
    size_t i;

    if (context->source_map == NULL)
    {
        return AMARK_ERROR_NONE;
    }

    for (i = 0; i < context->from->source_count; i++)
    {
        const amark_source_t *source = &context->from->sources[i];
        amark_source_id_t id;
        amark_error_t err;

        if (context->source_map[i] == SOURCE_UNUSED)
        {
            continue;
        }

        err = amark_document_add_source(context->to, source->bytes.data, source->bytes.length,
                                        &id);
        if (err != AMARK_ERROR_NONE)
        {
            context_rollback(context);
            return err;
        }

        context->source_map[i] = id;
    }

    return AMARK_ERROR_NONE;
}

/* Creates in *out_copy a detached copy of the node src alone: its type, type data,
 * literal, tokens (with the source identifiers of the destination) and records. */
static amark_error_t copy_node(copy_context_t *context, const amark_node_t *src,
                               amark_node_t **out_copy)
{
    amark_node_t *copy;
    amark_node_type_t type = AMARK_NODE_NONE;
    amark_error_t err;

    *out_copy = NULL;

    /* Checked before the copy started. */
    (void)map_type(context, src, &type);

    err = amark_node_create_copy(context->to, type, src, &copy);
    if (err != AMARK_ERROR_NONE)
    {
        return err;
    }

    if (src->literal != NULL)
    {
        /* The literal is NUL-terminated, so its length + 1 fits in size_t. */
        copy->literal = amark_malloc(src->literal_length + 1);
        if (copy->literal == NULL)
        {
            amark_node_destroy_subtree(copy);
            return AMARK_ERROR_OUT_OF_MEMORY;
        }

        memcpy(copy->literal, src->literal, src->literal_length + 1);
        copy->literal_length = src->literal_length;
    }

    if (src->token_count != 0)
    {
        size_t bytes;
        size_t i;

        /* Fits: the tokens of src take as many bytes. */
        (void)amark_size_mul(src->token_count, sizeof(amark_token_record_t), &bytes);
        copy->tokens = amark_malloc(bytes);
        if (copy->tokens == NULL)
        {
            amark_node_destroy_subtree(copy);
            return AMARK_ERROR_OUT_OF_MEMORY;
        }

        memcpy(copy->tokens, src->tokens, bytes);
        copy->token_count = src->token_count;
        copy->token_capacity = src->token_count;
        if (context->source_map != NULL)
        {
            for (i = 0; i < copy->token_count; i++)
            {
                copy->tokens[i].source = context->source_map[copy->tokens[i].source];
            }
        }
    }

    /* The records as they are (no AMARK_EDIT_CREATED for the copy itself): the
     * subtree keeps the invariant of AMARK_EDIT_DESCENDANTS within it, and its
     * insertion propagates it to its new ancestors. */
    copy->edits = src->edits;

    *out_copy = copy;
    return AMARK_ERROR_NONE;
}

/* Links child as the last child of parent, without records (the copy of a link of
 * the source). */
static void append_copy(amark_node_t *parent, amark_node_t *child)
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

/*
 * Copies the subtree of node into a detached subtree of the destination, whose top
 * goes to *out_copy, without recursion: the preorder of the source, going back up
 * with the parent pointers, and the parent of the next copy alongside. The types
 * are checked and the sources copied beforehand. On failure frees every node it
 * created.
 */
static amark_error_t copy_subtree(copy_context_t *context, const amark_node_t *node,
                                  amark_node_t **out_copy)
{
    const amark_node_t *src = node;
    /* The copy of the parent of src (NULL for node). */
    amark_node_t *parent = NULL;
    amark_node_t *top = NULL;

    *out_copy = NULL;

    for (;;)
    {
        amark_node_t *copy;
        amark_error_t err = copy_node(context, src, &copy);

        if (err != AMARK_ERROR_NONE)
        {
            if (top != NULL)
            {
                amark_node_destroy_subtree(top);
            }

            return err;
        }

        if (parent == NULL)
        {
            top = copy;
        }
        else
        {
            append_copy(parent, copy);
        }

        if (src->first_child != NULL)
        {
            parent = copy;
            src = src->first_child;
            continue;
        }

        while ((src != node) && (src->next == NULL))
        {
            src = src->parent;
            parent = parent->parent;
        }

        if (src == node)
        {
            break;
        }

        src = src->next;
    }

    *out_copy = top;
    return AMARK_ERROR_NONE;
}

/* The import of the subtree of node, not the root of its document, into to. */
static amark_error_t import_subtree(amark_document_t *to, const amark_node_t *node,
                                    amark_node_t **out_copy)
{
    copy_context_t context;
    amark_error_t err;

    err = context_init(&context, to, node->document);
    if (err != AMARK_ERROR_NONE)
    {
        return err;
    }

    err = check_types(&context, node);
    if (err == AMARK_ERROR_NONE)
    {
        mark_sources(&context, node);
        err = copy_sources(&context);
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = copy_subtree(&context, node, out_copy);
        if (err != AMARK_ERROR_NONE)
        {
            context_rollback(&context);
        }
    }

    context_deinit(&context);
    return err;
}

amark_error_t amark_node_copy(const amark_node_t *node, amark_node_t **out_copy)
{
    if (out_copy == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    *out_copy = NULL;
    if ((node == NULL) || (node == node->document->root))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    return import_subtree(node->document, node, out_copy);
}

amark_error_t amark_node_import(amark_document_t *document, const amark_node_t *node,
                                amark_node_t **out_copy)
{
    if (out_copy == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    *out_copy = NULL;
    if ((document == NULL) || (node == NULL) || (node == node->document->root))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    return import_subtree(document, node, out_copy);
}
