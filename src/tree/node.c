/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/* Nodes: creation, subtree destruction (internal), queries, type data and
 * associated pointers. */

#include <amark/node.h>

#include <amark/allocator.h>
#include <amark/error.h>
#include <amark/extension.h>

#include "base/allocator_internal.h"
#include "tree/syntax_set.h"
#include "tree/tree.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The types with the strictest alignment C99 has (it has no max_align_t). */
typedef union node_max_align
{
    long double long_double_value;
    long long long_long_value;
    double double_value;
    void *pointer_value;
    void (*function_value)(void);
} node_max_align_t;

typedef struct node_align_probe
{
    char c;
    node_max_align_t value;
} node_align_probe_t;

/* The alignment of node_max_align_t, and the offset of the type data in a node: the
 * size of the header rounded up to it. */
#define NODE_MAX_ALIGN offsetof(node_align_probe_t, value)
#define DATA_OFFSET \
    (((sizeof(struct amark_node) + (NODE_MAX_ALIGN - 1)) / NODE_MAX_ALIGN) * NODE_MAX_ALIGN)

/* The type data of node, or NULL when its type has none. */
static void *node_data(const amark_node_t *node)
{
    if (node->desc->data_size == 0)
    {
        return NULL;
    }

    return (char *)node + DATA_OFFSET;
}

/* Adds node to the ring of all the nodes of its document. */
static void ring_add(amark_node_t *node)
{
    amark_document_t *document = node->document;
    amark_node_t *head = document->ring;

    if (head == NULL)
    {
        node->ring_prev = node;
        node->ring_next = node;
        document->ring = node;
        return;
    }

    node->ring_next = head;
    node->ring_prev = head->ring_prev;
    head->ring_prev->ring_next = node;
    head->ring_prev = node;
}

/* Removes node from the ring of all the nodes of its document. */
static void ring_remove(amark_node_t *node)
{
    amark_document_t *document = node->document;

    if (node->ring_next == node)
    {
        document->ring = NULL;
    }
    else
    {
        node->ring_prev->ring_next = node->ring_next;
        node->ring_next->ring_prev = node->ring_prev;
        if (document->ring == node)
        {
            document->ring = node->ring_next;
        }
    }

    node->ring_prev = NULL;
    node->ring_next = NULL;
}

/* Frees node, whose links are no longer used: the type data, the literal, the
 * tokens, and its place in the ring. */
static void node_free(amark_node_t *node)
{
    if (node->desc->deinit != NULL)
    {
        node->desc->deinit(node, node_data(node));
    }

    amark_free(node->literal);
    amark_free(node->tokens);
    ring_remove(node);
    amark_free(node);
}

/* --- Creation and destruction ---------------------------------------------------- */

amark_error_t amark_node_create_with_desc(amark_document_t *document, amark_node_type_t type,
                                          const amark_node_type_desc_t *desc,
                                          amark_node_t **out_node)
{
    amark_node_t *node;
    size_t size;
    amark_error_t err;

    if (out_node == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    *out_node = NULL;
    if ((document == NULL) || (desc == NULL))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    if (!amark_size_add(DATA_OFFSET, desc->data_size, &size))
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }

    /* Zero-fills the header (no links, no literal, no tokens, no records) and the
     * type data. */
    node = amark_calloc(1, size);
    if (node == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }

    node->document = document;
    node->desc = desc;
    node->type = type;

    if (desc->init != NULL)
    {
        err = desc->init(node, node_data(node));
        if (err != AMARK_ERROR_NONE)
        {
            amark_free(node);
            return err;
        }
    }

    ring_add(node);
    amark_edit_mark(node, AMARK_EDIT_CREATED);

    *out_node = node;
    return AMARK_ERROR_NONE;
}

amark_error_t amark_node_create_copy(amark_document_t *document, amark_node_type_t type,
                                     const amark_node_t *src, amark_node_t **out_node)
{
    const amark_node_type_desc_t *desc = src->desc;
    amark_node_t *node;
    size_t size;

    *out_node = NULL;
    if (!amark_size_add(DATA_OFFSET, desc->data_size, &size))
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }

    /* Zero-filled like a new node, but the type data is the copy of the one of src
     * (the copy function, or else its bytes) instead of what init would make. */
    node = amark_calloc(1, size);
    if (node == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }

    node->document = document;
    node->desc = desc;
    node->type = type;

    if (desc->data_size != 0)
    {
        if (desc->copy != NULL)
        {
            amark_error_t err = desc->copy(node, node_data(node), src, node_data(src));

            /* Like a failed init: the data was never made, so no deinit. */
            if (err != AMARK_ERROR_NONE)
            {
                amark_free(node);
                return err;
            }
        }
        else
        {
            memcpy(node_data(node), node_data(src), desc->data_size);
        }
    }

    ring_add(node);

    *out_node = node;
    return AMARK_ERROR_NONE;
}

amark_error_t amark_node_create(amark_document_t *document, amark_node_type_t type,
                                amark_node_t **out_node)
{
    const amark_node_type_desc_t *desc;

    if (out_node == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    *out_node = NULL;
    if ((document == NULL) || (type == AMARK_NODE_DOCUMENT))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    desc = amark_syntax_set_desc(document->syntax, type);
    if (desc == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    return amark_node_create_with_desc(document, type, desc, out_node);
}

void amark_node_destroy_subtree(amark_node_t *node)
{
    amark_node_t *current = node;

    /* Postorder without recursion: go down to a node without children, free it
     * after unlinking it from its parent (whose next child becomes the first), and
     * go on from the parent. Each link is followed down once. */
    for (;;)
    {
        amark_node_t *parent;

        while (current->first_child != NULL)
        {
            current = current->first_child;
        }

        if (current == node)
        {
            node_free(current);
            return;
        }

        parent = current->parent;
        parent->first_child = current->next;
        if (current->next != NULL)
        {
            current->next->prev = NULL;
        }
        else
        {
            parent->last_child = NULL;
        }

        node_free(current);
        current = parent;
    }
}

/* --- Queries ------------------------------------------------------------------- */

amark_node_type_t amark_node_get_type(const amark_node_t *node)
{
    return (node != NULL) ? node->type : AMARK_NODE_NONE;
}

const char *amark_node_type_name(const amark_node_t *node)
{
    return (node != NULL) ? node->desc->name : "";
}

const amark_node_type_desc_t *amark_node_desc(const amark_node_t *node)
{
    return (node != NULL) ? node->desc : NULL;
}

amark_document_t *amark_node_document(const amark_node_t *node)
{
    return (node != NULL) ? node->document : NULL;
}

amark_node_t *amark_node_parent(const amark_node_t *node)
{
    return (node != NULL) ? node->parent : NULL;
}

amark_node_t *amark_node_first_child(const amark_node_t *node)
{
    return (node != NULL) ? node->first_child : NULL;
}

amark_node_t *amark_node_last_child(const amark_node_t *node)
{
    return (node != NULL) ? node->last_child : NULL;
}

amark_node_t *amark_node_next(const amark_node_t *node)
{
    return (node != NULL) ? node->next : NULL;
}

amark_node_t *amark_node_previous(const amark_node_t *node)
{
    return (node != NULL) ? node->prev : NULL;
}

bool amark_node_is_block(const amark_node_t *node)
{
    return (node != NULL) && (node->desc->level == AMARK_NODE_LEVEL_BLOCK);
}

bool amark_node_is_inline(const amark_node_t *node)
{
    return (node != NULL) && (node->desc->level == AMARK_NODE_LEVEL_INLINE);
}

bool amark_node_can_have_children(const amark_node_t *node)
{
    return (node != NULL) && ((node->desc->accepts != 0) || (node->desc->can_contain != NULL));
}

bool amark_node_can_contain(const amark_node_t *parent, amark_node_type_t child_type)
{
    const amark_node_type_desc_t *child_desc;

    if (parent == NULL)
    {
        return false;
    }

    child_desc = amark_syntax_set_desc(parent->document->syntax, child_type);
    if (child_desc == NULL)
    {
        return false;
    }

    if (parent->desc->can_contain != NULL)
    {
        return parent->desc->can_contain(parent->desc, child_desc);
    }

    return (parent->desc->accepts & child_desc->provides) != 0;
}

/* --- Associated pointer and type data -------------------------------------------- */

void *amark_node_user_data(const amark_node_t *node)
{
    return (node != NULL) ? node->user_data : NULL;
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
    return (node != NULL) ? node_data(node) : NULL;
}

const void *amark_node_data_const(const amark_node_t *node)
{
    return (node != NULL) ? node_data(node) : NULL;
}
