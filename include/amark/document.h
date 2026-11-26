/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * Documents: their life, the ownership of the syntax set, the sources and the
 * line table, the positions, the label normalization and the definition lookup,
 * and the lossless verification.
 *
 * Threads (14.3, 14.4):
 *   - A document, with all of its nodes, is used by one thread at a time. Even
 *     the queries (getting a value, for example) write internal caches, so they
 *     are not read-only operations: using one document from several threads at
 *     once needs synchronization by the application.
 *   - Different documents can be used from different threads at the same time.
 *   - A frozen syntax set is immutable and can be shared by several documents
 *     and threads.
 *
 * Memory: every allocation of a document goes through the global allocator
 * (amark/allocator.h). The document owns all of its nodes, those in its tree and
 * those removed from it, its sources and its line table, and frees them with
 * itself.
 */
#ifndef AMARK_DOCUMENT_H
#define AMARK_DOCUMENT_H

#include <amark/export.h>
#include <amark/buffer.h>
#include <amark/error.h>
#include <amark/node.h>

AMARK_BEGIN_DECLS

/*
 * Creates in *out a document whose tree holds only the document node. set is the
 * frozen syntax set of the document; NULL uses the static built-in set
 * (amark_syntax_set_core). The document borrows set: it must outlive the
 * document, unless the document takes its ownership
 * (amark_document_take_syntax_set). AMARK_ERROR_INVALID_ARGUMENT for a NULL out,
 * AMARK_ERROR_INVALID_STATE for a set that is not frozen. On failure *out is
 * NULL.
 */
AMARK_API amark_error_t amark_document_create(const amark_syntax_set_t *set,
                                              amark_document_t **out);

/*
 * Destroys document: every node that belongs to it (in the tree or not), its
 * sources and its line table, then, last, the syntax set it owns. Does nothing for NULL.
 */
AMARK_API void amark_document_destroy(amark_document_t *document);

/* The document node, the root of the tree of document (NULL for NULL). */
AMARK_API amark_node_t *amark_document_root(const amark_document_t *document);

/* The syntax set of document (NULL for NULL). */
AMARK_API const amark_syntax_set_t *amark_document_syntax_set(const amark_document_t *document);

/*
 * Gives the ownership of set to document: the document keeps it for its whole
 * life and destroys it after all of its nodes. set must be the syntax set the
 * document uses, created by the application. AMARK_ERROR_INVALID_ARGUMENT for
 * NULL, for a set the document does not use, for the static built-in set and
 * when the document already owns its set; then the document is unchanged and
 * the caller keeps the ownership of set.
 */
AMARK_API amark_error_t amark_document_take_syntax_set(amark_document_t *document,
                                                       amark_syntax_set_t *set);

/* ---- Sources and lines ------------------------------------------------------- */

/*
 * The sources and the line table (2.1-2.8):
 *   - A document copies the bytes of each source it is given and keeps them for
 *     its whole life (1.4). The sources get the identifiers 0, 1, ... in the
 *     order of registration.
 *   - Each source is divided into lines. The line endings are LF, CRLF and CR;
 *     the range of a line excludes its line ending. The lines get the serial
 *     numbers of the line table (amark_line_id_t, from 0) in the order in which
 *     they are completed, after the lines already in the table, and a number in
 *     their source (from 1).
 *   - A line is completed when its line ending is read. A line that ends with
 *     CR is completed by the next byte (CR or CRLF) or by the end of the input.
 *     At the end of the input, the bytes after the last line ending, if there
 *     is at least one, become a line without a line ending (AMARK_EOL_NONE).
 *     Appending bytes never changes the bytes already registered nor the lines
 *     already completed, and the same bytes split anywhere give the same lines
 *     as one registration.
 *   - A source that starts with the UTF-8 BOM (EF BB BF) is marked as having
 *     it; the BOM is not part of the first line (whose offset is then 3).
 *   - The width of a line counts its columns: a tab advances to the next
 *     multiple of 4, every other byte is one column.
 */

/* amark_document_add_source: the input also ends with these bytes. */
#define AMARK_SOURCE_FINISHED 0x01u

/* The line ending of a line. */
typedef enum amark_eol
{
    AMARK_EOL_NONE = 1, /* the last line of a source, without a line ending */
    AMARK_EOL_LF,
    AMARK_EOL_CRLF,
    AMARK_EOL_CR
} amark_eol_t;

/* A line of the line table. */
typedef struct amark_line_info
{
    size_t struct_size;       /* sizeof(amark_line_info_t), set by the caller */
    amark_source_id_t source; /* the source of the line */
    uint32_t number;          /* the number of the line in its source, from 1 */
    size_t offset;            /* the position of its first byte in the source (after the BOM) */
    size_t length;            /* its bytes, without the line ending */
    amark_eol_t eol;          /* its line ending */
    uint32_t width;           /* its columns */
} amark_line_info_t;

/*
 * Registers a copy of the length bytes at bytes as a new source of document,
 * divides it into lines and returns its identifier in *out. flags is 0 or
 * AMARK_SOURCE_FINISHED: with 0, more bytes can be appended
 * (amark_document_append_source) until amark_document_finish_source; with
 * AMARK_SOURCE_FINISHED, the input ends here. bytes may be NULL when length is
 * 0. AMARK_ERROR_INVALID_ARGUMENT for a NULL document or out, NULL bytes with a
 * length, and other flags; AMARK_ERROR_LIMIT_EXCEEDED when the line table would
 * hold more than UINT32_MAX - 1 lines, a line would be wider than UINT32_MAX
 * columns, or there are UINT32_MAX sources. On failure the document is unchanged.
 */
AMARK_API amark_error_t amark_document_add_source(amark_document_t *document,
                                                  const char *bytes, size_t length,
                                                  uint32_t flags, amark_source_id_t *out);

/*
 * Appends a copy of the length bytes at bytes to the end of source, and adds
 * the lines they complete to the end of the line table; the lines already
 * completed do not change. AMARK_ERROR_INVALID_ARGUMENT for a NULL document, an
 * unknown source and NULL bytes with a length; AMARK_ERROR_INVALID_STATE for a
 * source whose input has ended; and the errors of amark_document_add_source.
 * On failure the document is unchanged.
 */
AMARK_API amark_error_t amark_document_append_source(amark_document_t *document,
                                                     amark_source_id_t source,
                                                     const char *bytes, size_t length);

/*
 * Ends the input of source: completes its last line (a line that ends with CR,
 * and the bytes after the last line ending). AMARK_ERROR_INVALID_ARGUMENT for a
 * NULL document and an unknown source; AMARK_ERROR_INVALID_STATE for a source
 * whose input has already ended. On failure the document is unchanged.
 */
AMARK_API amark_error_t amark_document_finish_source(amark_document_t *document,
                                                     amark_source_id_t source);

/* The number of sources of document (0 for NULL). */
AMARK_API size_t amark_document_source_count(const amark_document_t *document);

/*
 * The bytes of source as registered, the BOM included: *data (never NULL) and
 * *length, and in *has_bom whether the source starts with the BOM. Each output
 * may be NULL. *data stays valid until the next change of document.
 * AMARK_ERROR_INVALID_ARGUMENT for a NULL document and an unknown source.
 */
AMARK_API amark_error_t amark_document_source_data(const amark_document_t *document,
                                                   amark_source_id_t source,
                                                   const char **data, size_t *length,
                                                   bool *has_bom);

/* The number of lines in the line table of document (0 for NULL). */
AMARK_API size_t amark_document_line_count(const amark_document_t *document);

/*
 * Fills *out with the line line of the line table (out->struct_size is set by
 * the caller). AMARK_ERROR_INVALID_ARGUMENT for NULL, a struct_size smaller than
 * sizeof(amark_line_info_t) and a line not in the table.
 */
AMARK_API amark_error_t amark_document_line(const amark_document_t *document,
                                            amark_line_id_t line, amark_line_info_t *out);

/*
 * Columns and bytes (2.3): the columns of a line count from 0 at its first byte
 * (after the BOM for the first line of a source); a tab advances to the next
 * multiple of 4, every other byte is one column. The bytes are positions in the
 * line, from 0 at its first byte. The column and the byte at the end of the line
 * (the width and the length) are valid positions.
 *
 * Converts column of line to the byte that holds it in *byte: the position of
 * the byte whose columns contain column (the length of the line for the width).
 * When column is inside a tab, *byte is the position of that tab and
 * *tab_offset the columns of the tab before column; otherwise *tab_offset is
 * 0. tab_offset may be NULL. AMARK_ERROR_INVALID_ARGUMENT for a NULL document
 * or byte, a line not in the table and a column past the width of the line;
 * AMARK_ERROR_OUT_OF_MEMORY when the index of the tabs of the line, built by
 * the first conversion on a line that has a tab, cannot be allocated. On
 * failure the outputs are unchanged.
 */
AMARK_API amark_error_t amark_document_column_to_byte(const amark_document_t *document,
                                                      amark_line_id_t line, uint32_t column,
                                                      size_t *byte, uint32_t *tab_offset);

/*
 * Converts byte of line to its first column in *column (the width of the line
 * for its length; the first column of the tab for a tab).
 * AMARK_ERROR_INVALID_ARGUMENT for a NULL document or column, a line not in the
 * table and a byte past the length of the line; AMARK_ERROR_OUT_OF_MEMORY as
 * for amark_document_column_to_byte. On failure *column is unchanged.
 */
AMARK_API amark_error_t amark_document_byte_to_column(const amark_document_t *document,
                                                      amark_line_id_t line, size_t byte,
                                                      uint32_t *column);

/*
 * The default line ending of document: the line ending of the first line of the
 * line table that has one, or AMARK_EOL_LF when there is none (and for NULL).
 */
AMARK_API amark_eol_t amark_document_default_eol(const amark_document_t *document);

/* ---- Positions --------------------------------------------------------------- */

/*
 * The range of a node in a source (2.9-2.11):
 *   - source: the source of the range;
 *   - start_offset, end_offset: the positions of its bytes in the source (the
 *     BOM included, as for amark_line_info_t.offset); end_offset is excluded;
 *   - start_line, start_column: the line of its first byte (its number in the
 *     source, from 1) and the position of that byte in the line + 1;
 *   - end_line, end_column: the same for its last byte, so that end_column is
 *     the position in the line of the end of the range (excluded); an empty
 *     range ends on its start line with end_column = start_column - 1.
 * A line ending in the range belongs to the line it ends.
 */
typedef struct amark_source_range
{
    size_t struct_size; /* sizeof(amark_source_range_t), set by the caller */
    amark_source_id_t source;
    size_t start_offset, end_offset;
    uint32_t start_line, start_column;
    uint32_t end_line, end_column;
} amark_source_range_t;

/*
 * Finds the range of node in its source: *has_position tells whether node has
 * one, and *out receives it when it has (out is not written otherwise).
 *   - A block (2.9): from its line range. The range starts on the first line
 *     of the line range, at the leftmost column of the records (margins, blank
 *     lines, layout tokens and content pieces) of the block and its descendants
 *     on that line, and ends on the last line at the rightmost column of the
 *     records on that line; a line without such a record starts at its column 0
 *     or ends at its width. The columns become bytes through the line table (a
 *     column inside a tab is that tab). A block without a line range has no
 *     position.
 *   - An inline node (2.10): from the content tokens of the node and its
 *     descendants on the base of its first token (in document order): the
 *     earliest start and the latest end, mapped to their lines and columns
 *     through the pieces of that laid content. A byte of a tab expanded to
 *     spaces is that tab; a virtual LF between the lines is the line ending of
 *     the earlier line. An inline node without a token on itself and on its
 *     descendants, or whose base is free content, has no position.
 * When the range would span several sources, it is cut to the source of its
 * start: its end moves back to the last line (or piece) of that source.
 * The time is proportional to the nodes of the subtree, plus the logarithm of
 * the records for each block and of the pieces for an inline node.
 * AMARK_ERROR_INVALID_ARGUMENT for NULL and a struct_size smaller than
 * sizeof(amark_source_range_t); AMARK_ERROR_OUT_OF_MEMORY when the index of
 * the tabs of a line cannot be built (amark_document_column_to_byte). On
 * failure the outputs are unchanged. The tree is never changed.
 */
AMARK_API amark_error_t amark_node_source_range(const amark_node_t *node,
                                                amark_source_range_t *out,
                                                bool *has_position);

/* ---- Lossless verification ---------------------------------------------------- */

/* The kinds of the inconsistencies the lossless verification reports (10.4). */
typedef enum amark_lossless_problem
{
    /* A column of a line covered by no record, or a line of no column that no
     * blank line and no content piece refers to; a byte of the content of a
     * leaf covered by no content token. */
    AMARK_LOSSLESS_GAP = 1,
    /* A column of a line, or a byte of the content of a leaf, covered more
     * than once. */
    AMARK_LOSSLESS_OVERLAP,
    /* A boundary of a record inside a character other than a tab. */
    AMARK_LOSSLESS_SPLIT_CHARACTER,
    /* A content token under a leaf whose base is not the content of that
     * leaf. */
    AMARK_LOSSLESS_FOREIGN_TOKEN
} amark_lossless_problem_t;

/*
 * The first inconsistency found by amark_document_check_lossless:
 *   - problem: its kind;
 *   - line, column: for the division of the lines, the line (its identifier in
 *     the line table) and the column; AMARK_LINE_NONE and 0 otherwise;
 *   - leaf, content_offset: for the coverage of a content, the leaf and the
 *     position in its content (the start of the token, on its own base, for a
 *     foreign token); NULL and 0 otherwise.
 */
typedef struct amark_lossless_report
{
    size_t struct_size; /* sizeof(amark_lossless_report_t), set by the caller */
    amark_lossless_problem_t problem;
    amark_line_id_t line;
    uint32_t column;
    const amark_node_t *leaf;
    size_t content_offset;
} amark_lossless_report_t;

/*
 * Verifies that the tree of document holds every byte of source exactly once
 * (10.1-10.5), looking only at the nodes connected to the document node:
 *   1. The division of the lines: on each completed line of source, the
 *      margins (a lazy margin has no columns and counts for nothing), the
 *      blank lines, the layout tokens and the content pieces cover every
 *      column exactly once; a line of no column is referred to by at least one
 *      blank line or content piece; and no boundary of these records is inside
 *      a character other than a tab (a boundary inside a tab is allowed: the
 *      tab is divided by columns). The BOM belongs to the source and the line
 *      endings to the line table, so the bytes of source are all covered.
 *   2. The coverage of the contents: for each block with content (every one,
 *      whatever its source), the content tokens of the leaf and of its
 *      descendants cover its content exactly once, and all are on the content
 *      of that leaf.
 * On the first inconsistency, in the order of the lines and then of the columns
 * (a split character first at a column), then in the document order of the
 * leaves and the order of the content positions, returns
 * AMARK_ERROR_NOT_LOSSLESS and writes it to *report when report is not NULL;
 * report is written only then. The time is proportional to the lines, the
 * columns and the bytes of the source, the records and the nodes of the tree
 * and the bytes and tokens of the contents (14.7). AMARK_ERROR_INVALID_ARGUMENT
 * for a NULL document, a source that is not in document and a report whose
 * struct_size is smaller than sizeof(amark_lossless_report_t);
 * AMARK_ERROR_OUT_OF_MEMORY when the working memory cannot be allocated. The
 * tree is never changed.
 */
AMARK_API amark_error_t amark_document_check_lossless(const amark_document_t *document,
                                                      amark_source_id_t source,
                                                      amark_lossless_report_t *report);

/* ---- Labels ------------------------------------------------------------------ */

/*
 * Appends to out the normalized form of the link label of length bytes at label,
 * by the rules of CommonMark (9.1), in this order:
 *   1. the leading and trailing whitespace is removed;
 *   2. each run of internal whitespace becomes one space (U+0020);
 *   3. each code point is replaced by its full case fold
 *      (amark_unicode_case_fold, the mappings C and F of CaseFolding.txt).
 * The whitespace is that of CommonMark link labels: space (U+0020), tab, line
 * feed and carriage return, so the line endings LF, CRLF and CR count. Every
 * other character, a full-width space (U+3000), a no-break space or a form feed
 * included, is an ordinary character: it is kept and case-folded. label is decoded as UTF-8 (amark/utf8.h): an invalid
 * sequence becomes U+FFFD and a NUL byte is an ordinary character. A label of
 * whitespace only appends nothing. label may be NULL when length is 0. Two
 * labels match when their normalized forms are the same bytes.
 * AMARK_ERROR_INVALID_ARGUMENT for a NULL out and a NULL label with a length;
 * AMARK_ERROR_OUT_OF_MEMORY and AMARK_ERROR_OVERFLOW as for amark_buffer_append.
 * On failure the content of out is unchanged.
 */
AMARK_API amark_error_t amark_label_normalize(const char *label, size_t length,
                                              amark_buffer_t *out);

/*
 * Finds in *out the link reference definition (a link_definition node) for
 * label, of length bytes (9.2, 9.3): among the definitions attached to the
 * document node whose label (amark_node_get of AMARK_LINK_DEFINITION_LABEL)
 * normalizes (amark_label_normalize) to the same bytes as label, the first in
 * document order; NULL when there is none, for a label that normalizes to
 * nothing, and for the definitions of a detached subtree. The lookup sees every
 * change made before it (labels, insertions, removals, moves). It takes time
 * proportional to the length of label, the number of definitions of that
 * label and the depth of the tree, plus, for the first lookup of a label of
 * several definitions after a structural change, the time to order them
 * (14.8); the labels changed since the last lookup are normalized again first.
 * label may be NULL when length is 0. AMARK_ERROR_INVALID_ARGUMENT for a NULL
 * document or out and a NULL label with a length; AMARK_ERROR_OUT_OF_MEMORY
 * when the index cannot be updated. On failure *out is NULL (when out is not
 * NULL); the tree is never changed.
 */
AMARK_API amark_error_t amark_document_find_definition(const amark_document_t *document,
                                                       const char *label, size_t length,
                                                       amark_node_t **out);

AMARK_END_DECLS

#endif /* AMARK_DOCUMENT_H */
