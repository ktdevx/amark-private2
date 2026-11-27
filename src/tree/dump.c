/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * Dump: the S-expression dump and the dump writer (13.4-13.7, 12.13).
 *
 * The subtree is walked in preorder without recursion and without a stack: the
 * walk goes down to the first child, across to the next sibling, or up to the
 * parent, and keeps only its depth below the top for the indentation (capped at
 * DUMP_MAX_INDENT_LEVELS levels). Each node
 * writes its header ("(" type-name and its attributes) when it is entered and
 * its ")" when it is left. On failure the buffer is cut back to the length it
 * had before the call.
 */
#include "tree/tree.h"

#include <string.h>

#include <amark/buffer.h>
#include <amark/utf8.h>

/* The writer of the attributes of the extension data (amark_dump_writer_t). */
struct amark_dump_writer
{
    amark_buffer_t *out;
};

/* The indentation stops growing at this depth (128 spaces), so that the dump of
 * a deep tree stays linear in its nodes; the builder reads white space only as a
 * separator, so the round trip is the same. */
#define DUMP_MAX_INDENT_LEVELS 64u

#define DUMP_KNOWN_FLAGS \
    (AMARK_DUMP_STYLE | AMARK_DUMP_LAYOUT | AMARK_DUMP_SPELLING | AMARK_DUMP_POSITIONS | \
     AMARK_DUMP_LINES)

/* The names of the token kinds (amark_spell_kind_t, from AMARK_SPELL_TEXT) and of
 * the line endings (amark_eol_t, from AMARK_EOL_NONE), as the test builder reads
 * them. */
static const char *const spell_kind_names[] = {"text",     "marker",     "space",
                                               "delimiter", "escape",    "char_ref",
                                               "replaced", "line_break", "tab_remainder"};
static const char *const eol_names[] = {"none", "lf", "crlf", "cr"};

/* ---- Atoms ---------------------------------------------------------------------- */

static amark_error_t put(amark_buffer_t *out, const char *text)
{
    return amark_buffer_append_cstr(out, text);
}

/* Appends number in decimal. */
static amark_error_t put_int(amark_buffer_t *out, int64_t number)
{
    char digits[24];
    size_t used = 0;
    /* The magnitude, also of INT64_MIN. */
    uint64_t magnitude = number < 0 ? (uint64_t)0 - (uint64_t)number : (uint64_t)number;
    char text[24];
    size_t length = 0;

    do
    {
        digits[used++] = (char)('0' + (int)(magnitude % 10u));
        magnitude /= 10u;
    } while (magnitude > 0);
    if (number < 0)
    {
        text[length++] = '-';
    }
    while (used > 0)
    {
        text[length++] = digits[--used];
    }
    return amark_buffer_append(out, text, length);
}

/* Appends " " and number in decimal. */
static amark_error_t put_space_int(amark_buffer_t *out, int64_t number)
{
    amark_error_t error = amark_buffer_append_byte(out, ' ');

    return error != AMARK_ERROR_NONE ? error : put_int(out, number);
}

/* Appends the escape \xHH of byte. */
static amark_error_t put_hex(amark_buffer_t *out, unsigned char byte)
{
    static const char hex[] = "0123456789ABCDEF";
    char text[4];

    text[0] = '\\';
    text[1] = 'x';
    text[2] = hex[byte >> 4];
    text[3] = hex[byte & 0x0Fu];
    return amark_buffer_append(out, text, sizeof(text));
}

/* Appends the length bytes at text as a string in double quotes (13.5): \" \\
 * \n \r \t, and \xHH for the other control characters (C0, DEL and C1, as
 * their bytes) and for each byte of invalid UTF-8. */
static amark_error_t put_string(amark_buffer_t *out, const char *text, size_t length)
{
    amark_error_t error = amark_buffer_append_byte(out, '"');
    size_t run = 0; /* the start of the bytes written as they are */
    size_t i = 0;

    while (error == AMARK_ERROR_NONE && i < length)
    {
        unsigned char c = (unsigned char)text[i];
        const char *escape = NULL;
        size_t hex_count = 0;
        size_t size = 1;

        if (c < 0x80u)
        {
            switch (c)
            {
            case '"':
                escape = "\\\"";
                break;
            case '\\':
                escape = "\\\\";
                break;
            case '\n':
                escape = "\\n";
                break;
            case '\r':
                escape = "\\r";
                break;
            case '\t':
                escape = "\\t";
                break;
            default:
                hex_count = (c < 0x20u || c == 0x7Fu) ? 1 : 0;
                break;
            }
        }
        else
        {
            uint32_t codepoint = 0;

            size = amark_utf8_decode(text + i, length - i, &codepoint);
            /* An invalid sequence decodes to U+FFFD over its maximal subpart; a
             * real U+FFFD is EF BF BD. */
            if ((codepoint == AMARK_UTF8_REPLACEMENT &&
                 !(size == 3 && memcmp(text + i, "\xEF\xBF\xBD", 3) == 0)) ||
                (codepoint >= 0x80u && codepoint <= 0x9Fu))
            {
                hex_count = size;
            }
        }
        if (escape == NULL && hex_count == 0)
        {
            i += size;
            continue;
        }
        error = amark_buffer_append(out, text + run, i - run);
        if (escape != NULL)
        {
            if (error == AMARK_ERROR_NONE)
            {
                error = put(out, escape);
            }
        }
        else
        {
            size_t k;

            for (k = 0; k < hex_count && error == AMARK_ERROR_NONE; k++)
            {
                error = put_hex(out, (unsigned char)text[i + k]);
            }
        }
        i += size;
        run = i;
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_buffer_append(out, text + run, length - run);
    }
    return error == AMARK_ERROR_NONE ? amark_buffer_append_byte(out, '"') : error;
}

/* Whether text matches [a-z][a-z0-9_]*. */
static bool is_name(const char *text)
{
    size_t i;

    if (text == NULL || text[0] < 'a' || text[0] > 'z')
    {
        return false;
    }
    for (i = 1; text[i] != '\0'; i++)
    {
        char c = text[i];

        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
        {
            return false;
        }
    }
    return true;
}

/* Appends " :" and name. */
static amark_error_t put_key(amark_buffer_t *out, const char *name)
{
    amark_error_t error = put(out, " :");

    return error != AMARK_ERROR_NONE ? error : put(out, name);
}

/* ---- Values --------------------------------------------------------------------- */

/* Appends value, a PRESENT value of the facet desc. */
static amark_error_t put_value(amark_buffer_t *out, const amark_facet_desc_t *desc,
                               const amark_value_t *value)
{
    switch (desc->kind)
    {
    case AMARK_FACET_STRING:
        return put_string(out, value->text != NULL ? value->text : "", value->length);
    case AMARK_FACET_INT:
        return put_int(out, value->number);
    case AMARK_FACET_BOOL:
        return put(out, value->number != 0 ? "true" : "false");
    case AMARK_FACET_ENUM:
        if (value->number >= 1 && (uint64_t)value->number <= (uint64_t)desc->enum_count)
        {
            const char *name = desc->enum_names[value->number - 1];

            return is_name(name) ? put(out, name) : put_string(out, name, strlen(name));
        }
        return put_int(out, value->number);
    }
    return put_int(out, value->number);
}

/* Whether facet desc is the literal: a STRING value facet named "literal". */
static bool is_literal(const amark_facet_desc_t *desc)
{
    return desc->role == AMARK_FACET_VALUE && desc->kind == AMARK_FACET_STRING &&
           strcmp(desc->name, "literal") == 0;
}

/* Appends the facets of node of role (the literal excepted) as " :name value"
 * (values) or as " :style (:name value ...)" (styles); the facets without a
 * PRESENT value are left out. */
static amark_error_t put_facets(amark_buffer_t *out, const amark_node_t *node,
                                amark_facet_role_t role)
{
    const amark_node_type_desc_t *type = node->desc;
    bool opened = false;
    size_t i;

    for (i = 0; i < type->facet_count; i++)
    {
        const amark_facet_desc_t *desc = &type->facets[i];
        amark_value_t value;
        amark_error_t error;

        if (desc->role != role || is_literal(desc))
        {
            continue;
        }
        memset(&value, 0, sizeof(value));
        value.struct_size = sizeof(value);
        error = amark_node_get(node, (amark_facet_t)(i + 1), &value);
        if (error != AMARK_ERROR_NONE)
        {
            return error;
        }
        if (value.state != AMARK_VALUE_PRESENT)
        {
            continue;
        }
        if (role == AMARK_FACET_STYLE)
        {
            error = put(out, opened ? " :" : " :style (:");
            opened = true;
            if (error == AMARK_ERROR_NONE)
            {
                error = put(out, desc->name);
            }
        }
        else
        {
            error = put_key(out, desc->name);
        }
        if (error == AMARK_ERROR_NONE)
        {
            error = amark_buffer_append_byte(out, ' ');
        }
        if (error == AMARK_ERROR_NONE)
        {
            error = put_value(out, desc, &value);
        }
        if (error != AMARK_ERROR_NONE)
        {
            return error;
        }
    }
    return opened ? amark_buffer_append_byte(out, ')') : AMARK_ERROR_NONE;
}

/* Appends " " and the literal of node, if its type has one. */
static amark_error_t put_literal(amark_buffer_t *out, const amark_node_t *node)
{
    const amark_node_type_desc_t *type = node->desc;
    size_t i;

    for (i = 0; i < type->facet_count; i++)
    {
        amark_value_t value;
        amark_error_t error;

        if (!is_literal(&type->facets[i]))
        {
            continue;
        }
        memset(&value, 0, sizeof(value));
        value.struct_size = sizeof(value);
        error = amark_node_get(node, (amark_facet_t)(i + 1), &value);
        if (error != AMARK_ERROR_NONE || value.state != AMARK_VALUE_PRESENT)
        {
            return error;
        }
        error = amark_buffer_append_byte(out, ' ');
        return error != AMARK_ERROR_NONE
                   ? error
                   : put_string(out, value.text != NULL ? value.text : "", value.length);
    }
    return AMARK_ERROR_NONE;
}

/* ---- Layout and spelling ---------------------------------------------------------- */

/* Appends the kind of a token, and later its options [:facet name] [:role n]. */
static amark_error_t put_kind(amark_buffer_t *out, uint8_t kind)
{
    if (kind >= AMARK_SPELL_TEXT && kind <= AMARK_SPELL_TAB_REMAINDER)
    {
        return put(out, spell_kind_names[kind - AMARK_SPELL_TEXT]);
    }
    return put_int(out, kind);
}

static amark_error_t put_options(amark_buffer_t *out, const amark_node_t *node, uint8_t facet,
                                 uint16_t role)
{
    amark_error_t error = AMARK_ERROR_NONE;

    if (facet != AMARK_FACET_NONE)
    {
        const amark_facet_desc_t *desc = amark_node_type_facet(node->desc, facet);

        error = put(out, " :facet ");
        if (error == AMARK_ERROR_NONE)
        {
            error = desc != NULL ? put(out, desc->name) : put_int(out, facet);
        }
    }
    if (error == AMARK_ERROR_NONE && role != 0)
    {
        error = put(out, " :role");
        if (error == AMARK_ERROR_NONE)
        {
            error = put_space_int(out, role);
        }
    }
    return error;
}

/* Appends "(" a b c, and the separator before it (" " after the first item). */
static amark_error_t put_triple(amark_buffer_t *out, size_t index, uint32_t a, uint32_t b,
                                uint32_t c)
{
    amark_error_t error = put(out, index > 0 ? " (" : "(");

    if (error == AMARK_ERROR_NONE)
    {
        error = put_int(out, a);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = put_space_int(out, b);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = put_space_int(out, c);
    }
    return error;
}

static amark_error_t put_layout(amark_buffer_t *out, const amark_node_t *node)
{
    const amark_tree_records_t *records = node->records;
    const amark_tree_content_t *content = node->content;
    amark_error_t error = AMARK_ERROR_NONE;
    size_t i;

    if (node->line_count > 0)
    {
        error = put(out, " :lines (");
        if (error == AMARK_ERROR_NONE)
        {
            error = put_int(out, node->line_first);
        }
        if (error == AMARK_ERROR_NONE)
        {
            error = put_space_int(out, node->line_count);
        }
        if (error == AMARK_ERROR_NONE)
        {
            error = amark_buffer_append_byte(out, ')');
        }
    }
    if (records != NULL && records->margin_count > 0 && error == AMARK_ERROR_NONE)
    {
        error = put(out, " :margins (");
        for (i = 0; i < records->margin_count && error == AMARK_ERROR_NONE; i++)
        {
            const amark_tree_margin_t *margin = &records->margins[i];

            error = put_triple(out, i, margin->line, margin->start, margin->end);
            if (error == AMARK_ERROR_NONE)
            {
                error = put(out, margin->state == AMARK_MARGIN_LAZY ? " lazy" : " present");
            }
            if (error == AMARK_ERROR_NONE && (margin->flags & AMARK_MARGIN_FIRST) != 0)
            {
                error = put(out, " first");
            }
            if (error == AMARK_ERROR_NONE)
            {
                error = put_options(out, node, AMARK_FACET_NONE, margin->role);
            }
            if (error == AMARK_ERROR_NONE)
            {
                error = amark_buffer_append_byte(out, ')');
            }
        }
        if (error == AMARK_ERROR_NONE)
        {
            error = amark_buffer_append_byte(out, ')');
        }
    }
    if (records != NULL && records->blank_line_count > 0 && error == AMARK_ERROR_NONE)
    {
        error = put(out, " :blanks (");
        for (i = 0; i < records->blank_line_count && error == AMARK_ERROR_NONE; i++)
        {
            const amark_tree_blank_line_t *blank = &records->blank_lines[i];

            error = put_triple(out, i, blank->line, blank->start, blank->end);
            if (error == AMARK_ERROR_NONE)
            {
                error = amark_buffer_append_byte(out, ')');
            }
        }
        if (error == AMARK_ERROR_NONE)
        {
            error = amark_buffer_append_byte(out, ')');
        }
    }
    if (records != NULL && records->token_count > 0 && error == AMARK_ERROR_NONE)
    {
        error = put(out, " :tokens (");
        for (i = 0; i < records->token_count && error == AMARK_ERROR_NONE; i++)
        {
            const amark_tree_layout_token_t *token = &records->tokens[i];

            error = put(out, i > 0 ? " (" : "(");
            if (error == AMARK_ERROR_NONE)
            {
                error = put_kind(out, token->kind);
            }
            if (error == AMARK_ERROR_NONE)
            {
                error = put_space_int(out, token->line);
            }
            if (error == AMARK_ERROR_NONE)
            {
                error = put_space_int(out, token->start);
            }
            if (error == AMARK_ERROR_NONE)
            {
                error = put_space_int(out, token->end);
            }
            if (error == AMARK_ERROR_NONE)
            {
                error = put_options(out, node, token->facet, token->role);
            }
            if (error == AMARK_ERROR_NONE)
            {
                error = amark_buffer_append_byte(out, ')');
            }
        }
        if (error == AMARK_ERROR_NONE)
        {
            error = amark_buffer_append_byte(out, ')');
        }
    }
    /* Empty free content with one line is the value "\n" of a CONTENT facet
     * (amark_tree_content_from_value; no other leaf holds it, see
     * amark_tree_type_has_content_facet): ":content free \"\"" would read back
     * without a line, so it is left out and the literal "\n", written last,
     * gives it. */
    if (content != NULL && error == AMARK_ERROR_NONE &&
        !(content->state == AMARK_CONTENT_FREE && content->length == 0 &&
          content->line_count == 1 && amark_tree_type_has_content_facet(node->desc)))
    {
        if (content->state == AMARK_CONTENT_FREE)
        {
            error = put(out, " :content free ");
            if (error == AMARK_ERROR_NONE)
            {
                error = put_string(out, content->bytes != NULL ? content->bytes : "",
                                   content->length);
            }
        }
        else
        {
            error = put(out, " :content laid (");
            for (i = 0; i < content->piece_count && error == AMARK_ERROR_NONE; i++)
            {
                const amark_tree_piece_t *piece = &content->pieces[i];

                error = put_triple(out, i, piece->line, piece->start, piece->end);
                if (error == AMARK_ERROR_NONE)
                {
                    error = amark_buffer_append_byte(out, ')');
                }
            }
            if (error == AMARK_ERROR_NONE)
            {
                error = amark_buffer_append_byte(out, ')');
            }
        }
    }
    return error;
}

static amark_error_t put_spelling(amark_buffer_t *out, const amark_node_t *node)
{
    const amark_tree_content_tokens_t *tokens = node->content_tokens;
    amark_error_t error = AMARK_ERROR_NONE;
    size_t i;

    if (tokens != NULL && tokens->count > 0)
    {
        error = put(out, " :spelling (");
        for (i = 0; i < tokens->count && error == AMARK_ERROR_NONE; i++)
        {
            const amark_tree_content_token_t *token = &tokens->items[i];

            error = put(out, i > 0 ? " (" : "(");
            if (error == AMARK_ERROR_NONE)
            {
                error = put_kind(out, token->kind);
            }
            if (error == AMARK_ERROR_NONE)
            {
                error = put_space_int(out, token->start);
            }
            if (error == AMARK_ERROR_NONE)
            {
                error = put_space_int(out, token->end);
            }
            if (error == AMARK_ERROR_NONE)
            {
                error = put_options(out, node, token->facet, token->role);
            }
            if (error == AMARK_ERROR_NONE)
            {
                error = amark_buffer_append_byte(out, ')');
            }
        }
        if (error == AMARK_ERROR_NONE)
        {
            error = amark_buffer_append_byte(out, ')');
        }
    }
    if (node->dropped != 0 && error == AMARK_ERROR_NONE)
    {
        bool first = true;
        amark_facet_t facet;

        error = put(out, " :dropped (");
        if (error == AMARK_ERROR_NONE && (node->dropped & AMARK_TREE_DROPPED_STRUCTURE) != 0)
        {
            error = put(out, "structure");
            first = false;
        }
        for (facet = 1; facet <= AMARK_FACET_MAX && error == AMARK_ERROR_NONE; facet++)
        {
            const amark_facet_desc_t *desc;

            if ((node->dropped & (1u << facet)) == 0)
            {
                continue;
            }
            desc = amark_node_type_facet(node->desc, facet);
            error = first ? AMARK_ERROR_NONE : amark_buffer_append_byte(out, ' ');
            first = false;
            if (error == AMARK_ERROR_NONE)
            {
                error = desc != NULL ? put(out, desc->name) : put_int(out, facet);
            }
        }
        if (error == AMARK_ERROR_NONE)
        {
            error = amark_buffer_append_byte(out, ')');
        }
    }
    return error;
}

static amark_error_t put_position(amark_buffer_t *out, const amark_node_t *node)
{
    amark_source_range_t range;
    bool has_position = false;
    amark_error_t error;

    memset(&range, 0, sizeof(range));
    range.struct_size = sizeof(range);
    error = amark_node_source_range(node, &range, &has_position);
    if (error != AMARK_ERROR_NONE || !has_position)
    {
        return error;
    }
    error = put(out, " :pos (");
    if (error == AMARK_ERROR_NONE)
    {
        error = put_int(out, range.start_line);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = put_space_int(out, range.start_column);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = put_space_int(out, range.end_line);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = put_space_int(out, range.end_column);
    }
    return error == AMARK_ERROR_NONE ? amark_buffer_append_byte(out, ')') : error;
}

static amark_error_t put_line_table(amark_buffer_t *out, const amark_document_t *document)
{
    size_t count = amark_document_line_count(document);
    amark_error_t error = put(out, " :line_table (");
    size_t i;

    for (i = 0; i < count && error == AMARK_ERROR_NONE; i++)
    {
        amark_line_info_t info;

        memset(&info, 0, sizeof(info));
        info.struct_size = sizeof(info);
        error = amark_document_line(document, (amark_line_id_t)i, &info);
        if (error == AMARK_ERROR_NONE)
        {
            error = put(out, i > 0 ? " (" : "(");
        }
        if (error == AMARK_ERROR_NONE)
        {
            error = put_int(out, info.source);
        }
        if (error == AMARK_ERROR_NONE)
        {
            error = put_space_int(out, info.number);
        }
        if (error == AMARK_ERROR_NONE)
        {
            error = put_space_int(out, (int64_t)info.offset);
        }
        if (error == AMARK_ERROR_NONE)
        {
            error = put_space_int(out, (int64_t)info.length);
        }
        if (error == AMARK_ERROR_NONE)
        {
            error = amark_buffer_append_byte(out, ' ');
        }
        if (error == AMARK_ERROR_NONE)
        {
            error = (info.eol >= AMARK_EOL_NONE && info.eol <= AMARK_EOL_CR)
                        ? put(out, eol_names[info.eol - AMARK_EOL_NONE])
                        : put_int(out, info.eol);
        }
        if (error == AMARK_ERROR_NONE)
        {
            error = put_space_int(out, info.width);
        }
        if (error == AMARK_ERROR_NONE)
        {
            error = amark_buffer_append_byte(out, ')');
        }
    }
    return error == AMARK_ERROR_NONE ? amark_buffer_append_byte(out, ')') : error;
}

/* ---- Nodes ---------------------------------------------------------------------- */

/* Appends the header of node: "(" type-name, its attributes and its literal. */
static amark_error_t put_header(amark_buffer_t *out, const amark_node_t *node, uint32_t flags)
{
    amark_error_t error = amark_buffer_append_byte(out, '(');

    if (error == AMARK_ERROR_NONE)
    {
        error = put(out, node->desc->name);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = put_facets(out, node, AMARK_FACET_VALUE);
    }
    if (error == AMARK_ERROR_NONE && node->desc->dump != NULL)
    {
        amark_dump_writer_t writer;

        writer.out = out;
        error = node->desc->dump(node, node->data, &writer);
    }
    if (error == AMARK_ERROR_NONE && (flags & AMARK_DUMP_STYLE) != 0)
    {
        error = put_facets(out, node, AMARK_FACET_STYLE);
    }
    if (error == AMARK_ERROR_NONE && (flags & AMARK_DUMP_LAYOUT) != 0)
    {
        error = put_layout(out, node);
    }
    if (error == AMARK_ERROR_NONE && (flags & AMARK_DUMP_SPELLING) != 0)
    {
        error = put_spelling(out, node);
    }
    if (error == AMARK_ERROR_NONE && (flags & AMARK_DUMP_POSITIONS) != 0)
    {
        error = put_position(out, node);
    }
    if (error == AMARK_ERROR_NONE && (flags & AMARK_DUMP_LINES) != 0 &&
        node == node->document->root)
    {
        error = put_line_table(out, node->document);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = put_literal(out, node);
    }
    return error;
}

/* Appends the dump of the subtree of top (the walk without recursion). */
static amark_error_t dump_subtree(const amark_node_t *top, uint32_t flags, amark_buffer_t *out)
{
    const amark_node_t *node = top;
    size_t depth = 0;
    amark_error_t error;

    for (;;)
    {
        error = put_header(out, node, flags);
        if (error != AMARK_ERROR_NONE)
        {
            return error;
        }
        if (node->first_child != NULL)
        {
            node = node->first_child;
            depth++;
        }
        else
        {
            /* Close node, and each ancestor below top that has no next sibling. */
            error = amark_buffer_append_byte(out, ')');
            while (error == AMARK_ERROR_NONE && node != top && node->next == NULL)
            {
                node = node->parent;
                depth--;
                error = amark_buffer_append_byte(out, ')');
            }
            if (error != AMARK_ERROR_NONE)
            {
                return error;
            }
            if (node == top)
            {
                return amark_buffer_append_byte(out, '\n');
            }
            node = node->next;
        }
        error = amark_buffer_append_byte(out, '\n');
        if (error == AMARK_ERROR_NONE)
        {
            error = amark_buffer_append_repeat(
                out, ' ', 2 * (depth < DUMP_MAX_INDENT_LEVELS ? depth : DUMP_MAX_INDENT_LEVELS));
        }
        if (error != AMARK_ERROR_NONE)
        {
            return error;
        }
    }
}

amark_error_t amark_node_dump(const amark_node_t *node, uint32_t flags, amark_buffer_t *out)
{
    size_t length;
    amark_error_t error;

    if (node == NULL || out == NULL || (flags & ~(uint32_t)DUMP_KNOWN_FLAGS) != 0)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    length = amark_buffer_length(out);
    error = dump_subtree(node, flags, out);
    if (error != AMARK_ERROR_NONE)
    {
        (void)amark_buffer_truncate(out, length);
    }
    return error;
}

/* ---- The dump writer ------------------------------------------------------------- */

/* Checks the writer and the name and appends " :name ". */
static amark_error_t writer_key(amark_dump_writer_t *writer, const char *name)
{
    amark_error_t error;

    if (writer == NULL || writer->out == NULL || !is_name(name))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    error = put_key(writer->out, name);
    return error == AMARK_ERROR_NONE ? amark_buffer_append_byte(writer->out, ' ') : error;
}

amark_error_t amark_dump_write_string(amark_dump_writer_t *writer, const char *name,
                                      const char *text, size_t length)
{
    amark_error_t error;

    if (text == NULL && length > 0)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    error = writer_key(writer, name);
    return error == AMARK_ERROR_NONE ? put_string(writer->out, text != NULL ? text : "", length)
                                     : error;
}

amark_error_t amark_dump_write_int(amark_dump_writer_t *writer, const char *name, int64_t number)
{
    amark_error_t error = writer_key(writer, name);

    return error == AMARK_ERROR_NONE ? put_int(writer->out, number) : error;
}

amark_error_t amark_dump_write_bool(amark_dump_writer_t *writer, const char *name, bool value)
{
    amark_error_t error = writer_key(writer, name);

    return error == AMARK_ERROR_NONE ? put(writer->out, value ? "true" : "false") : error;
}

amark_error_t amark_dump_write_symbol(amark_dump_writer_t *writer, const char *name,
                                      const char *symbol)
{
    amark_error_t error;

    if (!is_name(symbol))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    error = writer_key(writer, name);
    return error == AMARK_ERROR_NONE ? put(writer->out, symbol) : error;
}
