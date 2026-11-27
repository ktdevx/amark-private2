/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * Building trees from S-expressions for the tests (see tree_builder.h). Uses only
 * the public API.
 *
 * The build has two phases:
 *   1. The reader turns the text into a tree of atoms and lists (sx_t), each with
 *      its position, without recursion (a list keeps its parent while it is
 *      open).
 *   2. The builder walks the node lists in document order, again without
 *      recursion: for each node it sorts the items into attributes, a literal
 *      and child nodes (marking the children for the walk), creates the node,
 *      sets its dropped marks, appends it to its parent, records its layout,
 *      values and styles, adds its content tokens and checks the values derived
 *      from its spelling. A "(document ...)" node is the document node: its
 *      attributes are applied after its children.
 * The first failure is kept with its position; the nodes made so far are then
 * destroyed. The memory of the builder itself (the atoms and the reason) comes
 * from the C library, so that the test allocator sees only the allocations of
 * Amark.
 */
#include "tree_builder.h"

#include <amark/extension.h>

#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#define BUILDER_PRINTF(format_index, first_arg) \
    __attribute__((format(printf, format_index, first_arg)))
#else
#define BUILDER_PRINTF(format_index, first_arg)
#endif

/* ---- Atoms and lists ------------------------------------------------------------ */

typedef enum sx_kind
{
    SX_LIST,
    SX_STRING,
    SX_KEYWORD, /* text: the name, without the ":" */
    SX_SYMBOL,
    SX_INTEGER /* text: the digits; number, unless out_of_range */
} sx_kind_t;

typedef struct sx sx_t;

struct sx
{
    sx_kind_t kind;
    /* The position of the first character, from 1. */
    unsigned long line;
    unsigned long column;
    /* The bytes of a string (escapes decoded), the name of a keyword, the text of
     * a symbol or an integer; terminated by NUL. */
    char *text;
    size_t length;
    int64_t number;
    bool out_of_range;
    /* The relations of a list and its items. */
    sx_t *parent;
    sx_t *first;
    sx_t *last;
    sx_t *next;
    /* The chain of every sx_t, for the release. */
    sx_t *all_next;
    /* A node list: whether it is a child node of its parent list, and the node
     * built from it. */
    bool is_child;
    amark_node_t *node;
};

/* The attributes of a node list, sorted by build_one. */
typedef struct attrs
{
    sx_t *lines;
    sx_t *margins;
    sx_t *blanks;
    sx_t *tokens;
    sx_t *content_form; /* laid or free */
    sx_t *content;
    sx_t *spelling;
    sx_t *dropped;
    sx_t *pos;
    sx_t *line_table;
    /* The values given for each facet (index = facet number), with their keys
     * (NULL for the literal). */
    sx_t *values[AMARK_FACET_MAX + 1];
    /* Whether a token spells the facet: its value is derived, so it is checked
     * instead of recorded. */
    bool spelled[AMARK_FACET_MAX + 1];
} attrs_t;

typedef struct builder
{
    amark_document_t *document;
    const amark_syntax_set_t *set;
    /* The reader. */
    const char *text;
    size_t offset;
    unsigned long line;
    unsigned long column;
    sx_t *all;
    /* The first failure. */
    amark_error_t error;
    char *reason;
} builder_t;

/* ---- Failures ------------------------------------------------------------------- */

static char *format_text(const char *format, va_list args)
{
    va_list copy;
    int length;
    char *text;

    va_copy(copy, args);
    length = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (length < 0)
    {
        return NULL;
    }
    text = (char *)malloc((size_t)length + 1);
    if (text != NULL)
    {
        (void)vsnprintf(text, (size_t)length + 1, format, args);
    }
    return text;
}

/* Records the first failure: error, at line and column (0 for none), with the
 * message. Always returns false, for the callers to return. */
BUILDER_PRINTF(5, 6)
static bool fail_at(builder_t *b, amark_error_t error, unsigned long line, unsigned long column,
                    const char *format, ...)
{
    va_list args;
    char *message;
    size_t length;

    if (b->error != AMARK_ERROR_NONE)
    {
        return false;
    }
    b->error = error;
    va_start(args, format);
    message = format_text(format, args);
    va_end(args);
    if (message == NULL)
    {
        return false;
    }
    length = strlen(message) + 64;
    b->reason = (char *)malloc(length);
    if (b->reason != NULL)
    {
        if (line > 0)
        {
            (void)snprintf(b->reason, length, "line %lu, column %lu: %s", line, column, message);
        }
        else
        {
            (void)snprintf(b->reason, length, "%s", message);
        }
    }
    free(message);
    return false;
}

#define FAIL(b, sx, ...) \
    fail_at((b), AMARK_ERROR_INVALID_ARGUMENT, (sx)->line, (sx)->column, __VA_ARGS__)

/* A failure of an Amark function at sx: error, with what it refused. */
static bool refused(builder_t *b, const sx_t *sx, amark_error_t error, const char *function,
                    const char *what)
{
    return fail_at(b, error, sx->line, sx->column, "%s refused %s: %s", function, what,
                   amark_error_string(error));
}

/* A short description of sx for the messages. */
static const char *describe(const sx_t *sx)
{
    switch (sx->kind)
    {
    case SX_LIST:
        return "a list";
    case SX_STRING:
        return "a string";
    case SX_KEYWORD:
        return "a keyword";
    case SX_SYMBOL:
        return "a symbol";
    case SX_INTEGER:
        return "an integer";
    }
    return "an item";
}

/* ---- The reader ----------------------------------------------------------------- */

static sx_t *new_sx(builder_t *b, sx_kind_t kind, unsigned long line, unsigned long column)
{
    sx_t *sx = (sx_t *)calloc(1, sizeof(*sx));

    if (sx == NULL)
    {
        (void)fail_at(b, AMARK_ERROR_OUT_OF_MEMORY, 0, 0, "out of memory");
        return NULL;
    }
    sx->kind = kind;
    sx->line = line;
    sx->column = column;
    sx->all_next = b->all;
    b->all = sx;
    return sx;
}

static void append_item(sx_t *list, sx_t *item)
{
    item->parent = list;
    if (list->last != NULL)
    {
        list->last->next = item;
    }
    else
    {
        list->first = item;
    }
    list->last = item;
}

static void free_all(builder_t *b)
{
    while (b->all != NULL)
    {
        sx_t *next = b->all->all_next;

        free(b->all->text);
        free(b->all);
        b->all = next;
    }
}

static char peek(const builder_t *b)
{
    return b->text[b->offset];
}

static void advance(builder_t *b)
{
    if (b->text[b->offset] == '\n')
    {
        b->line++;
        b->column = 1;
    }
    else
    {
        b->column++;
    }
    b->offset++;
}

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static bool is_symbol_char(char c)
{
    return c != '\0' && !is_space(c) && c != '(' && c != ')' && c != '"' && c != ';';
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return -1;
}

/* Reads a string from its opening quote into sx. */
static bool read_string(builder_t *b, sx_t *sx)
{
    size_t capacity = 16;
    size_t length = 0;
    char *text = (char *)malloc(capacity);

    if (text == NULL)
    {
        return fail_at(b, AMARK_ERROR_OUT_OF_MEMORY, 0, 0, "out of memory");
    }
    sx->text = text;
    advance(b); /* '"' */
    for (;;)
    {
        char c = peek(b);
        char byte;

        if (c == '\0')
        {
            return FAIL(b, sx, "unterminated string");
        }
        if (c == '"')
        {
            advance(b);
            break;
        }
        if (c == '\\')
        {
            unsigned long line = b->line;
            unsigned long column = b->column;
            char e;

            advance(b);
            e = peek(b);
            switch (e)
            {
            case '"':
            case '\\':
                byte = e;
                advance(b);
                break;
            case 'n':
                byte = '\n';
                advance(b);
                break;
            case 'r':
                byte = '\r';
                advance(b);
                break;
            case 't':
                byte = '\t';
                advance(b);
                break;
            case 'x': {
                int high;
                int low;

                advance(b);
                high = hex_value(peek(b));
                if (high >= 0)
                {
                    advance(b);
                }
                low = hex_value(peek(b));
                if (high < 0 || low < 0)
                {
                    return fail_at(b, AMARK_ERROR_INVALID_ARGUMENT, line, column,
                                   "\\x escape without two hexadecimal digits");
                }
                advance(b);
                byte = (char)(unsigned char)(high * 16 + low);
                break;
            }
            default:
                return fail_at(b, AMARK_ERROR_INVALID_ARGUMENT, line, column,
                               "unknown escape in a string");
            }
        }
        else
        {
            byte = c;
            advance(b);
        }
        if (length + 1 >= capacity)
        {
            char *bigger = (char *)realloc(text, capacity * 2);

            if (bigger == NULL)
            {
                return fail_at(b, AMARK_ERROR_OUT_OF_MEMORY, 0, 0, "out of memory");
            }
            text = bigger;
            sx->text = text;
            capacity *= 2;
        }
        text[length++] = byte;
    }
    text[length] = '\0';
    sx->length = length;
    return true;
}

/* Reads a keyword, a symbol or an integer into sx: the run of symbol characters
 * from the current position (after the ":" of a keyword). */
static bool read_word(builder_t *b, sx_t *sx)
{
    size_t start = b->offset;
    size_t length;
    size_t i;

    while (is_symbol_char(peek(b)))
    {
        advance(b);
    }
    length = b->offset - start;
    sx->text = (char *)malloc(length + 1);
    if (sx->text == NULL)
    {
        return fail_at(b, AMARK_ERROR_OUT_OF_MEMORY, 0, 0, "out of memory");
    }
    memcpy(sx->text, b->text + start, length);
    sx->text[length] = '\0';
    sx->length = length;
    if (sx->kind == SX_KEYWORD)
    {
        return length > 0 ? true : FAIL(b, sx, "a keyword without a name");
    }
    /* An integer: an optional "-" and decimal digits. */
    i = (length > 0 && sx->text[0] == '-') ? 1 : 0;
    if (i == length)
    {
        return true;
    }
    for (; i < length; i++)
    {
        if (sx->text[i] < '0' || sx->text[i] > '9')
        {
            return true;
        }
    }
    sx->kind = SX_INTEGER;
    {
        bool negative = sx->text[0] == '-';
        uint64_t magnitude = 0;
        uint64_t limit = negative ? (uint64_t)INT64_MAX + 1u : (uint64_t)INT64_MAX;

        for (i = negative ? 1 : 0; i < length; i++)
        {
            unsigned digit = (unsigned)(sx->text[i] - '0');

            if (magnitude > (limit - digit) / 10u)
            {
                sx->out_of_range = true;
                return true;
            }
            magnitude = magnitude * 10u + digit;
        }
        if (negative)
        {
            sx->number = magnitude == (uint64_t)INT64_MAX + 1u ? INT64_MIN : -(int64_t)magnitude;
        }
        else
        {
            sx->number = (int64_t)magnitude;
        }
    }
    return true;
}

/* Reads the whole text into the items of top. */
static bool read_all(builder_t *b, sx_t *top)
{
    sx_t *open = top;

    for (;;)
    {
        char c = peek(b);
        sx_t *sx;

        if (is_space(c))
        {
            advance(b);
            continue;
        }
        if (c == ';')
        {
            while (peek(b) != '\0' && peek(b) != '\n')
            {
                advance(b);
            }
            continue;
        }
        if (c == '\0')
        {
            break;
        }
        if (c == ')')
        {
            if (open == top)
            {
                return fail_at(b, AMARK_ERROR_INVALID_ARGUMENT, b->line, b->column,
                               "unexpected ) without an open list");
            }
            advance(b);
            open = open->parent;
            continue;
        }
        sx = new_sx(b,
                    c == '(' ? SX_LIST
                    : c == '"' ? SX_STRING
                    : c == ':' ? SX_KEYWORD
                               : SX_SYMBOL,
                    b->line, b->column);
        if (sx == NULL)
        {
            return false;
        }
        append_item(open, sx);
        if (c == '(')
        {
            advance(b);
            open = sx;
        }
        else if (c == '"')
        {
            if (!read_string(b, sx))
            {
                return false;
            }
        }
        else
        {
            if (c == ':')
            {
                advance(b);
            }
            if (!read_word(b, sx))
            {
                return false;
            }
        }
    }
    if (open != top)
    {
        return FAIL(b, open, "unclosed ( at the end of the text");
    }
    return true;
}

/* ---- Reading items -------------------------------------------------------------- */

static bool is_symbol(const sx_t *sx, const char *name)
{
    return sx != NULL && sx->kind == SX_SYMBOL && strcmp(sx->text, name) == 0;
}

static size_t item_count(const sx_t *list)
{
    size_t count = 0;
    const sx_t *item;

    for (item = list->first; item != NULL; item = item->next)
    {
        count++;
    }
    return count;
}

/* An integer item from minimum to maximum into *out, or a failure at sx naming
 * what. */
static bool get_integer(builder_t *b, const sx_t *sx, int64_t minimum, int64_t maximum,
                        const char *what, int64_t *out)
{
    if (sx == NULL || sx->kind != SX_INTEGER || sx->out_of_range || sx->number < minimum ||
        sx->number > maximum)
    {
        if (sx == NULL)
        {
            return false;
        }
        return FAIL(b, sx, "expected an integer from %lld to %lld for %s", (long long)minimum,
                    (long long)maximum, what);
    }
    *out = sx->number;
    return true;
}

static bool get_u32(builder_t *b, const sx_t *sx, const char *what, uint32_t *out)
{
    int64_t number = 0;

    if (!get_integer(b, sx, 0, (int64_t)UINT32_MAX, what, &number))
    {
        return false;
    }
    *out = (uint32_t)number;
    return true;
}

/* Reads the first count items of list, which must have from count to
 * count + options items, into items[]; the rest go to items[count...] (NULL when
 * missing). */
static bool get_items(builder_t *b, const sx_t *list, size_t count, size_t options,
                      const char *shape, const sx_t **items)
{
    size_t n;
    const sx_t *item;
    size_t i;

    if (list->kind != SX_LIST)
    {
        return FAIL(b, list, "expected %s, not %s", shape, describe(list));
    }
    n = item_count(list);
    if (n < count || n > count + options)
    {
        return FAIL(b, list, "expected %s", shape);
    }
    for (i = 0; i < count + options; i++)
    {
        items[i] = NULL;
    }
    for (i = 0, item = list->first; item != NULL; i++, item = item->next)
    {
        items[i] = item;
    }
    return true;
}

static bool expect_list(builder_t *b, const sx_t *sx, const char *shape)
{
    return sx->kind == SX_LIST ? true : FAIL(b, sx, "expected %s, not %s", shape, describe(sx));
}

/* The options of a record from item on: [first] (when first_flag is not NULL),
 * [:facet name] (when facet is not NULL) and [:role n]. */
static bool get_options(builder_t *b, const sx_t *item, const amark_node_type_desc_t *desc,
                        uint32_t *first_flag, amark_facet_t *facet, uint16_t *role)
{
    for (; item != NULL; item = item->next)
    {
        if (first_flag != NULL && is_symbol(item, "first"))
        {
            *first_flag = AMARK_MARGIN_FIRST;
            continue;
        }
        if (item->kind == SX_KEYWORD && strcmp(item->text, "role") == 0 && item->next != NULL)
        {
            int64_t number = 0;

            if (!get_integer(b, item->next, 0, UINT16_MAX, ":role", &number))
            {
                return false;
            }
            *role = (uint16_t)number;
            item = item->next;
            continue;
        }
        if (facet != NULL && item->kind == SX_KEYWORD && strcmp(item->text, "facet") == 0 &&
            item->next != NULL)
        {
            if (item->next->kind != SX_SYMBOL)
            {
                return FAIL(b, item->next, "expected the name of a facet");
            }
            *facet = amark_node_type_find_facet(desc, item->next->text);
            if (*facet == AMARK_FACET_NONE)
            {
                return FAIL(b, item->next, "%s has no facet %s", desc->name, item->next->text);
            }
            item = item->next;
            continue;
        }
        return FAIL(b, item, "unexpected %s %s in a record", describe(item),
                    item->kind == SX_LIST ? "" : item->text);
    }
    return true;
}

static const char *const spell_kind_names[] = {"text",     "marker",     "space",
                                               "delimiter", "escape",    "char_ref",
                                               "replaced", "line_break", "tab_remainder"};

static bool get_spell_kind(builder_t *b, const sx_t *sx, amark_spell_kind_t *out)
{
    size_t i;

    if (sx->kind == SX_SYMBOL)
    {
        for (i = 0; i < sizeof(spell_kind_names) / sizeof(spell_kind_names[0]); i++)
        {
            if (strcmp(sx->text, spell_kind_names[i]) == 0)
            {
                *out = (amark_spell_kind_t)(AMARK_SPELL_TEXT + (int)i);
                return true;
            }
        }
    }
    return FAIL(b, sx, "unknown token kind %s", sx->kind == SX_LIST ? "(list)" : sx->text);
}

/* ---- Recording ------------------------------------------------------------------ */

static bool record_lines(builder_t *b, amark_node_t *node, const sx_t *list)
{
    const sx_t *items[2];
    uint32_t first = 0;
    uint32_t count = 0;
    amark_error_t error;

    if (!get_items(b, list, 2, 0, "(first count)", items) ||
        !get_u32(b, items[0], "the first line", &first) ||
        !get_u32(b, items[1], "the count of lines", &count))
    {
        return false;
    }
    error = amark_block_set_lines(node, first, count);
    if (error != AMARK_ERROR_NONE)
    {
        return refused(b, list, error, "amark_block_set_lines", "the lines");
    }
    return true;
}

static bool record_margins(builder_t *b, amark_node_t *node, const sx_t *list)
{
    const sx_t *record;

    if (!expect_list(b, list, "((line start end present|lazy [first] [:role n]) ...)"))
    {
        return false;
    }
    for (record = list->first; record != NULL; record = record->next)
    {
        const sx_t *items[4];
        amark_margin_t margin;
        amark_error_t error;

        memset(&margin, 0, sizeof(margin));
        margin.struct_size = sizeof(margin);
        if (record->kind != SX_LIST || item_count(record) < 4)
        {
            return FAIL(b, record, "expected (line start end present|lazy [first] [:role n])");
        }
        items[0] = record->first;
        items[1] = items[0]->next;
        items[2] = items[1]->next;
        items[3] = items[2]->next;
        if (!get_u32(b, items[0], "the line", &margin.line) ||
            !get_u32(b, items[1], "the start column", &margin.column_start) ||
            !get_u32(b, items[2], "the end column", &margin.column_end))
        {
            return false;
        }
        if (is_symbol(items[3], "present"))
        {
            margin.state = AMARK_MARGIN_PRESENT;
        }
        else if (is_symbol(items[3], "lazy"))
        {
            margin.state = AMARK_MARGIN_LAZY;
        }
        else
        {
            return FAIL(b, items[3], "expected present or lazy");
        }
        if (!get_options(b, items[3]->next, NULL, &margin.flags, NULL, &margin.role))
        {
            return false;
        }
        error = amark_block_add_margin(node, &margin);
        if (error != AMARK_ERROR_NONE)
        {
            return refused(b, record, error, "amark_block_add_margin", "the margin");
        }
    }
    return true;
}

static bool record_blanks(builder_t *b, amark_node_t *node, const sx_t *list)
{
    const sx_t *record;

    if (!expect_list(b, list, "((line start end) ...)"))
    {
        return false;
    }
    for (record = list->first; record != NULL; record = record->next)
    {
        const sx_t *items[3];
        amark_blank_line_t blank;
        amark_error_t error;

        memset(&blank, 0, sizeof(blank));
        blank.struct_size = sizeof(blank);
        if (!get_items(b, record, 3, 0, "(line start end)", items) ||
            !get_u32(b, items[0], "the line", &blank.line) ||
            !get_u32(b, items[1], "the start column", &blank.column_start) ||
            !get_u32(b, items[2], "the end column", &blank.column_end))
        {
            return false;
        }
        error = amark_block_add_blank_line(node, &blank);
        if (error != AMARK_ERROR_NONE)
        {
            return refused(b, record, error, "amark_block_add_blank_line", "the blank line");
        }
    }
    return true;
}

static bool record_tokens(builder_t *b, amark_node_t *node, const amark_node_type_desc_t *desc,
                          const sx_t *list)
{
    const sx_t *record;

    if (!expect_list(b, list, "((kind line start end [:facet name] [:role n]) ...)"))
    {
        return false;
    }
    for (record = list->first; record != NULL; record = record->next)
    {
        const sx_t *items[4];
        amark_layout_token_t token;
        amark_error_t error;

        memset(&token, 0, sizeof(token));
        token.struct_size = sizeof(token);
        if (record->kind != SX_LIST || item_count(record) < 4)
        {
            return FAIL(b, record, "expected (kind line start end [:facet name] [:role n])");
        }
        items[0] = record->first;
        items[1] = items[0]->next;
        items[2] = items[1]->next;
        items[3] = items[2]->next;
        if (!get_spell_kind(b, items[0], &token.kind) ||
            !get_u32(b, items[1], "the line", &token.line) ||
            !get_u32(b, items[2], "the start column", &token.column_start) ||
            !get_u32(b, items[3], "the end column", &token.column_end) ||
            !get_options(b, items[3]->next, desc, NULL, &token.facet, &token.role))
        {
            return false;
        }
        error = amark_block_add_token(node, &token);
        if (error != AMARK_ERROR_NONE)
        {
            return refused(b, record, error, "amark_block_add_token", "the layout token");
        }
    }
    return true;
}

static bool record_content(builder_t *b, amark_node_t *node, const sx_t *form, const sx_t *value)
{
    amark_error_t error;

    if (is_symbol(form, "free"))
    {
        if (value->kind != SX_STRING)
        {
            return FAIL(b, value, "expected the string of the free content");
        }
        error = amark_block_set_free_content(node, value->text, value->length);
        return error == AMARK_ERROR_NONE
                   ? true
                   : refused(b, value, error, "amark_block_set_free_content", "the free content");
    }
    if (!expect_list(b, value, "((line start end) ...)"))
    {
        return false;
    }
    for (value = value->first; value != NULL; value = value->next)
    {
        const sx_t *items[3];
        uint32_t line = 0;
        uint32_t start = 0;
        uint32_t end = 0;

        if (!get_items(b, value, 3, 0, "(line start end)", items) ||
            !get_u32(b, items[0], "the line", &line) ||
            !get_u32(b, items[1], "the start column", &start) ||
            !get_u32(b, items[2], "the end column", &end))
        {
            return false;
        }
        error = amark_block_add_content(node, line, start, end);
        if (error != AMARK_ERROR_NONE)
        {
            return refused(b, value, error, "amark_block_add_content", "the content piece");
        }
    }
    return true;
}

/* The nearest block with content among node and its ancestors, or NULL. */
static const amark_node_t *leaf_of(const amark_node_t *node)
{
    while (node != NULL && !(amark_node_is_block(node) && amark_node_has_content(node)))
    {
        node = amark_node_parent(node);
    }
    return node;
}

static bool record_spelling(builder_t *b, amark_node_t *node, const amark_node_type_desc_t *desc,
                            const sx_t *list)
{
    const amark_node_t *leaf = leaf_of(node);
    const sx_t *record;

    if (!expect_list(b, list, "((kind start end [:facet name] [:role n]) ...)"))
    {
        return false;
    }
    for (record = list->first; record != NULL; record = record->next)
    {
        const sx_t *items[3];
        amark_content_token_t token;
        int64_t start = 0;
        int64_t end = 0;
        amark_error_t error;

        memset(&token, 0, sizeof(token));
        token.struct_size = sizeof(token);
        if (record->kind != SX_LIST || item_count(record) < 3)
        {
            return FAIL(b, record, "expected (kind start end [:facet name] [:role n])");
        }
        items[0] = record->first;
        items[1] = items[0]->next;
        items[2] = items[1]->next;
        if (!get_spell_kind(b, items[0], &token.kind) ||
            !get_integer(b, items[1], 0, INT64_MAX, "the start", &start) ||
            !get_integer(b, items[2], 0, INT64_MAX, "the end", &end) ||
            !get_options(b, items[2]->next, desc, NULL, &token.facet, &token.role))
        {
            return false;
        }
        token.start = (size_t)start;
        token.end = (size_t)end;
        if (leaf == NULL)
        {
            return FAIL(b, record, "no block with content holds the content token");
        }
        error = amark_node_add_content_token(node, leaf, &token);
        if (error != AMARK_ERROR_NONE)
        {
            return refused(b, record, error, "amark_node_add_content_token", "the content token");
        }
    }
    return true;
}

/* Marks the facets that the tokens of a record list spell (:facet name). */
static void mark_spelled(const sx_t *list, const amark_node_type_desc_t *desc, attrs_t *attrs)
{
    const sx_t *record;

    if (list == NULL || list->kind != SX_LIST)
    {
        return;
    }
    for (record = list->first; record != NULL; record = record->next)
    {
        const sx_t *item;

        if (record->kind != SX_LIST)
        {
            continue;
        }
        for (item = record->first; item != NULL && item->next != NULL; item = item->next)
        {
            if (item->kind == SX_KEYWORD && strcmp(item->text, "facet") == 0 &&
                item->next->kind == SX_SYMBOL)
            {
                attrs->spelled[amark_node_type_find_facet(desc, item->next->text)] = true;
            }
        }
    }
}

/* The number of an INT, ENUM or BOOL value item for facet. */
static bool get_number(builder_t *b, const sx_t *value, const amark_facet_desc_t *facet,
                       int64_t *out)
{
    size_t i;

    switch (facet->kind)
    {
    case AMARK_FACET_INT:
        return get_integer(b, value, INT64_MIN, INT64_MAX, facet->name, out);
    case AMARK_FACET_BOOL:
        if (is_symbol(value, "true") || is_symbol(value, "false"))
        {
            *out = is_symbol(value, "true") ? 1 : 0;
            return true;
        }
        return FAIL(b, value, "expected true or false for %s", facet->name);
    case AMARK_FACET_ENUM:
        if (value->kind == SX_SYMBOL || value->kind == SX_STRING)
        {
            for (i = 0; i < facet->enum_count; i++)
            {
                if (strlen(facet->enum_names[i]) == value->length &&
                    memcmp(facet->enum_names[i], value->text, value->length) == 0)
                {
                    *out = (int64_t)i + 1;
                    return true;
                }
            }
        }
        return FAIL(b, value, "unknown value %s of %s",
                    value->kind == SX_LIST ? "(list)" : value->text, facet->name);
    case AMARK_FACET_STRING:
        break;
    }
    return FAIL(b, value, "unexpected value of %s", facet->name);
}

static bool record_value(builder_t *b, amark_node_t *node, amark_facet_t number,
                         const amark_facet_desc_t *facet, const sx_t *value)
{
    amark_error_t error;

    if (facet->kind == AMARK_FACET_STRING)
    {
        if (value->kind != SX_STRING)
        {
            return FAIL(b, value, "expected a string for %s", facet->name);
        }
        error = amark_node_record_string(node, number, value->text, value->length);
    }
    else
    {
        int64_t n = 0;

        if (!get_number(b, value, facet, &n))
        {
            return false;
        }
        error = amark_node_record_int(node, number, n);
    }
    if (error != AMARK_ERROR_NONE)
    {
        return fail_at(b, error, value->line, value->column,
                       "amark_node_record_%s refused the value of %s: %s",
                       facet->kind == AMARK_FACET_STRING ? "string" : "int", facet->name,
                       amark_error_string(error));
    }
    return true;
}

/* Writes text escaped (at most size - 1 bytes) into out, for the messages. */
static void escape_into(const char *text, size_t length, char *out, size_t size)
{
    size_t used = 0;
    size_t i;

    for (i = 0; i < length && used + 5 < size; i++)
    {
        unsigned char c = (unsigned char)text[i];

        if (c == '"' || c == '\\')
        {
            out[used++] = '\\';
            out[used++] = (char)c;
        }
        else if (c == '\n')
        {
            out[used++] = '\\';
            out[used++] = 'n';
        }
        else if (c < 0x20 || c == 0x7F)
        {
            used += (size_t)snprintf(out + used, size - used, "\\x%02X", (unsigned)c);
        }
        else
        {
            out[used++] = (char)c;
        }
    }
    out[used] = '\0';
}

/* Checks that the value read back for facet equals the value item. */
static bool check_value(builder_t *b, const amark_node_t *node, amark_facet_t number,
                        const amark_facet_desc_t *facet, const sx_t *value)
{
    amark_value_t current;
    amark_error_t error;
    char shown[128];

    memset(&current, 0, sizeof(current));
    current.struct_size = sizeof(current);
    error = amark_node_get(node, number, &current);
    if (error != AMARK_ERROR_NONE)
    {
        return fail_at(b, error, value->line, value->column, "amark_node_get failed for %s: %s",
                       facet->name, amark_error_string(error));
    }
    if (facet->kind == AMARK_FACET_STRING)
    {
        if (value->kind != SX_STRING)
        {
            return FAIL(b, value, "expected a string for %s", facet->name);
        }
        if (current.state == AMARK_VALUE_PRESENT && current.length == value->length &&
            memcmp(current.text, value->text, value->length) == 0)
        {
            return true;
        }
        escape_into(current.text != NULL ? current.text : "", current.length, shown,
                    sizeof(shown));
        return FAIL(b, value, "the value of %s differs from the value derived from the spelling, "
                              "\"%s\"",
                    facet->name, shown);
    }
    else
    {
        int64_t n = 0;

        if (!get_number(b, value, facet, &n))
        {
            return false;
        }
        if (current.state == AMARK_VALUE_PRESENT && current.number == n)
        {
            return true;
        }
        return FAIL(b, value, "the value of %s differs from the value derived from the spelling, "
                              "%lld",
                    facet->name, (long long)current.number);
    }
}

static bool set_dropped(builder_t *b, amark_node_t *node, const amark_node_type_desc_t *desc,
                        const sx_t *list)
{
    const sx_t *item;

    if (!expect_list(b, list, "(name ...)"))
    {
        return false;
    }
    for (item = list->first; item != NULL; item = item->next)
    {
        amark_error_t error;
        amark_facet_t facet = AMARK_FACET_NONE;

        if (is_symbol(item, "structure"))
        {
            error = amark_node_discard_spelling(node);
        }
        else
        {
            if (item->kind == SX_SYMBOL)
            {
                facet = amark_node_type_find_facet(desc, item->text);
            }
            if (facet == AMARK_FACET_NONE)
            {
                return FAIL(b, item, "unknown spelling %s of %s (structure or a facet)",
                            item->kind == SX_LIST ? "(list)" : item->text, desc->name);
            }
            error = amark_node_discard_facet_spelling(node, facet);
        }
        if (error != AMARK_ERROR_NONE)
        {
            return refused(b, item, error, "the discarding of the spelling", "the dropped mark");
        }
    }
    return true;
}

static const char *const eol_names[] = {"none", "lf", "crlf", "cr"};

static bool check_line_table(builder_t *b, const sx_t *list)
{
    size_t count = amark_document_line_count(b->document);
    const sx_t *entry;
    size_t index = 0;

    if (!expect_list(b, list, "((source number offset length eol width) ...)"))
    {
        return false;
    }
    if (item_count(list) != count)
    {
        return FAIL(b, list, "the line table has %lu lines, not %lu", (unsigned long)count,
                    (unsigned long)item_count(list));
    }
    for (entry = list->first; entry != NULL; entry = entry->next, index++)
    {
        const sx_t *items[6];
        amark_line_info_t info;
        int64_t numbers[4] = {0, 0, 0, 0};
        uint32_t width = 0;
        size_t i;
        amark_error_t error;

        if (!get_items(b, entry, 6, 0, "(source number offset length eol width)", items))
        {
            return false;
        }
        for (i = 0; i < 4; i++)
        {
            if (!get_integer(b, items[i], 0, INT64_MAX, "the line table", &numbers[i]))
            {
                return false;
            }
        }
        if (!get_u32(b, items[5], "the width", &width))
        {
            return false;
        }
        memset(&info, 0, sizeof(info));
        info.struct_size = sizeof(info);
        error = amark_document_line(b->document, (amark_line_id_t)index, &info);
        if (error != AMARK_ERROR_NONE)
        {
            return refused(b, entry, error, "amark_document_line", "the line");
        }
        if ((int64_t)info.source != numbers[0] || (int64_t)info.number != numbers[1] ||
            (int64_t)info.offset != numbers[2] || (int64_t)info.length != numbers[3] ||
            info.eol < AMARK_EOL_NONE || info.eol > AMARK_EOL_CR ||
            !is_symbol(items[4], eol_names[info.eol - AMARK_EOL_NONE]) || info.width != width)
        {
            return FAIL(b, entry,
                        "the line %lu differs from the line table, (%lu %lu %lu %lu %s %lu)",
                        (unsigned long)index, (unsigned long)info.source,
                        (unsigned long)info.number, (unsigned long)info.offset,
                        (unsigned long)info.length,
                        (info.eol >= AMARK_EOL_NONE && info.eol <= AMARK_EOL_CR)
                            ? eol_names[info.eol - AMARK_EOL_NONE]
                            : "?",
                        (unsigned long)info.width);
        }
    }
    return true;
}

/* ---- Building ------------------------------------------------------------------- */

/* Sorts the items of the node list sx (after its type name) into attrs, and marks
 * its child nodes. */
static bool sort_items(builder_t *b, sx_t *sx, const amark_node_type_desc_t *desc, attrs_t *attrs)
{
    sx_t *item;

    memset(attrs, 0, sizeof(*attrs));
    for (item = sx->first->next; item != NULL; item = item->next)
    {
        if (item->kind == SX_LIST)
        {
            item->is_child = true;
            continue;
        }
        if (item->kind == SX_STRING)
        {
            amark_facet_t facet = amark_node_type_find_facet(desc, "literal");

            if (facet == AMARK_FACET_NONE)
            {
                return FAIL(b, item, "%s has no literal", desc->name);
            }
            if (attrs->values[facet] != NULL)
            {
                return FAIL(b, item, "a second literal of %s", desc->name);
            }
            attrs->values[facet] = item;
            continue;
        }
        if (item->kind != SX_KEYWORD)
        {
            return FAIL(b, item, "unexpected %s %s in a node", describe(item), item->text);
        }
        if (item->next == NULL)
        {
            return FAIL(b, item, ":%s has no value", item->text);
        }
        {
            static const char *const names[] = {"lines",    "margins", "blanks",
                                                "tokens",   "spelling", "dropped",
                                                "pos",      "line_table"};
            sx_t **slots[8];
            size_t i;
            bool found = false;

            slots[0] = &attrs->lines;
            slots[1] = &attrs->margins;
            slots[2] = &attrs->blanks;
            slots[3] = &attrs->tokens;
            slots[4] = &attrs->spelling;
            slots[5] = &attrs->dropped;
            slots[6] = &attrs->pos;
            slots[7] = &attrs->line_table;
            for (i = 0; i < 8; i++)
            {
                if (strcmp(item->text, names[i]) == 0)
                {
                    if (*slots[i] != NULL)
                    {
                        return FAIL(b, item, ":%s twice", item->text);
                    }
                    *slots[i] = item->next;
                    found = true;
                    break;
                }
            }
            if (found)
            {
                item = item->next;
                continue;
            }
        }
        if (strcmp(item->text, "content") == 0)
        {
            sx_t *form = item->next;

            if (attrs->content_form != NULL)
            {
                return FAIL(b, item, ":content twice");
            }
            if (!is_symbol(form, "laid") && !is_symbol(form, "free"))
            {
                return FAIL(b, form, "expected laid or free after :content");
            }
            if (form->next == NULL)
            {
                return FAIL(b, item, ":content %s has no value", form->text);
            }
            attrs->content_form = form;
            attrs->content = form->next;
            item = form->next;
            continue;
        }
        if (strcmp(item->text, "style") == 0)
        {
            sx_t *style = item->next;
            sx_t *key;

            if (!expect_list(b, style, "(:name value ...)"))
            {
                return false;
            }
            for (key = style->first; key != NULL; key = key->next->next)
            {
                amark_facet_t facet = AMARK_FACET_NONE;
                const amark_facet_desc_t *facet_desc;

                if (key->kind != SX_KEYWORD)
                {
                    return FAIL(b, key, "expected :name value in :style");
                }
                if (key->next == NULL)
                {
                    return FAIL(b, key, ":%s has no value", key->text);
                }
                facet = amark_node_type_find_facet(desc, key->text);
                facet_desc = amark_node_type_facet(desc, facet);
                if (facet_desc == NULL || facet_desc->role != AMARK_FACET_STYLE)
                {
                    return FAIL(b, key, ":%s is not a style of %s", key->text, desc->name);
                }
                if (attrs->values[facet] != NULL)
                {
                    return FAIL(b, key, ":%s twice", key->text);
                }
                attrs->values[facet] = key->next;
            }
            item = style;
            continue;
        }
        {
            amark_facet_t facet = amark_node_type_find_facet(desc, item->text);
            const amark_facet_desc_t *facet_desc = amark_node_type_facet(desc, facet);

            if (facet_desc == NULL)
            {
                return FAIL(b, item, "unknown attribute :%s of %s", item->text, desc->name);
            }
            if (facet_desc->role == AMARK_FACET_STYLE)
            {
                return FAIL(b, item, ":%s is a style of %s: give it in :style", item->text,
                            desc->name);
            }
            if (attrs->values[facet] != NULL)
            {
                return FAIL(b, item, ":%s twice", item->text);
            }
            attrs->values[facet] = item->next;
            item = item->next;
        }
    }
    mark_spelled(attrs->tokens, desc, attrs);
    mark_spelled(attrs->spelling, desc, attrs);
    /* An empty CONTENT value of a node without :content is what a leaf without
     * content reads (the dump writes it so): it is checked, not recorded, so
     * that the leaf keeps no content. */
    {
        amark_facet_t facet;

        for (facet = 1; facet <= desc->facet_count && facet <= AMARK_FACET_MAX; facet++)
        {
            const amark_facet_desc_t *facet_desc = amark_node_type_facet(desc, facet);

            if ((facet_desc->flags & AMARK_FACET_CONTENT) != 0 && attrs->content == NULL &&
                attrs->values[facet] != NULL && attrs->values[facet]->kind == SX_STRING &&
                attrs->values[facet]->length == 0)
            {
                attrs->spelled[facet] = true;
            }
        }
    }
    return true;
}

/* Records the layout, the values, the styles and the content tokens of node,
 * and checks the values derived from its spelling and the line table. */
static bool apply_attrs(builder_t *b, amark_node_t *node, const amark_node_type_desc_t *desc,
                        const attrs_t *attrs)
{
    amark_facet_t facet;

    if (attrs->line_table != NULL)
    {
        if (amark_node_get_type(node) != AMARK_NODE_DOCUMENT)
        {
            return FAIL(b, attrs->line_table, "only the document node has :line_table");
        }
        if (!check_line_table(b, attrs->line_table))
        {
            return false;
        }
    }
    if (attrs->pos != NULL)
    {
        const sx_t *items[4];
        int64_t number = 0;
        size_t i;

        if (!get_items(b, attrs->pos, 4, 0, "(sl sc el ec)", items))
        {
            return false;
        }
        for (i = 0; i < 4; i++)
        {
            if (!get_integer(b, items[i], 0, INT64_MAX, ":pos", &number))
            {
                return false;
            }
        }
    }
    if ((attrs->lines != NULL && !record_lines(b, node, attrs->lines)) ||
        (attrs->margins != NULL && !record_margins(b, node, attrs->margins)) ||
        (attrs->blanks != NULL && !record_blanks(b, node, attrs->blanks)) ||
        (attrs->tokens != NULL && !record_tokens(b, node, desc, attrs->tokens)) ||
        (attrs->content != NULL && !record_content(b, node, attrs->content_form, attrs->content)))
    {
        return false;
    }
    /* The values and the styles that the spelling does not give. */
    for (facet = 1; facet <= desc->facet_count && facet <= AMARK_FACET_MAX; facet++)
    {
        const amark_facet_desc_t *facet_desc = amark_node_type_facet(desc, facet);

        if (attrs->values[facet] == NULL || attrs->spelled[facet] ||
            ((facet_desc->flags & AMARK_FACET_CONTENT) != 0 && attrs->content != NULL))
        {
            continue;
        }
        if (!record_value(b, node, facet, facet_desc, attrs->values[facet]))
        {
            return false;
        }
    }
    if (attrs->spelling != NULL && !record_spelling(b, node, desc, attrs->spelling))
    {
        return false;
    }
    /* The values the spelling gives are checked. */
    for (facet = 1; facet <= desc->facet_count && facet <= AMARK_FACET_MAX; facet++)
    {
        const amark_facet_desc_t *facet_desc = amark_node_type_facet(desc, facet);

        if (attrs->values[facet] == NULL ||
            !(attrs->spelled[facet] ||
              ((facet_desc->flags & AMARK_FACET_CONTENT) != 0 && attrs->content != NULL)))
        {
            continue;
        }
        if (!check_value(b, node, facet, facet_desc, attrs->values[facet]))
        {
            return false;
        }
    }
    return true;
}

/* The type of the node list sx, or a failure. */
static bool node_type(builder_t *b, const sx_t *sx, amark_node_type_t *type)
{
    const sx_t *name = sx->first;

    if (name == NULL || name->kind != SX_SYMBOL)
    {
        return FAIL(b, name != NULL ? name : sx, "expected a type name at the start of a node");
    }
    if (!amark_syntax_set_find_node_type(b->set, name->text, type))
    {
        return FAIL(b, name, "unknown node type %s", name->text);
    }
    return true;
}

/* Builds the node of the node list sx (not the document node) under parent: the
 * node is created and appended (sx->node), and its own attributes are applied.
 * A node that cannot be appended is destroyed at once; one that fails later is
 * destroyed with the top node it is under. */
static bool build_one(builder_t *b, sx_t *sx, amark_node_t *parent)
{
    amark_node_type_t type = AMARK_NODE_NONE;
    const amark_node_type_desc_t *desc;
    amark_node_t *node = NULL;
    attrs_t attrs;
    amark_error_t error;

    if (!node_type(b, sx, &type))
    {
        return false;
    }
    if (type == AMARK_NODE_DOCUMENT)
    {
        return FAIL(b, sx->first, "a document node can only be the top of the text");
    }
    desc = amark_syntax_set_node_type_desc(b->set, type);
    if (!sort_items(b, sx, desc, &attrs))
    {
        return false;
    }
    error = amark_node_create(b->document, type, &node);
    if (error != AMARK_ERROR_NONE)
    {
        return refused(b, sx, error, "amark_node_create", "the node");
    }
    /* The dropped marks first, on the new node: dropping all of its spelling
     * would also drop its records. */
    if (attrs.dropped != NULL && !set_dropped(b, node, desc, attrs.dropped))
    {
        amark_node_destroy(node);
        return false;
    }
    error = amark_node_append_child(parent, node);
    if (error != AMARK_ERROR_NONE)
    {
        amark_node_destroy(node);
        return refused(b, sx, error, "amark_node_append_child", "to append the node");
    }
    sx->node = node;
    return apply_attrs(b, node, desc, &attrs);
}

/* The first child node of the node list sx after child (from the start when
 * child is NULL), or NULL. */
static sx_t *next_child(const sx_t *sx, const sx_t *child)
{
    sx_t *item = child != NULL ? child->next : sx->first;

    while (item != NULL && !item->is_child)
    {
        item = item->next;
    }
    return item;
}

/* Builds the descendants of the node list top, whose node is already built
 * (top->node), in document order without recursion. */
static bool build_descendants(builder_t *b, sx_t *top)
{
    sx_t *current = top;

    for (;;)
    {
        sx_t *child = next_child(current, NULL);

        if (child != NULL)
        {
            if (!build_one(b, child, current->node))
            {
                return false;
            }
            current = child;
            continue;
        }
        /* Up to the next sibling of current or of one of its ancestors. */
        while (current != top)
        {
            sx_t *sibling = next_child(current->parent, current);

            if (sibling != NULL)
            {
                if (!build_one(b, sibling, current->parent->node))
                {
                    return false;
                }
                current = sibling;
                break;
            }
            current = current->parent;
        }
        if (current == top)
        {
            return true;
        }
    }
}

/* Builds the document node list sx on the document node: its children, then its
 * own attributes. */
static bool build_document(builder_t *b, sx_t *sx, amark_node_t *root)
{
    attrs_t attrs;
    const amark_node_type_desc_t *desc = amark_node_type_desc(root);

    if (!sort_items(b, sx, desc, &attrs))
    {
        return false;
    }
    sx->node = root;
    if (!build_descendants(b, sx))
    {
        return false;
    }
    if (attrs.dropped != NULL && !set_dropped(b, root, desc, attrs.dropped))
    {
        return false;
    }
    return apply_attrs(b, root, desc, &attrs);
}

/* Destroys the nodes built from the items of top. */
static void destroy_built(sx_t *top)
{
    sx_t *item;

    for (item = top->first; item != NULL; item = item->next)
    {
        if (item->node == NULL)
        {
            continue;
        }
        if (amark_node_get_type(item->node) == AMARK_NODE_DOCUMENT)
        {
            sx_t *child;

            for (child = item->first; child != NULL; child = child->next)
            {
                if (child->is_child && child->node != NULL)
                {
                    amark_node_destroy(child->node);
                }
            }
        }
        else
        {
            amark_node_destroy(item->node);
        }
    }
}

static bool build_all(builder_t *b, sx_t *top, amark_node_t *parent)
{
    amark_node_t *root = amark_document_root(b->document);
    sx_t *item;

    if (!read_all(b, top))
    {
        return false;
    }
    if (top->first == NULL)
    {
        return FAIL(b, top, "expected a node");
    }
    for (item = top->first; item != NULL; item = item->next)
    {
        amark_node_type_t type = AMARK_NODE_NONE;

        if (item->kind != SX_LIST)
        {
            return FAIL(b, item, "expected a node, not %s", describe(item));
        }
        if (!node_type(b, item, &type))
        {
            return false;
        }
        if (type == AMARK_NODE_DOCUMENT)
        {
            if (parent != root)
            {
                return FAIL(b, item->first,
                            "a document node can only be built on the document node");
            }
            if (!build_document(b, item, root))
            {
                return false;
            }
            continue;
        }
        if (!build_one(b, item, parent) || !build_descendants(b, item))
        {
            return false;
        }
    }
    return true;
}

amark_error_t amark_test_tree_build(amark_document_t *document, amark_node_t *parent,
                                    const char *sexpr, char **reason)
{
    builder_t b;
    sx_t *top;

    if (reason != NULL)
    {
        *reason = NULL;
    }
    memset(&b, 0, sizeof(b));
    b.error = AMARK_ERROR_NONE;
    if (document == NULL || sexpr == NULL ||
        (parent != NULL && amark_node_document(parent) != document))
    {
        (void)fail_at(&b, AMARK_ERROR_INVALID_ARGUMENT, 0, 0,
                      "the arguments: a NULL document or text, or a parent of another document");
    }
    else
    {
        b.document = document;
        b.set = amark_document_syntax_set(document);
        b.text = sexpr;
        b.line = 1;
        b.column = 1;
        top = new_sx(&b, SX_LIST, 1, 1);
        if (top != NULL)
        {
            if (!build_all(&b, top, parent != NULL ? parent : amark_document_root(document)))
            {
                destroy_built(top);
            }
        }
        free_all(&b);
    }
    if (reason != NULL)
    {
        *reason = b.reason;
    }
    else
    {
        free(b.reason);
    }
    return b.error;
}

void amark_test_tree_free_reason(char *reason)
{
    free(reason);
}
