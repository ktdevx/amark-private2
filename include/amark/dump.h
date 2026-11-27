/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The S-expression dump of a subtree, the dump writer and the dump flags.
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

/* The writer an extension type's dump function writes its attributes to
 * (opaque). */
typedef struct amark_dump_writer amark_dump_writer_t;

/* ---- The dump (13.4-13.7) ------------------------------------------------------ */

/* The styles: :style (:name value ...), without the unset ones. */
#define AMARK_DUMP_STYLE 0x01u
/* The layout: the line range, the margins, the blank lines, the own spelling
 * of the leaf (layout tokens) and the content. */
#define AMARK_DUMP_LAYOUT 0x02u
/* The spelling: the content tokens and the dropped-spelling marks. */
#define AMARK_DUMP_SPELLING 0x04u
/* The source positions (amark_node_source_range). */
#define AMARK_DUMP_POSITIONS 0x08u
/* The line table, on the document node. */
#define AMARK_DUMP_LINES 0x10u

/*
 * Appends to out the dump of node and its subtree, the same text for the same
 * tree (13.4):
 *
 *   node = "(" type-name {" " attr} [" " literal] {LF indent node} ")"
 *
 * The children of a node follow it, each on its own line indented by two spaces
 * per level below node, up to 64 levels (128 spaces: deeper nodes get the same
 * indentation, so the dump of a deep tree stays linear in its nodes), and the
 * dump ends with LF. The attributes, in this
 * order:
 *   - :name value for each value facet of the type, in the order of the facets,
 *     as amark_node_get reads it (a default value included; an UNRESOLVED
 *     destination or title of a reference link or image is left out), except a
 *     STRING value facet named "literal", which is the literal, written last;
 *   - the attributes the dump function of an extension type writes to the dump
 *     writer (12.13; the functions below);
 *   - with AMARK_DUMP_STYLE, :style (:name value ...) of the style facets that
 *     have a value, in the order of the facets, when there is one;
 *   - with AMARK_DUMP_LAYOUT, each that the node has:
 *       :lines (first count)
 *       :margins ((line start end present|lazy [first] [:role n]) ...)
 *       :blanks ((line start end) ...)
 *       :tokens ((kind line start end [:facet name] [:role n]) ...)
 *       :content laid ((line start end) ...)   or   :content free "..."
 *     (empty free content with one line, the value "\n" of a CONTENT facet, is
 *     left out: the literal "\n" stands for it)
 *   - with AMARK_DUMP_SPELLING, each that the node has:
 *       :spelling ((kind start end [:facet name] [:role n]) ...)
 *       :dropped (name ...)   "structure" for the structural spelling, then the
 *                             names of the facets, in the order of the facets
 *   - with AMARK_DUMP_POSITIONS, :pos (sl sc el ec) for a node that has a
 *     position (the start line and column and the end line and column of
 *     amark_node_source_range);
 *   - with AMARK_DUMP_LINES, on the document node, :line_table ((source number
 *     offset length eol width) ...) of every line of the document.
 * Lines are the identifiers of the line table, columns its columns and the
 * positions of the content tokens the bytes of their content. A value is a
 * string for a STRING facet, a decimal integer for an INT facet, true or false
 * for a BOOL facet, and the name of the value for an ENUM facet: a symbol when
 * the name matches [a-z][a-z0-9_]*, else a string. The kinds of the tokens are
 * text, marker, space, delimiter, escape, char_ref, replaced, line_break and
 * tab_remainder; the line endings none, lf, crlf and cr.
 *
 * A string is written in double quotes with the escapes \" \\ \n \r and \t; the
 * other control characters (U+0000-U+001F, U+007F and U+0080-U+009F), as their
 * bytes, and each byte of invalid UTF-8 are written as \xHH, with two
 * upper-case hexadecimal digits (13.5).
 *
 * The test support builds trees from this format (13.8): a dump with
 * AMARK_DUMP_LAYOUT and AMARK_DUMP_SPELLING, built into a document with the same
 * sources, dumps the same.
 *
 * The subtree is walked without recursion. Reading a value and the positions
 * can allocate (a derived value, the update of the definition index, the index
 * of the tabs of a line). AMARK_ERROR_INVALID_ARGUMENT for a NULL node or out
 * and unknown flags; AMARK_ERROR_OUT_OF_MEMORY; the error of the dump function
 * of an extension type. On failure out has its length before the call.
 */
AMARK_API amark_error_t amark_node_dump(const amark_node_t *node, uint32_t flags,
                                        amark_buffer_t *out);

/* ---- The dump writer (12.13) ------------------------------------------------- */

/*
 * Write an attribute " :name value" of the extension data of a node to the
 * dump, after the value facets of the node: name matches [a-z][a-z0-9_]*; the
 * value is the length bytes at text as a string (text may be NULL when length
 * is 0), number as a decimal integer, value as true or false, or symbol, which
 * matches [a-z][a-z0-9_]*, as it is. AMARK_ERROR_INVALID_ARGUMENT for a NULL
 * writer, an invalid name or symbol and NULL text with a length;
 * AMARK_ERROR_OUT_OF_MEMORY. The writer is valid only during the call of the
 * dump function it is given to.
 */
AMARK_API amark_error_t amark_dump_write_string(amark_dump_writer_t *writer, const char *name,
                                                const char *text, size_t length);
AMARK_API amark_error_t amark_dump_write_int(amark_dump_writer_t *writer, const char *name,
                                             int64_t number);
AMARK_API amark_error_t amark_dump_write_bool(amark_dump_writer_t *writer, const char *name,
                                              bool value);
AMARK_API amark_error_t amark_dump_write_symbol(amark_dump_writer_t *writer, const char *name,
                                                const char *symbol);

AMARK_END_DECLS

#endif /* AMARK_DUMP_H */
