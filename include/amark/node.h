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
