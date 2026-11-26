/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * DefinitionIndex: the label index, the document order and the lookup of the
 * definitions (9.2, 9.3, 14.8).
 *
 * The index of a document maps each normalized label to the group of the
 * link_definition nodes, attached or not, whose label normalizes to it
 * (amark_tree_def_group_t, in a hash table). It is not updated by the
 * operations themselves:
 *   - an operation that may change the label of a definition (a token record,
 *     move or split, a record or a set, a drop of the spelling: every change
 *     notified with AMARK_TREE_CHANGE_RECORD) only puts the definition on the
 *     dirty list of the document (amark_tree_def_index_mark);
 *   - the release of a definition removes it from its group and from the dirty
 *     list (amark_tree_def_index_forget);
 *   - the structural edits change nothing here (14.5): whether a definition is
 *     attached, and the document order, are decided by the lookup.
 * A lookup first processes the dirty list: each definition gets the key of its
 * current label (a label that normalizes to nothing is in no group), in time
 * proportional to the label. It then normalizes the label looked up, finds its
 * group and keeps the members attached to the document node (walking up from
 * each, in time proportional to the depth); among several, the first in
 * document order (by the sibling order under the common ancestor of two of
 * them). The result is kept in the group with the structure generation of the
 * document, and returned while the generation is the same and the members do
 * not change.
 */
#include "tree/tree.h"

#include <amark/allocator.h>
#include <amark/buffer.h>

#include <string.h>

/* The first size of the table. */
#define FIRST_BUCKET_COUNT 8u

/* ---- The dirty list ---------------------------------------------------------------- */

void amark_tree_def_index_mark(amark_node_t *node)
{
    amark_tree_def_index_t *index;

    if ((node->type != AMARK_NODE_LINK_DEFINITION) || node->def_dirty)
    {
        return;
    }
    index = &node->document->def_index;
    node->def_dirty = true;
    node->dirty_next = NULL;
    node->dirty_prev = index->dirty_last;
    if (index->dirty_last != NULL)
    {
        index->dirty_last->dirty_next = node;
    }
    else
    {
        index->dirty_first = node;
    }
    index->dirty_last = node;
    index->dirty_count++;
}

/* Removes node, which is dirty, from the dirty list. */
static void unmark(amark_node_t *node)
{
    amark_tree_def_index_t *index = &node->document->def_index;

    if (node->dirty_prev != NULL)
    {
        node->dirty_prev->dirty_next = node->dirty_next;
    }
    else
    {
        index->dirty_first = node->dirty_next;
    }
    if (node->dirty_next != NULL)
    {
        node->dirty_next->dirty_prev = node->dirty_prev;
    }
    else
    {
        index->dirty_last = node->dirty_prev;
    }
    node->def_dirty = false;
    node->dirty_next = NULL;
    node->dirty_prev = NULL;
    index->dirty_count--;
}

/* ---- The groups --------------------------------------------------------------------- */

/* The bucket of hash in the table of index (which has buckets). */
static amark_tree_def_group_t **bucket_of(const amark_tree_def_index_t *index, uint64_t hash)
{
    return &index->buckets[(size_t)(hash & (uint64_t)(index->bucket_count - 1u))];
}

/* The group of key, or NULL. */
static amark_tree_def_group_t *find_group(const amark_tree_def_index_t *index, const char *key,
                                          size_t length, uint64_t hash)
{
    amark_tree_def_group_t *group;

    if (index->bucket_count == 0)
    {
        return NULL;
    }
    for (group = *bucket_of(index, hash); group != NULL; group = group->bucket_next)
    {
        if ((group->hash == hash) && (group->key_length == length) &&
            (memcmp(group->key, key, length) == 0))
        {
            return group;
        }
    }
    return NULL;
}

/* Adds node to the members of group. */
static void group_add(amark_tree_def_group_t *group, amark_node_t *node)
{
    node->def_group = group;
    node->def_next = NULL;
    node->def_prev = group->last;
    if (group->last != NULL)
    {
        group->last->def_next = node;
    }
    else
    {
        group->first = node;
    }
    group->last = node;
    group->count++;
    group->cached_valid = false;
    group->cached = NULL;
}

/* Removes node from its group, and the group from the table once it has no
 * member left. */
static void group_remove(amark_tree_def_index_t *index, amark_node_t *node)
{
    amark_tree_def_group_t *group = node->def_group;

    if (node->def_prev != NULL)
    {
        node->def_prev->def_next = node->def_next;
    }
    else
    {
        group->first = node->def_next;
    }
    if (node->def_next != NULL)
    {
        node->def_next->def_prev = node->def_prev;
    }
    else
    {
        group->last = node->def_prev;
    }
    node->def_group = NULL;
    node->def_next = NULL;
    node->def_prev = NULL;
    group->count--;
    group->cached_valid = false;
    group->cached = NULL;
    if (group->count == 0)
    {
        amark_tree_def_group_t **link = bucket_of(index, group->hash);

        while (*link != group)
        {
            link = &(*link)->bucket_next;
        }
        *link = group->bucket_next;
        index->group_count--;
        amark_free(group->key);
        amark_free(group);
    }
}

void amark_tree_def_index_forget(amark_node_t *node)
{
    if (node->def_dirty)
    {
        unmark(node);
    }
    if (node->def_group != NULL)
    {
        group_remove(&node->document->def_index, node);
    }
}

void amark_tree_def_index_deinit(amark_document_t *document)
{
    amark_tree_def_index_t *index = &document->def_index;
    size_t i;

    /* The release of the nodes removed every group; free any left anyway. */
    for (i = 0; i < index->bucket_count; i++)
    {
        while (index->buckets[i] != NULL)
        {
            amark_tree_def_group_t *group = index->buckets[i];

            index->buckets[i] = group->bucket_next;
            amark_free(group->key);
            amark_free(group);
        }
    }
    amark_free(index->buckets);
    memset(index, 0, sizeof(*index));
}

/* ---- Processing the dirty list ------------------------------------------------------- */

/*
 * Gives node, the first dirty definition, the group of its current label and
 * takes it off the dirty list. scratch receives the normalized label. Every
 * allocation (the derived label, the normalized label, a new group, a larger
 * table) is done before anything changes: on failure node stays dirty in its
 * old group.
 */
static amark_error_t process(amark_document_t *document, amark_node_t *node,
                             amark_buffer_t *scratch)
{
    amark_tree_def_index_t *index = &document->def_index;
    amark_tree_def_group_t *target = NULL;
    amark_tree_def_group_t *created = NULL;
    amark_tree_def_group_t **buckets = NULL;
    size_t bucket_count = 0;
    amark_value_t label;
    amark_error_t error;
    const char *key;
    size_t length;
    uint64_t hash = 0;

    document->stats.index_steps++;
    label.struct_size = sizeof(label);
    error = amark_node_get(node, AMARK_LINK_DEFINITION_LABEL, &label);
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    amark_buffer_clear(scratch);
    error = amark_label_normalize(label.text, label.length, scratch);
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    key = amark_buffer_data(scratch);
    length = amark_buffer_length(scratch);
    if (length > 0)
    {
        hash = amark_tree_label_hash(key, length);
        target = find_group(index, key, length, hash);
    }
    if ((length > 0) && (target == NULL))
    {
        /* A new group, and a larger table when it would hold more groups than
         * buckets. */
        created = (amark_tree_def_group_t *)amark_calloc(1, sizeof(*created));
        if (created != NULL)
        {
            created->key = (char *)amark_malloc(length + 1);
        }
        if ((created == NULL) || (created->key == NULL))
        {
            amark_free(created);
            return AMARK_ERROR_OUT_OF_MEMORY;
        }
        memcpy(created->key, key, length);
        created->key[length] = '\0';
        created->key_length = length;
        created->hash = hash;
        if (index->group_count >= index->bucket_count)
        {
            bucket_count = index->bucket_count == 0 ? FIRST_BUCKET_COUNT : index->bucket_count * 2;
            buckets = (bucket_count <= SIZE_MAX / sizeof(*buckets))
                          ? (amark_tree_def_group_t **)amark_calloc(bucket_count,
                                                                    sizeof(*buckets))
                          : NULL;
            if (buckets == NULL)
            {
                amark_free(created->key);
                amark_free(created);
                return AMARK_ERROR_OUT_OF_MEMORY;
            }
        }
    }

    /* Commit. */
    if (buckets != NULL)
    {
        size_t i;

        for (i = 0; i < index->bucket_count; i++)
        {
            while (index->buckets[i] != NULL)
            {
                amark_tree_def_group_t *group = index->buckets[i];
                amark_tree_def_group_t **bucket =
                    &buckets[(size_t)(group->hash & (uint64_t)(bucket_count - 1u))];

                index->buckets[i] = group->bucket_next;
                group->bucket_next = *bucket;
                *bucket = group;
            }
        }
        amark_free(index->buckets);
        index->buckets = buckets;
        index->bucket_count = bucket_count;
    }
    if (created != NULL)
    {
        amark_tree_def_group_t **bucket = bucket_of(index, hash);

        created->bucket_next = *bucket;
        *bucket = created;
        index->group_count++;
        target = created;
    }
    if (node->def_group != target)
    {
        if (node->def_group != NULL)
        {
            group_remove(index, node);
        }
        if (target != NULL)
        {
            group_add(target, node);
        }
    }
    unmark(node);
    return AMARK_ERROR_NONE;
}

/* ---- The lookup ---------------------------------------------------------------------- */

/* The depth of node under the top of its tree in *depth; whether that top is
 * the document node. One step per ancestor. */
static bool attached(amark_document_t *document, const amark_node_t *node, size_t *depth)
{
    size_t count = 0;

    while (node->parent != NULL)
    {
        node = node->parent;
        count++;
        document->stats.index_steps++;
    }
    *depth = count;
    return node == document->root;
}

/* Whether a, at depth a_depth, comes before b, at depth b_depth, in document
 * order (both attached, different): the ancestors of both at the same depth
 * are lifted to the children of their common ancestor, whose sibling order
 * decides, read from both at once so that it stops at the nearer end. */
static bool precedes(amark_document_t *document, const amark_node_t *a, size_t a_depth,
                     const amark_node_t *b, size_t b_depth)
{
    const amark_node_t *x;
    const amark_node_t *y;

    while (a_depth > b_depth)
    {
        a = a->parent;
        a_depth--;
        document->stats.index_steps++;
    }
    while (b_depth > a_depth)
    {
        b = b->parent;
        b_depth--;
        document->stats.index_steps++;
    }
    if (a == b)
    {
        /* One is an ancestor of the other, so it comes first. */
        return true;
    }
    while (a->parent != b->parent)
    {
        a = a->parent;
        b = b->parent;
        document->stats.index_steps++;
    }
    x = a->next;
    y = b->next;
    for (;;)
    {
        document->stats.index_steps++;
        if (x == b)
        {
            return true;
        }
        if ((y == a) || (x == NULL))
        {
            return false;
        }
        if (y == NULL)
        {
            return true;
        }
        x = x->next;
        y = y->next;
    }
}

/* The first member of group attached to the document node, in document order,
 * or NULL; kept in the group for the current structure generation. */
static amark_node_t *decide(amark_document_t *document, amark_tree_def_group_t *group)
{
    amark_node_t *best = NULL;
    size_t best_depth = 0;
    amark_node_t *member;

    if (group->cached_valid && (group->cached_generation == document->structure_generation))
    {
        return group->cached;
    }
    for (member = group->first; member != NULL; member = member->def_next)
    {
        size_t depth;

        if (attached(document, member, &depth) &&
            ((best == NULL) || precedes(document, member, depth, best, best_depth)))
        {
            best = member;
            best_depth = depth;
        }
    }
    group->cached = best;
    group->cached_generation = document->structure_generation;
    group->cached_valid = true;
    return best;
}

amark_error_t amark_tree_find_definition(amark_document_t *document, const char *label,
                                         size_t length, amark_node_t **out)
{
    amark_tree_def_index_t *index = &document->def_index;
    amark_buffer_t *scratch = NULL;
    amark_error_t error;

    *out = NULL;
    error = amark_buffer_create(&scratch);
    while ((error == AMARK_ERROR_NONE) && (index->dirty_first != NULL))
    {
        error = process(document, index->dirty_first, scratch);
    }
    if (error == AMARK_ERROR_NONE)
    {
        amark_buffer_clear(scratch);
        error = amark_label_normalize(label, length, scratch);
    }
    if ((error == AMARK_ERROR_NONE) && (amark_buffer_length(scratch) > 0))
    {
        const char *key = amark_buffer_data(scratch);
        size_t key_length = amark_buffer_length(scratch);
        amark_tree_def_group_t *group =
            find_group(index, key, key_length, amark_tree_label_hash(key, key_length));

        if (group != NULL)
        {
            *out = decide(document, group);
        }
    }
    amark_buffer_destroy(scratch);
    return error;
}

amark_error_t amark_document_find_definition(const amark_document_t *document, const char *label,
                                             size_t length, amark_node_t **out)
{
    if (out == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    *out = NULL;
    if ((document == NULL) || ((label == NULL) && (length != 0)))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    /* A lookup updates the index and its cached results: the document is
     * not changed as the API sees it (14.4: a document is used by one thread
     * at a time, even for the queries). */
    return amark_tree_find_definition((amark_document_t *)document, label, length, out);
}
