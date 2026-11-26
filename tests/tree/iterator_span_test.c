/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The traversal (src/tree/iterator.c) and the source span (src/tree/span.c),
 * through the public API.
 *
 * Suite iter (3.2, 4.9): the events of a sample tree in document order (a
 * container is entered and exited, also without children; a leaf is only
 * entered), as the relation getters give them; the state before the first event
 * and after the end (DONE again on every later call); a leaf as the root (ENTER,
 * then DONE); a subtree with siblings and a parent is never left; a detached
 * subtree; a chain of 100000 nodes without recursion; the NULL arguments.
 * Suite iter_edit (3.3, 3.4): changing the literal of the current text node, and
 * replacing the children of the current node with amark_node_set_inline_text,
 * still visits the unvisited part of the tree as it is after the change; after
 * unlinking the current node, amark_iter_reset sets the position again and the
 * traversal continues; amark_iter_reset to an ENTER or an EXIT; the rejections of
 * amark_iter_reset (a node outside the subtree of the root, of another document,
 * an EXIT on a leaf, NONE and DONE, NULL) leave the iterator unchanged.
 * Suite span (3.5, 3.6): amark_node_source_span of a node whose tokens are only on
 * its descendants; the source of the first token in preorder wins over the tokens
 * of another source; a subtree without tokens (a new node, tokens only outside the
 * subtree) gives false and writes nothing; lines and columns with LF and CRLF, up
 * to the end of a source; a zero-length token counts as a position; a detached
 * subtree; NULL arguments, a struct_size below version 1 (nothing written) and a
 * larger struct_size (kept, the bytes after version 1 untouched).
 * Suite iter_oom: the out-of-memory sweep of amark_iter_create, which does not
 * sets *out_iter to NULL on a failure.
 */
#include <amark/allocator.h>
#include <amark/document.h>
#include <amark/error.h>
#include <amark/iterator.h>
#include <amark/node.h>
#include <amark/token.h>

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <naos/naos.h>

#include "test_allocator.h"

NAOS_TEARDOWN(restore)
{
    amark_allocator_set(NULL);
}

/* An expected event. */
typedef struct step
{
    amark_node_t *node;
    amark_event_t event;
} step_t;

/* Creates a node of type and appends it to parent (when not NULL). */
static amark_node_t *add(amark_document_t *document, amark_node_t *parent, amark_node_type_t type)
{
    amark_node_t *node = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, type, &node));
    if (parent != NULL)
    {
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(parent, node));
    }

    return node;
}

/* Creates a text node with literal and appends it to parent. */
static amark_node_t *add_text(amark_document_t *document, amark_node_t *parent, const char *literal)
{
    amark_node_t *node = add(document, parent, AMARK_NODE_TEXT);

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_literal(node, literal, strlen(literal)));

    return node;
}

/* Returns whether the next events of iter are the count steps, then DONE. Each
 * event is also checked against amark_iter_node and amark_iter_event. */
static bool follows(amark_iter_t *iter, const step_t *steps, size_t count)
{
    size_t i;

    for (i = 0; i < count; i++)
    {
        amark_event_t event = amark_iter_next(iter);

        if ((event != steps[i].event) || (amark_iter_event(iter) != event)
            || (amark_iter_node(iter) != steps[i].node))
        {
            return false;
        }
    }

    return (amark_iter_next(iter) == AMARK_EVENT_DONE) && (amark_iter_node(iter) == NULL)
           && (amark_iter_event(iter) == AMARK_EVENT_DONE);
}

/* The sample tree:
 *   document
 *     heading (text "A")
 *     paragraph (text "b", emph (text "c"), softbreak, emph)
 *     block_quote
 *     thematic_break */
typedef struct sample
{
    amark_document_t *document;
    amark_node_t *root;
    amark_node_t *heading;
    amark_node_t *heading_text;
    amark_node_t *paragraph;
    amark_node_t *b;
    amark_node_t *emph;
    amark_node_t *c;
    amark_node_t *softbreak;
    amark_node_t *empty_emph;
    amark_node_t *quote;
    amark_node_t *rule;
} sample_t;

static void build_sample(sample_t *s)
{
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &s->document));
    s->root = amark_document_root(s->document);
    s->heading = add(s->document, s->root, AMARK_NODE_HEADING);
    s->heading_text = add_text(s->document, s->heading, "A");
    s->paragraph = add(s->document, s->root, AMARK_NODE_PARAGRAPH);
    s->b = add_text(s->document, s->paragraph, "b");
    s->emph = add(s->document, s->paragraph, AMARK_NODE_EMPH);
    s->c = add_text(s->document, s->emph, "c");
    s->softbreak = add(s->document, s->paragraph, AMARK_NODE_SOFTBREAK);
    s->empty_emph = add(s->document, s->paragraph, AMARK_NODE_EMPH);
    s->quote = add(s->document, s->root, AMARK_NODE_BLOCK_QUOTE);
    s->rule = add(s->document, s->root, AMARK_NODE_THEMATIC_BREAK);
}

static amark_iter_t *create_iter(amark_node_t *root)
{
    amark_iter_t *iter = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_iter_create(root, &iter));
    NAOS_REQUIRE_NOT_NULL(iter);

    return iter;
}

/* --- Suite iter ---------------------------------------------------------------- */

NAOS_TEST(the_events_follow_the_document_order)
{
    sample_t s;
    amark_iter_t *iter;
    step_t steps[18];
    size_t n = 0;

    build_sample(&s);
    steps[n++] = (step_t){ s.root, AMARK_EVENT_ENTER };
    steps[n++] = (step_t){ s.heading, AMARK_EVENT_ENTER };
    steps[n++] = (step_t){ s.heading_text, AMARK_EVENT_ENTER };
    steps[n++] = (step_t){ s.heading, AMARK_EVENT_EXIT };
    steps[n++] = (step_t){ s.paragraph, AMARK_EVENT_ENTER };
    steps[n++] = (step_t){ s.b, AMARK_EVENT_ENTER };
    steps[n++] = (step_t){ s.emph, AMARK_EVENT_ENTER };
    steps[n++] = (step_t){ s.c, AMARK_EVENT_ENTER };
    steps[n++] = (step_t){ s.emph, AMARK_EVENT_EXIT };
    steps[n++] = (step_t){ s.softbreak, AMARK_EVENT_ENTER };
    steps[n++] = (step_t){ s.empty_emph, AMARK_EVENT_ENTER };
    steps[n++] = (step_t){ s.empty_emph, AMARK_EVENT_EXIT };
    steps[n++] = (step_t){ s.paragraph, AMARK_EVENT_EXIT };
    steps[n++] = (step_t){ s.quote, AMARK_EVENT_ENTER };
    steps[n++] = (step_t){ s.quote, AMARK_EVENT_EXIT };
    steps[n++] = (step_t){ s.rule, AMARK_EVENT_ENTER };
    steps[n++] = (step_t){ s.root, AMARK_EVENT_EXIT };

    iter = create_iter(s.root);
    NAOS_CHECK_PTR_EQ(s.root, amark_iter_root(iter));
    NAOS_CHECK_TRUE(follows(iter, steps, n));
    NAOS_CHECK_PTR_EQ(s.root, amark_iter_root(iter));
    amark_iter_destroy(iter);
    amark_document_destroy(s.document);
}

NAOS_TEST(the_events_match_the_relation_getters)
{
    sample_t s;
    amark_iter_t *iter;
    amark_node_t *expected;
    amark_event_t expected_event;
    size_t events = 0;

    /* A reference traversal written with the public relation getters. */
    build_sample(&s);
    iter = create_iter(s.root);
    expected = s.root;
    expected_event = AMARK_EVENT_ENTER;
    while (expected != NULL)
    {
        NAOS_REQUIRE_INT_EQ(expected_event, amark_iter_next(iter));
        NAOS_REQUIRE_PTR_EQ(expected, amark_iter_node(iter));
        events++;
        if ((expected_event == AMARK_EVENT_ENTER) && amark_node_can_have_children(expected))
        {
            if (amark_node_first_child(expected) != NULL)
            {
                expected = amark_node_first_child(expected);
            }
            else
            {
                expected_event = AMARK_EVENT_EXIT;
            }
        }
        else if (expected == s.root)
        {
            expected = NULL;
        }
        else if (amark_node_next(expected) != NULL)
        {
            expected = amark_node_next(expected);
            expected_event = AMARK_EVENT_ENTER;
        }
        else
        {
            expected = amark_node_parent(expected);
            expected_event = AMARK_EVENT_EXIT;
        }
    }

    NAOS_CHECK_UINT_EQ(17, events);
    NAOS_CHECK_INT_EQ(AMARK_EVENT_DONE, amark_iter_next(iter));
    amark_iter_destroy(iter);
    amark_document_destroy(s.document);
}

NAOS_TEST(the_state_before_the_first_event_and_after_the_end)
{
    sample_t s;
    amark_iter_t *iter;
    int i;

    build_sample(&s);
    iter = create_iter(s.quote);
    NAOS_CHECK_NULL(amark_iter_node(iter));
    NAOS_CHECK_INT_EQ(AMARK_EVENT_NONE, amark_iter_event(iter));
    NAOS_CHECK_PTR_EQ(s.quote, amark_iter_root(iter));

    NAOS_CHECK_INT_EQ(AMARK_EVENT_ENTER, amark_iter_next(iter));
    NAOS_CHECK_INT_EQ(AMARK_EVENT_EXIT, amark_iter_next(iter));
    NAOS_CHECK_PTR_EQ(s.quote, amark_iter_node(iter));
    for (i = 0; i < 3; i++)
    {
        NAOS_CHECK_INT_EQ(AMARK_EVENT_DONE, amark_iter_next(iter));
        NAOS_CHECK_NULL(amark_iter_node(iter));
        NAOS_CHECK_INT_EQ(AMARK_EVENT_DONE, amark_iter_event(iter));
    }

    amark_iter_destroy(iter);
    amark_document_destroy(s.document);
}

NAOS_TEST(a_leaf_root_is_entered_then_done)
{
    sample_t s;
    amark_iter_t *iter;
    step_t text_steps[1];
    step_t rule_steps[1];

    build_sample(&s);
    text_steps[0] = (step_t){ s.b, AMARK_EVENT_ENTER };
    iter = create_iter(s.b);
    NAOS_CHECK_TRUE(follows(iter, text_steps, 1));
    NAOS_CHECK_INT_EQ(AMARK_EVENT_DONE, amark_iter_next(iter));
    amark_iter_destroy(iter);

    rule_steps[0] = (step_t){ s.rule, AMARK_EVENT_ENTER };
    iter = create_iter(s.rule);
    NAOS_CHECK_TRUE(follows(iter, rule_steps, 1));
    amark_iter_destroy(iter);
    amark_document_destroy(s.document);
}

NAOS_TEST(a_subtree_is_never_left)
{
    sample_t s;
    amark_iter_t *iter;
    step_t steps[3];

    /* emph has siblings on both sides and a parent. */
    build_sample(&s);
    steps[0] = (step_t){ s.emph, AMARK_EVENT_ENTER };
    steps[1] = (step_t){ s.c, AMARK_EVENT_ENTER };
    steps[2] = (step_t){ s.emph, AMARK_EVENT_EXIT };
    iter = create_iter(s.emph);
    NAOS_CHECK_TRUE(follows(iter, steps, 3));
    amark_iter_destroy(iter);

    /* The heading is the first child of the root. */
    steps[0] = (step_t){ s.heading, AMARK_EVENT_ENTER };
    steps[1] = (step_t){ s.heading_text, AMARK_EVENT_ENTER };
    steps[2] = (step_t){ s.heading, AMARK_EVENT_EXIT };
    iter = create_iter(s.heading);
    NAOS_CHECK_TRUE(follows(iter, steps, 3));
    amark_iter_destroy(iter);
    amark_document_destroy(s.document);
}

NAOS_TEST(a_detached_subtree_is_traversed)
{
    amark_document_t *document = NULL;
    amark_node_t *paragraph;
    amark_node_t *text;
    amark_iter_t *iter;
    step_t steps[3];

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    paragraph = add(document, NULL, AMARK_NODE_PARAGRAPH);
    text = add_text(document, paragraph, "x");
    steps[0] = (step_t){ paragraph, AMARK_EVENT_ENTER };
    steps[1] = (step_t){ text, AMARK_EVENT_ENTER };
    steps[2] = (step_t){ paragraph, AMARK_EVENT_EXIT };
    iter = create_iter(paragraph);
    NAOS_CHECK_TRUE(follows(iter, steps, 3));
    amark_iter_destroy(iter);
    amark_document_destroy(document);
}

NAOS_TEST(a_deep_chain_is_traversed_without_recursion)
{
    amark_document_t *document = NULL;
    amark_node_t *root;
    amark_node_t *deepest;
    amark_iter_t *iter;
    amark_event_t event;
    size_t enters = 0;
    size_t exits = 0;
    size_t depth = 0;
    size_t max_depth = 0;
    size_t i;

    /* 100000 nested block quotes with a thematic break at the bottom. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    root = amark_document_root(document);
    deepest = root;
    for (i = 0; i < 100000; i++)
    {
        deepest = add(document, deepest, AMARK_NODE_BLOCK_QUOTE);
    }

    deepest = add(document, deepest, AMARK_NODE_THEMATIC_BREAK);

    iter = create_iter(root);
    while ((event = amark_iter_next(iter)) != AMARK_EVENT_DONE)
    {
        if (event == AMARK_EVENT_ENTER)
        {
            enters++;
            if (amark_node_can_have_children(amark_iter_node(iter)))
            {
                depth++;
                if (depth > max_depth)
                {
                    max_depth = depth;
                }
            }
        }
        else
        {
            exits++;
            depth--;
        }
    }

    NAOS_CHECK_UINT_EQ(100002, enters);
    NAOS_CHECK_UINT_EQ(100001, exits);
    NAOS_CHECK_UINT_EQ(100001, max_depth);
    NAOS_CHECK_UINT_EQ(0, depth);

    /* A reset to the bottom walks the ancestors without recursion too. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_iter_reset(iter, deepest, AMARK_EVENT_ENTER));
    NAOS_CHECK_INT_EQ(AMARK_EVENT_EXIT, amark_iter_next(iter));
    NAOS_CHECK_PTR_EQ(amark_node_parent(deepest), amark_iter_node(iter));
    amark_iter_destroy(iter);
    amark_document_destroy(document);
}

NAOS_TEST(the_null_arguments_are_handled)
{
    sample_t s;
    amark_iter_t *iter = (amark_iter_t *)&s;

    build_sample(&s);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_iter_create(NULL, &iter));
    NAOS_CHECK_NULL(iter);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_iter_create(s.root, NULL));

    amark_iter_destroy(NULL);
    NAOS_CHECK_INT_EQ(AMARK_EVENT_DONE, amark_iter_next(NULL));
    NAOS_CHECK_NULL(amark_iter_node(NULL));
    NAOS_CHECK_INT_EQ(AMARK_EVENT_NONE, amark_iter_event(NULL));
    NAOS_CHECK_NULL(amark_iter_root(NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_iter_reset(NULL, s.root, AMARK_EVENT_ENTER));
    amark_document_destroy(s.document);
}

NAOS_SUITE(iter,
    NAOS_SET_TEARDOWN(restore),
    NAOS_ADD_TEST(the_events_follow_the_document_order),
    NAOS_ADD_TEST(the_events_match_the_relation_getters),
    NAOS_ADD_TEST(the_state_before_the_first_event_and_after_the_end),
    NAOS_ADD_TEST(a_leaf_root_is_entered_then_done),
    NAOS_ADD_TEST(a_subtree_is_never_left),
    NAOS_ADD_TEST(a_detached_subtree_is_traversed),
    NAOS_ADD_TEST(a_deep_chain_is_traversed_without_recursion),
    NAOS_ADD_TEST(the_null_arguments_are_handled))

/* --- Suite iter_edit ----------------------------------------------------------- */

/* Moves iter n events forward and returns the last one. */
static amark_event_t advance(amark_iter_t *iter, size_t n)
{
    amark_event_t event = AMARK_EVENT_NONE;
    size_t i;

    for (i = 0; i < n; i++)
    {
        event = amark_iter_next(iter);
    }

    return event;
}

NAOS_TEST(changing_the_literal_of_the_current_text_continues)
{
    sample_t s;
    amark_iter_t *iter;
    step_t steps[11];
    size_t n = 0;
    const char *literal;

    build_sample(&s);
    iter = create_iter(s.root);

    /* (b, ENTER) is the 6th event. */
    NAOS_REQUIRE_INT_EQ(AMARK_EVENT_ENTER, advance(iter, 6));
    NAOS_REQUIRE_PTR_EQ(s.b, amark_iter_node(iter));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_literal(s.b, "changed", 7));
    literal = amark_node_literal(amark_iter_node(iter), NULL);
    NAOS_CHECK_STR_EQ("changed", literal);

    steps[n++] = (step_t){ s.emph, AMARK_EVENT_ENTER };
    steps[n++] = (step_t){ s.c, AMARK_EVENT_ENTER };
    steps[n++] = (step_t){ s.emph, AMARK_EVENT_EXIT };
    steps[n++] = (step_t){ s.softbreak, AMARK_EVENT_ENTER };
    steps[n++] = (step_t){ s.empty_emph, AMARK_EVENT_ENTER };
    steps[n++] = (step_t){ s.empty_emph, AMARK_EVENT_EXIT };
    steps[n++] = (step_t){ s.paragraph, AMARK_EVENT_EXIT };
    steps[n++] = (step_t){ s.quote, AMARK_EVENT_ENTER };
    steps[n++] = (step_t){ s.quote, AMARK_EVENT_EXIT };
    steps[n++] = (step_t){ s.rule, AMARK_EVENT_ENTER };
    steps[n++] = (step_t){ s.root, AMARK_EVENT_EXIT };
    NAOS_CHECK_TRUE(follows(iter, steps, n));
    amark_iter_destroy(iter);
    amark_document_destroy(s.document);
}

NAOS_TEST(replacing_the_children_of_the_current_node_visits_the_new_ones)
{
    sample_t s;
    amark_iter_t *iter;
    amark_node_t *first;
    amark_node_t *line_break;
    amark_node_t *second;
    step_t steps[10];
    size_t n = 0;

    build_sample(&s);
    iter = create_iter(s.root);

    /* At (paragraph, ENTER), its children become text "x", softbreak, text "y"
     * (the old ones are destroyed). */
    NAOS_REQUIRE_INT_EQ(AMARK_EVENT_ENTER, advance(iter, 5));
    NAOS_REQUIRE_PTR_EQ(s.paragraph, amark_iter_node(iter));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_inline_text(s.paragraph, "x\ny", 3));
    first = amark_node_first_child(s.paragraph);
    NAOS_REQUIRE_NOT_NULL(first);
    line_break = amark_node_next(first);
    NAOS_REQUIRE_NOT_NULL(line_break);
    second = amark_node_next(line_break);
    NAOS_REQUIRE_NOT_NULL(second);
    NAOS_REQUIRE_PTR_EQ(second, amark_node_last_child(s.paragraph));
    NAOS_REQUIRE_UINT_EQ(AMARK_NODE_SOFTBREAK, amark_node_get_type(line_break));

    steps[n++] = (step_t){ first, AMARK_EVENT_ENTER };
    steps[n++] = (step_t){ line_break, AMARK_EVENT_ENTER };
    steps[n++] = (step_t){ second, AMARK_EVENT_ENTER };
    steps[n++] = (step_t){ s.paragraph, AMARK_EVENT_EXIT };
    steps[n++] = (step_t){ s.quote, AMARK_EVENT_ENTER };
    steps[n++] = (step_t){ s.quote, AMARK_EVENT_EXIT };
    steps[n++] = (step_t){ s.rule, AMARK_EVENT_ENTER };
    steps[n++] = (step_t){ s.root, AMARK_EVENT_EXIT };
    NAOS_CHECK_TRUE(follows(iter, steps, n));
    amark_iter_destroy(iter);
    amark_document_destroy(s.document);
}

NAOS_TEST(replacing_the_children_on_the_exit_visits_the_rest)
{
    sample_t s;
    amark_iter_t *iter;
    step_t steps[7];
    size_t n = 0;

    /* At (heading, EXIT), its children and those of the unvisited paragraph
     * change: the traversal goes on with the new children of the paragraph. */
    build_sample(&s);
    iter = create_iter(s.root);
    NAOS_REQUIRE_INT_EQ(AMARK_EVENT_EXIT, advance(iter, 4));
    NAOS_REQUIRE_PTR_EQ(s.heading, amark_iter_node(iter));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_inline_text(s.heading, "new", 3));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_inline_text(s.paragraph, "p", 1));
    NAOS_REQUIRE_NOT_NULL(amark_node_first_child(s.paragraph));

    steps[n++] = (step_t){ s.paragraph, AMARK_EVENT_ENTER };
    steps[n++] = (step_t){ amark_node_first_child(s.paragraph), AMARK_EVENT_ENTER };
    steps[n++] = (step_t){ s.paragraph, AMARK_EVENT_EXIT };
    steps[n++] = (step_t){ s.quote, AMARK_EVENT_ENTER };
    steps[n++] = (step_t){ s.quote, AMARK_EVENT_EXIT };
    steps[n++] = (step_t){ s.rule, AMARK_EVENT_ENTER };
    steps[n++] = (step_t){ s.root, AMARK_EVENT_EXIT };
    NAOS_CHECK_TRUE(follows(iter, steps, n));
    amark_iter_destroy(iter);
    amark_document_destroy(s.document);
}

NAOS_TEST(editing_other_nodes_visits_the_tree_after_the_change)
{
    sample_t s;
    amark_iter_t *iter;
    amark_node_t *added = NULL;
    step_t steps[8];
    size_t n = 0;

    build_sample(&s);
    iter = create_iter(s.root);

    /* At (c, ENTER): the softbreak is destroyed, a text is added at the end of
     * the paragraph and the block quote is moved before the heading (visited
     * already). */
    NAOS_REQUIRE_INT_EQ(AMARK_EVENT_ENTER, advance(iter, 8));
    NAOS_REQUIRE_PTR_EQ(s.c, amark_iter_node(iter));
    amark_node_destroy(s.softbreak);
    added = add_text(s.document, s.paragraph, "z");
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_before(s.heading, s.quote));

    steps[n++] = (step_t){ s.emph, AMARK_EVENT_EXIT };
    steps[n++] = (step_t){ s.empty_emph, AMARK_EVENT_ENTER };
    steps[n++] = (step_t){ s.empty_emph, AMARK_EVENT_EXIT };
    steps[n++] = (step_t){ added, AMARK_EVENT_ENTER };
    steps[n++] = (step_t){ s.paragraph, AMARK_EVENT_EXIT };
    steps[n++] = (step_t){ s.rule, AMARK_EVENT_ENTER };
    steps[n++] = (step_t){ s.root, AMARK_EVENT_EXIT };
    NAOS_CHECK_TRUE(follows(iter, steps, n));
    amark_iter_destroy(iter);
    amark_document_destroy(s.document);
}

NAOS_TEST(unlinking_the_current_node_then_resetting_continues)
{
    sample_t s;
    amark_iter_t *iter;
    step_t steps[8];
    size_t n = 0;

    build_sample(&s);
    iter = create_iter(s.root);

    /* At (emph, ENTER): unlink it, continue after the previous sibling (a leaf,
     * ENTER). */
    NAOS_REQUIRE_INT_EQ(AMARK_EVENT_ENTER, advance(iter, 7));
    NAOS_REQUIRE_PTR_EQ(s.emph, amark_iter_node(iter));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_unlink(s.emph));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_iter_reset(iter, s.b, AMARK_EVENT_ENTER));
    NAOS_CHECK_PTR_EQ(s.b, amark_iter_node(iter));
    NAOS_CHECK_INT_EQ(AMARK_EVENT_ENTER, amark_iter_event(iter));

    steps[n++] = (step_t){ s.softbreak, AMARK_EVENT_ENTER };
    steps[n++] = (step_t){ s.empty_emph, AMARK_EVENT_ENTER };
    steps[n++] = (step_t){ s.empty_emph, AMARK_EVENT_EXIT };
    steps[n++] = (step_t){ s.paragraph, AMARK_EVENT_EXIT };
    steps[n++] = (step_t){ s.quote, AMARK_EVENT_ENTER };
    steps[n++] = (step_t){ s.quote, AMARK_EVENT_EXIT };
    steps[n++] = (step_t){ s.rule, AMARK_EVENT_ENTER };
    steps[n++] = (step_t){ s.root, AMARK_EVENT_EXIT };
    NAOS_CHECK_TRUE(follows(iter, steps, n));

    /* The unlinked node is outside the subtree of the root now. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_iter_reset(iter, s.emph, AMARK_EVENT_ENTER));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_iter_reset(iter, s.c, AMARK_EVENT_ENTER));
    amark_iter_destroy(iter);
    amark_document_destroy(s.document);
}

NAOS_TEST(destroying_the_current_node_then_resetting_to_the_parent_exit)
{
    sample_t s;
    amark_iter_t *iter;
    step_t steps[4];
    size_t n = 0;

    build_sample(&s);
    iter = create_iter(s.root);

    /* At (heading_text, ENTER): destroy it and continue with the exit of the
     * heading. */
    NAOS_REQUIRE_INT_EQ(AMARK_EVENT_ENTER, advance(iter, 3));
    NAOS_REQUIRE_PTR_EQ(s.heading_text, amark_iter_node(iter));
    amark_node_destroy(s.heading_text);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_iter_reset(iter, s.heading, AMARK_EVENT_EXIT));

    NAOS_REQUIRE_INT_EQ(AMARK_EVENT_ENTER, amark_iter_next(iter));
    NAOS_CHECK_PTR_EQ(s.paragraph, amark_iter_node(iter));

    /* A reset to a later position skips the rest of the paragraph. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_iter_reset(iter, s.paragraph, AMARK_EVENT_EXIT));
    steps[n++] = (step_t){ s.quote, AMARK_EVENT_ENTER };
    steps[n++] = (step_t){ s.quote, AMARK_EVENT_EXIT };
    steps[n++] = (step_t){ s.rule, AMARK_EVENT_ENTER };
    steps[n++] = (step_t){ s.root, AMARK_EVENT_EXIT };
    NAOS_CHECK_TRUE(follows(iter, steps, n));
    amark_iter_destroy(iter);
    amark_document_destroy(s.document);
}

NAOS_TEST(a_reset_after_the_end_or_before_the_start_continues)
{
    sample_t s;
    amark_iter_t *iter;
    step_t steps[3];

    build_sample(&s);

    /* Before the first event. */
    iter = create_iter(s.root);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_iter_reset(iter, s.quote, AMARK_EVENT_ENTER));
    steps[0] = (step_t){ s.quote, AMARK_EVENT_EXIT };
    steps[1] = (step_t){ s.rule, AMARK_EVENT_ENTER };
    steps[2] = (step_t){ s.root, AMARK_EVENT_EXIT };
    NAOS_CHECK_TRUE(follows(iter, steps, 3));

    /* After DONE: the root itself. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_iter_reset(iter, s.root, AMARK_EVENT_EXIT));
    NAOS_CHECK_INT_EQ(AMARK_EVENT_DONE, amark_iter_next(iter));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_iter_reset(iter, s.rule, AMARK_EVENT_ENTER));
    NAOS_CHECK_TRUE(follows(iter, steps + 2, 1));
    amark_iter_destroy(iter);
    amark_document_destroy(s.document);
}

/* Expects amark_iter_reset(iter, node, event) to be rejected and iter to stay at
 * (at, at_event). */
#define CHECK_REJECTED(iter, node, event, at, at_event) \
    do { \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, \
                          amark_iter_reset((iter), (node), (event))); \
        NAOS_CHECK_PTR_EQ((at), amark_iter_node(iter)); \
        NAOS_CHECK_INT_EQ((at_event), amark_iter_event(iter)); \
    } while (0)

NAOS_TEST(the_invalid_resets_are_rejected)
{
    sample_t s;
    amark_document_t *other = NULL;
    amark_node_t *detached;
    amark_iter_t *iter;

    build_sample(&s);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &other));
    detached = add(s.document, NULL, AMARK_NODE_PARAGRAPH);
    iter = create_iter(s.paragraph);
    NAOS_REQUIRE_INT_EQ(AMARK_EVENT_ENTER, advance(iter, 2));
    NAOS_REQUIRE_PTR_EQ(s.b, amark_iter_node(iter));

    /* Outside the subtree: a sibling, the parent, a detached node, another
     * document. */
    CHECK_REJECTED(iter, s.quote, AMARK_EVENT_ENTER, s.b, AMARK_EVENT_ENTER);
    CHECK_REJECTED(iter, s.root, AMARK_EVENT_EXIT, s.b, AMARK_EVENT_ENTER);
    CHECK_REJECTED(iter, detached, AMARK_EVENT_ENTER, s.b, AMARK_EVENT_ENTER);
    CHECK_REJECTED(iter, amark_document_root(other), AMARK_EVENT_ENTER, s.b, AMARK_EVENT_ENTER);

    /* An exit on a leaf, also on one that is the root's child. */
    CHECK_REJECTED(iter, s.softbreak, AMARK_EVENT_EXIT, s.b, AMARK_EVENT_ENTER);
    CHECK_REJECTED(iter, s.c, AMARK_EVENT_EXIT, s.b, AMARK_EVENT_ENTER);

    /* Events that are not positions, and NULL. */
    CHECK_REJECTED(iter, s.emph, AMARK_EVENT_NONE, s.b, AMARK_EVENT_ENTER);
    CHECK_REJECTED(iter, s.emph, AMARK_EVENT_DONE, s.b, AMARK_EVENT_ENTER);
    CHECK_REJECTED(iter, s.emph, (amark_event_t)7, s.b, AMARK_EVENT_ENTER);
    CHECK_REJECTED(iter, NULL, AMARK_EVENT_ENTER, s.b, AMARK_EVENT_ENTER);

    /* The accepted ones: the root itself, an exit on an empty container. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_iter_reset(iter, s.paragraph, AMARK_EVENT_ENTER));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_iter_reset(iter, s.empty_emph, AMARK_EVENT_EXIT));
    NAOS_CHECK_INT_EQ(AMARK_EVENT_EXIT, amark_iter_next(iter));
    NAOS_CHECK_PTR_EQ(s.paragraph, amark_iter_node(iter));
    NAOS_CHECK_INT_EQ(AMARK_EVENT_DONE, amark_iter_next(iter));

    amark_iter_destroy(iter);
    amark_document_destroy(other);
    amark_document_destroy(s.document);
}

NAOS_SUITE(iter_edit,
    NAOS_SET_TEARDOWN(restore),
    NAOS_ADD_TEST(changing_the_literal_of_the_current_text_continues),
    NAOS_ADD_TEST(replacing_the_children_of_the_current_node_visits_the_new_ones),
    NAOS_ADD_TEST(replacing_the_children_on_the_exit_visits_the_rest),
    NAOS_ADD_TEST(editing_other_nodes_visits_the_tree_after_the_change),
    NAOS_ADD_TEST(unlinking_the_current_node_then_resetting_continues),
    NAOS_ADD_TEST(destroying_the_current_node_then_resetting_to_the_parent_exit),
    NAOS_ADD_TEST(a_reset_after_the_end_or_before_the_start_continues),
    NAOS_ADD_TEST(the_invalid_resets_are_rejected))

/* --- Suite span ---------------------------------------------------------------- */

/* Adds to node a token of kind covering [offset, offset + length) of source. */
static void add_token(amark_node_t *node, amark_token_kind_t kind, amark_source_id_t source,
                      size_t offset, size_t length)
{
    amark_token_t token;

    memset(&token, 0, sizeof(token));
    token.struct_size = sizeof(token);
    token.kind = kind;
    token.source = source;
    token.offset = offset;
    token.length = length;
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_add_token(node, &token));
}

/* Registers a source with the NUL-terminated text and returns its identifier. */
static amark_source_id_t add_source(amark_document_t *document, const char *text)
{
    amark_source_id_t source = 0;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_add_source(document, text, strlen(text), &source));

    return source;
}

/* Returns whether span is (source, [start, end), start_line, start_column,
 * end_line, end_column). */
static bool span_is(const amark_source_span_t *span, amark_source_id_t source, size_t start,
                    size_t end, size_t start_line, size_t start_column, size_t end_line,
                    size_t end_column)
{
    return (span->source == source) && (span->start_offset == start) && (span->end_offset == end)
           && (span->start_line == start_line) && (span->start_column == start_column)
           && (span->end_line == end_line) && (span->end_column == end_column);
}

/* A span with struct_size set and the other fields filled with a marker. */
static amark_source_span_t fresh_span(void)
{
    amark_source_span_t span;

    memset(&span, 0x5a, sizeof(span));
    span.struct_size = sizeof(span);

    return span;
}

NAOS_TEST(a_node_with_tokens_only_on_descendants_spans_them)
{
    amark_document_t *document = NULL;
    amark_node_t *paragraph;
    amark_node_t *a;
    amark_node_t *emph;
    amark_node_t *b;
    amark_source_span_t span = fresh_span();
    amark_source_id_t source;

    /* "x *ab*\n": the paragraph has no token; its descendants cover [2, 6). */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    source = add_source(document, "x *ab*\n");
    paragraph = add(document, amark_document_root(document), AMARK_NODE_PARAGRAPH);
    a = add_text(document, paragraph, "a");
    emph = add(document, paragraph, AMARK_NODE_EMPH);
    b = add_text(document, emph, "b");
    add_token(a, AMARK_TOKEN_TEXT, source, 3, 1);
    add_token(emph, AMARK_TOKEN_MARKER, source, 2, 1);
    add_token(emph, AMARK_TOKEN_MARKER, source, 5, 1);
    add_token(b, AMARK_TOKEN_TEXT, source, 4, 1);

    NAOS_CHECK_TRUE(amark_node_source_span(paragraph, &span));
    NAOS_CHECK_TRUE(span_is(&span, source, 2, 6, 1, 3, 1, 7));
    NAOS_CHECK_UINT_EQ(sizeof(span), span.struct_size);

    span = fresh_span();
    NAOS_CHECK_TRUE(amark_node_source_span(amark_document_root(document), &span));
    NAOS_CHECK_TRUE(span_is(&span, source, 2, 6, 1, 3, 1, 7));

    span = fresh_span();
    NAOS_CHECK_TRUE(amark_node_source_span(b, &span));
    NAOS_CHECK_TRUE(span_is(&span, source, 4, 5, 1, 5, 1, 6));

    amark_document_destroy(document);
}

NAOS_TEST(the_source_of_the_first_token_in_preorder_wins)
{
    amark_document_t *document = NULL;
    amark_node_t *paragraph;
    amark_node_t *first;
    amark_node_t *second;
    amark_node_t *third;
    amark_source_span_t span = fresh_span();
    amark_source_id_t zero;
    amark_source_id_t one;

    /* The first token in preorder is in source one (on the first text); the tokens
     * of source zero, even those before it in source order, are ignored. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    zero = add_source(document, "abcdefghij");
    one = add_source(document, "0123456789");
    paragraph = add(document, amark_document_root(document), AMARK_NODE_PARAGRAPH);
    first = add_text(document, paragraph, "a");
    second = add_text(document, paragraph, "b");
    third = add_text(document, paragraph, "c");
    add_token(first, AMARK_TOKEN_TEXT, one, 4, 2);
    add_token(second, AMARK_TOKEN_TEXT, zero, 0, 10);
    add_token(second, AMARK_TOKEN_TEXT, one, 2, 1);
    add_token(third, AMARK_TOKEN_TEXT, one, 7, 2);
    add_token(third, AMARK_TOKEN_TEXT, zero, 9, 1);

    NAOS_CHECK_TRUE(amark_node_source_span(paragraph, &span));
    NAOS_CHECK_TRUE(span_is(&span, one, 2, 9, 1, 3, 1, 10));

    /* Within one node the tokens are in source order: the first is of source
     * zero. */
    span = fresh_span();
    NAOS_CHECK_TRUE(amark_node_source_span(second, &span));
    NAOS_CHECK_TRUE(span_is(&span, zero, 0, 10, 1, 1, 1, 11));

    amark_document_destroy(document);
}

NAOS_TEST(a_subtree_without_tokens_has_no_span)
{
    amark_document_t *document = NULL;
    amark_node_t *paragraph;
    amark_node_t *text;
    amark_node_t *sibling;
    amark_source_span_t span = fresh_span();
    amark_source_span_t before;
    amark_source_id_t source;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    source = add_source(document, "ab");

    /* A new node, and a new subtree whose tokens are only on a sibling (outside
     * the subtree) or the parent. */
    paragraph = add(document, amark_document_root(document), AMARK_NODE_PARAGRAPH);
    text = add_text(document, paragraph, "a");
    sibling = add_text(document, paragraph, "b");
    add_token(sibling, AMARK_TOKEN_TEXT, source, 1, 1);
    add_token(paragraph, AMARK_TOKEN_TEXT, source, 0, 1);
    before = span;
    NAOS_CHECK_FALSE(amark_node_source_span(text, &span));
    NAOS_CHECK_TRUE(memcmp(&before, &span, sizeof(span)) == 0);

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, AMARK_NODE_EMPH, &text));
    NAOS_CHECK_FALSE(amark_node_source_span(text, &span));
    NAOS_CHECK_TRUE(memcmp(&before, &span, sizeof(span)) == 0);

    amark_document_destroy(document);
}

NAOS_TEST(the_lines_and_columns_follow_lf_and_crlf)
{
    amark_document_t *document = NULL;
    amark_node_t *quote;
    amark_node_t *paragraph;
    amark_node_t *text;
    amark_source_span_t span = fresh_span();
    amark_source_id_t lf;
    amark_source_id_t crlf;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    lf = add_source(document, "> ab\n> cd\nxy");
    crlf = add_source(document, "> ab\r\n> cd\r\n");

    /* LF: [0, 10) is from line 1 column 1 to line 3 column 1 (after the LF). */
    quote = add(document, amark_document_root(document), AMARK_NODE_BLOCK_QUOTE);
    paragraph = add(document, quote, AMARK_NODE_PARAGRAPH);
    text = add_text(document, paragraph, "ab");
    add_token(quote, AMARK_TOKEN_MARKER, lf, 0, 1);
    add_token(quote, AMARK_TOKEN_MARKER, lf, 5, 1);
    add_token(text, AMARK_TOKEN_TEXT, lf, 2, 2);
    add_token(paragraph, AMARK_TOKEN_LINE_ENDING, lf, 4, 1);
    add_token(text, AMARK_TOKEN_TEXT, lf, 7, 2);
    add_token(paragraph, AMARK_TOKEN_LINE_ENDING, lf, 9, 1);
    NAOS_CHECK_TRUE(amark_node_source_span(quote, &span));
    NAOS_CHECK_TRUE(span_is(&span, lf, 0, 10, 1, 1, 3, 1));

    /* The second line only: [7, 9) is line 2, columns 3 to 5. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, AMARK_NODE_TEXT, &text));
    add_token(text, AMARK_TOKEN_TEXT, lf, 7, 2);
    span = fresh_span();
    NAOS_CHECK_TRUE(amark_node_source_span(text, &span));
    NAOS_CHECK_TRUE(span_is(&span, lf, 7, 9, 2, 3, 2, 5));

    /* CRLF: the CR and the LF of a line ending are on its line; the end of the
     * source (offset 12) is on line 3. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, AMARK_NODE_TEXT, &text));
    add_token(text, AMARK_TOKEN_TEXT, crlf, 2, 2);
    add_token(text, AMARK_TOKEN_LINE_ENDING, crlf, 4, 2);
    span = fresh_span();
    NAOS_CHECK_TRUE(amark_node_source_span(text, &span));
    NAOS_CHECK_TRUE(span_is(&span, crlf, 2, 6, 1, 3, 2, 1));

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, AMARK_NODE_TEXT, &text));
    add_token(text, AMARK_TOKEN_TEXT, crlf, 8, 2);
    add_token(text, AMARK_TOKEN_LINE_ENDING, crlf, 10, 2);
    span = fresh_span();
    NAOS_CHECK_TRUE(amark_node_source_span(text, &span));
    NAOS_CHECK_TRUE(span_is(&span, crlf, 8, 12, 2, 3, 3, 1));

    /* A token that ends with the LF source ([10, 12) of "xy") ends on line 3,
     * column 3. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, AMARK_NODE_TEXT, &text));
    add_token(text, AMARK_TOKEN_TEXT, lf, 10, 2);
    span = fresh_span();
    NAOS_CHECK_TRUE(amark_node_source_span(text, &span));
    NAOS_CHECK_TRUE(span_is(&span, lf, 10, 12, 3, 1, 3, 3));

    amark_document_destroy(document);
}

NAOS_TEST(a_zero_length_token_marks_a_position)
{
    amark_document_t *document = NULL;
    amark_node_t *quote;
    amark_node_t *text;
    amark_source_span_t span = fresh_span();
    amark_source_id_t source;

    /* A lazy continuation (length 0) counts: alone it gives an empty span, and it
     * extends the span of the other tokens. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    source = add_source(document, "> a\nb");
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(document, AMARK_NODE_BLOCK_QUOTE, &quote));
    add_token(quote, AMARK_TOKEN_LAZY_CONTINUATION, source, 4, 0);
    NAOS_CHECK_TRUE(amark_node_source_span(quote, &span));
    NAOS_CHECK_TRUE(span_is(&span, source, 4, 4, 2, 1, 2, 1));

    text = add(document, quote, AMARK_NODE_PARAGRAPH);
    add_token(text, AMARK_TOKEN_TEXT, source, 2, 1);
    span = fresh_span();
    NAOS_CHECK_TRUE(amark_node_source_span(quote, &span));
    NAOS_CHECK_TRUE(span_is(&span, source, 2, 4, 1, 3, 2, 1));

    amark_document_destroy(document);
}

NAOS_TEST(a_detached_subtree_has_its_span)
{
    amark_document_t *document = NULL;
    amark_node_t *paragraph;
    amark_node_t *emph;
    amark_node_t *text;
    amark_node_t *after;
    amark_source_span_t span = fresh_span();
    amark_source_id_t source;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    source = add_source(document, "a *b* c");
    paragraph = add(document, amark_document_root(document), AMARK_NODE_PARAGRAPH);
    emph = add(document, paragraph, AMARK_NODE_EMPH);
    text = add_text(document, emph, "b");
    after = add_text(document, paragraph, "c");
    add_token(text, AMARK_TOKEN_TEXT, source, 3, 1);
    add_token(after, AMARK_TOKEN_TEXT, source, 6, 1);
    add_token(paragraph, AMARK_TOKEN_TEXT, source, 0, 1);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_unlink(emph));

    NAOS_CHECK_TRUE(amark_node_source_span(emph, &span));
    NAOS_CHECK_TRUE(span_is(&span, source, 3, 4, 1, 4, 1, 5));

    amark_document_destroy(document);
}

NAOS_TEST(the_struct_size_and_null_arguments_are_handled)
{
    amark_document_t *document = NULL;
    amark_node_t *text;
    amark_source_id_t source;
    amark_source_span_t span = fresh_span();
    amark_source_span_t before;
    unsigned char larger[sizeof(amark_source_span_t) + 16];
    amark_source_span_t *large = (amark_source_span_t *)(void *)larger;
    size_t i;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    source = add_source(document, "ab\ncd");
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, AMARK_NODE_TEXT, &text));
    add_token(text, AMARK_TOKEN_TEXT, source, 3, 2);

    NAOS_CHECK_FALSE(amark_node_source_span(NULL, &span));
    NAOS_CHECK_FALSE(amark_node_source_span(text, NULL));

    /* A struct_size below version 1 is rejected and nothing is written. */
    span.struct_size = sizeof(span) - 1;
    before = span;
    NAOS_CHECK_FALSE(amark_node_source_span(text, &span));
    NAOS_CHECK_TRUE(memcmp(&before, &span, sizeof(span)) == 0);

    /* A larger struct_size is kept, and the bytes after version 1 are not
     * written. */
    memset(larger, 0xa5, sizeof(larger));
    large->struct_size = sizeof(larger);
    NAOS_CHECK_TRUE(amark_node_source_span(text, large));
    NAOS_CHECK_UINT_EQ(sizeof(larger), large->struct_size);
    NAOS_CHECK_TRUE(span_is(large, source, 3, 5, 2, 1, 2, 3));
    for (i = sizeof(amark_source_span_t); i < sizeof(larger); i++)
    {
        NAOS_CHECK_UINT_EQ(0xa5u, larger[i]);
    }

    amark_document_destroy(document);
}

NAOS_SUITE(span,
    NAOS_SET_TEARDOWN(restore),
    NAOS_ADD_TEST(a_node_with_tokens_only_on_descendants_spans_them),
    NAOS_ADD_TEST(the_source_of_the_first_token_in_preorder_wins),
    NAOS_ADD_TEST(a_subtree_without_tokens_has_no_span),
    NAOS_ADD_TEST(the_lines_and_columns_follow_lf_and_crlf),
    NAOS_ADD_TEST(a_zero_length_token_marks_a_position),
    NAOS_ADD_TEST(a_detached_subtree_has_its_span),
    NAOS_ADD_TEST(the_struct_size_and_null_arguments_are_handled))

/* --- Suite iter_oom ------------------------------------------------------------ */

static amark_error_t create_scenario(void *context)
{
    bool *ok = context;
    amark_document_t *document = NULL;
    amark_iter_t *iter;
    amark_error_t err;

    err = amark_document_create(NULL, &document);
    if (err != AMARK_ERROR_NONE)
    {
        return err;
    }

    iter = (amark_iter_t *)ok;
    err = amark_iter_create(amark_document_root(document), &iter);
    if (err == AMARK_ERROR_NONE)
    {
        if ((amark_iter_next(iter) != AMARK_EVENT_ENTER)
            || (amark_iter_node(iter) != amark_document_root(document))
            || (amark_iter_next(iter) != AMARK_EVENT_EXIT)
            || (amark_iter_next(iter) != AMARK_EVENT_DONE))
        {
            *ok = false;
        }

        amark_iter_destroy(iter);
    }
    else if (iter != NULL)
    {
        *ok = false;
    }

    amark_document_destroy(document);

    return err;
}

NAOS_TEST(creating_passes_the_out_of_memory_sweep)
{
    bool ok = true;

    AMARK_CHECK_OOM_SWEEP(create_scenario, &ok);
    NAOS_CHECK_TRUE(ok);
}

NAOS_SUITE(iter_oom,
    NAOS_SET_TEARDOWN(restore),
    NAOS_ADD_TEST(creating_passes_the_out_of_memory_sweep))

NAOS_MAIN(
    NAOS_ADD_SUITE(iter),
    NAOS_ADD_SUITE(iter_edit),
    NAOS_ADD_SUITE(span),
    NAOS_ADD_SUITE(iter_oom)
)
