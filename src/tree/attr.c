/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/* The built-in attributes: getters and setters. */

#include <amark/node.h>

#include <amark/allocator.h>
#include <amark/error.h>

#include "tree/syntax_set.h"
#include "tree/tree.h"

#include <stdbool.h>
#include <stddef.h>

/* The largest start number of a list. */
#define LIST_START_MAX 999999999L

/* The most code points of a link label. */
#define LABEL_MAX_CODE_POINTS 999u

/* The rules of a string attribute (applied after the common check). */
typedef enum string_rule
{
    /* No line ending (info, destination). */
    STRING_RULE_LINE,
    /* No blank line; the line endings are normalized to LF (title). */
    STRING_RULE_SPAN,
    /* The rules of STRING_RULE_SPAN and those of a link label (label). */
    STRING_RULE_LABEL
} string_rule_t;

/* --- Type data ---------------------------------------------------------------------- */

/* Whether node is of type. The built-in types have fixed identifiers and amark.core
 * cannot be replaced, so the identifier gives the layout of the data. */
static bool is_type(const amark_node_t *node, amark_node_type_t type)
{
    return (node != NULL) && (node->type == type);
}

/* Whether node is a link, an image or a link reference definition (whose data
 * starts with the layout of a link). */
static bool is_link(const amark_node_t *node)
{
    return is_type(node, AMARK_NODE_LINK) || is_type(node, AMARK_NODE_IMAGE)
           || is_type(node, AMARK_NODE_LINK_DEFINITION);
}

/* The type data of node when its type is type, or NULL (for the getters). */
static const void *data_of(const amark_node_t *node, amark_node_type_t type)
{
    return is_type(node, type) ? amark_node_data_const(node) : NULL;
}

/* The type data of node when its type is type, or NULL (for the setters). */
static void *mutable_data_of(amark_node_t *node, amark_node_type_t type)
{
    return is_type(node, type) ? amark_node_data(node) : NULL;
}

/* The link data of node, or NULL (for the getters). */
static const amark_link_data_t *link_data_of(const amark_node_t *node)
{
    return is_link(node) ? amark_node_data_const(node) : NULL;
}

/* The link data of node, or NULL (for the setters). */
static amark_link_data_t *mutable_link_data_of(amark_node_t *node)
{
    return is_link(node) ? amark_node_data(node) : NULL;
}

/* --- Strings -------------------------------------------------------------------------- */

/* The text of string ("" for NULL), writing its length to *out_length when
 * out_length is not NULL; "" and 0 when string is NULL. */
static const char *get_string(const amark_builtin_string_t *string, size_t *out_length)
{
    if (out_length != NULL)
    {
        *out_length = 0;
    }

    if ((string == NULL) || (string->text == NULL))
    {
        return "";
    }

    if (out_length != NULL)
    {
        *out_length = string->length;
    }

    return string->text;
}

/* Whether c is an ASCII punctuation character (which a backslash escapes). */
static bool is_ascii_punctuation(char c)
{
    return ((c >= '!') && (c <= '/')) || ((c >= ':') && (c <= '@'))
           || ((c >= '[') && (c <= '`')) || ((c >= '{') && (c <= '~'));
}

/*
 * Whether text (valid UTF-8 without NUL or blank line) is a link label CommonMark
 * can make: 1-999 code points (CRLF counted as one line ending, as it is kept as
 * LF), at least one that is not a space, a tab or a line ending, and no "[" or "]"
 * that is not backslash-escaped ("\\[" is an escaped backslash and a raw "["), and
 * no trailing backslash that no escape consumes (it would escape the closing "]").
 */
static bool is_label(const char *text, size_t length)
{
    size_t code_points = 0;
    bool has_content = false;
    size_t i;

    for (i = 0; i < length; i++)
    {
        unsigned char c = (unsigned char)text[i];

        /* Each lead byte (not 10xxxxxx) starts a code point. */
        if ((c & 0xC0u) != 0x80u)
        {
            code_points++;
        }

        if ((c == '[') || (c == ']'))
        {
            return false;
        }

        if ((c == '\\') && ((i + 1) == length))
        {
            return false;
        }

        if ((c == '\\') && is_ascii_punctuation(text[i + 1]))
        {
            /* The escaped character is one more code point and is content. */
            code_points++;
            has_content = true;
            i++;
        }
        else if ((c == '\r') && ((i + 1) < length) && (text[i + 1] == '\n'))
        {
            i++;
        }
        else if ((c != ' ') && (c != '\t') && (c != '\n') && (c != '\r'))
        {
            has_content = true;
        }
    }

    return (code_points >= 1) && (code_points <= LABEL_MAX_CODE_POINTS) && has_content;
}

/*
 * Sets string, an attribute of node, to text[0..length) after the common check and
 * the rules of rule (all before anything is allocated). The same value changes and
 * records nothing; another value is allocated before the old one is freed and
 * records AMARK_EDIT_CONTENT. The tokens of node are not changed.
 */
static amark_error_t set_string(amark_node_t *node, amark_builtin_string_t *string,
                                string_rule_t rule, const char *text, size_t length)
{
    char *value;
    size_t value_length;
    amark_error_t err;

    if (string == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    err = amark_text_check(text, length);
    if (err != AMARK_ERROR_NONE)
    {
        return err;
    }

    if (((rule == STRING_RULE_LINE) && amark_text_has_line_ending(text, length))
        || ((rule != STRING_RULE_LINE) && amark_text_has_blank_line(text, length))
        || ((rule == STRING_RULE_LABEL) && !is_label(text, length)))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    /* A text without line endings is copied as it is. */
    err = amark_text_normalize(text, length, false, &value, &value_length);
    if (err != AMARK_ERROR_NONE)
    {
        return err;
    }

    if (amark_text_equal(value, value_length, string->text, string->length))
    {
        amark_free(value);
        return AMARK_ERROR_NONE;
    }

    amark_free(string->text);
    string->text = value;
    string->length = value_length;
    amark_edit_mark(node, AMARK_EDIT_CONTENT);

    return AMARK_ERROR_NONE;
}

/* --- heading -------------------------------------------------------------------------- */

int amark_node_heading_level(const amark_node_t *node)
{
    const amark_heading_data_t *data = data_of(node, AMARK_NODE_HEADING);

    return (data != NULL) ? data->level : 0;
}

amark_error_t amark_node_set_heading_level(amark_node_t *node, int level)
{
    amark_heading_data_t *data = mutable_data_of(node, AMARK_NODE_HEADING);

    if ((data == NULL) || (level < 1) || (level > 6))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    if (data->level != level)
    {
        data->level = level;
        amark_edit_mark(node, AMARK_EDIT_CONTENT);
    }

    return AMARK_ERROR_NONE;
}

/* --- list ----------------------------------------------------------------------------- */

amark_list_type_t amark_node_list_type(const amark_node_t *node)
{
    const amark_list_data_t *data = data_of(node, AMARK_NODE_LIST);

    return (data != NULL) ? data->type : AMARK_LIST_NONE;
}

amark_error_t amark_node_set_list_type(amark_node_t *node, amark_list_type_t type)
{
    amark_list_data_t *data = mutable_data_of(node, AMARK_NODE_LIST);

    if ((data == NULL) || ((type != AMARK_LIST_BULLET) && (type != AMARK_LIST_ORDERED)))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    if (data->type != type)
    {
        data->type = type;
        amark_edit_mark(node, AMARK_EDIT_CONTENT);
    }

    return AMARK_ERROR_NONE;
}

long amark_node_list_start(const amark_node_t *node)
{
    const amark_list_data_t *data = data_of(node, AMARK_NODE_LIST);

    return (data != NULL) ? data->start : 0;
}

amark_error_t amark_node_set_list_start(amark_node_t *node, long start)
{
    amark_list_data_t *data = mutable_data_of(node, AMARK_NODE_LIST);

    if ((data == NULL) || (start < 0) || (start > LIST_START_MAX))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    /* The start number is kept (and compared) whatever the type of the list, so a
     * bullet list keeps it for a later change to ordered. */
    if (data->start != start)
    {
        data->start = start;
        amark_edit_mark(node, AMARK_EDIT_CONTENT);
    }

    return AMARK_ERROR_NONE;
}

bool amark_node_list_tight(const amark_node_t *node)
{
    const amark_list_data_t *data = data_of(node, AMARK_NODE_LIST);

    return (data != NULL) && data->tight;
}

amark_error_t amark_node_set_list_tight(amark_node_t *node, bool tight)
{
    amark_list_data_t *data = mutable_data_of(node, AMARK_NODE_LIST);

    if (data == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    if (data->tight != tight)
    {
        data->tight = tight;
        amark_edit_mark(node, AMARK_EDIT_CONTENT);
    }

    return AMARK_ERROR_NONE;
}

/* --- code_block ----------------------------------------------------------------------- */

const char *amark_node_code_info(const amark_node_t *node, size_t *out_length)
{
    const amark_code_block_data_t *data = data_of(node, AMARK_NODE_CODE_BLOCK);

    return get_string((data != NULL) ? &data->info : NULL, out_length);
}

amark_error_t amark_node_set_code_info(amark_node_t *node, const char *text, size_t length)
{
    amark_code_block_data_t *data = mutable_data_of(node, AMARK_NODE_CODE_BLOCK);

    return set_string(node, (data != NULL) ? &data->info : NULL, STRING_RULE_LINE, text,
                      length);
}

/* --- link, image, link_definition ----------------------------------------------------- */

const char *amark_node_link_destination(const amark_node_t *node, size_t *out_length)
{
    const amark_link_data_t *data = link_data_of(node);

    return get_string((data != NULL) ? &data->destination : NULL, out_length);
}

amark_error_t amark_node_set_link_destination(amark_node_t *node, const char *text,
                                              size_t length)
{
    amark_link_data_t *data = mutable_link_data_of(node);

    return set_string(node, (data != NULL) ? &data->destination : NULL, STRING_RULE_LINE,
                      text, length);
}

const char *amark_node_link_title(const amark_node_t *node, size_t *out_length)
{
    const amark_link_data_t *data = link_data_of(node);

    return get_string((data != NULL) ? &data->title : NULL, out_length);
}

amark_error_t amark_node_set_link_title(amark_node_t *node, const char *text, size_t length)
{
    amark_link_data_t *data = mutable_link_data_of(node);

    return set_string(node, (data != NULL) ? &data->title : NULL, STRING_RULE_SPAN, text,
                      length);
}

const char *amark_node_link_label(const amark_node_t *node, size_t *out_length)
{
    const amark_link_definition_data_t *data = data_of(node, AMARK_NODE_LINK_DEFINITION);

    return get_string((data != NULL) ? &data->label : NULL, out_length);
}

amark_error_t amark_node_set_link_label(amark_node_t *node, const char *text, size_t length)
{
    amark_link_definition_data_t *data = mutable_data_of(node, AMARK_NODE_LINK_DEFINITION);

    return set_string(node, (data != NULL) ? &data->label : NULL, STRING_RULE_LABEL, text,
                      length);
}
