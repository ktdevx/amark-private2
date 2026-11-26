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

AMARK_END_DECLS

#endif /* AMARK_NODE_H */
