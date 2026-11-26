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

#endif /* AMARK_TREE_TREE_H */
