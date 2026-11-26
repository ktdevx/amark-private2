/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * Facets: the storage of the facets, getting, setting, unsetting and recording
 * them, the calls of the hooks, the dropping of the spelling of a facet and of
 * a node, and the setting of the inline text of a node.
 *
 * Each node holds one amark_tree_facet_t per facet of its type (tree.h): a
 * state (none, set, recorded), the number or the owned string, and the cache
 * of the derived string. The getter reads, in order, the value derived from
 * the spelling (derive.c: the tokens of a STRING facet, the content of the leaf
 * for a CONTENT facet), the value set or recorded, then the default value of a
 * value facet, then UNSET for a style. The setters check the value, return
 * early for the same value, normalize the multi-line values, prepare the new
 * value, call the on_change hook and commit only when it succeeds; the records
 * do the same without the hook and without the early return. An edit (a set,
 * or the unset of a style) drops the spelling of its facet: the tokens that
 * spell it are removed and the facet is marked in amark_node_t.dropped. A
 * CONTENT facet stores no value: setting or recording it gives the leaf new
 * free content. Dropping a spelling keeps the values: a value derived from
 * the dropped tokens becomes the set value, and laid content becomes free.
 */
#include "tree/tree.h"

#include <amark/allocator.h>
#include <amark/utf8.h>

#include <string.h>

/* What a setter does: an edit (amark_node_set_*) or a record
 * (amark_node_record_*). */
typedef enum store_mode
{
    STORE_SET,
    STORE_RECORD,
    /* The value of a new node without spelling (amark_node_set_inline_text):
     * checked and stored as set, without the hook and without dropping a
     * spelling. */
    STORE_FRESH
} store_mode_t;

/* The descriptor of facet of the type of node, or NULL when node is NULL or its
 * type has no such facet. */
static const amark_facet_desc_t *facet_desc(const amark_node_t *node, amark_facet_t facet)
{
    if (node == NULL)
    {
        return NULL;
    }
    return amark_node_type_facet(node->desc, facet);
}

/* ---- Getting ------------------------------------------------------------------- */

/*
 * The current value of facet (desc) of node, in the order of the getter
 * (design, Tree: Facets):
 *   1. the destination and the title of a reference link or image, resolved by
 *      the definition index (UNRESOLVED without a definition) (DefinitionIndex
 *      adds it here);
 *   2. an OVERRIDABLE facet without an override: the value derived from the
 *      layout (tight) (Verbatim adds it here);
 *   3. a STRING facet with tokens: the value derived from its tokens;
 *   4. a CONTENT facet: the value derived from the content of the leaf;
 *   5. the value set or recorded;
 *   6. the default value of a value facet, and UNSET for a style.
 * The steps 3 and 4 fill the cache of the derived value:
 * AMARK_ERROR_OUT_OF_MEMORY when it cannot be allocated.
 */
static amark_error_t facet_value(const amark_node_t *node, amark_facet_t facet,
                                 const amark_facet_desc_t *desc, amark_value_t *out)
{
    const amark_tree_facet_t *slot = &node->facets[facet - 1];
    bool derived = false;
    amark_error_t error;

    out->state = AMARK_VALUE_PRESENT;
    out->number = 0;
    out->text = NULL;
    out->length = 0;
    /* 3 and 4. */
    error = amark_tree_derive(node, facet, desc, &derived, &out->text, &out->length);
    if ((error != AMARK_ERROR_NONE) || derived)
    {
        return error;
    }
    /* 5. */
    if (slot->state != AMARK_TREE_FACET_NONE)
    {
        if (desc->kind == AMARK_FACET_STRING)
        {
            out->text = slot->text;
            out->length = slot->length;
        }
        else
        {
            out->number = slot->number;
        }
        return AMARK_ERROR_NONE;
    }
    /* 6. */
    if (desc->role == AMARK_FACET_STYLE)
    {
        out->state = AMARK_VALUE_UNSET;
        return AMARK_ERROR_NONE;
    }
    if (desc->kind == AMARK_FACET_STRING)
    {
        out->text = desc->default_text != NULL ? desc->default_text : "";
        out->length = strlen(out->text);
    }
    else
    {
        out->number = desc->default_number;
    }
    return AMARK_ERROR_NONE;
}

amark_error_t amark_node_get(const amark_node_t *node, amark_facet_t facet, amark_value_t *out)
{
    const amark_facet_desc_t *desc = facet_desc(node, facet);
    amark_value_t value;
    amark_error_t error;

    if (desc == NULL || out == NULL || out->struct_size < sizeof(amark_value_t))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    value.struct_size = out->struct_size;
    error = facet_value(node, facet, desc, &value);
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    *out = value;
    return AMARK_ERROR_NONE;
}

/* ---- Checking -------------------------------------------------------------------- */

/* Whether the bytes hold a line break (LF or CR). */
static bool has_line_break(const char *text, size_t length)
{
    return memchr(text, '\n', length) != NULL || memchr(text, '\r', length) != NULL;
}

/* Whether the bytes hold a blank line: a line of only spaces and tabs between
 * two line breaks (LF, CRLF or CR). The first and the last line are not
 * between two line breaks. */
static bool has_blank_line(const char *text, size_t length)
{
    bool after_break = false;
    bool blank = false;
    size_t i;

    for (i = 0; i < length; i++)
    {
        char c = text[i];

        if (c == '\n' || c == '\r')
        {
            if (after_break && blank)
            {
                return true;
            }
            if (c == '\r' && i + 1 < length && text[i + 1] == '\n')
            {
                i++;
            }
            after_break = true;
            blank = true;
        }
        else if (c != ' ' && c != '\t')
        {
            blank = false;
        }
    }
    return false;
}

/* Checks number against the kind and the range of an INT, ENUM or BOOL facet. */
static bool number_in_range(const amark_facet_desc_t *desc, int64_t number)
{
    switch (desc->kind)
    {
    case AMARK_FACET_INT:
        return number >= desc->int_min && number <= desc->int_max;
    case AMARK_FACET_ENUM:
        return number >= 1 && (uint64_t)number <= (uint64_t)desc->enum_count;
    case AMARK_FACET_BOOL:
        return number == 0 || number == 1;
    case AMARK_FACET_STRING:
    default:
        return false;
    }
}

/* Checks the bytes of a STRING facet against its flags. */
static bool string_allowed(const amark_facet_desc_t *desc, const char *text, size_t length)
{
    if (memchr(text, '\0', length) != NULL || !amark_utf8_validate(text, length, NULL))
    {
        return false;
    }
    if ((desc->flags & AMARK_FACET_NO_LINE_BREAK) != 0 && has_line_break(text, length))
    {
        return false;
    }
    if ((desc->flags & AMARK_FACET_NO_BLANK_LINE) != 0 && has_blank_line(text, length))
    {
        return false;
    }
    return (desc->flags & AMARK_FACET_NONEMPTY) == 0 || length > 0;
}

/*
 * Step 1 of the setters and the records: checks value (PRESENT; a number, or a
 * non-NULL text and its length) for facet (desc) of node: the kind, the range,
 * the enumeration and the flags, then the validate function of the facet.
 */
static amark_error_t check_value(const amark_node_t *node, amark_facet_t facet,
                                 const amark_facet_desc_t *desc, const amark_value_t *value)
{
    if (desc->kind == AMARK_FACET_STRING)
    {
        if (value->text == NULL || !string_allowed(desc, value->text, value->length))
        {
            return AMARK_ERROR_INVALID_ARGUMENT;
        }
    }
    else if (value->text != NULL || !number_in_range(desc, value->number))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    if (desc->validate != NULL)
    {
        return desc->validate(node, facet, value, desc->user_data);
    }
    return AMARK_ERROR_NONE;
}

/* ---- Normalizing and comparing --------------------------------------------------- */

/*
 * Writes the bytes normalized for a LINES facet to out (when not NULL) and
 * returns their length: CRLF and CR become LF, and a value that is not empty
 * ends with LF. The length is at most length + 1.
 */
static size_t normalize_lines(const char *text, size_t length, char *out)
{
    size_t n = 0;
    size_t i;

    for (i = 0; i < length; i++)
    {
        char c = text[i];

        if (c == '\r')
        {
            if (i + 1 < length && text[i + 1] == '\n')
            {
                continue;
            }
            c = '\n';
        }
        if (out != NULL)
        {
            out[n] = c;
        }
        n++;
    }
    if (n > 0 && text[length - 1] != '\n' && text[length - 1] != '\r')
    {
        if (out != NULL)
        {
            out[n] = '\n';
        }
        n++;
    }
    return n;
}

/* Whether the bytes text, normalized as a set value of desc would be, equal
 * the other bytes. */
static bool same_string(const amark_facet_desc_t *desc, const char *text, size_t length,
                        const char *other, size_t other_length)
{
    size_t n = 0;
    size_t i;

    if ((desc->flags & AMARK_FACET_LINES) == 0)
    {
        return length == other_length && (length == 0 || memcmp(text, other, length) == 0);
    }
    /* The same walk as normalize_lines, comparing instead of writing. */
    for (i = 0; i < length; i++)
    {
        char c = text[i];

        if (c == '\r')
        {
            if (i + 1 < length && text[i + 1] == '\n')
            {
                continue;
            }
            c = '\n';
        }
        if (n >= other_length || other[n] != c)
        {
            return false;
        }
        n++;
    }
    if (n > 0 && text[length - 1] != '\n' && text[length - 1] != '\r')
    {
        if (n >= other_length || other[n] != '\n')
        {
            return false;
        }
        n++;
    }
    return n == other_length;
}

/*
 * Step 2 of the setters: *same tells whether value equals the current value of
 * facet (6.9). For an OVERRIDABLE facet only an override is compared: setting
 * the derived value makes it an override (6.11). AMARK_ERROR_OUT_OF_MEMORY when
 * the current value cannot be derived.
 */
static amark_error_t is_current_value(const amark_node_t *node, amark_facet_t facet,
                                      const amark_facet_desc_t *desc,
                                      const amark_value_t *value, bool *same)
{
    amark_value_t current;
    amark_error_t error;

    *same = false;
    if ((desc->flags & AMARK_FACET_OVERRIDABLE) != 0 &&
        node->facets[facet - 1].state == AMARK_TREE_FACET_NONE)
    {
        return AMARK_ERROR_NONE;
    }
    current.struct_size = sizeof(current);
    error = facet_value(node, facet, desc, &current);
    if (error != AMARK_ERROR_NONE || current.state != AMARK_VALUE_PRESENT)
    {
        return error;
    }
    if (desc->kind == AMARK_FACET_STRING)
    {
        *same = same_string(desc, value->text, value->length, current.text, current.length);
    }
    else
    {
        *same = value->number == current.number;
    }
    return AMARK_ERROR_NONE;
}

/* ---- Dropping the spelling ------------------------------------------------------- */

/* Removes the tokens of node that spell facet and marks the spelling of facet
 * as dropped (6.7, 6.8). Allocates nothing and does not notify. */
static void drop_facet_spelling(amark_node_t *node, amark_facet_t facet)
{
    amark_tree_remove_facet_tokens(node, facet);
    node->dropped |= 1u << facet;
}

/* Whether a token of node spells facet. */
static bool is_spelled(const amark_node_t *node, amark_facet_t facet)
{
    return amark_tree_layout_spells(node, facet) || amark_tree_content_spells(node, facet);
}

/* An owned copy of the length bytes at text, terminated by NUL, or NULL when
 * it cannot be allocated. */
static char *copy_text(const char *text, size_t length)
{
    char *copy;

    if (length >= SIZE_MAX)
    {
        return NULL;
    }
    copy = (char *)amark_malloc(length + 1);
    if (copy != NULL)
    {
        if (length > 0)
        {
            memcpy(copy, text, length);
        }
        copy[length] = '\0';
    }
    return copy;
}

/*
 * Prepares in *out the free content that keeps the value of the CONTENT facet
 * (desc) of leaf when its spelling is dropped, or NULL when the content needs
 * no change: only laid content, or content with tokens of the leaf on it
 * (whose replaced ranges are part of the value), is replaced, by the value as
 * set (the replaced bytes become U+FFFD).
 */
static amark_error_t prepare_content_value(const amark_node_t *leaf, amark_facet_t facet,
                                           const amark_facet_desc_t *desc,
                                           amark_tree_content_t **out)
{
    const amark_tree_content_t *content = leaf->content;
    const char *text = NULL;
    size_t length = 0;
    bool derived = false;
    amark_error_t error;

    *out = NULL;
    if ((content == NULL) ||
        ((content->state != AMARK_CONTENT_LAID) &&
         ((leaf->content_tokens == NULL) || (leaf->content_tokens->base != content))))
    {
        return AMARK_ERROR_NONE;
    }
    error = amark_tree_derive(leaf, facet, desc, &derived, &text, &length);
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    return amark_tree_content_from_value(text, length, out);
}

/*
 * Prepares in *text and *length an owned copy of the value of the STRING facet
 * (desc) of node derived from its tokens, or NULL when no token spells it.
 */
static amark_error_t prepare_derived_copy(const amark_node_t *node, amark_facet_t facet,
                                          const amark_facet_desc_t *desc, char **text,
                                          size_t *length)
{
    const char *derived_text = NULL;
    size_t derived_length = 0;
    bool derived = false;
    amark_error_t error;

    *text = NULL;
    *length = 0;
    if ((desc->kind != AMARK_FACET_STRING) || ((desc->flags & AMARK_FACET_CONTENT) != 0) ||
        !is_spelled(node, facet))
    {
        return AMARK_ERROR_NONE;
    }
    error = amark_tree_derive(node, facet, desc, &derived, &derived_text, &derived_length);
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    *text = copy_text(derived_text, derived_length);
    if (*text == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    *length = derived_length;
    return AMARK_ERROR_NONE;
}

/* ---- Storing ----------------------------------------------------------------------- */

/*
 * Replaces the stored value of facet with the prepared one (text owned, or
 * NULL for a number) in state, and notifies the change. Allocates nothing.
 */
static void commit(amark_node_t *node, amark_facet_t facet, uint8_t state, int64_t number,
                   char *text, size_t length)
{
    amark_tree_facet_t *slot = &node->facets[facet - 1];

    amark_free(slot->text);
    slot->state = state;
    slot->number = number;
    slot->text = text;
    slot->length = length;
    amark_tree_changed(node, AMARK_TREE_CHANGE_RECORD);
}

/*
 * The setters and the records. value holds the number, or the text (non-NULL)
 * and its length, as given by the caller; is_string tells which function was
 * called.
 */
static amark_error_t store(amark_node_t *node, amark_facet_t facet, bool is_string,
                           const amark_value_t *value, store_mode_t mode)
{
    const amark_facet_desc_t *desc = facet_desc(node, facet);
    amark_value_t prepared;
    amark_tree_content_t *content = NULL;
    char *text = NULL;
    bool is_content;
    bool same = false;
    amark_error_t error;

    if (desc == NULL || is_string != (desc->kind == AMARK_FACET_STRING))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    /* The value of a CONTENT facet is the content of a leaf. */
    is_content = (desc->flags & AMARK_FACET_CONTENT) != 0;
    if (is_content && (node->desc->level != AMARK_NODE_LEVEL_BLOCK ||
                       (node->desc->flags & AMARK_NODE_TYPE_CONTENT) == 0))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    /* 1. Check. */
    error = check_value(node, facet, desc, value);
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    /* 2. The same value changes nothing (an edit only: a record always stores
     *    its value and its state). */
    if (mode == STORE_SET)
    {
        error = is_current_value(node, facet, desc, value, &same);
        if (error != AMARK_ERROR_NONE || same)
        {
            return error;
        }
    }
    /* 3. Normalize and 4. prepare the new value. */
    prepared = *value;
    if (is_string)
    {
        bool lines = (desc->flags & AMARK_FACET_LINES) != 0;
        size_t length = lines ? normalize_lines(value->text, value->length, NULL) : value->length;

        if (length >= SIZE_MAX)
        {
            return AMARK_ERROR_OVERFLOW;
        }
        text = (char *)amark_malloc(length + 1);
        if (text == NULL)
        {
            return AMARK_ERROR_OUT_OF_MEMORY;
        }
        if (lines)
        {
            (void)normalize_lines(value->text, value->length, text);
        }
        else if (length > 0)
        {
            memcpy(text, value->text, length);
        }
        text[length] = '\0';
        prepared.text = text;
        prepared.length = length;
        if (is_content)
        {
            error = amark_tree_content_from_value(text, length, &content);
            if (error != AMARK_ERROR_NONE)
            {
                amark_free(text);
                return error;
            }
        }
    }
    if (mode == STORE_SET && desc->on_change != NULL)
    {
        /* The node still holds the old value while the hook runs. */
        error = desc->on_change(node, facet, &prepared, desc->user_data);
        if (error != AMARK_ERROR_NONE)
        {
            /* 5. The hook failed: discard the prepared value. */
            amark_tree_content_release(content, true);
            amark_free(text);
            return error;
        }
    }
    /* 5. Commit. An edit of a value or a style also drops the spelling of that
     *    facet (6.7, 6.8), as amark_node_discard_facet_spelling does: its
     *    tokens are removed and the facet is marked as dropped; the other
     *    facets and the other nodes keep theirs. A record keeps the spelling
     *    (6.14). A CONTENT facet takes the new content: the facet stores
     *    nothing, and no token is put on the new content (8.7). */
    if (mode == STORE_SET)
    {
        drop_facet_spelling(node, facet);
    }
    if (is_content)
    {
        amark_free(text);
        amark_tree_leaf_take_content(node, content);
        return AMARK_ERROR_NONE;
    }
    commit(node, facet, mode == STORE_RECORD ? AMARK_TREE_FACET_RECORDED : AMARK_TREE_FACET_SET,
           is_string ? 0 : prepared.number, text, prepared.length);
    return AMARK_ERROR_NONE;
}

/* The value of a number for store. */
static amark_value_t number_value(int64_t number)
{
    amark_value_t value;

    value.struct_size = sizeof(value);
    value.state = AMARK_VALUE_PRESENT;
    value.number = number;
    value.text = NULL;
    value.length = 0;
    return value;
}

/* The value of a string for store: a NULL text with a length is refused by
 * the caller, and a NULL text without one is the empty string. */
static amark_value_t string_value(const char *text, size_t length)
{
    amark_value_t value;

    value.struct_size = sizeof(value);
    value.state = AMARK_VALUE_PRESENT;
    value.number = 0;
    value.text = text != NULL ? text : "";
    value.length = length;
    return value;
}

amark_error_t amark_node_set_int(amark_node_t *node, amark_facet_t facet, int64_t number)
{
    amark_value_t value = number_value(number);

    return store(node, facet, false, &value, STORE_SET);
}

amark_error_t amark_node_set_string(amark_node_t *node, amark_facet_t facet, const char *text,
                                    size_t length)
{
    amark_value_t value;

    if (text == NULL && length > 0)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    value = string_value(text, length);
    return store(node, facet, true, &value, STORE_SET);
}

amark_error_t amark_node_record_int(amark_node_t *node, amark_facet_t facet, int64_t number)
{
    amark_value_t value = number_value(number);

    return store(node, facet, false, &value, STORE_RECORD);
}

amark_error_t amark_node_record_string(amark_node_t *node, amark_facet_t facet,
                                       const char *text, size_t length)
{
    amark_value_t value;

    if (text == NULL && length > 0)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    value = string_value(text, length);
    return store(node, facet, true, &value, STORE_RECORD);
}

amark_error_t amark_node_unset(amark_node_t *node, amark_facet_t facet)
{
    const amark_facet_desc_t *desc = facet_desc(node, facet);

    if (desc == NULL ||
        (desc->role != AMARK_FACET_STYLE && (desc->flags & AMARK_FACET_OVERRIDABLE) == 0))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    if (desc->role != AMARK_FACET_STYLE)
    {
        /* An OVERRIDABLE value facet: the override goes. */
        if (node->facets[facet - 1].state != AMARK_TREE_FACET_NONE)
        {
            commit(node, facet, AMARK_TREE_FACET_NONE, 0, NULL, 0);
        }
        return AMARK_ERROR_NONE;
    }
    /* A style has a value when one is stored, or when its tokens derive one
     * (a STRING style). */
    if ((node->facets[facet - 1].state == AMARK_TREE_FACET_NONE) &&
        ((desc->kind != AMARK_FACET_STRING) || !is_spelled(node, facet)))
    {
        return AMARK_ERROR_NONE;
    }
    /* Unsetting a style also drops its spelling, as the commit of a setter. */
    drop_facet_spelling(node, facet);
    commit(node, facet, AMARK_TREE_FACET_NONE, 0, NULL, 0);
    return AMARK_ERROR_NONE;
}

/* ---- Dropping the spelling of a facet and of a node (6.13) ----------------------- */

amark_error_t amark_node_discard_facet_spelling(amark_node_t *node, amark_facet_t facet)
{
    const amark_facet_desc_t *desc = facet_desc(node, facet);
    amark_tree_content_t *content = NULL;
    char *text = NULL;
    size_t length = 0;
    amark_error_t error;

    if (desc == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    if ((desc->flags & AMARK_FACET_CONTENT) != 0)
    {
        /* The value of the content stays: laid content, or content with the
         * tokens of the leaf, becomes the value as set. */
        error = prepare_content_value(node, facet, desc, &content);
        if (error != AMARK_ERROR_NONE)
        {
            return error;
        }
    }
    else
    {
        /* A value derived from the tokens becomes the set value. */
        error = prepare_derived_copy(node, facet, desc, &text, &length);
        if (error != AMARK_ERROR_NONE)
        {
            return error;
        }
    }
    if ((content == NULL) && (text == NULL) && !is_spelled(node, facet) &&
        ((node->dropped & (1u << facet)) != 0))
    {
        /* Nothing left to drop. */
        return AMARK_ERROR_NONE;
    }

    /* Commit (no allocation from here on). */
    drop_facet_spelling(node, facet);
    if (content != NULL)
    {
        amark_tree_leaf_replace_content(node, content);
    }
    if (text != NULL)
    {
        commit(node, facet, AMARK_TREE_FACET_SET, 0, text, length);
    }
    amark_tree_changed(node, AMARK_TREE_CHANGE_RECORD);
    return AMARK_ERROR_NONE;
}

/*
 * The list whose OVERRIDABLE facets (tight) are derived from the blank lines
 * that dropping all of the spelling of node removes: node itself when it is a
 * list, its parent when node is an item directly under a list, else NULL.
 */
static amark_node_t *derived_list_of(amark_node_t *node)
{
    if (node->type == AMARK_NODE_LIST)
    {
        return node;
    }
    if ((node->type == AMARK_NODE_ITEM) && (node->parent != NULL) &&
        (node->parent->type == AMARK_NODE_LIST))
    {
        return node->parent;
    }
    return NULL;
}

amark_error_t amark_node_discard_spelling(amark_node_t *node)
{
    char *texts[AMARK_FACET_MAX];
    size_t lengths[AMARK_FACET_MAX];
    int64_t overrides[AMARK_FACET_MAX];
    bool overridden[AMARK_FACET_MAX];
    amark_tree_content_t *content = NULL;
    amark_facet_t content_facet = AMARK_FACET_NONE;
    amark_node_t *list;
    size_t count;
    size_t i;
    uint32_t marks = AMARK_TREE_DROPPED_STRUCTURE;
    amark_error_t error = AMARK_ERROR_NONE;

    if (node == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    count = node->desc->facet_count;
    memset(texts, 0, sizeof(texts));
    memset(lengths, 0, sizeof(lengths));
    memset(overrides, 0, sizeof(overrides));
    memset(overridden, 0, sizeof(overridden));

    /* 1. The values derived from the tokens become set values; the facets
     *    that have tokens get their dropped mark in step 4. */
    for (i = 0; (i < count) && (error == AMARK_ERROR_NONE); i++)
    {
        const amark_facet_desc_t *desc = &node->desc->facets[i];

        if (is_spelled(node, (amark_facet_t)(i + 1)))
        {
            marks |= 1u << (i + 1);
        }
        if ((desc->flags & AMARK_FACET_CONTENT) != 0)
        {
            if (content_facet == AMARK_FACET_NONE)
            {
                content_facet = (amark_facet_t)(i + 1);
            }
            continue;
        }
        error = prepare_derived_copy(node, (amark_facet_t)(i + 1), desc, &texts[i], &lengths[i]);
    }
    /* 1. The values derived from the blank lines stay: the OVERRIDABLE facets
     *    without an override (tight) of the list of node (node itself, or the
     *    parent list of an item) take their current value as the override.
     *    They are numbers, read without allocation. */
    list = derived_list_of(node);
    if (list != NULL)
    {
        for (i = 0; (i < list->desc->facet_count) && (error == AMARK_ERROR_NONE); i++)
        {
            const amark_facet_desc_t *desc = &list->desc->facets[i];
            amark_value_t value;

            if (((desc->flags & AMARK_FACET_OVERRIDABLE) == 0) ||
                (desc->kind == AMARK_FACET_STRING) ||
                (list->facets[i].state != AMARK_TREE_FACET_NONE))
            {
                continue;
            }
            value.struct_size = sizeof(value);
            error = facet_value(list, (amark_facet_t)(i + 1), desc, &value);
            overrides[i] = value.number;
            overridden[i] = true;
        }
    }
    /* 3 (prepared before the tokens go). Laid content becomes free content with
     * the same bytes; the content of a CONTENT facet keeps its value. */
    if (error == AMARK_ERROR_NONE)
    {
        if (content_facet != AMARK_FACET_NONE)
        {
            error = prepare_content_value(node, content_facet,
                                          &node->desc->facets[content_facet - 1], &content);
        }
        else if ((node->content != NULL) && (node->content->state == AMARK_CONTENT_LAID))
        {
            error = amark_tree_content_free_copy(node->content, &content);
        }
    }
    if (error != AMARK_ERROR_NONE)
    {
        for (i = 0; i < count; i++)
        {
            amark_free(texts[i]);
        }
        return error;
    }

    /* Commit (no allocation from here on). */
    for (i = 0; i < count; i++)
    {
        if (texts[i] != NULL)
        {
            commit(node, (amark_facet_t)(i + 1), AMARK_TREE_FACET_SET, 0, texts[i], lengths[i]);
        }
    }
    for (i = 0; (list != NULL) && (i < list->desc->facet_count); i++)
    {
        if (overridden[i])
        {
            commit(list, (amark_facet_t)(i + 1), AMARK_TREE_FACET_SET, overrides[i], NULL, 0);
        }
    }
    /* 2. The own layout tokens, content tokens, margins and blank lines. */
    amark_tree_records_free(node->records);
    node->records = NULL;
    amark_tree_content_tokens_free(node->content_tokens);
    node->content_tokens = NULL;
    amark_tree_drop_derived(node, AMARK_FACET_NONE);
    /* 3. The tokens of the leaf and its descendants move to the new content
     *    only when its bytes are the same; otherwise they stay on the old
     *    object (8.8). */
    if (content != NULL)
    {
        amark_tree_leaf_replace_content(node, content);
    }
    /* 4. The structural spelling and the facets that had tokens are marked as
     *    dropped; the line range stays. */
    node->dropped |= marks;
    amark_tree_changed(node, AMARK_TREE_CHANGE_RECORD);
    return AMARK_ERROR_NONE;
}

/* ---- Setting the inline text (6.12) ------------------------------------------------ */

/* Destroys the chain of new nodes from first on (linked by next, without a
 * parent). */
static void destroy_chain(amark_node_t *first)
{
    while (first != NULL)
    {
        amark_node_t *next = first->next;

        first->next = NULL;
        first->prev = NULL;
        amark_node_destroy(first);
        first = next;
    }
}

/* Appends a new node of type to the chain [*first, *last]; *out receives it. */
static amark_error_t chain_new(amark_document_t *document, amark_node_type_t type,
                               amark_node_t **first, amark_node_t **last, amark_node_t **out)
{
    amark_error_t error = amark_tree_node_new(document, type, out);

    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    if (*last == NULL)
    {
        *first = *out;
    }
    else
    {
        (*last)->next = *out;
        (*out)->prev = *last;
    }
    *last = *out;
    return AMARK_ERROR_NONE;
}

amark_error_t amark_node_set_inline_text(amark_node_t *node, const char *text, size_t length)
{
    const amark_syntax_set_t *set;
    amark_node_t *first = NULL;
    amark_node_t *last = NULL;
    amark_node_t *created = NULL;
    size_t position = 0;
    amark_error_t error = AMARK_ERROR_NONE;

    if ((node == NULL) || ((text == NULL) && (length > 0)))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    if (text == NULL)
    {
        text = "";
    }
    set = node->document->set;
    if (!amark_registry_can_contain(set, node, node->type, AMARK_NODE_TEXT) ||
        (has_line_break(text, length) &&
         !amark_registry_can_contain(set, node, node->type, AMARK_NODE_SOFTBREAK)) ||
        (memchr(text, '\0', length) != NULL) || !amark_utf8_validate(text, length, NULL))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    /* The new children, built apart: a text for each nonempty line, a
     * softbreak for each line break. */
    while (error == AMARK_ERROR_NONE)
    {
        size_t end = position;

        while ((end < length) && (text[end] != '\n') && (text[end] != '\r'))
        {
            end++;
        }
        if (end > position)
        {
            amark_value_t value = string_value(text + position, end - position);

            error = chain_new(node->document, AMARK_NODE_TEXT, &first, &last, &created);
            if (error == AMARK_ERROR_NONE)
            {
                error = store(created, AMARK_TEXT_LITERAL, true, &value, STORE_FRESH);
            }
        }
        if ((error != AMARK_ERROR_NONE) || (end == length))
        {
            break;
        }
        position = end + (((text[end] == '\r') && (end + 1 < length) && (text[end + 1] == '\n'))
                              ? 2u
                              : 1u);
        error = chain_new(node->document, AMARK_NODE_SOFTBREAK, &first, &last, &created);
    }
    if (error != AMARK_ERROR_NONE)
    {
        destroy_chain(first);
        return error;
    }

    /* Commit: the old children go, the new ones take their place. Appending
     * cannot fail: the types were checked and a text or a softbreak holds no
     * link. */
    while (node->first_child != NULL)
    {
        amark_node_destroy(node->first_child);
    }
    while (first != NULL)
    {
        amark_node_t *next = first->next;

        first->next = NULL;
        first->prev = NULL;
        (void)amark_node_append_child(node, first);
        first = next;
    }
    amark_tree_changed(node, AMARK_TREE_CHANGE_STRUCTURE);
    return AMARK_ERROR_NONE;
}

