/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * Building trees from S-expressions for the tests (9.7), and the dump check.
 *
 * tree_builder.c is the INTERFACE source of amark_test_tree: it is compiled with
 * the settings of each test that links it, and it uses only the public API, so it
 * goes with either amark or amark_internal. It depends on Naos (the macro below),
 * so fuzz targets must not link it.
 *
 * The input is the format of amark_node_dump (amark/dump.h):
 *     node   = "(" type-name { attr } [ string ] { node } ")"
 *     attr   = ":" attr-name value
 *     value  = string | integer | symbol | "(" value { value } ")"
 *     string = DQUOTE { char | escape } DQUOTE
 * Spaces, tabs, LF and CR separate the items; the indentation of the dump is not
 * required. A type name is [a-z0-9][a-z0-9_.-]*, looked up in the syntax set of
 * the document. An attribute name and a symbol are [a-z][a-z0-9_]*. An integer is
 * an optional '-' and decimal digits. The escapes of a string are \", \\, \n, \r,
 * \t and \xHH (two hexadecimal digits); its other bytes are taken as they are.
 *
 * The attributes:
 *   - The built-in attributes of the built-in types are set by the public setters:
 *     heading :level (integer); list :type (bullet, ordered), :start (integer),
 *     :tight (true, false); code_block :info; link and image :destination,
 *     :title; link_definition :label, :destination, :title (strings). An empty
 *     string is not set (a new node already has it; the setters of a label
 *     reject it), so the dump of a new node is read back.
 *   - :tokens is a list of tokens (kind offset length {flag} [:role n] [:source s]
 *     [:tab o w]), with the kind names of amark_token_kind_name and the flags
 *     prefix and partial_tab, added by amark_node_add_token. The sources must
 *     already be registered in the document.
 *   - :edits and :span are read and ignored.
 *   - Any other attribute is passed to attr_fn: a string value decoded (its
 *     escapes replaced), any other value (integer, symbol, list) as its text in
 *     the input. The value is NUL-terminated (length does not count the NUL;
 *     a string may also hold NUL bytes) and valid only during the call.
 * The literal string is set by amark_node_set_literal (an empty one is not set).
 */
#ifndef AMARK_TEST_TREE_BUILDER_H
#define AMARK_TEST_TREE_BUILDER_H

#include <stddef.h>
#include <string.h>

#include <amark/buffer.h>
#include <amark/document.h>
#include <amark/dump.h>
#include <amark/error.h>
#include <amark/node.h>

#include <naos/naos.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Receives an extension attribute of node (see above); another result than
 * AMARK_ERROR_NONE fails the building with that result. */
typedef amark_error_t (*amark_test_tree_attr_fn_t)(amark_node_t *node, const char *name,
                                                   const char *value, size_t length,
                                                   void *context);

/*
 * Builds the tree of sexpr in document. When the top node is document, its
 * children are appended to the root of document (after the children it already
 * has) and *out_node is the root; otherwise the top node is created detached and
 * is *out_node. Edit tracking is off while building and is restored afterwards,
 * so the new nodes have no edit records.
 *
 * A syntax error, an unknown type or token kind, a value the setters or
 * amark_node_add_token reject and a child the containment rule rejects are
 * AMARK_ERROR_INVALID_ARGUMENT; an allocation failure is AMARK_ERROR_OUT_OF_MEMORY
 * (or what the setter returns); a failure of attr_fn is returned as it is. On a
 * failure, every node built is destroyed and the tokens added to the root are
 * removed (the document has the same tree and tokens as before), and *out_node is
 * NULL. What attr_fn changed on the root (an extension attribute of
 * (document ...)) is not undone. On success, the :tokens of (document ...) are
 * added to the tokens the root already has.
 *
 * out_node and out_reason may be NULL. On success *out_reason is NULL; on a
 * failure it is a NUL-terminated reason that starts with the position, such as
 * "offset 12 (line 2, column 5): expected ')'", allocated with malloc (not the
 * Amark allocator, so the test allocator does not count it) and freed with
 * amark_test_tree_free_reason. It is NULL when even that allocation failed.
 * A NULL document or sexpr is AMARK_ERROR_INVALID_ARGUMENT with a reason too.
 */
amark_error_t amark_test_tree_build(amark_document_t *document, const char *sexpr,
                                    amark_test_tree_attr_fn_t attr_fn, void *context,
                                    amark_node_t **out_node, const char **out_reason);

/* Frees a reason of amark_test_tree_build (NULL does nothing). */
void amark_test_tree_free_reason(const char *reason);

/* Prints expected and actual on standard error, each line indented, for a dump
 * that does not match (used by AMARK_CHECK_TREE_DUMP). */
void amark_test_tree_print_mismatch(const char *expected, const char *actual);

/*
 * Checks that amark_node_dump of node with flags succeeds and gives exactly
 * expected_literal, a string literal. On a mismatch both dumps are printed as
 * they are (several lines) and the check fails at the line of the call.
 * Expanded at the call site, which links the library.
 */
#define AMARK_CHECK_TREE_DUMP(node, flags, expected_literal) \
    do { \
        static const char amark_tree_expected_[] = "" expected_literal; \
        amark_buffer_t *amark_tree_out_ = NULL; \
        amark_error_t amark_tree_err_ = amark_buffer_create(&amark_tree_out_); \
        if (amark_tree_err_ == AMARK_ERROR_NONE) \
        { \
            amark_tree_err_ = amark_node_dump((node), (flags), amark_tree_out_); \
        } \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_tree_err_); \
        if (amark_tree_err_ == AMARK_ERROR_NONE) \
        { \
            const char *amark_tree_actual_ = amark_buffer_data(amark_tree_out_); \
            size_t amark_tree_length_ = amark_buffer_length(amark_tree_out_); \
            if ((amark_tree_length_ != (sizeof(amark_tree_expected_) - 1)) \
                || (memcmp(amark_tree_actual_, amark_tree_expected_, amark_tree_length_) != 0)) \
            { \
                amark_test_tree_print_mismatch(amark_tree_expected_, amark_tree_actual_); \
            } \
            NAOS_CHECK_MEM_EQ(amark_tree_actual_, amark_tree_length_, amark_tree_expected_, \
                              sizeof(amark_tree_expected_) - 1); \
        } \
        amark_buffer_destroy(amark_tree_out_); \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* AMARK_TEST_TREE_BUILDER_H */
