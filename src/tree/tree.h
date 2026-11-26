/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The internal header of the syntax tree: the internal structures (documents,
 * nodes, the record arrays, the content objects, the lines) and the internal
 * functions shared by the modules of src/tree/.
 */
#ifndef AMARK_TREE_TREE_H
#define AMARK_TREE_TREE_H

#include <amark/node.h>
#include <amark/document.h>
#include <amark/value.h>
#include <amark/spelling.h>
#include <amark/iterator.h>
#include <amark/dump.h>
#include <amark/extension.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- Registry (registry.c) ------------------------------------------------- */

/* The name of the built-in extension, always first in a syntax set. */
#define AMARK_TREE_CORE_EXTENSION_NAME "amark.core"

/* A node type of a frozen syntax set: its descriptor and its extension. */
typedef struct amark_registry_type
{
    const amark_node_type_desc_t *desc;
    const amark_extension_t *extension;
} amark_registry_type_t;

/* An entry of the name index of a frozen syntax set. */
typedef struct amark_registry_name
{
    const char *name;
    amark_node_type_t type;
} amark_registry_name_t;

/* A slot of a frozen syntax set, in the slot index sorted by kind and then by the
 * order of the set. */
typedef struct amark_registry_slot
{
    uint32_t kind;
    const amark_slot_t *slot;
    const amark_extension_t *extension;
} amark_registry_slot_t;

struct amark_syntax_set
{
    /* The extensions in order; extensions[0] is the built-in extension. */
    const amark_extension_t **extensions;
    size_t extension_count;
    size_t extension_capacity;
    bool frozen;
    /* The static built-in set (amark_syntax_set_core): never freed. */
    bool is_static;
    /* The tables built by freezing (empty before): the types of the built-in
     * extension (identifier i + 1 is builtin_types[i]), the types of the other
     * extensions (identifier AMARK_NODE_DYNAMIC_FIRST + i is dynamic_types[i]),
     * the names of all types sorted by name, and the slots sorted by kind. */
    amark_registry_type_t *builtin_types;
    size_t builtin_type_count;
    amark_registry_type_t *dynamic_types;
    size_t dynamic_type_count;
    amark_registry_name_t *names;
    size_t name_count;
    amark_registry_slot_t *slots;
    size_t slot_count;
};

/* Whether a node of type parent can have a child of type child in the frozen set
 * set: both types exist, child is not the document, parent accepts the content
 * class of child, and the can_contain function of parent, if any, accepts child
 * (it receives self, the would-be parent node, which may be NULL). */
bool amark_registry_can_contain(const amark_syntax_set_t *set, const amark_node_t *self,
                                amark_node_type_t parent, amark_node_type_t child);

/* ---- BuiltinTypes (builtin_types.c) ---------------------------------------- */

/* The built-in extension "amark.core" with the 19 built-in node types, in the
 * order of their identifiers (static; not freed). */
const amark_extension_t *amark_tree_core_extension(void);

/* Checks that set can be used by a document: AMARK_ERROR_INVALID_ARGUMENT for
 * NULL, AMARK_ERROR_INVALID_STATE when it is not frozen (12.8). */
amark_error_t amark_syntax_set_require_frozen(const amark_syntax_set_t *set);

/* ---- NodeCore (node.c) ---------------------------------------------------- */

/*
 * The storage of one facet of a node. A new node has its facet array
 * zero-filled; what the fields mean (the state, the default values, the
 * derivation cache) belongs to Facets.
 */
typedef struct amark_tree_facet
{
    uint8_t state;
    int64_t number;
    char *text;
    size_t length;
    char *cache;
    size_t cache_length;
} amark_tree_facet_t;

struct amark_node
{
    amark_document_t *document;
    const amark_node_type_desc_t *desc;
    amark_node_type_t type;
    /* The tree. */
    amark_node_t *parent;
    amark_node_t *first_child;
    amark_node_t *last_child;
    amark_node_t *next;
    amark_node_t *prev;
    /* The ring of every node of the document (in the tree or not). */
    amark_node_t *ring_next;
    amark_node_t *ring_prev;
    void *user_data;
    /* The cache flags (AMARK_TREE_CACHE_*). A node whose cache is valid has
     * only children whose cache is valid: a change invalidates the node and its
     * ancestors (amark_tree_changed). A new node has none. */
    uint32_t cache_flags;
    /* desc->facet_count entries (NULL when there is none). */
    amark_tree_facet_t *facets;
    /* desc->data_size bytes of extension data (NULL when 0). */
    void *data;
    /* The mark of the last internal check (amark_tree_check) that saw the
     * node; meaningless outside of it. */
    uint64_t check_mark;
};

/*
 * Allocates a node of type in document, calls the init function of the type and
 * adds the node to the ring of the document. AMARK_ERROR_INVALID_ARGUMENT for a
 * type the syntax set does not hold. On failure nothing is left allocated, the
 * document is unchanged and *out is NULL. The arguments are not NULL.
 */
amark_error_t amark_tree_node_new(amark_document_t *document, amark_node_type_t type,
                                  amark_node_t **out);

/* Releases one node: calls the deinit function of its type, removes it from the
 * ring of its document and frees it. Its tree links are not looked at. */
void amark_tree_node_release(amark_node_t *node);

/* Removes node from its parent and its siblings (node keeps its children) and
 * notifies the old parent of a structural change. Does nothing when node has
 * no parent. */
void amark_tree_node_detach(amark_node_t *node);

/* The cached results of a node (Verbatim) are valid. */
#define AMARK_TREE_CACHE_VALID 0x01u

/* The kinds of the changes notified with amark_tree_changed. */
typedef enum amark_tree_change
{
    /* The records, the values or the styles of the node changed. */
    AMARK_TREE_CHANGE_RECORD = 1,
    /* The children of the node changed (a node was inserted, moved, replaced
     * or removed under it). */
    AMARK_TREE_CHANGE_STRUCTURE = 2
} amark_tree_change_t;

/*
 * The change notification, called by the structural edits, the recording and
 * the setters after a change of node:
 *   - invalidates the cache of node and of its ancestors, up to the first one
 *     already invalid (its ancestors are then invalid too);
 *   - for a structural change, advances the structure generation of the
 *     document (DefinitionIndex uses it to drop its document-order results).
 * Each node visited counts one step of stats.invalidation_steps.
 */
void amark_tree_changed(amark_node_t *node, amark_tree_change_t kind);

/* ---- LineTable (line_table.c) ----------------------------------------------- */

/* The maximum number of lines of a line table (the identifiers stay below
 * AMARK_LINE_NONE). */
#define AMARK_TREE_MAX_LINES ((size_t)UINT32_MAX - 1u)

/* A tab of a line, in the index of the tabs of the line: its position in the
 * line and its first column. */
typedef struct amark_tree_tab
{
    uint32_t byte;
    uint32_t column;
} amark_tree_tab_t;

/* A completed line of the line table (amark_line_info_t without the size). */
typedef struct amark_tree_line
{
    amark_source_id_t source;
    uint32_t number;
    size_t offset;
    size_t length;
    uint8_t eol; /* amark_eol_t */
    /* Whether the line holds a tab (set when the line is completed). */
    bool has_tab;
    uint32_t width;
    /* The index of the tabs of the line, in order (tab_count entries), built by
     * the first conversion between columns and bytes on a line that has a tab;
     * NULL before, and always for a line without a tab. A line is shorter than
     * 4 GiB (its width is at most UINT32_MAX), so the positions fit in 32 bits. */
    amark_tree_tab_t *tab_index;
    uint32_t tab_count;
} amark_tree_line_t;

/*
 * A source: its bytes, its completed lines, and the state of the division into
 * lines (the unfinished line).
 */
typedef struct amark_tree_source
{
    /* The bytes, the BOM included (NULL while capacity is 0). */
    char *data;
    size_t length;
    size_t capacity;
    /* Whether the presence of the BOM is decided: it is once the source has 3
     * bytes, once its bytes are not a prefix of the BOM, and at the end of the
     * input. No line is completed before. */
    bool bom_known;
    bool has_bom;
    /* The input has ended (AMARK_SOURCE_FINISHED, amark_document_finish_source). */
    bool finished;
    /* The lines were given with the bytes (amark_line_table_append_lines), not
     * found by dividing them: a line without a line ending may be followed by
     * another line, and a CR line by a line whose bytes start with LF. Such a
     * source is finished and has no BOM. */
    bool given_lines;
    /* The unfinished line: its first byte, the end of the bytes already looked
     * at (a CR at the end of data waits there for the next byte), and the width
     * of the bytes before that end. */
    size_t line_start;
    size_t scanned;
    uint64_t width;
    /* Whether the bytes before that end hold a tab. */
    bool has_tab;
    /* The identifiers of the lines of the source, in order (lines[k] has the
     * number k + 1). */
    amark_line_id_t *lines;
    size_t line_count;
    size_t line_capacity;
} amark_tree_source_t;

/* Frees the sources and the line table of document and leaves them empty. */
void amark_line_table_deinit(amark_document_t *document);

/* The number of bytes of a line ending (0 for AMARK_EOL_NONE). */
size_t amark_line_table_eol_length(uint8_t eol);

/* A line given to amark_line_table_append_lines: the range of its bytes in the
 * given bytes (without the line ending) and its line ending. */
typedef struct amark_tree_line_range
{
    size_t offset;
    size_t length;
    uint8_t eol; /* amark_eol_t */
} amark_tree_line_range_t;

/*
 * Adds to document one new finished source made of the count given lines, in
 * order, without dividing its bytes again (CopyImport): line k is the bytes
 * bytes[ranges[k].offset, + ranges[k].length) followed by the bytes of its line
 * ending ranges[k].eol (none, LF, CRLF or CR), and gets the next identifier of
 * the line table and the number k + 1 in the source. So a line without a line
 * ending may be followed by another line, and a CR line by an empty LF line;
 * the new source has no BOM. The identifier of the source goes to *out; its
 * first line is the line count of the table before the call.
 * AMARK_ERROR_INVALID_ARGUMENT for a NULL document or out, NULL bytes with a
 * length, NULL ranges with a count, a range outside the bytes, an unknown line
 * ending and a range that holds a CR or an LF; AMARK_ERROR_LIMIT_EXCEEDED and
 * AMARK_ERROR_OVERFLOW as for amark_document_add_source;
 * AMARK_ERROR_OUT_OF_MEMORY. On failure nothing is added.
 */
amark_error_t amark_line_table_append_lines(amark_document_t *document, const char *bytes,
                                            size_t length, const amark_tree_line_range_t *ranges,
                                            size_t count, amark_source_id_t *out);

/* ---- Document (document.c) ------------------------------------------------- */

/*
 * The complexity counters of a document (internal statistics): the tests check
 * the complexity of the operations with them. Each component adds the counters
 * it needs.
 */
typedef struct amark_tree_stats
{
    /* The nodes allocated and released over the life of the document. */
    size_t nodes_created;
    size_t nodes_released;
    /* The steps of the walks that free subtrees. */
    size_t release_steps;
    /* The steps of the checks of the structural edits (the ancestors of the
     * insertion point and, under a link, the descendants of the inserted
     * node). */
    size_t structure_steps;
    /* The nodes visited by the change notification. */
    size_t invalidation_steps;
} amark_tree_stats_t;

struct amark_document
{
    /* The frozen syntax set, borrowed or owned. */
    const amark_syntax_set_t *set;
    /* The same set when the document owns it, otherwise NULL. */
    amark_syntax_set_t *owned_set;
    /* The document node; also the anchor of the ring of every node. */
    amark_node_t *root;
    /* The number of nodes in the ring, the root included. */
    size_t node_count;
    /* Advanced by every structural change (amark_tree_changed). */
    uint64_t structure_generation;
    amark_tree_stats_t stats;
    /* The sources (index = identifier) and the line table (index = line
     * identifier), and the line ending of the first line that has one (0 while
     * there is none). */
    amark_tree_source_t *sources;
    size_t source_count;
    size_t source_capacity;
    amark_tree_line_t *lines;
    size_t line_count;
    size_t line_capacity;
    uint8_t default_eol;
    /* The epoch of the internal check (amark_tree_check): each check advances
     * it by 2 and marks the nodes it sees with it. */
    uint64_t check_epoch;
};

/* ---- TreeCheck (tree_check.c) ------------------------------------------------ */

/* The kinds of the violations of the internal invariants. */
typedef enum amark_tree_violation
{
    AMARK_TREE_VIOLATION_NONE = 0,
    /* The ring of every node: a broken or asymmetric ring, a node of another
     * document, a count that differs from node_count, a node twice, or a node
     * of a tree that is not in the ring. */
    AMARK_TREE_VIOLATION_RING,
    /* The relations: the parent, child and sibling links are not symmetric, a
     * node is reached twice (a cycle), a node is not reachable from the top of
     * its tree, or the document node is not a top. */
    AMARK_TREE_VIOLATION_RELATION,
    /* The containment rules: the parent does not accept the child, or the
     * document node is not of the document type. */
    AMARK_TREE_VIOLATION_CONTAINMENT,
    /* A link among the descendants of a link. */
    AMARK_TREE_VIOLATION_LINK_IN_LINK,
    /* A node whose cache is valid has a child whose cache is not. */
    AMARK_TREE_VIOLATION_CACHE,
    /* The sources and the line table: the lines of a source are not the
     * division of its bytes in order (offsets, ranges, line endings, widths,
     * numbers, identifiers; for given lines, their sequence), the index of the
     * tabs of a line does not list its tabs, or the default line ending is not
     * the one of the first line that has one. */
    AMARK_TREE_VIOLATION_LINES
} amark_tree_violation_t;

/* The first violation found by amark_tree_check. */
typedef struct amark_tree_check_report
{
    amark_tree_violation_t kind;
    /* The node where the violation was found (the node that breaks the rule;
     * for a cache violation, the valid node with an invalid child), or NULL. */
    const amark_node_t *node;
    /* A static description of the broken rule, or NULL when there is none. */
    const char *message;
} amark_tree_check_report_t;

/*
 * Checks the internal invariants of document (for the tests and the fuzz
 * targets, after each operation) and returns the kind of the first violation,
 * or AMARK_TREE_VIOLATION_NONE. report, if not NULL, receives the first
 * violation. Runs in time proportional to the nodes of the document, without
 * recursion and without allocation; it only writes the check marks.
 *
 * The checks run in order and stop at the first violation, so that a later
 * check can rely on the invariants of the earlier ones: the document node, the
 * ring, then a walk of every tree that runs the node checks on each node
 * (relations, containment, link in link, cache). A component that adds
 * records or caches adds its invariant as a node check or a document check in
 * tree_check.c.
 */
amark_tree_violation_t amark_tree_check(amark_document_t *document,
                                        amark_tree_check_report_t *report);

/* The name of a violation kind, for the messages of the tests ("ring",
 * "relation", ...; "none" for AMARK_TREE_VIOLATION_NONE). */
const char *amark_tree_violation_name(amark_tree_violation_t kind);

#endif /* AMARK_TREE_TREE_H */
