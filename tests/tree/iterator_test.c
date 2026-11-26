/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The tests of Iterator (syntax-tree): the events of the tree of INPUT.md 3.1 in
 * document order (enter and exit for a node whose type can have children, enter
 * only for the others), the traversal that does not leave its root, the edits
 * during a traversal (the content and the children of the current node, and the
 * nodes other than the current node and its ancestors: the unvisited part of
 * the changed tree is visited in document order), a deep tree without
 * recursion, the reset of the position, and the invalid arguments and the
 * allocation failure (4.2-4.4).
 */
#include "tree/tree.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <naos/naos.h>

#include "test_allocator.h"

/* ---- Helpers ------------------------------------------------------------------- */

static amark_node_t *make(amark_document_t *document, amark_node_type_t type, const char *label)
{
    amark_node_t *node = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, type, &node));
    NAOS_REQUIRE_NOT_NULL(node);
    /* The label names the node in the traces. */
    amark_node_set_user_data(node, (void *)label);
    return node;
}

static void append(amark_node_t *parent, amark_node_t *child)
{
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(parent, child));
}

static void check_tree_ok(amark_document_t *document)
{
    amark_tree_check_report_t report;

    NAOS_CHECK_STR_EQ("none", amark_tree_violation_name(amark_tree_check(document, &report)));
    NAOS_CHECK_NULL(report.node);
}

/* A trace of events: "+label" for an enter event and "-label" for an exit
 * event, separated by spaces. A node without a label is named by its type. */
typedef struct trace
{
    char text[1024];
    size_t length;
} trace_t;

static void trace_init(trace_t *t)
{
    t->text[0] = '\0';
    t->length = 0;
}

static void trace_add(trace_t *t, amark_event_t event, const amark_node_t *node)
{
    const char *label = (const char *)amark_node_user_data(node);
    size_t length;

    if (label == NULL)
    {
        label = amark_node_type_desc(node)->name;
    }
    length = strlen(label);
    NAOS_REQUIRE_TRUE(t->length + length + 3 < sizeof(t->text));
    if (t->length > 0)
    {
        t->text[t->length++] = ' ';
    }
    t->text[t->length++] = event == AMARK_EVENT_ENTER ? '+' : '-';
    memcpy(t->text + t->length, label, length);
    t->length += length;
    t->text[t->length] = '\0';
}

/* Moves iter to the next event, which must not be AMARK_EVENT_DONE, adds it to
 * t and returns it. */
static amark_event_t step(amark_iter_t *iter, trace_t *t)
{
    amark_event_t event = amark_iter_next(iter);

    NAOS_REQUIRE_TRUE(event == AMARK_EVENT_ENTER || event == AMARK_EVENT_EXIT);
    NAOS_REQUIRE_NOT_NULL(amark_iter_node(iter));
    trace_add(t, event, amark_iter_node(iter));
    return event;
}

/* Adds the remaining events of iter to t, up to AMARK_EVENT_DONE. */
static void finish(amark_iter_t *iter, trace_t *t)
{
    amark_event_t event;

    while ((event = amark_iter_next(iter)) != AMARK_EVENT_DONE)
    {
        NAOS_REQUIRE_TRUE(event == AMARK_EVENT_ENTER || event == AMARK_EVENT_EXIT);
        trace_add(t, event, amark_iter_node(iter));
    }
}

/* Moves iter until the event event of node (which must come). */
static void advance_to(amark_iter_t *iter, trace_t *t, const amark_node_t *node,
                       amark_event_t event)
{
    for (;;)
    {
        amark_event_t current = step(iter, t);

        if (current == event && amark_iter_node(iter) == node)
        {
            return;
        }
    }
}

/* The whole trace of the subtree of root. */
static void trace_subtree(amark_node_t *root, trace_t *t)
{
    amark_iter_t *iter = NULL;

    trace_init(t);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_iter_create(root, &iter));
    finish(iter, t);
    amark_iter_destroy(iter);
}

/* ---- Fixture: the tree of INPUT.md 3.1 ---------------------------------------- */

/*
 *   document
 *   +- quote (block_quote)
 *      +- list
 *         +- item
 *            +- para (paragraph)
 *               +- one (text "one ")
 *               +- emph
 *               |  +- two (text "two")
 *               |  +- sb (softbreak)
 *               |  +- three (text "three")
 *               +- four (text " four")
 */
typedef struct fixture
{
    amark_document_t *document;
    amark_node_t *root;
    amark_node_t *quote;
    amark_node_t *list;
    amark_node_t *item;
    amark_node_t *para;
    amark_node_t *one;
    amark_node_t *emph;
    amark_node_t *two;
    amark_node_t *sb;
    amark_node_t *three;
    amark_node_t *four;
} fixture_t;

static const char *const input_3_1_trace =
    "+document +quote +list +item +para +one +emph +two +sb +three -emph +four -para -item "
    "-list -quote -document";

static void fixture_init(fixture_t *f)
{
    memset(f, 0, sizeof(*f));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &f->document));
    f->root = amark_document_root(f->document);
    amark_node_set_user_data(f->root, (void *)"document");
    f->quote = make(f->document, AMARK_NODE_BLOCK_QUOTE, "quote");
    f->list = make(f->document, AMARK_NODE_LIST, "list");
    f->item = make(f->document, AMARK_NODE_ITEM, "item");
    f->para = make(f->document, AMARK_NODE_PARAGRAPH, "para");
    f->one = make(f->document, AMARK_NODE_TEXT, "one");
    f->emph = make(f->document, AMARK_NODE_EMPH, "emph");
    f->two = make(f->document, AMARK_NODE_TEXT, "two");
    f->sb = make(f->document, AMARK_NODE_SOFTBREAK, "sb");
    f->three = make(f->document, AMARK_NODE_TEXT, "three");
    f->four = make(f->document, AMARK_NODE_TEXT, "four");

    append(f->root, f->quote);
    append(f->quote, f->list);
    append(f->list, f->item);
    append(f->item, f->para);
    append(f->para, f->one);
    append(f->para, f->emph);
    append(f->emph, f->two);
    append(f->emph, f->sb);
    append(f->emph, f->three);
    append(f->para, f->four);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_string(f->one, 1, "one ", 4));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_string(f->two, 1, "two", 3));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_string(f->three, 1, "three", 5));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_string(f->four, 1, " four", 5));
}

static void fixture_deinit(fixture_t *f)
{
    check_tree_ok(f->document);
    amark_document_destroy(f->document);
}

/* ---- The events in document order (4.2) ------------------------------------------ */

NAOS_TEST(input_3_1_events_in_document_order)
{
    fixture_t f;
    amark_iter_t *iter = NULL;
    trace_t t;

    fixture_init(&f);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_iter_create(f.root, &iter));
    NAOS_REQUIRE_NOT_NULL(iter);
    /* No current node before the first event. */
    NAOS_CHECK_NULL(amark_iter_node(iter));

    trace_init(&t);
    NAOS_CHECK_INT_EQ(AMARK_EVENT_ENTER, step(iter, &t));
    NAOS_CHECK_PTR_EQ(f.root, amark_iter_node(iter));
    finish(iter, &t);
    NAOS_CHECK_STR_EQ(input_3_1_trace, t.text);

    /* Once over, the traversal stays over, on the root. */
    NAOS_CHECK_INT_EQ(AMARK_EVENT_DONE, amark_iter_next(iter));
    NAOS_CHECK_INT_EQ(AMARK_EVENT_DONE, amark_iter_next(iter));
    NAOS_CHECK_PTR_EQ(f.root, amark_iter_node(iter));
    amark_iter_destroy(iter);
    fixture_deinit(&f);
}

NAOS_TEST(a_container_without_children_has_enter_and_exit)
{
    fixture_t f;
    amark_node_t *empty_list;
    amark_node_t *empty_emph;
    trace_t t;

    fixture_init(&f);
    empty_list = make(f.document, AMARK_NODE_LIST, "elist");
    empty_emph = make(f.document, AMARK_NODE_EMPH, "eemph");
    append(f.root, empty_list);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_after(f.four, empty_emph));

    trace_subtree(f.root, &t);
    NAOS_CHECK_STR_EQ("+document +quote +list +item +para +one +emph +two +sb +three -emph "
                      "+four +eemph -eemph -para -item -list -quote +elist -elist -document",
                      t.text);

    /* The root of a document without children. */
    {
        amark_document_t *empty = NULL;

        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &empty));
        trace_subtree(amark_document_root(empty), &t);
        NAOS_CHECK_STR_EQ("+document -document", t.text);
        amark_document_destroy(empty);
    }
    fixture_deinit(&f);
}

NAOS_TEST(the_traversal_does_not_leave_its_root)
{
    fixture_t f;
    amark_node_t *detached;
    amark_node_t *detached_child;
    amark_iter_t *iter = NULL;
    trace_t t;

    fixture_init(&f);
    /* A subtree with siblings after it and an ancestor. */
    trace_subtree(f.emph, &t);
    NAOS_CHECK_STR_EQ("+emph +two +sb +three -emph", t.text);
    trace_subtree(f.item, &t);
    NAOS_CHECK_STR_EQ("+item +para +one +emph +two +sb +three -emph +four -para -item", t.text);

    /* A leaf root: only its enter event, then over on it. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_iter_create(f.two, &iter));
    trace_init(&t);
    finish(iter, &t);
    NAOS_CHECK_STR_EQ("+two", t.text);
    NAOS_CHECK_PTR_EQ(f.two, amark_iter_node(iter));
    NAOS_CHECK_INT_EQ(AMARK_EVENT_DONE, amark_iter_next(iter));
    amark_iter_destroy(iter);

    /* A detached subtree. */
    detached = make(f.document, AMARK_NODE_BLOCK_QUOTE, "dquote");
    detached_child = make(f.document, AMARK_NODE_PARAGRAPH, "dpara");
    append(detached, detached_child);
    trace_subtree(detached, &t);
    NAOS_CHECK_STR_EQ("+dquote +dpara -dpara -dquote", t.text);
    amark_node_destroy(detached);
    fixture_deinit(&f);
}

/* ---- Edits during a traversal (4.3) ---------------------------------------------- */

NAOS_TEST(changing_the_children_of_the_entered_node_visits_the_new_children)
{
    fixture_t f;
    amark_iter_t *iter = NULL;
    trace_t t;

    fixture_init(&f);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_iter_create(f.root, &iter));
    trace_init(&t);
    advance_to(iter, &t, f.para, AMARK_EVENT_ENTER);
    /* The children of the current node are replaced (the old ones are
     * destroyed): the new children are visited. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_inline_text(f.para, "a\nb", 3));
    finish(iter, &t);
    NAOS_CHECK_STR_EQ("+document +quote +list +item +para +text +softbreak +text -para -item "
                      "-list -quote -document",
                      t.text);
    amark_iter_destroy(iter);
    fixture_deinit(&f);
}

NAOS_TEST(removing_every_child_of_the_entered_node_goes_to_its_exit)
{
    fixture_t f;
    amark_iter_t *iter = NULL;
    trace_t t;

    fixture_init(&f);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_iter_create(f.root, &iter));
    trace_init(&t);
    advance_to(iter, &t, f.emph, AMARK_EVENT_ENTER);
    amark_node_destroy(f.two);
    amark_node_destroy(f.sb);
    amark_node_destroy(f.three);
    finish(iter, &t);
    NAOS_CHECK_STR_EQ("+document +quote +list +item +para +one +emph -emph +four -para -item "
                      "-list -quote -document",
                      t.text);
    amark_iter_destroy(iter);
    fixture_deinit(&f);
}

NAOS_TEST(adding_children_to_an_entered_node_without_children_visits_them)
{
    fixture_t f;
    amark_node_t *empty_emph;
    amark_iter_t *iter = NULL;
    trace_t t;

    fixture_init(&f);
    empty_emph = make(f.document, AMARK_NODE_EMPH, "eemph");
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_after(f.four, empty_emph));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_iter_create(f.para, &iter));
    trace_init(&t);
    advance_to(iter, &t, empty_emph, AMARK_EVENT_ENTER);
    /* Moves a node already visited (other than the current node and its
     * ancestors) into the current node: it is visited again there. */
    append(empty_emph, f.one);
    append(empty_emph, make(f.document, AMARK_NODE_TEXT, "new"));
    finish(iter, &t);
    NAOS_CHECK_STR_EQ("+para +one +emph +two +sb +three -emph +four +eemph +one +new -eemph -para",
                      t.text);
    amark_iter_destroy(iter);
    fixture_deinit(&f);
}

NAOS_TEST(changing_the_content_of_the_current_node_continues_the_traversal)
{
    fixture_t f;
    amark_iter_t *iter = NULL;
    amark_value_t value;
    trace_t t;

    fixture_init(&f);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_iter_create(f.root, &iter));
    trace_init(&t);
    advance_to(iter, &t, f.two, AMARK_EVENT_ENTER);
    /* The value of the current leaf. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_string(f.two, 1, "deux", 4));
    advance_to(iter, &t, f.para, AMARK_EVENT_EXIT);
    /* The value of the current node at its exit event. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_string(f.one, 1, "un ", 3));
    finish(iter, &t);
    NAOS_CHECK_STR_EQ(input_3_1_trace, t.text);
    memset(&value, 0, sizeof(value));
    value.struct_size = sizeof(value);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_get(f.two, 1, &value));
    NAOS_CHECK_STR_EQ("deux", value.text);
    amark_iter_destroy(iter);
    fixture_deinit(&f);
}

NAOS_TEST(editing_other_nodes_visits_the_unvisited_part_in_document_order)
{
    fixture_t f;
    amark_iter_t *iter = NULL;
    trace_t t;

    fixture_init(&f);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_iter_create(f.root, &iter));
    trace_init(&t);
    advance_to(iter, &t, f.sb, AMARK_EVENT_ENTER);
    /* Before the current node (already visited): not visited. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_insert_before(f.sb, make(f.document, AMARK_NODE_TEXT, "before")));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_prepend_child(f.para, make(f.document, AMARK_NODE_TEXT, "first")));
    /* After the current node and after an ancestor: visited. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_insert_after(f.sb, make(f.document, AMARK_NODE_TEXT, "after")));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_insert_after(f.item, make(f.document, AMARK_NODE_ITEM, "item2")));
    append(f.root, make(f.document, AMARK_NODE_PARAGRAPH, "last"));
    /* An unvisited node removed: not visited. */
    amark_node_destroy(f.three);
    /* An unvisited node moved before the current position: not visited. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_after(f.one, f.four));
    finish(iter, &t);
    NAOS_CHECK_STR_EQ("+document +quote +list +item +para +one +emph +two +sb +after -emph -para "
                      "-item +item2 -item2 -list -quote +last -last -document",
                      t.text);

    /* The changed tree, traversed again. */
    trace_subtree(f.root, &t);
    NAOS_CHECK_STR_EQ("+document +quote +list +item +para +first +one +four +emph +two +before +sb "
                      "+after -emph -para -item +item2 -item2 -list -quote +last -last -document",
                      t.text);
    amark_iter_destroy(iter);
    fixture_deinit(&f);
}

NAOS_TEST(changing_the_children_of_the_node_at_its_exit_does_not_revisit)
{
    fixture_t f;
    amark_iter_t *iter = NULL;
    trace_t t;

    fixture_init(&f);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_iter_create(f.root, &iter));
    trace_init(&t);
    advance_to(iter, &t, f.emph, AMARK_EVENT_EXIT);
    /* The children of the current node at its exit event are visited
     * already. */
    append(f.emph, make(f.document, AMARK_NODE_TEXT, "late"));
    amark_node_destroy(f.two);
    finish(iter, &t);
    NAOS_CHECK_STR_EQ(input_3_1_trace, t.text);
    amark_iter_destroy(iter);
    fixture_deinit(&f);
}

/* ---- A deep tree (4.2) ------------------------------------------------------------- */

#define DEEP_LEVELS 100000u

NAOS_TEST(a_deep_tree_is_traversed_without_recursion)
{
    amark_document_t *document = NULL;
    amark_node_t *top;
    amark_node_t *leaf;
    const amark_node_t *last = NULL;
    amark_iter_t *iter = NULL;
    amark_event_t event;
    size_t enters = 0;
    size_t exits = 0;
    size_t depth = 0;
    size_t max_depth = 0;
    size_t i;
    bool ordered = true;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    /* Built from the bottom up: each insertion walks the ancestors of its
     * parent, which a detached new quote does not have. */
    leaf = NULL;
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, AMARK_NODE_PARAGRAPH, &leaf));
    top = leaf;
    for (i = 0; i < DEEP_LEVELS; i++)
    {
        amark_node_t *quote = NULL;

        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                            amark_node_create(document, AMARK_NODE_BLOCK_QUOTE, &quote));
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(quote, top));
        top = quote;
    }
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_append_child(amark_document_root(document), top));

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_iter_create(amark_document_root(document), &iter));
    while ((event = amark_iter_next(iter)) != AMARK_EVENT_DONE)
    {
        const amark_node_t *node = amark_iter_node(iter);

        if (event == AMARK_EVENT_ENTER)
        {
            /* Each enter event goes one level down, into the node entered
             * last. */
            ordered = ordered && amark_node_parent(node) == last;
            last = node;
            enters++;
            depth++;
            if (depth > max_depth)
            {
                max_depth = depth;
            }
        }
        else
        {
            /* Each exit event goes one level up, from the deepest node. */
            ordered = ordered && node == last;
            last = amark_node_parent(node);
            exits++;
            depth--;
        }
        if (node == leaf && event == AMARK_EVENT_ENTER)
        {
            /* The paragraph is the deepest node, entered after every quote. */
            ordered = ordered && enters == DEEP_LEVELS + 2u && exits == 0;
        }
    }
    NAOS_CHECK_TRUE(ordered);
    /* The document, the quotes and the paragraph are all entered and exited
     * (a paragraph can have inline children). */
    NAOS_CHECK_UINT_EQ(DEEP_LEVELS + 2u, enters);
    NAOS_CHECK_UINT_EQ(DEEP_LEVELS + 2u, exits);
    NAOS_CHECK_UINT_EQ(DEEP_LEVELS + 2u, max_depth);
    NAOS_CHECK_UINT_EQ(0, depth);
    amark_iter_destroy(iter);
    amark_document_destroy(document);
}

/* ---- Reset (4.4) --------------------------------------------------------------------- */

NAOS_TEST(reset_continues_from_the_given_position)
{
    fixture_t f;
    amark_iter_t *iter = NULL;
    trace_t t;

    fixture_init(&f);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_iter_create(f.root, &iter));

    /* The enter event of a node: its children come next. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_iter_reset(iter, f.emph, AMARK_EVENT_ENTER));
    NAOS_CHECK_PTR_EQ(f.emph, amark_iter_node(iter));
    trace_init(&t);
    finish(iter, &t);
    NAOS_CHECK_STR_EQ("+two +sb +three -emph +four -para -item -list -quote -document", t.text);

    /* Backward, after the traversal is over: the exit event of a node. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_iter_reset(iter, f.emph, AMARK_EVENT_EXIT));
    NAOS_CHECK_PTR_EQ(f.emph, amark_iter_node(iter));
    trace_init(&t);
    finish(iter, &t);
    NAOS_CHECK_STR_EQ("+four -para -item -list -quote -document", t.text);

    /* The enter event of a leaf. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_iter_reset(iter, f.sb, AMARK_EVENT_ENTER));
    trace_init(&t);
    NAOS_CHECK_INT_EQ(AMARK_EVENT_ENTER, step(iter, &t));
    NAOS_CHECK_PTR_EQ(f.three, amark_iter_node(iter));

    /* The root: its enter event starts over, its exit event ends. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_iter_reset(iter, f.root, AMARK_EVENT_ENTER));
    trace_init(&t);
    finish(iter, &t);
    NAOS_CHECK_STR_EQ("+quote +list +item +para +one +emph +two +sb +three -emph +four -para -item "
                      "-list -quote -document",
                      t.text);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_iter_reset(iter, f.root, AMARK_EVENT_EXIT));
    NAOS_CHECK_INT_EQ(AMARK_EVENT_DONE, amark_iter_next(iter));
    NAOS_CHECK_PTR_EQ(f.root, amark_iter_node(iter));
    amark_iter_destroy(iter);

    /* Within a subtree root. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_iter_create(f.para, &iter));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_iter_reset(iter, f.three, AMARK_EVENT_ENTER));
    trace_init(&t);
    finish(iter, &t);
    NAOS_CHECK_STR_EQ("-emph +four -para", t.text);
    amark_iter_destroy(iter);
    fixture_deinit(&f);
}

NAOS_TEST(reset_rejects_positions_outside_the_subtree)
{
    fixture_t f;
    amark_document_t *other = NULL;
    amark_node_t *detached;
    amark_iter_t *iter = NULL;
    trace_t t;

    fixture_init(&f);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &other));
    detached = make(f.document, AMARK_NODE_TEXT, "detached");
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_iter_create(f.emph, &iter));
    trace_init(&t);
    NAOS_CHECK_INT_EQ(AMARK_EVENT_ENTER, step(iter, &t));
    NAOS_CHECK_INT_EQ(AMARK_EVENT_ENTER, step(iter, &t));
    NAOS_REQUIRE_PTR_EQ(f.two, amark_iter_node(iter));

    /* Each refusal leaves the position unchanged. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_iter_reset(NULL, f.emph, AMARK_EVENT_ENTER));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_iter_reset(iter, NULL, AMARK_EVENT_ENTER));
    /* Outside the subtree of the root: an ancestor, a sibling, a cousin, a
     * detached node and the root of another document. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_iter_reset(iter, f.para, AMARK_EVENT_ENTER));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_iter_reset(iter, f.four, AMARK_EVENT_ENTER));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_iter_reset(iter, f.root, AMARK_EVENT_EXIT));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_iter_reset(iter, detached, AMARK_EVENT_ENTER));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_iter_reset(iter, amark_document_root(other), AMARK_EVENT_ENTER));
    /* An exit event of a node whose type cannot have children, and events
     * that are not positions. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_iter_reset(iter, f.three, AMARK_EVENT_EXIT));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_iter_reset(iter, f.emph, AMARK_EVENT_DONE));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_iter_reset(iter, f.emph, (amark_event_t)0));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_iter_reset(iter, f.emph, (amark_event_t)99));

    NAOS_CHECK_PTR_EQ(f.two, amark_iter_node(iter));
    finish(iter, &t);
    NAOS_CHECK_STR_EQ("+emph +two +sb +three -emph", t.text);
    amark_iter_destroy(iter);
    amark_node_destroy(detached);
    amark_document_destroy(other);
    fixture_deinit(&f);
}

/* ---- Invalid arguments and allocation failure ----------------------------------- */

static amark_test_allocator_t test_allocator;

NAOS_TEST(invalid_arguments_and_allocation_failure)
{
    fixture_t f;
    amark_iter_t *iter = (amark_iter_t *)&f;
    amark_error_t error;

    fixture_init(&f);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_iter_create(NULL, &iter));
    NAOS_CHECK_NULL(iter);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_iter_create(f.root, NULL));
    NAOS_CHECK_INT_EQ(AMARK_EVENT_DONE, amark_iter_next(NULL));
    NAOS_CHECK_NULL(amark_iter_node(NULL));
    amark_iter_destroy(NULL);

    amark_test_allocator_init(&test_allocator);
    test_allocator.fail_at = 1;
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_allocator_set(amark_test_allocator_activate(&test_allocator)));
    iter = (amark_iter_t *)&f;
    error = amark_iter_create(f.root, &iter);
    amark_allocator_set(NULL);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_OUT_OF_MEMORY, error);
    NAOS_CHECK_NULL(iter);
    NAOS_CHECK_UINT_EQ(1, test_allocator.failures);
    NAOS_CHECK_UINT_EQ(0, test_allocator.live_blocks);
    fixture_deinit(&f);
}

NAOS_SUITE(iterator,
    NAOS_ADD_TEST(input_3_1_events_in_document_order),
    NAOS_ADD_TEST(a_container_without_children_has_enter_and_exit),
    NAOS_ADD_TEST(the_traversal_does_not_leave_its_root),
    NAOS_ADD_TEST(changing_the_children_of_the_entered_node_visits_the_new_children),
    NAOS_ADD_TEST(removing_every_child_of_the_entered_node_goes_to_its_exit),
    NAOS_ADD_TEST(adding_children_to_an_entered_node_without_children_visits_them),
    NAOS_ADD_TEST(changing_the_content_of_the_current_node_continues_the_traversal),
    NAOS_ADD_TEST(editing_other_nodes_visits_the_unvisited_part_in_document_order),
    NAOS_ADD_TEST(changing_the_children_of_the_node_at_its_exit_does_not_revisit),
    NAOS_ADD_TEST(a_deep_tree_is_traversed_without_recursion),
    NAOS_ADD_TEST(reset_continues_from_the_given_position),
    NAOS_ADD_TEST(reset_rejects_positions_outside_the_subtree),
    NAOS_ADD_TEST(invalid_arguments_and_allocation_failure)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(iterator)
)
