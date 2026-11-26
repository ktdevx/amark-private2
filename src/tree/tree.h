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

/* ---- Layout (layout.c) ------------------------------------------------------ */

/* A margin of a container (amark_margin_t without the size; 16 bytes). */
typedef struct amark_tree_margin
{
    amark_line_id_t line;
    uint32_t start;
    uint32_t end;
    uint8_t state; /* amark_margin_state_t */
    uint8_t flags; /* AMARK_MARGIN_* */
    uint16_t role;
} amark_tree_margin_t;

/* A blank line of a block (amark_blank_line_t without the size). */
typedef struct amark_tree_blank_line
{
    amark_line_id_t line;
    uint32_t start;
    uint32_t end;
} amark_tree_blank_line_t;

/* A layout token of a block (amark_layout_token_t without the size; 16 bytes). */
typedef struct amark_tree_layout_token
{
    amark_line_id_t line;
    uint32_t start;
    uint32_t end;
    uint8_t kind;  /* amark_spell_kind_t */
    uint8_t facet; /* amark_facet_t, at most AMARK_FACET_MAX */
    uint16_t role;
} amark_tree_layout_token_t;

/*
 * The layout records of a node, allocated by its first record (a new node has
 * none). Each array is in the order of its records (margins and blank lines by
 * line, one per line; tokens by line and column, without overlap), grows by
 * doubling and is NULL while its capacity is 0.
 */
typedef struct amark_tree_records
{
    amark_tree_margin_t *margins;
    size_t margin_count;
    size_t margin_capacity;
    amark_tree_blank_line_t *blank_lines;
    size_t blank_line_count;
    size_t blank_line_capacity;
    amark_tree_layout_token_t *tokens;
    size_t token_count;
    size_t token_capacity;
} amark_tree_records_t;

/* Frees records and what it holds (NULL does nothing). */
void amark_tree_records_free(amark_tree_records_t *records);

/*
 * Makes room for extra more layout tokens in the records of node: *out receives
 * the record structure of node, allocated when node has none (*created tells),
 * which the caller stores in node once it commits, or frees with
 * amark_tree_records_free when it was created and the caller fails. The tokens
 * array grows by doubling (the tokens moved count in the record steps). On
 * failure node is unchanged and nothing is left allocated.
 */
amark_error_t amark_tree_reserve_layout_tokens(amark_node_t *node, size_t extra,
                                               amark_tree_records_t **out, bool *created);

/*
 * Checks a record of node on line with the columns [start, end): the line is in
 * the line table and in the line range of node, start <= end <= the width of
 * the line, and both boundaries are boundaries of characters or inside tabs.
 * AMARK_ERROR_INVALID_ARGUMENT otherwise; AMARK_ERROR_OUT_OF_MEMORY when the
 * index of the tabs of the line cannot be built (an internal cache of the line
 * table; the node is not changed).
 */
amark_error_t amark_tree_check_record_columns(const amark_node_t *node, amark_line_id_t line,
                                              uint32_t start, uint32_t end);

/* The dropped-spelling marks of a node (amark_node_t.dropped): bit 0 is the
 * structural spelling (the margins, the blank lines and the layout tokens),
 * bit n (1 to 31) is the spelling of facet n. */
#define AMARK_TREE_DROPPED_STRUCTURE 0x01u

/* ---- Content (content.c) ---------------------------------------------------- */

/* The largest content: a content is shorter than 4 GiB, so that the positions
 * of the content tokens fit in 32 bits (AMARK_ERROR_LIMIT_EXCEEDED beyond). */
#define AMARK_TREE_MAX_CONTENT ((size_t)UINT32_MAX)

/* A piece of laid content (amark_content_piece_t without the size). */
typedef struct amark_tree_piece
{
    amark_line_id_t line;
    uint32_t start;
    uint32_t end;
    size_t content_start;
    size_t content_length;
} amark_tree_piece_t;

/*
 * A content object: the content of the leaves that own it. It refers to no
 * node; the nodes refer to it, counted in two separate reference counts:
 *   - owners: the leaves whose content it is (amark_node_t.content);
 *   - bases: the nodes whose content tokens are on it (Content tokens).
 * It is freed when both counts reach 0 (amark_tree_content_release). A shared
 * object (owners >= 2, or bases >= 1 for an edit that moves existing bytes) is
 * copied before it changes, so its existing bytes and state never change while
 * it is shared.
 *
 *   - state: AMARK_CONTENT_LAID or AMARK_CONTENT_FREE.
 *   - bytes, length, capacity: the bytes of the content, terminated by NUL
 *     (bytes is NULL while capacity is 0); length is at most
 *     AMARK_TREE_MAX_CONTENT. Laid content is the pieces joined by a virtual
 *     LF between lines; free content holds no CR.
 *   - line_count: for laid content, the lines that have a piece (an empty line
 *     is an empty piece); for free content, 0 when empty, else its LFs + 1,
 *     except that the value "\n" of a CONTENT facet is empty content with one
 *     line (amark_tree_content_from_value).
 *   - pieces: for laid content, its pieces in the order of the lines and the
 *     columns (grows by doubling; NULL while the capacity is 0); none for free
 *     content.
 *   - check_mark, check_owners, check_bases: the scratch of the internal check
 *     (amark_tree_check); meaningless outside of it.
 */
typedef struct amark_tree_content
{
    size_t owners;
    size_t bases;
    uint8_t state; /* amark_content_state_t */
    char *bytes;
    size_t length;
    size_t capacity;
    size_t line_count;
    amark_tree_piece_t *pieces;
    size_t piece_count;
    size_t piece_capacity;
    uint64_t check_mark;
    size_t check_owners;
    size_t check_bases;
} amark_tree_content_t;

/* Releases one reference to content: one owner when owner is true, else one
 * base; frees the object once both counts are 0 (NULL does nothing). */
void amark_tree_content_release(amark_tree_content_t *content, bool owner);

/* A content token (amark_content_token_t without the size; 12 bytes): the
 * bytes [start, end) of the base of its node. */
typedef struct amark_tree_content_token
{
    uint32_t start;
    uint32_t end;
    uint8_t kind;  /* amark_spell_kind_t */
    uint8_t facet; /* amark_facet_t, at most AMARK_FACET_MAX */
    uint16_t role;
} amark_tree_content_token_t;

/*
 * The content tokens of a node, allocated by its first token (a new node has
 * none):
 *   - base: the content object the tokens are on, of which the node counts as
 *     one base; NULL exactly when count is 0;
 *   - items: the tokens in the order of the content, nonempty and without
 *     overlap, within the bytes of the base (grows by doubling; NULL while the
 *     capacity is 0).
 */
typedef struct amark_tree_content_tokens
{
    amark_tree_content_t *base;
    amark_tree_content_token_t *items;
    size_t count;
    size_t capacity;
} amark_tree_content_tokens_t;

/* Frees tokens, releasing its base (NULL does nothing). */
void amark_tree_content_tokens_free(amark_tree_content_tokens_t *tokens);

/*
 * Makes a new free content object from the value of a CONTENT facet (Facets,
 * the setting of a CONTENT facet): value, of length bytes, is normalized to LF
 * here (CRLF and CR become LF, also for a CONTENT facet without LINES);
 * the content is value without its last LF, if any, and its line count is 0
 * for an empty value, else the LFs of the content + 1 (so "" has no line and
 * "\n" one empty line). The object has one owner, for the leaf that takes it
 * with amark_tree_leaf_take_content, or that the caller releases with
 * amark_tree_content_release(content, true). AMARK_ERROR_OUT_OF_MEMORY and
 * AMARK_ERROR_LIMIT_EXCEEDED leave nothing allocated.
 */
amark_error_t amark_tree_content_from_value(const char *value, size_t length,
                                            amark_tree_content_t **out);

/* Removes every layout and content token of node that spells facet (not
 * AMARK_FACET_NONE); the content tokens release their base when none is left.
 * Allocates nothing; drops the derived values of node; does not notify. */
void amark_tree_remove_facet_tokens(amark_node_t *node, amark_facet_t facet);

/* Makes content (one owner, from amark_tree_content_from_value) the content of
 * leaf, a block with content, releasing the previous one; drops the derived
 * CONTENT facets of leaf and notifies the change. Allocates nothing. */
void amark_tree_leaf_take_content(amark_node_t *leaf, amark_tree_content_t *content);

/* Whether the node type desc has a CONTENT facet (the literal of code_block and
 * html_block). Only the content of such a leaf can be empty free content with
 * one line (the value "\n" of the facet); empty free content of any other leaf
 * has no line. */
bool amark_tree_type_has_content_facet(const amark_node_type_desc_t *desc);

/*
 * Makes a new free content object with the bytes and the line count of source
 * (Facets, dropping the spelling of a leaf: laid content becomes free with the
 * same bytes; a virtual LF becomes a real one), for a leaf of the type desc:
 * when source is empty and desc has no CONTENT facet, the copy has no line
 * (amark_tree_type_has_content_facet). The object has one owner, as for
 * amark_tree_content_from_value. AMARK_ERROR_OUT_OF_MEMORY leaves nothing
 * allocated.
 */
amark_error_t amark_tree_content_free_copy(const amark_tree_content_t *source,
                                           const amark_node_type_desc_t *desc,
                                           amark_tree_content_t **out);

/* As amark_tree_leaf_take_content, and when content has the same bytes as the
 * old content of leaf, the content tokens of leaf and of its descendants on
 * the old content move to content first (otherwise they stay on the old one).
 * Allocates nothing. */
void amark_tree_leaf_replace_content(amark_node_t *leaf, amark_tree_content_t *content);

/* ---- Verbatim (verbatim.c) ---------------------------------------------------- */

/* The kinds of the first inconsistency of the coverage of a content
 * (amark_tree_content_cover). */
typedef enum amark_tree_cover_problem
{
    /* The tokens cover the content exactly once. */
    AMARK_TREE_COVER_NONE = 0,
    /* A byte of the content is covered by no token. */
    AMARK_TREE_COVER_GAP,
    /* A byte of the content is covered by more than one token. */
    AMARK_TREE_COVER_OVERLAP,
    /* A token is on another content object than the content of the leaf. */
    AMARK_TREE_COVER_FOREIGN
} amark_tree_cover_problem_t;

/* The result of amark_tree_content_cover. */
typedef struct amark_tree_cover
{
    /* The tokens cover the content exactly once, all on the content of the
     * leaf (problem is AMARK_TREE_COVER_NONE). */
    bool covered;
    amark_tree_cover_problem_t problem;
    /* The position of the first inconsistency: the byte of the content not
     * covered once, or the start of the foreign token (on its own base); 0
     * when covered. */
    size_t offset;
    /* The node of the foreign token; NULL for the other problems. */
    const amark_node_t *node;
} amark_tree_cover_t;

/* The scratch of amark_tree_content_cover: an array of counts that grows to the
 * longest content it was used for, so that a walk of many leaves (13.2, the
 * lossless check) allocates it once. Zero-initialize it; release it with
 * amark_tree_cover_scratch_free. */
typedef struct amark_tree_cover_scratch
{
    int32_t *counts;
    size_t capacity;
} amark_tree_cover_scratch_t;

/* Frees the array of scratch and leaves it empty (NULL does nothing). */
void amark_tree_cover_scratch_free(amark_tree_cover_scratch_t *scratch);

/*
 * The coverage of the content of leaf (a block with content) by the content
 * tokens of leaf and of its descendants (11.3, 10.2): whether each byte of the
 * content is covered by exactly one token and every token is on the content of
 * leaf. *out receives the first inconsistency in the order of the content
 * positions: a gap or an overlap at its byte, or a foreign token at its start
 * (on its own base; a foreign token comes first at the same position). Content
 * that is none counts as empty, every token then being foreign. The counts are a
 * difference array in scratch (grown when the content is longer than it); the
 * time is proportional to the length of the content, the tokens and the nodes
 * under leaf. AMARK_ERROR_OUT_OF_MEMORY when scratch cannot grow (*out is then
 * not written). The arguments are not NULL.
 */
amark_error_t amark_tree_content_cover(const amark_node_t *leaf,
                                       amark_tree_cover_scratch_t *scratch,
                                       amark_tree_cover_t *out);

/*
 * The value derived from the layout for an OVERRIDABLE facet of node without an
 * override, the step 2 of the getter (design, Tree: Facets): for the tight of
 * a list (3.7), true with *number 1 for tight and 0 for loose, the result
 * cached in the cache flags of the list (AMARK_TREE_CACHE_TIGHT); false for a
 * facet without such a derivation (*number is then not written). The arguments
 * are not NULL. Allocates nothing.
 */
bool amark_tree_derive_overridable(const amark_node_t *node, amark_facet_t facet,
                                   int64_t *number);

/*
 * Whether list is loose (3.7), without the cache: a blank line held by list
 * lies between two of its sibling children, or a blank line held by a child of
 * list lies between two sibling children of that child. "Between" is after the
 * last line of the earlier block and before the first line of the next one;
 * children without a line range are left out. Blank lines held by deeper
 * blocks (a nested list, a block quote) do not count. Each child and blank
 * line examined counts one step of *steps: the time is proportional to the
 * children of list, their children and the blank lines of them all.
 */
bool amark_tree_list_loose(const amark_node_t *list, size_t *steps);

/* ---- NodeCore (node.c) ---------------------------------------------------- */

/* The states of the storage of a facet (amark_tree_facet_t.state). */
/* No value is stored: the getter returns the derived value, if any, else the
 * default value of a value facet and UNSET for a style. A CONTENT facet is
 * always in this state: its value is the content of the leaf. */
#define AMARK_TREE_FACET_NONE 0u
/* A value set by an edit (amark_node_set_*): for an OVERRIDABLE facet, the
 * override. */
#define AMARK_TREE_FACET_SET 1u
/* A value recorded by a parser or an extension (amark_node_record_*). */
#define AMARK_TREE_FACET_RECORDED 2u

/*
 * The storage of one facet of a node (Facets, facet.c). A new node has its
 * facet array zero-filled, so every facet starts in AMARK_TREE_FACET_NONE.
 *   - state: AMARK_TREE_FACET_NONE, _SET or _RECORDED (always NONE for a
 *     CONTENT facet).
 *   - number: the value of an INT, ENUM or BOOL facet that has one, within the
 *     range of the facet; 0 otherwise.
 *   - text, length: the value of a STRING facet that has one: an owned copy,
 *     terminated by NUL, of valid UTF-8 without NUL, normalized to LF for a
 *     LINES facet; NULL and 0 otherwise.
 *   - cache, cache_length: the string derived from the tokens of the facet,
 *     or from the content of the leaf for a CONTENT facet (derive.c),
 *     terminated by NUL; NULL and 0 while there is none. It is dropped when
 *     the tokens of the node or the content of the leaf change, and equals
 *     the value derived now while it is there.
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
    /* The columns remembered by the verbatim judgment (Verbatim, verbatim.c),
     * one per line of the line range: the start columns (AMARK_TREE_CACHE_STARTS)
     * and then the end columns (AMARK_TREE_CACHE_ENDS). Meaningful only while
     * the cache is valid; a stale array is kept until the next judgment of the
     * node or its release. NULL when there is none. */
    uint32_t *verbatim_columns;
    /* The leaf of an inline node (the nearest block among its ancestors, or
     * NULL), remembered by the verbatim judgment (verbatim.c) while
     * leaf_memo_generation is the structure generation of the document + 1 (0:
     * nothing remembered). A remembered inline node has a block parent that is
     * its leaf, or an inline parent that remembers the same leaf. Not part of
     * the value of the node: a copy does not carry it. */
    amark_node_t *leaf_memo;
    uint64_t leaf_memo_generation;
    /* desc->facet_count entries (NULL when there is none). */
    amark_tree_facet_t *facets;
    /* desc->data_size bytes of extension data (NULL when 0). */
    void *data;
    /* The line range of a block (Layout): line_first and line_count lines; no
     * range while line_count is 0 (line_first is then meaningless). */
    amark_line_id_t line_first;
    uint32_t line_count;
    /* The layout records (Layout), NULL until the first record. */
    amark_tree_records_t *records;
    /* The content of a leaf (Content), of which the leaf is an owner; NULL
     * while it has none (AMARK_CONTENT_NONE). */
    amark_tree_content_t *content;
    /* The content tokens (Content) of a leaf or an inline node, NULL until
     * the first token. */
    amark_tree_content_tokens_t *content_tokens;
    /* The dropped-spelling marks (AMARK_TREE_DROPPED_*). */
    uint32_t dropped;
    /* The definition index (DefinitionIndex, def_index.c), for a
     * link_definition only: the group of its key (NULL while it is in none),
     * its neighbours among the members of that group, and the dirty mark with
     * the links of the dirty list of the document. */
    struct amark_tree_def_group *def_group;
    amark_node_t *def_next;
    amark_node_t *def_prev;
    bool def_dirty;
    amark_node_t *dirty_next;
    amark_node_t *dirty_prev;
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

/* Adds node, allocated for its document (node->document) and not yet in its
 * ring, at the end of the ring (the first node, the document node, makes a ring
 * of its own) and counts it as created. Allocates nothing. Used by
 * amark_tree_node_new and by CopyImport, which commits the nodes it built. */
void amark_tree_node_link(amark_node_t *node);

/* Releases one node: calls the deinit function of its type, removes it from the
 * definition index and the ring of its document and frees it. Its tree links
 * are not looked at. */
void amark_tree_node_release(amark_node_t *node);

/* Removes node from its parent and its siblings (node keeps its children) and
 * notifies the old parent of a structural change. Does nothing when node has
 * no parent. */
void amark_tree_node_detach(amark_node_t *node);

/* The cached verbatim results of a node (Verbatim) are valid. The flags up to
 * AMARK_TREE_CACHE_ENDS are meaningful only while this one is set. */
#define AMARK_TREE_CACHE_VALID 0x01u
/* The verbatim judgment of the node holds (verbatim.c): for a block, every
 * condition of 11.4 except that each line is covered up to its end; for an
 * inline node, the conditions of 11.3 within its own range. */
#define AMARK_TREE_CACHE_VERBATIM 0x02u
/* With AMARK_TREE_CACHE_VERBATIM, a block: the coverage of each of its lines
 * reaches the end of the line. */
#define AMARK_TREE_CACHE_FULL_LINES 0x04u
/* With AMARK_TREE_CACHE_VERBATIM, a block that is not a container: the start
 * column of each line is in verbatim_columns. */
#define AMARK_TREE_CACHE_STARTS 0x08u
/* With AMARK_TREE_CACHE_VERBATIM and without AMARK_TREE_CACHE_FULL_LINES, a
 * block: the end column of the coverage of each line is in verbatim_columns
 * (after the start columns, if any). */
#define AMARK_TREE_CACHE_ENDS 0x10u
/* A list: the tightness derived from its blank lines (Verbatim,
 * amark_tree_derive_overridable) is cached, loose with AMARK_TREE_CACHE_LOOSE.
 * Independent of AMARK_TREE_CACHE_VALID: the derivation reads the list, its
 * children and their children only, so the change notification clears it on
 * the changed node, its parent and its grandparent whatever their verbatim
 * results. */
#define AMARK_TREE_CACHE_TIGHT 0x20u
#define AMARK_TREE_CACHE_LOOSE 0x40u

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
 *     document (DefinitionIndex uses it to drop its document-order results);
 *   - for a change of the records or the values, puts a link_definition on the
 *     dirty list of the definition index (its label may have changed).
 * Each node visited counts one step of stats.invalidation_steps. The cached
 * tightness (AMARK_TREE_CACHE_TIGHT) of node, its parent and its grandparent
 * is cleared as well, as the tightness of a list depends on its children and
 * on their children.
 */
void amark_tree_changed(amark_node_t *node, amark_tree_change_t kind);

/* ---- DefinitionIndex (label.c, def_index.c) -------------------------------- */

/*
 * A group of the definition index: the link_definition nodes (attached or not)
 * whose label normalizes to key.
 *   - bucket_next: the next group of the same bucket of the table;
 *   - hash: the hash of key (amark_tree_label_hash);
 *   - key, key_length: the normalized label, owned, terminated by NUL, never
 *     empty;
 *   - first, last, count: the members, linked by def_next and def_prev;
 *   - cached, cached_generation, cached_valid: the result of the last lookup
 *     (the first member attached to the document node in document order, or
 *     NULL for none) and the structure generation it was decided at; dropped
 *     when the members change.
 */
typedef struct amark_tree_def_group
{
    struct amark_tree_def_group *bucket_next;
    uint64_t hash;
    char *key;
    size_t key_length;
    amark_node_t *first;
    amark_node_t *last;
    size_t count;
    amark_node_t *cached;
    uint64_t cached_generation;
    bool cached_valid;
} amark_tree_def_group_t;

/*
 * The definition index of a document: a hash table of the groups (buckets,
 * bucket_count a power of 2 or 0, group_count groups in all), and the dirty
 * list (the link_definition nodes whose label may have changed since they were
 * put in their group, in the order they were marked).
 */
typedef struct amark_tree_def_index
{
    amark_tree_def_group_t **buckets;
    size_t bucket_count;
    size_t group_count;
    amark_node_t *dirty_first;
    amark_node_t *dirty_last;
    size_t dirty_count;
} amark_tree_def_index_t;

/* The hash of a normalized label (FNV-1a). */
uint64_t amark_tree_label_hash(const char *key, size_t length);

/* Whether the label of length bytes normalizes (amark_label_normalize) to the
 * key_length bytes at key. Allocates nothing (for the internal check). */
bool amark_tree_label_matches(const char *label, size_t length, const char *key,
                              size_t key_length);

/* Puts node on the dirty list of its document when it is a link_definition
 * not already on it: called by the change notification for every change of
 * the records or the values of a node (amark_tree_changed). Allocates
 * nothing. */
void amark_tree_def_index_mark(amark_node_t *node);

/* Removes node from its group and from the dirty list (the release of a
 * node). Allocates nothing. */
void amark_tree_def_index_forget(amark_node_t *node);

/* Frees the table of the index of document (after its nodes are released). */
void amark_tree_def_index_deinit(amark_document_t *document);

/*
 * The lookup (amark_document_find_definition without the argument checks):
 * processes the dirty list, then finds in *out the first link_definition
 * attached to the document node, in document order, whose label normalizes as
 * label does, or NULL. AMARK_ERROR_OUT_OF_MEMORY writes NULL; the index stays
 * consistent (the definitions not yet processed stay on the dirty list).
 */
amark_error_t amark_tree_find_definition(amark_document_t *document, const char *label,
                                         size_t length, amark_node_t **out);

/* ---- Facets (facet.c) -------------------------------------------------------- */

/*
 * The parts of a facet edit for the on_change hooks of the built-in types
 * (BuiltinTypes), which change several facets all or nothing: they prepare
 * every allocation first, then commit with the functions below, which allocate
 * nothing and cannot fail.
 */

/* Prepares in *text and *length an owned copy, terminated by NUL, of the
 * current value of the STRING facet of node as amark_node_get reads it (an
 * UNRESOLVED value is the empty string). AMARK_ERROR_OUT_OF_MEMORY leaves
 * nothing allocated. */
amark_error_t amark_tree_facet_copy_value(const amark_node_t *node, amark_facet_t facet,
                                          char **text, size_t *length);

/* Prepares in *text and *length an owned copy of the value of the STRING facet
 * (not CONTENT) of node derived from its tokens, or NULL and 0 when no token
 * spells it: the value that dropping its spelling keeps as the set value.
 * AMARK_ERROR_OUT_OF_MEMORY leaves nothing allocated. */
amark_error_t amark_tree_facet_copy_spelled(const amark_node_t *node, amark_facet_t facet,
                                            char **text, size_t *length);

/* Removes the tokens of node that spell facet and marks the spelling of facet
 * as dropped. Allocates nothing and does not notify. */
void amark_tree_drop_facet_spelling(amark_node_t *node, amark_facet_t facet);

/* Stores number, or text (owned by the node from now on, from the copies
 * above) and length, as the set value of facet of node, releasing the previous
 * one, and notifies the change. Allocates nothing. */
void amark_tree_facet_commit_set(amark_node_t *node, amark_facet_t facet, int64_t number,
                                 char *text, size_t length);

/* ---- Derive (derive.c) ------------------------------------------------------- */

/*
 * The value of facet (desc) of node derived from its spelling, the steps 3 and
 * 4 of the getter (design, Tree: Facets):
 *   - a CONTENT facet: always derived from the content of the leaf (the empty
 *     string without content);
 *   - another STRING facet spelled by tokens (amark_tree_layout_spells or
 *     amark_tree_content_spells): derived from its tokens;
 *   - otherwise *derived is false and nothing else is written.
 * A derived value is cached in the facet (cache, cache_length) and *text and
 * *length receive the cache, valid until the next change of the node.
 * AMARK_ERROR_OUT_OF_MEMORY leaves no cache. The cache is the only part of the
 * node it writes.
 */
amark_error_t amark_tree_derive(const amark_node_t *node, amark_facet_t facet,
                                const amark_facet_desc_t *desc, bool *derived, const char **text,
                                size_t *length);

/* Whether a layout token (amark_tree_layout_spells) or a content token
 * (amark_tree_content_spells) of node spells facet (false for
 * AMARK_FACET_NONE). A STRING facet is never spelled by both: the records
 * refuse it. */
bool amark_tree_layout_spells(const amark_node_t *node, amark_facet_t facet);
bool amark_tree_content_spells(const amark_node_t *node, amark_facet_t facet);

/* Whether a new token of facet on node would spell a STRING facet with both
 * kinds of tokens: a layout token (layout true) where a content token spells
 * the facet, or a content token (layout false) where a layout token does. The
 * records refuse such a token. */
bool amark_tree_mixes_spelling(const amark_node_t *node, amark_facet_t facet, bool layout);

/* For the internal check: whether the cache of facet of node, if any, is the
 * value derived now (a cache of a facet that derives nothing is not).
 * Allocates nothing. */
bool amark_tree_derived_cache_ok(const amark_node_t *node, amark_facet_t facet);

/* Drops the cached derived value of facet of node, or of every facet for
 * AMARK_FACET_NONE: called when the tokens of the node change. */
void amark_tree_drop_derived(amark_node_t *node, amark_facet_t facet);

/* Drops the cached values of the CONTENT facets of leaf: called when its
 * content changes. */
void amark_tree_drop_content_derived(amark_node_t *leaf);

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
    /* The steps of the layout records (Layout): one per record appended, plus
     * the records moved by each growth of an array. */
    size_t record_steps;
    /* The content objects copied because they were shared when a leaf changed
     * its content (Content, copy on write). */
    size_t content_copies;
    /* The steps of the definition index (DefinitionIndex): one per dirty
     * definition processed, per ancestor visited to see whether a candidate is
     * attached, and per node visited to order several candidates. */
    size_t index_steps;
    /* The steps of the verbatim judgment (Verbatim): one per node visited by
     * the walk, per line of a block judged, per record, child or token
     * examined, and per ancestor visited to find the leaf of an inline node. */
    size_t verbatim_steps;
    /* The steps of the derivation of the tightness of the lists (Verbatim):
     * one per derivation, plus one per child and per blank line examined. */
    size_t tight_steps;
    /* The steps of the lossless check (LosslessCheck): one per node visited by
     * each walk, per record collected and placed, per line and per column of
     * the source checked, and per byte and token of each content covered. */
    size_t lossless_steps;
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
    /* The definition index (DefinitionIndex). */
    amark_tree_def_index_t def_index;
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
    /* A node whose cache is valid has a child whose cache is not, or the
     * verbatim results of a valid node are inconsistent: unknown flags, flags
     * that do not fit the node (FULL_LINES, STARTS or ENDS without VERBATIM or
     * on an inline node, STARTS on a container or missing on another block,
     * ENDS with FULL_LINES or missing without it), remembered columns without
     * the flags that need them or missing when needed, a verbatim block without
     * a line range or (a container) without one margin per line, a
     * remembered column beyond the width of its line, and a current
     * remembered leaf of an inline node that is not its leaf (or one on a
     * block). */
    AMARK_TREE_VIOLATION_CACHE,
    /* The sources and the line table: the lines of a source are not the
     * division of its bytes in order (offsets, ranges, line endings, widths,
     * numbers, identifiers; for given lines, their sequence), the index of the
     * tabs of a line does not list its tabs, or the default line ending is not
     * the one of the first line that has one. */
    AMARK_TREE_VIOLATION_LINES,
    /* The storage of the facets of a node: the facet array does not match the
     * facet count of the type, a state is unknown, a number is outside the
     * range of its facet or set without a value, a string is missing, not
     * terminated at its length, holds NUL or invalid UTF-8, or is not
     * normalized for a LINES facet, a value is held by a facet of another
     * kind or by a CONTENT facet, a cache length is set without a cache, a
     * cache differs from the value derived now (or a facet that derives
     * nothing has one), a STRING facet is spelled by both layout and
     * content tokens, or a token spells a facet whose spelling is dropped. */
    AMARK_TREE_VIOLATION_FACETS,
    /* The layout of a node: a line range outside the line table, records on a
     * node without a range or outside it, margins on a type that is not a
     * container, blank lines on a type that cannot have block children, records
     * out of order (margins and blank lines not one per line in the order of the
     * lines, tokens not in the order of the lines and columns or overlapping), a
     * column beyond the width of its line or a reversed range, an unknown state,
     * flag, kind or facet, a lazy margin with columns, a blank line that does not
     * end at the end of its line, an empty token, a record array whose count
     * exceeds its capacity, or a dropped mark of a facet the type does not
     * have. */
    AMARK_TREE_VIOLATION_LAYOUT,
    /* The content of a leaf: content on a node that is not a block with
     * content, an unknown state, an array whose count exceeds its capacity, a
     * length of 4 GiB or more, bytes not terminated, pieces on free content or
     * out of order (an empty piece is alone on its line) or outside the line
     * range or the width of their lines, bytes
     * that are not the pieces joined as laid content (or free content that holds
     * a CR), a wrong line count, or an owner or base count that differs from
     * the nodes that refer to the object; content tokens on a node that is
     * neither an inline node nor a block with content, tokens without a base
     * (or a base without tokens), a token array whose count exceeds its
     * capacity, and tokens that are empty, beyond the bytes of their base, out
     * of order or overlapping, of an unknown kind, or of a facet the type does
     * not have. */
    AMARK_TREE_VIOLATION_CONTENT,
    /* The definition index: a dirty list that is broken or asymmetric, holds a
     * node that is not a link_definition of the document or not marked, or
     * differs from the marked nodes; a group in the wrong bucket, with a wrong
     * hash, an empty key, a broken member list or a wrong count, or a group
     * count that differs from the groups; a member that is not a
     * link_definition or points to another group; a node outside the table
     * that points to a group; a link_definition that is not dirty whose label
     * does not normalize to the key of its group (or to nothing when it is in
     * none); a cached result that is not a member of its group, or decided at
     * a future generation. */
    AMARK_TREE_VIOLATION_INDEX
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
 * (relations, containment, link in link, cache, facets, layout). A component
 * that adds records or caches adds its invariant as a node check or a document
 * check in tree_check.c.
 */
amark_tree_violation_t amark_tree_check(amark_document_t *document,
                                        amark_tree_check_report_t *report);

/* The name of a violation kind, for the messages of the tests ("ring",
 * "relation", ...; "none" for AMARK_TREE_VIOLATION_NONE). */
const char *amark_tree_violation_name(amark_tree_violation_t kind);

#endif /* AMARK_TREE_TREE_H */
