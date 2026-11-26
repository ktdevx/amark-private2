/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The tests of NodeCore (structural editing) (syntax-tree): the insertion before,
 * after, as the first and as the last child, the moves within the tree, the
 * replacement, the unlinking, the invalid arguments that leave the tree
 * unchanged (containment, the full link-in-link rule, cycles, the document node,
 * the nodes of another document), the change notification (the invalidation of
 * the caches up to an ancestor already invalid and the structure generation) and
 * the complexity of the insertions (1.5, 5.2-5.5, 14.5).
 */
#include "tree/tree.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <naos/naos.h>

/* ---- Fixture -------------------------------------------------------------- */

/*
 * The tree of the fixture:
 *
 *   root
 *   +- para1 (paragraph)
 *   |  +- t1 (text)
 *   |  +- link1 (link)
 *   |  |  +- emph1 (emph)
 *   |  |     +- t2 (text)
 *   |  +- image1 (image)
 *   +- quote (block_quote)
 *   |  +- para2 (paragraph)
 *   |     +- t3 (text)
 *   +- list
 *      +- item
 *         +- para3 (paragraph)
 *
 * and, outside the tree: dlink (link) with a child dtext (text), demph (emph)
 * with a child dlink2 (link), dpara (paragraph). other is a second document with
 * a paragraph other_para in its tree.
 */
typedef struct fixture
{
    amark_document_t *document;
    amark_node_t *root;
    amark_node_t *para1;
    amark_node_t *t1;
    amark_node_t *link1;
    amark_node_t *emph1;
    amark_node_t *t2;
    amark_node_t *image1;
    amark_node_t *quote;
    amark_node_t *para2;
    amark_node_t *t3;
    amark_node_t *list;
    amark_node_t *item;
    amark_node_t *para3;
    amark_node_t *dlink;
    amark_node_t *dtext;
    amark_node_t *demph;
    amark_node_t *dlink2;
    amark_node_t *dpara;
    amark_document_t *other;
    amark_node_t *other_para;
} fixture_t;

#define FIXTURE_NODES 18

/* The nodes of the fixture's document, in a fixed order. */
static void fixture_nodes(const fixture_t *f, amark_node_t **nodes)
{
    nodes[0] = f->root;
    nodes[1] = f->para1;
    nodes[2] = f->t1;
    nodes[3] = f->link1;
    nodes[4] = f->emph1;
    nodes[5] = f->t2;
    nodes[6] = f->image1;
    nodes[7] = f->quote;
    nodes[8] = f->para2;
    nodes[9] = f->t3;
    nodes[10] = f->list;
    nodes[11] = f->item;
    nodes[12] = f->para3;
    nodes[13] = f->dlink;
    nodes[14] = f->dtext;
    nodes[15] = f->demph;
    nodes[16] = f->dlink2;
    nodes[17] = f->dpara;
}

static amark_node_t *make(amark_document_t *document, amark_node_type_t type)
{
    amark_node_t *node = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, type, &node));
    NAOS_REQUIRE_NOT_NULL(node);
    return node;
}

static void append(amark_node_t *parent, amark_node_t *child)
{
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(parent, child));
}

/* Marks every node of the fixture's document as having a valid cache. */
static void validate_all(fixture_t *f)
{
    amark_node_t *nodes[FIXTURE_NODES];
    size_t i;

    fixture_nodes(f, nodes);
    for (i = 0; i < FIXTURE_NODES; i++)
    {
        nodes[i]->cache_flags |= AMARK_TREE_CACHE_VALID;
    }
}

static void fixture_init(fixture_t *f)
{
    memset(f, 0, sizeof(*f));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &f->document));
    f->root = amark_document_root(f->document);
    f->para1 = make(f->document, AMARK_NODE_PARAGRAPH);
    f->t1 = make(f->document, AMARK_NODE_TEXT);
    f->link1 = make(f->document, AMARK_NODE_LINK);
    f->emph1 = make(f->document, AMARK_NODE_EMPH);
    f->t2 = make(f->document, AMARK_NODE_TEXT);
    f->image1 = make(f->document, AMARK_NODE_IMAGE);
    f->quote = make(f->document, AMARK_NODE_BLOCK_QUOTE);
    f->para2 = make(f->document, AMARK_NODE_PARAGRAPH);
    f->t3 = make(f->document, AMARK_NODE_TEXT);
    f->list = make(f->document, AMARK_NODE_LIST);
    f->item = make(f->document, AMARK_NODE_ITEM);
    f->para3 = make(f->document, AMARK_NODE_PARAGRAPH);
    f->dlink = make(f->document, AMARK_NODE_LINK);
    f->dtext = make(f->document, AMARK_NODE_TEXT);
    f->demph = make(f->document, AMARK_NODE_EMPH);
    f->dlink2 = make(f->document, AMARK_NODE_LINK);
    f->dpara = make(f->document, AMARK_NODE_PARAGRAPH);

    append(f->root, f->para1);
    append(f->para1, f->t1);
    append(f->para1, f->link1);
    append(f->link1, f->emph1);
    append(f->emph1, f->t2);
    append(f->para1, f->image1);
    append(f->root, f->quote);
    append(f->quote, f->para2);
    append(f->para2, f->t3);
    append(f->root, f->list);
    append(f->list, f->item);
    append(f->item, f->para3);
    append(f->dlink, f->dtext);
    append(f->demph, f->dlink2);

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &f->other));
    f->other_para = make(f->other, AMARK_NODE_PARAGRAPH);
    append(amark_document_root(f->other), f->other_para);
    validate_all(f);
}

static void fixture_deinit(fixture_t *f)
{
    amark_document_destroy(f->document);
    amark_document_destroy(f->other);
}

/* The links and the cache flags of every node, and the structure generation. */
typedef struct snapshot
{
    amark_node_t *links[FIXTURE_NODES][5];
    uint32_t flags[FIXTURE_NODES];
    uint64_t generation;
    uint64_t other_generation;
    amark_node_t *other_links[5];
} snapshot_t;

static void take_links(const amark_node_t *node, amark_node_t **links)
{
    links[0] = node->parent;
    links[1] = node->first_child;
    links[2] = node->last_child;
    links[3] = node->prev;
    links[4] = node->next;
}

static void take_snapshot(const fixture_t *f, snapshot_t *s)
{
    amark_node_t *nodes[FIXTURE_NODES];
    size_t i;

    memset(s, 0, sizeof(*s));
    fixture_nodes(f, nodes);
    for (i = 0; i < FIXTURE_NODES; i++)
    {
        take_links(nodes[i], s->links[i]);
        s->flags[i] = nodes[i]->cache_flags;
    }
    s->generation = f->document->structure_generation;
    s->other_generation = f->other->structure_generation;
    take_links(f->other_para, s->other_links);
}

/* Checks that the fixture is as in before. */
static void check_unchanged(const fixture_t *f, const snapshot_t *before)
{
    snapshot_t after;

    take_snapshot(f, &after);
    NAOS_CHECK_INT_EQ(0, memcmp(before, &after, sizeof(after)));
}

/* Checks the symmetry of the links under parent and returns its child count. */
static size_t check_children(const amark_node_t *parent)
{
    const amark_node_t *child;
    const amark_node_t *prev = NULL;
    size_t count = 0;

    for (child = parent->first_child; child != NULL; child = child->next)
    {
        NAOS_CHECK_PTR_EQ(parent, child->parent);
        NAOS_CHECK_PTR_EQ(prev, child->prev);
        prev = child;
        count++;
    }
    NAOS_CHECK_PTR_EQ(prev, parent->last_child);
    return count;
}

static void check_detached(const amark_node_t *node)
{
    NAOS_CHECK_NULL(node->parent);
    NAOS_CHECK_NULL(node->prev);
    NAOS_CHECK_NULL(node->next);
}

/* ---- Insertion -------------------------------------------------------------- */

NAOS_TEST(insert_in_each_position)
{
    fixture_t f;
    amark_node_t *a;
    amark_node_t *b;
    amark_node_t *c;
    amark_node_t *d;

    fixture_init(&f);
    a = make(f.document, AMARK_NODE_PARAGRAPH);
    b = make(f.document, AMARK_NODE_PARAGRAPH);
    c = make(f.document, AMARK_NODE_PARAGRAPH);
    d = make(f.document, AMARK_NODE_PARAGRAPH);

    /* root: para1 quote list */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_prepend_child(f.root, a));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(f.root, b));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_before(f.quote, c));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_after(f.quote, d));

    /* root: a para1 c quote d list b */
    NAOS_CHECK_UINT_EQ(7, check_children(f.root));
    NAOS_CHECK_PTR_EQ(a, amark_node_first_child(f.root));
    NAOS_CHECK_PTR_EQ(f.para1, amark_node_next(a));
    NAOS_CHECK_PTR_EQ(c, amark_node_next(f.para1));
    NAOS_CHECK_PTR_EQ(f.quote, amark_node_next(c));
    NAOS_CHECK_PTR_EQ(d, amark_node_next(f.quote));
    NAOS_CHECK_PTR_EQ(f.list, amark_node_next(d));
    NAOS_CHECK_PTR_EQ(b, amark_node_next(f.list));
    NAOS_CHECK_PTR_EQ(b, amark_node_last_child(f.root));

    /* Insertion at the ends through before and after. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_after(b, f.dpara));
    NAOS_CHECK_PTR_EQ(f.dpara, amark_node_last_child(f.root));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_before(a, f.dpara));
    NAOS_CHECK_PTR_EQ(f.dpara, amark_node_first_child(f.root));
    NAOS_CHECK_UINT_EQ(8, check_children(f.root));

    /* Into an empty parent. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_prepend_child(a, f.dlink));
    NAOS_CHECK_UINT_EQ(1, check_children(a));
    NAOS_CHECK_PTR_EQ(f.dlink, amark_node_first_child(a));
    NAOS_CHECK_PTR_EQ(f.dlink, amark_node_last_child(a));
    /* The subtree comes along. */
    NAOS_CHECK_PTR_EQ(f.dtext, amark_node_first_child(f.dlink));
    fixture_deinit(&f);
}

NAOS_TEST(move_within_the_tree_keeps_the_subtree)
{
    fixture_t f;

    fixture_init(&f);
    /* Move quote (with its subtree) into the item, after para3. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(f.item, f.quote));
    NAOS_CHECK_UINT_EQ(2, check_children(f.root));
    NAOS_CHECK_PTR_EQ(f.list, amark_node_next(f.para1));
    NAOS_CHECK_UINT_EQ(2, check_children(f.item));
    NAOS_CHECK_PTR_EQ(f.quote, amark_node_next(f.para3));
    NAOS_CHECK_PTR_EQ(f.para2, amark_node_first_child(f.quote));
    NAOS_CHECK_PTR_EQ(f.t3, amark_node_first_child(f.para2));

    /* Move among siblings: t1 after image1, then image1 first. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_after(f.image1, f.t1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_prepend_child(f.para1, f.image1));
    NAOS_CHECK_UINT_EQ(3, check_children(f.para1));
    NAOS_CHECK_PTR_EQ(f.image1, amark_node_first_child(f.para1));
    NAOS_CHECK_PTR_EQ(f.link1, amark_node_next(f.image1));
    NAOS_CHECK_PTR_EQ(f.t1, amark_node_next(f.link1));

    /* Moving a node next to itself, or before its next sibling, keeps the order. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_before(f.link1, f.image1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_after(f.link1, f.link1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_before(f.link1, f.link1));
    NAOS_CHECK_UINT_EQ(3, check_children(f.para1));
    NAOS_CHECK_PTR_EQ(f.image1, amark_node_first_child(f.para1));
    NAOS_CHECK_PTR_EQ(f.link1, amark_node_next(f.image1));
    NAOS_CHECK_PTR_EQ(f.t1, amark_node_next(f.link1));

    /* Move a text out of a link into another paragraph. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(f.para3, f.t2));
    NAOS_CHECK_UINT_EQ(0, check_children(f.emph1));
    NAOS_CHECK_PTR_EQ(f.t2, amark_node_first_child(f.para3));
    fixture_deinit(&f);
}

/* ---- Replacement and unlinking ----------------------------------------------- */

NAOS_TEST(replace_puts_the_new_node_in_place)
{
    fixture_t f;

    fixture_init(&f);
    /* A detached node replaces quote; quote is detached with its subtree. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_replace(f.quote, f.dpara));
    NAOS_CHECK_UINT_EQ(3, check_children(f.root));
    NAOS_CHECK_PTR_EQ(f.dpara, amark_node_next(f.para1));
    NAOS_CHECK_PTR_EQ(f.list, amark_node_next(f.dpara));
    check_detached(f.quote);
    NAOS_CHECK_PTR_EQ(f.para2, amark_node_first_child(f.quote));

    /* A node in the tree replaces another: it moves. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_replace(f.para1, f.para3));
    NAOS_CHECK_UINT_EQ(3, check_children(f.root));
    NAOS_CHECK_PTR_EQ(f.para3, amark_node_first_child(f.root));
    NAOS_CHECK_UINT_EQ(0, check_children(f.item));
    check_detached(f.para1);

    /* A node replaces its own ancestor. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(f.root, f.quote));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_replace(f.quote, f.para2));
    NAOS_CHECK_PTR_EQ(f.para2, amark_node_last_child(f.root));
    NAOS_CHECK_UINT_EQ(0, check_children(f.quote));
    check_detached(f.quote);
    NAOS_CHECK_PTR_EQ(f.t3, amark_node_first_child(f.para2));

    /* A sibling replaces its neighbour. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_replace(f.link1, f.image1));
    NAOS_CHECK_UINT_EQ(2, check_children(f.para1));
    NAOS_CHECK_PTR_EQ(f.image1, amark_node_next(f.t1));
    check_detached(f.link1);

    /* Replacing a node by itself changes nothing. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_replace(f.t1, f.t1));
    NAOS_CHECK_PTR_EQ(f.t1, amark_node_first_child(f.para1));
    fixture_deinit(&f);
}

NAOS_TEST(unlinked_node_can_be_inserted_again)
{
    fixture_t f;

    fixture_init(&f);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_unlink(f.quote));
    check_detached(f.quote);
    NAOS_CHECK_UINT_EQ(2, check_children(f.root));
    /* It stays in the document with its subtree. */
    NAOS_CHECK_PTR_EQ(f.document, amark_node_document(f.quote));
    NAOS_CHECK_PTR_EQ(f.para2, amark_node_first_child(f.quote));
    NAOS_CHECK_UINT_EQ(1, check_children(f.quote));

    /* Unlinking a detached node does nothing. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_unlink(f.quote));
    check_detached(f.quote);

    /* Inserted again elsewhere. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(f.item, f.quote));
    NAOS_CHECK_PTR_EQ(f.item, amark_node_parent(f.quote));
    NAOS_CHECK_UINT_EQ(2, check_children(f.item));

    /* The first, the last and the only child. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_unlink(f.t1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_unlink(f.image1));
    NAOS_CHECK_UINT_EQ(1, check_children(f.para1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_unlink(f.link1));
    NAOS_CHECK_UINT_EQ(0, check_children(f.para1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(f.para1, f.link1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_before(f.link1, f.t1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_after(f.link1, f.image1));
    NAOS_CHECK_UINT_EQ(3, check_children(f.para1));
    NAOS_CHECK_PTR_EQ(f.t2, amark_node_first_child(f.emph1));

    /* A destroyed node leaves its parent consistent. */
    amark_node_destroy(f.link1);
    NAOS_CHECK_UINT_EQ(2, check_children(f.para1));
    fixture_deinit(&f);
}

/* ---- Invalid arguments ----------------------------------------------------- */

NAOS_TEST(null_arguments_are_rejected)
{
    fixture_t f;
    snapshot_t before;

    fixture_init(&f);
    take_snapshot(&f, &before);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_unlink(NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_insert_before(NULL, f.dpara));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_insert_before(f.quote, NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_insert_after(NULL, f.dpara));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_insert_after(f.quote, NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_prepend_child(NULL, f.dpara));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_prepend_child(f.root, NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_append_child(NULL, f.dpara));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_append_child(f.root, NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_replace(NULL, f.dpara));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_replace(f.quote, NULL));
    check_unchanged(&f, &before);
    fixture_deinit(&f);
}

NAOS_TEST(containment_violations_are_rejected)
{
    fixture_t f;
    snapshot_t before;

    fixture_init(&f);
    take_snapshot(&f, &before);
    /* A block where only items go, an inline among blocks, a block among inlines. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_append_child(f.list, f.dpara));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_insert_before(f.item, f.dpara));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_prepend_child(f.root, f.t1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_insert_after(f.quote, f.dtext));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_append_child(f.para1, f.dpara));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_insert_after(f.t1, f.quote));
    /* An item outside a list; children under a leaf. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_append_child(f.quote, f.item));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_append_child(f.t1, f.dtext));
    /* Replacements that break the rule of the parent. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_replace(f.item, f.dpara));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_replace(f.t1, f.para3));
    check_unchanged(&f, &before);
    fixture_deinit(&f);
}

NAOS_TEST(link_in_link_is_rejected_at_any_depth)
{
    fixture_t f;
    snapshot_t before;
    amark_node_t *image;
    amark_node_t *strong;

    fixture_init(&f);
    take_snapshot(&f, &before);
    /* A link directly under a link (the can_contain hook). */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_append_child(f.link1, f.dlink));
    /* A link under an emph under a link (the ancestors of the parent). */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_append_child(f.emph1, f.dlink));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_insert_after(f.t2, f.dlink));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_replace(f.t2, f.dlink));
    /* An emph that holds a link, under a link (the descendants of the node). */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_append_child(f.link1, f.demph));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_prepend_child(f.emph1, f.demph));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_replace(f.t2, f.demph));
    /* Under a detached link: an emph holding a link, and a link of the tree. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_append_child(f.dlink, f.demph));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_insert_after(f.dtext, f.link1));
    check_unchanged(&f, &before);

    /* An image is not a link: a link may go in an image, and an image in a link. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(f.image1, f.dlink));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_unlink(f.dlink));
    /* But not a link in an image in a link. */
    image = make(f.document, AMARK_NODE_IMAGE);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(f.emph1, image));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_append_child(image, f.dlink));
    /* Deeper: a strong holding an emph holding a link. */
    strong = make(f.document, AMARK_NODE_STRONG);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(strong, f.demph));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_append_child(image, strong));
    NAOS_CHECK_PTR_EQ(NULL, amark_node_parent(strong));
    /* Without the link, the same subtree is accepted. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_unlink(f.dlink2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(image, strong));
    fixture_deinit(&f);
}

NAOS_TEST(insertion_under_own_descendant_is_rejected)
{
    fixture_t f;
    snapshot_t before;

    fixture_init(&f);
    take_snapshot(&f, &before);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_append_child(f.quote, f.quote));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_append_child(f.para2, f.quote));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_insert_before(f.t2, f.link1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_insert_after(f.para3, f.list));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_prepend_child(f.emph1, f.link1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_replace(f.t2, f.emph1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_replace(f.para3, f.item));
    /* Also in a detached subtree. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_insert_before(f.dlink2, f.demph));
    check_unchanged(&f, &before);
    fixture_deinit(&f);
}

NAOS_TEST(the_document_node_cannot_move)
{
    fixture_t f;
    snapshot_t before;

    fixture_init(&f);
    take_snapshot(&f, &before);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_unlink(f.root));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_append_child(f.quote, f.root));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_insert_after(f.para1, f.root));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_replace(f.para1, f.root));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_replace(f.root, f.dpara));
    /* The root has no siblings. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_insert_before(f.root, f.dpara));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_insert_after(f.root, f.dpara));
    /* Nor does a detached node, which has no position to replace. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_insert_before(f.dpara, f.para3));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_insert_after(f.dlink, f.t1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_replace(f.dpara, f.para3));
    check_unchanged(&f, &before);
    fixture_deinit(&f);
}

NAOS_TEST(nodes_of_another_document_are_rejected)
{
    fixture_t f;
    snapshot_t before;
    amark_node_t *other_root;

    fixture_init(&f);
    other_root = amark_document_root(f.other);
    take_snapshot(&f, &before);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_append_child(f.root, f.other_para));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_prepend_child(other_root, f.dpara));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_insert_before(f.quote, f.other_para));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_insert_after(f.other_para, f.dpara));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_replace(f.quote, f.other_para));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_replace(f.other_para, f.dpara));
    check_unchanged(&f, &before);
    NAOS_CHECK_PTR_EQ(f.other_para, amark_node_first_child(other_root));
    fixture_deinit(&f);
}

/* ---- Change notification ---------------------------------------------------- */

static int is_valid(const amark_node_t *node)
{
    return (node->cache_flags & AMARK_TREE_CACHE_VALID) != 0;
}

NAOS_TEST(notification_invalidates_up_to_an_invalid_ancestor)
{
    fixture_t f;
    uint64_t generation;
    uint64_t other_generation;
    size_t steps;

    fixture_init(&f);
    generation = f.document->structure_generation;
    other_generation = f.other->structure_generation;

    /* A change of records: t2 and its ancestors, no new generation. */
    steps = f.document->stats.invalidation_steps;
    amark_tree_changed(f.t2, AMARK_TREE_CHANGE_RECORD);
    NAOS_CHECK_FALSE(is_valid(f.t2));
    NAOS_CHECK_FALSE(is_valid(f.emph1));
    NAOS_CHECK_FALSE(is_valid(f.link1));
    NAOS_CHECK_FALSE(is_valid(f.para1));
    NAOS_CHECK_FALSE(is_valid(f.root));
    NAOS_CHECK_UINT_EQ(5, f.document->stats.invalidation_steps - steps);
    NAOS_CHECK_UINT_EQ(generation, f.document->structure_generation);
    /* Siblings and other branches keep their caches. */
    NAOS_CHECK_TRUE(is_valid(f.t1));
    NAOS_CHECK_TRUE(is_valid(f.image1));
    NAOS_CHECK_TRUE(is_valid(f.quote));
    NAOS_CHECK_TRUE(is_valid(f.para2));

    /* The walk stops at the first ancestor already invalid. */
    steps = f.document->stats.invalidation_steps;
    amark_tree_changed(f.t1, AMARK_TREE_CHANGE_RECORD);
    NAOS_CHECK_FALSE(is_valid(f.t1));
    NAOS_CHECK_UINT_EQ(2, f.document->stats.invalidation_steps - steps);
    steps = f.document->stats.invalidation_steps;
    amark_tree_changed(f.t2, AMARK_TREE_CHANGE_RECORD);
    NAOS_CHECK_UINT_EQ(1, f.document->stats.invalidation_steps - steps);

    /* A structural change advances the generation. */
    amark_tree_changed(f.t3, AMARK_TREE_CHANGE_STRUCTURE);
    NAOS_CHECK_UINT_EQ(generation + 1, f.document->structure_generation);
    NAOS_CHECK_FALSE(is_valid(f.para2));
    NAOS_CHECK_FALSE(is_valid(f.quote));
    NAOS_CHECK_TRUE(is_valid(f.para3));
    NAOS_CHECK_UINT_EQ(other_generation, f.other->structure_generation);
    fixture_deinit(&f);
}

NAOS_TEST(structural_edits_notify_the_old_and_new_places)
{
    fixture_t f;
    uint64_t generation;

    fixture_init(&f);
    generation = f.document->structure_generation;

    /* Moving t3 into para3: both branches are invalid up to the root. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(f.para3, f.t3));
    NAOS_CHECK_TRUE(f.document->structure_generation > generation);
    NAOS_CHECK_FALSE(is_valid(f.para2));
    NAOS_CHECK_FALSE(is_valid(f.quote));
    NAOS_CHECK_FALSE(is_valid(f.para3));
    NAOS_CHECK_FALSE(is_valid(f.item));
    NAOS_CHECK_FALSE(is_valid(f.list));
    NAOS_CHECK_FALSE(is_valid(f.root));
    NAOS_CHECK_TRUE(is_valid(f.para1));

    /* Unlinking notifies the old parent. */
    validate_all(&f);
    generation = f.document->structure_generation;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_unlink(f.t2));
    NAOS_CHECK_TRUE(f.document->structure_generation > generation);
    NAOS_CHECK_FALSE(is_valid(f.emph1));
    NAOS_CHECK_FALSE(is_valid(f.link1));
    NAOS_CHECK_FALSE(is_valid(f.root));
    NAOS_CHECK_TRUE(is_valid(f.quote));

    /* Replacing notifies the position. */
    validate_all(&f);
    generation = f.document->structure_generation;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_replace(f.para2, f.dpara));
    NAOS_CHECK_TRUE(f.document->structure_generation > generation);
    NAOS_CHECK_FALSE(is_valid(f.quote));
    NAOS_CHECK_FALSE(is_valid(f.root));
    NAOS_CHECK_TRUE(is_valid(f.list));

    /* Destroying a node of the tree notifies its parent. */
    validate_all(&f);
    generation = f.document->structure_generation;
    amark_node_destroy(f.para3);
    NAOS_CHECK_TRUE(f.document->structure_generation > generation);
    NAOS_CHECK_FALSE(is_valid(f.item));
    NAOS_CHECK_FALSE(is_valid(f.root));
    NAOS_CHECK_TRUE(is_valid(f.quote));
    fixture_deinit(&f);
}

/* ---- Complexity (14.5) -------------------------------------------------------- */

/* Builds under root a chain of depth block quotes and returns the deepest; adds
 * width paragraphs under root first so that the document is large. */
static amark_node_t *build_chain(amark_document_t *document, size_t depth, size_t width)
{
    amark_node_t *root = amark_document_root(document);
    amark_node_t *parent = root;
    size_t i;

    for (i = 0; i < width; i++)
    {
        append(root, make(document, AMARK_NODE_PARAGRAPH));
    }
    for (i = 0; i < depth; i++)
    {
        amark_node_t *quote = make(document, AMARK_NODE_BLOCK_QUOTE);

        append(parent, quote);
        parent = quote;
    }
    return parent;
}

/* The steps of inserting a new paragraph as the last child of parent. */
static size_t insertion_steps(amark_document_t *document, amark_node_t *parent)
{
    amark_node_t *node = make(document, AMARK_NODE_PARAGRAPH);
    size_t before = document->stats.structure_steps + document->stats.invalidation_steps;

    append(parent, node);
    return document->stats.structure_steps + document->stats.invalidation_steps - before;
}

NAOS_TEST(insertion_time_is_proportional_to_the_depth)
{
    amark_document_t *small = NULL;
    amark_document_t *large = NULL;
    amark_node_t *small_deep;
    amark_node_t *large_deep;
    amark_node_t *large_shallow;
    size_t small_steps;
    size_t large_steps;
    size_t shallow_steps;
    size_t move_steps;
    size_t before;
    amark_node_t *moving;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &small));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &large));
    small_deep = build_chain(small, 100, 10);
    large_deep = build_chain(large, 100, 20000);
    large_shallow = amark_document_root(large);

    /* The same depth costs the same, whatever the number of nodes and siblings. */
    small_steps = insertion_steps(small, small_deep);
    large_steps = insertion_steps(large, large_deep);
    NAOS_CHECK_UINT_EQ(small_steps, large_steps);
    /* Proportional to the depth: at most a few steps per level. */
    NAOS_CHECK_UINT_GE(large_steps, 100);
    NAOS_CHECK_UINT_LE(large_steps, 3 * 101 + 4);
    /* Near the root, among 20000 siblings, a few steps. */
    shallow_steps = insertion_steps(large, large_shallow);
    NAOS_CHECK_UINT_LE(shallow_steps, 8);

    /* A move costs the depths of both places. */
    moving = amark_node_last_child(large_deep);
    before = large->stats.structure_steps + large->stats.invalidation_steps;
    append(large_shallow, moving);
    move_steps = large->stats.structure_steps + large->stats.invalidation_steps - before;
    NAOS_CHECK_UINT_LE(move_steps, 3 * 101 + 8);

    amark_document_destroy(small);
    amark_document_destroy(large);
}

NAOS_SUITE(structure,
    NAOS_ADD_TEST(insert_in_each_position),
    NAOS_ADD_TEST(move_within_the_tree_keeps_the_subtree),
    NAOS_ADD_TEST(replace_puts_the_new_node_in_place),
    NAOS_ADD_TEST(unlinked_node_can_be_inserted_again),
    NAOS_ADD_TEST(null_arguments_are_rejected),
    NAOS_ADD_TEST(containment_violations_are_rejected),
    NAOS_ADD_TEST(link_in_link_is_rejected_at_any_depth),
    NAOS_ADD_TEST(insertion_under_own_descendant_is_rejected),
    NAOS_ADD_TEST(the_document_node_cannot_move),
    NAOS_ADD_TEST(nodes_of_another_document_are_rejected),
    NAOS_ADD_TEST(notification_invalidates_up_to_an_invalid_ancestor),
    NAOS_ADD_TEST(structural_edits_notify_the_old_and_new_places),
    NAOS_ADD_TEST(insertion_time_is_proportional_to_the_depth)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(structure)
)
