/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The extension registry: the facet descriptors, the node type descriptors, the
 * syntax extensions, the slots, the serial numbers of the slot kinds and the
 * syntax sets.
 *
 * A syntax set is an ordered list of syntax extensions. It starts with the
 * built-in extension "amark.core", which cannot be removed. While it is not
 * frozen, extensions are appended, inserted, removed and replaced; freezing it
 * checks the node type names and assigns the node type identifiers, and from then
 * on it is immutable: it can be shared by several documents and threads (14.3),
 * and only the queries of a frozen set find node types and slots.
 *
 * A syntax set borrows the descriptors: the extensions, their node types, their
 * facets and their slots must outlive every syntax set that holds them (usually
 * they are static constants).
 */
#ifndef AMARK_EXTENSION_H
#define AMARK_EXTENSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <amark/export.h>
#include <amark/error.h>
#include <amark/node.h>
#include <amark/value.h>
#include <amark/dump.h>

AMARK_BEGIN_DECLS

/* ---- Facets ---------------------------------------------------------------- */

/* Whether a facet is part of the meaning of the node (a value) or of its
 * notation (a style). */
typedef enum amark_facet_role
{
    AMARK_FACET_VALUE = 1,
    AMARK_FACET_STYLE = 2
} amark_facet_role_t;

/* The kind of the values of a facet. */
typedef enum amark_facet_kind
{
    AMARK_FACET_STRING = 1,
    AMARK_FACET_INT = 2,
    AMARK_FACET_ENUM = 3,
    AMARK_FACET_BOOL = 4
} amark_facet_kind_t;

/* The flags of a facet. All but AMARK_FACET_OVERRIDABLE apply only to STRING
 * facets. */
/* Derivation: escapes and character references are not decoded. */
#define AMARK_FACET_RAW 0x0001u
/* Derivation: a virtual line break becomes a space. */
#define AMARK_FACET_FOLD_LINE_BREAKS 0x0002u
/* Several lines: a set value is normalized to LF and, when not empty, ends with
 * LF. */
#define AMARK_FACET_LINES 0x0004u
/* The value cannot contain a line break. */
#define AMARK_FACET_NO_LINE_BREAK 0x0008u
/* The value cannot contain a blank line. */
#define AMARK_FACET_NO_BLANK_LINE 0x0010u
/* The value cannot be set to the empty string. */
#define AMARK_FACET_NONEMPTY 0x0020u
/* A derived value facet: setting it overrides the derived value, unsetting it
 * returns to the derivation. Not allowed on a style facet. */
#define AMARK_FACET_OVERRIDABLE 0x0040u
/* The value is the content of the leaf (the literal of code_block and
 * html_block). Only on a node type with AMARK_NODE_TYPE_CONTENT. */
#define AMARK_FACET_CONTENT 0x0080u

/*
 * The descriptor of a facet. Checked when its extension is added to a syntax set:
 *   - name matches [a-z][a-z0-9_]* and is unique within its node type;
 *   - role and kind are values of their enumerations, flags holds only the flags
 *     above, and a STYLE facet has no AMARK_FACET_OVERRIDABLE;
 *   - an INT facet has int_min <= int_max; an ENUM facet has 1 or more
 *     enum_names, none of them NULL;
 *   - the default value of a VALUE facet (default_number) is within int_min to
 *     int_max for INT, 1 to enum_count for ENUM, and 0 or 1 for BOOL.
 */
typedef struct amark_facet_desc
{
    size_t struct_size;
    /* [a-z][a-z0-9_]*, unique within the node type. */
    const char *name;
    amark_facet_role_t role;
    amark_facet_kind_t kind;
    /* AMARK_FACET_* flags. */
    uint32_t flags;
    /* The range of an INT facet. */
    int64_t int_min, int_max;
    /* The names of an ENUM facet; its values are 1 to enum_count. */
    const char *const *enum_names;
    size_t enum_count;
    /* The default value of an INT, ENUM or BOOL value facet. */
    int64_t default_number;
    /* The default value of a STRING value facet (NULL is the empty string). */
    const char *default_text;
    /* Optional: checks a new value; returns AMARK_ERROR_NONE to accept it. */
    amark_error_t (*validate)(const amark_node_t *node, amark_facet_t facet,
                              const amark_value_t *value, void *user_data);
    /* Optional: called when the value is set, before it is committed. */
    amark_error_t (*on_change)(amark_node_t *node, amark_facet_t facet,
                               const amark_value_t *new_value, void *user_data);
    void *user_data;
} amark_facet_desc_t;

/* ---- Node types ------------------------------------------------------------ */

/* The content classes: the bits of provides and accepts of a node type. */
#define AMARK_CONTENT_BLOCK 0x01u
#define AMARK_CONTENT_INLINE 0x02u
#define AMARK_CONTENT_LIST_ITEM 0x04u

/* The flags of a node type (block types only). */
/* The leaf has content (paragraph, heading, code_block, html_block, ...). */
#define AMARK_NODE_TYPE_CONTENT 0x01u
/* The child blocks can share a line in columns that do not overlap (7.8). */
#define AMARK_NODE_TYPE_SHARE_LINES 0x02u
/* A container with a margin on each line it covers (block_quote, item, the
 * containers of extensions). */
#define AMARK_NODE_TYPE_MARGINS 0x04u

/* The largest data_size of a node type. */
#define AMARK_NODE_TYPE_MAX_DATA_SIZE 65536u

/*
 * The descriptor of a node type. The built-in types use the same descriptor as
 * the types of extensions. Checked when its extension is added to a syntax set:
 *   - name matches [a-z0-9][a-z0-9_.-]*; level is AMARK_NODE_LEVEL_BLOCK or
 *     AMARK_NODE_LEVEL_INLINE;
 *   - provides is exactly one content class, accepts holds only content classes;
 *   - flags holds only AMARK_NODE_TYPE_* flags, and an inline type has none;
 *   - facet_count is at most AMARK_FACET_MAX, and each facet is valid (see
 *     amark_facet_desc_t); data_size is at most AMARK_NODE_TYPE_MAX_DATA_SIZE.
 * Node type names must be unique within a syntax set; this is checked when the
 * set is frozen.
 */
struct amark_node_type_desc
{
    size_t struct_size;
    /* [a-z0-9][a-z0-9_.-]* */
    const char *name;
    amark_node_level_t level;
    /* The content class this type belongs to (one AMARK_CONTENT_* bit). */
    uint32_t provides;
    /* The content classes this type accepts as children. */
    uint32_t accepts;
    /* AMARK_NODE_TYPE_* flags. */
    uint32_t flags;
    /* facets[i] is facet number i + 1. */
    const amark_facet_desc_t *facets;
    /* 0 to AMARK_FACET_MAX. */
    size_t facet_count;
    /* The size of the extension data of each node, 0 to
     * AMARK_NODE_TYPE_MAX_DATA_SIZE. */
    size_t data_size;
    /* The rest is optional (NULL allowed). */
    /* Narrows the containment rule for the children of this type: called only
     * for a child type whose content class this type accepts, it returns false
     * to refuse it. self is the would-be parent (NULL when only the types are
     * compared). */
    bool (*can_contain)(const amark_node_t *self, amark_node_type_t child);
    /* Initializes the zero-filled extension data of a new node. */
    amark_error_t (*init)(amark_node_t *node, void *data);
    /* Frees what the extension data owns. */
    void (*deinit)(amark_node_t *node, void *data);
    /* Copies the extension data of src into the zero-filled dst_data. */
    amark_error_t (*copy)(amark_node_t *dst, void *dst_data, const amark_node_t *src,
                          const void *src_data);
    /* Compares the extension data. */
    bool (*equal)(const amark_node_t *a, const void *a_data, const amark_node_t *b,
                  const void *b_data);
    /* Writes the attributes of the extension data to the dump. */
    amark_error_t (*dump)(const amark_node_t *node, const void *data, amark_dump_writer_t *writer);
    void *user_data;
};

/* Returns the number of the facet of desc named name, or AMARK_FACET_NONE when
 * desc has no such facet or an argument is NULL. */
AMARK_API amark_facet_t amark_node_type_find_facet(const amark_node_type_desc_t *desc,
                                                   const char *name);

/* Returns the descriptor of facet number facet of desc, or NULL when facet is
 * not 1 to desc->facet_count or desc is NULL. */
AMARK_API const amark_facet_desc_t *amark_node_type_facet(const amark_node_type_desc_t *desc,
                                                          amark_facet_t facet);

/* ---- Slots ----------------------------------------------------------------- */

/*
 * The base of a slot: the structure of each slot kind has this type as its first
 * member. The registry does not know the structures of the kinds; it only
 * enumerates the slots of a kind.
 */
typedef struct amark_slot
{
    /* The size of the whole structure of the slot kind that embeds this base, at
     * least sizeof(amark_slot_t). */
    size_t struct_size;
    /* The slot kind number (0 is invalid). */
    uint32_t kind;
} amark_slot_t;

/* The ranges of the slot kind numbers: AMARK_SLOT_KIND_AMARK_FIRST to
 * AMARK_SLOT_KIND_AMARK_LAST for the Amark specs, AMARK_SLOT_KIND_USER_FIRST and
 * above for the application's own slots. */
#define AMARK_SLOT_KIND_AMARK_FIRST 1u
#define AMARK_SLOT_KIND_AMARK_LAST 255u
#define AMARK_SLOT_KIND_USER_FIRST 256u

/*
 * The slot kinds of the Amark specs, as one list of serial numbers that do not
 * overlap (12.10). A new kind takes the next number at the end. The meaning of
 * each kind and the structure of its slots are defined by the spec named in the
 * comment. The registry accepts any kind other than 0.
 */
/* block-parser */
#define AMARK_SLOT_BLOCK_SYNTAX 1u
#define AMARK_SLOT_PARAGRAPH_CONSUMER 2u
#define AMARK_SLOT_POSTPROCESS 3u
#define AMARK_SLOT_PARSE_STATE 4u
/* inline-parser */
#define AMARK_SLOT_INLINE_SYNTAX 5u
#define AMARK_SLOT_INLINE_DELIMITER 6u
#define AMARK_SLOT_INLINE_BRACKET 7u
#define AMARK_SLOT_INLINE_TEXT_SCANNER 8u
#define AMARK_SLOT_INLINE_NODE_TRAITS 9u
#define AMARK_SLOT_INLINE_SPECIAL 10u
/* html-renderer */
#define AMARK_SLOT_HTML_NODE 11u
#define AMARK_SLOT_HTML_HOOKS 12u
/* markdown-renderer */
#define AMARK_SLOT_MARKDOWN_NODE 13u
#define AMARK_SLOT_MARKDOWN_HOOKS 14u

/* ---- Syntax extensions ----------------------------------------------------- */

/*
 * A syntax extension: the node types and the slots it adds to a syntax set.
 * Checked when it is added to a syntax set: struct_size is at least
 * sizeof(amark_extension_t), name matches [a-z0-9][a-z0-9_.-]*, the arrays are
 * not NULL when their counts are not 0, no element is NULL, every node type is
 * valid, and every slot has a struct_size of at least sizeof(amark_slot_t) and a
 * kind other than 0.
 */
typedef struct amark_extension
{
    size_t struct_size;
    /* [a-z0-9][a-z0-9_.-]*, unique within a syntax set. */
    const char *name;
    const amark_node_type_desc_t *const *node_types;
    size_t node_type_count;
    const amark_slot_t *const *slots;
    size_t slot_count;
    void *user_data;
} amark_extension_t;

/* ---- Syntax sets ----------------------------------------------------------- */

/* Creates in *out_set a syntax set that holds only the built-in extension
 * "amark.core", not frozen. */
AMARK_API amark_error_t amark_syntax_set_create(amark_syntax_set_t **out_set);

/* The static built-in syntax set: frozen, with only the built-in extension
 * "amark.core". It is shared and immutable; amark_syntax_set_destroy does nothing
 * on it, and a document cannot take its ownership. */
AMARK_API const amark_syntax_set_t *amark_syntax_set_core(void);

/* Creates in *out_set a syntax set with the same extensions as set, in the same
 * order, not frozen (12.12). */
AMARK_API amark_error_t amark_syntax_set_clone(const amark_syntax_set_t *set,
                                               amark_syntax_set_t **out_set);

/* Destroys set. Does nothing for NULL and for the static built-in set. */
AMARK_API void amark_syntax_set_destroy(amark_syntax_set_t *set);

/*
 * Change the list of extensions of a set that is not frozen
 * (AMARK_ERROR_INVALID_STATE otherwise): append extension at the end, insert it
 * before the extension named anchor_name, remove the extension named name, or
 * replace the extension named name with extension at the same place. An invalid
 * descriptor, an extension name already in the set (other than the replaced
 * one), an unknown name, and a change that would remove the built-in extension or
 * move it from the front are AMARK_ERROR_INVALID_ARGUMENT. On failure set is
 * unchanged.
 */
AMARK_API amark_error_t amark_syntax_set_append(amark_syntax_set_t *set,
                                                const amark_extension_t *extension);
AMARK_API amark_error_t amark_syntax_set_insert_before(amark_syntax_set_t *set,
                                                       const char *anchor_name,
                                                       const amark_extension_t *extension);
AMARK_API amark_error_t amark_syntax_set_remove(amark_syntax_set_t *set, const char *name);
AMARK_API amark_error_t amark_syntax_set_replace(amark_syntax_set_t *set, const char *name,
                                                 const amark_extension_t *extension);

/*
 * Freezes set: checks that no two node types have the same name
 * (AMARK_ERROR_INVALID_ARGUMENT, and set stays not frozen), then assigns the
 * identifiers. The types of the built-in extension get 1 and up in their order;
 * the types of the other extensions get AMARK_NODE_DYNAMIC_FIRST and up in the
 * order of the set, so sets with the same composition assign the same
 * identifiers. Freezing a frozen set does nothing.
 */
AMARK_API amark_error_t amark_syntax_set_freeze(amark_syntax_set_t *set);

/* Whether set is frozen (false for NULL). */
AMARK_API bool amark_syntax_set_is_frozen(const amark_syntax_set_t *set);

/* The extensions of set, in order (the built-in extension first). The count is 0
 * and the lookups return NULL for a NULL set, an index out of range or an
 * unknown name. */
AMARK_API size_t amark_syntax_set_extension_count(const amark_syntax_set_t *set);
AMARK_API const amark_extension_t *amark_syntax_set_extension_at(const amark_syntax_set_t *set,
                                                                 size_t index);
AMARK_API const amark_extension_t *amark_syntax_set_find_extension(const amark_syntax_set_t *set,
                                                                   const char *name);

/*
 * The queries below need a frozen set: on a set that is not frozen they find
 * nothing.
 *   - find_node_type: the identifier of the node type named name. Returns false
 *     and sets *out_type to AMARK_NODE_NONE when there is none.
 *   - node_type_of: the identifier of the node type with the descriptor desc
 *     (the same pointer). Same failure as find_node_type.
 *   - node_type_desc: the descriptor of type, or NULL.
 *   - slot_count and slot_at: the slots of kind, in the order of the set and,
 *     within an extension, in the order of its slots. slot_at returns NULL for an
 *     index out of range, and sets *out_extension (which may be NULL) to the
 *     extension of the slot, or NULL.
 */
AMARK_API bool amark_syntax_set_find_node_type(const amark_syntax_set_t *set, const char *name,
                                               amark_node_type_t *out_type);
AMARK_API bool amark_syntax_set_node_type_of(const amark_syntax_set_t *set,
                                             const amark_node_type_desc_t *desc,
                                             amark_node_type_t *out_type);
AMARK_API const amark_node_type_desc_t *amark_syntax_set_node_type_desc(
    const amark_syntax_set_t *set, amark_node_type_t type);
AMARK_API size_t amark_syntax_set_slot_count(const amark_syntax_set_t *set, uint32_t kind);
AMARK_API const amark_slot_t *amark_syntax_set_slot_at(const amark_syntax_set_t *set,
                                                       uint32_t kind, size_t index,
                                                       const amark_extension_t **out_extension);

AMARK_END_DECLS

#endif /* AMARK_EXTENSION_H */
