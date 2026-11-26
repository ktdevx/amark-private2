/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * Deep trees and the complexity of the edits, checked through the internal
 * counters of the document (src/tree/tree.h).
 *
 * Suite deep_tree (4.9): on a chain of 100000 block quotes with a paragraph and a
 * text at the bottom, the traversal, the copy, the import into another document,
 * the import of the children into another document, the comparison, the
 * destruction of the attached chain, of a detached deep copy and of documents
 * holding deep chains finish without recursion (also under the sanitizers, whose
 * stacks are tighter); a chain of 10000 block quotes is dumped without the flags.
 * Suite complexity (10.3, 10.4, 10.5): 100000 alternating literals set on the leaf
 * of a 100000 deep chain walk the ancestors once in all (the first edit), so
 * stat_propagation_steps stays within a constant times the number of edits;
 * 100000 children inserted at the bottom of the chain with edit tracking on also
 * walk the ancestors once in all; inserting and moving nodes without children at
 * the bottom of the chain never walks the ancestors (stat_cycle_steps unchanged),
 * while a node with children walks them once; 100000 tokens added in source order
 * move no token (stat_token_moves 0), while one added out of order moves those
 * after it.
 */
#include "tree/tree.h"

#include <amark/buffer.h>
#include <amark/document.h>
#include <amark/dump.h>
#include <amark/error.h>
#include <amark/iterator.h>
#include <amark/node.h>
#include <amark/token.h>

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <naos/naos.h>

/* The depth of the deep chains, and of the dumped one (the indentation makes the
 * dump quadratic in the depth). */
#define DEPTH      100000u
#define DUMP_DEPTH 10000u
/* The number of edits, children and tokens of the complexity tests. */
#define COUNT      100000u

#define CHECK_VALID(document) \
    do { \
        const char *check_reason_ = "unset"; \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_tree_check((document), &check_reason_)); \
        NAOS_CHECK_NULL(check_reason_); \
    } while (0)

/* A chain of block quotes under the root of a document, with a paragraph holding a
 * text "x" (with a text token over the source "x") at the bottom. */
typedef struct chain
{
    amark_document_t *document;
    amark_node_t *top;
    amark_node_t *bottom;
    amark_node_t *paragraph;
    amark_node_t *text;
} chain_t;

/* Creates a node of type and appends it to parent. */
static amark_node_t *add(amark_document_t *document, amark_node_t *parent,
                         amark_node_type_t type)
{
    amark_node_t *node = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, type, &node));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(parent, node));
    return node;
}

/* Creates a node of type. */
static amark_node_t *create(amark_document_t *document, amark_node_type_t type)
{
    amark_node_t *node = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, type, &node));
    return node;
}

/*
 * Creates a document (edit tracking off) and builds the chain of depth block
 * quotes in it, from the bottom up: each block quote receives the chain below it
 * while detached, so that each cycle check walks one ancestor and the build stays
 * linear even when the check of a node without children walks the ancestors (the
 * mutation the complexity suite must detect quickly).
 */
static void chain_build(chain_t *chain, size_t depth)
{
    amark_token_t token;
    amark_source_id_t source = 0;
    amark_node_t *node;
    size_t i;

    memset(chain, 0, sizeof(*chain));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &chain->document));
    chain->paragraph = create(chain->document, AMARK_NODE_PARAGRAPH);
    chain->text = add(chain->document, chain->paragraph, AMARK_NODE_TEXT);
    node = chain->paragraph;
    for (i = 0; i < depth; i++)
    {
        amark_node_t *quote = create(chain->document, AMARK_NODE_BLOCK_QUOTE);

        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(quote, node));
        if (i == 0)
        {
            chain->bottom = quote;
        }

        node = quote;
    }

    chain->top = node;
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_append_child(amark_document_root(chain->document), node));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_literal(chain->text, "x", 1));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_add_source(chain->document, "x", 1, &source));
    memset(&token, 0, sizeof(token));
    token.struct_size = sizeof(token);
    token.kind = AMARK_TOKEN_TEXT;
    token.source = source;
    token.offset = 0;
    token.length = 1;
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_add_token(chain->text, &token));
}

/* The number of nodes of the ring of document. */
static size_t ring_count(const amark_document_t *document)
{
    const amark_node_t *node;
    size_t count = 1;

    for (node = document->ring->ring_next; node != document->ring; node = node->ring_next)
    {
        count++;
    }

    return count;
}

/* The depth of the chain of first children of node (node included), and its last
 * node in *out_last. */
static size_t chain_depth(const amark_node_t *node, const amark_node_t **out_last)
{
    size_t depth = 0;

    *out_last = NULL;
    while (node != NULL)
    {
        depth++;
        *out_last = node;
        node = amark_node_first_child(node);
    }

    return depth;
}

/* Checks that the subtree of top is a chain of DEPTH block quotes, a paragraph and
 * a text "x" with one token. */
static void check_chain_shape(const amark_node_t *top)
{
    const amark_node_t *last = NULL;
    size_t length = 0;
    const char *literal;

    NAOS_REQUIRE_NOT_NULL(top);
    NAOS_CHECK_UINT_EQ(DEPTH + 2, chain_depth(top, &last));
    NAOS_REQUIRE_NOT_NULL(last);
    NAOS_CHECK_INT_EQ(AMARK_NODE_TEXT, amark_node_get_type(last));
    NAOS_CHECK_INT_EQ(AMARK_NODE_PARAGRAPH, amark_node_get_type(amark_node_parent(last)));
    literal = amark_node_literal(last, &length);
    NAOS_CHECK_MEM_EQ(literal, length, "x", 1);
    NAOS_CHECK_UINT_EQ(1, amark_node_token_count(last));
}

/* --- Suite deep_tree -------------------------------------------------------------- */

NAOS_TEST(a_deep_chain_is_traversed)
{
    chain_t chain;
    amark_iter_t *iter = NULL;
    amark_event_t event;
    size_t enters = 0;
    size_t exits = 0;
    const amark_node_t *last_entered = NULL;

    chain_build(&chain, DEPTH);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_iter_create(amark_document_root(chain.document), &iter));
    while ((event = amark_iter_next(iter)) != AMARK_EVENT_DONE)
    {
        if (event == AMARK_EVENT_ENTER)
        {
            enters++;
            last_entered = amark_iter_node(iter);
        }
        else
        {
            exits++;
        }
    }

    /* The root, the block quotes, the paragraph and the text are entered; all but
     * the text (a leaf) are exited. */
    NAOS_CHECK_UINT_EQ(DEPTH + 3, enters);
    NAOS_CHECK_UINT_EQ(DEPTH + 2, exits);
    NAOS_CHECK_PTR_EQ(chain.text, last_entered);
    amark_iter_destroy(iter);
    amark_document_destroy(chain.document);
}

NAOS_TEST(a_deep_chain_is_copied_and_compared)
{
    chain_t chain;
    amark_node_t *copy = NULL;
    const amark_node_t *last = NULL;

    chain_build(&chain, DEPTH);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_copy(chain.top, &copy));
    check_chain_shape(copy);
    NAOS_CHECK_NULL(amark_node_parent(copy));
    NAOS_CHECK_UINT_EQ((2 * (DEPTH + 2)) + 1, ring_count(chain.document));
    NAOS_CHECK_TRUE(amark_node_equal(chain.top, copy, 0));
    NAOS_CHECK_TRUE(amark_node_equal(copy, chain.top, AMARK_EQUAL_NORMALIZE_TEXT));

    /* A difference at the bottom is found after walking the whole chain. */
    (void)chain_depth(copy, &last);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_set_literal((amark_node_t *)last, "y", 1));
    NAOS_CHECK_FALSE(amark_node_equal(chain.top, copy, 0));
    CHECK_VALID(chain.document);

    /* The detached deep copy is destroyed without recursion. */
    amark_node_destroy(copy);
    NAOS_CHECK_UINT_EQ(DEPTH + 3, ring_count(chain.document));
    CHECK_VALID(chain.document);
    amark_document_destroy(chain.document);
}

NAOS_TEST(a_deep_chain_is_imported_into_another_document)
{
    chain_t chain;
    amark_document_t *to = NULL;
    amark_node_t *copy = NULL;

    chain_build(&chain, DEPTH);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &to));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_import(to, chain.top, &copy));
    NAOS_CHECK_TRUE(amark_node_equal(chain.top, copy, 0));
    /* The source of the deep token is copied, so the copy outlives the source
     * document. */
    amark_document_destroy(chain.document);
    check_chain_shape(copy);
    NAOS_CHECK_UINT_EQ(1, amark_document_source_count(to));
    NAOS_CHECK_UINT_EQ(DEPTH + 3, ring_count(to));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(amark_document_root(to), copy));
    CHECK_VALID(to);
    amark_document_destroy(to);
}

NAOS_TEST(the_children_of_a_deep_chain_are_imported_into_another_document)
{
    chain_t chain;
    amark_document_t *to = NULL;
    amark_node_t *root;

    chain_build(&chain, DEPTH);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &to));
    root = amark_document_root(to);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_import_children(root, NULL,
                                                   amark_document_root(chain.document)));
    NAOS_CHECK_TRUE(amark_node_equal(amark_document_root(chain.document), root, 0));
    amark_document_destroy(chain.document);
    check_chain_shape(amark_node_first_child(root));
    NAOS_CHECK_NULL(amark_node_next(amark_node_first_child(root)));
    NAOS_CHECK_UINT_EQ(1, amark_document_source_count(to));
    NAOS_CHECK_UINT_EQ(DEPTH + 3, ring_count(to));
    CHECK_VALID(to);
    amark_document_destroy(to);
}

NAOS_TEST(an_attached_deep_chain_is_destroyed)
{
    chain_t chain;
    amark_node_t *root;

    chain_build(&chain, DEPTH);
    root = amark_document_root(chain.document);
    amark_node_destroy(chain.top);
    NAOS_CHECK_NULL(amark_node_first_child(root));
    NAOS_CHECK_UINT_EQ(1, ring_count(chain.document));
    CHECK_VALID(chain.document);
    amark_document_destroy(chain.document);
}

NAOS_TEST(a_document_with_deep_chains_is_destroyed)
{
    chain_t chain;
    amark_node_t *copy = NULL;

    /* An attached chain and a detached copy of it, both freed by the document. */
    chain_build(&chain, DEPTH);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_copy(chain.top, &copy));
    NAOS_CHECK_UINT_EQ((2 * (DEPTH + 2)) + 1, ring_count(chain.document));
    amark_document_destroy(chain.document);
}

NAOS_TEST(a_chain_of_ten_thousand_is_dumped)
{
    chain_t chain;
    amark_buffer_t *out = NULL;
    const char *data;
    size_t expected;
    size_t length;
    size_t depth;

    chain_build(&chain, DUMP_DEPTH);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_create(&out));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_dump(amark_document_root(chain.document), 0, out));
    /* "(document" ")" LF, each block quote LF, its indentation, "(block_quote" and
     * ")", then the paragraph and the text one and two levels deeper. */
    expected = 9 + 1 + 1;
    for (depth = 1; depth <= DUMP_DEPTH; depth++)
    {
        expected += 1 + (2 * depth) + 12 + 1;
    }

    expected += 1 + (2 * (DUMP_DEPTH + 1)) + 10 + 1;
    expected += 1 + (2 * (DUMP_DEPTH + 2)) + 9 + 1;
    data = amark_buffer_data(out);
    length = amark_buffer_length(out);
    NAOS_CHECK_UINT_EQ(expected, length);
    NAOS_REQUIRE_UINT_GE(length, DUMP_DEPTH + 40);
    NAOS_CHECK_MEM_EQ(data, 26, "(document\n  (block_quote\n ", 26);
    NAOS_CHECK_MEM_EQ(data + length - (DUMP_DEPTH + 13), 9, "(text \"x\"", 9);
    NAOS_CHECK_INT_EQ('\n', data[length - 1]);
    amark_buffer_destroy(out);
    amark_document_destroy(chain.document);
}

NAOS_SUITE(deep_tree,
    NAOS_ADD_TEST(a_deep_chain_is_traversed),
    NAOS_ADD_TEST(a_deep_chain_is_copied_and_compared),
    NAOS_ADD_TEST(a_deep_chain_is_imported_into_another_document),
    NAOS_ADD_TEST(the_children_of_a_deep_chain_are_imported_into_another_document),
    NAOS_ADD_TEST(an_attached_deep_chain_is_destroyed),
    NAOS_ADD_TEST(a_document_with_deep_chains_is_destroyed),
    NAOS_ADD_TEST(a_chain_of_ten_thousand_is_dumped))

/* --- Suite complexity ------------------------------------------------------------- */

NAOS_TEST(edits_of_a_deep_leaf_propagate_once)
{
    chain_t chain;
    amark_document_t *document;
    size_t after_first;
    size_t first_walk = 0;
    size_t i;

    chain_build(&chain, DEPTH);
    document = chain.document;
    amark_document_set_edit_tracking(document, true);
    NAOS_REQUIRE_UINT_EQ(0, document->stat_propagation_steps);

    /* The first edit gives AMARK_EDIT_DESCENDANTS to every ancestor of the text:
     * the paragraph, the block quotes and the root. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_literal(chain.text, "a", 1));
    after_first = document->stat_propagation_steps;
    NAOS_CHECK_UINT_EQ(DEPTH + 2, after_first);

    /* The later ones stop at the parent, which has it already. The loop stops at the
     * first edit that walks, so that a propagation that does not stop fails fast. */
    for (i = 1; i < COUNT; i++)
    {
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                            amark_node_set_literal(chain.text, ((i % 2) == 0) ? "a" : "b", 1));
        if (document->stat_propagation_steps != after_first)
        {
            first_walk = i;
            break;
        }
    }

    NAOS_CHECK_UINT_EQ(0, first_walk);
    NAOS_CHECK_UINT_EQ(after_first, document->stat_propagation_steps);
    NAOS_CHECK_UINT_LE(document->stat_propagation_steps, 2 * COUNT);
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_CONTENT, amark_node_edits(chain.text));
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_DESCENDANTS, amark_node_edits(chain.top));
    CHECK_VALID(document);
    amark_document_destroy(document);
}

NAOS_TEST(insertions_at_a_deep_node_propagate_once_and_never_walk_for_cycles)
{
    chain_t chain;
    amark_document_t *document;
    size_t first_walk = COUNT;
    size_t cycle_steps;
    size_t i;

    chain_build(&chain, DEPTH);
    document = chain.document;
    amark_document_set_edit_tracking(document, true);
    cycle_steps = document->stat_cycle_steps;

    /* Each text appended to the deep paragraph records AMARK_EDIT_CHILDREN on it
     * and AMARK_EDIT_CREATED on the text: only the first gives
     * AMARK_EDIT_DESCENDANTS to the paragraph, the block quotes and the root, and
     * none walks for the cycle check. The loop stops at the first insertion that
     * walks, so that a walk on each insertion fails fast. */
    for (i = 0; i < COUNT; i++)
    {
        amark_node_t *node = NULL;

        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, AMARK_NODE_TEXT, &node));
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(chain.paragraph, node));
        if ((document->stat_cycle_steps != cycle_steps)
            || ((i > 0) && (document->stat_propagation_steps != (DEPTH + 2))))
        {
            first_walk = i;
            break;
        }
    }

    NAOS_CHECK_UINT_EQ(COUNT, first_walk);
    NAOS_CHECK_UINT_EQ(DEPTH + 2, document->stat_propagation_steps);
    NAOS_CHECK_UINT_EQ(COUNT, ring_count(document) - (DEPTH + 3));
    NAOS_CHECK_UINT_LE(document->stat_propagation_steps, 2 * COUNT);
    NAOS_CHECK_UINT_EQ(cycle_steps, document->stat_cycle_steps);
    CHECK_VALID(document);
    amark_document_destroy(document);
}

NAOS_TEST(a_node_without_children_is_inserted_without_walking_the_ancestors)
{
    chain_t chain;
    amark_document_t *document;
    amark_node_t *rule = NULL;
    amark_node_t *quote = NULL;
    amark_node_t *inner = NULL;
    size_t cycle_steps;

    chain_build(&chain, DEPTH);
    document = chain.document;
    cycle_steps = document->stat_cycle_steps;

    /* Every kind of insertion of a node without children, and its moves, at the
     * bottom of the chain. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(document, AMARK_NODE_THEMATIC_BREAK, &rule));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(chain.bottom, rule));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_prepend_child(chain.bottom, rule));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_after(chain.paragraph, rule));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_before(chain.paragraph, rule));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(document, AMARK_NODE_BLOCK_QUOTE, &quote));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_replace(rule, quote));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(chain.bottom, rule));
    NAOS_CHECK_UINT_EQ(cycle_steps, document->stat_cycle_steps);

    /* A node with children walks the ancestors of its new parent once: the block
     * quotes and the root. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_unlink(quote));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(document, AMARK_NODE_PARAGRAPH, &inner));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(quote, inner));
    NAOS_CHECK_UINT_EQ(cycle_steps, document->stat_cycle_steps);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(chain.bottom, quote));
    NAOS_CHECK_UINT_EQ(cycle_steps + DEPTH + 1, document->stat_cycle_steps);
    CHECK_VALID(document);
    amark_document_destroy(document);
}

NAOS_TEST(tokens_added_in_source_order_move_nothing)
{
    static char bytes[COUNT];
    amark_document_t *document = NULL;
    amark_node_t *text;
    amark_token_t token;
    amark_token_t read;
    amark_source_id_t source = 0;
    size_t i;

    memset(bytes, 'a', sizeof(bytes));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    text = add(document, add(document, amark_document_root(document), AMARK_NODE_PARAGRAPH),
               AMARK_NODE_TEXT);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_add_source(document, bytes, sizeof(bytes), &source));
    memset(&token, 0, sizeof(token));
    token.struct_size = sizeof(token);
    token.kind = AMARK_TOKEN_TEXT;
    token.source = source;
    token.length = 1;
    for (i = 0; i < COUNT; i++)
    {
        token.offset = i;
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_add_token(text, &token));
    }

    NAOS_CHECK_UINT_EQ(COUNT, amark_node_token_count(text));
    NAOS_CHECK_UINT_EQ(0, document->stat_token_moves);
    memset(&read, 0, sizeof(read));
    read.struct_size = sizeof(read);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_token_at(text, COUNT - 1, &read));
    NAOS_CHECK_UINT_EQ(COUNT - 1, read.offset);

    /* A token out of order goes after the token of the same offset, moving the
     * others. */
    token.offset = 1;
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_add_token(text, &token));
    NAOS_CHECK_UINT_EQ(COUNT - 2, document->stat_token_moves);
    CHECK_VALID(document);
    amark_document_destroy(document);
}

NAOS_SUITE(complexity,
    NAOS_ADD_TEST(edits_of_a_deep_leaf_propagate_once),
    NAOS_ADD_TEST(insertions_at_a_deep_node_propagate_once_and_never_walk_for_cycles),
    NAOS_ADD_TEST(a_node_without_children_is_inserted_without_walking_the_ancestors),
    NAOS_ADD_TEST(tokens_added_in_source_order_move_nothing))

NAOS_MAIN(
    NAOS_ADD_SUITE(deep_tree),
    NAOS_ADD_SUITE(complexity)
)
