/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The deterministic S-expression dump of a subtree, for tests and debugging, and
 * the writer the dump function of an extension type uses for its attributes.
 *
 * Format (lines end with LF, the dump ends with one LF, each level is indented by
 * two spaces):
 *   node   = "(" type-name { " " attr } [ " " string ] { LF indent node } ")"
 *   attr   = ":" attr-name " " value
 *   value  = string | integer | symbol | "(" value { " " value } ")"
 *   string = DQUOTE { char } DQUOTE
 * The attributes come in this order:
 *   - the built-in attributes: heading :level; list :type (bullet or ordered),
 *     :start (only for an ordered list), :tight (true or false); code_block :info;
 *     link and image :destination :title; link_definition :label :destination
 *     :title;
 *   - those the dump function of an extension type writes, in its order;
 *   - :edits (AMARK_DUMP_EDITS, only when the node has records): the list of the
 *     bits set, in the order (content children descendants relocated created),
 *     then each unknown bit as bitN, N its number from 0 ("bit5" for 0x20);
 *   - :span (AMARK_DUMP_SPANS, only when amark_node_source_span finds one):
 *     (start-line start-column end-line end-column);
 *   - :tokens (AMARK_DUMP_TOKENS, only when the node has tokens): the list of its
 *     tokens in their order, each (kind offset length {flag} [:role n] [:source s]
 *     [:tab o w]) with kind from amark_token_kind_name, the flags prefix
 *     (AMARK_TOKEN_FLAG_CONTAINER_PREFIX) and partial_tab, :role and :source only
 *     when not 0, and :tab with tab_column_offset and tab_columns only with
 *     partial_tab;
 * then the literal string, for the types that have a literal mode (also when it is
 * empty). Integers are in decimal, with a '-' when negative.
 *
 * Strings: '"' is written \", '\' is \\, LF \n, CR \r, a tab \t, and the other
 * bytes 0x00-0x1F and 0x7F and each byte of an invalid UTF-8 sequence \xHH (two
 * lowercase hexadecimal digits); valid UTF-8 (U+FFFD included) is written as it is.
 *
 * Attribute names and symbols match [a-z][a-z0-9_]*.
 */
#ifndef AMARK_DUMP_H
#define AMARK_DUMP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <amark/export.h>
#include <amark/error.h>
#include <amark/buffer.h>
#include <amark/node.h>

AMARK_BEGIN_DECLS

/* The flags of amark_node_dump: also write the tokens, the edit records, the
 * source spans. */
#define AMARK_DUMP_TOKENS 0x01u
#define AMARK_DUMP_EDITS  0x02u
#define AMARK_DUMP_SPANS  0x04u

/* The writer passed to the dump function of an extension type (opaque); valid only
 * during that call. */
typedef struct amark_dump_writer amark_dump_writer_t;

/*
 * Appends the dump of the subtree of node to out, walking it without recursion.
 * Unknown bits of flags are ignored. A NULL node or out is
 * AMARK_ERROR_INVALID_ARGUMENT; the failure of a dump function of an extension type
 * (or of its writer) is returned, as is AMARK_ERROR_OUT_OF_MEMORY or
 * AMARK_ERROR_OVERFLOW from out. On failure out keeps its previous length.
 */
AMARK_API amark_error_t amark_node_dump(const amark_node_t *node, uint32_t flags,
                                        amark_buffer_t *out);

/*
 * Write the attribute name (matching [a-z][a-z0-9_]*) with a string (length bytes
 * of any content, escaped; NULL only with length 0, which is ""), an integer, a
 * boolean (true or false) or a symbol (matching [a-z][a-z0-9_]*) value. A name,
 * symbol or string that breaks these rules is AMARK_ERROR_INVALID_ARGUMENT, and
 * so is a NULL writer. The writer keeps its first failure: every later write
 * returns it without writing, and amark_node_dump fails with it even when the
 * dump function returns AMARK_ERROR_NONE.
 */
AMARK_API amark_error_t amark_dump_attr_string(amark_dump_writer_t *writer, const char *name,
                                               const char *value, size_t length);
AMARK_API amark_error_t amark_dump_attr_int(amark_dump_writer_t *writer, const char *name,
                                            long long value);
AMARK_API amark_error_t amark_dump_attr_bool(amark_dump_writer_t *writer, const char *name,
                                             bool value);
AMARK_API amark_error_t amark_dump_attr_symbol(amark_dump_writer_t *writer, const char *name,
                                               const char *symbol);

AMARK_END_DECLS

#endif /* AMARK_DUMP_H */
