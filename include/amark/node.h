/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The nodes of the syntax tree: the opaque types, the identifier types, the
 * identifiers of the built-in node types, the relations, the structural editing,
 * the copy, the import and the comparison.
 *
 * Threads (14.3, 14.4):
 *   - A document, with all of its nodes, is used by one thread at a time. Even
 *     the queries (getting a value, for example) write internal caches, so they
 *     are not read-only operations: using one document from several threads at
 *     once needs synchronization by the application.
 *   - Different documents can be used from different threads at the same time.
 *   - A frozen syntax set is immutable and can be shared by several documents
 *     and threads.
 */
#ifndef AMARK_NODE_H
#define AMARK_NODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <amark/export.h>
#include <amark/error.h>

AMARK_BEGIN_DECLS

/* A document: the sources, the line table and the tree of nodes (opaque). */
typedef struct amark_document amark_document_t;

/* A node of the syntax tree, owned by its document (opaque). */
typedef struct amark_node amark_node_t;

/* An ordered list of syntax extensions, and, once frozen, the node types and the
 * slots they define (opaque; amark/extension.h). */
typedef struct amark_syntax_set amark_syntax_set_t;

/* The descriptor of a node type (defined in amark/extension.h). */
typedef struct amark_node_type_desc amark_node_type_desc_t;

/*
 * The identifier of a node type within a frozen syntax set. 0 is no type, the
 * built-in types use 1-AMARK_NODE_BUILTIN_LAST, and the types of the other
 * extensions get AMARK_NODE_DYNAMIC_FIRST and above when the set is frozen, in
 * the order of the set.
 */
typedef uint32_t amark_node_type_t;

#define AMARK_NODE_NONE 0u
#define AMARK_NODE_BUILTIN_LAST 255u
#define AMARK_NODE_DYNAMIC_FIRST 256u

/* The identifiers of the built-in node types (the extension "amark.core"; their
 * names are the macro names in lower case without the prefix). */
#define AMARK_NODE_DOCUMENT 1u
#define AMARK_NODE_BLOCK_QUOTE 2u
#define AMARK_NODE_LIST 3u
#define AMARK_NODE_ITEM 4u
#define AMARK_NODE_CODE_BLOCK 5u
#define AMARK_NODE_HTML_BLOCK 6u
#define AMARK_NODE_PARAGRAPH 7u
#define AMARK_NODE_HEADING 8u
#define AMARK_NODE_THEMATIC_BREAK 9u
#define AMARK_NODE_LINK_DEFINITION 10u
#define AMARK_NODE_TEXT 11u
#define AMARK_NODE_SOFTBREAK 12u
#define AMARK_NODE_LINEBREAK 13u
#define AMARK_NODE_CODE 14u
#define AMARK_NODE_HTML_INLINE 15u
#define AMARK_NODE_EMPH 16u
#define AMARK_NODE_STRONG 17u
#define AMARK_NODE_LINK 18u
#define AMARK_NODE_IMAGE 19u

/* The level of a node type. 0 is invalid, so that a descriptor that forgets it is
 * rejected when it is added to a syntax set. */
typedef enum amark_node_level
{
    AMARK_NODE_LEVEL_BLOCK = 1,
    AMARK_NODE_LEVEL_INLINE = 2
} amark_node_level_t;

/* The number of a facet (a value or a style) within its node type: facet i of a
 * descriptor's facets array is number i + 1. 0 is no facet. */
typedef uint32_t amark_facet_t;

#define AMARK_FACET_NONE 0u
#define AMARK_FACET_MAX 31u

/* The identifier of a source of a document. */
typedef uint32_t amark_source_id_t;

/* The serial number of a line in the line table of a document, from 0. */
typedef uint32_t amark_line_id_t;

/* No line. */
#define AMARK_LINE_NONE UINT32_MAX

/* ---- Creation and release ---------------------------------------------------- */

/*
 * Creates in *out a node of type type that belongs to document: it is not in the
 * tree (no parent, no sibling, no child), has no line range and no spelling, and
 * its facets have the defaults of the type. When the type has extension data
 * (data_size), the data is zero-filled and then given to the init function of
 * the type, if any; when init fails, the node is freed (without deinit) and its
 * error is returned. AMARK_ERROR_INVALID_ARGUMENT for NULL and for a type the
 * syntax set of document does not hold. On failure *out is NULL and document is
 * unchanged.
 */
AMARK_API amark_error_t amark_node_create(amark_document_t *document, amark_node_type_t type,
                                          amark_node_t **out);

/*
 * Frees node and its descendants: when node is in a tree, it is first removed
 * from it. The deinit function of the type is called for each node that has
 * one. The walk uses neither recursion nor a stack, so a subtree of any depth
 * can be freed. Does nothing for NULL and for the root of a document (it is
 * freed with its document).
 */
AMARK_API void amark_node_destroy(amark_node_t *node);

/* ---- Relations (4.1) ----------------------------------------------------------- */

/* The type of node (AMARK_NODE_NONE for NULL). */
AMARK_API amark_node_type_t amark_node_get_type(const amark_node_t *node);

/* The descriptor of the type of node (NULL for NULL). */
AMARK_API const amark_node_type_desc_t *amark_node_type_desc(const amark_node_t *node);

/* The document node belongs to (NULL for NULL). */
AMARK_API amark_document_t *amark_node_document(const amark_node_t *node);

/* The parent, the first and the last child, and the next and the previous
 * sibling of node; NULL when there is none or node is NULL. */
AMARK_API amark_node_t *amark_node_parent(const amark_node_t *node);
AMARK_API amark_node_t *amark_node_first_child(const amark_node_t *node);
AMARK_API amark_node_t *amark_node_last_child(const amark_node_t *node);
AMARK_API amark_node_t *amark_node_next(const amark_node_t *node);
AMARK_API amark_node_t *amark_node_previous(const amark_node_t *node);

/*
 * The classification of the type of node (false for NULL):
 *   - is_block, is_inline: the level of the type.
 *   - is_container: a block with a margin on each line it covers
 *     (AMARK_NODE_TYPE_MARGINS: block_quote, item, the containers of extensions);
 *     document and list hold blocks but are not containers.
 *   - has_content: a leaf block with content (AMARK_NODE_TYPE_CONTENT).
 */
AMARK_API bool amark_node_is_block(const amark_node_t *node);
AMARK_API bool amark_node_is_inline(const amark_node_t *node);
AMARK_API bool amark_node_is_container(const amark_node_t *node);
AMARK_API bool amark_node_has_content(const amark_node_t *node);

/* Whether parent can have a child of type child under the containment rules of
 * the syntax set of its document (false for NULL and for an unknown type). */
AMARK_API bool amark_node_can_contain(const amark_node_t *parent, amark_node_type_t child);

/* ---- Structural editing (1.5, 5.2-5.5, 14.5) ------------------------------------ */

/*
 * The structural edits change only the links: the moved nodes keep their
 * subtrees, their layout and their spelling (5.3). A node that is already in a
 * tree (or under a detached node) is first removed from its place, so an edit
 * also moves nodes within the tree.
 *
 * AMARK_ERROR_INVALID_ARGUMENT, with no change, for:
 *   - NULL;
 *   - nodes of different documents;
 *   - the document node as the node to place, to unlink or to replace, or as the
 *     anchor of insert_before and insert_after;
 *   - an anchor (insert_before, insert_after) or an old node (replace) without a
 *     parent: it has no position;
 *   - a node placed under itself or under one of its descendants;
 *   - a violation of the containment rules of the new parent
 *     (amark_node_can_contain), and a link that would get a link among its
 *     descendants, at any depth.
 *
 * The time is proportional to the depths of the old and the new places, whatever
 * the number of nodes of the document (14.5); an insertion under a link also
 * walks the inserted subtree.
 */

/* Removes node (with its subtree) from the tree; it stays in its document and
 * can be inserted again (1.5). Does nothing when node has no parent. */
AMARK_API amark_error_t amark_node_unlink(amark_node_t *node);

/* Places node just before or just after anchor, under the parent of anchor.
 * Nothing changes when node is anchor. */
AMARK_API amark_error_t amark_node_insert_before(amark_node_t *anchor, amark_node_t *node);
AMARK_API amark_error_t amark_node_insert_after(amark_node_t *anchor, amark_node_t *node);

/* Places node as the first or the last child of parent. */
AMARK_API amark_error_t amark_node_prepend_child(amark_node_t *parent, amark_node_t *node);
AMARK_API amark_error_t amark_node_append_child(amark_node_t *parent, amark_node_t *node);

/* Places new_node where old_node is and leaves old_node (with its remaining
 * subtree) detached (5.4). new_node may come from anywhere in the document, even
 * from the subtree of old_node. Nothing changes when new_node is old_node. */
AMARK_API amark_error_t amark_node_replace(amark_node_t *old_node, amark_node_t *new_node);

/* ---- Copying (5.6) ----------------------------------------------------------------- */

/*
 * Copies node and its subtree into *out, a new detached node of the same
 * document (5.6). Each copied node has the type, the values and the styles
 * (set or recorded), the line range, the layout records, the content tokens,
 * the dropped-spelling marks and the extension data of its original (copied
 * by the copy function of the type, or byte for byte when the type has none);
 * the user pointer is not copied (NULL). A leaf shares the content object of
 * its original and the content tokens stay on the same base: later edits
 * copy shared content before changing it, so an edit of one copy never
 * changes the values, the spelling, the verbatim judgment or the positions of
 * the other. A copied link_definition is looked up with its label from its
 * insertion into the tree on. The subtree is walked without recursion.
 *
 * AMARK_ERROR_INVALID_ARGUMENT for NULL arguments and the document node;
 * AMARK_ERROR_OUT_OF_MEMORY, or the error of the copy function of a type. On
 * failure *out is NULL and the document is unchanged.
 */
AMARK_API amark_error_t amark_node_copy(const amark_node_t *node, amark_node_t **out);

/* ---- Importing (5.7–5.9) ----------------------------------------------------------- */

/*
 * Imports node and its subtree from its document into target: *out receives a
 * new detached node of target that is a copy of node as amark_node_copy makes
 * it, except that
 *   - each type is the type of the same name in the syntax set of target;
 *   - the lines the subtree refers to (the line ranges, the margins, the blank
 *     lines, the layout tokens and the pieces of the content) are added to
 *     target as one new finished source, in their order, each with its bytes
 *     and its line ending as they are (a line without a line ending may be
 *     followed by another line, and a CR line by an empty LF line), and the
 *     copied records refer to them, so the spelling is kept (5.7);
 *   - each content object is copied once, so the nodes that share one in the
 *     source share its copy in target.
 * A copied link_definition is looked up with its label from its insertion into
 * the tree on. Nothing is added when the subtree refers to no line. An import
 * into the document of node is amark_node_copy. The subtree is walked without
 * recursion.
 *
 * AMARK_ERROR_INVALID_ARGUMENT for NULL arguments, the document node, and a
 * type of the subtree that target does not have (or describes with other
 * facets, flags or data), or that the syntax set of target does not accept
 * under its parent (5.9); AMARK_ERROR_LIMIT_EXCEEDED when target cannot hold
 * the lines; AMARK_ERROR_OUT_OF_MEMORY, or the error of the copy function of a
 * type. On failure *out is NULL and neither document changes.
 */
AMARK_API amark_error_t amark_node_import(amark_document_t *target, const amark_node_t *node,
                                          amark_node_t **out);

/*
 * Imports every child of source_parent, in order, with its subtree, as
 * amark_node_import does (one new source for all of them), and inserts them
 * under target_parent before before, or at the end when before is NULL, in one
 * operation (5.8). source_parent may be the document node, and may be in the
 * document of target_parent (the children are then copied). Nothing happens
 * when source_parent has no child.
 *
 * AMARK_ERROR_INVALID_ARGUMENT, inserting nothing and changing no document, for
 * NULL arguments, a before that is not a child of target_parent, a child that
 * target_parent does not accept or a link that would get a link among its
 * descendants, and the errors of amark_node_import (5.9). All are copied
 * before any is inserted: on failure nothing changes.
 */
AMARK_API amark_error_t amark_node_import_children(amark_node_t *target_parent,
                                                   amark_node_t *before,
                                                   const amark_node_t *source_parent);

/* ---- User pointers and extension data (4.5) ------------------------------------ */

/* The pointer the application associated with node (NULL until set, and for a
 * NULL node). Amark never uses nor frees it. */
AMARK_API void *amark_node_user_data(const amark_node_t *node);

/* Associates user_data with node. Does nothing for a NULL node. */
AMARK_API void amark_node_set_user_data(amark_node_t *node, void *user_data);

/* The extension data of node (data_size bytes of its type), or NULL when the type
 * has none or node is NULL. */
AMARK_API void *amark_node_data(amark_node_t *node);

AMARK_END_DECLS

#endif /* AMARK_NODE_H */
