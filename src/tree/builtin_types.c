/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/* The descriptors of the 19 built-in node types (with their function tables) and
 * the amark.core extension. */

#include "tree/syntax_set.h"

#include <amark/allocator.h>
#include <amark/error.h>
#include <amark/extension.h>
#include <amark/node.h>

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/*
 * The function tables use only the public headers and the layouts of the type data
 * (tree/syntax_set.h), as the function tables of an extension would. The built-in
 * attributes are written by the dump itself (tree/dump.c), in the fixed order of
 * the design, so no built-in type has a dump function.
 */

/* --- The string attributes ------------------------------------------------------- */

/* The most string attributes of a type (link_definition). */
#define MAX_STRINGS 3u

/* Releases each string and sets it to "". */
static void release_strings(amark_builtin_string_t *const *strings, size_t count)
{
    size_t i;

    for (i = 0; i < count; i++)
    {
        amark_free(strings[i]->text);
        strings[i]->text = NULL;
        strings[i]->length = 0;
    }
}

/* Copies src into dst, which owns nothing; "" (length 0) stays NULL. */
static amark_error_t copy_string(amark_builtin_string_t *dst, const amark_builtin_string_t *src)
{
    dst->text = NULL;
    dst->length = 0;
    if (src->length == 0)
    {
        return AMARK_ERROR_NONE;
    }

    dst->text = amark_malloc(src->length + 1);
    if (dst->text == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }

    memcpy(dst->text, src->text, src->length);
    dst->text[src->length] = '\0';
    dst->length = src->length;

    return AMARK_ERROR_NONE;
}

/* Copies each string of src into dst, which owns nothing. On failure dst owns
 * nothing (all its strings are ""). */
static amark_error_t copy_strings(amark_builtin_string_t *const *dst,
                                  const amark_builtin_string_t *const *src, size_t count)
{
    amark_error_t err;
    size_t i;

    for (i = 0; i < count; i++)
    {
        dst[i]->text = NULL;
        dst[i]->length = 0;
    }

    for (i = 0; i < count; i++)
    {
        err = copy_string(dst[i], src[i]);
        if (err != AMARK_ERROR_NONE)
        {
            release_strings(dst, count);
            return err;
        }
    }

    return AMARK_ERROR_NONE;
}

/* Whether a and b hold the same bytes. */
static bool string_equal(const amark_builtin_string_t *a, const amark_builtin_string_t *b)
{
    if (a->length != b->length)
    {
        return false;
    }

    return (a->length == 0) || (memcmp(a->text, b->text, a->length) == 0);
}

/* Whether each string of a equals the string of b at the same place. */
static bool strings_equal(const amark_builtin_string_t *const *a,
                          const amark_builtin_string_t *const *b, size_t count)
{
    size_t i;

    for (i = 0; i < count; i++)
    {
        if (!string_equal(a[i], b[i]))
        {
            return false;
        }
    }

    return true;
}

/* Write in out the string attributes of the data of a type, in a fixed order, and
 * return their number; the const versions are for the source of a copy and for
 * the comparison. */
static size_t code_block_strings(amark_code_block_data_t *data,
                                 amark_builtin_string_t *out[MAX_STRINGS])
{
    out[0] = &data->info;
    return 1;
}

static size_t link_strings(amark_link_data_t *data, amark_builtin_string_t *out[MAX_STRINGS])
{
    out[0] = &data->destination;
    out[1] = &data->title;
    return 2;
}

static size_t link_definition_strings(amark_link_definition_data_t *data,
                                      amark_builtin_string_t *out[MAX_STRINGS])
{
    out[0] = &data->link.destination;
    out[1] = &data->link.title;
    out[2] = &data->label;
    return 3;
}

static size_t code_block_const_strings(const amark_code_block_data_t *data,
                                       const amark_builtin_string_t *out[MAX_STRINGS])
{
    out[0] = &data->info;
    return 1;
}

static size_t link_const_strings(const amark_link_data_t *data,
                                 const amark_builtin_string_t *out[MAX_STRINGS])
{
    out[0] = &data->destination;
    out[1] = &data->title;
    return 2;
}

static size_t link_definition_const_strings(const amark_link_definition_data_t *data,
                                            const amark_builtin_string_t *out[MAX_STRINGS])
{
    out[0] = &data->link.destination;
    out[1] = &data->link.title;
    out[2] = &data->label;
    return 3;
}

/* code_block */

static void code_block_deinit(amark_node_t *node, void *data)
{
    amark_builtin_string_t *strings[MAX_STRINGS];
    size_t count = code_block_strings(data, strings);

    (void)node;
    release_strings(strings, count);
}

static amark_error_t code_block_copy(amark_node_t *dst, void *dst_data, const amark_node_t *src,
                                     const void *src_data)
{
    amark_builtin_string_t *to[MAX_STRINGS];
    const amark_builtin_string_t *from[MAX_STRINGS];
    size_t count = code_block_strings(dst_data, to);

    (void)dst;
    (void)src;
    (void)code_block_const_strings(src_data, from);

    return copy_strings(to, from, count);
}

static bool code_block_equal(const amark_node_t *a, const void *a_data, const amark_node_t *b,
                             const void *b_data)
{
    const amark_builtin_string_t *x[MAX_STRINGS];
    const amark_builtin_string_t *y[MAX_STRINGS];
    size_t count = code_block_const_strings(a_data, x);

    (void)a;
    (void)b;
    (void)code_block_const_strings(b_data, y);

    return strings_equal(x, y, count);
}

/* link and image */

static void link_deinit(amark_node_t *node, void *data)
{
    amark_builtin_string_t *strings[MAX_STRINGS];
    size_t count = link_strings(data, strings);

    (void)node;
    release_strings(strings, count);
}

static amark_error_t link_copy(amark_node_t *dst, void *dst_data, const amark_node_t *src,
                               const void *src_data)
{
    amark_builtin_string_t *to[MAX_STRINGS];
    const amark_builtin_string_t *from[MAX_STRINGS];
    size_t count = link_strings(dst_data, to);

    (void)dst;
    (void)src;
    (void)link_const_strings(src_data, from);

    return copy_strings(to, from, count);
}

static bool link_equal(const amark_node_t *a, const void *a_data, const amark_node_t *b,
                       const void *b_data)
{
    const amark_builtin_string_t *x[MAX_STRINGS];
    const amark_builtin_string_t *y[MAX_STRINGS];
    size_t count = link_const_strings(a_data, x);

    (void)a;
    (void)b;
    (void)link_const_strings(b_data, y);

    return strings_equal(x, y, count);
}

/* link_definition */

static void link_definition_deinit(amark_node_t *node, void *data)
{
    amark_builtin_string_t *strings[MAX_STRINGS];
    size_t count = link_definition_strings(data, strings);

    (void)node;
    release_strings(strings, count);
}

static amark_error_t link_definition_copy(amark_node_t *dst, void *dst_data,
                                          const amark_node_t *src, const void *src_data)
{
    amark_builtin_string_t *to[MAX_STRINGS];
    const amark_builtin_string_t *from[MAX_STRINGS];
    size_t count = link_definition_strings(dst_data, to);

    (void)dst;
    (void)src;
    (void)link_definition_const_strings(src_data, from);

    return copy_strings(to, from, count);
}

static bool link_definition_equal(const amark_node_t *a, const void *a_data,
                                  const amark_node_t *b, const void *b_data)
{
    const amark_builtin_string_t *x[MAX_STRINGS];
    const amark_builtin_string_t *y[MAX_STRINGS];
    size_t count = link_definition_const_strings(a_data, x);

    (void)a;
    (void)b;
    (void)link_definition_const_strings(b_data, y);

    return strings_equal(x, y, count);
}

/* --- list and heading ---------------------------------------------------------- */

static amark_error_t list_init(amark_node_t *node, void *data)
{
    amark_list_data_t *list = data;

    (void)node;
    list->type = AMARK_LIST_BULLET;
    list->start = 1;
    list->tight = true;

    return AMARK_ERROR_NONE;
}

/* The start number counts only for an ordered list. */
static bool list_equal(const amark_node_t *a, const void *a_data, const amark_node_t *b,
                       const void *b_data)
{
    const amark_list_data_t *x = a_data;
    const amark_list_data_t *y = b_data;

    (void)a;
    (void)b;

    if ((x->type != y->type) || (x->tight != y->tight))
    {
        return false;
    }

    return (x->type != AMARK_LIST_ORDERED) || (x->start == y->start);
}

static amark_error_t heading_init(amark_node_t *node, void *data)
{
    amark_heading_data_t *heading = data;

    (void)node;
    heading->level = 1;

    return AMARK_ERROR_NONE;
}

/* --- code ------------------------------------------------------------------------ */

/* A code span is never empty. */
static amark_error_t code_validate_literal(const amark_node_t *node, const char *text,
                                           size_t length)
{
    (void)node;
    (void)text;

    return (length == 0) ? AMARK_ERROR_INVALID_ARGUMENT : AMARK_ERROR_NONE;
}

/* --- The descriptors --------------------------------------------------------------- */

#define BLOCK AMARK_NODE_LEVEL_BLOCK
#define INLINE AMARK_NODE_LEVEL_INLINE
#define C_BLOCK AMARK_CONTENT_BLOCK
#define C_INLINE AMARK_CONTENT_INLINE
#define C_ITEM AMARK_CONTENT_LIST_ITEM

/* A descriptor without type data or function. */
#define PLAIN_TYPE(var, type_name, level, provides, accepts, literal_mode) \
    static const amark_node_type_desc_t var = { \
        sizeof(amark_node_type_desc_t), type_name, level, provides, accepts, literal_mode, 0, \
        NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL \
    }

/* A descriptor with string attributes in its type data. */
#define STRING_TYPE(var, type_name, level, provides, accepts, literal_mode, data_type, prefix) \
    static const amark_node_type_desc_t var = { \
        sizeof(amark_node_type_desc_t), type_name, level, provides, accepts, literal_mode, \
        sizeof(data_type), NULL, NULL, NULL, prefix##_deinit, prefix##_copy, prefix##_equal, \
        NULL, NULL \
    }

PLAIN_TYPE(document_type, "document", BLOCK, 0, C_BLOCK, AMARK_LITERAL_NONE);
PLAIN_TYPE(block_quote_type, "block_quote", BLOCK, C_BLOCK, C_BLOCK, AMARK_LITERAL_NONE);

static const amark_node_type_desc_t list_type = {
    sizeof(amark_node_type_desc_t), "list", BLOCK, C_BLOCK, C_ITEM, AMARK_LITERAL_NONE,
    sizeof(amark_list_data_t), NULL, NULL, list_init, NULL, NULL, list_equal, NULL, NULL
};

PLAIN_TYPE(item_type, "item", BLOCK, C_ITEM, C_BLOCK, AMARK_LITERAL_NONE);
STRING_TYPE(code_block_type, "code_block", BLOCK, C_BLOCK, 0, AMARK_LITERAL_LINES,
            amark_code_block_data_t, code_block);
PLAIN_TYPE(html_block_type, "html_block", BLOCK, C_BLOCK, 0, AMARK_LITERAL_LINES);
PLAIN_TYPE(paragraph_type, "paragraph", BLOCK, C_BLOCK, C_INLINE, AMARK_LITERAL_NONE);

static const amark_node_type_desc_t heading_type = {
    sizeof(amark_node_type_desc_t), "heading", BLOCK, C_BLOCK, C_INLINE, AMARK_LITERAL_NONE,
    sizeof(amark_heading_data_t), NULL, NULL, heading_init, NULL, NULL, NULL, NULL, NULL
};

PLAIN_TYPE(thematic_break_type, "thematic_break", BLOCK, C_BLOCK, 0, AMARK_LITERAL_NONE);
STRING_TYPE(link_definition_type, "link_definition", BLOCK, C_BLOCK, 0, AMARK_LITERAL_NONE,
            amark_link_definition_data_t, link_definition);
PLAIN_TYPE(text_type, "text", INLINE, C_INLINE, 0, AMARK_LITERAL_LINE);
PLAIN_TYPE(softbreak_type, "softbreak", INLINE, C_INLINE, 0, AMARK_LITERAL_NONE);
PLAIN_TYPE(linebreak_type, "linebreak", INLINE, C_INLINE, 0, AMARK_LITERAL_NONE);

static const amark_node_type_desc_t code_type = {
    sizeof(amark_node_type_desc_t), "code", INLINE, C_INLINE, 0, AMARK_LITERAL_LINE, 0, NULL,
    code_validate_literal, NULL, NULL, NULL, NULL, NULL, NULL
};

PLAIN_TYPE(html_inline_type, "html_inline", INLINE, C_INLINE, 0, AMARK_LITERAL_SPAN);
PLAIN_TYPE(emph_type, "emph", INLINE, C_INLINE, C_INLINE, AMARK_LITERAL_NONE);
PLAIN_TYPE(strong_type, "strong", INLINE, C_INLINE, C_INLINE, AMARK_LITERAL_NONE);
STRING_TYPE(link_type, "link", INLINE, C_INLINE, C_INLINE, AMARK_LITERAL_NONE, amark_link_data_t,
            link);
STRING_TYPE(image_type, "image", INLINE, C_INLINE, C_INLINE, AMARK_LITERAL_NONE,
            amark_link_data_t, link);

const amark_node_type_desc_t *const amark_builtin_types[AMARK_BUILTIN_TYPE_COUNT + 1] = {
    NULL,
    &document_type,
    &block_quote_type,
    &list_type,
    &item_type,
    &code_block_type,
    &html_block_type,
    &paragraph_type,
    &heading_type,
    &thematic_break_type,
    &link_definition_type,
    &text_type,
    &softbreak_type,
    &linebreak_type,
    &code_type,
    &html_inline_type,
    &emph_type,
    &strong_type,
    &link_type,
    &image_type
};

const amark_extension_t amark_core_extension = {
    sizeof(amark_extension_t), "amark.core", &amark_builtin_types[1], AMARK_BUILTIN_TYPE_COUNT,
    NULL, 0, NULL
};
