/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The nodes of the syntax tree: the node type identifiers and the built-in types,
 * the queries, the structural editing, the literals and the built-in attributes,
 * the edit records, the comparison and the source span of a node.
 *
 * This header holds the forward declarations of the syntax tree, once: the other
 * headers of the syntax tree include it and do not repeat them (C99 does not allow
 * a typedef to be repeated).
 *
 * Conventions (those of amark/error.h, and):
 *   - A query given a NULL node returns "nothing": NULL, 0, false or "".
 *   - A function given a node of another document, or a node whose type does not
 *     fit the operation, returns AMARK_ERROR_INVALID_ARGUMENT.
 *   - A function that returns a string returns it NUL-terminated, and its length
 *     in *out_length when out_length is not NULL. The string is valid until the
 *     next change or the destruction of the node.
 *   - A public structure begins with size_t struct_size. For a structure the
 *     library writes (amark_source_span_t, amark_token_t, amark_token_report_t),
 *     the caller sets struct_size and the library writes only that many bytes.
 *
 * Every node belongs to one document (amark/document.h), which owns its memory:
 * destroying the document destroys all its nodes, including the detached ones.
 */
#ifndef AMARK_NODE_H
#define AMARK_NODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <amark/export.h>
#include <amark/error.h>

AMARK_BEGIN_DECLS

/* A document: the nodes, the sources and the edit tracking state (opaque). */
typedef struct amark_document amark_document_t;

/* A node of the syntax tree (opaque). */
typedef struct amark_node amark_node_t;

/* An ordered set of syntax extensions (opaque, amark/extension.h). */
typedef struct amark_syntax_set amark_syntax_set_t;

/* The descriptor of a node type (defined in amark/extension.h). */
typedef struct amark_node_type_desc amark_node_type_desc_t;

/* The identifier of a node type in a frozen syntax set. */
typedef uint32_t amark_node_type_t;

/* The identifier of a source registered in a document, from 0 in registration
 * order. */
typedef uint32_t amark_source_id_t;

/*
 * The values of amark_node_type_t. Extensions add types, so the set is open and
 * the values are constants rather than an enum. 1-255 are reserved for the
 * built-in types; the types of extensions get 256 and above, in the order of the
 * syntax set, when the set is frozen.
 */
#define AMARK_NODE_NONE              0u
#define AMARK_NODE_DOCUMENT          1u
#define AMARK_NODE_BLOCK_QUOTE       2u
#define AMARK_NODE_LIST              3u
#define AMARK_NODE_ITEM              4u
#define AMARK_NODE_CODE_BLOCK        5u
#define AMARK_NODE_HTML_BLOCK        6u
#define AMARK_NODE_PARAGRAPH         7u
#define AMARK_NODE_HEADING           8u
#define AMARK_NODE_THEMATIC_BREAK    9u
#define AMARK_NODE_LINK_DEFINITION  10u
#define AMARK_NODE_TEXT             11u
#define AMARK_NODE_SOFTBREAK        12u
#define AMARK_NODE_LINEBREAK        13u
#define AMARK_NODE_CODE             14u
#define AMARK_NODE_HTML_INLINE      15u
#define AMARK_NODE_EMPH             16u
#define AMARK_NODE_STRONG           17u
#define AMARK_NODE_LINK             18u
#define AMARK_NODE_IMAGE            19u
#define AMARK_NODE_BUILTIN_LAST    255u
#define AMARK_NODE_DYNAMIC_FIRST   256u

/* --- Creation and queries ------------------------------------------------------ */

/*
 * Creates a detached node of type in document, with the default attributes of the
 * type (its data zero-filled, then initialized by the init function of its
 * descriptor). Records AMARK_EDIT_CREATED when edit tracking is on. The document
 * type cannot be created (AMARK_ERROR_INVALID_ARGUMENT): a document has one root,
 * made with the document. An unknown type is AMARK_ERROR_INVALID_ARGUMENT.
 */
AMARK_API amark_error_t amark_node_create(amark_document_t *document, amark_node_type_t type,
                                          amark_node_t **out_node);

/* Returns the type of node (AMARK_NODE_NONE for NULL). */
AMARK_API amark_node_type_t amark_node_get_type(const amark_node_t *node);

/* Returns the name of the type of node ("paragraph", "gfm.table", ...). */
AMARK_API const char *amark_node_type_name(const amark_node_t *node);

/* Returns the descriptor of the type of node. */
AMARK_API const amark_node_type_desc_t *amark_node_desc(const amark_node_t *node);

/* Returns the document node belongs to. */
AMARK_API amark_document_t *amark_node_document(const amark_node_t *node);

/* The relations of node; NULL when there is none. */
AMARK_API amark_node_t *amark_node_parent(const amark_node_t *node);
AMARK_API amark_node_t *amark_node_first_child(const amark_node_t *node);
AMARK_API amark_node_t *amark_node_last_child(const amark_node_t *node);
AMARK_API amark_node_t *amark_node_next(const amark_node_t *node);
AMARK_API amark_node_t *amark_node_previous(const amark_node_t *node);

/* Whether the type of node is block-level or inline-level. */
AMARK_API bool amark_node_is_block(const amark_node_t *node);
AMARK_API bool amark_node_is_inline(const amark_node_t *node);

/* Whether the type of node can have children (it accepts a content class or has a
 * can_contain function). */
AMARK_API bool amark_node_can_have_children(const amark_node_t *node);

/* Whether a node of child_type may be a child of parent, by the containment rule
 * (the can_contain function of the type of parent, or else its accepts and the
 * provides of child_type). */
AMARK_API bool amark_node_can_contain(const amark_node_t *parent, amark_node_type_t child_type);

/* The pointer the application associates with node. Setting it records no edit,
 * and copies of node do not get it. */
AMARK_API void *amark_node_user_data(const amark_node_t *node);
AMARK_API void amark_node_set_user_data(amark_node_t *node, void *user_data);

/* The type data of node (data_size bytes of its descriptor); NULL when data_size
 * is 0. */
AMARK_API void *amark_node_data(amark_node_t *node);
AMARK_API const void *amark_node_data_const(const amark_node_t *node);

/* --- Edit records ------------------------------------------------------------- */

/*
 * The edit record bits of a node (amark_node_edits). Recorded only while edit
 * tracking is on (amark_document_set_edit_tracking), except that inserting a
 * subtree that already has records always propagates AMARK_EDIT_DESCENDANTS to
 * the new ancestors.
 *   AMARK_EDIT_CONTENT      The literal or an attribute (type data) of the node
 *                           changed.
 *   AMARK_EDIT_CHILDREN     A direct child was inserted, removed, moved or
 *                           replaced (including a reordering).
 *   AMARK_EDIT_DESCENDANTS  A descendant has a record. Every ancestor of a node
 *                           with this bit has it too.
 *   AMARK_EDIT_RELOCATED    The node was inserted as the result of a move, a copy
 *                           or an import: its tokens are valid, but the context
 *                           around it changed.
 *   AMARK_EDIT_CREATED      The node was created while tracking was on (it has no
 *                           tokens).
 * Bits from 0x20 up are reserved for future records. Treat an unknown bit as
 * AMARK_EDIT_CONTENT.
 */
#define AMARK_EDIT_CONTENT     0x01u
#define AMARK_EDIT_CHILDREN    0x02u
#define AMARK_EDIT_DESCENDANTS 0x04u
#define AMARK_EDIT_RELOCATED   0x08u
#define AMARK_EDIT_CREATED     0x10u

/* Returns the edit record bits of node. */
AMARK_API uint32_t amark_node_edits(const amark_node_t *node);

/* Records AMARK_EDIT_CONTENT on node (when tracking is on). The setters of an
 * extension type call it when they change the type data. */
AMARK_API void amark_node_mark_edited(amark_node_t *node);

/* --- Literals and attributes -------------------------------------------------- */

/*
 * Every string set below must be valid UTF-8 without NUL bytes. A line ending is
 * LF, CR or CRLF; a blank line is a line between two line endings that holds only
 * spaces and tabs. Setting the same value changes and records nothing; another
 * value records AMARK_EDIT_CONTENT. Setting a value never changes the syntax
 * tokens of the node. A getter on a node of another type returns the default (0,
 * AMARK_LIST_NONE, false or ""); a setter returns AMARK_ERROR_INVALID_ARGUMENT.
 */

/* Returns the literal of node ("" when it has none). */
AMARK_API const char *amark_node_literal(const amark_node_t *node, size_t *out_length);

/*
 * Sets the literal of node by the literal mode of its type: a line literal (text,
 * code) rejects line endings; a span literal (html_inline) rejects blank lines and
 * normalizes line endings to LF; a lines literal (code_block, html_block)
 * normalizes line endings to LF and, when not empty, ends with LF. A type without
 * a literal rejects it; then the validate_literal function of the type checks it
 * (code rejects an empty literal).
 */
AMARK_API amark_error_t amark_node_set_literal(amark_node_t *node, const char *text,
                                               size_t length);

/*
 * Replaces the children of node, which must be able to contain text, with the
 * text nodes and soft breaks of text split at its line endings (no empty text is
 * created). On failure nothing changes. Records AMARK_EDIT_CHILDREN on node and
 * AMARK_EDIT_CREATED on the new children.
 */
AMARK_API amark_error_t amark_node_set_inline_text(amark_node_t *node, const char *text,
                                                   size_t length);

/* The level of a heading, 1-6 (0 for another type). */
AMARK_API int amark_node_heading_level(const amark_node_t *node);
AMARK_API amark_error_t amark_node_set_heading_level(amark_node_t *node, int level);

/* The type of a list. */
typedef enum amark_list_type
{
    AMARK_LIST_NONE = 0,
    AMARK_LIST_BULLET = 1,
    AMARK_LIST_ORDERED = 2
} amark_list_type_t;

/* The type (AMARK_LIST_BULLET or AMARK_LIST_ORDERED), the start number (0-999999999)
 * and the tightness of a list. */
AMARK_API amark_list_type_t amark_node_list_type(const amark_node_t *node);
AMARK_API amark_error_t amark_node_set_list_type(amark_node_t *node, amark_list_type_t type);
AMARK_API long amark_node_list_start(const amark_node_t *node);
AMARK_API amark_error_t amark_node_set_list_start(amark_node_t *node, long start);
AMARK_API bool amark_node_list_tight(const amark_node_t *node);
AMARK_API amark_error_t amark_node_set_list_tight(amark_node_t *node, bool tight);

/* The info string of a code block (no line ending). */
AMARK_API const char *amark_node_code_info(const amark_node_t *node, size_t *out_length);
AMARK_API amark_error_t amark_node_set_code_info(amark_node_t *node, const char *text,
                                                 size_t length);

/* The destination (no line ending) and the title (no blank line) of a link, an
 * image or a link reference definition. */
AMARK_API const char *amark_node_link_destination(const amark_node_t *node, size_t *out_length);
AMARK_API amark_error_t amark_node_set_link_destination(amark_node_t *node, const char *text,
                                                        size_t length);
AMARK_API const char *amark_node_link_title(const amark_node_t *node, size_t *out_length);
AMARK_API amark_error_t amark_node_set_link_title(amark_node_t *node, const char *text,
                                                  size_t length);

/* The label of a link reference definition: 1-999 code points, at least one that
 * is not whitespace, no unescaped "[" or "]", no blank line. */
AMARK_API const char *amark_node_link_label(const amark_node_t *node, size_t *out_length);
AMARK_API amark_error_t amark_node_set_link_label(amark_node_t *node, const char *text,
                                                  size_t length);

/* --- Structural editing ------------------------------------------------------- */

/*
 * The insertion functions check, before changing the tree, that both nodes belong
 * to the same document, that node is not the root, that the sibling has a parent,
 * the containment rule and that node is not an ancestor of the new parent; any
 * violation is AMARK_ERROR_INVALID_ARGUMENT and leaves the tree unchanged.
 * Inserting a node that is in the tree moves it: the old parent and the new parent
 * get AMARK_EDIT_CHILDREN, and node gets AMARK_EDIT_RELOCATED unless it has
 * AMARK_EDIT_CREATED. Using the node itself as the sibling does nothing and
 * succeeds.
 */

/* Detaches node from its parent. The root is AMARK_ERROR_INVALID_ARGUMENT; a
 * detached node is left as is (success). */
AMARK_API amark_error_t amark_node_unlink(amark_node_t *node);

/* Detaches node when it is in the tree and destroys its subtree. Does nothing for
 * NULL or the root. */
AMARK_API void amark_node_destroy(amark_node_t *node);

AMARK_API amark_error_t amark_node_insert_before(amark_node_t *sibling, amark_node_t *node);
AMARK_API amark_error_t amark_node_insert_after(amark_node_t *sibling, amark_node_t *node);
AMARK_API amark_error_t amark_node_prepend_child(amark_node_t *parent, amark_node_t *node);
AMARK_API amark_error_t amark_node_append_child(amark_node_t *parent, amark_node_t *node);

/* Puts new_node at the place of old_node and detaches old_node. A detached
 * old_node is AMARK_ERROR_INVALID_ARGUMENT. */
AMARK_API amark_error_t amark_node_replace(amark_node_t *old_node, amark_node_t *new_node);

/* --- Copy and import ---------------------------------------------------------- */

/*
 * Copies the subtree of node into a detached subtree of the same document: the
 * types, the type data (the copy function, or else the bytes), the literals, the
 * tokens and the edit records. The associated pointers are not copied.
 */
AMARK_API amark_error_t amark_node_copy(const amark_node_t *node, amark_node_t **out_copy);

/*
 * Copies the subtree of node, which may belong to another document, into a
 * detached subtree of document. Every type of the subtree must be in the syntax
 * set of document (by its descriptor), or AMARK_ERROR_INVALID_ARGUMENT. The sources
 * the tokens refer to are copied into document. The root of a document is
 * AMARK_ERROR_INVALID_ARGUMENT (use amark_node_import_children). On failure
 * document is unchanged.
 */
AMARK_API amark_error_t amark_node_import(amark_document_t *document, const amark_node_t *node,
                                          amark_node_t **out_copy);

/*
 * Copies every child of source_parent, which may belong to another document, and
 * inserts the copies before before (at the end when before is NULL), a child of
 * parent, in their order. All or nothing: the types, the containment rule of each
 * child and before are checked, and every allocation is made, before the tree
 * changes; on failure the document of parent is unchanged. Records
 * AMARK_EDIT_CHILDREN on parent and AMARK_EDIT_RELOCATED on each copy. A
 * source_parent without children does nothing and succeeds.
 */
AMARK_API amark_error_t amark_node_import_children(amark_node_t *parent, amark_node_t *before,
                                                   const amark_node_t *source_parent);

/* --- Comparison --------------------------------------------------------------- */

/*
 * Compares each run of adjacent text siblings as one string (the concatenation of
 * their literals; a run never joins text across another node or across levels)
 * and ignores empty text nodes, so that a node whose children are all empty text
 * nodes equals a node without children. a and b themselves, when they are text
 * nodes, are compared by their own literals.
 */
#define AMARK_EQUAL_NORMALIZE_TEXT 0x01u

/*
 * Whether the subtrees of a and b have the same meaning: the same type
 * descriptors (the same descriptor is the same type in every syntax set), type
 * data (the equal function of the type when it has one, otherwise the data_size
 * bytes), literals and children, in order. The tokens, the positions, the edit
 * records and the associated pointers are ignored. a and b may belong to
 * different documents and syntax sets. false when a or b is NULL (also both).
 * Unknown bits of flags are ignored. Walks both subtrees without recursion and
 * allocates nothing.
 */
AMARK_API bool amark_node_equal(const amark_node_t *a, const amark_node_t *b, uint32_t flags);

/* --- Source span -------------------------------------------------------------- */

/* The range of a source covered by the tokens of a subtree. Lines and columns
 * count from 1; columns are in bytes. */
typedef struct amark_source_span
{
    size_t struct_size;
    amark_source_id_t source;
    size_t start_offset;
    size_t end_offset;
    size_t start_line;
    size_t start_column;
    size_t end_line;
    size_t end_column;
} amark_source_span_t;

/*
 * Writes in *out_span the range [start_offset, end_offset) of the source of the
 * first token of the subtree of node (in preorder, then in the order of the tokens
 * of each node) that the tokens of the subtree in that source cover, with its lines
 * and columns (amark_document_position; the end is the position of end_offset).
 * The tokens of other sources are ignored; zero-length tokens (a lazy continuation)
 * count as a position. node may be detached. Writes only struct_size bytes and
 * keeps the struct_size of the caller. Returns false, and writes nothing, when the
 * subtree has no token, node or out_span is NULL, or the struct_size of out_span is
 * below that of version 1.
 */
AMARK_API bool amark_node_source_span(const amark_node_t *node, amark_source_span_t *out_span);

AMARK_END_DECLS

#endif /* AMARK_NODE_H */
