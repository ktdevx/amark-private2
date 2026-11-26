/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The extension registry: node type descriptors, syntax extension descriptors,
 * the base of slots, and syntax sets.
 *
 * A syntax set is an ordered list of syntax extensions. It starts with amark.core,
 * the extension that holds the built-in node types (amark/node.h, AMARK_NODE_*),
 * which cannot be removed, replaced, or preceded. Freezing the set checks the
 * type names, gives the types of the extensions the identifiers 256, 257, ... in
 * the order of the set, and builds the indexes; a frozen set cannot change
 * (AMARK_ERROR_INVALID_STATE) and can be used from several threads at once. The
 * same list of extensions always gives the same identifiers.
 *
 * Lifetimes: the descriptors and the extensions must outlive the syntax sets that
 * refer to them (they are usually static), and a syntax set must outlive every
 * document that uses it.
 */
#ifndef AMARK_EXTENSION_H
#define AMARK_EXTENSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <amark/export.h>
#include <amark/error.h>
#include <amark/node.h>
#include <amark/dump.h>

AMARK_BEGIN_DECLS

/* The content classes, the bits of provides and accepts of a node type. */
#define AMARK_CONTENT_BLOCK      0x01u
#define AMARK_CONTENT_INLINE     0x02u
#define AMARK_CONTENT_LIST_ITEM  0x04u

/* The level of a node type. 0 is invalid, so that a descriptor that forgets it is
 * rejected when it is registered. */
typedef enum amark_node_level
{
    AMARK_NODE_LEVEL_BLOCK = 1,
    AMARK_NODE_LEVEL_INLINE = 2
} amark_node_level_t;

/* How a node type holds a literal (amark_node_set_literal). */
typedef enum amark_literal_mode
{
    /* No literal. */
    AMARK_LITERAL_NONE = 0,
    /* No line ending (text, code). */
    AMARK_LITERAL_LINE = 1,
    /* Line endings allowed and normalized to LF, no blank line (html_inline). */
    AMARK_LITERAL_SPAN = 2,
    /* Line endings normalized to LF, ending with LF when not empty (code_block,
     * html_block). */
    AMARK_LITERAL_LINES = 3
} amark_literal_mode_t;

/* The largest data_size of a node type. */
#define AMARK_NODE_TYPE_MAX_DATA_SIZE 65536u

/*
 * The descriptor of a node type. The built-in types use the same descriptor as
 * the types of extensions. The typedef amark_node_type_desc_t is in amark/node.h.
 */
struct amark_node_type_desc
{
    size_t struct_size;
    /* The name: "paragraph" for a built-in type; the name of an extension type
     * contains a "." ("gfm.table"). */
    const char *name;
    /* AMARK_NODE_LEVEL_BLOCK or AMARK_NODE_LEVEL_INLINE. */
    amark_node_level_t level;
    /* The content classes (AMARK_CONTENT_*) this type belongs to. */
    uint32_t provides;
    /* The content classes this type accepts as children. A type that accepts none
     * and has no can_contain is a leaf. */
    uint32_t accepts;
    amark_literal_mode_t literal_mode;
    /* The size of the type data of each node, zero-filled at creation; may be 0. */
    size_t data_size;
    /* The rest is optional (NULL allowed). */
    /* Overrides the containment rule for the children of this type. */
    bool (*can_contain)(const struct amark_node_type_desc *self,
                        const struct amark_node_type_desc *child);
    /* Checks a literal after the rules of literal_mode. */
    amark_error_t (*validate_literal)(const amark_node_t *node, const char *text, size_t length);
    /* Initializes the zero-filled type data of a new node. */
    amark_error_t (*init)(amark_node_t *node, void *data);
    /* Frees what the type data owns. */
    void (*deinit)(amark_node_t *node, void *data);
    /* Copies the type data (otherwise its bytes are copied). */
    amark_error_t (*copy)(amark_node_t *dst, void *dst_data, const amark_node_t *src,
                          const void *src_data);
    /* Compares the type data (otherwise its bytes are compared). */
    bool (*equal)(const amark_node_t *a, const void *a_data, const amark_node_t *b,
                  const void *b_data);
    /* Writes the attributes of the type data to the dump. */
    amark_error_t (*dump)(const amark_node_t *node, const void *data, amark_dump_writer_t *writer);
    void *user_data;
};

/*
 * The base of a slot: the structure of each slot kind has this type as its first
 * member, base.
 */
typedef struct amark_slot
{
    /* The size of the whole structure of the slot kind that embeds this base (not
     * sizeof(amark_slot_t)); a user reads only the fields within that size. */
    size_t struct_size;
    /* The slot kind number (0 is invalid). */
    uint32_t kind;
} amark_slot_t;

/* The slot kind numbers: 1-255 are used by the Amark specs, 256 and above by the
 * application's own slots. */
#define AMARK_SLOT_KIND_AMARK_FIRST   1u
#define AMARK_SLOT_KIND_AMARK_LAST  255u
#define AMARK_SLOT_KIND_USER_FIRST  256u

/*
 * The slot kinds of Amark, numbered in one list (no range per spec): a new kind
 * takes the next number at the end, and the spec that runs it adds its name and
 * value to this header. 15-255 are reserved for future Amark specs (assigning one
 * updates the list of the syntax-tree spec). The registry does not enforce the
 * list; it accepts any kind other than 0.
 *    1 AMARK_SLOT_BLOCK_SYNTAX, 2 AMARK_SLOT_PARAGRAPH_CONSUMER,
 *    3 AMARK_SLOT_POSTPROCESS, 4 AMARK_SLOT_PARSE_STATE (block-parser)
 *    5 AMARK_SLOT_INLINE_SYNTAX, 6 AMARK_SLOT_INLINE_DELIMITER,
 *    7 AMARK_SLOT_INLINE_BRACKET, 8 AMARK_SLOT_INLINE_TEXT_SCANNER,
 *    9 AMARK_SLOT_INLINE_NODE_TRAITS, 10 AMARK_SLOT_INLINE_SPECIAL (inline-parser)
 *   11 AMARK_SLOT_HTML_NODE, 12 AMARK_SLOT_HTML_HOOKS (html-renderer)
 *   13 AMARK_SLOT_MARKDOWN_NODE, 14 AMARK_SLOT_MARKDOWN_HOOKS (markdown-renderer)
 */

/* A syntax extension: the node types and the slots it adds to a syntax set. */
typedef struct amark_extension
{
    size_t struct_size;
    /* The name, matching [a-z0-9][a-z0-9_.-]* ("commonmark.atx_heading"). */
    const char *name;
    const amark_node_type_desc_t *const *node_types;
    size_t node_type_count;
    const amark_slot_t *const *slots;
    size_t slot_count;
    void *user_data;
} amark_extension_t;

/* Creates in *out_set a syntax set that holds only amark.core, not frozen. */
AMARK_API amark_error_t amark_syntax_set_create(amark_syntax_set_t **out_set);

/* Creates in *out_set a copy of set with the same extensions, not frozen. */
AMARK_API amark_error_t amark_syntax_set_clone(const amark_syntax_set_t *set,
                                               amark_syntax_set_t **out_set);

/* Destroys set. Does nothing for NULL and for the set of amark_syntax_set_core(). */
AMARK_API void amark_syntax_set_destroy(amark_syntax_set_t *set);

/* Returns the static, frozen syntax set that holds only amark.core. */
AMARK_API const amark_syntax_set_t *amark_syntax_set_core(void);

/*
 * Change the list of extensions of a set that is not frozen
 * (AMARK_ERROR_INVALID_STATE otherwise): append extension, insert it before the
 * extension named anchor_name, remove the extension named name, or replace it
 * with extension at the same place (the name may change). The descriptors are
 * checked, and an extension name must not appear twice; an unknown name, an
 * invalid descriptor or a change to amark.core is AMARK_ERROR_INVALID_ARGUMENT. On
 * failure set is unchanged.
 */
AMARK_API amark_error_t amark_syntax_set_append(amark_syntax_set_t *set,
                                                const amark_extension_t *extension);
AMARK_API amark_error_t amark_syntax_set_insert_before(amark_syntax_set_t *set,
                                                       const char *anchor_name,
                                                       const amark_extension_t *extension);
AMARK_API amark_error_t amark_syntax_set_remove(amark_syntax_set_t *set, const char *name);
AMARK_API amark_error_t amark_syntax_set_replace(amark_syntax_set_t *set, const char *name,
                                                 const amark_extension_t *extension);

/* Freezes set. Two types with the same name are AMARK_ERROR_INVALID_ARGUMENT, and
 * set stays not frozen. */
AMARK_API amark_error_t amark_syntax_set_freeze(amark_syntax_set_t *set);

/* Whether set is frozen. */
AMARK_API bool amark_syntax_set_is_frozen(const amark_syntax_set_t *set);

/* The extensions of set, in order (amark.core first). */
AMARK_API size_t amark_syntax_set_extension_count(const amark_syntax_set_t *set);
AMARK_API const amark_extension_t *amark_syntax_set_extension_at(const amark_syntax_set_t *set,
                                                                 size_t index);
AMARK_API const amark_extension_t *amark_syntax_set_find_extension(const amark_syntax_set_t *set,
                                                                   const char *name);

/*
 * The queries below need a frozen set; before freezing they find nothing. They
 * find a type by name or by descriptor, the descriptor of a type, and the slots
 * of a kind, in the order of the set and, within an extension, in the order of
 * its slots (*out_extension gets the extension of the slot).
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
