/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * Document: the creation and destruction of documents, the ownership of the
 * syntax set and the ring of every node (the sources and the line table are
 * LineTable's, in line_table.c).
 */
#include "tree/tree.h"

#include <amark/allocator.h>

amark_error_t amark_document_create(const amark_syntax_set_t *set, amark_document_t **out)
{
    amark_document_t *document;
    amark_node_t *root;
    amark_error_t error;

    if (out == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    *out = NULL;
    if (set == NULL)
    {
        set = amark_syntax_set_core();
    }
    error = amark_syntax_set_require_frozen(set);
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    document = (amark_document_t *)amark_calloc(1, sizeof(*document));
    if (document == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    document->set = set;
    error = amark_tree_node_new(document, AMARK_NODE_DOCUMENT, &root);
    if (error != AMARK_ERROR_NONE)
    {
        amark_free(document);
        return error;
    }
    document->root = root;
    *out = document;
    return AMARK_ERROR_NONE;
}

void amark_document_destroy(amark_document_t *document)
{
    amark_syntax_set_t *owned_set;
    amark_node_t *root;

    if (document == NULL)
    {
        return;
    }
    /* Every node, in the tree or not, is in the ring: release them all without
     * looking at the tree, the root last. */
    root = document->root;
    while (root->ring_next != root)
    {
        amark_tree_node_release(root->ring_next);
    }
    amark_tree_node_release(root);
    amark_tree_def_index_deinit(document);
    amark_line_table_deinit(document);
    /* The owned syntax set goes last, after every node (1.8). */
    owned_set = document->owned_set;
    amark_free(document);
    amark_syntax_set_destroy(owned_set);
}

amark_node_t *amark_document_root(const amark_document_t *document)
{
    return document != NULL ? document->root : NULL;
}

const amark_syntax_set_t *amark_document_syntax_set(const amark_document_t *document)
{
    return document != NULL ? document->set : NULL;
}

amark_error_t amark_document_take_syntax_set(amark_document_t *document, amark_syntax_set_t *set)
{
    if (document == NULL || set == NULL || set != document->set || set->is_static ||
        document->owned_set != NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    document->owned_set = set;
    return AMARK_ERROR_NONE;
}
