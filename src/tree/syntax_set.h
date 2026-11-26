/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The definition of amark_syntax_set_t, the lookup of a descriptor by identifier
 * that documents and nodes use, and the tables of the built-in node types.
 * Internal: not part of the public API and not exported.
 */
#ifndef AMARK_TREE_SYNTAX_SET_H
#define AMARK_TREE_SYNTAX_SET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <amark/extension.h>
#include <amark/node.h>

/* The number of built-in node types (AMARK_NODE_DOCUMENT to AMARK_NODE_IMAGE). */
#define AMARK_BUILTIN_TYPE_COUNT 19u

/* An entry of the name index of a frozen set: every type name, sorted by name. */
typedef struct amark_syntax_name_entry
{
    const char *name;
    amark_node_type_t type;
} amark_syntax_name_entry_t;

/* An entry of the slot index of a frozen set: sorted by kind, then in the order of
 * the set and, within an extension, in the order of its slots. */
typedef struct amark_syntax_slot_entry
{
    const amark_slot_t *slot;
    const amark_extension_t *extension;
} amark_syntax_slot_entry_t;

/*
 * A syntax set. The state goes one way, from editable to frozen
 * (amark_syntax_set_freeze). The arrays are read through pointers to const, so
 * that the static core set (is_static) can point to const tables; a set on the
 * heap owns its arrays.
 */
struct amark_syntax_set
{
    bool frozen;
    /* The static set of amark_syntax_set_core(): never freed, never owned by a
     * document. */
    bool is_static;
    /* The extensions in order; the first is amark_core_extension. */
    const amark_extension_t *const *extensions;
    size_t extension_count;
    size_t extension_capacity;
    /* After freezing: the descriptors of the extension types, the type
     * AMARK_NODE_DYNAMIC_FIRST + i at index i. */
    const amark_node_type_desc_t *const *dynamic_types;
    size_t dynamic_type_count;
    /* After freezing: the name index of every type (built-in and extension). */
    const amark_syntax_name_entry_t *name_index;
    size_t name_index_count;
    /* After freezing: the slot index. */
    const amark_syntax_slot_entry_t *slot_index;
    size_t slot_index_count;
};

/*
 * The descriptors of the built-in node types, indexed by identifier: index 0
 * (AMARK_NODE_NONE) is NULL, index i (1 to AMARK_BUILTIN_TYPE_COUNT) is the type i.
 * Defined in tree/builtin_types.c.
 */
extern const amark_node_type_desc_t *const amark_builtin_types[AMARK_BUILTIN_TYPE_COUNT + 1];

/* The amark.core extension: the built-in node types in identifier order, no slot.
 * Defined in tree/builtin_types.c. */
extern const amark_extension_t amark_core_extension;

/* Returns the descriptor of type in the frozen set, in constant time, or NULL for
 * an unknown type. */
const amark_node_type_desc_t *amark_syntax_set_desc(const amark_syntax_set_t *set,
                                                    amark_node_type_t type);

#endif /* AMARK_TREE_SYNTAX_SET_H */
