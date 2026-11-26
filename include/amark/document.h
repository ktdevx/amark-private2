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
 * those removed from it, and frees them with itself.
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
 * Destroys document: every node that belongs to it (in the tree or not), then,
 * last, the syntax set it owns. Does nothing for NULL.
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

AMARK_END_DECLS

#endif /* AMARK_DOCUMENT_H */
