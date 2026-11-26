/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/* Building trees from S-expressions for the tests (see tree_builder.h). Uses only
 * the public API. */

#include "tree_builder.h"

#include <amark/allocator.h>
#include <amark/buffer.h>
#include <amark/document.h>
#include <amark/error.h>
#include <amark/extension.h>
#include <amark/node.h>
#include <amark/token.h>

#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The longest type or attribute name, without its NUL. */
#define NAME_MAX_LENGTH 63u

/* The state of one building: the input, the position and the first failure. */
typedef struct builder
{
    amark_document_t *document;
    const amark_syntax_set_t *set;
    const char *input;
    size_t pos;
    amark_test_tree_attr_fn_t attr_fn;
    void *context;
    /* The decoded string, or the text of a value for attr_fn. */
    amark_buffer_t *scratch;
    amark_error_t error;
    size_t error_pos;
    char message[192];
} builder_t;

/* The kinds of values. */
typedef enum value_kind
{
    VALUE_STRING,
    VALUE_INTEGER,
    VALUE_SYMBOL,
    VALUE_LIST
} value_kind_t;

/* A value read: its kind, its extent in the input, and the integer or the decoded
 * string (in the scratch buffer). */
typedef struct value
{
    value_kind_t kind;
    size_t start;
    size_t end;
    long long integer;
} value_t;

/* --- Failures ------------------------------------------------------------------- */

/* Records the failure error at pos with a formatted message, unless a failure is
 * already recorded. Returns false, for the callers to return. */
static bool fail(builder_t *b, amark_error_t error, size_t pos, const char *format, ...)
{
    va_list args;

    if (b->error != AMARK_ERROR_NONE)
    {
        return false;
    }

    b->error = error;
    b->error_pos = pos;
    va_start(args, format);
    (void)vsnprintf(b->message, sizeof(b->message), format, args);
    va_end(args);

    return false;
}

/* Records the failure of a library call at pos. */
static bool fail_call(builder_t *b, amark_error_t error, size_t pos, const char *what)
{
    return fail(b, error, pos, "%s failed (%s)", what, amark_error_string(error));
}

/* Allocates the reason "offset N (line L, column C): message" for the failure at
 * pos of input (input may be NULL). Returns NULL when malloc fails. */
static char *format_reason(const char *input, size_t pos, const char *message)
{
    unsigned long line = 1;
    unsigned long column = 1;
    char text[320];
    size_t length;
    char *reason;
    size_t i;

    for (i = 0; (input != NULL) && (i < pos); i++)
    {
        if (input[i] == '\n')
        {
            line++;
            column = 1;
        }
        else
        {
            column++;
        }
    }

    (void)snprintf(text, sizeof(text), "offset %lu (line %lu, column %lu): %s",
                   (unsigned long)pos, line, column, message);
    length = strlen(text);
    reason = malloc(length + 1);
    if (reason != NULL)
    {
        memcpy(reason, text, length + 1);
    }

    return reason;
}

void amark_test_tree_free_reason(const char *reason)
{
    free((void *)reason);
}

void amark_test_tree_print_mismatch(const char *expected, const char *actual)
{
    fprintf(stderr, "tree dump mismatch\n  expected:\n%s\n  actual:\n%s\n",
            (expected != NULL) ? expected : "(null)", (actual != NULL) ? actual : "(null)");
}

/* --- Lexing --------------------------------------------------------------------- */

static bool is_space(char c)
{
    return ((c == ' ') || (c == '\t') || (c == '\n') || (c == '\r'));
}

static bool is_lower(char c)
{
    return ((c >= 'a') && (c <= 'z'));
}

static bool is_digit(char c)
{
    return ((c >= '0') && (c <= '9'));
}

/* Whether c may follow the first character of a type name ([a-z0-9_.-]). */
static bool is_type_char(char c)
{
    return (is_lower(c) || is_digit(c) || (c == '_') || (c == '.') || (c == '-'));
}

/* Whether c may follow the first character of an attribute name or a symbol. */
static bool is_identifier_char(char c)
{
    return (is_lower(c) || is_digit(c) || (c == '_'));
}

/* The character at the position (NUL at the end). */
static char peek(const builder_t *b)
{
    return b->input[b->pos];
}

static void skip_space(builder_t *b)
{
    while (is_space(peek(b)))
    {
        b->pos++;
    }
}

/* Consumes c after optional spaces, or fails with "expected 'c'". */
static bool expect(builder_t *b, char c)
{
    skip_space(b);
    if (peek(b) != c)
    {
        return fail(b, AMARK_ERROR_INVALID_ARGUMENT, b->pos, "expected '%c'", c);
    }

    b->pos++;

    return true;
}

/* Reads a name into name (NUL-terminated): a type name when type is true
 * ([a-z0-9][a-z0-9_.-]*), otherwise an identifier ([a-z][a-z0-9_]*). what names
 * it in the failures. */
static bool read_name(builder_t *b, bool type, char name[NAME_MAX_LENGTH + 1], const char *what)
{
    size_t start = b->pos;
    size_t length;
    char first = peek(b);

    if (!(is_lower(first) || (type && is_digit(first))))
    {
        return fail(b, AMARK_ERROR_INVALID_ARGUMENT, start, "expected %s", what);
    }

    while (type ? is_type_char(peek(b)) : is_identifier_char(peek(b)))
    {
        b->pos++;
    }

    length = b->pos - start;
    if (length > NAME_MAX_LENGTH)
    {
        return fail(b, AMARK_ERROR_INVALID_ARGUMENT, start, "%s too long", what);
    }

    memcpy(name, b->input + start, length);
    name[length] = '\0';

    return true;
}

/* The value of the hexadecimal digit c, or -1. */
static int hex_value(char c)
{
    if (is_digit(c))
    {
        return c - '0';
    }

    if ((c >= 'a') && (c <= 'f'))
    {
        return (c - 'a') + 10;
    }

    if ((c >= 'A') && (c <= 'F'))
    {
        return (c - 'A') + 10;
    }

    return -1;
}

/* Reads the string at the position (a '"') and decodes it into the scratch
 * buffer. */
static bool read_string(builder_t *b)
{
    size_t start = b->pos;
    amark_error_t err;

    amark_buffer_clear(b->scratch);
    b->pos++;
    for (;;)
    {
        char c = peek(b);
        size_t run = b->pos;
        unsigned char byte;

        /* The bytes without escape, appended in runs. */
        while ((c != '\0') && (c != '"') && (c != '\\'))
        {
            b->pos++;
            c = peek(b);
        }

        err = amark_buffer_append(b->scratch, b->input + run, b->pos - run);
        if (err != AMARK_ERROR_NONE)
        {
            return fail_call(b, err, start, "decoding the string");
        }

        if (c == '\0')
        {
            return fail(b, AMARK_ERROR_INVALID_ARGUMENT, start, "unterminated string");
        }

        if (c == '"')
        {
            b->pos++;
            return true;
        }

        /* An escape. */
        switch (b->input[b->pos + 1])
        {
        case '"':
            byte = '"';
            break;

        case '\\':
            byte = '\\';
            break;

        case 'n':
            byte = '\n';
            break;

        case 'r':
            byte = '\r';
            break;

        case 't':
            byte = '\t';
            break;

        case 'x':
        {
            int high = hex_value(b->input[b->pos + 2]);
            int low = (high >= 0) ? hex_value(b->input[b->pos + 3]) : -1;

            if (low < 0)
            {
                return fail(b, AMARK_ERROR_INVALID_ARGUMENT, b->pos,
                            "\\x needs two hexadecimal digits");
            }

            byte = (unsigned char)((high * 16) + low);
            b->pos += 2;
            break;
        }

        default:
            return fail(b, AMARK_ERROR_INVALID_ARGUMENT, b->pos, "unknown escape");
        }

        b->pos += 2;
        err = amark_buffer_append_byte(b->scratch, byte);
        if (err != AMARK_ERROR_NONE)
        {
            return fail_call(b, err, start, "decoding the string");
        }
    }
}

/* Reads the integer at the position: an optional '-' and decimal digits that fit
 * in long long. */
static bool read_integer(builder_t *b, long long *out_value)
{
    size_t start = b->pos;
    bool negative = false;
    /* The magnitude, accumulated as unsigned (LLONG_MIN included). */
    unsigned long long magnitude = 0;
    unsigned long long limit = (unsigned long long)LLONG_MAX;

    if (peek(b) == '-')
    {
        negative = true;
        limit++;
        b->pos++;
    }

    if (!is_digit(peek(b)))
    {
        return fail(b, AMARK_ERROR_INVALID_ARGUMENT, start, "expected an integer");
    }

    while (is_digit(peek(b)))
    {
        unsigned digit = (unsigned)(peek(b) - '0');

        if (magnitude > ((limit - digit) / 10u))
        {
            return fail(b, AMARK_ERROR_INVALID_ARGUMENT, start, "integer out of range");
        }

        magnitude = (magnitude * 10u) + digit;
        b->pos++;
    }

    if (is_identifier_char(peek(b)) || (peek(b) == '.') || (peek(b) == '-'))
    {
        return fail(b, AMARK_ERROR_INVALID_ARGUMENT, start, "expected an integer");
    }

    if (negative)
    {
        /* -(magnitude - 1) - 1 without overflow; magnitude is at least 1 here, or
         * the value is 0. */
        *out_value = (magnitude == 0u) ? 0 : (-(long long)(magnitude - 1u) - 1);
    }
    else
    {
        *out_value = (long long)magnitude;
    }

    return true;
}

/* Reads a symbol ([a-z][a-z0-9_]*) at the position. */
static bool read_symbol(builder_t *b)
{
    size_t start = b->pos;

    if (!is_lower(peek(b)))
    {
        return fail(b, AMARK_ERROR_INVALID_ARGUMENT, start, "expected a value");
    }

    while (is_identifier_char(peek(b)))
    {
        b->pos++;
    }

    return true;
}

/* Reads the value after optional spaces into v. A list is read without recursion
 * (its items are only checked); a string is decoded into the scratch buffer. */
static bool read_value(builder_t *b, value_t *v)
{
    size_t depth = 0;
    char c;

    skip_space(b);
    v->start = b->pos;
    v->integer = 0;
    c = peek(b);
    if (c == '"')
    {
        v->kind = VALUE_STRING;
        if (!read_string(b))
        {
            return false;
        }
    }
    else if ((c == '-') || is_digit(c))
    {
        v->kind = VALUE_INTEGER;
        if (!read_integer(b, &v->integer))
        {
            return false;
        }
    }
    else if (c == '(')
    {
        v->kind = VALUE_LIST;
        do
        {
            skip_space(b);
            c = peek(b);
            if (c == '(')
            {
                depth++;
                b->pos++;
                skip_space(b);
                if (peek(b) == ')')
                {
                    return fail(b, AMARK_ERROR_INVALID_ARGUMENT, b->pos, "empty list");
                }

                continue;
            }

            if (c == ')')
            {
                depth--;
                b->pos++;
                continue;
            }

            if (c == '\0')
            {
                return fail(b, AMARK_ERROR_INVALID_ARGUMENT, b->pos, "expected ')'");
            }

            if (c == ':')
            {
                /* An option inside a list, as in the tokens. */
                char name[NAME_MAX_LENGTH + 1];

                b->pos++;
                if (!read_name(b, false, name, "an attribute name"))
                {
                    return false;
                }

                continue;
            }

            if (c == '"')
            {
                if (!read_string(b))
                {
                    return false;
                }
            }
            else if ((c == '-') || is_digit(c))
            {
                long long ignored;

                if (!read_integer(b, &ignored))
                {
                    return false;
                }
            }
            else if (!read_symbol(b))
            {
                return false;
            }
        }
        while (depth > 0);
    }
    else
    {
        v->kind = VALUE_SYMBOL;
        if (!read_symbol(b))
        {
            return false;
        }
    }

    v->end = b->pos;

    return true;
}

/* Whether the symbol value v is the text symbol. */
static bool symbol_is(const builder_t *b, const value_t *v, const char *symbol)
{
    size_t length = strlen(symbol);

    return ((v->kind == VALUE_SYMBOL) && ((v->end - v->start) == length)
            && (memcmp(b->input + v->start, symbol, length) == 0));
}

/* --- Attributes ----------------------------------------------------------------- */

/* The kind whose name is the symbol v (AMARK_TOKEN_TEXT ... ), or 0. */
static amark_token_kind_t token_kind_of(const builder_t *b, const value_t *v)
{
    int kind;

    for (kind = AMARK_TOKEN_TEXT; kind <= AMARK_TOKEN_LAZY_CONTINUATION; kind++)
    {
        if (symbol_is(b, v, amark_token_kind_name((amark_token_kind_t)kind)))
        {
            return (amark_token_kind_t)kind;
        }
    }

    return (amark_token_kind_t)0;
}

/* Reads a non-negative integer into *out_value, failing when it exceeds max. */
static bool read_count(builder_t *b, unsigned long long max, unsigned long long *out_value)
{
    value_t v;

    if (!read_value(b, &v))
    {
        return false;
    }

    if ((v.kind != VALUE_INTEGER) || (v.integer < 0) || ((unsigned long long)v.integer > max))
    {
        return fail(b, AMARK_ERROR_INVALID_ARGUMENT, v.start, "expected an integer from 0 to %llu",
                    max);
    }

    *out_value = (unsigned long long)v.integer;

    return true;
}

/* Reads one token (kind offset length {flag} [:role n] [:source s] [:tab o w])
 * and adds it to node. */
static bool read_token(builder_t *b, amark_node_t *node)
{
    amark_token_t token;
    unsigned long long number = 0;
    size_t start;
    value_t v;
    amark_error_t err;

    memset(&token, 0, sizeof(token));
    token.struct_size = sizeof(token);
    skip_space(b);
    start = b->pos;
    if (!expect(b, '('))
    {
        return false;
    }

    if (!read_value(b, &v))
    {
        return false;
    }

    token.kind = token_kind_of(b, &v);
    if (token.kind == (amark_token_kind_t)0)
    {
        return fail(b, AMARK_ERROR_INVALID_ARGUMENT, v.start, "unknown token kind");
    }

    if (!read_count(b, SIZE_MAX, &number))
    {
        return false;
    }

    token.offset = (size_t)number;
    if (!read_count(b, SIZE_MAX, &number))
    {
        return false;
    }

    token.length = (size_t)number;
    for (;;)
    {
        char name[NAME_MAX_LENGTH + 1];
        size_t name_pos;

        skip_space(b);
        if (peek(b) == ')')
        {
            b->pos++;
            break;
        }

        if (peek(b) != ':')
        {
            if (!read_value(b, &v))
            {
                return false;
            }

            if (symbol_is(b, &v, "prefix"))
            {
                token.flags |= AMARK_TOKEN_FLAG_CONTAINER_PREFIX;
            }
            else if (symbol_is(b, &v, "partial_tab"))
            {
                token.flags |= AMARK_TOKEN_FLAG_PARTIAL_TAB;
            }
            else
            {
                return fail(b, AMARK_ERROR_INVALID_ARGUMENT, v.start, "unknown token flag");
            }

            continue;
        }

        b->pos++;
        name_pos = b->pos;
        if (!read_name(b, false, name, "a token option"))
        {
            return false;
        }

        if (strcmp(name, "role") == 0)
        {
            if (!read_count(b, UINT16_MAX, &number))
            {
                return false;
            }

            token.role = (uint16_t)number;
        }
        else if (strcmp(name, "source") == 0)
        {
            if (!read_count(b, UINT32_MAX, &number))
            {
                return false;
            }

            token.source = (amark_source_id_t)number;
        }
        else if (strcmp(name, "tab") == 0)
        {
            if (!read_count(b, UINT_MAX, &number))
            {
                return false;
            }

            token.tab_column_offset = (unsigned)number;
            if (!read_count(b, UINT_MAX, &number))
            {
                return false;
            }

            token.tab_columns = (unsigned)number;
        }
        else
        {
            return fail(b, AMARK_ERROR_INVALID_ARGUMENT, name_pos, "unknown token option :%s",
                        name);
        }
    }

    err = amark_node_add_token(node, &token);
    if (err != AMARK_ERROR_NONE)
    {
        return fail(b, err, start, "the token is rejected (%s)", amark_error_string(err));
    }

    return true;
}

/* Reads the list of tokens of :tokens and adds them to node. */
static bool read_tokens(builder_t *b, amark_node_t *node)
{
    if (!expect(b, '('))
    {
        return false;
    }

    for (;;)
    {
        skip_space(b);
        if (peek(b) == ')')
        {
            b->pos++;
            return true;
        }

        if (peek(b) != '(')
        {
            return fail(b, AMARK_ERROR_INVALID_ARGUMENT, b->pos, "expected '(' or ')'");
        }

        if (!read_token(b, node))
        {
            return false;
        }
    }
}

/* The setters of the built-in string attributes. */
typedef amark_error_t (*string_setter_t)(amark_node_t *node, const char *text, size_t length);

/* The setter of the built-in string attribute name of a node of type, or NULL. */
static string_setter_t builtin_string_setter(amark_node_type_t type, const char *name)
{
    bool link = ((type == AMARK_NODE_LINK) || (type == AMARK_NODE_IMAGE)
                 || (type == AMARK_NODE_LINK_DEFINITION));

    if ((type == AMARK_NODE_CODE_BLOCK) && (strcmp(name, "info") == 0))
    {
        return amark_node_set_code_info;
    }

    if (link && (strcmp(name, "destination") == 0))
    {
        return amark_node_set_link_destination;
    }

    if (link && (strcmp(name, "title") == 0))
    {
        return amark_node_set_link_title;
    }

    if ((type == AMARK_NODE_LINK_DEFINITION) && (strcmp(name, "label") == 0))
    {
        return amark_node_set_link_label;
    }

    return NULL;
}

/* Checks the result of a setter for the attribute name at pos. */
static bool check_set(builder_t *b, amark_error_t err, size_t pos, const char *name)
{
    if (err != AMARK_ERROR_NONE)
    {
        return fail(b, err, pos, "the value of :%s is rejected (%s)", name,
                    amark_error_string(err));
    }

    return true;
}

/*
 * Sets a built-in attribute of node from v when node is of a built-in type that
 * has the attribute name; *out_handled tells whether it has.
 */
static bool set_builtin_attr(builder_t *b, amark_node_t *node, const char *name, const value_t *v,
                             bool *out_handled)
{
    amark_node_type_t type = amark_node_get_type(node);
    string_setter_t setter = builtin_string_setter(type, name);

    *out_handled = true;
    if (setter != NULL)
    {
        if (v->kind != VALUE_STRING)
        {
            return fail(b, AMARK_ERROR_INVALID_ARGUMENT, v->start, ":%s expects a string", name);
        }

        if (amark_buffer_length(b->scratch) == 0)
        {
            return true;
        }

        return check_set(b, setter(node, amark_buffer_data(b->scratch),
                                   amark_buffer_length(b->scratch)),
                         v->start, name);
    }

    if ((type == AMARK_NODE_HEADING) && (strcmp(name, "level") == 0))
    {
        if ((v->kind != VALUE_INTEGER) || (v->integer < INT_MIN) || (v->integer > INT_MAX))
        {
            return fail(b, AMARK_ERROR_INVALID_ARGUMENT, v->start, ":level expects an integer");
        }

        return check_set(b, amark_node_set_heading_level(node, (int)v->integer), v->start, name);
    }

    if ((type == AMARK_NODE_LIST) && (strcmp(name, "type") == 0))
    {
        amark_list_type_t list_type;

        if (symbol_is(b, v, "bullet"))
        {
            list_type = AMARK_LIST_BULLET;
        }
        else if (symbol_is(b, v, "ordered"))
        {
            list_type = AMARK_LIST_ORDERED;
        }
        else
        {
            return fail(b, AMARK_ERROR_INVALID_ARGUMENT, v->start,
                        ":type expects bullet or ordered");
        }

        return check_set(b, amark_node_set_list_type(node, list_type), v->start, name);
    }

    if ((type == AMARK_NODE_LIST) && (strcmp(name, "start") == 0))
    {
        if ((v->kind != VALUE_INTEGER) || (v->integer < LONG_MIN) || (v->integer > LONG_MAX))
        {
            return fail(b, AMARK_ERROR_INVALID_ARGUMENT, v->start, ":start expects an integer");
        }

        return check_set(b, amark_node_set_list_start(node, (long)v->integer), v->start, name);
    }

    if ((type == AMARK_NODE_LIST) && (strcmp(name, "tight") == 0))
    {
        bool tight;

        if (symbol_is(b, v, "true"))
        {
            tight = true;
        }
        else if (symbol_is(b, v, "false"))
        {
            tight = false;
        }
        else
        {
            return fail(b, AMARK_ERROR_INVALID_ARGUMENT, v->start, ":tight expects true or false");
        }

        return check_set(b, amark_node_set_list_tight(node, tight), v->start, name);
    }

    *out_handled = false;

    return true;
}

/* Reads one attribute (the position is at its ':') and applies it to node. */
static bool read_attr(builder_t *b, amark_node_t *node)
{
    char name[NAME_MAX_LENGTH + 1];
    size_t start = b->pos;
    bool handled = false;
    value_t v;
    amark_error_t err;

    b->pos++;
    if (!read_name(b, false, name, "an attribute name"))
    {
        return false;
    }

    if (strcmp(name, "tokens") == 0)
    {
        return read_tokens(b, node);
    }

    if (!read_value(b, &v))
    {
        return false;
    }

    if ((strcmp(name, "edits") == 0) || (strcmp(name, "span") == 0))
    {
        return true;
    }

    if (!set_builtin_attr(b, node, name, &v, &handled))
    {
        return false;
    }

    if (handled)
    {
        return true;
    }

    if (b->attr_fn == NULL)
    {
        return fail(b, AMARK_ERROR_INVALID_ARGUMENT, start, "no attribute function for :%s",
                    name);
    }

    /* Any value but a string goes as its text; the scratch buffer holds it with a
     * NUL after it. */
    if (v.kind != VALUE_STRING)
    {
        amark_buffer_clear(b->scratch);
        err = amark_buffer_append(b->scratch, b->input + v.start, v.end - v.start);
        if (err != AMARK_ERROR_NONE)
        {
            return fail_call(b, err, start, "copying the value");
        }
    }

    err = b->attr_fn(node, name, amark_buffer_data(b->scratch), amark_buffer_length(b->scratch),
                     b->context);
    if (err != AMARK_ERROR_NONE)
    {
        return fail(b, err, start, "the attribute function rejects :%s (%s)", name,
                    amark_error_string(err));
    }

    return true;
}

/* --- Nodes ---------------------------------------------------------------------- */

/* Reads the type name after '(' and creates a node of that type, appended to
 * parent unless parent is NULL. */
static bool create_node(builder_t *b, amark_node_t *parent, amark_node_t **out_node)
{
    char name[NAME_MAX_LENGTH + 1];
    size_t start = b->pos;
    amark_node_type_t type = AMARK_NODE_NONE;
    amark_node_t *node = NULL;
    amark_error_t err;

    if (!read_name(b, true, name, "a type name"))
    {
        return false;
    }

    if (!amark_syntax_set_find_node_type(b->set, name, &type))
    {
        return fail(b, AMARK_ERROR_INVALID_ARGUMENT, start, "unknown type '%s'", name);
    }

    err = amark_node_create(b->document, type, &node);
    if (err != AMARK_ERROR_NONE)
    {
        return fail(b, err, start, "creating a node of '%s' failed (%s)", name,
                    amark_error_string(err));
    }

    if (parent != NULL)
    {
        err = amark_node_append_child(parent, node);
        if (err != AMARK_ERROR_NONE)
        {
            amark_node_destroy(node);
            return fail(b, err, start, "'%s' cannot be a child of '%s' (%s)", name,
                        amark_node_type_name(parent), amark_error_string(err));
        }
    }

    *out_node = node;

    return true;
}

/* Reads the literal string (the position is at its '"') and sets it on node. */
static bool read_literal(builder_t *b, amark_node_t *node)
{
    size_t start = b->pos;
    amark_error_t err;

    if (!read_string(b))
    {
        return false;
    }

    if (amark_buffer_length(b->scratch) == 0)
    {
        return true;
    }

    err = amark_node_set_literal(node, amark_buffer_data(b->scratch),
                                 amark_buffer_length(b->scratch));
    if (err != AMARK_ERROR_NONE)
    {
        return fail(b, err, start, "the literal is rejected (%s)", amark_error_string(err));
    }

    return true;
}

/*
 * Reads the whole input. *top is the top node: the root of the document when the
 * input starts with (document, otherwise the detached node created. The walk keeps
 * no stack: a node closes into its parent. A node accepts attributes, then a
 * literal, then children, in that order.
 */
static bool read_tree(builder_t *b, amark_node_t **top)
{
    amark_node_t *root = amark_document_root(b->document);
    amark_node_t *current = NULL;
    bool attrs_allowed = true;
    bool literal_allowed = true;

    if (!expect(b, '('))
    {
        return false;
    }

    skip_space(b);
    if ((strncmp(b->input + b->pos, "document", 8) == 0) && !is_type_char(b->input[b->pos + 8]))
    {
        b->pos += 8;
        *top = root;
    }
    else if (!create_node(b, NULL, top))
    {
        return false;
    }

    current = *top;
    for (;;)
    {
        char c;

        skip_space(b);
        c = peek(b);
        if (c == ':')
        {
            if (!attrs_allowed)
            {
                return fail(b, AMARK_ERROR_INVALID_ARGUMENT, b->pos,
                            "attribute after the literal or a child");
            }

            if (!read_attr(b, current))
            {
                return false;
            }
        }
        else if (c == '"')
        {
            if (!literal_allowed)
            {
                return fail(b, AMARK_ERROR_INVALID_ARGUMENT, b->pos,
                            "literal after a literal or a child");
            }

            attrs_allowed = false;
            literal_allowed = false;
            if (!read_literal(b, current))
            {
                return false;
            }
        }
        else if (c == '(')
        {
            amark_node_t *child = NULL;

            b->pos++;
            skip_space(b);
            if (!create_node(b, current, &child))
            {
                return false;
            }

            current = child;
            attrs_allowed = true;
            literal_allowed = true;
        }
        else if (c == ')')
        {
            b->pos++;
            if (current == *top)
            {
                break;
            }

            /* Back in the parent, after a child. */
            current = amark_node_parent(current);
            attrs_allowed = false;
            literal_allowed = false;
        }
        else
        {
            return fail(b, AMARK_ERROR_INVALID_ARGUMENT, b->pos, "expected ')'");
        }
    }

    skip_space(b);
    if (peek(b) != '\0')
    {
        return fail(b, AMARK_ERROR_INVALID_ARGUMENT, b->pos, "unexpected text after the tree");
    }

    return true;
}

/* --- Building ------------------------------------------------------------------- */

/* Writes the reason of the failure of b in *out_reason (when not NULL). */
static void set_reason(const builder_t *b, const char *input, const char **out_reason)
{
    if (out_reason != NULL)
    {
        *out_reason = format_reason(input, b->error_pos, b->message);
    }
}

/* Whether the tokens a and b have the same fields (struct_size aside). */
static bool same_token(const amark_token_t *a, const amark_token_t *b)
{
    return ((a->kind == b->kind) && (a->role == b->role) && (a->flags == b->flags)
            && (a->source == b->source) && (a->offset == b->offset) && (a->length == b->length)
            && (a->tab_column_offset == b->tab_column_offset)
            && (a->tab_columns == b->tab_columns));
}

/* Reads the token at index of node (the index is in range). */
static void token_at(const amark_node_t *node, size_t index, amark_token_t *out_token)
{
    memset(out_token, 0, sizeof(*out_token));
    out_token->struct_size = sizeof(*out_token);
    (void)amark_node_token_at(node, index, out_token);
}

/* Copies the tokens of root into *out_saved (NULL when it has none), allocated
 * with the Amark allocator. */
static amark_error_t save_tokens(const amark_node_t *root, amark_token_t **out_saved,
                                 size_t *out_count)
{
    size_t count = amark_node_token_count(root);
    amark_token_t *saved = NULL;
    size_t i;

    if (count > 0)
    {
        saved = amark_calloc(count, sizeof(*saved));
        if (saved == NULL)
        {
            return AMARK_ERROR_OUT_OF_MEMORY;
        }
    }

    for (i = 0; i < count; i++)
    {
        token_at(root, i, &saved[i]);
    }

    *out_saved = saved;
    *out_count = count;

    return AMARK_ERROR_NONE;
}

/*
 * Removes from root the tokens the building added, so that it has the saved ones
 * again. Adding a token never moves the others out of their order, so the saved
 * tokens are a subsequence of the current ones: each current token that does not
 * match the next saved one (by its fields) was added and is removed. An added
 * token equal to a saved one may be the one kept, which gives the same tokens.
 * Removing a token in range never fails nor allocates, so this cannot fail.
 */
static void restore_tokens(amark_node_t *root, const amark_token_t *saved, size_t saved_count)
{
    size_t matched = 0;
    size_t i = 0;

    while (i < amark_node_token_count(root))
    {
        amark_token_t token;

        token_at(root, i, &token);
        if ((matched < saved_count) && same_token(&token, &saved[matched]))
        {
            matched++;
            i++;
        }
        else
        {
            (void)amark_node_remove_token(root, i);
        }
    }
}

amark_error_t amark_test_tree_build(amark_document_t *document, const char *sexpr,
                                    amark_test_tree_attr_fn_t attr_fn, void *context,
                                    amark_node_t **out_node, const char **out_reason)
{
    builder_t b;
    amark_node_t *root;
    amark_node_t *last_before;
    amark_node_t *top = NULL;
    amark_token_t *saved_tokens = NULL;
    size_t saved_count = 0;
    bool tracking;
    bool ok;
    amark_error_t err;

    memset(&b, 0, sizeof(b));
    if (out_node != NULL)
    {
        *out_node = NULL;
    }

    if (out_reason != NULL)
    {
        *out_reason = NULL;
    }

    if ((document == NULL) || (sexpr == NULL))
    {
        (void)fail(&b, AMARK_ERROR_INVALID_ARGUMENT, 0, "NULL document or input");
        set_reason(&b, NULL, out_reason);
        return b.error;
    }

    err = amark_buffer_create(&b.scratch);
    if (err != AMARK_ERROR_NONE)
    {
        (void)fail_call(&b, err, 0, "creating the scratch buffer");
        set_reason(&b, sexpr, out_reason);
        return b.error;
    }

    /* The tokens of the root, to put back on a failure. */
    err = save_tokens(amark_document_root(document), &saved_tokens, &saved_count);
    if (err != AMARK_ERROR_NONE)
    {
        (void)fail_call(&b, err, 0, "saving the tokens of the root");
        set_reason(&b, sexpr, out_reason);
        amark_buffer_destroy(b.scratch);
        return b.error;
    }

    b.document = document;
    b.set = amark_document_syntax_set(document);
    b.input = sexpr;
    b.attr_fn = attr_fn;
    b.context = context;
    root = amark_document_root(document);
    last_before = amark_node_last_child(root);
    tracking = amark_document_edit_tracking(document);
    amark_document_set_edit_tracking(document, false);

    ok = read_tree(&b, &top);
    if (!ok)
    {
        if (top == root)
        {
            /* The children appended to the root, after those it had. */
            amark_node_t *child;

            while ((child = ((last_before != NULL) ? amark_node_next(last_before)
                                                   : amark_node_first_child(root)))
                   != NULL)
            {
                amark_node_destroy(child);
            }

            restore_tokens(root, saved_tokens, saved_count);
        }
        else
        {
            amark_node_destroy(top);
        }

        top = NULL;
        set_reason(&b, sexpr, out_reason);
    }

    amark_document_set_edit_tracking(document, tracking);
    amark_free(saved_tokens);
    amark_buffer_destroy(b.scratch);
    if (out_node != NULL)
    {
        *out_node = top;
    }

    return b.error;
}
