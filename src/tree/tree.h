/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The definitions of amark_document_t and amark_node_t, the internal token and
 * source records, and the functions the modules of src/tree/ share. Internal: not
 * part of the public API and not exported.
 */
#ifndef AMARK_TREE_TREE_H
#define AMARK_TREE_TREE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <amark/buffer.h>
#include <amark/document.h>
#include <amark/error.h>
#include <amark/node.h>

#include "base/buffer_internal.h"

/* A syntax token as a node stores it (amark_token_t is the public form). */
typedef struct amark_token_record
{
    size_t offset;
    size_t length;
    uint32_t source;
    uint16_t role;
    uint8_t kind;
    uint8_t flags;
    uint8_t tab_column_offset;
    uint8_t tab_columns;
} amark_token_record_t;

/* A node. One allocation holds this header and, after it at the alignment of
 * max_align, the type data (data_size bytes of desc). */
struct amark_node
{
    amark_document_t *document;
    const amark_node_type_desc_t *desc;
    amark_node_type_t type;
    /* AMARK_EDIT_* bits. */
    uint32_t edits;
    amark_node_t *parent;
    amark_node_t *first_child;
    amark_node_t *last_child;
    amark_node_t *prev;
    amark_node_t *next;
    /* The ring of all the nodes of the document (attached or not). */
    amark_node_t *ring_prev;
    amark_node_t *ring_next;
    /* The literal; NULL is "". */
    char *literal;
    size_t literal_length;
    /* The tokens, sorted by (source, offset, tab_column_offset, order of
     * addition). */
    amark_token_record_t *tokens;
    size_t token_count;
    size_t token_capacity;
    void *user_data;
};

/* A source of a document and the index of its line starts. */
typedef struct amark_source
{
    /* The bytes, embedded with amark_buffer_init. */
    amark_buffer_t bytes;
    size_t *line_starts;
    size_t line_count;
    size_t line_capacity;
    /* The source ends with a CR, which pairs with an LF at the start of the next
     * chunk. */
    bool pending_cr;
} amark_source_t;

/* A document. */
struct amark_document
{
    const amark_syntax_set_t *syntax;
    /* The syntax set taken with amark_document_take_syntax_set (the same as
     * syntax), or NULL. Goes from NULL to non-NULL once and then stays until the
     * document is destroyed. */
    amark_syntax_set_t *owned_syntax;
    amark_node_t *root;
    /* One node of the ring of all the nodes. */
    amark_node_t *ring;
    amark_source_t *sources;
    size_t source_count;
    size_t source_capacity;
    bool tracking;
    /* Counters for the INTERNAL tests of the complexity, never exposed: the
     * ancestor steps of the edit propagation (amark_edit_mark) and the tokens
     * moved by sorted insertions (token.c). 0 when the document is created. */
    size_t stat_propagation_steps;
    size_t stat_token_moves;
};

/* --- edit_tracking.c ---------------------------------------------------------- */

/*
 * Records bits on node when the edit tracking of its document is on (otherwise
 * does nothing), and gives AMARK_EDIT_DESCENDANTS to its ancestors, from the parent
 * up, stopping at the first ancestor that already has it.
 */
void amark_edit_mark(amark_node_t *node, uint32_t bits);

/* --- node.c ------------------------------------------------------------------- */

/*
 * Creates a detached node of type, whose descriptor is desc, in document,
 * including the document type (the root, made by document.c) and the types found
 * by descriptor (copies and imports). Records AMARK_EDIT_CREATED when tracking is
 * on.
 */
amark_error_t amark_node_create_with_desc(amark_document_t *document, amark_node_type_t type,
                                          const amark_node_type_desc_t *desc,
                                          amark_node_t **out_node);

/*
 * Destroys the subtree of node, which is detached, in postorder and without
 * recursion: for each node, the deinit function of its type, its literal,
 * its tokens, and its removal from the ring of the document.
 */
void amark_node_destroy_subtree(amark_node_t *node);

/* --- structure_edit.c --------------------------------------------------------- */

/* Whether a node whose descriptor is child_desc may be a child of parent: the
 * can_contain function of the type of parent, or else its accepts and the provides
 * of child_desc. */
bool amark_node_allows_child(const amark_node_t *parent, const amark_node_type_desc_t *child_desc);

/*
 * Links node, a detached node already checked, as a child of parent before before
 * (at the end when before is NULL, otherwise a child of parent). Allocates nothing
 * and cannot fail. Records AMARK_EDIT_CHILDREN on parent and AMARK_EDIT_RELOCATED on
 * node unless it has AMARK_EDIT_CREATED, and propagates AMARK_EDIT_DESCENDANTS when
 * node has records, whether tracking is on or not.
 */
void amark_node_link_child(amark_node_t *parent, amark_node_t *before, amark_node_t *node);

/* --- tree_check.c ------------------------------------------------------------- */

/*
 * Checks the invariants of document without recursion: the parent, child and
 * sibling links are symmetric, every child satisfies the containment rule
 * (amark_node_allows_child), every ancestor of a node with edit records has
 * AMARK_EDIT_DESCENDANTS, the tokens are sorted and within registered sources,
 * every node of the tree and of the detached subtrees is in the ring, and the
 * subtrees of the nodes of the ring without a parent hold as many nodes as the
 * ring. Takes time linear in the nodes and the tokens and allocates nothing.
 * Returns AMARK_ERROR_NONE, or AMARK_ERROR_INVALID_STATE with a static string that
 * describes the violation in *out_reason (when out_reason is not NULL; NULL on a
 * success); AMARK_ERROR_INVALID_ARGUMENT for a NULL document. For the tests and
 * the fuzz targets.
 */
amark_error_t amark_tree_check(const amark_document_t *document, const char **out_reason);

#endif /* AMARK_TREE_TREE_H */
