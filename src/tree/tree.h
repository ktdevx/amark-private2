/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The internal header of the syntax tree: the internal structures (documents,
 * nodes, the record arrays, the content objects, the lines) and the internal
 * functions shared by the modules of src/tree/.
 */
#ifndef AMARK_TREE_TREE_H
#define AMARK_TREE_TREE_H

#include <amark/node.h>
#include <amark/document.h>
#include <amark/value.h>
#include <amark/spelling.h>
#include <amark/iterator.h>
#include <amark/dump.h>
#include <amark/extension.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- Registry (registry.c) ------------------------------------------------- */

/* The name of the built-in extension, always first in a syntax set. */
#define AMARK_TREE_CORE_EXTENSION_NAME "amark.core"

/* A node type of a frozen syntax set: its descriptor and its extension. */
typedef struct amark_registry_type
{
    const amark_node_type_desc_t *desc;
    const amark_extension_t *extension;
} amark_registry_type_t;

/* An entry of the name index of a frozen syntax set. */
typedef struct amark_registry_name
{
    const char *name;
    amark_node_type_t type;
} amark_registry_name_t;

/* A slot of a frozen syntax set, in the slot index sorted by kind and then by the
 * order of the set. */
typedef struct amark_registry_slot
{
    uint32_t kind;
    const amark_slot_t *slot;
    const amark_extension_t *extension;
} amark_registry_slot_t;

struct amark_syntax_set
{
    /* The extensions in order; extensions[0] is the built-in extension. */
    const amark_extension_t **extensions;
    size_t extension_count;
    size_t extension_capacity;
    bool frozen;
    /* The static built-in set (amark_syntax_set_core): never freed. */
    bool is_static;
    /* The tables built by freezing (empty before): the types of the built-in
     * extension (identifier i + 1 is builtin_types[i]), the types of the other
     * extensions (identifier AMARK_NODE_DYNAMIC_FIRST + i is dynamic_types[i]),
     * the names of all types sorted by name, and the slots sorted by kind. */
    amark_registry_type_t *builtin_types;
    size_t builtin_type_count;
    amark_registry_type_t *dynamic_types;
    size_t dynamic_type_count;
    amark_registry_name_t *names;
    size_t name_count;
    amark_registry_slot_t *slots;
    size_t slot_count;
};

/* Whether a node of type parent can have a child of type child in the frozen set
 * set: both types exist, child is not the document, parent accepts the content
 * class of child, and the can_contain function of parent, if any, accepts child
 * (it receives self, the would-be parent node, which may be NULL). */
bool amark_registry_can_contain(const amark_syntax_set_t *set, const amark_node_t *self,
                                amark_node_type_t parent, amark_node_type_t child);

/* ---- BuiltinTypes (builtin_types.c) ---------------------------------------- */

/* The built-in extension "amark.core" with the 19 built-in node types, in the
 * order of their identifiers (static; not freed). */
const amark_extension_t *amark_tree_core_extension(void);

/* Checks that set can be used by a document: AMARK_ERROR_INVALID_ARGUMENT for
 * NULL, AMARK_ERROR_INVALID_STATE when it is not frozen (12.8). */
amark_error_t amark_syntax_set_require_frozen(const amark_syntax_set_t *set);

/* ---- NodeCore (node.c) ---------------------------------------------------- */

/*
 * The storage of one facet of a node. A new node has its facet array
 * zero-filled; what the fields mean (the state, the default values, the
 * derivation cache) belongs to Facets.
 */
typedef struct amark_tree_facet
{
    uint8_t state;
    int64_t number;
    char *text;
    size_t length;
    char *cache;
    size_t cache_length;
} amark_tree_facet_t;

struct amark_node
{
    amark_document_t *document;
    const amark_node_type_desc_t *desc;
    amark_node_type_t type;
    /* The tree. */
    amark_node_t *parent;
    amark_node_t *first_child;
    amark_node_t *last_child;
    amark_node_t *next;
    amark_node_t *prev;
    /* The ring of every node of the document (in the tree or not). */
    amark_node_t *ring_next;
    amark_node_t *ring_prev;
    void *user_data;
    /* desc->facet_count entries (NULL when there is none). */
    amark_tree_facet_t *facets;
    /* desc->data_size bytes of extension data (NULL when 0). */
    void *data;
};

/*
 * Allocates a node of type in document, calls the init function of the type and
 * adds the node to the ring of the document. AMARK_ERROR_INVALID_ARGUMENT for a
 * type the syntax set does not hold. On failure nothing is left allocated, the
 * document is unchanged and *out is NULL. The arguments are not NULL.
 */
amark_error_t amark_tree_node_new(amark_document_t *document, amark_node_type_t type,
                                  amark_node_t **out);

/* Releases one node: calls the deinit function of its type, removes it from the
 * ring of its document and frees it. Its tree links are not looked at. */
void amark_tree_node_release(amark_node_t *node);

/* Removes node from its parent and its siblings (only the links; node keeps its
 * children). Does nothing when node has no parent. */
void amark_tree_node_detach(amark_node_t *node);

/* ---- Document (document.c) ------------------------------------------------- */

/*
 * The complexity counters of a document (internal statistics): the tests check
 * the complexity of the operations with them. Each component adds the counters
 * it needs.
 */
typedef struct amark_tree_stats
{
    /* The nodes allocated and released over the life of the document. */
    size_t nodes_created;
    size_t nodes_released;
    /* The steps of the walks that free subtrees. */
    size_t release_steps;
} amark_tree_stats_t;

struct amark_document
{
    /* The frozen syntax set, borrowed or owned. */
    const amark_syntax_set_t *set;
    /* The same set when the document owns it, otherwise NULL. */
    amark_syntax_set_t *owned_set;
    /* The document node; also the anchor of the ring of every node. */
    amark_node_t *root;
    /* The number of nodes in the ring, the root included. */
    size_t node_count;
    amark_tree_stats_t stats;
};

#endif /* AMARK_TREE_TREE_H */
