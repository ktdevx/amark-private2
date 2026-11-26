/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The spelling of the tree: the token kinds, the recording and reading of the
 * margins, the blank lines, the leaf spellings and the content, the operations
 * that drop the spelling, and whether the original spelling can be output.
 */
#ifndef AMARK_SPELLING_H
#define AMARK_SPELLING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <amark/export.h>
#include <amark/error.h>
#include <amark/node.h>
#include <amark/value.h>

AMARK_BEGIN_DECLS

/* ---- Token kinds ---------------------------------------------------------------- */

/* The kind of a spelling token: what its bytes are and how they take part in the
 * value derived from the tokens of a facet. */
typedef enum amark_spell_kind
{
    /* Bytes of the value as they are. */
    AMARK_SPELL_TEXT = 1,
    /* A structural marker (`#`, a fence, an underline, `***`, a list marker). */
    AMARK_SPELL_MARKER,
    /* Structural white space (an indentation, trailing spaces). */
    AMARK_SPELL_SPACE,
    /* A delimiter (`*`, `[`, `](`, a quote of a title). */
    AMARK_SPELL_DELIMITER,
    /* A backslash escape. */
    AMARK_SPELL_ESCAPE,
    /* An entity or numeric character reference. */
    AMARK_SPELL_CHAR_REF,
    /* Bytes the value replaces with U+FFFD. */
    AMARK_SPELL_REPLACED,
    /* A virtual line break of the content (not a layout token). */
    AMARK_SPELL_LINE_BREAK,
    /* The spaces of the content that stand for the rest of a tab (not a layout
     * token). */
    AMARK_SPELL_TAB_REMAINDER
} amark_spell_kind_t;

/* ---- Layout records ------------------------------------------------------------- */

/*
 * The layout of a block is recorded in lines and columns of the line table of
 * its document (columns count a tab up to the next multiple of 4):
 *   - the line range of the block: the lines it covers, without its trailing
 *     blank lines;
 *   - the margins of a container (AMARK_NODE_TYPE_MARGINS): one per line it
 *     covers, in the order of the lines;
 *   - the blank lines of a block that can have block children (the document, a
 *     list, a container): a blank line belongs to the innermost such block that
 *     covers it and has no child whose range holds it, so the blank lines
 *     between the items of a list belong to the list; in the order of the lines,
 *     one per line;
 *   - the layout tokens of a block (its own spelling: indentations, markers,
 *     fence lines, closing `#`, trailing spaces), in the order of the lines and
 *     the columns, without overlap.
 * A range of columns may start or end inside a tab (a tab can be shared between
 * a margin and the spelling or the content of a leaf), but not inside another
 * character. Every record lies within the line range of its block, which is
 * set first. Recording is not an edit: it keeps the spelling of the node.
 */

/* The state of a margin. */
typedef enum amark_margin_state
{
    /* The margin is on the line (its columns may be empty only on a blank line,
     * for an item). */
    AMARK_MARGIN_PRESENT = 1,
    /* The margin was left out by a lazy continuation line: its range of columns
     * is empty, at the column where the margin of the outer container ends on
     * the line (0 when there is none). */
    AMARK_MARGIN_LAZY = 2
} amark_margin_state_t;

/* The flag of the margin of the first line of an item (the line of the marker). */
#define AMARK_MARGIN_FIRST 0x01u

/* The margin of a container on one line. The caller sets struct_size to
 * sizeof(amark_margin_t). */
typedef struct amark_margin
{
    size_t struct_size;
    amark_line_id_t line;
    amark_margin_state_t state;
    /* AMARK_MARGIN_* flags. */
    uint32_t flags;
    /* The columns [column_start, column_end); empty for AMARK_MARGIN_LAZY. */
    uint32_t column_start;
    uint32_t column_end;
    /* The meaning of the margin for its type; 0 is none. */
    uint16_t role;
} amark_margin_t;

/* A blank line of a block: the columns of its white space, up to the end of the
 * line. The caller sets struct_size to sizeof(amark_blank_line_t). */
typedef struct amark_blank_line
{
    size_t struct_size;
    amark_line_id_t line;
    uint32_t column_start;
    uint32_t column_end;
} amark_blank_line_t;

/* A token of the own spelling of a block, on one line. The caller sets
 * struct_size to sizeof(amark_layout_token_t). */
typedef struct amark_layout_token
{
    size_t struct_size;
    amark_line_id_t line;
    /* The columns [column_start, column_end), not empty. */
    uint32_t column_start;
    uint32_t column_end;
    /* Not AMARK_SPELL_LINE_BREAK nor AMARK_SPELL_TAB_REMAINDER. */
    amark_spell_kind_t kind;
    /* The facet the token spells, or AMARK_FACET_NONE for structural spelling. */
    amark_facet_t facet;
    /* The meaning of the token for its type; 0 is none. */
    uint16_t role;
} amark_layout_token_t;

/*
 * Sets the line range of block to the count lines from first (count 0 removes
 * the range; first is then ignored). AMARK_ERROR_INVALID_ARGUMENT for NULL, a node
 * that is not a block, a range outside the line table, and a range that leaves
 * out a record or a content piece of the block (a range cannot be removed
 * while the block has records or pieces). Setting the same range changes nothing; otherwise the change is
 * notified.
 */
AMARK_API amark_error_t amark_block_set_lines(amark_node_t *block, amark_line_id_t first,
                                              uint32_t count);

/* The line range of block: its first line and its number of lines
 * (AMARK_LINE_NONE and 0 when it has none). AMARK_ERROR_INVALID_ARGUMENT for NULL
 * and a node that is not a block. first or count may be NULL. */
AMARK_API amark_error_t amark_block_lines(const amark_node_t *block, amark_line_id_t *first,
                                          uint32_t *count);

/*
 * Appends a margin to container. AMARK_ERROR_INVALID_ARGUMENT, with the container
 * unchanged, for NULL, a struct_size below sizeof(amark_margin_t), a node whose
 * type is not a container (AMARK_NODE_TYPE_MARGINS), an unknown state or flag, a
 * line outside the line table or the line range of the container, a line not
 * after the line of the last margin, a column beyond the width of the line,
 * column_start after column_end, a column inside a character other than a tab,
 * a lazy margin with columns, and a present margin without columns unless the
 * rest of the line from column_start is blank. AMARK_ERROR_OUT_OF_MEMORY leaves
 * the container unchanged.
 */
AMARK_API amark_error_t amark_block_add_margin(amark_node_t *container,
                                               const amark_margin_t *margin);

/*
 * Appends a blank line to block. AMARK_ERROR_INVALID_ARGUMENT, with the block
 * unchanged, for NULL, a struct_size below sizeof(amark_blank_line_t), a node
 * that cannot have block children, a line outside the line table or the line
 * range of the block, a line not after the line of the last blank line, a
 * column_end other than the width of the line, a column_start after it or inside
 * a character other than a tab, and columns that hold anything but spaces and
 * tabs. AMARK_ERROR_OUT_OF_MEMORY leaves the block unchanged.
 */
AMARK_API amark_error_t amark_block_add_blank_line(amark_node_t *block,
                                                   const amark_blank_line_t *blank);

/*
 * Appends a layout token to block. AMARK_ERROR_INVALID_ARGUMENT, with the block
 * unchanged, for NULL, a struct_size below sizeof(amark_layout_token_t), a node
 * that is not a block, an unknown kind or AMARK_SPELL_LINE_BREAK or
 * AMARK_SPELL_TAB_REMAINDER, a facet the type does not have, a line outside the
 * line table or the line range of the block, an empty range of columns, a column
 * beyond the width of the line or inside a character other than a tab, and a
 * token that does not come after the last token (on a later line, or on the same
 * line from its end on). AMARK_ERROR_OUT_OF_MEMORY leaves the block unchanged.
 */
AMARK_API amark_error_t amark_block_add_token(amark_node_t *block,
                                              const amark_layout_token_t *token);

/*
 * Removes the margin of container on line and marks the structural spelling of
 * the container as dropped; the change is notified. Used by the on_change hook
 * of the built-in list and by the hooks of extensions.
 * AMARK_ERROR_INVALID_ARGUMENT for NULL, a node that is not a container, and a
 * line without a margin.
 */
AMARK_API amark_error_t amark_block_discard_margin(amark_node_t *container,
                                                   amark_line_id_t line);

/* The number of margins, blank lines and layout tokens of node (0 for NULL and
 * for a node without them). */
AMARK_API size_t amark_block_margin_count(const amark_node_t *container);
AMARK_API size_t amark_block_blank_line_count(const amark_node_t *block);
AMARK_API size_t amark_block_token_count(const amark_node_t *block);

/* Reads the record index of node, in the order of the records, into *out (whose
 * struct_size the caller sets). AMARK_ERROR_INVALID_ARGUMENT for NULL, a
 * struct_size below the size of the structure and an index out of range. */
AMARK_API amark_error_t amark_block_margin_at(const amark_node_t *container, size_t index,
                                              amark_margin_t *out);
AMARK_API amark_error_t amark_block_blank_line_at(const amark_node_t *block, size_t index,
                                                  amark_blank_line_t *out);
AMARK_API amark_error_t amark_block_token_at(const amark_node_t *block, size_t index,
                                             amark_layout_token_t *out);

/*
 * Finds the margin of container on line, in time logarithmic in its margins:
 * *found tells whether there is one, and *out, if out is not NULL, receives it
 * (out->struct_size set by the caller). A node that is not a container has no
 * margin. AMARK_ERROR_INVALID_ARGUMENT for a NULL container or found, and an out
 * whose struct_size is below sizeof(amark_margin_t).
 */
AMARK_API amark_error_t amark_block_find_margin(const amark_node_t *container,
                                                amark_line_id_t line, amark_margin_t *out,
                                                bool *found);

/* ---- Content -------------------------------------------------------------------- */

/*
 * The content of a leaf (a block whose type has AMARK_NODE_TYPE_CONTENT) is the
 * text that is left of its lines once the margins and its own spelling are
 * taken out, in the order of the lines. It is in one of these states:
 *   - none: the leaf has no content yet;
 *   - laid: each piece of the content is a range of columns of a line of the
 *     document; the pieces of different lines are joined by a virtual "\n" (the
 *     line ending itself belongs to the line table), the pieces of one line are
 *     joined without anything between them (the columns between them are the
 *     own spelling of the leaf), and the columns of a tab cut by the start or the
 *     end of a piece are spaces;
 *   - free: the content holds bytes of its own that do not come from the lines,
 *     with CRLF and CR normalized to LF.
 */
typedef enum amark_content_state
{
    AMARK_CONTENT_NONE = 1,
    AMARK_CONTENT_LAID = 2,
    AMARK_CONTENT_FREE = 3
} amark_content_state_t;

/* A piece of laid content: the columns [column_start, column_end) of line, and
 * the bytes [content_start, content_start + content_length) of the content they
 * become (a tab cut by a boundary counts as spaces). The caller sets
 * struct_size to sizeof(amark_content_piece_t). */
typedef struct amark_content_piece
{
    size_t struct_size;
    amark_line_id_t line;
    uint32_t column_start;
    uint32_t column_end;
    size_t content_start;
    size_t content_length;
} amark_content_piece_t;

/*
 * Appends the columns [column_start, column_end) of line to the laid content of
 * leaf (the content becomes laid when it was none). Pieces are added in order:
 * on a later line than the last piece (an empty range stands for an empty
 * line), or on the same line from the end of the last piece on, with columns.
 * AMARK_ERROR_INVALID_ARGUMENT, with the leaf unchanged, for NULL, a node that is
 * not a block with content, free content, a line outside the line table or the
 * line range of the leaf, a piece out of order, column_start after column_end,
 * a column beyond the width of the line or inside a character other than a
 * tab. AMARK_ERROR_LIMIT_EXCEEDED when the content would reach 4 GiB,
 * AMARK_ERROR_OUT_OF_MEMORY; both leave the leaf unchanged. The change is
 * notified.
 */
AMARK_API amark_error_t amark_block_add_content(amark_node_t *leaf, amark_line_id_t line,
                                                uint32_t column_start, uint32_t column_end);

/*
 * Makes the content of leaf free content holding a copy of the length bytes at
 * bytes, with CRLF and CR normalized to LF (bytes may be NULL when length is 0).
 * The previous content of the leaf is released, never changed.
 * AMARK_ERROR_INVALID_ARGUMENT for a NULL leaf, a node that is not a block with
 * content, and NULL bytes with a length; AMARK_ERROR_LIMIT_EXCEEDED when the
 * content would reach 4 GiB; AMARK_ERROR_OUT_OF_MEMORY. On failure the leaf is
 * unchanged. The change is notified.
 */
AMARK_API amark_error_t amark_block_set_free_content(amark_node_t *leaf, const char *bytes,
                                                     size_t length);

/* The state of the content of leaf (AMARK_CONTENT_NONE for NULL and for a node
 * that has no content). */
AMARK_API amark_content_state_t amark_block_content_state(const amark_node_t *leaf);

/* The bytes of the content of leaf: *bytes (never NULL; "" when there is no
 * content) and *length. AMARK_ERROR_INVALID_ARGUMENT for NULL arguments and a node
 * that is not a block with content. */
AMARK_API amark_error_t amark_block_content_bytes(const amark_node_t *leaf, const char **bytes,
                                                  size_t *length);

/* The number of pieces of the content of leaf (0 for NULL, and for content that
 * is not laid). */
AMARK_API size_t amark_block_content_piece_count(const amark_node_t *leaf);

/* Reads piece index of the content of leaf into *out (whose struct_size the
 * caller sets). AMARK_ERROR_INVALID_ARGUMENT for NULL, a struct_size below
 * sizeof(amark_content_piece_t) and an index out of range. */
AMARK_API amark_error_t amark_block_content_piece_at(const amark_node_t *leaf, size_t index,
                                                     amark_content_piece_t *out);

AMARK_END_DECLS

#endif /* AMARK_SPELLING_H */
