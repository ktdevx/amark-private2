/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/* Documents: creation and destruction, sources and their line index, positions,
 * edit tracking switch, syntax set ownership. */

#include <amark/document.h>

#include <amark/allocator.h>
#include <amark/error.h>
#include <amark/extension.h>
#include <amark/node.h>

#include "base/buffer_internal.h"
#include "tree/syntax_set.h"
#include "tree/tree.h"

#include <stdbool.h>
#include <stddef.h>

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

    /* Zero-filled: no node, no source, no owned syntax set, tracking off. */
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
