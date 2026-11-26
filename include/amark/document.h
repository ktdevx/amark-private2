/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * Documents: the owner of the nodes, the sources and the edit tracking state of a
 * syntax tree; the positions in the sources and the lossless check of the syntax
 * tokens.
 *
 * A document uses a frozen syntax set (amark/extension.h), which must outlive the
 * document unless the document takes its ownership
 * (amark_document_take_syntax_set). A document is used from one thread at a time;
 * different documents are independent.
 */
#ifndef AMARK_DOCUMENT_H
#define AMARK_DOCUMENT_H

#include <stdbool.h>
#include <stddef.h>

#include <amark/export.h>
#include <amark/error.h>
#include <amark/node.h>

AMARK_BEGIN_DECLS

/*
 * Creates in *out_document a document with only its root (a node of type
 * document), using the frozen syntax set syntax, or amark_syntax_set_core() when
 * syntax is NULL. A syntax set that is not frozen is AMARK_ERROR_INVALID_STATE.
 * Edit tracking starts off.
 */
AMARK_API amark_error_t amark_document_create(const amark_syntax_set_t *syntax,
                                              amark_document_t **out_document);

/*
 * Destroys document: every node (including the detached ones), then the sources,
 * then the syntax set it owns (if any), then the document itself. Does nothing for
 * NULL.
 */
AMARK_API void amark_document_destroy(amark_document_t *document);

/* Returns the root of document (a node of type document). */
AMARK_API amark_node_t *amark_document_root(const amark_document_t *document);

/* Returns the syntax set document uses. */
AMARK_API const amark_syntax_set_t *amark_document_syntax_set(const amark_document_t *document);

/*
 * Registers a copy of the length bytes at data as a new source of document and
 * stores its identifier (0, 1, ... in registration order) in *out_source. A NULL
 * data with length 0 is an empty source; a NULL data with another length is
 * AMARK_ERROR_INVALID_ARGUMENT.
 */
AMARK_API amark_error_t amark_document_add_source(amark_document_t *document, const char *data,
                                                  size_t length, amark_source_id_t *out_source);

/*
 * Appends the length bytes at data to the source source of document. The bytes
 * already there keep their offsets (the tokens that refer to them stay valid),
 * but a pointer returned by amark_document_source_data may become invalid. A CR at
 * the end of a chunk and an LF at the start of the next one are one line ending.
 */
AMARK_API amark_error_t amark_document_append_source(amark_document_t *document,
                                                     amark_source_id_t source, const char *data,
                                                     size_t length);

/* Returns the number of sources of document. */
AMARK_API size_t amark_document_source_count(const amark_document_t *document);

/* Returns the bytes of the source source of document, and their number in
 * *out_length when out_length is not NULL; NULL for a source that is not
 * registered. */
AMARK_API const char *amark_document_source_data(const amark_document_t *document,
                                                 amark_source_id_t source, size_t *out_length);

/*
 * Converts offset (0 <= offset <= the length of the source) in the source source
 * of document to a line and a column, both counted from 1 (the column in bytes).
 * A line ends with LF, CR or CRLF. An offset out of range is
 * AMARK_ERROR_INVALID_ARGUMENT.
 */
AMARK_API amark_error_t amark_document_position(const amark_document_t *document,
                                                amark_source_id_t source, size_t offset,
                                                size_t *out_line, size_t *out_column);

/* Turns the edit tracking of document on or off (amark/node.h, AMARK_EDIT_*). A
 * parser builds the tree with tracking off and turns it on before returning it. */
AMARK_API void amark_document_set_edit_tracking(amark_document_t *document, bool enabled);

/* Whether the edit tracking of document is on. */
AMARK_API bool amark_document_edit_tracking(const amark_document_t *document);

/*
 * Takes the ownership of set, a syntax set on the heap that document uses: after
 * a success, amark_document_destroy destroys set after the nodes and the sources,
 * and the caller must not destroy it. set must be the syntax set of document
 * itself, not the static one of amark_syntax_set_core(), and document must not own
 * a syntax set yet; otherwise (or for a NULL argument) returns
 * AMARK_ERROR_INVALID_ARGUMENT, document is unchanged and the ownership stays with
 * the caller. Allocates nothing.
 *
 * A caller that holds only a const pointer (such as the result of
 * amark_document_syntax_set) must not give the ownership away: do not cast the
 * const away to call this function. Other documents may use the same syntax set,
 * but they must be destroyed before the document that owns it.
 */
AMARK_API amark_error_t amark_document_take_syntax_set(amark_document_t *document,
                                                       amark_syntax_set_t *set);

/* --- Lossless check of the syntax tokens ------------------------------------- */

/* The kind of problem amark_document_check_tokens reports. */
typedef enum amark_token_problem
{
    /* No problem (or the check ended for another reason). */
    AMARK_TOKEN_PROBLEM_NONE = 0,
    /* A byte is covered by no token. */
    AMARK_TOKEN_PROBLEM_GAP = 1,
    /* A byte is covered by two tokens or more. */
    AMARK_TOKEN_PROBLEM_OVERLAP = 2,
    /* The partial tab tokens of a tab do not match its columns. */
    AMARK_TOKEN_PROBLEM_TAB_COLUMNS = 3
} amark_token_problem_t;

/* The first problem amark_document_check_tokens found. */
typedef struct amark_token_report
{
    size_t struct_size;
    amark_token_problem_t problem;
    /* The offset of the first byte with the problem. */
    size_t offset;
    /* The owner of the token involved (for a gap, of the token before it; NULL when
     * there is none). */
    const amark_node_t *node;
} amark_token_report_t;

/*
 * Checks that the tokens of the nodes reachable from the root of document that
 * refer to the source source cover each byte of the source exactly once, with
 * the partial tabs matching the columns of their tab (tab stops of 4 columns).
 * Returns AMARK_ERROR_NONE when they do, or AMARK_ERROR_NOT_LOSSLESS and, when
 * out_report is not NULL, writes the first problem in *out_report (a documented
 * exception: an output argument written on failure). For any other result, a
 * non-NULL *out_report gets AMARK_TOKEN_PROBLEM_NONE, offset 0 and node NULL.
 * out_report may be NULL. A NULL document, a source that is not registered and a
 * struct_size below that of version 1 are AMARK_ERROR_INVALID_ARGUMENT.
 */
AMARK_API amark_error_t amark_document_check_tokens(const amark_document_t *document,
                                                    amark_source_id_t source,
                                                    amark_token_report_t *out_report);

AMARK_END_DECLS

#endif /* AMARK_DOCUMENT_H */
