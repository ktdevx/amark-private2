/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The traversal (src/tree/iterator.c), through the public API.
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
 * Suite iter_oom: the out-of-memory sweep of amark_iter_create, which does not
 * sets *out_iter to NULL on a failure.
 */
#include <amark/allocator.h>
#include <amark/document.h>
#include <amark/error.h>
#include <amark/iterator.h>
#include <amark/node.h>

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
    NAOS_ADD_SUITE(iter_oom)
)
