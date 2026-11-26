/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * Documents and the node core (src/tree/document.c, src/tree/node.c).
 *
 * Suite document (1.1, 1.2, 1.3, 1.9, 7.8, 8.7): a new document holds only its
 * root, uses amark_syntax_set_core() for a NULL syntax set or the frozen set it is
 * given, rejects a set that is not frozen, starts with edit tracking off and can
 * switch it; its destruction frees every node (the detached ones too, with their
 * literals, tokens and type data); two documents used in turn do not interfere;
 * and the out-of-memory sweep of the creation leaves nothing behind.
 *
 * Suite node (2.4, 3.1, 3.7, 4.1, 8.12): amark_node_create makes a detached node of
 * the document with no relation, rejects the document type, unknown types and
 * NULL arguments; the queries return "nothing" for NULL; the associated pointer;
 * the type data (NULL without data, aligned, zero-filled before init); the
 * function table of an extension type (init, its failure, deinit, can_contain);
 * amark_node_destroy_subtree frees a deep subtree without recursion; and the
 * out-of-memory sweep of the creation.
 *
 * Suite source (1.4, 1.5, 3.5): amark_document_add_source copies the bytes and
 * numbers the sources from 0, with the NULL and length 0 rules; appending keeps
 * the bytes at each offset and updates the line index, a CRLF split between two
 * chunks being one line ending; amark_document_position converts the offsets of
 * LF, CR and CRLF lines to lines and columns and rejects the out of range ones;
 * the destruction of the document frees the sources; and the out-of-memory sweeps
 * of the addition and the appending leave the document unchanged.
 *
 * Suite ownership (1.3, 1.10, 1.11): amark_document_take_syntax_set takes the heap
 * set the document uses, which the destruction of the document then frees after
 * every node (a deinit that reads the set still finds it, under the sanitizers
 * too), leaving no block; the static core set, a set the document does not use, a
 * second hand-over and NULL arguments are rejected, the document is unchanged and
 * the caller still destroys its set once.
 */
#include "tree/syntax_set.h"
#include "tree/tree.h"

#include <amark/allocator.h>
#include <amark/document.h>
#include <amark/error.h>
#include <amark/extension.h>
#include <amark/node.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <naos/naos.h>

#include "test_allocator.h"
#include "test_check.h"

/* The test allocator, set by the tests that count allocations; static so that no
 * dangling pointer stays set if a test stops early. */
static amark_test_allocator_t counting;

NAOS_TEARDOWN(restore)
{
    amark_allocator_set(NULL);
}

/* Sets the counting test allocator and stops the test if that fails (it is then
 * not set). While it is set, the tests use no NAOS_REQUIRE_*. */
#define SET_COUNTING() \
    do { \
        amark_test_allocator_init(&counting); \
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, \
                            amark_allocator_set(amark_test_allocator_activate(&counting))); \
    } while (0)

/* --- Helpers ---------------------------------------------------------------------- */

/* Links child, a detached node, as the last child of parent, through the internal
 * structure (the structural editing comes with structure_edit.c). Records
 * nothing. */
static void link_last(amark_node_t *parent, amark_node_t *child)
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

/* The number of nodes in the ring of document, or SIZE_MAX when a link of the ring
 * is not symmetric or a node of the ring belongs to another document. */
static size_t ring_count(const amark_document_t *document)
{
    const amark_node_t *node = document->ring;
    size_t count = 0;

    if (node == NULL)
    {
        return 0;
    }

    do
    {
        if ((node->ring_next->ring_prev != node) || (node->document != document))
        {
            return SIZE_MAX;
        }

        count++;
        node = node->ring_next;
    } while (node != document->ring);

    return count;
}

/* Whether node is in the ring of its document. */
static bool in_ring(const amark_node_t *node)
{
    const amark_node_t *current = node->document->ring;

    if (current == NULL)
    {
        return false;
    }

    do
    {
        if (current == node)
        {
            return true;
        }

        current = current->ring_next;
    } while (current != node->document->ring);

    return false;
}

/* Gives node a literal and two tokens allocated with amark_malloc, as the literal
 * and token functions will. */
static bool give_literal_and_tokens(amark_node_t *node, const char *text)
{
    size_t length = strlen(text);

    node->literal = amark_malloc(length + 1);
    node->tokens = amark_malloc(2 * sizeof(amark_token_record_t));
    if ((node->literal == NULL) || (node->tokens == NULL))
    {
        return false;
    }

    memcpy(node->literal, text, length + 1);
    node->literal_length = length;
    memset(node->tokens, 0, 2 * sizeof(amark_token_record_t));
    node->token_count = 2;
    node->token_capacity = 2;

    return true;
}

/* Sets *s to a copy of text allocated with amark_malloc. */
static bool give_string(amark_builtin_string_t *s, const char *text)
{
    size_t length = strlen(text);

    s->text = amark_malloc(length + 1);
    if (s->text == NULL)
    {
        return false;
    }

    memcpy(s->text, text, length + 1);
    s->length = length;

    return true;
}

/* The alignments the type data must have: those of long double and of a pointer. */
typedef struct long_double_probe
{
    char c;
    long double value;
} long_double_probe_t;

typedef struct pointer_probe
{
    char c;
    void *value;
} pointer_probe_t;

/* Whether p has the alignment of long double and of pointers. */
static bool is_max_aligned(const void *p)
{
    return (((uintptr_t)p % offsetof(long_double_probe_t, value)) == 0)
           && (((uintptr_t)p % offsetof(pointer_probe_t, value)) == 0);
}

/* --- An extension type ------------------------------------------------------------ */

/* The type data of test.box: a pointer and a long double, which need the
 * alignment of the type data, and a flag init sets. */
typedef struct box_data
{
    long double number;
    void *pointer;
    char *owned;
    bool initialized;
} box_data_t;

/* The calls of the function table of test.box, and whether init fails. */
static size_t box_inits;
static size_t box_deinits;
static bool box_init_fails;
static bool box_init_saw_zeros;

static amark_error_t box_init(amark_node_t *node, void *data)
{
    box_data_t *box = data;

    box_inits++;
    box_init_saw_zeros = (amark_node_data(node) == data) && (box->number == 0.0L)
                         && (box->pointer == NULL) && (box->owned == NULL) && !box->initialized;
    if (box_init_fails)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }

    box->owned = amark_malloc(16);
    if (box->owned == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }

    box->initialized = true;

    return AMARK_ERROR_NONE;
}

static void box_deinit(amark_node_t *node, void *data)
{
    box_data_t *box = data;

    (void)node;
    box_deinits++;
    amark_free(box->owned);
    box->owned = NULL;
}

/* test.box holds only paragraphs, by can_contain, though it accepts nothing. */
static bool box_can_contain(const amark_node_type_desc_t *self,
                            const amark_node_type_desc_t *child)
{
    (void)self;

    return child == amark_builtin_types[AMARK_NODE_PARAGRAPH];
}

static const amark_node_type_desc_t box_type = {
    sizeof(amark_node_type_desc_t), "test.box", AMARK_NODE_LEVEL_BLOCK, AMARK_CONTENT_BLOCK, 0,
    AMARK_LITERAL_NONE, sizeof(box_data_t), box_can_contain, NULL, box_init, box_deinit, NULL,
    NULL, NULL, NULL
};

static const amark_node_type_desc_t *const test_types[] = { &box_type };

static const amark_extension_t test_extension = {
    sizeof(amark_extension_t), "test", test_types, 1, NULL, 0, NULL
};

/* Creates in *out_set a frozen set with test_extension (test.box is 256). */
static amark_error_t create_test_set(amark_syntax_set_t **out_set)
{
    amark_syntax_set_t *set = NULL;
    amark_error_t err;

    *out_set = NULL;
    err = amark_syntax_set_create(&set);
    if (err == AMARK_ERROR_NONE)
    {
        err = amark_syntax_set_append(set, &test_extension);
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = amark_syntax_set_freeze(set);
    }

    if (err != AMARK_ERROR_NONE)
    {
        amark_syntax_set_destroy(set);
        return err;
    }

    *out_set = set;
    return AMARK_ERROR_NONE;
}

/* --- Suite document -------------------------------------------------------------- */

NAOS_TEST(a_new_document_holds_only_its_root)
{
    amark_document_t *document = NULL;
    amark_node_t *root;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    root = amark_document_root(document);
    NAOS_REQUIRE_NOT_NULL(root);

    NAOS_CHECK_PTR_EQ(amark_syntax_set_core(), amark_document_syntax_set(document));
    NAOS_CHECK_UINT_EQ(AMARK_NODE_DOCUMENT, amark_node_get_type(root));
    NAOS_CHECK_STR_EQ("document", amark_node_type_name(root));
    NAOS_CHECK_PTR_EQ(amark_builtin_types[AMARK_NODE_DOCUMENT], amark_node_desc(root));
    NAOS_CHECK_PTR_EQ(document, amark_node_document(root));
    NAOS_CHECK_NULL(amark_node_parent(root));
    NAOS_CHECK_NULL(amark_node_first_child(root));
    NAOS_CHECK_NULL(amark_node_last_child(root));
    NAOS_CHECK_NULL(amark_node_next(root));
    NAOS_CHECK_NULL(amark_node_previous(root));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(root));
    NAOS_CHECK_UINT_EQ(1, ring_count(document));
    NAOS_CHECK_FALSE(amark_document_edit_tracking(document));

    amark_document_destroy(document);
}

NAOS_TEST(a_document_uses_the_frozen_set_it_is_given)
{
    amark_syntax_set_t *set = NULL;
    amark_document_t *document = NULL;
    amark_node_t *box = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, create_test_set(&set));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(set, &document));

    NAOS_CHECK_PTR_EQ(set, amark_document_syntax_set(document));
    NAOS_CHECK_UINT_EQ(AMARK_NODE_DOCUMENT, amark_node_get_type(amark_document_root(document)));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, 256, &box));
    NAOS_CHECK_STR_EQ("test.box", amark_node_type_name(box));
    NAOS_CHECK_PTR_EQ(&box_type, amark_node_desc(box));

    amark_document_destroy(document);
    amark_syntax_set_destroy(set);
}

NAOS_TEST(a_set_that_is_not_frozen_is_an_invalid_state)
{
    amark_syntax_set_t *set = NULL;
    amark_document_t *document;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_create(&set));

    /* Not NULL before the call, to see that a failure sets it to NULL. */
    document = (amark_document_t *)&document;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_STATE, amark_document_create(set, &document));
    NAOS_CHECK_NULL(document);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_document_create(NULL, NULL));

    amark_syntax_set_destroy(set);
}

NAOS_TEST(edit_tracking_starts_off_and_can_be_switched)
{
    amark_document_t *document = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));

    NAOS_CHECK_FALSE(amark_document_edit_tracking(document));
    amark_document_set_edit_tracking(document, true);
    NAOS_CHECK_TRUE(amark_document_edit_tracking(document));
    amark_document_set_edit_tracking(document, false);
    NAOS_CHECK_FALSE(amark_document_edit_tracking(document));

    amark_document_destroy(document);
}

NAOS_TEST(null_documents_give_nothing)
{
    amark_document_destroy(NULL);
    amark_document_set_edit_tracking(NULL, true);

    NAOS_CHECK_NULL(amark_document_root(NULL));
    NAOS_CHECK_NULL(amark_document_syntax_set(NULL));
    NAOS_CHECK_FALSE(amark_document_edit_tracking(NULL));
}

NAOS_TEST(destroying_a_document_frees_every_node)
{
    amark_document_t *document = NULL;
    amark_node_t *root = NULL;
    amark_node_t *nodes[AMARK_BUILTIN_TYPE_COUNT + 1] = { NULL };
    amark_node_t *loose = NULL;
    amark_node_t *loose_child = NULL;
    amark_link_definition_data_t *definition;
    amark_code_block_data_t *code_block;
    amark_node_type_t type;
    bool ok = true;
    size_t live_after;

    SET_COUNTING();

    /* A node of each type: the blocks under the root, the inlines in a detached
     * paragraph (with a detached text of its own), and literals, tokens and string
     * attributes in the type data. */
    ok = (amark_document_create(NULL, &document) == AMARK_ERROR_NONE);
    if (ok)
    {
        root = amark_document_root(document);
    }

    for (type = AMARK_NODE_BLOCK_QUOTE; ok && (type <= AMARK_BUILTIN_TYPE_COUNT); type++)
    {
        ok = (amark_node_create(document, type, &nodes[type]) == AMARK_ERROR_NONE);
    }

    ok = ok && (amark_node_create(document, AMARK_NODE_PARAGRAPH, &loose) == AMARK_ERROR_NONE);
    ok = ok && (amark_node_create(document, AMARK_NODE_TEXT, &loose_child) == AMARK_ERROR_NONE);
    if (ok)
    {
        link_last(root, nodes[AMARK_NODE_PARAGRAPH]);
        link_last(root, nodes[AMARK_NODE_CODE_BLOCK]);
        link_last(root, nodes[AMARK_NODE_LINK_DEFINITION]);
        link_last(nodes[AMARK_NODE_PARAGRAPH], nodes[AMARK_NODE_TEXT]);
        link_last(nodes[AMARK_NODE_PARAGRAPH], nodes[AMARK_NODE_LINK]);
        link_last(loose, loose_child);
        code_block = amark_node_data(nodes[AMARK_NODE_CODE_BLOCK]);
        definition = amark_node_data(nodes[AMARK_NODE_LINK_DEFINITION]);
        ok = give_literal_and_tokens(nodes[AMARK_NODE_TEXT], "text")
             && give_literal_and_tokens(nodes[AMARK_NODE_CODE_BLOCK], "code\n")
             && give_literal_and_tokens(loose_child, "loose")
             && give_string(&code_block->info, "c")
             && give_string(&definition->label, "label")
             && give_string(&definition->link.destination, "/url")
             && give_string(&definition->link.title, "title");
    }

    live_after = counting.live_blocks;
    amark_document_destroy(document);
    amark_allocator_set(NULL);

    NAOS_CHECK_TRUE(ok);
    NAOS_CHECK_UINT_GT(live_after, (size_t)AMARK_BUILTIN_TYPE_COUNT + 2);
    NAOS_CHECK_UINT_EQ(0, counting.live_blocks);
}

NAOS_TEST(destroying_a_document_runs_the_deinit_of_each_node)
{
    amark_syntax_set_t *set = NULL;
    amark_document_t *document = NULL;
    amark_node_t *attached = NULL;
    amark_node_t *detached = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, create_test_set(&set));
    box_inits = 0;
    box_deinits = 0;
    box_init_fails = false;
    SET_COUNTING();

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_create(set, &document));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, 256, &attached));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, 256, &detached));
    if ((attached != NULL) && (detached != NULL))
    {
        link_last(amark_document_root(document), attached);
    }

    amark_document_destroy(document);
    amark_allocator_set(NULL);

    NAOS_CHECK_UINT_EQ(2, box_inits);
    NAOS_CHECK_UINT_EQ(2, box_deinits);
    NAOS_CHECK_UINT_EQ(0, counting.live_blocks);

    amark_syntax_set_destroy(set);
}

NAOS_TEST(two_documents_used_in_turn_do_not_interfere)
{
    amark_document_t *a = NULL;
    amark_document_t *b = NULL;
    amark_node_t *a_nodes[4] = { NULL };
    amark_node_t *b_nodes[4] = { NULL };
    amark_node_t *late = NULL;
    size_t i;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &a));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &b));
    amark_document_set_edit_tracking(b, true);

    /* Creations in turn: each node goes to its own document, and only b
     * records. */
    for (i = 0; i < 4; i++)
    {
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                          amark_node_create(a, AMARK_NODE_PARAGRAPH, &a_nodes[i]));
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_create(b, AMARK_NODE_TEXT, &b_nodes[i]));
        NAOS_CHECK_PTR_EQ(a, amark_node_document(a_nodes[i]));
        NAOS_CHECK_PTR_EQ(b, amark_node_document(b_nodes[i]));
        NAOS_CHECK_UINT_EQ(0, amark_node_edits(a_nodes[i]));
        NAOS_CHECK_UINT_EQ(AMARK_EDIT_CREATED, amark_node_edits(b_nodes[i]));
    }

    NAOS_CHECK_UINT_EQ(5, ring_count(a));
    NAOS_CHECK_UINT_EQ(5, ring_count(b));
    NAOS_CHECK_FALSE(amark_document_edit_tracking(a));

    /* Destroying a leaves b whole and usable. */
    amark_document_destroy(a);
    NAOS_CHECK_UINT_EQ(5, ring_count(b));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_create(b, AMARK_NODE_EMPH, &late));
    NAOS_CHECK_UINT_EQ(6, ring_count(b));
    NAOS_CHECK_PTR_EQ(b, amark_node_document(late));
    NAOS_CHECK_PTR_EQ(b, amark_node_document(amark_document_root(b)));

    amark_document_destroy(b);
}

/* Creates and destroys a document with the core set; a failure must leave
 * *out_document NULL. */
static amark_error_t document_create_scenario(void *context)
{
    bool *unchanged = context;
    amark_document_t *document = NULL;
    amark_error_t err;

    err = amark_document_create(NULL, &document);
    if ((err != AMARK_ERROR_NONE) && (document != NULL))
    {
        *unchanged = false;
    }

    amark_document_destroy(document);

    return err;
}

NAOS_TEST(document_creation_passes_the_out_of_memory_sweep)
{
    bool unchanged = true;

    AMARK_CHECK_OOM_SWEEP(document_create_scenario, &unchanged);
    NAOS_CHECK_TRUE(unchanged);
}

NAOS_SUITE(document,
    NAOS_SET_TEARDOWN(restore),
    NAOS_ADD_TEST(a_new_document_holds_only_its_root),
    NAOS_ADD_TEST(a_document_uses_the_frozen_set_it_is_given),
    NAOS_ADD_TEST(a_set_that_is_not_frozen_is_an_invalid_state),
    NAOS_ADD_TEST(edit_tracking_starts_off_and_can_be_switched),
    NAOS_ADD_TEST(null_documents_give_nothing),
    NAOS_ADD_TEST(destroying_a_document_frees_every_node),
    NAOS_ADD_TEST(destroying_a_document_runs_the_deinit_of_each_node),
    NAOS_ADD_TEST(two_documents_used_in_turn_do_not_interfere),
    NAOS_ADD_TEST(document_creation_passes_the_out_of_memory_sweep))

/* --- Suite node --------------------------------------------------------------------- */

NAOS_TEST(a_new_node_is_detached_in_its_document)
{
    amark_document_t *document = NULL;
    amark_node_t *node = NULL;
    amark_node_type_t type;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));

    for (type = AMARK_NODE_BLOCK_QUOTE; type <= AMARK_BUILTIN_TYPE_COUNT; type++)
    {
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, type, &node));
        NAOS_CHECK_UINT_EQ(type, amark_node_get_type(node));
        NAOS_CHECK_STR_EQ(amark_builtin_types[type]->name, amark_node_type_name(node));
        NAOS_CHECK_PTR_EQ(amark_builtin_types[type], amark_node_desc(node));
        NAOS_CHECK_PTR_EQ(document, amark_node_document(node));
        NAOS_CHECK_NULL(amark_node_parent(node));
        NAOS_CHECK_NULL(amark_node_first_child(node));
        NAOS_CHECK_NULL(amark_node_last_child(node));
        NAOS_CHECK_NULL(amark_node_next(node));
        NAOS_CHECK_NULL(amark_node_previous(node));
        NAOS_CHECK_NULL(amark_node_user_data(node));
        NAOS_CHECK_UINT_EQ(0, amark_node_edits(node));
        NAOS_CHECK_UINT_EQ(0, node->literal_length);
        NAOS_CHECK_UINT_EQ(0, node->token_count);
        NAOS_CHECK_TRUE(in_ring(node));
    }

    NAOS_CHECK_UINT_EQ(AMARK_BUILTIN_TYPE_COUNT, ring_count(document));
    NAOS_CHECK_NULL(amark_node_first_child(amark_document_root(document)));

    amark_document_destroy(document);
}

NAOS_TEST(creation_rejects_the_document_type_and_unknown_types)
{
    static const amark_node_type_t rejected[] = {
        AMARK_NODE_NONE, AMARK_NODE_DOCUMENT, AMARK_BUILTIN_TYPE_COUNT + 1, AMARK_NODE_BUILTIN_LAST,
        AMARK_NODE_DYNAMIC_FIRST, AMARK_NODE_DYNAMIC_FIRST + 1, UINT32_MAX
    };
    amark_syntax_set_t *set = NULL;
    amark_document_t *core = NULL;
    amark_document_t *extended = NULL;
    amark_node_t *node;
    size_t i;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, create_test_set(&set));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &core));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(set, &extended));

    for (i = 0; i < (sizeof(rejected) / sizeof(rejected[0])); i++)
    {
        node = (amark_node_t *)&node;
        NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                          amark_node_create(core, rejected[i], &node));
        NAOS_CHECK_NULL(node);
    }

    /* 256 exists only in the extended set. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_create(extended, AMARK_NODE_DOCUMENT, &node));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_create(extended, 257, &node));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_create(extended, 256, &node));

    node = (amark_node_t *)&node;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_create(NULL, AMARK_NODE_PARAGRAPH, &node));
    NAOS_CHECK_NULL(node);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_create(core, AMARK_NODE_PARAGRAPH, NULL));
    NAOS_CHECK_UINT_EQ(1, ring_count(core));
    NAOS_CHECK_UINT_EQ(2, ring_count(extended));

    amark_document_destroy(extended);
    amark_document_destroy(core);
    amark_syntax_set_destroy(set);
}

NAOS_TEST(null_nodes_give_nothing)
{
    NAOS_CHECK_UINT_EQ(AMARK_NODE_NONE, amark_node_get_type(NULL));
    NAOS_CHECK_STR_EQ("", amark_node_type_name(NULL));
    NAOS_CHECK_NULL(amark_node_desc(NULL));
    NAOS_CHECK_NULL(amark_node_document(NULL));
    NAOS_CHECK_NULL(amark_node_parent(NULL));
    NAOS_CHECK_NULL(amark_node_first_child(NULL));
    NAOS_CHECK_NULL(amark_node_last_child(NULL));
    NAOS_CHECK_NULL(amark_node_next(NULL));
    NAOS_CHECK_NULL(amark_node_previous(NULL));
    NAOS_CHECK_FALSE(amark_node_is_block(NULL));
    NAOS_CHECK_FALSE(amark_node_is_inline(NULL));
    NAOS_CHECK_FALSE(amark_node_can_have_children(NULL));
    NAOS_CHECK_FALSE(amark_node_can_contain(NULL, AMARK_NODE_PARAGRAPH));
    NAOS_CHECK_NULL(amark_node_user_data(NULL));
    NAOS_CHECK_NULL(amark_node_data(NULL));
    NAOS_CHECK_NULL(amark_node_data_const(NULL));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(NULL));

    amark_node_set_user_data(NULL, &counting);
    amark_node_mark_edited(NULL);
}

NAOS_TEST(the_relations_follow_the_links)
{
    amark_document_t *document = NULL;
    amark_node_t *root;
    amark_node_t *paragraph = NULL;
    amark_node_t *first = NULL;
    amark_node_t *second = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    root = amark_document_root(document);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(document, AMARK_NODE_PARAGRAPH, &paragraph));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, AMARK_NODE_TEXT, &first));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, AMARK_NODE_TEXT, &second));
    link_last(root, paragraph);
    link_last(paragraph, first);
    link_last(paragraph, second);

    NAOS_CHECK_PTR_EQ(paragraph, amark_node_first_child(root));
    NAOS_CHECK_PTR_EQ(paragraph, amark_node_last_child(root));
    NAOS_CHECK_PTR_EQ(root, amark_node_parent(paragraph));
    NAOS_CHECK_PTR_EQ(first, amark_node_first_child(paragraph));
    NAOS_CHECK_PTR_EQ(second, amark_node_last_child(paragraph));
    NAOS_CHECK_PTR_EQ(second, amark_node_next(first));
    NAOS_CHECK_PTR_EQ(first, amark_node_previous(second));
    NAOS_CHECK_NULL(amark_node_previous(first));
    NAOS_CHECK_NULL(amark_node_next(second));
    NAOS_CHECK_PTR_EQ(paragraph, amark_node_parent(second));

    amark_document_destroy(document);
}

NAOS_TEST(the_associated_pointer_is_kept_and_records_nothing)
{
    amark_document_t *document = NULL;
    amark_node_t *node = NULL;
    int value = 0;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, AMARK_NODE_TEXT, &node));
    amark_document_set_edit_tracking(document, true);

    amark_node_set_user_data(node, &value);
    NAOS_CHECK_PTR_EQ(&value, amark_node_user_data(node));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(node));
    amark_node_set_user_data(amark_document_root(document), node);
    NAOS_CHECK_PTR_EQ(node, amark_node_user_data(amark_document_root(document)));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(amark_document_root(document)));
    amark_node_set_user_data(node, NULL);
    NAOS_CHECK_NULL(amark_node_user_data(node));

    amark_document_destroy(document);
}

NAOS_TEST(the_type_data_is_null_without_data_and_aligned_with_it)
{
    amark_document_t *document = NULL;
    amark_node_t *node = NULL;
    amark_node_type_t type;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    NAOS_CHECK_NULL(amark_node_data(amark_document_root(document)));

    for (type = AMARK_NODE_BLOCK_QUOTE; type <= AMARK_BUILTIN_TYPE_COUNT; type++)
    {
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, type, &node));
        if (amark_builtin_types[type]->data_size == 0)
        {
            NAOS_CHECK_NULL(amark_node_data(node));
            NAOS_CHECK_NULL(amark_node_data_const(node));
        }
        else
        {
            NAOS_CHECK_NOT_NULL(amark_node_data(node));
            NAOS_CHECK_PTR_EQ(amark_node_data(node), amark_node_data_const(node));
            /* After the header, at the alignment of long double and pointers. */
            NAOS_CHECK_TRUE((const char *)amark_node_data(node)
                            >= ((const char *)node + sizeof(*node)));
            NAOS_CHECK_TRUE(is_max_aligned(amark_node_data(node)));
        }
    }

    amark_document_destroy(document);
}

NAOS_TEST(an_extension_type_gets_its_function_table)
{
    amark_syntax_set_t *set = NULL;
    amark_document_t *document = NULL;
    amark_node_t *box = NULL;
    amark_node_t *other = NULL;
    box_data_t *data;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, create_test_set(&set));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(set, &document));
    box_inits = 0;
    box_deinits = 0;
    box_init_fails = false;
    box_init_saw_zeros = false;

    /* init sees the zero-filled data, at the place amark_node_data gives. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, 256, &box));
    data = amark_node_data(box);
    NAOS_REQUIRE_NOT_NULL(data);
    NAOS_CHECK_UINT_EQ(1, box_inits);
    NAOS_CHECK_TRUE(box_init_saw_zeros);
    NAOS_CHECK_TRUE(data->initialized);
    NAOS_CHECK_TRUE(is_max_aligned(data));
    data->pointer = &data;
    NAOS_CHECK_PTR_EQ(&data, ((const box_data_t *)amark_node_data_const(box))->pointer);

    /* can_contain decides alone: test.box accepts nothing but holds paragraphs. */
    NAOS_CHECK_TRUE(amark_node_is_block(box));
    NAOS_CHECK_FALSE(amark_node_is_inline(box));
    NAOS_CHECK_TRUE(amark_node_can_have_children(box));
    NAOS_CHECK_TRUE(amark_node_can_contain(box, AMARK_NODE_PARAGRAPH));
    NAOS_CHECK_FALSE(amark_node_can_contain(box, AMARK_NODE_HEADING));
    NAOS_CHECK_FALSE(amark_node_can_contain(box, 256));
    NAOS_CHECK_FALSE(amark_node_can_contain(box, 257));
    /* The root accepts blocks, test.box among them. */
    NAOS_CHECK_TRUE(amark_node_can_contain(amark_document_root(document), 256));

    /* A failing init: the error, no node and nothing left. */
    box_init_fails = true;
    other = (amark_node_t *)&other;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_OUT_OF_MEMORY, amark_node_create(document, 256, &other));
    NAOS_CHECK_NULL(other);
    NAOS_CHECK_UINT_EQ(2, box_inits);
    NAOS_CHECK_UINT_EQ(0, box_deinits);
    NAOS_CHECK_UINT_EQ(2, ring_count(document));
    box_init_fails = false;

    amark_document_destroy(document);
    NAOS_CHECK_UINT_EQ(1, box_deinits);

    amark_syntax_set_destroy(set);
}

NAOS_TEST(a_deep_detached_subtree_is_destroyed_without_recursion)
{
    enum
    {
        DEPTH = 100000
    };
    amark_document_t *document = NULL;
    amark_node_t *top = NULL;
    amark_node_t *bottom = NULL;
    amark_node_t *child = NULL;
    amark_node_t *kept = NULL;
    bool ok;
    size_t i;

    SET_COUNTING();

    /* A chain of DEPTH block quotes, each also with a text sibling of the next,
     * and a node outside the subtree. */
    ok = (amark_document_create(NULL, &document) == AMARK_ERROR_NONE)
         && (amark_node_create(document, AMARK_NODE_PARAGRAPH, &kept) == AMARK_ERROR_NONE)
         && (amark_node_create(document, AMARK_NODE_BLOCK_QUOTE, &top) == AMARK_ERROR_NONE);
    bottom = top;
    for (i = 1; ok && (i < DEPTH); i++)
    {
        ok = (amark_node_create(document, AMARK_NODE_BLOCK_QUOTE, &child) == AMARK_ERROR_NONE);
        if (ok)
        {
            link_last(bottom, child);
            ok = (amark_node_create(document, AMARK_NODE_THEMATIC_BREAK, &child)
                  == AMARK_ERROR_NONE);
        }

        if (ok)
        {
            link_last(bottom, child);
            bottom = bottom->first_child;
        }
    }

    if (ok)
    {
        amark_node_destroy_subtree(top);
    }

    amark_allocator_set(NULL);

    NAOS_REQUIRE_TRUE(ok);
    NAOS_CHECK_UINT_EQ(2, ring_count(document));
    NAOS_CHECK_TRUE(in_ring(kept));
    NAOS_CHECK_TRUE(in_ring(amark_document_root(document)));
    /* The document and its 2 nodes. */
    NAOS_CHECK_UINT_EQ(3, counting.live_blocks);

    amark_allocator_set(amark_test_allocator_activate(&counting));
    amark_document_destroy(document);
    amark_allocator_set(NULL);

    NAOS_CHECK_UINT_EQ(0, counting.live_blocks);
}

/* Creates a document, then a node of each built-in type but document; a failure
 * must leave *out_node NULL and the ring as before. */
static amark_error_t node_create_scenario(void *context)
{
    bool *unchanged = context;
    amark_document_t *document = NULL;
    amark_node_t *node;
    amark_node_type_t type;
    amark_error_t err;

    err = amark_document_create(NULL, &document);
    for (type = AMARK_NODE_BLOCK_QUOTE;
         (err == AMARK_ERROR_NONE) && (type <= AMARK_BUILTIN_TYPE_COUNT); type++)
    {
        node = (amark_node_t *)&node;
        err = amark_node_create(document, type, &node);
        if ((err != AMARK_ERROR_NONE)
            && ((node != NULL) || (ring_count(document) != (size_t)(type - 1))))
        {
            *unchanged = false;
        }
    }

    amark_document_destroy(document);

    return err;
}

NAOS_TEST(node_creation_passes_the_out_of_memory_sweep)
{
    bool unchanged = true;

    AMARK_CHECK_OOM_SWEEP(node_create_scenario, &unchanged);
    NAOS_CHECK_TRUE(unchanged);
}

NAOS_SUITE(node,
    NAOS_SET_TEARDOWN(restore),
    NAOS_ADD_TEST(a_new_node_is_detached_in_its_document),
    NAOS_ADD_TEST(creation_rejects_the_document_type_and_unknown_types),
    NAOS_ADD_TEST(null_nodes_give_nothing),
    NAOS_ADD_TEST(the_relations_follow_the_links),
    NAOS_ADD_TEST(the_associated_pointer_is_kept_and_records_nothing),
    NAOS_ADD_TEST(the_type_data_is_null_without_data_and_aligned_with_it),
    NAOS_ADD_TEST(an_extension_type_gets_its_function_table),
    NAOS_ADD_TEST(a_deep_detached_subtree_is_destroyed_without_recursion),
    NAOS_ADD_TEST(node_creation_passes_the_out_of_memory_sweep))

/* --- Suite source ------------------------------------------------------------------ */

/* An expected position: offset, line, column. */
typedef struct expected_position
{
    size_t offset;
    size_t line;
    size_t column;
} expected_position_t;

/* Whether every offset of expected has its line and column in source of
 * document. */
static bool positions_are(const amark_document_t *document, amark_source_id_t source,
                          const expected_position_t *expected, size_t count)
{
    size_t i;

    for (i = 0; i < count; i++)
    {
        size_t line = 0;
        size_t column = 0;

        if ((amark_document_position(document, source, expected[i].offset, &line, &column)
             != AMARK_ERROR_NONE) ||
            (line != expected[i].line) || (column != expected[i].column))
        {
            return false;
        }
    }

    return true;
}

/* The positions of "a\nbc\r\rd\r\n\ne" (LF, CR, CR, CRLF, LF), whatever the chunks:
 * offsets 0..11. The LF of the CRLF (offset 8) is on the line of its CR. */
static const expected_position_t mixed_positions[] = {
    { 0, 1, 1 }, { 1, 1, 2 },                 /* a \n */
    { 2, 2, 1 }, { 3, 2, 2 }, { 4, 2, 3 },    /* b c \r */
    { 5, 3, 1 },                              /* \r */
    { 6, 4, 1 }, { 7, 4, 2 }, { 8, 4, 3 },    /* d \r \n */
    { 9, 5, 1 },                              /* \n */
    { 10, 6, 1 }, { 11, 6, 2 },               /* e, the end */
};

#define MIXED "a\nbc\r\rd\r\n\ne"
#define MIXED_COUNT (sizeof(mixed_positions) / sizeof(mixed_positions[0]))

NAOS_TEST(sources_are_copied_and_numbered_from_zero)
{
    amark_document_t *document = NULL;
    char text[] = "abc";
    amark_source_id_t id = 99;
    const char *data;
    size_t length = 99;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    NAOS_CHECK_UINT_EQ(0, amark_document_source_count(document));

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_add_source(document, text, 3, &id));
    NAOS_CHECK_UINT_EQ(0, id);
    text[0] = 'X';
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_add_source(document, "", 0, &id));
    NAOS_CHECK_UINT_EQ(1, id);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_add_source(document, NULL, 0, &id));
    NAOS_CHECK_UINT_EQ(2, id);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_add_source(document, "x\ny", 3, &id));
    NAOS_CHECK_UINT_EQ(3, id);
    for (length = 0; length < 10; length++)
    {
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_add_source(document, "z", 1, &id));
        NAOS_CHECK_UINT_EQ(4 + length, id);
    }

    NAOS_CHECK_UINT_EQ(14, amark_document_source_count(document));

    /* The copy does not follow the caller's bytes. */
    data = amark_document_source_data(document, 0, &length);
    AMARK_CHECK_BYTES_EQ(data, length, "abc");
    data = amark_document_source_data(document, 1, &length);
    NAOS_CHECK_NOT_NULL(data);
    NAOS_CHECK_UINT_EQ(0, length);
    data = amark_document_source_data(document, 2, &length);
    NAOS_CHECK_NOT_NULL(data);
    NAOS_CHECK_UINT_EQ(0, length);
    data = amark_document_source_data(document, 3, NULL);
    AMARK_CHECK_BYTES_EQ(data, 3, "x\ny");

    amark_document_destroy(document);
}

NAOS_TEST(source_arguments_are_checked)
{
    amark_document_t *document = NULL;
    const char *data;
    amark_source_id_t id = 99;
    size_t length = 99;
    size_t line = 99;
    size_t column = 99;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));

    /* NULL data with a length, NULL document, NULL out_source: nothing added. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_document_add_source(document, NULL, 1, &id));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_document_add_source(NULL, "a", 1, &id));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_document_add_source(document, "a", 1, NULL));
    NAOS_CHECK_UINT_EQ(99, id);
    NAOS_CHECK_UINT_EQ(0, amark_document_source_count(document));

    /* Unknown sources. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_document_append_source(document, 0, "a", 1));
    NAOS_CHECK_NULL(amark_document_source_data(document, 0, &length));
    NAOS_CHECK_UINT_EQ(0, length);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_document_position(document, 0, 0, &line, &column));
    NAOS_CHECK_UINT_EQ(0, line);
    NAOS_CHECK_UINT_EQ(0, column);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_add_source(document, "ab", 2, &id));
    NAOS_CHECK_UINT_EQ(0, id);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_document_append_source(document, 1, "a", 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_document_append_source(NULL, 0, "a", 1));
    NAOS_CHECK_NULL(amark_document_source_data(document, 1, NULL));
    NAOS_CHECK_NULL(amark_document_source_data(NULL, 0, NULL));
    NAOS_CHECK_UINT_EQ(0, amark_document_source_count(NULL));

    /* NULL data: nothing to append with length 0, rejected with another length. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_append_source(document, 0, NULL, 0));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_document_append_source(document, 0, NULL, 1));
    data = amark_document_source_data(document, 0, &length);
    AMARK_CHECK_BYTES_EQ(data, length, "ab");

    /* Positions: offset up to the length, NULL outputs, unknown source. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_position(document, 0, 2, &line, &column));
    NAOS_CHECK_UINT_EQ(1, line);
    NAOS_CHECK_UINT_EQ(3, column);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_document_position(document, 0, 3, &line, &column));
    NAOS_CHECK_UINT_EQ(0, line);
    NAOS_CHECK_UINT_EQ(0, column);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_document_position(document, 0, SIZE_MAX, &line, &column));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_document_position(document, 0, 0, NULL, &column));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_document_position(document, 0, 0, &line, NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_document_position(NULL, 0, 0, &line, &column));

    amark_document_destroy(document);
}

NAOS_TEST(an_empty_source_has_one_position)
{
    amark_document_t *document = NULL;
    amark_source_id_t id = 99;
    size_t line = 0;
    size_t column = 0;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_add_source(document, NULL, 0, &id));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_position(document, id, 0, &line, &column));
    NAOS_CHECK_UINT_EQ(1, line);
    NAOS_CHECK_UINT_EQ(1, column);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_document_position(document, id, 1, &line, &column));

    amark_document_destroy(document);
}

NAOS_TEST(positions_follow_lf_cr_and_crlf_lines)
{
    amark_document_t *document = NULL;
    amark_source_id_t id = 99;
    size_t line = 0;
    size_t column = 0;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_document_add_source(document, MIXED, sizeof(MIXED) - 1, &id));
    NAOS_CHECK_TRUE(positions_are(document, id, mixed_positions, MIXED_COUNT));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_document_position(document, id, sizeof(MIXED), &line, &column));

    /* Ending with a line break: the end is on a new, empty line. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_add_source(document, "x\r\n", 3, &id));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_position(document, id, 3, &line, &column));
    NAOS_CHECK_UINT_EQ(2, line);
    NAOS_CHECK_UINT_EQ(1, column);

    amark_document_destroy(document);
}

NAOS_TEST(every_split_into_chunks_gives_the_same_positions)
{
    const char text[] = MIXED;
    size_t length = sizeof(text) - 1;
    size_t first;
    size_t second;
    size_t bad_first = SIZE_MAX;
    size_t bad_second = SIZE_MAX;

    /* Three chunks cut at every pair of places, the CRLF split between two chunks
     * included (first or second at 8), and empty chunks. */
    for (first = 0; first <= length; first++)
    {
        for (second = first; second <= length; second++)
        {
            amark_document_t *document = NULL;
            amark_source_id_t id = 99;
            const char *data;
            size_t stored = 0;
            bool ok;

            NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
            ok = (amark_document_add_source(document, text, first, &id) == AMARK_ERROR_NONE) &&
                 (amark_document_append_source(document, id, text + first, second - first)
                  == AMARK_ERROR_NONE) &&
                 (amark_document_append_source(document, id, text + second, length - second)
                  == AMARK_ERROR_NONE);
            NAOS_CHECK_TRUE(ok);
            data = amark_document_source_data(document, id, &stored);
            AMARK_CHECK_BYTES_EQ(data, stored, MIXED);
            if (!positions_are(document, id, mixed_positions, MIXED_COUNT) &&
                (bad_first == SIZE_MAX))
            {
                bad_first = first;
                bad_second = second;
            }

            amark_document_destroy(document);
        }
    }

    /* The first cuts whose positions differ, if any. */
    NAOS_CHECK_UINT_EQ(SIZE_MAX, bad_first);
    NAOS_CHECK_UINT_EQ(SIZE_MAX, bad_second);
}

NAOS_TEST(a_crlf_split_between_chunks_is_one_line_ending)
{
    amark_document_t *document = NULL;
    amark_source_id_t id = 99;
    size_t line = 0;
    size_t column = 0;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_add_source(document, "a\r", 2, &id));

    /* Before the LF comes, the end follows a CR line ending. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_position(document, id, 2, &line, &column));
    NAOS_CHECK_UINT_EQ(2, line);
    NAOS_CHECK_UINT_EQ(1, column);
    NAOS_CHECK_TRUE(document->sources[id].pending_cr);

    /* An empty chunk keeps the pending CR. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_append_source(document, id, "", 0));
    NAOS_CHECK_TRUE(document->sources[id].pending_cr);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_append_source(document, id, "\nb", 2));
    NAOS_CHECK_FALSE(document->sources[id].pending_cr);
    NAOS_CHECK_UINT_EQ(2, document->sources[id].line_count);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_position(document, id, 2, &line, &column));
    NAOS_CHECK_UINT_EQ(1, line);
    NAOS_CHECK_UINT_EQ(3, column);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_position(document, id, 3, &line, &column));
    NAOS_CHECK_UINT_EQ(2, line);
    NAOS_CHECK_UINT_EQ(1, column);

    /* A CR and then a CR at the start of the next chunk are two line endings. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_append_source(document, id, "\r", 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_append_source(document, id, "\r", 1));
    NAOS_CHECK_UINT_EQ(4, document->sources[id].line_count);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_position(document, id, 6, &line, &column));
    NAOS_CHECK_UINT_EQ(4, line);
    NAOS_CHECK_UINT_EQ(1, column);

    amark_document_destroy(document);
}

NAOS_TEST(appending_keeps_the_bytes_at_each_offset)
{
    amark_document_t *document = NULL;
    amark_source_id_t first = 99;
    amark_source_id_t second = 99;
    const char *data;
    size_t length = 0;
    size_t i;
    size_t line = 0;
    size_t column = 0;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_add_source(document, "0123", 4, &first));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_add_source(document, "other", 5, &second));

    /* Many chunks, so that the bytes and the line index are reallocated. */
    for (i = 0; i < 200; i++)
    {
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                          amark_document_append_source(document, first, "ab\n", 3));
    }

    data = amark_document_source_data(document, first, &length);
    NAOS_REQUIRE_NOT_NULL(data);
    NAOS_CHECK_UINT_EQ(4 + (200 * 3), length);
    AMARK_CHECK_BYTES_EQ(data, 4, "0123");
    for (i = 0; i < 200; i++)
    {
        AMARK_CHECK_BYTES_EQ(data + 4 + (i * 3), 3, "ab\n");
    }

    NAOS_CHECK_INT_EQ(0, data[length]);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_document_position(document, first, 4 + (150 * 3) + 1, &line, &column));
    NAOS_CHECK_UINT_EQ(151, line);
    NAOS_CHECK_UINT_EQ(2, column);

    /* Appending part of the source to itself. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_append_source(document, first, data, 4));
    data = amark_document_source_data(document, first, &length);
    AMARK_CHECK_BYTES_EQ(data + length - 4, 4, "0123");
    AMARK_CHECK_BYTES_EQ(data, 4, "0123");

    /* The other source is untouched. */
    data = amark_document_source_data(document, second, &length);
    AMARK_CHECK_BYTES_EQ(data, length, "other");

    amark_document_destroy(document);
}

/* Whether source id of document holds exactly the bytes expected (length bytes),
 * has line_count lines and the pending CR pending_cr. */
static bool source_is(const amark_document_t *document, amark_source_id_t id,
                      const char *expected, size_t length, size_t line_count, bool pending_cr)
{
    size_t stored = 0;
    const char *data = amark_document_source_data(document, id, &stored);

    return (data != NULL) && (stored == length) && (memcmp(data, expected, length) == 0) &&
           (document->sources[id].line_count == line_count) &&
           (document->sources[id].pending_cr == pending_cr);
}

NAOS_TEST(appending_a_source_to_itself_survives_the_reallocation)
{
    amark_document_t *document = NULL;
    amark_source_id_t plain = 99;
    amark_source_id_t lines = 99;
    const char *data;
    size_t length = 0;
    size_t capacity;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));

    /* All of the bytes, at the exact capacity of the copy: the append must
     * reallocate them while reading them. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_add_source(document, "0123", 4, &plain));
    capacity = document->sources[plain].bytes.capacity;
    data = amark_document_source_data(document, plain, &length);
    NAOS_CHECK_UINT_GT((length * 2) + 1, capacity);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_append_source(document, plain, data, length));
    NAOS_CHECK_UINT_GT(document->sources[plain].bytes.capacity, capacity);
    NAOS_CHECK_TRUE(source_is(document, plain, "01230123", 8, 1, false));

    /* With line endings: the CR at the end of the source and the LF at the start
     * of its copy are one line ending, "\na\r" + "\na\r" being the lines "\n",
     * "a\r\n", "a\r" and an empty last one. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_add_source(document, "\na\r", 3, &lines));
    capacity = document->sources[lines].bytes.capacity;
    data = amark_document_source_data(document, lines, &length);
    NAOS_CHECK_UINT_GT((length * 2) + 1, capacity);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_append_source(document, lines, data, length));
    NAOS_CHECK_TRUE(source_is(document, lines, "\na\r\na\r", 6, 4, true));
    NAOS_CHECK_TRUE(positions_are(document, lines,
                                  (const expected_position_t[]){ { 0, 1, 1 }, { 1, 2, 1 },
                                                                  { 3, 2, 3 }, { 4, 3, 1 },
                                                                  { 6, 4, 1 } },
                                  5));

    amark_document_destroy(document);
}

NAOS_TEST(destroying_a_document_frees_its_sources)
{
    amark_document_t *document = NULL;
    amark_source_id_t id = 0;
    bool ok;
    size_t i;
    size_t live_after;

    SET_COUNTING();

    ok = (amark_document_create(NULL, &document) == AMARK_ERROR_NONE);
    for (i = 0; ok && (i < 9); i++)
    {
        ok = (amark_document_add_source(document, "a\r\nb", 4, &id) == AMARK_ERROR_NONE) &&
             (amark_document_append_source(document, id, "\rc\n", 3) == AMARK_ERROR_NONE);
    }

    ok = ok && (amark_document_add_source(document, NULL, 0, &id) == AMARK_ERROR_NONE);
    live_after = counting.live_blocks;
    amark_document_destroy(document);
    amark_allocator_set(NULL);

    NAOS_CHECK_TRUE(ok);
    NAOS_CHECK_UINT_GT(live_after, 10);
    NAOS_CHECK_UINT_EQ(0, counting.live_blocks);
}

/* Adds a source to a document that has one; a failure must leave the document with
 * its one source unchanged and *out_source untouched. */
static amark_error_t add_source_scenario(void *context)
{
    bool *unchanged = context;
    amark_document_t *document = NULL;
    amark_source_id_t id = 0;
    amark_source_id_t added = 0;
    size_t i;
    amark_error_t err;

    err = amark_document_create(NULL, &document);
    if (err == AMARK_ERROR_NONE)
    {
        err = amark_document_add_source(document, "a\r", 2, &id);
    }

    /* Fills the first array of sources, so that one of the additions grows it. */
    for (i = 0; (err == AMARK_ERROR_NONE) && (i < 4); i++)
    {
        size_t count = amark_document_source_count(document);

        err = amark_document_add_source(document, "b\nc\r\nd\re", 8, &added);
        if (err != AMARK_ERROR_NONE)
        {
            if ((amark_document_source_count(document) != count) || (added != (count - 1)) ||
                !source_is(document, 0, "a\r", 2, 2, true))
            {
                *unchanged = false;
            }
        }
        else if ((added != count) || !source_is(document, added, "b\nc\r\nd\re", 8, 4, false))
        {
            *unchanged = false;
        }
    }

    amark_document_destroy(document);

    return err;
}

NAOS_TEST(adding_a_source_passes_the_out_of_memory_sweep)
{
    bool unchanged = true;

    AMARK_CHECK_OOM_SWEEP(add_source_scenario, &unchanged);
    NAOS_CHECK_TRUE(unchanged);
}

/* Appends chunks to a source that ends with a CR; a failure must leave its bytes,
 * its line index and its pending CR unchanged. */
static amark_error_t append_source_scenario(void *context)
{
    bool *unchanged = context;
    amark_document_t *document = NULL;
    amark_source_id_t id = 0;
    amark_error_t err;

    err = amark_document_create(NULL, &document);
    if (err == AMARK_ERROR_NONE)
    {
        err = amark_document_add_source(document, "x\r", 2, &id);
    }

    if (err == AMARK_ERROR_NONE)
    {
        /* Many line starts, so that the line index grows. */
        err = amark_document_append_source(document, id, "\n1\n2\r3\r\n4\n5\n6\n7\n8\r", 18);
        if ((err != AMARK_ERROR_NONE) && !source_is(document, id, "x\r", 2, 2, true))
        {
            *unchanged = false;
        }
    }

    if (err == AMARK_ERROR_NONE)
    {
        if (!source_is(document, id, "x\r\n1\n2\r3\r\n4\n5\n6\n7\n8\r", 20, 10, true))
        {
            *unchanged = false;
        }

        if (!positions_are(document, id,
                           (const expected_position_t[]){ { 2, 1, 3 }, { 3, 2, 1 },
                                                           { 9, 4, 3 }, { 20, 10, 1 } },
                           4))
        {
            *unchanged = false;
        }
    }

    amark_document_destroy(document);

    return err;
}

NAOS_TEST(appending_to_a_source_passes_the_out_of_memory_sweep)
{
    bool unchanged = true;

    AMARK_CHECK_OOM_SWEEP(append_source_scenario, &unchanged);
    NAOS_CHECK_TRUE(unchanged);
}

NAOS_SUITE(source,
    NAOS_SET_TEARDOWN(restore),
    NAOS_ADD_TEST(sources_are_copied_and_numbered_from_zero),
    NAOS_ADD_TEST(source_arguments_are_checked),
    NAOS_ADD_TEST(an_empty_source_has_one_position),
    NAOS_ADD_TEST(positions_follow_lf_cr_and_crlf_lines),
    NAOS_ADD_TEST(every_split_into_chunks_gives_the_same_positions),
    NAOS_ADD_TEST(a_crlf_split_between_chunks_is_one_line_ending),
    NAOS_ADD_TEST(appending_keeps_the_bytes_at_each_offset),
    NAOS_ADD_TEST(appending_a_source_to_itself_survives_the_reallocation),
    NAOS_ADD_TEST(destroying_a_document_frees_its_sources),
    NAOS_ADD_TEST(adding_a_source_passes_the_out_of_memory_sweep),
    NAOS_ADD_TEST(appending_to_a_source_passes_the_out_of_memory_sweep))

/* --- Suite ownership ---------------------------------------------------------------- */

/* The lookups the deinit of test.reader made through the syntax set of the document,
 * and how many of them found the type and its extension. */
static size_t reader_deinits;
static size_t reader_lookups_found;

/* Reads the syntax set of the document of node: the extension list, the dynamic
 * types and the name index, all freed with the set. Under the sanitizers, a set
 * freed before the nodes makes these reads fail the test. */
static void reader_deinit(amark_node_t *node, void *data)
{
    const amark_syntax_set_t *set = amark_document_syntax_set(amark_node_document(node));
    amark_node_type_t by_desc = AMARK_NODE_NONE;
    amark_node_type_t by_name = AMARK_NODE_NONE;

    (void)data;
    reader_deinits++;
    if ((amark_syntax_set_find_extension(set, "reader") != NULL)
        && amark_syntax_set_node_type_of(set, amark_node_desc(node), &by_desc)
        && amark_syntax_set_find_node_type(set, "reader.node", &by_name) && (by_desc == by_name)
        && (amark_syntax_set_node_type_desc(set, by_desc) == amark_node_desc(node)))
    {
        reader_lookups_found++;
    }
}

static const amark_node_type_desc_t reader_type = {
    sizeof(amark_node_type_desc_t), "reader.node", AMARK_NODE_LEVEL_BLOCK, AMARK_CONTENT_BLOCK,
    0, AMARK_LITERAL_NONE, 0, NULL, NULL, NULL, reader_deinit, NULL, NULL, NULL, NULL
};

static const amark_node_type_desc_t *const reader_types[] = { &reader_type };

static const amark_extension_t reader_extension = {
    sizeof(amark_extension_t), "reader", reader_types, 1, NULL, 0, NULL
};

/* Creates in *out_set a frozen set with reader_extension (reader.node is 256). */
static amark_error_t create_reader_set(amark_syntax_set_t **out_set)
{
    amark_syntax_set_t *set = NULL;
    amark_error_t err;

    *out_set = NULL;
    err = amark_syntax_set_create(&set);
    if (err == AMARK_ERROR_NONE)
    {
        err = amark_syntax_set_append(set, &reader_extension);
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = amark_syntax_set_freeze(set);
    }

    if (err != AMARK_ERROR_NONE)
    {
        amark_syntax_set_destroy(set);
        return err;
    }

    *out_set = set;
    return AMARK_ERROR_NONE;
}

NAOS_TEST(a_set_handed_to_the_document_is_freed_with_it)
{
    amark_syntax_set_t *set = NULL;
    amark_document_t *document = NULL;
    amark_node_t *attached = NULL;
    amark_node_t *detached = NULL;
    amark_error_t taken = AMARK_ERROR_INVALID_STATE;
    bool owned = false;
    bool ok;
    size_t live_after;

    box_init_fails = false;
    SET_COUNTING();

    ok = (create_test_set(&set) == AMARK_ERROR_NONE)
         && (amark_document_create(set, &document) == AMARK_ERROR_NONE)
         && (amark_node_create(document, 256, &attached) == AMARK_ERROR_NONE)
         && (amark_node_create(document, 256, &detached) == AMARK_ERROR_NONE);
    if (ok)
    {
        link_last(amark_document_root(document), attached);
        taken = amark_document_take_syntax_set(document, set);
        owned = (document->owned_syntax == set) && (amark_document_syntax_set(document) == set);
    }

    /* The set is the document's now: the caller destroys only the document. */
    live_after = counting.live_blocks;
    amark_document_destroy(document);
    if (taken != AMARK_ERROR_NONE)
    {
        amark_syntax_set_destroy(set);
    }

    amark_allocator_set(NULL);

    NAOS_CHECK_TRUE(ok);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, taken);
    NAOS_CHECK_TRUE(owned);
    NAOS_CHECK_UINT_GT(live_after, 4);
    NAOS_CHECK_UINT_EQ(0, counting.live_blocks);
}

NAOS_TEST(the_owned_set_is_freed_after_the_deinit_of_every_node)
{
    amark_syntax_set_t *set = NULL;
    amark_document_t *document = NULL;
    amark_node_t *nodes[3] = { NULL };
    amark_error_t taken = AMARK_ERROR_INVALID_STATE;
    bool ok;
    size_t i;

    reader_deinits = 0;
    reader_lookups_found = 0;
    SET_COUNTING();

    /* Under the root, in a subtree and detached: the root is freed first, the
     * detached node after it, and the set only after both. */
    ok = (create_reader_set(&set) == AMARK_ERROR_NONE)
         && (amark_document_create(set, &document) == AMARK_ERROR_NONE);
    for (i = 0; ok && (i < 3); i++)
    {
        ok = (amark_node_create(document, 256, &nodes[i]) == AMARK_ERROR_NONE);
    }

    if (ok)
    {
        link_last(amark_document_root(document), nodes[0]);
        link_last(amark_document_root(document), nodes[1]);
        taken = amark_document_take_syntax_set(document, set);
    }

    amark_document_destroy(document);
    if (taken != AMARK_ERROR_NONE)
    {
        amark_syntax_set_destroy(set);
    }

    amark_allocator_set(NULL);

    NAOS_CHECK_TRUE(ok);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, taken);
    NAOS_CHECK_UINT_EQ(3, reader_deinits);
    NAOS_CHECK_UINT_EQ(3, reader_lookups_found);
    NAOS_CHECK_UINT_EQ(0, counting.live_blocks);
}

NAOS_TEST(a_rejected_hand_over_leaves_the_set_to_the_caller)
{
    amark_syntax_set_t *set = NULL;
    amark_syntax_set_t *other = NULL;
    amark_syntax_set_t *core = (amark_syntax_set_t *)amark_syntax_set_core();
    amark_document_t *document = NULL;
    amark_document_t *core_document = NULL;
    amark_error_t errors[6];
    bool unowned = false;
    bool ok;
    size_t i;

    SET_COUNTING();

    ok = (create_test_set(&set) == AMARK_ERROR_NONE)
         && (create_test_set(&other) == AMARK_ERROR_NONE)
         && (amark_document_create(set, &document) == AMARK_ERROR_NONE)
         && (amark_document_create(NULL, &core_document) == AMARK_ERROR_NONE);
    if (ok)
    {
        /* The static core set (the one core_document uses), a set the document does
         * not use (a heap one, and the core set for a document with another set),
         * and NULL arguments. */
        errors[0] = amark_document_take_syntax_set(core_document, core);
        errors[1] = amark_document_take_syntax_set(document, other);
        errors[2] = amark_document_take_syntax_set(core_document, set);
        errors[3] = amark_document_take_syntax_set(document, core);
        errors[4] = amark_document_take_syntax_set(NULL, set);
        errors[5] = amark_document_take_syntax_set(document, NULL);
        unowned = (document->owned_syntax == NULL) && (core_document->owned_syntax == NULL)
                  && (amark_document_syntax_set(document) == set)
                  && (amark_document_syntax_set(core_document) == amark_syntax_set_core());
    }

    /* The caller still owns each set: destroying them after the documents frees
     * each block once (the test allocator fails a second free). */
    amark_document_destroy(document);
    amark_document_destroy(core_document);
    amark_syntax_set_destroy(set);
    amark_syntax_set_destroy(other);
    amark_syntax_set_destroy(core);
    amark_allocator_set(NULL);

    NAOS_REQUIRE_TRUE(ok);
    NAOS_CHECK_TRUE(unowned);
    for (i = 0; i < 6; i++)
    {
        NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, errors[i]);
    }

    NAOS_CHECK_UINT_EQ(0, counting.live_blocks);
    NAOS_CHECK_TRUE(amark_syntax_set_is_frozen(amark_syntax_set_core()));
}

NAOS_TEST(a_second_hand_over_is_rejected)
{
    amark_syntax_set_t *set = NULL;
    amark_document_t *document = NULL;
    amark_error_t first = AMARK_ERROR_INVALID_STATE;
    amark_error_t second = AMARK_ERROR_NONE;
    bool still_owned = false;
    bool ok;

    SET_COUNTING();

    ok = (create_test_set(&set) == AMARK_ERROR_NONE)
         && (amark_document_create(set, &document) == AMARK_ERROR_NONE);
    if (ok)
    {
        first = amark_document_take_syntax_set(document, set);
        second = amark_document_take_syntax_set(document, set);
        still_owned = (document->owned_syntax == set);
    }

    /* The document frees the set once (the test allocator fails a second free). */
    amark_document_destroy(document);
    if (first != AMARK_ERROR_NONE)
    {
        amark_syntax_set_destroy(set);
    }

    amark_allocator_set(NULL);

    NAOS_CHECK_TRUE(ok);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, first);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, second);
    NAOS_CHECK_TRUE(still_owned);
    NAOS_CHECK_UINT_EQ(0, counting.live_blocks);
}

NAOS_TEST(a_set_shared_with_another_document_outlives_it_when_owned)
{
    amark_syntax_set_t *set = NULL;
    amark_document_t *owner = NULL;
    amark_document_t *user = NULL;
    amark_node_t *node = NULL;
    amark_error_t taken = AMARK_ERROR_INVALID_STATE;
    bool ok;

    reader_deinits = 0;
    reader_lookups_found = 0;
    SET_COUNTING();

    /* Another document that uses the set is destroyed before the owner (the rule of
     * amark/document.h): the deinit of its node still finds the set. */
    ok = (create_reader_set(&set) == AMARK_ERROR_NONE)
         && (amark_document_create(set, &owner) == AMARK_ERROR_NONE)
         && (amark_document_create(set, &user) == AMARK_ERROR_NONE)
         && (amark_node_create(user, 256, &node) == AMARK_ERROR_NONE);
    if (ok)
    {
        taken = amark_document_take_syntax_set(owner, set);
    }

    amark_document_destroy(user);
    amark_document_destroy(owner);
    if (taken != AMARK_ERROR_NONE)
    {
        amark_syntax_set_destroy(set);
    }

    amark_allocator_set(NULL);

    NAOS_CHECK_TRUE(ok);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, taken);
    NAOS_CHECK_UINT_EQ(1, reader_lookups_found);
    NAOS_CHECK_UINT_EQ(0, counting.live_blocks);
}

NAOS_SUITE(ownership,
    NAOS_SET_TEARDOWN(restore),
    NAOS_ADD_TEST(a_set_handed_to_the_document_is_freed_with_it),
    NAOS_ADD_TEST(the_owned_set_is_freed_after_the_deinit_of_every_node),
    NAOS_ADD_TEST(a_rejected_hand_over_leaves_the_set_to_the_caller),
    NAOS_ADD_TEST(a_second_hand_over_is_rejected),
    NAOS_ADD_TEST(a_set_shared_with_another_document_outlives_it_when_owned))

NAOS_MAIN(
    NAOS_ADD_SUITE(document),
    NAOS_ADD_SUITE(node),
    NAOS_ADD_SUITE(source),
    NAOS_ADD_SUITE(ownership)
)
