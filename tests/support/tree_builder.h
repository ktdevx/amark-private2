/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * Building trees from S-expressions in the format of the dump for the tests
 * (design, TestSupport; 13.8).
 *
 * tree_builder.c is the INTERFACE source of amark_test_tree: it is compiled with
 * the settings of each test that links it, and it uses only the public API, so it
 * goes with either amark or amark_internal. It depends on Naos, so fuzz targets
 * must not link it.
 *
 * The format (design, "Tree: Iterator, Equality, Dump, TreeCheck"):
 *
 *   node = "(" type-name {attr} [literal] {node} ")"
 *
 * Atoms are separated by white space (space, tab, LF, CR); ";" starts a comment
 * that runs to the end of the line. A string is "..." with the escapes \" \\ \n
 * \r \t and \xHH (any byte); an integer is an optional "-" and decimal digits;
 * a keyword is ":" and a name; any other run of characters other than white
 * space, "(", ")", '"' and ";" is a symbol. The attributes of a node:
 *
 *   :name value       a value facet of the type, recorded with
 *                     amark_node_record_*: a string for a STRING facet, an
 *                     integer for an INT facet, true or false for a BOOL facet,
 *                     and the name of a value (a symbol or a string) for an ENUM
 *                     facet
 *   literal           a string after the attributes: the facet named "literal"
 *   :style (:name value ...)
 *                     style facets, recorded the same way
 *   :lines (first count)
 *   :margins ((line start end present|lazy [first] [:role n]) ...)
 *   :blanks ((line start end) ...)
 *   :tokens ((kind line start end [:facet name] [:role n]) ...)
 *   :content laid ((line start end) ...)   or   :content free "..."
 *                     the layout of a block, recorded in this order with the
 *                     Layout and Content functions (amark_block_*); lines are
 *                     the line identifiers of the line table of the document,
 *                     whose sources are added before the build, and columns are
 *                     its columns
 *   :spelling ((kind start end [:facet name] [:role n]) ...)
 *                     the content tokens of the node, on the content of the
 *                     nearest block with content among the node and its
 *                     ancestors (amark_node_add_content_token)
 *   :dropped (name ...)
 *                     the dropped-spelling marks: "structure" for the
 *                     structural spelling (amark_node_discard_spelling on the new
 *                     node), a facet name for the spelling of that facet
 *                     (amark_node_discard_facet_spelling on the new node)
 *   :pos (sl sc el ec)
 *                     accepted and ignored (positions are derived)
 *   :line_table ((source number offset length eol width) ...)
 *                     the document node only: checked against the line table of
 *                     the document (eol is none, lf, crlf or cr)
 *
 * The kinds of the tokens are text, marker, space, delimiter, escape, char_ref,
 * replaced, line_break and tab_remainder (amark_spell_kind_t).
 *
 * A node is built in this order: the node is created, its dropped marks are
 * set, it is appended to its parent, its layout is recorded, its values and
 * styles are recorded, its content tokens are added, its children are built. A
 * CONTENT facet (the literal of code_block and html_block) is recorded only when
 * the node has no :content; otherwise, as every value given, it is checked
 * against the value read back (amark_node_get), which also checks a value
 * derived from the tokens.
 */
#ifndef AMARK_TEST_TREE_BUILDER_H
#define AMARK_TEST_TREE_BUILDER_H

#include <amark/node.h>
#include <amark/document.h>
#include <amark/value.h>
#include <amark/spelling.h>
#include <amark/dump.h>

#include <naos/naos.h>

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * Builds the nodes of sexpr, one or more nodes in sequence, in document and
 * appends them to parent, or to the document node when parent is NULL. A
 * "(document ...)" node stands for the document node itself (parent must then
 * be NULL or the document node): its children are appended to it, and its
 * attributes are recorded on it after its children are built.
 *
 * Returns AMARK_ERROR_NONE and sets *reason to NULL on success. On failure
 * every node the build created is destroyed, and *reason (when reason is not
 * NULL) receives a description of the failure that begins, for a failure in
 * sexpr, with its position ("line L, column C: ..."), to be released with
 * amark_test_tree_free_reason (NULL when even that could not be allocated).
 * Returns AMARK_ERROR_INVALID_ARGUMENT for NULL arguments and a malformed or
 * inconsistent S-expression, AMARK_ERROR_OUT_OF_MEMORY when an allocation
 * fails, and otherwise the error of the Amark function that refused a record.
 * The records already made on the document node by a failing "(document ...)"
 * stay.
 */
amark_error_t amark_test_tree_build(amark_document_t *document, amark_node_t *parent,
                                    const char *sexpr, char **reason);

/* Releases a reason of amark_test_tree_build. NULL is ignored. */
void amark_test_tree_free_reason(char *reason);

/*
 * Builds sexpr into parent of document (amark_test_tree_build) and checks, at the
 * line of the caller and without stopping the test, that it succeeds; on failure
 * the reason is shown in the message of the check.
 */
#define AMARK_CHECK_TREE_BUILD(document, parent, sexpr) \
    do { \
        char *amark_reason_ = NULL; \
        amark_error_t amark_error_ = \
            amark_test_tree_build((document), (parent), (sexpr), &amark_reason_); \
        NAOS_CHECK_STR_EQ("", amark_reason_ != NULL ? amark_reason_ : ""); \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_error_); \
        amark_test_tree_free_reason(amark_reason_); \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* AMARK_TEST_TREE_BUILDER_H */
