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
 * The sources and the line table (2.1, 2.2, 2.4-2.8):
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
 * The default line ending of document: the line ending of the first line of the
 * line table that has one, or AMARK_EOL_LF when there is none (and for NULL).
 */
AMARK_API amark_eol_t amark_document_default_eol(const amark_document_t *document);

AMARK_END_DECLS

#endif /* AMARK_DOCUMENT_H */
