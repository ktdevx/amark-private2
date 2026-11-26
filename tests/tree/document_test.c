/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The tests of Document and of the life of nodes (syntax-tree): the creation of
 * documents with and without a syntax set, the transfer of the ownership of the
 * syntax set, the creation of nodes, the relations, the user pointers, the
 * extension data and the init and deinit functions of the types, the release of
 * nodes and of subtrees, and the allocation failures (1.1-1.3, 1.6-1.9, 4.1,
 * 4.5, 14.4).
 *
 * The structural editing is not part of this component: the tests that need a
 * tree link the nodes through the internal fields (link_child).
 */
#include "tree/tree.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <naos/naos.h>

#include "test_allocator.h"

/* ---- Extension types used by the tests ------------------------------------ */

/* The calls of the init and deinit functions of the test types. */
static size_t init_calls;
static size_t deinit_calls;
/* When not 0, init fails with this error. */
static amark_error_t init_failure;
/* The data init received, and whether it was zero-filled. */
static void *init_data;
static int init_data_was_zero;

typedef struct box_data
{
    uint32_t magic;
    unsigned char bytes[28];
} box_data_t;

#define BOX_MAGIC 0x426f7821u

static amark_error_t box_init(amark_node_t *node, void *data)
{
    box_data_t *box = (box_data_t *)data;
    static const box_data_t zero;

    (void)node;
    init_calls++;
    init_data = data;
    init_data_was_zero = memcmp(box, &zero, sizeof(zero)) == 0;
    if (init_failure != AMARK_ERROR_NONE)
    {
        return init_failure;
    }
    box->magic = BOX_MAGIC;
    return AMARK_ERROR_NONE;
}

static void box_deinit(amark_node_t *node, void *data)
{
    (void)node;
    if (data != NULL && ((box_data_t *)data)->magic == BOX_MAGIC)
    {
        deinit_calls++;
    }
}

static const char *const align_names[] = {"left", "center", "right"};

static const amark_facet_desc_t box_facets[] = {
    {sizeof(amark_facet_desc_t), "title", AMARK_FACET_VALUE, AMARK_FACET_STRING, 0, 0, 0, NULL,
     0, 0, "untitled", NULL, NULL, NULL},
    {sizeof(amark_facet_desc_t), "width", AMARK_FACET_VALUE, AMARK_FACET_INT, 0, 1, 100, NULL, 0,
     10, NULL, NULL, NULL, NULL},
    {sizeof(amark_facet_desc_t), "align", AMARK_FACET_STYLE, AMARK_FACET_ENUM, 0, 0, 0,
     align_names, 3, 0, NULL, NULL, NULL, NULL},
};

/* A container block with facets, extension data and init and deinit. */
static const amark_node_type_desc_t box_type = {
    sizeof(amark_node_type_desc_t), "test.box", AMARK_NODE_LEVEL_BLOCK, AMARK_CONTENT_BLOCK,
    AMARK_CONTENT_BLOCK, AMARK_NODE_TYPE_MARGINS, box_facets, 3, sizeof(box_data_t),
    NULL, box_init, box_deinit, NULL, NULL, NULL, NULL};

/* A block without data that has deinit only (it receives NULL data). */
static size_t plain_deinit_calls;

static void plain_deinit(amark_node_t *node, void *data)
{
    (void)node;
    if (data == NULL)
    {
        plain_deinit_calls++;
    }
}

static const amark_node_type_desc_t plain_type = {
    sizeof(amark_node_type_desc_t), "test.plain", AMARK_NODE_LEVEL_BLOCK, AMARK_CONTENT_BLOCK,
    0, 0, NULL, 0, 0, NULL, NULL, plain_deinit, NULL, NULL, NULL, NULL};

static const amark_node_type_desc_t *const test_types[] = {&box_type, &plain_type};
static const amark_extension_t test_extension = {
    sizeof(amark_extension_t), "test.ext", test_types, 2, NULL, 0, NULL};

#define BOX_TYPE AMARK_NODE_DYNAMIC_FIRST
#define PLAIN_TYPE (AMARK_NODE_DYNAMIC_FIRST + 1u)

static void reset_hooks(void)
{
    init_calls = 0;
    deinit_calls = 0;
    init_failure = AMARK_ERROR_NONE;
    init_data = NULL;
    init_data_was_zero = 0;
    plain_deinit_calls = 0;
}

/* Creates a frozen set with test_extension, or returns NULL. */
static amark_syntax_set_t *new_test_set(void)
{
    amark_syntax_set_t *set = NULL;

    if (amark_syntax_set_create(&set) != AMARK_ERROR_NONE)
    {
        return NULL;
    }
    if (amark_syntax_set_append(set, &test_extension) != AMARK_ERROR_NONE ||
        amark_syntax_set_freeze(set) != AMARK_ERROR_NONE)
    {
        amark_syntax_set_destroy(set);
        return NULL;
    }
    return set;
}

/* Links child as the last child of parent (the structural editing is task 2.4;
 * these tests only need trees to free). */
static void link_child(amark_node_t *parent, amark_node_t *child)
{
    child->parent = parent;
    child->prev = parent->last_child;
    child->next = NULL;
    if (parent->last_child != NULL)
    {
        parent->last_child->next = child;
    }
    else
    {
        parent->first_child = child;
    }
    parent->last_child = child;
}

static amark_node_t *new_node(amark_document_t *document, amark_node_type_t type)
{
    amark_node_t *node = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, type, &node));
    NAOS_REQUIRE_NOT_NULL(node);
    return node;
}

/* The nodes of the ring of document, counted from the root. */
static size_t ring_size(const amark_document_t *document)
{
    const amark_node_t *node = document->root;
    size_t count = 0;

    do
    {
        count++;
        node = node->ring_next;
    } while (node != document->root && count < (size_t)1 << 30);
    return count;
}

/* Checks that document keeps the internal invariants (amark_tree_check). */
static void check_tree_ok(amark_document_t *document)
{
    amark_tree_check_report_t report;

    NAOS_CHECK_STR_EQ("none", amark_tree_violation_name(amark_tree_check(document, &report)));
    NAOS_CHECK_NULL(report.node);
}

static amark_test_allocator_t test_allocator;

static void use_test_allocator(void)
{
    amark_test_allocator_init(&test_allocator);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_allocator_set(amark_test_allocator_activate(&test_allocator)));
}

/* ---- Creating documents (1.1, 1.2) ----------------------------------------- */

NAOS_TEST(create_without_a_set_uses_the_builtin_set)
{
    amark_document_t *document = NULL;
    amark_node_t *root;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    NAOS_REQUIRE_NOT_NULL(document);
    NAOS_CHECK_PTR_EQ(amark_syntax_set_core(), amark_document_syntax_set(document));

    root = amark_document_root(document);
    NAOS_REQUIRE_NOT_NULL(root);
    NAOS_CHECK_UINT_EQ(AMARK_NODE_DOCUMENT, amark_node_get_type(root));
    NAOS_CHECK_PTR_EQ(amark_syntax_set_node_type_desc(amark_syntax_set_core(),
                                                      AMARK_NODE_DOCUMENT),
                      amark_node_type_desc(root));
    NAOS_CHECK_PTR_EQ(document, amark_node_document(root));
    NAOS_CHECK_NULL(amark_node_parent(root));
    NAOS_CHECK_NULL(amark_node_first_child(root));
    NAOS_CHECK_NULL(amark_node_last_child(root));
    NAOS_CHECK_NULL(amark_node_next(root));
    NAOS_CHECK_NULL(amark_node_previous(root));
    NAOS_CHECK_UINT_EQ(1, document->node_count);
    NAOS_CHECK_UINT_EQ(1, ring_size(document));

    amark_document_destroy(document);
}

NAOS_TEST(create_with_a_frozen_set_uses_it)
{
    amark_syntax_set_t *set = new_test_set();
    amark_document_t *document = NULL;

    NAOS_REQUIRE_NOT_NULL(set);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(set, &document));
    NAOS_CHECK_PTR_EQ(set, amark_document_syntax_set(document));
    NAOS_CHECK_UINT_EQ(AMARK_NODE_DOCUMENT, amark_node_get_type(amark_document_root(document)));

    /* The set is borrowed: it outlives the document. */
    amark_document_destroy(document);
    NAOS_CHECK_TRUE(amark_syntax_set_is_frozen(set));
    amark_syntax_set_destroy(set);
}

NAOS_TEST(create_rejects_a_set_that_is_not_frozen)
{
    amark_syntax_set_t *set = NULL;
    amark_document_t *document = (amark_document_t *)&set;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_create(&set));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_STATE, amark_document_create(set, &document));
    NAOS_CHECK_NULL(document);
    amark_syntax_set_destroy(set);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_document_create(NULL, NULL));
}

NAOS_TEST(create_and_destroy_combinations_leave_no_block)
{
    amark_syntax_set_t *set;
    amark_document_t *first = NULL;
    amark_document_t *second = NULL;
    amark_node_t *node = NULL;
    size_t live;

    use_test_allocator();
    reset_hooks();
    set = new_test_set();
    /* Two documents share one frozen set; one of them takes it. */
    (void)amark_document_create(set, &first);
    (void)amark_document_create(set, &second);
    (void)amark_node_create(first, BOX_TYPE, &node);
    (void)amark_node_create(second, PLAIN_TYPE, &node);
    amark_document_destroy(first);
    (void)amark_document_take_syntax_set(second, set);
    amark_document_destroy(second);
    /* Destroying NULL does nothing. */
    amark_document_destroy(NULL);
    live = test_allocator.live_blocks;
    amark_allocator_set(NULL);

    NAOS_CHECK_NOT_NULL(set);
    NAOS_CHECK_NOT_NULL(node);
    NAOS_CHECK_UINT_EQ(0, live);
    NAOS_CHECK_UINT_EQ(1, deinit_calls);
    NAOS_CHECK_UINT_EQ(1, plain_deinit_calls);
}

/* ---- The ownership of the syntax set (1.8, 1.9) ---------------------------- */

NAOS_TEST(take_syntax_set_frees_it_after_the_nodes)
{
    amark_syntax_set_t *set;
    amark_document_t *document = NULL;
    amark_node_t *node = NULL;
    amark_error_t error;
    size_t live;

    use_test_allocator();
    reset_hooks();
    set = new_test_set();
    (void)amark_document_create(set, &document);
    (void)amark_node_create(document, BOX_TYPE, &node);
    error = amark_document_take_syntax_set(document, set);
    /* The nodes are freed first: deinit still finds its type in the owned set. */
    amark_document_destroy(document);
    live = test_allocator.live_blocks;
    amark_allocator_set(NULL);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, error);
    NAOS_CHECK_UINT_EQ(1, deinit_calls);
    NAOS_CHECK_UINT_EQ(0, live);
}

NAOS_TEST(take_syntax_set_violations_change_nothing)
{
    amark_syntax_set_t *set = new_test_set();
    amark_syntax_set_t *other = new_test_set();
    amark_document_t *document = NULL;
    amark_document_t *core_document = NULL;

    NAOS_REQUIRE_NOT_NULL(set);
    NAOS_REQUIRE_NOT_NULL(other);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(set, &document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &core_document));

    /* NULL arguments. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_document_take_syntax_set(NULL, set));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_document_take_syntax_set(document, NULL));
    /* A set the document does not use. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_document_take_syntax_set(document, other));
    NAOS_CHECK_NULL(document->owned_set);
    /* The static built-in set, even when the document uses it. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_document_take_syntax_set(
                          core_document, (amark_syntax_set_t *)amark_syntax_set_core()));
    NAOS_CHECK_NULL(core_document->owned_set);
    NAOS_CHECK_PTR_EQ(amark_syntax_set_core(), amark_document_syntax_set(core_document));

    /* The document already owns its set. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_take_syntax_set(document, set));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_document_take_syntax_set(document, set));
    NAOS_CHECK_PTR_EQ(set, document->owned_set);
    NAOS_CHECK_PTR_EQ(set, amark_document_syntax_set(document));

    /* other stayed with the caller. */
    NAOS_CHECK_TRUE(amark_syntax_set_is_frozen(other));
    amark_syntax_set_destroy(other);
    amark_document_destroy(core_document);
    amark_document_destroy(document);
}

/* ---- Creating nodes (1.1, 4.1, 4.5) ---------------------------------------- */

NAOS_TEST(node_create_makes_a_detached_node_with_zero_facets)
{
    amark_syntax_set_t *set = new_test_set();
    amark_document_t *document = NULL;
    amark_node_t *node;
    static const amark_tree_facet_t zero_facet;
    size_t i;

    NAOS_REQUIRE_NOT_NULL(set);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(set, &document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_take_syntax_set(document, set));
    reset_hooks();

    node = new_node(document, BOX_TYPE);
    NAOS_CHECK_UINT_EQ(BOX_TYPE, amark_node_get_type(node));
    NAOS_CHECK_PTR_EQ(&box_type, amark_node_type_desc(node));
    NAOS_CHECK_PTR_EQ(document, amark_node_document(node));
    NAOS_CHECK_NULL(amark_node_parent(node));
    NAOS_CHECK_NULL(amark_node_first_child(node));
    NAOS_CHECK_NULL(amark_node_last_child(node));
    NAOS_CHECK_NULL(amark_node_next(node));
    NAOS_CHECK_NULL(amark_node_previous(node));
    NAOS_CHECK_NULL(amark_node_user_data(node));
    /* The facet storage is zero-filled; the defaults are interpreted elsewhere. */
    NAOS_REQUIRE_NOT_NULL(node->facets);
    for (i = 0; i < box_type.facet_count; i++)
    {
        NAOS_CHECK_INT_EQ(0, memcmp(&node->facets[i], &zero_facet, sizeof(zero_facet)));
    }
    /* The extension data is zero-filled and given to init. */
    NAOS_CHECK_UINT_EQ(1, init_calls);
    NAOS_CHECK_TRUE(init_data_was_zero);
    NAOS_REQUIRE_NOT_NULL(amark_node_data(node));
    NAOS_CHECK_PTR_EQ(init_data, amark_node_data(node));
    NAOS_CHECK_UINT_EQ(BOX_MAGIC, ((box_data_t *)amark_node_data(node))->magic);

    /* Built-in types: no facet storage when the type has none, no data. */
    node = new_node(document, AMARK_NODE_PARAGRAPH);
    NAOS_CHECK_NULL(node->facets);
    NAOS_CHECK_NULL(amark_node_data(node));
    node = new_node(document, AMARK_NODE_LIST);
    NAOS_REQUIRE_NOT_NULL(node->facets);
    for (i = 0; i < 5; i++)
    {
        NAOS_CHECK_INT_EQ(0, memcmp(&node->facets[i], &zero_facet, sizeof(zero_facet)));
    }

    NAOS_CHECK_UINT_EQ(4, document->node_count);
    NAOS_CHECK_UINT_EQ(4, ring_size(document));
    check_tree_ok(document);
    amark_document_destroy(document);
    NAOS_CHECK_UINT_EQ(1, deinit_calls);
}

NAOS_TEST(node_create_rejects_invalid_arguments)
{
    amark_document_t *document = NULL;
    amark_node_t *node = (amark_node_t *)&document;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_create(NULL, AMARK_NODE_PARAGRAPH, &node));
    NAOS_CHECK_NULL(node);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_create(document, AMARK_NODE_PARAGRAPH, NULL));
    node = (amark_node_t *)&document;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_create(document, AMARK_NODE_NONE, &node));
    NAOS_CHECK_NULL(node);
    node = (amark_node_t *)&document;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_create(document, 20u, &node));
    NAOS_CHECK_NULL(node);
    /* A type of an extension that the set of the document does not hold. */
    node = (amark_node_t *)&document;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_create(document, BOX_TYPE, &node));
    NAOS_CHECK_NULL(node);
    NAOS_CHECK_UINT_EQ(1, document->node_count);
    amark_document_destroy(document);
}

NAOS_TEST(init_failure_leaves_the_document_unchanged)
{
    amark_syntax_set_t *set;
    amark_document_t *document = NULL;
    amark_node_t *node = (amark_node_t *)&set;
    amark_error_t error;
    size_t live_before;
    size_t live_after;
    size_t count;

    use_test_allocator();
    reset_hooks();
    set = new_test_set();
    (void)amark_document_create(set, &document);
    live_before = test_allocator.live_blocks;
    init_failure = AMARK_ERROR_LIMIT_EXCEEDED;
    error = amark_node_create(document, BOX_TYPE, &node);
    live_after = test_allocator.live_blocks;
    count = document != NULL ? document->node_count : 0;
    amark_document_destroy(document);
    amark_syntax_set_destroy(set);
    amark_allocator_set(NULL);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_LIMIT_EXCEEDED, error);
    NAOS_CHECK_NULL(node);
    NAOS_CHECK_UINT_EQ(live_before, live_after);
    NAOS_CHECK_UINT_EQ(1, count);
    /* The node that failed to initialize is not deinitialized. */
    NAOS_CHECK_UINT_EQ(1, init_calls);
    NAOS_CHECK_UINT_EQ(0, deinit_calls);
}

/* ---- Relations, user pointers, classification (4.1, 4.5) ------------------- */

NAOS_TEST(relations_follow_the_links)
{
    amark_document_t *document = NULL;
    amark_node_t *root;
    amark_node_t *quote;
    amark_node_t *first;
    amark_node_t *second;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    root = amark_document_root(document);
    quote = new_node(document, AMARK_NODE_BLOCK_QUOTE);
    first = new_node(document, AMARK_NODE_PARAGRAPH);
    second = new_node(document, AMARK_NODE_PARAGRAPH);
    link_child(root, quote);
    link_child(quote, first);
    link_child(quote, second);

    NAOS_CHECK_PTR_EQ(root, amark_node_parent(quote));
    NAOS_CHECK_PTR_EQ(quote, amark_node_first_child(root));
    NAOS_CHECK_PTR_EQ(quote, amark_node_last_child(root));
    NAOS_CHECK_PTR_EQ(first, amark_node_first_child(quote));
    NAOS_CHECK_PTR_EQ(second, amark_node_last_child(quote));
    NAOS_CHECK_PTR_EQ(second, amark_node_next(first));
    NAOS_CHECK_PTR_EQ(first, amark_node_previous(second));
    NAOS_CHECK_NULL(amark_node_next(second));
    NAOS_CHECK_NULL(amark_node_previous(first));
    NAOS_CHECK_PTR_EQ(quote, amark_node_parent(second));

    check_tree_ok(document);
    amark_document_destroy(document);
}

NAOS_TEST(queries_on_null_return_none)
{
    NAOS_CHECK_NULL(amark_document_root(NULL));
    NAOS_CHECK_NULL(amark_document_syntax_set(NULL));
    NAOS_CHECK_UINT_EQ(AMARK_NODE_NONE, amark_node_get_type(NULL));
    NAOS_CHECK_NULL(amark_node_type_desc(NULL));
    NAOS_CHECK_NULL(amark_node_document(NULL));
    NAOS_CHECK_NULL(amark_node_parent(NULL));
    NAOS_CHECK_NULL(amark_node_first_child(NULL));
    NAOS_CHECK_NULL(amark_node_last_child(NULL));
    NAOS_CHECK_NULL(amark_node_next(NULL));
    NAOS_CHECK_NULL(amark_node_previous(NULL));
    NAOS_CHECK_FALSE(amark_node_is_block(NULL));
    NAOS_CHECK_FALSE(amark_node_is_inline(NULL));
    NAOS_CHECK_FALSE(amark_node_is_container(NULL));
    NAOS_CHECK_FALSE(amark_node_has_content(NULL));
    NAOS_CHECK_FALSE(amark_node_can_contain(NULL, AMARK_NODE_PARAGRAPH));
    NAOS_CHECK_NULL(amark_node_user_data(NULL));
    NAOS_CHECK_NULL(amark_node_data(NULL));
    amark_node_set_user_data(NULL, NULL);
    amark_node_destroy(NULL);
}

NAOS_TEST(user_data_is_kept)
{
    amark_document_t *document = NULL;
    amark_node_t *node;
    int value = 7;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    node = new_node(document, AMARK_NODE_TEXT);
    amark_node_set_user_data(node, &value);
    NAOS_CHECK_PTR_EQ(&value, amark_node_user_data(node));
    amark_node_set_user_data(node, NULL);
    NAOS_CHECK_NULL(amark_node_user_data(node));
    amark_node_set_user_data(amark_document_root(document), &value);
    NAOS_CHECK_PTR_EQ(&value, amark_node_user_data(amark_document_root(document)));
    amark_document_destroy(document);
}

NAOS_TEST(classification_follows_the_descriptor)
{
    amark_syntax_set_t *set = new_test_set();
    amark_document_t *document = NULL;
    amark_node_t *quote;
    amark_node_t *paragraph;
    amark_node_t *text;
    amark_node_t *list;
    amark_node_t *box;
    amark_node_t *link;

    NAOS_REQUIRE_NOT_NULL(set);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(set, &document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_take_syntax_set(document, set));
    quote = new_node(document, AMARK_NODE_BLOCK_QUOTE);
    paragraph = new_node(document, AMARK_NODE_PARAGRAPH);
    text = new_node(document, AMARK_NODE_TEXT);
    list = new_node(document, AMARK_NODE_LIST);
    box = new_node(document, BOX_TYPE);
    link = new_node(document, AMARK_NODE_LINK);

    NAOS_CHECK_TRUE(amark_node_is_block(quote));
    NAOS_CHECK_FALSE(amark_node_is_inline(quote));
    NAOS_CHECK_TRUE(amark_node_is_container(quote));
    NAOS_CHECK_FALSE(amark_node_has_content(quote));

    NAOS_CHECK_TRUE(amark_node_is_block(paragraph));
    NAOS_CHECK_FALSE(amark_node_is_container(paragraph));
    NAOS_CHECK_TRUE(amark_node_has_content(paragraph));

    NAOS_CHECK_FALSE(amark_node_is_block(text));
    NAOS_CHECK_TRUE(amark_node_is_inline(text));
    NAOS_CHECK_FALSE(amark_node_is_container(text));
    NAOS_CHECK_FALSE(amark_node_has_content(text));

    /* document and list hold blocks but have no margins. */
    NAOS_CHECK_FALSE(amark_node_is_container(list));
    NAOS_CHECK_FALSE(amark_node_is_container(amark_document_root(document)));
    NAOS_CHECK_TRUE(amark_node_is_container(box));

    NAOS_CHECK_TRUE(amark_node_can_contain(amark_document_root(document), AMARK_NODE_PARAGRAPH));
    NAOS_CHECK_TRUE(amark_node_can_contain(amark_document_root(document), BOX_TYPE));
    NAOS_CHECK_FALSE(amark_node_can_contain(amark_document_root(document), AMARK_NODE_TEXT));
    NAOS_CHECK_FALSE(amark_node_can_contain(amark_document_root(document), AMARK_NODE_DOCUMENT));
    NAOS_CHECK_TRUE(amark_node_can_contain(list, AMARK_NODE_ITEM));
    NAOS_CHECK_FALSE(amark_node_can_contain(list, AMARK_NODE_PARAGRAPH));
    NAOS_CHECK_TRUE(amark_node_can_contain(paragraph, AMARK_NODE_TEXT));
    NAOS_CHECK_FALSE(amark_node_can_contain(text, AMARK_NODE_TEXT));
    NAOS_CHECK_FALSE(amark_node_can_contain(link, AMARK_NODE_LINK));
    NAOS_CHECK_FALSE(amark_node_can_contain(quote, 999u));

    amark_document_destroy(document);
}

/* ---- Releasing nodes (1.3, 1.6) -------------------------------------------- */

NAOS_TEST(document_destroy_frees_detached_nodes)
{
    amark_syntax_set_t *set;
    amark_document_t *document = NULL;
    amark_node_t *quote = NULL;
    amark_node_t *box = NULL;
    amark_node_t *paragraph = NULL;
    amark_node_t *plain = NULL;
    size_t live;

    use_test_allocator();
    reset_hooks();
    set = new_test_set();
    (void)amark_document_create(set, &document);
    (void)amark_document_take_syntax_set(document, set);
    /* A tree in the document, a detached subtree and a detached node. */
    (void)amark_node_create(document, AMARK_NODE_BLOCK_QUOTE, &quote);
    (void)amark_node_create(document, BOX_TYPE, &box);
    (void)amark_node_create(document, AMARK_NODE_PARAGRAPH, &paragraph);
    (void)amark_node_create(document, PLAIN_TYPE, &plain);
    if (document != NULL && quote != NULL && box != NULL && paragraph != NULL)
    {
        link_child(document->root, quote);
        link_child(box, paragraph);
    }
    amark_document_destroy(document);
    live = test_allocator.live_blocks;
    amark_allocator_set(NULL);

    NAOS_CHECK_NOT_NULL(plain);
    NAOS_CHECK_UINT_EQ(0, live);
    NAOS_CHECK_UINT_EQ(1, deinit_calls);
    NAOS_CHECK_UINT_EQ(1, plain_deinit_calls);
}

NAOS_TEST(node_destroy_frees_the_subtree_and_unlinks_it)
{
    amark_syntax_set_t *set = new_test_set();
    amark_document_t *document = NULL;
    amark_node_t *root;
    amark_node_t *first;
    amark_node_t *box;
    amark_node_t *last;
    amark_node_t *inner;

    NAOS_REQUIRE_NOT_NULL(set);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(set, &document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_take_syntax_set(document, set));
    reset_hooks();
    root = amark_document_root(document);
    first = new_node(document, AMARK_NODE_PARAGRAPH);
    box = new_node(document, BOX_TYPE);
    last = new_node(document, AMARK_NODE_PARAGRAPH);
    inner = new_node(document, BOX_TYPE);
    link_child(root, first);
    link_child(root, box);
    link_child(root, last);
    link_child(box, inner);
    link_child(inner, new_node(document, AMARK_NODE_PARAGRAPH));
    link_child(box, new_node(document, AMARK_NODE_PARAGRAPH));
    NAOS_CHECK_UINT_EQ(7, document->node_count);
    check_tree_ok(document);

    /* The middle child, with its 3 descendants: its siblings are relinked. */
    amark_node_destroy(box);
    NAOS_CHECK_UINT_EQ(3, document->node_count);
    NAOS_CHECK_UINT_EQ(3, ring_size(document));
    NAOS_CHECK_UINT_EQ(2, deinit_calls);
    NAOS_CHECK_PTR_EQ(last, amark_node_next(first));
    NAOS_CHECK_PTR_EQ(first, amark_node_previous(last));
    check_tree_ok(document);

    /* The first and the last child. */
    amark_node_destroy(first);
    NAOS_CHECK_PTR_EQ(last, amark_node_first_child(root));
    NAOS_CHECK_NULL(amark_node_previous(last));
    amark_node_destroy(last);
    NAOS_CHECK_NULL(amark_node_first_child(root));
    NAOS_CHECK_NULL(amark_node_last_child(root));
    NAOS_CHECK_UINT_EQ(1, document->node_count);

    /* A detached node. */
    amark_node_destroy(new_node(document, PLAIN_TYPE));
    NAOS_CHECK_UINT_EQ(1, plain_deinit_calls);
    NAOS_CHECK_UINT_EQ(1, document->node_count);

    /* The root is freed only with its document. */
    amark_node_destroy(root);
    NAOS_CHECK_PTR_EQ(root, amark_document_root(document));
    NAOS_CHECK_UINT_EQ(1, document->node_count);
    check_tree_ok(document);

    amark_document_destroy(document);
}

NAOS_TEST(deep_subtree_is_freed_without_recursion)
{
    amark_document_t *document = NULL;
    amark_node_t *top;
    amark_node_t *node;
    size_t depth;
    size_t steps;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    top = new_node(document, AMARK_NODE_BLOCK_QUOTE);
    link_child(amark_document_root(document), top);
    node = top;
    for (depth = 1; depth < 100000; depth++)
    {
        amark_node_t *child = new_node(document, AMARK_NODE_BLOCK_QUOTE);

        link_child(node, child);
        /* A sibling on every level, so that both directions are walked. */
        link_child(node, new_node(document, AMARK_NODE_THEMATIC_BREAK));
        node = child;
    }
    NAOS_CHECK_UINT_EQ(1 + 100000 + 99999, document->node_count);
    /* The checker walks the deep tree without recursion. */
    check_tree_ok(document);

    document->stats.release_steps = 0;
    amark_node_destroy(top);
    steps = document->stats.release_steps;
    NAOS_CHECK_UINT_EQ(1, document->node_count);
    NAOS_CHECK_NULL(amark_node_first_child(amark_document_root(document)));
    /* The walk is linear in the nodes of the subtree. */
    NAOS_CHECK_UINT_LE(steps, (size_t)3 * (100000 + 99999));
    NAOS_CHECK_UINT_GE(steps, (size_t)(100000 + 99999));

    /* The same tree, left to the document. */
    top = new_node(document, AMARK_NODE_BLOCK_QUOTE);
    link_child(amark_document_root(document), top);
    node = top;
    for (depth = 1; depth < 100000; depth++)
    {
        amark_node_t *child = new_node(document, AMARK_NODE_BLOCK_QUOTE);

        link_child(node, child);
        node = child;
    }
    check_tree_ok(document);
    amark_document_destroy(document);
}

/* ---- Statistics ------------------------------------------------------------ */

NAOS_TEST(statistics_count_the_nodes)
{
    amark_document_t *document = NULL;
    amark_node_t *node;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    NAOS_CHECK_UINT_EQ(1, document->stats.nodes_created);
    NAOS_CHECK_UINT_EQ(0, document->stats.nodes_released);
    node = new_node(document, AMARK_NODE_PARAGRAPH);
    link_child(node, new_node(document, AMARK_NODE_TEXT));
    NAOS_CHECK_UINT_EQ(3, document->stats.nodes_created);
    amark_node_destroy(node);
    NAOS_CHECK_UINT_EQ(2, document->stats.nodes_released);
    check_tree_ok(document);
    amark_document_destroy(document);
}

/* ---- Allocation failures (1.7) --------------------------------------------- */

/* Every allocation of the scenario fails in turn: each failure returns
 * AMARK_ERROR_OUT_OF_MEMORY, leaves the document as it was (the same nodes, the
 * same owned set, *out NULL) and leaks nothing. */
static amark_error_t oom_scenario(void *context)
{
    amark_syntax_set_t *set = NULL;
    amark_document_t *document = NULL;
    amark_node_t *box = NULL;
    amark_node_t *node = NULL;
    amark_error_t error;
    size_t count;

    (void)context;
    error = amark_syntax_set_create(&set);
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_syntax_set_append(set, &test_extension);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_syntax_set_freeze(set);
    }
    if (error == AMARK_ERROR_NONE)
    {
        document = (amark_document_t *)&set;
        error = amark_document_create(set, &document);
        if (error != AMARK_ERROR_NONE && document != NULL)
        {
            error = AMARK_ERROR_INVALID_STATE;
        }
    }
    if (error != AMARK_ERROR_NONE)
    {
        amark_syntax_set_destroy(set);
        return error;
    }
    /* From here the document owns the set. */
    error = amark_document_take_syntax_set(document, set);
    if (error != AMARK_ERROR_NONE)
    {
        amark_document_destroy(document);
        amark_syntax_set_destroy(set);
        return error;
    }
    count = document->node_count;
    box = (amark_node_t *)&count;
    error = amark_node_create(document, BOX_TYPE, &box);
    if (error != AMARK_ERROR_NONE && (box != NULL || document->node_count != count ||
                                      ring_size(document) != count))
    {
        error = AMARK_ERROR_INVALID_STATE;
    }
    if (error == AMARK_ERROR_NONE)
    {
        link_child(document->root, box);
        count = document->node_count;
        node = (amark_node_t *)&count;
        error = amark_node_create(document, AMARK_NODE_LIST, &node);
        if (error != AMARK_ERROR_NONE &&
            (node != NULL || document->node_count != count || box->first_child != NULL))
        {
            error = AMARK_ERROR_INVALID_STATE;
        }
    }
    if (error == AMARK_ERROR_NONE)
    {
        link_child(box, node);
        error = amark_node_create(document, AMARK_NODE_PARAGRAPH, &node);
    }
    if (error == AMARK_ERROR_NONE)
    {
        amark_node_destroy(box);
        if (document->node_count != 2)
        {
            error = AMARK_ERROR_INVALID_STATE;
        }
    }
    amark_document_destroy(document);
    return error;
}

NAOS_TEST(out_of_memory_leaves_the_document_unchanged)
{
    reset_hooks();
    AMARK_CHECK_OOM_SWEEP(oom_scenario, NULL);
    /* Every node that init accepted was deinitialized. */
    NAOS_CHECK_UINT_EQ(init_calls, deinit_calls);
}

NAOS_SETUP(reset)
{
    reset_hooks();
}

NAOS_TEARDOWN(restore_allocator)
{
    (void)amark_allocator_set(NULL);
}

NAOS_SUITE(document,
    NAOS_SET_SETUP(reset),
    NAOS_SET_TEARDOWN(restore_allocator),
    NAOS_ADD_TEST(create_without_a_set_uses_the_builtin_set),
    NAOS_ADD_TEST(create_with_a_frozen_set_uses_it),
    NAOS_ADD_TEST(create_rejects_a_set_that_is_not_frozen),
    NAOS_ADD_TEST(create_and_destroy_combinations_leave_no_block),
    NAOS_ADD_TEST(take_syntax_set_frees_it_after_the_nodes),
    NAOS_ADD_TEST(take_syntax_set_violations_change_nothing),
    NAOS_ADD_TEST(node_create_makes_a_detached_node_with_zero_facets),
    NAOS_ADD_TEST(node_create_rejects_invalid_arguments),
    NAOS_ADD_TEST(init_failure_leaves_the_document_unchanged),
    NAOS_ADD_TEST(relations_follow_the_links),
    NAOS_ADD_TEST(queries_on_null_return_none),
    NAOS_ADD_TEST(user_data_is_kept),
    NAOS_ADD_TEST(classification_follows_the_descriptor),
    NAOS_ADD_TEST(document_destroy_frees_detached_nodes),
    NAOS_ADD_TEST(node_destroy_frees_the_subtree_and_unlinks_it),
    NAOS_ADD_TEST(deep_subtree_is_freed_without_recursion),
    NAOS_ADD_TEST(statistics_count_the_nodes),
    NAOS_ADD_TEST(out_of_memory_leaves_the_document_unchanged)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(document)
)
