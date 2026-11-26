/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/* The S-expression dump (amark_node_dump) and the dump writer (amark_dump_attr_*). */

#include <amark/dump.h>

#include <amark/buffer.h>
#include <amark/error.h>
#include <amark/extension.h>
#include <amark/node.h>
#include <amark/token.h>
#include <amark/utf8.h>

#include "tree/syntax_set.h"
#include "tree/tree.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * The writer: the output buffer and the first failure. Every write after a
 * failure does nothing and returns that failure, and amark_node_dump returns it
 * even when the dump function of the extension type ignored it.
 */
struct amark_dump_writer
{
    amark_buffer_t *out;
    amark_error_t error;
};

/* --- Low-level writes --------------------------------------------------------- */

/* Appends length bytes of data, unless the writer has failed. */
static void put(amark_dump_writer_t *writer, const char *data, size_t length)
{
    if (writer->error == AMARK_ERROR_NONE)
    {
        writer->error = amark_buffer_append(writer->out, data, length);
    }
}

/* Appends the NUL-terminated text. */
static void put_cstr(amark_dump_writer_t *writer, const char *text)
{
    put(writer, text, strlen(text));
}

/* Appends one byte. */
static void put_byte(amark_dump_writer_t *writer, char byte)
{
    put(writer, &byte, 1);
}

/* Appends the decimal digits of value. */
static void put_unsigned(amark_dump_writer_t *writer, unsigned long long value)
{
    char digits[24];
    size_t position = sizeof(digits);

    do
    {
        digits[--position] = (char)('0' + (int)(value % 10u));
        value /= 10u;
    }
    while (value != 0u);

    put(writer, &digits[position], sizeof(digits) - position);
}

/* Appends value in decimal, with a '-' when it is negative (LLONG_MIN included). */
static void put_signed(amark_dump_writer_t *writer, long long value)
{
    if (value < 0)
    {
        put_byte(writer, '-');
        /* The magnitude without overflow: -(value + 1) + 1. */
        put_unsigned(writer, (unsigned long long)(-(value + 1)) + 1u);
        return;
    }

    put_unsigned(writer, (unsigned long long)value);
}

/* Appends the escape \xHH (lowercase hexadecimal) of byte. */
static void put_hex_escape(amark_dump_writer_t *writer, unsigned char byte)
{
    static const char hex[] = "0123456789abcdef";
    char escape[4];

    escape[0] = '\\';
    escape[1] = 'x';
    escape[2] = hex[byte >> 4];
    escape[3] = hex[byte & 0x0fu];
    put(writer, escape, sizeof(escape));
}

/* Whether the bytes at text (length bytes) start with the valid encoding of
 * U+FFFD, which amark_utf8_decode also reports for an invalid sequence. */
static bool is_encoded_replacement(const unsigned char *text, size_t length)
{
    return ((length >= 3) && (text[0] == 0xefu) && (text[1] == 0xbfu) && (text[2] == 0xbdu));
}

/*
 * Appends text (length bytes; NULL only with length 0) as a quoted string: '"' is
 * \", '\' is \\, LF is \n, CR is \r, a tab is \t, the other bytes 0x00-0x1F and
 * 0x7F and each byte of an invalid UTF-8 sequence are \xHH; valid UTF-8 is kept.
 * The bytes that need no escape are appended in runs.
 */
static void put_string(amark_dump_writer_t *writer, const char *text, size_t length)
{
    const unsigned char *bytes;
    size_t run = 0;
    size_t i = 0;

    if (text == NULL)
    {
        text = "";
    }

    bytes = (const unsigned char *)text;
    put_byte(writer, '"');
    while (i < length)
    {
        unsigned char byte = bytes[i];
        const char *escape = NULL;
        size_t size = 1;
        bool invalid = false;

        if (byte >= 0x80u)
        {
            uint32_t codepoint = 0;

            size = amark_utf8_decode(text + i, length - i, &codepoint);
            invalid = ((codepoint == AMARK_UTF8_REPLACEMENT)
                       && !is_encoded_replacement(bytes + i, length - i));
            if (!invalid)
            {
                i += size;
                continue;
            }
        }
        else if (byte == '"')
        {
            escape = "\\\"";
        }
        else if (byte == '\\')
        {
            escape = "\\\\";
        }
        else if (byte == '\n')
        {
            escape = "\\n";
        }
        else if (byte == '\r')
        {
            escape = "\\r";
        }
        else if (byte == '\t')
        {
            escape = "\\t";
        }
        else if ((byte < 0x20u) || (byte == 0x7fu))
        {
            invalid = true;
        }
        else
        {
            i++;
            continue;
        }

        put(writer, text + run, i - run);
        if (escape != NULL)
        {
            put_cstr(writer, escape);
        }
        else
        {
            size_t k;

            for (k = 0; k < size; k++)
            {
                put_hex_escape(writer, bytes[i + k]);
            }
        }

        i += size;
        run = i;
    }

    put(writer, text + run, length - run);
    put_byte(writer, '"');
}

/* Appends " :name " (name is already checked). */
static void put_attr_name(amark_dump_writer_t *writer, const char *name)
{
    put(writer, " :", 2);
    put_cstr(writer, name);
    put_byte(writer, ' ');
}

/* Appends a built-in string attribute. */
static void put_builtin_string(amark_dump_writer_t *writer, const char *name,
                               const amark_builtin_string_t *value)
{
    put_attr_name(writer, name);
    put_string(writer, value->text, value->length);
}

/* --- The writer --------------------------------------------------------------- */

/* Whether text matches [a-z][a-z0-9_]*, the grammar of attribute names and of
 * symbols. */
static bool is_identifier(const char *text)
{
    size_t i;

    if ((text == NULL) || (text[0] < 'a') || (text[0] > 'z'))
    {
        return false;
    }

    for (i = 1; text[i] != '\0'; i++)
    {
        char c = text[i];

        if (!(((c >= 'a') && (c <= 'z')) || ((c >= '0') && (c <= '9')) || (c == '_')))
        {
            return false;
        }
    }

    return true;
}

/* The common start of the amark_dump_attr_* functions: checks the writer, its
 * state and the name, recording a failure, and writes the name. */
static amark_error_t begin_attr(amark_dump_writer_t *writer, const char *name)
{
    if (writer == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    if (writer->error != AMARK_ERROR_NONE)
    {
        return writer->error;
    }

    if (!is_identifier(name))
    {
        writer->error = AMARK_ERROR_INVALID_ARGUMENT;
        return writer->error;
    }

    put_attr_name(writer, name);

    return writer->error;
}

amark_error_t amark_dump_attr_string(amark_dump_writer_t *writer, const char *name,
                                     const char *value, size_t length)
{
    amark_error_t err;

    if ((writer != NULL) && (writer->error == AMARK_ERROR_NONE) && (value == NULL)
        && (length != 0))
    {
        writer->error = AMARK_ERROR_INVALID_ARGUMENT;
    }

    err = begin_attr(writer, name);
    if (err != AMARK_ERROR_NONE)
    {
        return err;
    }

    put_string(writer, value, length);

    return writer->error;
}

amark_error_t amark_dump_attr_int(amark_dump_writer_t *writer, const char *name, long long value)
{
    amark_error_t err = begin_attr(writer, name);

    if (err != AMARK_ERROR_NONE)
    {
        return err;
    }

    put_signed(writer, value);

    return writer->error;
}

amark_error_t amark_dump_attr_bool(amark_dump_writer_t *writer, const char *name, bool value)
{
    amark_error_t err = begin_attr(writer, name);

    if (err != AMARK_ERROR_NONE)
    {
        return err;
    }

    put_cstr(writer, value ? "true" : "false");

    return writer->error;
}

amark_error_t amark_dump_attr_symbol(amark_dump_writer_t *writer, const char *name,
                                     const char *symbol)
{
    amark_error_t err;

    if ((writer != NULL) && (writer->error == AMARK_ERROR_NONE) && !is_identifier(symbol))
    {
        writer->error = AMARK_ERROR_INVALID_ARGUMENT;
    }

    err = begin_attr(writer, name);
    if (err != AMARK_ERROR_NONE)
    {
        return err;
    }

    put_cstr(writer, symbol);

    return writer->error;
}

/* --- The attributes of a node ------------------------------------------------- */

/* Writes the built-in attributes of node, by the data layouts of
 * tree/syntax_set.h (the built-in types have no dump function). */
static void put_builtin_attrs(amark_dump_writer_t *writer, const amark_node_t *node)
{
    const amark_node_type_desc_t *desc = node->desc;
    const void *data = amark_node_data_const(node);

    if (desc == amark_builtin_types[AMARK_NODE_HEADING])
    {
        const amark_heading_data_t *heading = data;

        put_attr_name(writer, "level");
        put_signed(writer, heading->level);
    }
    else if (desc == amark_builtin_types[AMARK_NODE_LIST])
    {
        const amark_list_data_t *list = data;
        bool ordered = (list->type == AMARK_LIST_ORDERED);

        put_attr_name(writer, "type");
        put_cstr(writer, ordered ? "ordered" : "bullet");
        if (ordered)
        {
            put_attr_name(writer, "start");
            put_signed(writer, list->start);
        }

        put_attr_name(writer, "tight");
        put_cstr(writer, list->tight ? "true" : "false");
    }
    else if (desc == amark_builtin_types[AMARK_NODE_CODE_BLOCK])
    {
        const amark_code_block_data_t *code_block = data;

        put_builtin_string(writer, "info", &code_block->info);
    }
    else if ((desc == amark_builtin_types[AMARK_NODE_LINK])
             || (desc == amark_builtin_types[AMARK_NODE_IMAGE]))
    {
        const amark_link_data_t *link = data;

        put_builtin_string(writer, "destination", &link->destination);
        put_builtin_string(writer, "title", &link->title);
    }
    else if (desc == amark_builtin_types[AMARK_NODE_LINK_DEFINITION])
    {
        const amark_link_definition_data_t *definition = data;

        put_builtin_string(writer, "label", &definition->label);
        put_builtin_string(writer, "destination", &definition->link.destination);
        put_builtin_string(writer, "title", &definition->link.title);
    }
}

/* The names of the edit record bits, from bit 0 up. */
static const char *const edit_names[] = {
    "content", "children", "descendants", "relocated", "created"
};

/* Writes :edits when node has records: the known bits in order, then the unknown
 * ones as bitN (N the bit number, from 0). */
static void put_edits(amark_dump_writer_t *writer, const amark_node_t *node)
{
    uint32_t edits = node->edits;
    unsigned bit;
    bool first = true;

    if (edits == 0u)
    {
        return;
    }

    put_attr_name(writer, "edits");
    put_byte(writer, '(');
    for (bit = 0; bit < 32u; bit++)
    {
        if ((edits & ((uint32_t)1u << bit)) == 0u)
        {
            continue;
        }

        if (!first)
        {
            put_byte(writer, ' ');
        }

        first = false;
        if (bit < (sizeof(edit_names) / sizeof(edit_names[0])))
        {
            put_cstr(writer, edit_names[bit]);
        }
        else
        {
            put(writer, "bit", 3);
            put_unsigned(writer, bit);
        }
    }

    put_byte(writer, ')');
}

/* Writes :span when the subtree of node has tokens (amark_node_source_span). */
static void put_span(amark_dump_writer_t *writer, const amark_node_t *node)
{
    amark_source_span_t span;

    memset(&span, 0, sizeof(span));
    span.struct_size = sizeof(span);
    if (!amark_node_source_span(node, &span))
    {
        return;
    }

    put_attr_name(writer, "span");
    put_byte(writer, '(');
    put_unsigned(writer, span.start_line);
    put_byte(writer, ' ');
    put_unsigned(writer, span.start_column);
    put_byte(writer, ' ');
    put_unsigned(writer, span.end_line);
    put_byte(writer, ' ');
    put_unsigned(writer, span.end_column);
    put_byte(writer, ')');
}

/* Writes one token: (kind offset length {flag} [:role n] [:source s] [:tab o w]). */
static void put_token(amark_dump_writer_t *writer, const amark_token_record_t *token)
{
    put_byte(writer, '(');
    put_cstr(writer, amark_token_kind_name((amark_token_kind_t)token->kind));
    put_byte(writer, ' ');
    put_unsigned(writer, token->offset);
    put_byte(writer, ' ');
    put_unsigned(writer, token->length);
    if ((token->flags & AMARK_TOKEN_FLAG_CONTAINER_PREFIX) != 0u)
    {
        put(writer, " prefix", 7);
    }

    if ((token->flags & AMARK_TOKEN_FLAG_PARTIAL_TAB) != 0u)
    {
        put(writer, " partial_tab", 12);
    }

    if (token->role != 0u)
    {
        put(writer, " :role ", 7);
        put_unsigned(writer, token->role);
    }

    if (token->source != 0u)
    {
        put(writer, " :source ", 9);
        put_unsigned(writer, token->source);
    }

    if ((token->flags & AMARK_TOKEN_FLAG_PARTIAL_TAB) != 0u)
    {
        put(writer, " :tab ", 6);
        put_unsigned(writer, token->tab_column_offset);
        put_byte(writer, ' ');
        put_unsigned(writer, token->tab_columns);
    }

    put_byte(writer, ')');
}

/* Writes :tokens when node has tokens: a list of the tokens in their order. */
static void put_tokens(amark_dump_writer_t *writer, const amark_node_t *node)
{
    size_t i;

    if (node->token_count == 0)
    {
        return;
    }

    put_attr_name(writer, "tokens");
    put_byte(writer, '(');
    for (i = 0; i < node->token_count; i++)
    {
        if (i > 0)
        {
            put_byte(writer, ' ');
        }

        put_token(writer, &node->tokens[i]);
    }

    put_byte(writer, ')');
}

/*
 * Writes the opening of node: "(", the type name, the attributes in the order of
 * the format and the literal of a type that has a literal mode. Calls the dump
 * function of the type, whose failure becomes the failure of the writer.
 */
static void put_node_head(amark_dump_writer_t *writer, const amark_node_t *node, uint32_t flags)
{
    const amark_node_type_desc_t *desc = node->desc;

    put_byte(writer, '(');
    put_cstr(writer, desc->name);
    put_builtin_attrs(writer, node);
    if ((desc->dump != NULL) && (writer->error == AMARK_ERROR_NONE))
    {
        amark_error_t err = desc->dump(node, amark_node_data_const(node), writer);

        if ((err != AMARK_ERROR_NONE) && (writer->error == AMARK_ERROR_NONE))
        {
            writer->error = err;
        }
    }

    if ((flags & AMARK_DUMP_EDITS) != 0u)
    {
        put_edits(writer, node);
    }

    if ((flags & AMARK_DUMP_SPANS) != 0u)
    {
        put_span(writer, node);
    }

    if ((flags & AMARK_DUMP_TOKENS) != 0u)
    {
        put_tokens(writer, node);
    }

    if (desc->literal_mode != AMARK_LITERAL_NONE)
    {
        put_byte(writer, ' ');
        put_string(writer, node->literal, node->literal_length);
    }
}

/* --- The dump ----------------------------------------------------------------- */

amark_error_t amark_node_dump(const amark_node_t *node, uint32_t flags, amark_buffer_t *out)
{
    amark_dump_writer_t writer;
    const amark_node_t *current = node;
    size_t start_length;
    size_t depth = 0;

    if ((node == NULL) || (out == NULL))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    writer.out = out;
    writer.error = AMARK_ERROR_NONE;
    start_length = amark_buffer_length(out);

    /* A preorder walk without recursion: each node opens on its own line, indented
     * by its depth, and closes after its last child. */
    while (writer.error == AMARK_ERROR_NONE)
    {
        if (current != node)
        {
            put_byte(&writer, '\n');
            if (writer.error == AMARK_ERROR_NONE)
            {
                writer.error = amark_buffer_append_repeat(out, ' ', depth * 2u);
            }
        }

        put_node_head(&writer, current, flags);
        if (current->first_child != NULL)
        {
            current = current->first_child;
            depth++;
            continue;
        }

        put_byte(&writer, ')');
        while ((current != node) && (current->next == NULL))
        {
            current = current->parent;
            depth--;
            put_byte(&writer, ')');
        }

        if (current == node)
        {
            break;
        }

        current = current->next;
    }

    put_byte(&writer, '\n');
    if (writer.error != AMARK_ERROR_NONE)
    {
        /* Shortening never fails nor allocates. */
        (void)amark_buffer_truncate(out, start_length);
    }

    return writer.error;
}
