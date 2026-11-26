/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/* Literals: string checks and normalization, getting and setting literals, setting
 * inline text. */

#include <amark/node.h>

#include <amark/allocator.h>
#include <amark/error.h>
#include <amark/extension.h>
#include <amark/utf8.h>

#include "base/allocator_internal.h"
#include "tree/syntax_set.h"
#include "tree/tree.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* --- String helpers ---------------------------------------------------------------- */

amark_error_t amark_text_check(const char *text, size_t length)
{
    if (text == NULL)
    {
        return (length == 0) ? AMARK_ERROR_NONE : AMARK_ERROR_INVALID_ARGUMENT;
    }

    if ((memchr(text, '\0', length) != NULL) || !amark_utf8_validate(text, length, NULL))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    return AMARK_ERROR_NONE;
}

bool amark_text_has_line_ending(const char *text, size_t length)
{
    size_t i;

    for (i = 0; i < length; i++)
    {
        if ((text[i] == '\n') || (text[i] == '\r'))
        {
            return true;
        }
    }

    return false;
}

bool amark_text_has_blank_line(const char *text, size_t length)
{
    bool after_line_ending = false;
    bool only_blank = true;
    size_t i;

    for (i = 0; i < length; i++)
    {
        char c = text[i];

        if ((c == '\n') || (c == '\r'))
        {
            if (after_line_ending && only_blank)
            {
                return true;
            }

            /* CRLF is one line ending. */
            if ((c == '\r') && ((i + 1) < length) && (text[i + 1] == '\n'))
            {
                i++;
            }

            after_line_ending = true;
            only_blank = true;
        }
        else if ((c != ' ') && (c != '\t'))
        {
            only_blank = false;
        }
    }

    return false;
}

amark_error_t amark_text_normalize(const char *text, size_t length, bool final_lf,
                                   char **out_text, size_t *out_length)
{
    size_t size;
    size_t in;
    size_t out = 0;
    char *copy;

    *out_text = NULL;
    *out_length = 0;
    if (length == 0)
    {
        return AMARK_ERROR_NONE;
    }

    /* At most length bytes, an added LF and the NUL. */
    if (!amark_size_add(length, 2, &size))
    {
        return AMARK_ERROR_OVERFLOW;
    }

    copy = amark_malloc(size);
    if (copy == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }

    for (in = 0; in < length; in++)
    {
        if (text[in] == '\r')
        {
            copy[out] = '\n';
            if (((in + 1) < length) && (text[in + 1] == '\n'))
            {
                in++;
            }
        }
        else
        {
            copy[out] = text[in];
        }

        out++;
    }

    if (final_lf && (copy[out - 1] != '\n'))
    {
        copy[out] = '\n';
        out++;
    }

    copy[out] = '\0';
    *out_text = copy;
    *out_length = out;

    return AMARK_ERROR_NONE;
}

bool amark_text_equal(const char *a, size_t a_length, const char *b, size_t b_length)
{
    return (a_length == b_length) && ((a_length == 0) || (memcmp(a, b, a_length) == 0));
}

/* --- Literals ---------------------------------------------------------------------- */

const char *amark_node_literal(const amark_node_t *node, size_t *out_length)
{
    if (out_length != NULL)
    {
        *out_length = 0;
    }

    if ((node == NULL) || (node->literal == NULL))
    {
        return "";
    }

    if (out_length != NULL)
    {
        *out_length = node->literal_length;
    }

    return node->literal;
}

amark_error_t amark_node_set_literal(amark_node_t *node, const char *text, size_t length)
{
    amark_literal_mode_t mode;
    char *value;
    size_t value_length;
    amark_error_t err;

    if (node == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    err = amark_text_check(text, length);
    if (err != AMARK_ERROR_NONE)
    {
        return err;
    }

    /* The rules of the literal mode, before anything is allocated. */
    mode = node->desc->literal_mode;
    if ((mode == AMARK_LITERAL_NONE)
        || ((mode == AMARK_LITERAL_LINE) && amark_text_has_line_ending(text, length))
        || ((mode == AMARK_LITERAL_SPAN) && amark_text_has_blank_line(text, length)))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    err = amark_text_normalize(text, length, mode == AMARK_LITERAL_LINES, &value,
                               &value_length);
    if (err != AMARK_ERROR_NONE)
    {
        return err;
    }

    /* The type checks the value it would keep. */
    if (node->desc->validate_literal != NULL)
    {
        err = node->desc->validate_literal(node, (value != NULL) ? value : "", value_length);
        if (err != AMARK_ERROR_NONE)
        {
            amark_free(value);
            return err;
        }
    }

    if (amark_text_equal(value, value_length, node->literal, node->literal_length))
    {
        amark_free(value);
        return AMARK_ERROR_NONE;
    }

    amark_free(node->literal);
    node->literal = value;
    node->literal_length = value_length;
    amark_edit_mark(node, AMARK_EDIT_CONTENT);

    return AMARK_ERROR_NONE;
}

/* --- Inline text ------------------------------------------------------------------- */

/* Destroys the detached nodes of the chain that starts at first (linked by next,
 * without a parent). */
static void destroy_chain(amark_node_t *first)
{
    while (first != NULL)
    {
        amark_node_t *next = first->next;

        first->prev = NULL;
        first->next = NULL;
        amark_node_destroy_subtree(first);
        first = next;
    }
}

/*
 * Creates a detached node of type (whose descriptor is desc) with the literal
 * text[0..length) (none when length is 0), and appends it to the chain that ends at
 * *last (starting at *first).
 */
static amark_error_t chain_node(amark_document_t *document, amark_node_type_t type,
                                const amark_node_type_desc_t *desc, const char *text,
                                size_t length, amark_node_t **first, amark_node_t **last)
{
    amark_node_t *node;
    char *literal = NULL;
    size_t literal_length = 0;
    amark_error_t err;

    err = amark_text_normalize(text, length, false, &literal, &literal_length);
    if (err != AMARK_ERROR_NONE)
    {
        return err;
    }

    err = amark_node_create_with_desc(document, type, desc, &node);
    if (err != AMARK_ERROR_NONE)
    {
        amark_free(literal);
        return err;
    }

    /* The type checks the literal of a text node (the soft breaks have none). */
    if ((length != 0) && (desc->validate_literal != NULL))
    {
        err = desc->validate_literal(node, literal, literal_length);
        if (err != AMARK_ERROR_NONE)
        {
            amark_free(literal);
            amark_node_destroy_subtree(node);
            return err;
        }
    }

    node->literal = literal;
    node->literal_length = literal_length;
    node->prev = *last;
    if (*last != NULL)
    {
        (*last)->next = node;
    }
    else
    {
        *first = node;
    }

    *last = node;

    return AMARK_ERROR_NONE;
}

/*
 * Creates the detached text nodes and soft breaks of text: each line ending (LF, CR
 * or CRLF) is a soft break, and each non-empty part between them is a text node,
 * so leading, trailing and consecutive line endings give soft breaks with no text
 * between them, and "" gives nothing. On failure, destroys what it created and
 * writes NULL.
 */
static amark_error_t create_inline_nodes(amark_node_t *target, const char *text,
                                         size_t length, amark_node_t **out_first)
{
    amark_document_t *document = target->document;
    const amark_node_type_desc_t *text_desc;
    const amark_node_type_desc_t *softbreak_desc;
    amark_node_t *first = NULL;
    amark_node_t *last = NULL;
    size_t start = 0;
    size_t i;
    amark_error_t err = AMARK_ERROR_NONE;

    *out_first = NULL;
    text_desc = amark_syntax_set_desc(document->syntax, AMARK_NODE_TEXT);
    softbreak_desc = amark_syntax_set_desc(document->syntax, AMARK_NODE_SOFTBREAK);
    if (amark_text_has_line_ending(text, length)
        && ((softbreak_desc == NULL) || !amark_node_allows_child(target, softbreak_desc)))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    for (i = 0; (err == AMARK_ERROR_NONE) && (i <= length); i++)
    {
        size_t end = i;

        if ((i < length) && (text[i] != '\n') && (text[i] != '\r'))
        {
            continue;
        }

        if (end > start)
        {
            err = chain_node(document, AMARK_NODE_TEXT, text_desc, text + start, end - start,
                             &first, &last);
        }

        if ((err == AMARK_ERROR_NONE) && (i < length))
        {
            if ((text[i] == '\r') && ((i + 1) < length) && (text[i + 1] == '\n'))
            {
                i++;
            }

            err = chain_node(document, AMARK_NODE_SOFTBREAK, softbreak_desc, NULL, 0, &first,
                             &last);
        }

        start = i + 1;
    }

    if (err != AMARK_ERROR_NONE)
    {
        destroy_chain(first);
        return err;
    }

    *out_first = first;

    return AMARK_ERROR_NONE;
}

amark_error_t amark_node_set_inline_text(amark_node_t *node, const char *text, size_t length)
{
    amark_node_t *first;
    amark_node_t *child;
    amark_error_t err;

    if ((node == NULL) || !amark_node_can_contain(node, AMARK_NODE_TEXT))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    err = amark_text_check(text, length);
    if (err != AMARK_ERROR_NONE)
    {
        return err;
    }

    /* Every new child first: a failure leaves the children as they are. */
    err = create_inline_nodes(node, text, length, &first);
    if (err != AMARK_ERROR_NONE)
    {
        return err;
    }

    /* Nothing to replace with nothing: no change, no record. */
    if ((first == NULL) && (node->first_child == NULL))
    {
        return AMARK_ERROR_NONE;
    }

    while ((child = node->first_child) != NULL)
    {
        node->first_child = child->next;
        if (child->next != NULL)
        {
            child->next->prev = NULL;
        }
        else
        {
            node->last_child = NULL;
        }

        child->parent = NULL;
        child->next = NULL;
        amark_node_destroy_subtree(child);
    }

    amark_edit_mark(node, AMARK_EDIT_CHILDREN);

    while (first != NULL)
    {
        child = first;
        first = child->next;
        child->prev = NULL;
        child->next = NULL;
        amark_node_link_child(node, NULL, child);
    }

    return AMARK_ERROR_NONE;
}
