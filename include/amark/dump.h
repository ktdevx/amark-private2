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
 * The attributes come in this order: the built-in attributes, those the dump
 * function of an extension type writes, :edits (AMARK_DUMP_EDITS), :span
 * (AMARK_DUMP_SPANS), :tokens (AMARK_DUMP_TOKENS), then the literal string.
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

/* Appends the dump of the subtree of node to out. On failure out keeps its
 * previous length. */
AMARK_API amark_error_t amark_node_dump(const amark_node_t *node, uint32_t flags,
                                        amark_buffer_t *out);

/*
 * Write the attribute name (matching [a-z][a-z0-9_]*, otherwise
 * AMARK_ERROR_INVALID_ARGUMENT) with a string, an integer, a boolean or a symbol
 * value.
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
