/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/* Syntax sets: creation, changes, cloning, freezing, lookups, the slot index and
 * the static core set. */

#include "tree/syntax_set.h"

#include <amark/allocator.h>
#include <amark/error.h>
#include <amark/extension.h>
#include <amark/node.h>

#include "base/allocator_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The capacity of the extension array of a new set. */
#define INITIAL_CAPACITY 4u

/* The extensions of the static core set. */
static const amark_extension_t *const core_extensions[1] = { &amark_core_extension };

/* The name index of the static core set: the built-in type names, sorted with
 * strcmp. */
static const amark_syntax_name_entry_t core_name_index[AMARK_BUILTIN_TYPE_COUNT] = {
    { "block_quote", AMARK_NODE_BLOCK_QUOTE },
    { "code", AMARK_NODE_CODE },
    { "code_block", AMARK_NODE_CODE_BLOCK },
    { "document", AMARK_NODE_DOCUMENT },
    { "emph", AMARK_NODE_EMPH },
    { "heading", AMARK_NODE_HEADING },
    { "html_block", AMARK_NODE_HTML_BLOCK },
    { "html_inline", AMARK_NODE_HTML_INLINE },
    { "image", AMARK_NODE_IMAGE },
    { "item", AMARK_NODE_ITEM },
    { "linebreak", AMARK_NODE_LINEBREAK },
    { "link", AMARK_NODE_LINK },
    { "link_definition", AMARK_NODE_LINK_DEFINITION },
    { "list", AMARK_NODE_LIST },
    { "paragraph", AMARK_NODE_PARAGRAPH },
    { "softbreak", AMARK_NODE_SOFTBREAK },
    { "strong", AMARK_NODE_STRONG },
    { "text", AMARK_NODE_TEXT },
    { "thematic_break", AMARK_NODE_THEMATIC_BREAK }
};

/* The static, frozen set of amark_syntax_set_core(): const data only. */
static const struct amark_syntax_set core_set = {
    true, true, core_extensions, 1, 1, NULL, 0, core_name_index, AMARK_BUILTIN_TYPE_COUNT, NULL, 0
};

/* --- Checks of the descriptors ---------------------------------------------------- */

/* Whether name matches [a-z0-9][a-z0-9_.-]*. */
static bool is_valid_name(const char *name)
{
    const char *p;

    if ((name == NULL) || (name[0] == '\0'))
    {
        return false;
    }

    if (!(((name[0] >= 'a') && (name[0] <= 'z')) || ((name[0] >= '0') && (name[0] <= '9'))))
    {
        return false;
    }

    for (p = name + 1; *p != '\0'; p++)
    {
        char c = *p;

        if (!(((c >= 'a') && (c <= 'z')) || ((c >= '0') && (c <= '9')) || (c == '_')
              || (c == '.') || (c == '-')))
        {
            return false;
        }
    }

    return true;
}

/* Whether desc is a valid descriptor of an extension type. */
static bool is_valid_type_desc(const amark_node_type_desc_t *desc)
{
    if ((desc == NULL) || (desc->struct_size < sizeof(amark_node_type_desc_t)))
    {
        return false;
    }

    if (!is_valid_name(desc->name) || (strchr(desc->name, '.') == NULL))
    {
        return false;
    }

    switch (desc->level)
    {
    case AMARK_NODE_LEVEL_BLOCK:
    case AMARK_NODE_LEVEL_INLINE:
        break;
    default:
        return false;
    }

    switch (desc->literal_mode)
    {
    case AMARK_LITERAL_NONE:
    case AMARK_LITERAL_LINE:
    case AMARK_LITERAL_SPAN:
    case AMARK_LITERAL_LINES:
        break;
    default:
        return false;
    }

    return desc->data_size <= AMARK_NODE_TYPE_MAX_DATA_SIZE;
}

/* Whether extension is a valid descriptor of an extension (its name, its node types
 * and its slots). */
static bool is_valid_extension(const amark_extension_t *extension)
{
    size_t i;

    if ((extension == NULL) || (extension->struct_size < sizeof(amark_extension_t)))
    {
        return false;
    }

    if (!is_valid_name(extension->name))
    {
        return false;
    }

    if ((extension->node_type_count > 0) && (extension->node_types == NULL))
    {
        return false;
    }

    for (i = 0; i < extension->node_type_count; i++)
    {
        if (!is_valid_type_desc(extension->node_types[i]))
        {
            return false;
        }
    }

    if ((extension->slot_count > 0) && (extension->slots == NULL))
    {
        return false;
    }

    for (i = 0; i < extension->slot_count; i++)
    {
        const amark_slot_t *slot = extension->slots[i];

        if ((slot == NULL) || (slot->kind == 0) || (slot->struct_size < sizeof(amark_slot_t)))
        {
            return false;
        }
    }

    return true;
}

/* --- The list of extensions -------------------------------------------------------- */

/* Returns the index of the extension named name in set, or extension_count. */
static size_t find_index(const amark_syntax_set_t *set, const char *name)
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

/* Makes room for one more extension. On failure, set is unchanged. */
static amark_error_t ensure_room(amark_syntax_set_t *set)
{
    size_t new_capacity;
    size_t bytes;
    void *new_extensions;

    if (set->extension_count < set->extension_capacity)
    {
        return AMARK_ERROR_NONE;
    }

    if (!amark_size_mul(set->extension_capacity, 2, &new_capacity)
        || !amark_size_mul(new_capacity, sizeof(set->extensions[0]), &bytes))
    {
        return AMARK_ERROR_OVERFLOW;
    }

    new_extensions = amark_realloc((void *)set->extensions, bytes);
    if (new_extensions == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }

    set->extensions = new_extensions;
    set->extension_capacity = new_capacity;

    return AMARK_ERROR_NONE;
}

/*
 * The checks common to append, insert_before and replace: set is a set that is not
 * frozen, extension is valid, and its name is not in set (except at index
 * replaced, the extension it replaces; extension_count for none).
 */
static amark_error_t check_new_extension(const amark_syntax_set_t *set,
                                         const amark_extension_t *extension, size_t replaced)
{
    size_t found;

    if (set == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    if (set->frozen)
    {
        return AMARK_ERROR_INVALID_STATE;
    }

    if (!is_valid_extension(extension))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    found = find_index(set, extension->name);
    if ((found != set->extension_count) && (found != replaced))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    return AMARK_ERROR_NONE;
}

/* Inserts extension at index (0 < index <= extension_count) of set. */
static amark_error_t insert_at(amark_syntax_set_t *set, size_t index,
                               const amark_extension_t *extension)
{
    const amark_extension_t **extensions;
    amark_error_t err;

    err = ensure_room(set);
    if (err != AMARK_ERROR_NONE)
    {
        return err;
    }

    extensions = (const amark_extension_t **)set->extensions;
    memmove(&extensions[index + 1], &extensions[index],
            (set->extension_count - index) * sizeof(extensions[0]));
    extensions[index] = extension;
    set->extension_count++;

    return AMARK_ERROR_NONE;
}

/* Creates a heap set, not frozen, holding a copy of count extensions. */
static amark_error_t create_with(const amark_extension_t *const *extensions, size_t count,
                                 amark_syntax_set_t **out_set)
{
    amark_syntax_set_t *set;
    size_t capacity = (count > INITIAL_CAPACITY) ? count : INITIAL_CAPACITY;
    size_t bytes;
    const amark_extension_t **copy;

    if (!amark_size_mul(capacity, sizeof(extensions[0]), &bytes))
    {
        return AMARK_ERROR_OVERFLOW;
    }

    set = amark_calloc(1, sizeof(*set));
    if (set == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }

    copy = amark_malloc(bytes);
    if (copy == NULL)
    {
        amark_free(set);
        return AMARK_ERROR_OUT_OF_MEMORY;
    }

    memcpy(copy, extensions, count * sizeof(extensions[0]));
    set->frozen = false;
    set->is_static = false;
    set->extensions = copy;
    set->extension_count = count;
    set->extension_capacity = capacity;
    *out_set = set;

    return AMARK_ERROR_NONE;
}

amark_error_t amark_syntax_set_create(amark_syntax_set_t **out_set)
{
    if (out_set == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    *out_set = NULL;

    return create_with(core_extensions, 1, out_set);
}

amark_error_t amark_syntax_set_clone(const amark_syntax_set_t *set, amark_syntax_set_t **out_set)
{
    if (out_set == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    *out_set = NULL;
    if (set == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    return create_with(set->extensions, set->extension_count, out_set);
}

/* Frees what freezing built (the set is then not frozen). */
static void free_indexes(amark_syntax_set_t *set)
{
    amark_free((void *)set->dynamic_types);
    amark_free((void *)set->name_index);
    amark_free((void *)set->slot_index);
    set->dynamic_types = NULL;
    set->dynamic_type_count = 0;
    set->name_index = NULL;
    set->name_index_count = 0;
    set->slot_index = NULL;
    set->slot_index_count = 0;
}

void amark_syntax_set_destroy(amark_syntax_set_t *set)
{
    if ((set == NULL) || set->is_static)
    {
        return;
    }

    free_indexes(set);
    amark_free((void *)set->extensions);
    amark_free(set);
}

const amark_syntax_set_t *amark_syntax_set_core(void)
{
    return &core_set;
}

amark_error_t amark_syntax_set_append(amark_syntax_set_t *set, const amark_extension_t *extension)
{
    amark_error_t err;

    err = check_new_extension(set, extension, (set != NULL) ? set->extension_count : 0);
    if (err != AMARK_ERROR_NONE)
    {
        return err;
    }

    return insert_at(set, set->extension_count, extension);
}

amark_error_t amark_syntax_set_insert_before(amark_syntax_set_t *set, const char *anchor_name,
                                             const amark_extension_t *extension)
{
    amark_error_t err;
    size_t index;

    err = check_new_extension(set, extension, (set != NULL) ? set->extension_count : 0);
    if (err != AMARK_ERROR_NONE)
    {
        return err;
    }

    if (anchor_name == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    /* Nothing goes before amark.core (index 0). */
    index = find_index(set, anchor_name);
    if ((index == 0) || (index == set->extension_count))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    return insert_at(set, index, extension);
}

amark_error_t amark_syntax_set_remove(amark_syntax_set_t *set, const char *name)
{
    const amark_extension_t **extensions;
    size_t index;

    if (set == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    if (set->frozen)
    {
        return AMARK_ERROR_INVALID_STATE;
    }

    if (name == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    /* amark.core (index 0) cannot be removed. */
    index = find_index(set, name);
    if ((index == 0) || (index == set->extension_count))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    extensions = (const amark_extension_t **)set->extensions;
    memmove(&extensions[index], &extensions[index + 1],
            (set->extension_count - index - 1) * sizeof(extensions[0]));
    set->extension_count--;

    return AMARK_ERROR_NONE;
}

amark_error_t amark_syntax_set_replace(amark_syntax_set_t *set, const char *name,
                                       const amark_extension_t *extension)
{
    amark_error_t err;
    size_t index;

    if (set == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    if (set->frozen)
    {
        return AMARK_ERROR_INVALID_STATE;
    }

    if (name == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    /* amark.core (index 0) cannot be replaced. */
    index = find_index(set, name);
    if ((index == 0) || (index == set->extension_count))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    err = check_new_extension(set, extension, index);
    if (err != AMARK_ERROR_NONE)
    {
        return err;
    }

    ((const amark_extension_t **)set->extensions)[index] = extension;

    return AMARK_ERROR_NONE;
}

/* --- Freezing ------------------------------------------------------------------------ */

static int compare_name_entries(const void *a, const void *b)
{
    const amark_syntax_name_entry_t *x = a;
    const amark_syntax_name_entry_t *y = b;

    return strcmp(x->name, y->name);
}

static int compare_slot_entries(const void *a, const void *b)
{
    const amark_syntax_slot_entry_t *x = a;
    const amark_syntax_slot_entry_t *y = b;

    if (x->slot->kind != y->slot->kind)
    {
        return (x->slot->kind < y->slot->kind) ? -1 : 1;
    }

    if (x->order != y->order)
    {
        return (x->order < y->order) ? -1 : 1;
    }

    return 0;
}

/* Allocates count elements of size bytes in *out (NULL for 0 elements). */
static amark_error_t allocate_array(size_t count, size_t size, void **out)
{
    size_t bytes;

    *out = NULL;
    if (count == 0)
    {
        return AMARK_ERROR_NONE;
    }

    if (!amark_size_mul(count, size, &bytes))
    {
        return AMARK_ERROR_OVERFLOW;
    }

    *out = amark_malloc(bytes);
    if (*out == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }

    return AMARK_ERROR_NONE;
}

amark_error_t amark_syntax_set_freeze(amark_syntax_set_t *set)
{
    size_t type_count = 0;
    size_t slot_count = 0;
    size_t name_count;
    size_t i;
    size_t j;
    size_t n;
    void *types_memory = NULL;
    void *names_memory = NULL;
    void *slots_memory = NULL;
    const amark_node_type_desc_t **types;
    amark_syntax_name_entry_t *names;
    amark_syntax_slot_entry_t *slots;
    amark_error_t err;

    if (set == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    if (set->frozen)
    {
        return AMARK_ERROR_INVALID_STATE;
    }

    /* Count the extension types (amark.core, index 0, holds the built-in ones) and
     * the slots; the identifiers must fit in amark_node_type_t. */
    for (i = 1; i < set->extension_count; i++)
    {
        if (!amark_size_add(type_count, set->extensions[i]->node_type_count, &type_count))
        {
            return AMARK_ERROR_OVERFLOW;
        }
    }

    for (i = 0; i < set->extension_count; i++)
    {
        if (!amark_size_add(slot_count, set->extensions[i]->slot_count, &slot_count))
        {
            return AMARK_ERROR_OVERFLOW;
        }
    }

    if (type_count > (size_t)(UINT32_MAX - AMARK_NODE_DYNAMIC_FIRST))
    {
        return AMARK_ERROR_OVERFLOW;
    }

    if (!amark_size_add(type_count, AMARK_BUILTIN_TYPE_COUNT, &name_count))
    {
        return AMARK_ERROR_OVERFLOW;
    }

    /* Allocate everything before changing set. */
    err = allocate_array(type_count, sizeof(*types), &types_memory);
    if (err == AMARK_ERROR_NONE)
    {
        err = allocate_array(name_count, sizeof(*names), &names_memory);
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = allocate_array(slot_count, sizeof(*slots), &slots_memory);
    }

    if (err != AMARK_ERROR_NONE)
    {
        amark_free(types_memory);
        amark_free(names_memory);
        amark_free(slots_memory);
        return err;
    }

    types = types_memory;
    names = names_memory;
    slots = slots_memory;

    /* The identifiers 256, 257, ... in the order of the set, and the name index. */
    memcpy(names, core_name_index, sizeof(core_name_index));
    n = 0;
    for (i = 1; i < set->extension_count; i++)
    {
        const amark_extension_t *extension = set->extensions[i];

        for (j = 0; j < extension->node_type_count; j++)
        {
            types[n] = extension->node_types[j];
            names[AMARK_BUILTIN_TYPE_COUNT + n].name = types[n]->name;
            names[AMARK_BUILTIN_TYPE_COUNT + n].type = AMARK_NODE_DYNAMIC_FIRST
                                                       + (amark_node_type_t)n;
            n++;
        }
    }

    qsort(names, name_count, sizeof(*names), compare_name_entries);
    for (i = 1; i < name_count; i++)
    {
        if (strcmp(names[i - 1].name, names[i].name) == 0)
        {
            amark_free(types_memory);
            amark_free(names_memory);
            amark_free(slots_memory);
            return AMARK_ERROR_INVALID_ARGUMENT;
        }
    }

    /* The slot index: by kind, then in the order of the set and of the slots. */
    n = 0;
    for (i = 0; i < set->extension_count; i++)
    {
        const amark_extension_t *extension = set->extensions[i];

        for (j = 0; j < extension->slot_count; j++)
        {
            slots[n].slot = extension->slots[j];
            slots[n].extension = extension;
            slots[n].order = n;
            n++;
        }
    }

    if (slot_count > 0)
    {
        qsort(slots, slot_count, sizeof(*slots), compare_slot_entries);
    }

    set->dynamic_types = types;
    set->dynamic_type_count = type_count;
    set->name_index = names;
    set->name_index_count = name_count;
    set->slot_index = slots;
    set->slot_index_count = slot_count;
    set->frozen = true;

    return AMARK_ERROR_NONE;
}

/* --- Queries ------------------------------------------------------------------------ */

bool amark_syntax_set_is_frozen(const amark_syntax_set_t *set)
{
    return (set != NULL) && set->frozen;
}

size_t amark_syntax_set_extension_count(const amark_syntax_set_t *set)
{
    return (set != NULL) ? set->extension_count : 0;
}

const amark_extension_t *amark_syntax_set_extension_at(const amark_syntax_set_t *set,
                                                       size_t index)
{
    if ((set == NULL) || (index >= set->extension_count))
    {
        return NULL;
    }

    return set->extensions[index];
}

const amark_extension_t *amark_syntax_set_find_extension(const amark_syntax_set_t *set,
                                                         const char *name)
{
    size_t index;

    if ((set == NULL) || (name == NULL))
    {
        return NULL;
    }

    index = find_index(set, name);

    return (index < set->extension_count) ? set->extensions[index] : NULL;
}

const amark_node_type_desc_t *amark_syntax_set_desc(const amark_syntax_set_t *set,
                                                    amark_node_type_t type)
{
    if ((type >= 1) && (type <= AMARK_BUILTIN_TYPE_COUNT))
    {
        return amark_builtin_types[type];
    }

    if ((type >= AMARK_NODE_DYNAMIC_FIRST)
        && ((size_t)(type - AMARK_NODE_DYNAMIC_FIRST) < set->dynamic_type_count))
    {
        return set->dynamic_types[type - AMARK_NODE_DYNAMIC_FIRST];
    }

    return NULL;
}

bool amark_syntax_set_find_node_type(const amark_syntax_set_t *set, const char *name,
                                     amark_node_type_t *out_type)
{
    size_t low = 0;
    size_t high;

    if (out_type != NULL)
    {
        *out_type = AMARK_NODE_NONE;
    }

    if ((set == NULL) || !set->frozen || (name == NULL))
    {
        return false;
    }

    /* Binary search of the sorted name index. */
    high = set->name_index_count;
    while (low < high)
    {
        size_t mid = low + ((high - low) / 2);
        int cmp = strcmp(name, set->name_index[mid].name);

        if (cmp == 0)
        {
            if (out_type != NULL)
            {
                *out_type = set->name_index[mid].type;
            }

            return true;
        }

        if (cmp < 0)
        {
            high = mid;
        }
        else
        {
            low = mid + 1;
        }
    }

    return false;
}

bool amark_syntax_set_node_type_of(const amark_syntax_set_t *set,
                                   const amark_node_type_desc_t *desc,
                                   amark_node_type_t *out_type)
{
    amark_node_type_t type = AMARK_NODE_NONE;
    size_t i;

    if (out_type != NULL)
    {
        *out_type = AMARK_NODE_NONE;
    }

    if ((set == NULL) || !set->frozen || (desc == NULL))
    {
        return false;
    }

    for (i = 1; (i <= AMARK_BUILTIN_TYPE_COUNT) && (type == AMARK_NODE_NONE); i++)
    {
        if (amark_builtin_types[i] == desc)
        {
            type = (amark_node_type_t)i;
        }
    }

    for (i = 0; (i < set->dynamic_type_count) && (type == AMARK_NODE_NONE); i++)
    {
        if (set->dynamic_types[i] == desc)
        {
            type = AMARK_NODE_DYNAMIC_FIRST + (amark_node_type_t)i;
        }
    }

    if (type == AMARK_NODE_NONE)
    {
        return false;
    }

    if (out_type != NULL)
    {
        *out_type = type;
    }

    return true;
}

const amark_node_type_desc_t *amark_syntax_set_node_type_desc(const amark_syntax_set_t *set,
                                                              amark_node_type_t type)
{
    if ((set == NULL) || !set->frozen)
    {
        return NULL;
    }

    return amark_syntax_set_desc(set, type);
}

/* Returns the first index of the slot index of set whose kind is not less than
 * kind. */
static size_t slot_lower_bound(const amark_syntax_set_t *set, uint32_t kind)
{
    size_t low = 0;
    size_t high = set->slot_index_count;

    while (low < high)
    {
        size_t mid = low + ((high - low) / 2);

        if (set->slot_index[mid].slot->kind < kind)
        {
            low = mid + 1;
        }
        else
        {
            high = mid;
        }
    }

    return low;
}

size_t amark_syntax_set_slot_count(const amark_syntax_set_t *set, uint32_t kind)
{
    size_t first;
    size_t end;

    if ((set == NULL) || !set->frozen || (kind == 0))
    {
        return 0;
    }

    first = slot_lower_bound(set, kind);
    end = first;
    while ((end < set->slot_index_count) && (set->slot_index[end].slot->kind == kind))
    {
        end++;
    }

    return end - first;
}

const amark_slot_t *amark_syntax_set_slot_at(const amark_syntax_set_t *set, uint32_t kind,
                                             size_t index,
                                             const amark_extension_t **out_extension)
{
    size_t first;
    const amark_syntax_slot_entry_t *entry;

    if (out_extension != NULL)
    {
        *out_extension = NULL;
    }

    if ((set == NULL) || !set->frozen || (kind == 0))
    {
        return NULL;
    }

    first = slot_lower_bound(set, kind);
    if ((index >= (set->slot_index_count - first))
        || (set->slot_index[first + index].slot->kind != kind))
    {
        return NULL;
    }

    entry = &set->slot_index[first + index];
    if (out_extension != NULL)
    {
        *out_extension = entry->extension;
    }

    return entry->slot;
}
