/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * Registry: the syntax sets, freezing, the identifiers, the lookups and the slots.
 *
 * A syntax set holds a growable array of borrowed extension pointers, with the
 * built-in extension first. Every change checks its arguments and allocates
 * before it changes the set, so a failure leaves the set as it was. Freezing
 * builds, in newly allocated arrays, the type tables, the name index (sorted by
 * name, which also finds duplicate names) and the slot index (sorted by kind,
 * then by the order of the set); it commits them only when all succeeded.
 */
#include "tree/tree.h"

#include <stdlib.h>
#include <string.h>

#include <amark/allocator.h>

/* ---- Checks of the descriptors --------------------------------------------- */

#define FACET_STRING_FLAGS                                                                   \
    (AMARK_FACET_RAW | AMARK_FACET_FOLD_LINE_BREAKS | AMARK_FACET_LINES |                   \
     AMARK_FACET_NO_LINE_BREAK | AMARK_FACET_NO_BLANK_LINE | AMARK_FACET_NONEMPTY |         \
     AMARK_FACET_CONTENT)
#define FACET_ALL_FLAGS (FACET_STRING_FLAGS | AMARK_FACET_OVERRIDABLE)
#define CONTENT_ALL (AMARK_CONTENT_BLOCK | AMARK_CONTENT_INLINE | AMARK_CONTENT_LIST_ITEM)
#define NODE_TYPE_ALL_FLAGS \
    (AMARK_NODE_TYPE_CONTENT | AMARK_NODE_TYPE_SHARE_LINES | AMARK_NODE_TYPE_MARGINS)

static bool is_lower(char c)
{
    return c >= 'a' && c <= 'z';
}

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

/* [a-z][a-z0-9_]* */
static bool is_facet_name(const char *name)
{
    const char *p;

    if (name == NULL || !is_lower(name[0]))
    {
        return false;
    }
    for (p = name + 1; *p != '\0'; p++)
    {
        if (!is_lower(*p) && !is_digit(*p) && *p != '_')
        {
            return false;
        }
    }
    return true;
}

/* [a-z0-9][a-z0-9_.-]*, the names of node types and extensions. */
static bool is_type_name(const char *name)
{
    const char *p;

    if (name == NULL || (!is_lower(name[0]) && !is_digit(name[0])))
    {
        return false;
    }
    for (p = name + 1; *p != '\0'; p++)
    {
        if (!is_lower(*p) && !is_digit(*p) && *p != '_' && *p != '.' && *p != '-')
        {
            return false;
        }
    }
    return true;
}

static bool is_single_bit(uint32_t bits)
{
    return bits != 0 && (bits & (bits - 1u)) == 0;
}

static bool check_facet(const amark_facet_desc_t *facet, const amark_node_type_desc_t *type)
{
    size_t i;

    if (facet->struct_size < sizeof(amark_facet_desc_t) || !is_facet_name(facet->name))
    {
        return false;
    }
    if (facet->role != AMARK_FACET_VALUE && facet->role != AMARK_FACET_STYLE)
    {
        return false;
    }
    if ((facet->flags & ~(uint32_t)FACET_ALL_FLAGS) != 0)
    {
        return false;
    }
    if (facet->role == AMARK_FACET_STYLE && (facet->flags & AMARK_FACET_OVERRIDABLE) != 0)
    {
        return false;
    }
    if (facet->kind != AMARK_FACET_STRING && (facet->flags & FACET_STRING_FLAGS) != 0)
    {
        return false;
    }
    if ((facet->flags & AMARK_FACET_CONTENT) != 0 &&
        (type->flags & AMARK_NODE_TYPE_CONTENT) == 0)
    {
        return false;
    }
    switch (facet->kind)
    {
    case AMARK_FACET_STRING:
        return true;
    case AMARK_FACET_INT:
        if (facet->int_min > facet->int_max)
        {
            return false;
        }
        return facet->role != AMARK_FACET_VALUE ||
               (facet->default_number >= facet->int_min &&
                facet->default_number <= facet->int_max);
    case AMARK_FACET_ENUM:
        if (facet->enum_names == NULL || facet->enum_count == 0)
        {
            return false;
        }
        for (i = 0; i < facet->enum_count; i++)
        {
            if (facet->enum_names[i] == NULL)
            {
                return false;
            }
        }
        return facet->role != AMARK_FACET_VALUE ||
               (facet->default_number >= 1 &&
                (uint64_t)facet->default_number <= (uint64_t)facet->enum_count);
    case AMARK_FACET_BOOL:
        return facet->role != AMARK_FACET_VALUE || facet->default_number == 0 ||
               facet->default_number == 1;
    default:
        return false;
    }
}

static bool check_node_type(const amark_node_type_desc_t *type)
{
    size_t i;
    size_t j;

    if (type->struct_size < sizeof(amark_node_type_desc_t) || !is_type_name(type->name))
    {
        return false;
    }
    if (type->level != AMARK_NODE_LEVEL_BLOCK && type->level != AMARK_NODE_LEVEL_INLINE)
    {
        return false;
    }
    if (!is_single_bit(type->provides) || (type->provides & ~(uint32_t)CONTENT_ALL) != 0 ||
        (type->accepts & ~(uint32_t)CONTENT_ALL) != 0)
    {
        return false;
    }
    if ((type->flags & ~(uint32_t)NODE_TYPE_ALL_FLAGS) != 0 ||
        (type->level == AMARK_NODE_LEVEL_INLINE && type->flags != 0))
    {
        return false;
    }
    if (type->facet_count > AMARK_FACET_MAX || (type->facet_count != 0 && type->facets == NULL) ||
        type->data_size > AMARK_NODE_TYPE_MAX_DATA_SIZE)
    {
        return false;
    }
    for (i = 0; i < type->facet_count; i++)
    {
        if (!check_facet(&type->facets[i], type))
        {
            return false;
        }
        for (j = 0; j < i; j++)
        {
            if (strcmp(type->facets[i].name, type->facets[j].name) == 0)
            {
                return false;
            }
        }
    }
    return true;
}

static bool check_extension(const amark_extension_t *extension)
{
    size_t i;

    if (extension->struct_size < sizeof(amark_extension_t) || !is_type_name(extension->name))
    {
        return false;
    }
    if ((extension->node_type_count != 0 && extension->node_types == NULL) ||
        (extension->slot_count != 0 && extension->slots == NULL))
    {
        return false;
    }
    for (i = 0; i < extension->node_type_count; i++)
    {
        if (extension->node_types[i] == NULL || !check_node_type(extension->node_types[i]))
        {
            return false;
        }
    }
    for (i = 0; i < extension->slot_count; i++)
    {
        const amark_slot_t *slot = extension->slots[i];

        if (slot == NULL || slot->struct_size < sizeof(amark_slot_t) || slot->kind == 0)
        {
            return false;
        }
    }
    return true;
}

/* ---- Facet lookups --------------------------------------------------------- */

amark_facet_t amark_node_type_find_facet(const amark_node_type_desc_t *desc, const char *name)
{
    size_t i;

    if (desc == NULL || name == NULL)
    {
        return AMARK_FACET_NONE;
    }
    for (i = 0; i < desc->facet_count && i < AMARK_FACET_MAX; i++)
    {
        if (strcmp(desc->facets[i].name, name) == 0)
        {
            return (amark_facet_t)(i + 1);
        }
    }
    return AMARK_FACET_NONE;
}

const amark_facet_desc_t *amark_node_type_facet(const amark_node_type_desc_t *desc,
                                                amark_facet_t facet)
{
    if (desc == NULL || facet == AMARK_FACET_NONE || facet > desc->facet_count ||
        facet > AMARK_FACET_MAX)
    {
        return NULL;
    }
    return &desc->facets[facet - 1];
}

/* ---- Creation and destruction ---------------------------------------------- */

/* Frees the tables built by freezing and makes them empty. */
static void free_tables(amark_syntax_set_t *set)
{
    amark_free(set->builtin_types);
    amark_free(set->dynamic_types);
    amark_free(set->names);
    amark_free(set->slots);
    set->builtin_types = NULL;
    set->builtin_type_count = 0;
    set->dynamic_types = NULL;
    set->dynamic_type_count = 0;
    set->names = NULL;
    set->name_count = 0;
    set->slots = NULL;
    set->slot_count = 0;
}

/* Allocates an unfrozen set with room for capacity extensions (at least 1) and
 * no extension. */
static amark_error_t new_set(size_t capacity, amark_syntax_set_t **out_set)
{
    amark_syntax_set_t *set;

    *out_set = NULL;
    if (capacity > SIZE_MAX / sizeof(const amark_extension_t *))
    {
        return AMARK_ERROR_OVERFLOW;
    }
    set = (amark_syntax_set_t *)amark_calloc(1, sizeof(*set));
    if (set == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    set->extensions =
        (const amark_extension_t **)amark_malloc(capacity * sizeof(const amark_extension_t *));
    if (set->extensions == NULL)
    {
        amark_free(set);
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    set->extension_capacity = capacity;
    *out_set = set;
    return AMARK_ERROR_NONE;
}

amark_error_t amark_syntax_set_create(amark_syntax_set_t **out_set)
{
    amark_error_t error;

    if (out_set == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    error = new_set(4, out_set);
    if (error == AMARK_ERROR_NONE)
    {
        (*out_set)->extensions[0] = amark_tree_core_extension();
        (*out_set)->extension_count = 1;
    }
    return error;
}

amark_error_t amark_syntax_set_clone(const amark_syntax_set_t *set, amark_syntax_set_t **out_set)
{
    amark_error_t error;

    if (out_set == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    *out_set = NULL;
    if (set == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    error = new_set(set->extension_count, out_set);
    if (error == AMARK_ERROR_NONE)
    {
        memcpy((void *)(*out_set)->extensions, (const void *)set->extensions,
               set->extension_count * sizeof(const amark_extension_t *));
        (*out_set)->extension_count = set->extension_count;
    }
    return error;
}

void amark_syntax_set_destroy(amark_syntax_set_t *set)
{
    if (set == NULL || set->is_static)
    {
        return;
    }
    free_tables(set);
    amark_free((void *)set->extensions);
    amark_free(set);
}

/* ---- Changes --------------------------------------------------------------- */

/* The index of the extension named name, or extension_count. */
static size_t index_of(const amark_syntax_set_t *set, const char *name)
{
    size_t i;

    for (i = 0; i < set->extension_count; i++)
    {
        if (strcmp(set->extensions[i]->name, name) == 0)
        {
            return i;
        }
    }
    return set->extension_count;
}

/* Checks a set to change and an extension to add: the state, the descriptor, and
 * that no extension other than the one at index skip has its name. */
static amark_error_t check_change(const amark_syntax_set_t *set, const amark_extension_t *extension,
                                  size_t skip)
{
    size_t found;

    if (set->frozen)
    {
        return AMARK_ERROR_INVALID_STATE;
    }
    if (extension == NULL || !check_extension(extension))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    found = index_of(set, extension->name);
    if (found != set->extension_count && found != skip)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    return AMARK_ERROR_NONE;
}

/* Inserts extension at index, growing the array if needed. */
static amark_error_t insert_at(amark_syntax_set_t *set, size_t index,
                               const amark_extension_t *extension)
{
    if (set->extension_count == set->extension_capacity)
    {
        const amark_extension_t **grown;
        size_t capacity = set->extension_capacity * 2;

        if (capacity < set->extension_capacity ||
            capacity > SIZE_MAX / sizeof(const amark_extension_t *))
        {
            return AMARK_ERROR_OVERFLOW;
        }
        grown = (const amark_extension_t **)amark_realloc(
            (void *)set->extensions, capacity * sizeof(const amark_extension_t *));
        if (grown == NULL)
        {
            return AMARK_ERROR_OUT_OF_MEMORY;
        }
        set->extensions = grown;
        set->extension_capacity = capacity;
    }
    memmove((void *)(set->extensions + index + 1), (const void *)(set->extensions + index),
            (set->extension_count - index) * sizeof(const amark_extension_t *));
    set->extensions[index] = extension;
    set->extension_count++;
    return AMARK_ERROR_NONE;
}

amark_error_t amark_syntax_set_append(amark_syntax_set_t *set, const amark_extension_t *extension)
{
    amark_error_t error;

    if (set == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    error = check_change(set, extension, SIZE_MAX);
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    return insert_at(set, set->extension_count, extension);
}

amark_error_t amark_syntax_set_insert_before(amark_syntax_set_t *set, const char *anchor_name,
                                             const amark_extension_t *extension)
{
    amark_error_t error;
    size_t index;

    if (set == NULL || anchor_name == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    error = check_change(set, extension, SIZE_MAX);
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    index = index_of(set, anchor_name);
    /* Nothing goes before the built-in extension. */
    if (index == set->extension_count || index == 0)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    return insert_at(set, index, extension);
}

amark_error_t amark_syntax_set_remove(amark_syntax_set_t *set, const char *name)
{
    size_t index;

    if (set == NULL || name == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    if (set->frozen)
    {
        return AMARK_ERROR_INVALID_STATE;
    }
    index = index_of(set, name);
    if (index == set->extension_count || index == 0)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    memmove((void *)(set->extensions + index), (const void *)(set->extensions + index + 1),
            (set->extension_count - index - 1) * sizeof(const amark_extension_t *));
    set->extension_count--;
    return AMARK_ERROR_NONE;
}

amark_error_t amark_syntax_set_replace(amark_syntax_set_t *set, const char *name,
                                       const amark_extension_t *extension)
{
    amark_error_t error;
    size_t index;

    if (set == NULL || name == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    if (set->frozen)
    {
        return AMARK_ERROR_INVALID_STATE;
    }
    index = index_of(set, name);
    if (index == set->extension_count || index == 0)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    error = check_change(set, extension, index);
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    set->extensions[index] = extension;
    return AMARK_ERROR_NONE;
}

/* ---- Freezing -------------------------------------------------------------- */

static int compare_names(const void *a, const void *b)
{
    const amark_registry_name_t *x = (const amark_registry_name_t *)a;
    const amark_registry_name_t *y = (const amark_registry_name_t *)b;

    return strcmp(x->name, y->name);
}

/* The slots are sorted by kind; within a kind, they keep the order of the set
 * (and of the slots of each extension): qsort is not stable, so each entry
 * carries its position in that order to break the ties. */
typedef struct slot_entry
{
    amark_registry_slot_t slot;
    size_t order;
} slot_entry_t;

static int compare_slots(const void *a, const void *b)
{
    const slot_entry_t *x = (const slot_entry_t *)a;
    const slot_entry_t *y = (const slot_entry_t *)b;

    if (x->slot.kind != y->slot.kind)
    {
        return x->slot.kind < y->slot.kind ? -1 : 1;
    }
    if (x->order != y->order)
    {
        return x->order < y->order ? -1 : 1;
    }
    return 0;
}

/* Allocates count elements of size bytes; a count of 0 gives NULL, which is not a
 * failure. */
static amark_error_t alloc_array(size_t count, size_t size, void **out)
{
    *out = NULL;
    if (count == 0)
    {
        return AMARK_ERROR_NONE;
    }
    if (count > SIZE_MAX / size)
    {
        return AMARK_ERROR_OVERFLOW;
    }
    *out = amark_malloc(count * size);
    return *out == NULL ? AMARK_ERROR_OUT_OF_MEMORY : AMARK_ERROR_NONE;
}

/* Adds two counts, reporting an overflow. */
static bool add_count(size_t *total, size_t count)
{
    if (count > SIZE_MAX - *total)
    {
        return false;
    }
    *total += count;
    return true;
}

amark_error_t amark_syntax_set_freeze(amark_syntax_set_t *set)
{
    const amark_extension_t *core;
    amark_registry_type_t *builtin = NULL;
    amark_registry_type_t *dynamic = NULL;
    amark_registry_name_t *names = NULL;
    amark_registry_slot_t *slots = NULL;
    slot_entry_t *entries = NULL;
    size_t dynamic_count = 0;
    size_t slot_count = 0;
    size_t name_count;
    size_t i;
    size_t j;
    size_t k;
    amark_error_t error;

    if (set == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    if (set->frozen)
    {
        return AMARK_ERROR_NONE;
    }
    core = set->extensions[0];
    if (core->node_type_count > AMARK_NODE_BUILTIN_LAST)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    if (!add_count(&slot_count, core->slot_count))
    {
        return AMARK_ERROR_OVERFLOW;
    }
    for (i = 1; i < set->extension_count; i++)
    {
        if (!add_count(&dynamic_count, set->extensions[i]->node_type_count) ||
            !add_count(&slot_count, set->extensions[i]->slot_count))
        {
            return AMARK_ERROR_OVERFLOW;
        }
    }
    if (dynamic_count > (size_t)(UINT32_MAX - AMARK_NODE_DYNAMIC_FIRST))
    {
        return AMARK_ERROR_LIMIT_EXCEEDED;
    }
    name_count = core->node_type_count + dynamic_count;

    error = alloc_array(core->node_type_count, sizeof(*builtin), (void **)&builtin);
    if (error == AMARK_ERROR_NONE)
    {
        error = alloc_array(dynamic_count, sizeof(*dynamic), (void **)&dynamic);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = alloc_array(name_count, sizeof(*names), (void **)&names);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = alloc_array(slot_count, sizeof(*slots), (void **)&slots);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = alloc_array(slot_count, sizeof(*entries), (void **)&entries);
    }
    if (error != AMARK_ERROR_NONE)
    {
        goto fail;
    }

    /* The types, their identifiers and the name index. */
    k = 0;
    for (i = 0; i < set->extension_count; i++)
    {
        const amark_extension_t *extension = set->extensions[i];

        for (j = 0; j < extension->node_type_count; j++)
        {
            amark_registry_type_t type;

            type.desc = extension->node_types[j];
            type.extension = extension;
            names[k].name = type.desc->name;
            if (i == 0)
            {
                builtin[j] = type;
                names[k].type = (amark_node_type_t)(j + 1);
            }
            else
            {
                size_t index = k - core->node_type_count;

                dynamic[index] = type;
                names[k].type = (amark_node_type_t)(AMARK_NODE_DYNAMIC_FIRST + index);
            }
            k++;
        }
    }
    if (name_count > 1)
    {
        qsort(names, name_count, sizeof(*names), compare_names);
    }
    for (i = 1; i < name_count; i++)
    {
        if (strcmp(names[i - 1].name, names[i].name) == 0)
        {
            error = AMARK_ERROR_INVALID_ARGUMENT;
            goto fail;
        }
    }

    /* The slot index. */
    k = 0;
    for (i = 0; i < set->extension_count; i++)
    {
        const amark_extension_t *extension = set->extensions[i];

        for (j = 0; j < extension->slot_count; j++)
        {
            entries[k].slot.kind = extension->slots[j]->kind;
            entries[k].slot.slot = extension->slots[j];
            entries[k].slot.extension = extension;
            entries[k].order = k;
            k++;
        }
    }
    if (slot_count > 1)
    {
        qsort(entries, slot_count, sizeof(*entries), compare_slots);
    }
    for (i = 0; i < slot_count; i++)
    {
        slots[i] = entries[i].slot;
    }
    amark_free(entries);

    set->builtin_types = builtin;
    set->builtin_type_count = core->node_type_count;
    set->dynamic_types = dynamic;
    set->dynamic_type_count = dynamic_count;
    set->names = names;
    set->name_count = name_count;
    set->slots = slots;
    set->slot_count = slot_count;
    set->frozen = true;
    return AMARK_ERROR_NONE;

fail:
    amark_free(builtin);
    amark_free(dynamic);
    amark_free(names);
    amark_free(slots);
    amark_free(entries);
    return error;
}

bool amark_syntax_set_is_frozen(const amark_syntax_set_t *set)
{
    return set != NULL && set->frozen;
}

amark_error_t amark_syntax_set_require_frozen(const amark_syntax_set_t *set)
{
    if (set == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    return set->frozen ? AMARK_ERROR_NONE : AMARK_ERROR_INVALID_STATE;
}

/* ---- Lookups --------------------------------------------------------------- */

size_t amark_syntax_set_extension_count(const amark_syntax_set_t *set)
{
    return set == NULL ? 0 : set->extension_count;
}

const amark_extension_t *amark_syntax_set_extension_at(const amark_syntax_set_t *set, size_t index)
{
    if (set == NULL || index >= set->extension_count)
    {
        return NULL;
    }
    return set->extensions[index];
}

const amark_extension_t *amark_syntax_set_find_extension(const amark_syntax_set_t *set,
                                                         const char *name)
{
    size_t index;

    if (set == NULL || name == NULL)
    {
        return NULL;
    }
    index = index_of(set, name);
    return index == set->extension_count ? NULL : set->extensions[index];
}

/* The entry of the name index for name, or NULL. Empty before freezing. */
static const amark_registry_name_t *find_name(const amark_syntax_set_t *set, const char *name)
{
    size_t low = 0;
    size_t high = set->name_count;

    while (low < high)
    {
        size_t middle = low + (high - low) / 2;
        int order = strcmp(name, set->names[middle].name);

        if (order == 0)
        {
            return &set->names[middle];
        }
        if (order < 0)
        {
            high = middle;
        }
        else
        {
            low = middle + 1;
        }
    }
    return NULL;
}

bool amark_syntax_set_find_node_type(const amark_syntax_set_t *set, const char *name,
                                     amark_node_type_t *out_type)
{
    const amark_registry_name_t *entry;

    if (out_type == NULL)
    {
        return false;
    }
    *out_type = AMARK_NODE_NONE;
    if (set == NULL || name == NULL)
    {
        return false;
    }
    entry = find_name(set, name);
    if (entry == NULL)
    {
        return false;
    }
    *out_type = entry->type;
    return true;
}

const amark_node_type_desc_t *amark_syntax_set_node_type_desc(const amark_syntax_set_t *set,
                                                              amark_node_type_t type)
{
    if (set == NULL || type == AMARK_NODE_NONE)
    {
        return NULL;
    }
    if (type <= AMARK_NODE_BUILTIN_LAST)
    {
        return type <= set->builtin_type_count ? set->builtin_types[type - 1].desc : NULL;
    }
    if (type < AMARK_NODE_DYNAMIC_FIRST || type - AMARK_NODE_DYNAMIC_FIRST >= set->dynamic_type_count)
    {
        return NULL;
    }
    return set->dynamic_types[type - AMARK_NODE_DYNAMIC_FIRST].desc;
}

bool amark_syntax_set_node_type_of(const amark_syntax_set_t *set,
                                   const amark_node_type_desc_t *desc, amark_node_type_t *out_type)
{
    amark_node_type_t type = AMARK_NODE_NONE;

    if (out_type == NULL)
    {
        return false;
    }
    *out_type = AMARK_NODE_NONE;
    if (desc == NULL || desc->name == NULL ||
        !amark_syntax_set_find_node_type(set, desc->name, &type) ||
        amark_syntax_set_node_type_desc(set, type) != desc)
    {
        return false;
    }
    *out_type = type;
    return true;
}

/* The index of the first slot of kind or a later kind in the slot index. */
static size_t lower_bound_kind(const amark_syntax_set_t *set, uint32_t kind)
{
    size_t low = 0;
    size_t high = set->slot_count;

    while (low < high)
    {
        size_t middle = low + (high - low) / 2;

        if (set->slots[middle].kind < kind)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    return low;
}

size_t amark_syntax_set_slot_count(const amark_syntax_set_t *set, uint32_t kind)
{
    size_t first;

    if (set == NULL || kind == 0)
    {
        return 0;
    }
    first = lower_bound_kind(set, kind);
    if (kind == UINT32_MAX)
    {
        return set->slot_count - first;
    }
    return lower_bound_kind(set, kind + 1u) - first;
}

const amark_slot_t *amark_syntax_set_slot_at(const amark_syntax_set_t *set, uint32_t kind,
                                             size_t index, const amark_extension_t **out_extension)
{
    size_t first;

    if (out_extension != NULL)
    {
        *out_extension = NULL;
    }
    if (index >= amark_syntax_set_slot_count(set, kind))
    {
        return NULL;
    }
    first = lower_bound_kind(set, kind);
    if (out_extension != NULL)
    {
        *out_extension = set->slots[first + index].extension;
    }
    return set->slots[first + index].slot;
}

/* ---- Containment ------------------------------------------------------------ */

bool amark_registry_can_contain(const amark_syntax_set_t *set, const amark_node_t *self,
                                amark_node_type_t parent, amark_node_type_t child)
{
    const amark_node_type_desc_t *parent_desc;
    const amark_node_type_desc_t *child_desc;

    if (child == AMARK_NODE_DOCUMENT)
    {
        return false;
    }
    parent_desc = amark_syntax_set_node_type_desc(set, parent);
    child_desc = amark_syntax_set_node_type_desc(set, child);
    if (parent_desc == NULL || child_desc == NULL ||
        (parent_desc->accepts & child_desc->provides) == 0)
    {
        return false;
    }
    return parent_desc->can_contain == NULL || parent_desc->can_contain(self, child);
}
