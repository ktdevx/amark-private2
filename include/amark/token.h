/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * Syntax tokens: the original notation of a node, as ranges of the sources of its
 * document. A parser records them so that an unedited document is covered byte
 * for byte (amark_document_check_tokens); the tokens of a node are kept sorted by
 * (source, offset, tab_column_offset, order of addition). Changing tokens records
 * no edit.
 */
#ifndef AMARK_TOKEN_H
#define AMARK_TOKEN_H

#include <stddef.h>
#include <stdint.h>

#include <amark/export.h>
#include <amark/error.h>
#include <amark/document.h>

AMARK_BEGIN_DECLS

/* The kind of a token, and what amark_node_add_token checks for it. */
typedef enum amark_token_kind
{
    /* Bytes that appear as they are in the meaning (a literal or an attribute). */
    AMARK_TOKEN_TEXT = 1,
    /* A syntax mark (#, >, *, a fence, a bracket, a list marker, ...); length 1 or
     * more. */
    AMARK_TOKEN_MARKER = 2,
    /* Spaces and tabs outside the meaning (indentation, padding, trailing). */
    AMARK_TOKEN_SPACE = 3,
    /* A line ending: "\n", "\r\n" or "\r". */
    AMARK_TOKEN_LINE_ENDING = 4,
    /* A backslash escape (one character in the meaning); length 2, starting with
     * a backslash. */
    AMARK_TOKEN_ESCAPE = 5,
    /* An entity or numeric character reference; starts with & and ends with ;. */
    AMARK_TOKEN_CHAR_REF = 6,
    /* Bytes replaced by another character in the meaning (NUL, invalid UTF-8);
     * length 1 or more. */
    AMARK_TOKEN_REPLACED = 7,
    /* The UTF-8 byte order mark at offset 0 (EF BB BF). */
    AMARK_TOKEN_BOM = 8,
    /* The mark that a lazy continuation line omitted the prefix of this
     * container; length 0. */
    AMARK_TOKEN_LAZY_CONTINUATION = 9
} amark_token_kind_t;

/* The notation a container puts at the start of a line, before the content of its
 * children (the > of a block quote and its optional space, the marker of a list
 * item, the indentation of continuation lines, a lazy continuation). */
#define AMARK_TOKEN_FLAG_CONTAINER_PREFIX 0x01u

/* The token covers some columns of one tab only: length 1, the byte is a tab,
 * tab_columns is 1-4 and tab_column_offset + tab_columns is at most 4. */
#define AMARK_TOKEN_FLAG_PARTIAL_TAB      0x02u

/* A syntax token. */
typedef struct amark_token
{
    size_t struct_size;
    amark_token_kind_t kind;
    /* A number the syntax extension of the owner type defines; 0 is none. */
    uint16_t role;
    /* AMARK_TOKEN_FLAG_*; an unknown flag is AMARK_ERROR_INVALID_ARGUMENT. */
    uint32_t flags;
    amark_source_id_t source;
    size_t offset;
    size_t length;
    /* With AMARK_TOKEN_FLAG_PARTIAL_TAB: the columns of the same tab used by the
     * tokens before this one. */
    unsigned tab_column_offset;
    /* With AMARK_TOKEN_FLAG_PARTIAL_TAB: the columns this token covers. */
    unsigned tab_columns;
} amark_token_t;

/* Returns the number of tokens of node. */
AMARK_API size_t amark_node_token_count(const amark_node_t *node);

/* Writes the token at index of node in *out_token (up to its struct_size). An
 * index out of range is AMARK_ERROR_INVALID_ARGUMENT. */
AMARK_API amark_error_t amark_node_token_at(const amark_node_t *node, size_t index,
                                            amark_token_t *out_token);

/*
 * Adds token to node, at its sorted place (adding in source order takes amortized
 * constant time). Checks the kind and the flags, the bytes the kind requires,
 * that the source is registered in the document of node and that offset + length
 * is within the source (an overflow included); a violation is
 * AMARK_ERROR_INVALID_ARGUMENT. Zero-length tokens are kept.
 */
AMARK_API amark_error_t amark_node_add_token(amark_node_t *node, const amark_token_t *token);

/* Removes the token at index of node. */
AMARK_API amark_error_t amark_node_remove_token(amark_node_t *node, size_t index);

/* Moves the tokens [first, first + count) of from to to, a node of the same
 * document, at their sorted places. On failure neither node changes. */
AMARK_API amark_error_t amark_node_transfer_tokens(amark_node_t *from, size_t first, size_t count,
                                                   amark_node_t *to);

/* Returns the name of kind ("text", "marker", ...), or "unknown". */
AMARK_API const char *amark_token_kind_name(amark_token_kind_t kind);

AMARK_END_DECLS

#endif /* AMARK_TOKEN_H */
