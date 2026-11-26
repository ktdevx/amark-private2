/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * NodeCore: the allocation and release of nodes, the relations, the user
 * pointers, the type data and the change notification.
 */
#include "tree/tree.h"

#include <amark/allocator.h>

/* ---- Allocation and release ------------------------------------------------ */

/* Frees the memory of node and of what it holds, without calling deinit. */
static void free_node_memory(amark_node_t *node)
{
    if (node->facets != NULL)
    {
        size_t i;

        for (i = 0; i < node->desc->facet_count; i++)
        {
            amark_free(node->facets[i].text);
            amark_free(node->facets[i].cache);
        }
        amark_free(node->facets);
    }
    amark_tree_records_free(node->records);
    amark_tree_content_tokens_free(node->content_tokens);
    amark_tree_content_release(node->content, true);
    amark_free(node->data);
    amark_free(node);
}

amark_error_t amark_tree_node_new(amark_document_t *document, amark_node_type_t type,
                                  amark_node_t **out)
{
    const amark_node_type_desc_t *desc;
    amark_node_t *node;
    amark_error_t error;

    *out = NULL;
    desc = amark_syntax_set_node_type_desc(document->set, type);
    if (desc == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    node = (amark_node_t *)amark_calloc(1, sizeof(*node));
    if (node == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    node->document = document;
    node->desc = desc;
    node->type = type;
    if (desc->facet_count > 0)
    {
        node->facets =
            (amark_tree_facet_t *)amark_calloc(desc->facet_count, sizeof(amark_tree_facet_t));
        if (node->facets == NULL)
        {
            free_node_memory(node);
            return AMARK_ERROR_OUT_OF_MEMORY;
        }
    }
    if (desc->data_size > 0)
    {
        node->data = amark_calloc(1, desc->data_size);
        if (node->data == NULL)
        {
            free_node_memory(node);
            return AMARK_ERROR_OUT_OF_MEMORY;
        }
    }
    if (desc->init != NULL)
    {
        error = desc->init(node, node->data);
        if (error != AMARK_ERROR_NONE)
        {
            free_node_memory(node);
            return error;
        }
    }

    /* Commit: add the node at the end of the ring (before the root). */
    if (document->root == NULL)
    {
        node->ring_next = node;
        node->ring_prev = node;
    }
    else
    {
        node->ring_next = document->root;
        node->ring_prev = document->root->ring_prev;
        node->ring_prev->ring_next = node;
        document->root->ring_prev = node;
    }
    document->node_count++;
    document->stats.nodes_created++;
    *out = node;
    return AMARK_ERROR_NONE;
}

void amark_tree_node_release(amark_node_t *node)
{
    amark_document_t *document = node->document;

    if (node->desc->deinit != NULL)
    {
        node->desc->deinit(node, node->data);
    }
    amark_tree_def_index_forget(node);
    node->ring_prev->ring_next = node->ring_next;
    node->ring_next->ring_prev = node->ring_prev;
    document->node_count--;
    document->stats.nodes_released++;
    free_node_memory(node);
}

void amark_tree_node_detach(amark_node_t *node)
{
    amark_node_t *parent = node->parent;

    if (parent == NULL)
    {
        return;
    }
    if (node->prev != NULL)
    {
        node->prev->next = node->next;
    }
    else
    {
        parent->first_child = node->next;
    }
    if (node->next != NULL)
    {
        node->next->prev = node->prev;
    }
    else
    {
        parent->last_child = node->prev;
    }
    node->parent = NULL;
    node->prev = NULL;
    node->next = NULL;
    amark_tree_changed(parent, AMARK_TREE_CHANGE_STRUCTURE);
}

/* ---- Change notification ---------------------------------------------------- */

void amark_tree_changed(amark_node_t *node, amark_tree_change_t kind)
{
    amark_document_t *document = node->document;
    amark_node_t *current;

    if (kind == AMARK_TREE_CHANGE_STRUCTURE)
    {
        document->structure_generation++;
    }
    else
    {
        /* The label of a definition may have changed: the next lookup of the
         * definition index reads it again. */
        amark_tree_def_index_mark(node);
    }
    /* A node whose cache is invalid has only invalid ancestors: stop there. */
    for (current = node; current != NULL; current = current->parent)
    {
        document->stats.invalidation_steps++;
        if ((current->cache_flags & AMARK_TREE_CACHE_VALID) == 0)
        {
            break;
        }
        current->cache_flags &= ~AMARK_TREE_CACHE_VALID;
    }
}

amark_error_t amark_node_create(amark_document_t *document, amark_node_type_t type,
                                amark_node_t **out)
{
    if (out == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    *out = NULL;
    if (document == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    return amark_tree_node_new(document, type, out);
}

void amark_node_destroy(amark_node_t *node)
{
    amark_document_t *document;
    amark_node_t *current;

    if (node == NULL || node == node->document->root)
    {
        return;
    }
    document = node->document;
    amark_tree_node_detach(node);

    /* Post-order walk over the links, without recursion or stack: go down to the
     * first leaf, release it, and continue with its next sibling, or with its
     * parent, which has then lost all of its children. */
    current = node;
    for (;;)
    {
        amark_node_t *parent;
        amark_node_t *next;

        while (current->first_child != NULL)
        {
            current = current->first_child;
            document->stats.release_steps++;
        }
        document->stats.release_steps++;
        if (current == node)
        {
            amark_tree_node_release(current);
            return;
        }
        parent = current->parent;
        next = current->next;
        amark_tree_node_release(current);
        parent->first_child = next;
        if (next != NULL)
        {
            next->prev = NULL;
            current = next;
        }
        else
        {
            parent->last_child = NULL;
            current = parent;
        }
    }
}

/* ---- Relations ------------------------------------------------------------- */

amark_node_type_t amark_node_get_type(const amark_node_t *node)
{
    return node != NULL ? node->type : AMARK_NODE_NONE;
}

const amark_node_type_desc_t *amark_node_type_desc(const amark_node_t *node)
{
    return node != NULL ? node->desc : NULL;
}

amark_document_t *amark_node_document(const amark_node_t *node)
{
    return node != NULL ? node->document : NULL;
}

amark_node_t *amark_node_parent(const amark_node_t *node)
{
    return node != NULL ? node->parent : NULL;
}

amark_node_t *amark_node_first_child(const amark_node_t *node)
{
    return node != NULL ? node->first_child : NULL;
}

amark_node_t *amark_node_last_child(const amark_node_t *node)
{
    return node != NULL ? node->last_child : NULL;
}

amark_node_t *amark_node_next(const amark_node_t *node)
{
    return node != NULL ? node->next : NULL;
}

amark_node_t *amark_node_previous(const amark_node_t *node)
{
    return node != NULL ? node->prev : NULL;
}

/* ---- Classification -------------------------------------------------------- */

bool amark_node_is_block(const amark_node_t *node)
{
    return node != NULL && node->desc->level == AMARK_NODE_LEVEL_BLOCK;
}

bool amark_node_is_inline(const amark_node_t *node)
{
    return node != NULL && node->desc->level == AMARK_NODE_LEVEL_INLINE;
}

bool amark_node_is_container(const amark_node_t *node)
{
    return node != NULL && (node->desc->flags & AMARK_NODE_TYPE_MARGINS) != 0;
}

bool amark_node_has_content(const amark_node_t *node)
{
    return node != NULL && (node->desc->flags & AMARK_NODE_TYPE_CONTENT) != 0;
}

bool amark_node_can_contain(const amark_node_t *parent, amark_node_type_t child)
{
    if (parent == NULL)
    {
        return false;
    }
    return amark_registry_can_contain(parent->document->set, parent, parent->type, child);
}

/* ---- User pointers and extension data -------------------------------------- */

void *amark_node_user_data(const amark_node_t *node)
{
    return node != NULL ? node->user_data : NULL;
}

void amark_node_set_user_data(amark_node_t *node, void *user_data)
{
    if (node != NULL)
    {
        node->user_data = user_data;
    }
}

void *amark_node_data(amark_node_t *node)
{
    return node != NULL ? node->data : NULL;
}
