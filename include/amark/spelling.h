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
 * while the block has records or pieces). Setting the same range changes
 * nothing; otherwise the change is notified.
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
 * AMARK_SPELL_TAB_REMAINDER, a facet the type does not have or whose spelling
 * is dropped, a line outside the line table or the line range of the block, an
 * empty range of columns, a column beyond the width of the line or inside a
 * character other than a tab, a token that spells a facet whose kind does not
 * match the bytes of its columns (the rules of the content tokens below for
 * AMARK_SPELL_TEXT, AMARK_SPELL_REPLACED, AMARK_SPELL_ESCAPE and
 * AMARK_SPELL_CHAR_REF, on the bytes without the part of a tab cut by a
 * boundary; the last three may not cut a tab), and a token that does not come
 * after the last token (on a later line, or on the same line from its end on).
 * A structural token (AMARK_FACET_NONE) holds any bytes.
 * AMARK_ERROR_OUT_OF_MEMORY leaves the block unchanged.
 */
AMARK_API amark_error_t amark_block_add_token(amark_node_t *block,
                                              const amark_layout_token_t *token);

/*
 * The edits of the block stage (before the inlines of a leaf are parsed): a
 * leaf whose lines are read again as another block gives them up. Each needs a
 * block with content (AMARK_NODE_TYPE_CONTENT) without content tokens, without
 * inline children and without free content; the lines are the count lines from
 * first, at the start or at the end of its line range (every line of the range
 * too). The content of the leaf is built again from the pieces that are left,
 * with the same correspondence to the columns of the lines; when no piece is
 * left, the leaf has no content. Content shared with another node is copied
 * first and the copy becomes the content of the leaf, so that the other node
 * keeps its bytes. The changes are notified. AMARK_ERROR_INVALID_ARGUMENT, with
 * the nodes unchanged, for the arguments outside of these rules;
 * AMARK_ERROR_OUT_OF_MEMORY and AMARK_ERROR_OVERFLOW leave the nodes unchanged.
 */

/*
 * Moves the lines from the leaf from to the leaf to (7.10): to gets them as its
 * line range, with the layout tokens of from on them and their content pieces
 * as its content; from keeps the rest of its range, its tokens and its pieces.
 * The margins belong to the containers and are not moved (a leaf with a margin
 * or a blank line on the lines is refused). to is another block with content of
 * the same document, with no line range, no content, no records and no content
 * tokens yet; each moved layout token must spell a facet of the type of to
 * whose spelling is not dropped on to (or none).
 */
AMARK_API amark_error_t amark_block_move_lines(amark_node_t *from, amark_node_t *to,
                                               amark_line_id_t first, uint32_t count);

/* Removes the lines from leaf (7.11): their layout tokens and content pieces go
 * away and the line range shrinks (a leaf with a margin or a blank line on the
 * lines is refused). */
AMARK_API amark_error_t amark_block_remove_lines(amark_node_t *leaf, amark_line_id_t first,
                                                 uint32_t count);

/*
 * Turns the first columns of the laid content of leaf into its own spelling
 * (7.11), such as the marker of a task list item: the columns from the start of
 * the first piece become a layout token of kind, facet and role (the rules of
 * amark_block_add_token; it may come before other tokens, without overlapping
 * them), and the piece starts after them (a tab cut by the new start is left as
 * spaces). columns is at least 1 and at most the columns of the first piece; the
 * piece may become empty only when no other piece is on its line. The other
 * nodes keep their values: the parser records them with their own functions.
 * The preconditions and the errors are those of the block stage above, except
 * that the leaf needs laid content and keeps its line range.
 */
AMARK_API amark_error_t amark_block_trim_content_start(amark_node_t *leaf, uint32_t columns,
                                                       amark_spell_kind_t kind,
                                                       amark_facet_t facet, uint16_t role);

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
 * line), or on the same line from the end of the last piece on, with columns,
 * when the last piece is not empty.
 * AMARK_ERROR_INVALID_ARGUMENT, with the leaf unchanged, for NULL, a node that is
 * not a block with content, free content, a line outside the line table or the
 * line range of the leaf, a piece out of order, column_start after column_end,
 * a column beyond the width of the line or inside a character other than a
 * tab. AMARK_ERROR_LIMIT_EXCEEDED when the content would reach 4 GiB,
 * AMARK_ERROR_OUT_OF_MEMORY; both leave the leaf unchanged. Content shared
 * with another leaf is copied first, and the copy becomes the content of the
 * leaf (the tokens of the leaf and its descendants on the old content move to
 * the copy); content with tokens of other nodes on it is not copied, as the
 * bytes already there do not change. The change is notified.
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

/* ---- Content tokens -------------------------------------------------------------- */

/*
 * The content tokens spell the inline structure on the content of a leaf: each
 * is a nonempty range of bytes of the content, recorded on the leaf itself or on
 * one of its inline descendants. The tokens of a node are on one content object,
 * their base (the content of the leaf they were recorded on, which they keep
 * when the leaf gets another content or the node moves to another leaf), in the
 * order of the content and without overlap. The kind must match the bytes:
 *   - AMARK_SPELL_LINE_BREAK: line breaks only (the virtual "\n" of laid content,
 *     an LF of free content);
 *   - AMARK_SPELL_TAB_REMAINDER: spaces of laid content that stand for the
 *     columns of a tab cut by the boundary of a piece;
 *   - AMARK_SPELL_REPLACED: NUL bytes and invalid UTF-8 sequences only;
 *   - AMARK_SPELL_ESCAPE: a backslash and an ASCII punctuation character;
 *   - AMARK_SPELL_CHAR_REF: one character reference (amark_char_ref_decode);
 *   - AMARK_SPELL_TEXT: valid UTF-8 without NUL (a NUL byte or an invalid
 *     sequence is spelled by AMARK_SPELL_REPLACED);
 *   - the other kinds: any bytes.
 * Both ends of a token are boundaries of the UTF-8 characters of the content
 * (an invalid sequence decodes as the bytes of its maximal subpart).
 */

/* A content token: the bytes [start, end) of its base. The caller sets
 * struct_size to sizeof(amark_content_token_t). */
typedef struct amark_content_token
{
    size_t struct_size;
    size_t start;
    size_t end;
    amark_spell_kind_t kind;
    /* The facet the token spells, or AMARK_FACET_NONE for structural spelling. */
    amark_facet_t facet;
    /* The meaning of the token for its type; 0 is none. */
    uint16_t role;
} amark_content_token_t;

/*
 * Appends token, on the content of leaf, to node: leaf itself or one of its
 * inline descendants. AMARK_ERROR_INVALID_ARGUMENT, with the node unchanged, for
 * NULL, a struct_size below sizeof(amark_content_token_t), a leaf that is not a
 * block with content or has none, a node that is neither leaf nor an inline
 * descendant of it, a node whose tokens are on another content object, an
 * unknown kind, a facet the type of node does not have or whose spelling is
 * dropped on node, an empty range, a range outside the content or with an end
 * inside a UTF-8 character, a kind that does not match the bytes, and a token
 * that starts before the end of the last token of node.
 * AMARK_ERROR_OUT_OF_MEMORY leaves the node unchanged. The derived values of
 * node are dropped and the change is notified.
 */
AMARK_API amark_error_t amark_node_add_content_token(amark_node_t *node, const amark_node_t *leaf,
                                                     const amark_content_token_t *token);

/* The number of content tokens of node (0 for NULL). */
AMARK_API size_t amark_node_content_token_count(const amark_node_t *node);

/* Reads content token index of node into *out (whose struct_size the caller
 * sets). AMARK_ERROR_INVALID_ARGUMENT for NULL, a struct_size below
 * sizeof(amark_content_token_t) and an index out of range. */
AMARK_API amark_error_t amark_node_content_token_at(const amark_node_t *node, size_t index,
                                                    amark_content_token_t *out);

/*
 * Moves the count tokens of from that start at index first to to, where they
 * go between the tokens of to in the order of the content.
 * AMARK_ERROR_INVALID_ARGUMENT, with both nodes unchanged, for NULL, the same
 * node, nodes of different documents, a count of 0 or a range beyond the tokens
 * of from, a to that is neither an inline node nor a block with content, a to
 * whose tokens are on another content object, a token whose facet the type of
 * to does not have or whose spelling is dropped on to, and tokens that would
 * overlap the tokens of to.
 * AMARK_ERROR_OUT_OF_MEMORY leaves both nodes unchanged. The derived values of
 * both nodes are dropped and the changes are notified.
 */
AMARK_API amark_error_t amark_node_move_content_tokens(amark_node_t *from, amark_node_t *to,
                                                       size_t first, size_t count);

/*
 * Splits the AMARK_SPELL_TEXT token index of node at the content position at
 * into [start, at) and [at, end), both with the facet and the role of the
 * token. AMARK_ERROR_INVALID_ARGUMENT, with the node unchanged, for NULL, an
 * index out of range, a token of another kind, a position not strictly inside
 * the token, and a position inside a UTF-8 character. AMARK_ERROR_OUT_OF_MEMORY
 * leaves the node unchanged. The derived values of node are dropped and the
 * change is notified.
 */
AMARK_API amark_error_t amark_node_split_content_token(amark_node_t *node, size_t index,
                                                       size_t at);

/*
 * The base of the content tokens of node: its bytes (*bytes, never NULL, ""
 * without tokens) and *length, its *state (AMARK_CONTENT_NONE when node has no
 * tokens), and *is_own_leaf_content, whether the base is the current content of
 * the nearest leaf among node and its ancestors (false without tokens). Each
 * output may be NULL. AMARK_ERROR_INVALID_ARGUMENT for a NULL node.
 */
AMARK_API amark_error_t amark_node_content_base(const amark_node_t *node, const char **bytes,
                                               size_t *length, amark_content_state_t *state,
                                               bool *is_own_leaf_content);

/*
 * Replaces the content and the inline children of target with those of
 * source_leaf, a block with content of the same document or of another one
 * (8.6, the msgstr insertion):
 *   - the content of target becomes new free content holding a copy of the
 *     bytes and the line count of the content of source_leaf (empty, without
 *     a line, when it has none; empty content has no line either when the type
 *     of target has no CONTENT facet);
 *   - the children of target are destroyed, and the inline children of
 *     source_leaf are copied (amark_node_copy) or, from another document,
 *     imported (amark_node_import) in their place, with their spelling: their
 *     content tokens on the content of source_leaf go to the new content of
 *     target, those on another content object follow the rules of the copy
 *     and the import;
 *   - target keeps its line range, its layout records (margins, blank lines
 *     and the layout tokens of its own spelling), its style and value facets,
 *     and its own content tokens on their base, its previous content.
 * source_leaf may be target itself; source_leaf is not changed. A replaced
 * link_definition goes on the dirty list of the definition index.
 * AMARK_ERROR_INVALID_ARGUMENT for NULL, a target or a source_leaf that is not
 * a block with content, a child type of source_leaf that the syntax set of
 * target does not have or that target cannot contain; AMARK_ERROR_LIMIT_EXCEEDED
 * when the lines to import do not fit; AMARK_ERROR_OUT_OF_MEMORY. On failure
 * nothing changes. The changes are notified.
 */
AMARK_API amark_error_t amark_block_replace_content(amark_node_t *target,
                                                    const amark_node_t *source_leaf);

/* ---- Spelling state and verbatim output ------------------------------------------ */

/* The state of the own spelling of a node (11.1). The caller sets struct_size to
 * sizeof(amark_spelling_state_t). */
typedef struct amark_spelling_state
{
    size_t struct_size;
    /* The node has a record other than its line range (a margin, a blank line, a
     * layout token, laid content) or a content token. */
    bool has_spelling;
    /* No spelling of the node was dropped (dropped_facets is 0 and
     * structure_dropped is false). */
    bool complete;
    /* Bit n is set when the spelling of facet n was dropped (by setting or
     * unsetting the facet, or by dropping its spelling). */
    uint32_t dropped_facets;
    /* The structural spelling of the node (its margins, blank lines and layout
     * tokens) was dropped (amark_node_discard_spelling,
     * amark_block_discard_margin). */
    bool structure_dropped;
} amark_spelling_state_t;

/* Reads the spelling state of node into *out (whose struct_size the caller
 * sets). AMARK_ERROR_INVALID_ARGUMENT for NULL and a struct_size below
 * sizeof(amark_spelling_state_t). */
AMARK_API amark_error_t amark_node_spelling_state(const amark_node_t *node,
                                                  amark_spelling_state_t *out);

/*
 * Whether the content of leaf can be output in its original lines (11.3): *out
 * is true exactly when
 *   - the content of leaf is laid;
 *   - neither leaf nor any of its inline descendants dropped any spelling;
 *   - every inline descendant has content tokens, and the tokens of leaf and of
 *     its descendants are on the current content of leaf;
 *   - those tokens cover each byte of the content exactly once;
 *   - the nesting order is kept: the range of the tokens of each inline node
 *     (from its first token to its last) lies within the range of its parent (the
 *     whole content for the children of leaf), the ranges of siblings follow
 *     each other in the order of the siblings without overlap, and no token of
 *     a node falls within the range of one of its children.
 * Allocates nothing; runs in time proportional to the tokens and the nodes under
 * leaf. AMARK_ERROR_INVALID_ARGUMENT for NULL arguments and a node that is not a
 * block with content (AMARK_NODE_TYPE_CONTENT).
 */
AMARK_API amark_error_t amark_block_content_verbatim(const amark_node_t *leaf, bool *out);

/*
 * Whether the subtree of node can be output in its original spelling (11.4):
 * *out is true exactly when
 *   - for a block: no node of the subtree dropped any spelling; the block has a
 *     line range; the content of each leaf with content can be output in its
 *     original lines (amark_block_content_verbatim; a leaf without content has
 *     nothing spelled on a content); the children of each block hold, lie in its
 *     range and follow each other in the order of their lines; and each line of
 *     the range of each block is covered without gap or overlap, from a column
 *     c0 up to the end of the line, by the margin of the line (required on each
 *     line of a container), the layout tokens, the blank line, the content
 *     pieces and the children of the block. Only the children of a type with
 *     AMARK_NODE_TYPE_SHARE_LINES share a line, in columns that follow each other
 *     in the order of the children (7.8). The columns left of c0 (the margins of
 *     the ancestors of node, which amark_block_find_margin tells, 11.5) are not
 *     judged: a block moved under other containers keeps its own spelling but
 *     its lines lack their margins, so its new ancestors are not verbatim;
 *   - for an inline node: no node of the subtree dropped any spelling, its
 *     tokens are on the laid content of its leaf, and the tokens of the subtree
 *     nest and cover the range of the node exactly once.
 * The results are cached in the nodes and dropped by every change of a node or
 * of its descendants (records, values, styles, structure), so that judging every
 * node of a document takes time proportional to its nodes and records (14.9).
 * The cache is internal state: like any getter, the query is not a read-only
 * operation for concurrency (one thread per document). AMARK_ERROR_INVALID_ARGUMENT
 * for NULL arguments; AMARK_ERROR_OUT_OF_MEMORY when the columns the judgment
 * remembers cannot be allocated (*out is then false, and the results already
 * cached stay consistent).
 */
AMARK_API amark_error_t amark_node_verbatim(const amark_node_t *node, bool *out);

/* Whether node is a new node (11.6): it has no line range, no record, no content
 * token and no dropped mark of its structural spelling (false for NULL). */
AMARK_API bool amark_node_is_new(const amark_node_t *node);

AMARK_END_DECLS

#endif /* AMARK_SPELLING_H */
