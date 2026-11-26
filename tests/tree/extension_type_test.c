/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * Extension types for the tests, from the registry to editing, comparison and the
 * dump, through the public API.
 *
 * The extension t has a table-like t.table (block, its type data owns a caption
 * string and holds a number of columns, with init/deinit/copy/equal/dump, and a
 * can_contain that accepts only t.row), t.row (data_size 0 with
 * init/deinit/copy/equal, and a can_contain that accepts only t.cell), t.cell (a
 * can_contain that accepts the inline types but the soft break) and t.mark (an
 * inline line literal with a validate_literal), and slots of a kind of the
 * application's range (AMARK_SLOT_KIND_USER_FIRST and above). The extension u has
 * u.note and a slot of the same kind.
 *
 * Suite extension_registry (2.5, 8.1, 8.8): the identifiers of the types in the
 * order of the set, the lookups both ways, the built-in types described the same
 * way, the slots of a user kind in the order of the set with their extensions.
 * Suite extension_nodes (7.9, 8.12, 4.8): creation, the containment overrides, the
 * type data set with amark_node_mark_edited, the literal check, the inline text of
 * a type that rejects soft breaks, the copy, the import into a set that has the
 * types (other identifiers) and into one that lacks them, the comparison, the dump
 * and the destruction, with the calls of each function counted and no block left
 * (test allocator); the copy function of a type without data is called as init
 * and deinit are.
 * Suite extension_round_trip (9.6): the dump of a tree of extension types equals
 * the dump of the tree built from it by amark_test_tree_build with the attribute
 * function, also in the other syntax set; an attribute the function rejects fails
 * the building.
 */
#include <amark/allocator.h>
#include <amark/buffer.h>
#include <amark/document.h>
#include <amark/dump.h>
#include <amark/error.h>
#include <amark/extension.h>
#include <amark/node.h>
#include <amark/token.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <naos/naos.h>

#include "test_allocator.h"
#include "tree_builder.h"

/* --- The calls of the function tables -------------------------------------------- */

typedef struct calls
{
    size_t table_init;
    size_t table_deinit;
    size_t table_copy;
    /* The calls of the copy function of t.table that succeeded. */
    size_t table_copied;
    size_t table_equal;
    size_t table_dump;
    size_t table_can_contain;
    size_t row_init;
    size_t row_deinit;
    size_t row_copy;
    size_t row_equal;
    size_t row_can_contain;
    /* The calls of the functions of t.row that got a data pointer (data_size 0
     * gives NULL). */
    size_t row_with_data;
    size_t cell_can_contain;
    size_t mark_validate;
} calls_t;

static calls_t calls;

/* What the equal function of t.row answers. */
static bool row_equal_answer = true;

NAOS_SETUP(reset)
{
    memset(&calls, 0, sizeof(calls));
    row_equal_answer = true;
}

NAOS_TEARDOWN(restore)
{
    amark_allocator_set(NULL);
}

/* --- t.table ---------------------------------------------------------------------- */

/* The type data of t.table: a caption it owns (never NULL once initialized) and a
 * number of columns. */
typedef struct table_data
{
    char *caption;
    size_t caption_length;
    long long columns;
} table_data_t;

/* Copies text[0..length) into a new NUL-terminated block. */
static char *duplicate(const char *text, size_t length)
{
    char *copy = amark_malloc(length + 1);

    if (copy != NULL)
    {
        if (length != 0)
        {
            memcpy(copy, text, length);
        }

        copy[length] = '\0';
    }

    return copy;
}

static amark_error_t table_init(amark_node_t *node, void *data)
{
    table_data_t *table = data;

    (void)node;
    calls.table_init++;
    table->caption = duplicate("", 0);

    return (table->caption != NULL) ? AMARK_ERROR_NONE : AMARK_ERROR_OUT_OF_MEMORY;
}

static void table_deinit(amark_node_t *node, void *data)
{
    (void)node;
    calls.table_deinit++;
    amark_free(((table_data_t *)data)->caption);
}

static amark_error_t table_copy(amark_node_t *dst, void *dst_data, const amark_node_t *src,
                                const void *src_data)
{
    table_data_t *to = dst_data;
    const table_data_t *from = src_data;

    (void)dst;
    (void)src;
    calls.table_copy++;
    to->caption = duplicate(from->caption, from->caption_length);
    if (to->caption == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }

    to->caption_length = from->caption_length;
    to->columns = from->columns;
    calls.table_copied++;

    return AMARK_ERROR_NONE;
}

static bool table_equal(const amark_node_t *a, const void *a_data, const amark_node_t *b,
                        const void *b_data)
{
    const table_data_t *x = a_data;
    const table_data_t *y = b_data;

    (void)a;
    (void)b;
    calls.table_equal++;

    return (x->columns == y->columns) && (x->caption_length == y->caption_length)
           && (memcmp(x->caption, y->caption, x->caption_length) == 0);
}

static amark_error_t table_dump(const amark_node_t *node, const void *data,
                                amark_dump_writer_t *writer)
{
    const table_data_t *table = data;
    amark_error_t err;

    (void)node;
    calls.table_dump++;
    err = amark_dump_attr_string(writer, "caption", table->caption, table->caption_length);
    if (err == AMARK_ERROR_NONE)
    {
        err = amark_dump_attr_int(writer, "columns", table->columns);
    }

    return err;
}

static bool table_can_contain(const amark_node_type_desc_t *self,
                              const amark_node_type_desc_t *child);

static const amark_node_type_desc_t table_type = {
    sizeof(amark_node_type_desc_t), "t.table", AMARK_NODE_LEVEL_BLOCK, AMARK_CONTENT_BLOCK, 0,
    AMARK_LITERAL_NONE, sizeof(table_data_t), table_can_contain, NULL, table_init,
    table_deinit, table_copy, table_equal, table_dump, NULL
};

/* --- t.row, t.cell, t.mark, u.note -------------------------------------------------- */

static void count_row_data(const void *data)
{
    if (data != NULL)
    {
        calls.row_with_data++;
    }
}

static amark_error_t row_init(amark_node_t *node, void *data)
{
    (void)node;
    calls.row_init++;
    count_row_data(data);

    return AMARK_ERROR_NONE;
}

static void row_deinit(amark_node_t *node, void *data)
{
    (void)node;
    calls.row_deinit++;
    count_row_data(data);
}

static amark_error_t row_copy(amark_node_t *dst, void *dst_data, const amark_node_t *src,
                              const void *src_data)
{
    (void)dst;
    (void)src;
    calls.row_copy++;
    count_row_data(dst_data);
    count_row_data(src_data);

    return AMARK_ERROR_NONE;
}

static bool row_equal(const amark_node_t *a, const void *a_data, const amark_node_t *b,
                      const void *b_data)
{
    (void)a;
    (void)b;
    calls.row_equal++;
    count_row_data(a_data);
    count_row_data(b_data);

    return row_equal_answer;
}

static bool row_can_contain(const amark_node_type_desc_t *self,
                            const amark_node_type_desc_t *child);

/* A row provides no content class: only the override of t.table accepts it. */
static const amark_node_type_desc_t row_type = {
    sizeof(amark_node_type_desc_t), "t.row", AMARK_NODE_LEVEL_BLOCK, 0, 0, AMARK_LITERAL_NONE,
    0, row_can_contain, NULL, row_init, row_deinit, row_copy, row_equal, NULL, NULL
};

/* Accepts the inline types but the soft break (a cell holds one line). */
static bool cell_can_contain(const amark_node_type_desc_t *self,
                             const amark_node_type_desc_t *child)
{
    (void)self;
    calls.cell_can_contain++;

    return ((child->provides & AMARK_CONTENT_INLINE) != 0)
           && (strcmp(child->name, "softbreak") != 0);
}

static const amark_node_type_desc_t cell_type = {
    sizeof(amark_node_type_desc_t), "t.cell", AMARK_NODE_LEVEL_BLOCK, 0, 0, AMARK_LITERAL_NONE,
    0, cell_can_contain, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

/* Rejects a literal with a '!'. */
static amark_error_t mark_validate(const amark_node_t *node, const char *text, size_t length)
{
    (void)node;
    calls.mark_validate++;

    return (memchr(text, '!', length) == NULL) ? AMARK_ERROR_NONE
                                               : AMARK_ERROR_INVALID_ARGUMENT;
}

static const amark_node_type_desc_t mark_type = {
    sizeof(amark_node_type_desc_t), "t.mark", AMARK_NODE_LEVEL_INLINE, AMARK_CONTENT_INLINE, 0,
    AMARK_LITERAL_LINE, 0, NULL, mark_validate, NULL, NULL, NULL, NULL, NULL, NULL
};

static bool table_can_contain(const amark_node_type_desc_t *self,
                              const amark_node_type_desc_t *child)
{
    (void)self;
    calls.table_can_contain++;

    return (child == &row_type);
}

static bool row_can_contain(const amark_node_type_desc_t *self,
                            const amark_node_type_desc_t *child)
{
    (void)self;
    calls.row_can_contain++;

    return (child == &cell_type);
}

static const amark_node_type_desc_t note_type = {
    sizeof(amark_node_type_desc_t), "u.note", AMARK_NODE_LEVEL_BLOCK, AMARK_CONTENT_BLOCK, 0,
    AMARK_LITERAL_NONE, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

/* --- The extensions t and u ---------------------------------------------------------- */

/* A slot kind of the application's range, and another one. */
#define T_SLOT_KIND     (AMARK_SLOT_KIND_USER_FIRST + 7u)
#define T_OTHER_KIND    AMARK_SLOT_KIND_USER_FIRST

typedef struct t_slot
{
    amark_slot_t base;
    const char *label;
} t_slot_t;

static const t_slot_t t_slot_first = { { sizeof(t_slot_t), T_SLOT_KIND }, "t.first" };
static const t_slot_t t_slot_other = { { sizeof(t_slot_t), T_OTHER_KIND }, "t.other" };
static const t_slot_t t_slot_second = { { sizeof(t_slot_t), T_SLOT_KIND }, "t.second" };
static const t_slot_t u_slot = { { sizeof(t_slot_t), T_SLOT_KIND }, "u.only" };

static const amark_node_type_desc_t *const t_types[] = {
    &table_type, &row_type, &cell_type, &mark_type
};

static const amark_slot_t *const t_slots[] = {
    &t_slot_first.base, &t_slot_other.base, &t_slot_second.base
};

static const amark_extension_t t_extension = {
    sizeof(amark_extension_t), "t", t_types, 4, t_slots, 3, NULL
};

static const amark_node_type_desc_t *const u_types[] = { &note_type };

static const amark_slot_t *const u_slots[] = { &u_slot.base };

static const amark_extension_t u_extension = {
    sizeof(amark_extension_t), "u", u_types, 1, u_slots, 1, NULL
};

/* --- Helpers ------------------------------------------------------------------------- */

/* Creates the frozen syntax set core + first + second (NULL on a failure, which is
 * checked without stopping the test, as the test allocator may be set). */
static amark_syntax_set_t *create_set(const amark_extension_t *first,
                                      const amark_extension_t *second)
{
    amark_syntax_set_t *set = NULL;
    amark_error_t err = amark_syntax_set_create(&set);

    if (err == AMARK_ERROR_NONE)
    {
        err = amark_syntax_set_append(set, first);
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = amark_syntax_set_append(set, second);
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = amark_syntax_set_freeze(set);
    }

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, err);
    if (err != AMARK_ERROR_NONE)
    {
        amark_syntax_set_destroy(set);
        return NULL;
    }

    return set;
}

/* The identifier of the type name in the syntax set of document (AMARK_NODE_NONE
 * when there is none). */
static amark_node_type_t type_of(const amark_document_t *document, const char *name)
{
    amark_node_type_t type = AMARK_NODE_NONE;

    (void)amark_syntax_set_find_node_type(amark_document_syntax_set(document), name, &type);

    return type;
}

/* Creates a node of the type name in document (NULL on a checked failure). */
static amark_node_t *create(amark_document_t *document, const char *name)
{
    amark_node_t *node = NULL;

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_create(document, type_of(document, name), &node));

    return node;
}

/*
 * The setters of t.table, as an extension would write them: setting the same
 * value does nothing; another value changes the type data and calls
 * amark_node_mark_edited.
 */
static amark_error_t table_set_caption(amark_node_t *node, const char *text, size_t length)
{
    table_data_t *table;
    char *caption;

    if (amark_node_desc(node) != &table_type)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    table = amark_node_data(node);
    if ((table->caption_length == length) && (memcmp(table->caption, text, length) == 0))
    {
        return AMARK_ERROR_NONE;
    }

    caption = duplicate(text, length);
    if (caption == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }

    amark_free(table->caption);
    table->caption = caption;
    table->caption_length = length;
    amark_node_mark_edited(node);

    return AMARK_ERROR_NONE;
}

static amark_error_t table_set_columns(amark_node_t *node, long long columns)
{
    table_data_t *table;

    if (amark_node_desc(node) != &table_type)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    table = amark_node_data(node);
    if (table->columns != columns)
    {
        table->columns = columns;
        amark_node_mark_edited(node);
    }

    return AMARK_ERROR_NONE;
}

/* The caption of a t.table. */
static const char *table_caption(const amark_node_t *node)
{
    const table_data_t *table = amark_node_data_const(node);

    return (table != NULL) ? table->caption : NULL;
}

/* The attribute function of amark_test_tree_build: :caption and :columns of a
 * t.table. */
static amark_error_t table_attr(amark_node_t *node, const char *name, const char *value,
                                size_t length, void *context)
{
    (void)context;
    if (amark_node_desc(node) != &table_type)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    if (strcmp(name, "caption") == 0)
    {
        return table_set_caption(node, value, length);
    }

    if (strcmp(name, "columns") == 0)
    {
        return table_set_columns(node, strtoll(value, NULL, 10));
    }

    return AMARK_ERROR_INVALID_ARGUMENT;
}

/* Appends child to parent and checks that it succeeds. */
#define APPEND(parent, child) \
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child((parent), (child)))

/* Checks that appending child to parent is rejected and leaves parent without a
 * new last child. */
#define REJECT(parent, child) \
    do { \
        amark_node_t *reject_last_ = amark_node_last_child(parent); \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, \
                          amark_node_append_child((parent), (child))); \
        NAOS_CHECK_PTR_EQ(reject_last_, amark_node_last_child(parent)); \
        NAOS_CHECK_NULL(amark_node_parent(child)); \
    } while (0)

/*
 * Builds in document (core + t + u), with edit tracking off, the sample
 * (t.table :caption "Fruits" :columns 2 (t.row (t.cell (text "apple") (t.mark
 * "ok")))) under the root, with the token (text 0 5) of the source "apple" on the
 * text. Returns the table (NULL on a checked failure).
 */
static amark_node_t *build_sample(amark_document_t *document)
{
    amark_node_t *table = create(document, "t.table");
    amark_node_t *row = create(document, "t.row");
    amark_node_t *cell = create(document, "t.cell");
    amark_node_t *mark = create(document, "t.mark");
    amark_token_t token;
    amark_source_id_t source = 0;

    if ((table == NULL) || (row == NULL) || (cell == NULL) || (mark == NULL))
    {
        amark_node_destroy(table);
        amark_node_destroy(row);
        amark_node_destroy(cell);
        amark_node_destroy(mark);
        return NULL;
    }

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, table_set_caption(table, "Fruits", 6));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, table_set_columns(table, 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_inline_text(cell, "apple", 5));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_literal(mark, "ok", 2));
    APPEND(cell, mark);
    APPEND(row, cell);
    APPEND(table, row);
    APPEND(amark_document_root(document), table);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_add_source(document, "apple", 5, &source));
    memset(&token, 0, sizeof(token));
    token.struct_size = sizeof(token);
    token.kind = AMARK_TOKEN_TEXT;
    token.source = source;
    token.offset = 0;
    token.length = 5;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_add_token(amark_node_first_child(cell), &token));

    return table;
}

/* The dump of the root of the sample (build_sample). */
#define SAMPLE_DUMP \
    "(document\n" \
    "  (t.table :caption \"Fruits\" :columns 2\n" \
    "    (t.row\n" \
    "      (t.cell\n" \
    "        (text \"apple\")\n" \
    "        (t.mark \"ok\")))))\n"

/* Writes the dump of node with flags into *out (a new buffer; NULL on a checked
 * failure). */
static amark_buffer_t *dump_of(const amark_node_t *node, uint32_t flags)
{
    amark_buffer_t *out = NULL;
    amark_error_t err = amark_buffer_create(&out);

    if (err == AMARK_ERROR_NONE)
    {
        err = amark_node_dump(node, flags, out);
    }

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, err);
    if (err != AMARK_ERROR_NONE)
    {
        amark_buffer_destroy(out);
        return NULL;
    }

    return out;
}

/* --- Suite extension_registry ------------------------------------------------------ */

NAOS_TEST(the_types_get_identifiers_in_the_order_of_the_set)
{
    amark_syntax_set_t *tu = create_set(&t_extension, &u_extension);
    amark_syntax_set_t *ut = create_set(&u_extension, &t_extension);
    static const amark_node_type_desc_t *const tu_order[] = {
        &table_type, &row_type, &cell_type, &mark_type, &note_type
    };
    static const amark_node_type_desc_t *const ut_order[] = {
        &note_type, &table_type, &row_type, &cell_type, &mark_type
    };
    amark_node_type_t type;
    size_t i;

    NAOS_REQUIRE_NOT_NULL(tu);
    NAOS_REQUIRE_NOT_NULL(ut);

    for (i = 0; i < 5; i++)
    {
        amark_node_type_t expected = AMARK_NODE_DYNAMIC_FIRST + (amark_node_type_t)i;

        type = AMARK_NODE_NONE;
        NAOS_CHECK_TRUE(amark_syntax_set_find_node_type(tu, tu_order[i]->name, &type));
        NAOS_CHECK_UINT_EQ(expected, type);
        type = AMARK_NODE_NONE;
        NAOS_CHECK_TRUE(amark_syntax_set_node_type_of(tu, tu_order[i], &type));
        NAOS_CHECK_UINT_EQ(expected, type);
        NAOS_CHECK_PTR_EQ(tu_order[i], amark_syntax_set_node_type_desc(tu, expected));

        type = AMARK_NODE_NONE;
        NAOS_CHECK_TRUE(amark_syntax_set_find_node_type(ut, ut_order[i]->name, &type));
        NAOS_CHECK_UINT_EQ(expected, type);
        NAOS_CHECK_PTR_EQ(ut_order[i], amark_syntax_set_node_type_desc(ut, expected));
    }

    NAOS_CHECK_NULL(amark_syntax_set_node_type_desc(tu, AMARK_NODE_DYNAMIC_FIRST + 5u));
    NAOS_CHECK_FALSE(amark_syntax_set_find_node_type(amark_syntax_set_core(), "t.table", &type));

    amark_syntax_set_destroy(tu);
    amark_syntax_set_destroy(ut);
}

NAOS_TEST(the_builtin_types_are_described_like_the_extension_types)
{
    amark_syntax_set_t *set = create_set(&t_extension, &u_extension);
    const amark_extension_t *core;
    const amark_node_type_desc_t *paragraph;
    size_t i;
    bool found = false;

    NAOS_REQUIRE_NOT_NULL(set);

    /* amark.core is the first extension and holds the built-in descriptors, which
     * are found the same way as those of t. */
    core = amark_syntax_set_extension_at(set, 0);
    NAOS_REQUIRE_NOT_NULL(core);
    NAOS_CHECK_STR_EQ("amark.core", core->name);
    paragraph = amark_syntax_set_node_type_desc(set, AMARK_NODE_PARAGRAPH);
    NAOS_REQUIRE_NOT_NULL(paragraph);
    NAOS_CHECK_UINT_EQ(sizeof(amark_node_type_desc_t), paragraph->struct_size);
    NAOS_CHECK_STR_EQ("paragraph", paragraph->name);
    for (i = 0; i < core->node_type_count; i++)
    {
        if (core->node_types[i] == paragraph)
        {
            found = true;
        }
    }

    NAOS_CHECK_TRUE(found);
    NAOS_CHECK_PTR_EQ(&t_extension, amark_syntax_set_find_extension(set, "t"));
    NAOS_CHECK_PTR_EQ(&u_extension, amark_syntax_set_extension_at(set, 2));

    amark_syntax_set_destroy(set);
}

/* Checks the slots of kind T_SLOT_KIND of set: the labels and extensions in
 * order. */
static void check_slots(const amark_syntax_set_t *set, const t_slot_t *const *slots,
                        const amark_extension_t *const *extensions, size_t count)
{
    size_t i;

    NAOS_CHECK_UINT_EQ(count, amark_syntax_set_slot_count(set, T_SLOT_KIND));
    for (i = 0; i < count; i++)
    {
        const amark_extension_t *extension = NULL;
        const amark_slot_t *slot = amark_syntax_set_slot_at(set, T_SLOT_KIND, i, &extension);

        NAOS_CHECK_PTR_EQ(&slots[i]->base, slot);
        NAOS_CHECK_PTR_EQ(extensions[i], extension);
        if (slot != NULL)
        {
            NAOS_CHECK_UINT_EQ(T_SLOT_KIND, slot->kind);
            NAOS_CHECK_UINT_EQ(sizeof(t_slot_t), slot->struct_size);
            NAOS_CHECK_STR_EQ(slots[i]->label, ((const t_slot_t *)slot)->label);
        }
    }

    NAOS_CHECK_NULL(amark_syntax_set_slot_at(set, T_SLOT_KIND, count, NULL));
}

NAOS_TEST(the_slots_of_a_user_kind_come_in_the_order_of_the_set)
{
    amark_syntax_set_t *tu = create_set(&t_extension, &u_extension);
    amark_syntax_set_t *ut = create_set(&u_extension, &t_extension);
    static const t_slot_t *const tu_slots[] = { &t_slot_first, &t_slot_second, &u_slot };
    static const amark_extension_t *const tu_extensions[] = {
        &t_extension, &t_extension, &u_extension
    };
    static const t_slot_t *const ut_slots[] = { &u_slot, &t_slot_first, &t_slot_second };
    static const amark_extension_t *const ut_extensions[] = {
        &u_extension, &t_extension, &t_extension
    };
    const amark_extension_t *extension = NULL;

    NAOS_REQUIRE_NOT_NULL(tu);
    NAOS_REQUIRE_NOT_NULL(ut);
    NAOS_CHECK_TRUE(T_SLOT_KIND >= AMARK_SLOT_KIND_USER_FIRST);
    NAOS_CHECK_TRUE(T_OTHER_KIND > AMARK_SLOT_KIND_AMARK_LAST);

    check_slots(tu, tu_slots, tu_extensions, 3);
    check_slots(ut, ut_slots, ut_extensions, 3);

    /* The other kind has its own slot; an unused kind has none. */
    NAOS_CHECK_UINT_EQ(1, amark_syntax_set_slot_count(tu, T_OTHER_KIND));
    NAOS_CHECK_PTR_EQ(&t_slot_other.base,
                      amark_syntax_set_slot_at(tu, T_OTHER_KIND, 0, &extension));
    NAOS_CHECK_PTR_EQ(&t_extension, extension);
    NAOS_CHECK_UINT_EQ(0, amark_syntax_set_slot_count(tu, T_SLOT_KIND + 1u));
    NAOS_CHECK_UINT_EQ(0, amark_syntax_set_slot_count(amark_syntax_set_core(), T_SLOT_KIND));

    amark_syntax_set_destroy(tu);
    amark_syntax_set_destroy(ut);
}

NAOS_SUITE(extension_registry,
    NAOS_SET_SETUP(reset),
    NAOS_SET_TEARDOWN(restore),
    NAOS_ADD_TEST(the_types_get_identifiers_in_the_order_of_the_set),
    NAOS_ADD_TEST(the_builtin_types_are_described_like_the_extension_types),
    NAOS_ADD_TEST(the_slots_of_a_user_kind_come_in_the_order_of_the_set))

/* --- Suite extension_nodes ------------------------------------------------------------ */

NAOS_TEST(the_containment_overrides_decide_the_insertions)
{
    amark_syntax_set_t *set = create_set(&t_extension, &u_extension);
    amark_document_t *document = NULL;
    amark_node_t *root;
    amark_node_t *table;
    amark_node_t *row;
    amark_node_t *cell;
    amark_node_t *mark;
    amark_node_t *paragraph;
    amark_node_t *text;

    NAOS_REQUIRE_NOT_NULL(set);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(set, &document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_take_syntax_set(document, set));
    root = amark_document_root(document);
    table = create(document, "t.table");
    row = create(document, "t.row");
    cell = create(document, "t.cell");
    mark = create(document, "t.mark");
    paragraph = create(document, "paragraph");
    text = create(document, "text");
    NAOS_REQUIRE_NOT_NULL(text);

    /* The queries follow the overrides. */
    NAOS_CHECK_TRUE(amark_node_can_contain(table, type_of(document, "t.row")));
    NAOS_CHECK_FALSE(amark_node_can_contain(table, AMARK_NODE_PARAGRAPH));
    NAOS_CHECK_FALSE(amark_node_can_contain(table, type_of(document, "t.cell")));
    NAOS_CHECK_TRUE(amark_node_can_contain(row, type_of(document, "t.cell")));
    NAOS_CHECK_TRUE(amark_node_can_contain(cell, AMARK_NODE_TEXT));
    NAOS_CHECK_TRUE(amark_node_can_contain(cell, type_of(document, "t.mark")));
    NAOS_CHECK_FALSE(amark_node_can_contain(cell, AMARK_NODE_SOFTBREAK));
    NAOS_CHECK_FALSE(amark_node_can_contain(cell, AMARK_NODE_PARAGRAPH));
    NAOS_CHECK_FALSE(amark_node_can_contain(root, type_of(document, "t.row")));
    NAOS_CHECK_TRUE(amark_node_can_contain(root, type_of(document, "t.table")));
    /* A type that accepts no class but has can_contain may have children. */
    NAOS_CHECK_TRUE(amark_node_can_have_children(row));
    NAOS_CHECK_FALSE(amark_node_can_have_children(mark));
    NAOS_CHECK_TRUE(amark_node_is_inline(mark));
    NAOS_CHECK_TRUE(amark_node_is_block(table));

    /* The insertions: each rejection leaves the tree as it was. */
    REJECT(table, paragraph);
    REJECT(table, cell);
    REJECT(root, row);
    REJECT(row, text);
    REJECT(mark, text);
    APPEND(table, row);
    APPEND(row, cell);
    APPEND(cell, text);
    APPEND(cell, mark);
    REJECT(cell, paragraph);
    APPEND(root, table);
    /* A paragraph holds t.mark by the classes (no override). */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(paragraph, mark));
    NAOS_CHECK_PTR_EQ(paragraph, amark_node_parent(mark));
    NAOS_CHECK_PTR_EQ(text, amark_node_last_child(cell));
    /* A move back into the cell, and a replacement that the override rejects. */
    APPEND(cell, mark);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_replace(row, paragraph));
    NAOS_CHECK_PTR_EQ(table, amark_node_parent(row));
    NAOS_CHECK_TRUE(calls.table_can_contain > 0);
    NAOS_CHECK_TRUE(calls.row_can_contain > 0);
    NAOS_CHECK_TRUE(calls.cell_can_contain > 0);

    amark_document_destroy(document);
    NAOS_CHECK_UINT_EQ(1, calls.table_init);
    NAOS_CHECK_UINT_EQ(1, calls.table_deinit);
    NAOS_CHECK_UINT_EQ(1, calls.row_init);
    NAOS_CHECK_UINT_EQ(1, calls.row_deinit);
}

NAOS_TEST(inline_text_follows_an_override_that_rejects_soft_breaks)
{
    amark_syntax_set_t *set = create_set(&t_extension, &u_extension);
    amark_document_t *document = NULL;
    amark_node_t *table;
    amark_node_t *row;
    amark_node_t *cell;
    amark_node_t *first;

    NAOS_REQUIRE_NOT_NULL(set);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(set, &document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_take_syntax_set(document, set));
    table = create(document, "t.table");
    row = create(document, "t.row");
    cell = create(document, "t.cell");
    NAOS_REQUIRE_NOT_NULL(cell);
    APPEND(row, cell);
    APPEND(table, row);
    APPEND(amark_document_root(document), table);

    /* One line is a text; a line ending would need a soft break, which the cell
     * rejects: nothing changes. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_inline_text(cell, "a b", 3));
    first = amark_node_first_child(cell);
    NAOS_REQUIRE_NOT_NULL(first);
    NAOS_CHECK_STR_EQ("a b", amark_node_literal(first, NULL));
    NAOS_CHECK_NULL(amark_node_next(first));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_inline_text(cell, "a\nb", 3));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_inline_text(cell, "\r\n", 2));
    NAOS_CHECK_PTR_EQ(first, amark_node_first_child(cell));
    NAOS_CHECK_PTR_EQ(first, amark_node_last_child(cell));
    /* A row cannot contain text at all. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_inline_text(row, "a", 1));
    NAOS_CHECK_PTR_EQ(cell, amark_node_first_child(row));

    /* "" removes the children: AMARK_EDIT_CHILDREN on the cell, and its ancestors
     * get AMARK_EDIT_DESCENDANTS. Again on the cell without children, nothing is
     * recorded. */
    amark_document_set_edit_tracking(document, true);
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(cell));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_inline_text(cell, "", 0));
    NAOS_CHECK_NULL(amark_node_first_child(cell));
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_CHILDREN, amark_node_edits(cell));
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_DESCENDANTS, amark_node_edits(row));
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_DESCENDANTS, amark_node_edits(table));
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_DESCENDANTS, amark_node_edits(amark_document_root(document)));

    amark_node_destroy(row);
    row = create(document, "t.row");
    cell = create(document, "t.cell");
    NAOS_REQUIRE_NOT_NULL(cell);
    APPEND(row, cell);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_inline_text(cell, "", 0));
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_CREATED, amark_node_edits(cell));
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_CREATED | AMARK_EDIT_CHILDREN | AMARK_EDIT_DESCENDANTS,
                       amark_node_edits(row));

    amark_document_destroy(document);
}

NAOS_TEST(the_literal_check_of_the_type_is_called)
{
    amark_syntax_set_t *set = create_set(&t_extension, &u_extension);
    amark_document_t *document = NULL;
    amark_node_t *mark;
    amark_node_t *table;

    NAOS_REQUIRE_NOT_NULL(set);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(set, &document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_take_syntax_set(document, set));
    mark = create(document, "t.mark");
    table = create(document, "t.table");
    NAOS_REQUIRE_NOT_NULL(table);
    amark_document_set_edit_tracking(document, true);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_literal(mark, "ok", 2));
    NAOS_CHECK_UINT_EQ(1, calls.mark_validate);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_literal(mark, "no!", 3));
    NAOS_CHECK_UINT_EQ(2, calls.mark_validate);
    NAOS_CHECK_STR_EQ("ok", amark_node_literal(mark, NULL));
    /* The rules of the literal mode come first: a line ending never reaches the
     * function. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_literal(mark, "a\nb", 3));
    NAOS_CHECK_UINT_EQ(2, calls.mark_validate);
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_CONTENT, amark_node_edits(mark));
    /* A type without a literal mode rejects literals. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_literal(table, "x", 1));

    amark_document_destroy(document);
}

NAOS_TEST(the_type_data_changes_are_recorded_by_mark_edited)
{
    amark_syntax_set_t *set = create_set(&t_extension, &u_extension);
    amark_document_t *document = NULL;
    amark_node_t *root;
    amark_node_t *table;

    NAOS_REQUIRE_NOT_NULL(set);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(set, &document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_take_syntax_set(document, set));
    root = amark_document_root(document);
    table = build_sample(document);
    NAOS_REQUIRE_NOT_NULL(table);

    /* Built with tracking off: nothing recorded. */
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(table));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(root));

    amark_document_set_edit_tracking(document, true);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, table_set_caption(table, "Fruits", 6));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, table_set_columns(table, 2));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(table));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, table_set_columns(table, 3));
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_CONTENT, amark_node_edits(table));
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_DESCENDANTS, amark_node_edits(root));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(amark_node_first_child(table)));
    AMARK_CHECK_TREE_DUMP(table, AMARK_DUMP_EDITS,
                          "(t.table :caption \"Fruits\" :columns 3 :edits (content)\n"
                          "  (t.row\n"
                          "    (t.cell\n"
                          "      (text \"apple\")\n"
                          "      (t.mark \"ok\"))))\n");

    amark_document_destroy(document);
}

NAOS_TEST(a_whole_lifecycle_calls_the_function_tables_and_leaks_nothing)
{
    static amark_test_allocator_t allocator;
    amark_syntax_set_t *tu;
    amark_syntax_set_t *ut;
    amark_document_t *document = NULL;
    amark_document_t *other = NULL;
    amark_document_t *core = NULL;
    amark_node_t *table;
    amark_node_t *copy = NULL;
    amark_node_t *imported = NULL;
    amark_node_t *rejected = NULL;
    amark_buffer_t *before = NULL;
    amark_buffer_t *after = NULL;
    calls_t before_import;

    amark_test_allocator_init(&allocator);
    amark_allocator_set(amark_test_allocator_activate(&allocator));

    /* The set of the document (owned by it), the other order, and core. */
    tu = create_set(&t_extension, &u_extension);
    ut = create_set(&u_extension, &t_extension);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_create(tu, &document));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_take_syntax_set(document, tu));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_create(ut, &other));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &core));

    /* Creation: init of each node (with NULL data for data_size 0). */
    table = build_sample(document);
    NAOS_CHECK_UINT_EQ(1, calls.table_init);
    NAOS_CHECK_UINT_EQ(1, calls.row_init);
    NAOS_CHECK_UINT_EQ(0, calls.table_deinit);
    NAOS_CHECK_STR_EQ("Fruits", table_caption(table));

    /* The dump calls the dump function once per t.table. */
    AMARK_CHECK_TREE_DUMP(amark_document_root(document), 0, SAMPLE_DUMP);
    NAOS_CHECK_UINT_EQ(1, calls.table_dump);

    /* The copy: the copy functions instead of init, a caption of its own, equal to
     * the original through the equal functions. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_copy(table, &copy));
    NAOS_CHECK_UINT_EQ(1, calls.table_copy);
    NAOS_CHECK_UINT_EQ(1, calls.row_copy);
    NAOS_CHECK_UINT_EQ(1, calls.table_init);
    NAOS_CHECK_UINT_EQ(1, calls.row_init);
    NAOS_CHECK_NULL(amark_node_parent(copy));
    NAOS_CHECK_STR_EQ("Fruits", table_caption(copy));
    NAOS_CHECK_TRUE(table_caption(copy) != table_caption(table));
    NAOS_CHECK_TRUE(amark_node_equal(table, copy, 0));
    NAOS_CHECK_UINT_EQ(1, calls.table_equal);
    NAOS_CHECK_UINT_EQ(1, calls.row_equal);

    /* The equal function of t.row decides although it has no data. */
    row_equal_answer = false;
    NAOS_CHECK_FALSE(amark_node_equal(table, copy, 0));
    NAOS_CHECK_UINT_EQ(2, calls.row_equal);
    row_equal_answer = true;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, table_set_caption(copy, "Other", 5));
    NAOS_CHECK_FALSE(amark_node_equal(table, copy, 0));
    NAOS_CHECK_STR_EQ("Fruits", table_caption(table));

    /* Destroying the copy calls deinit for each of its nodes. */
    amark_node_destroy(copy);
    NAOS_CHECK_UINT_EQ(1, calls.table_deinit);
    NAOS_CHECK_UINT_EQ(1, calls.row_deinit);

    /* The import into the set that lacks the types is rejected before any copy, and
     * leaves core as it was. */
    before_import = calls;
    rejected = table;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_import(core, table, &rejected));
    NAOS_CHECK_NULL(rejected);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_import(core, amark_node_first_child(table), &rejected));
    NAOS_CHECK_UINT_EQ(before_import.table_copy, calls.table_copy);
    NAOS_CHECK_UINT_EQ(before_import.row_copy, calls.row_copy);
    NAOS_CHECK_UINT_EQ(0, amark_document_source_count(core));
    NAOS_CHECK_NULL(amark_node_first_child(amark_document_root(core)));
    AMARK_CHECK_TREE_DUMP(amark_document_root(core), 0, "(document)\n");

    /* The import into the other order: the same descriptors with other
     * identifiers, the source copied, the copy functions called. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_import(other, table, &imported));
    NAOS_CHECK_UINT_EQ(before_import.table_copy + 1, calls.table_copy);
    NAOS_CHECK_UINT_EQ(before_import.row_copy + 1, calls.row_copy);
    NAOS_CHECK_PTR_EQ(&table_type, amark_node_desc(imported));
    NAOS_CHECK_UINT_EQ(AMARK_NODE_DYNAMIC_FIRST, amark_node_get_type(table));
    NAOS_CHECK_UINT_EQ(AMARK_NODE_DYNAMIC_FIRST + 1u, amark_node_get_type(imported));
    NAOS_CHECK_UINT_EQ(type_of(other, "t.row"),
                       amark_node_get_type(amark_node_first_child(imported)));
    NAOS_CHECK_TRUE(amark_node_equal(table, imported, 0));
    NAOS_CHECK_UINT_EQ(1, amark_document_source_count(other));
    APPEND(amark_document_root(other), imported);
    before = dump_of(table, AMARK_DUMP_TOKENS);

    /* The imported subtree outlives the document it came from. */
    amark_document_destroy(document);
    NAOS_CHECK_UINT_EQ(2, calls.table_deinit);
    NAOS_CHECK_UINT_EQ(2, calls.row_deinit);
    after = dump_of(imported, AMARK_DUMP_TOKENS);
    if ((before != NULL) && (after != NULL))
    {
        NAOS_CHECK_MEM_EQ(amark_buffer_data(before), amark_buffer_length(before),
                          amark_buffer_data(after), amark_buffer_length(after));
    }

    AMARK_CHECK_TREE_DUMP(imported, AMARK_DUMP_TOKENS,
                          "(t.table :caption \"Fruits\" :columns 2\n"
                          "  (t.row\n"
                          "    (t.cell\n"
                          "      (text :tokens ((text 0 5)) \"apple\")\n"
                          "      (t.mark \"ok\"))))\n");
    NAOS_CHECK_STR_EQ("apple", amark_document_source_data(other, 0, NULL));
    amark_buffer_destroy(before);
    amark_buffer_destroy(after);

    /* Destruction: every init and copy has its deinit; t.row never got data. */
    amark_document_destroy(other);
    amark_document_destroy(core);
    amark_syntax_set_destroy(ut);
    amark_allocator_set(NULL);
    NAOS_CHECK_UINT_EQ(1, calls.table_init);
    NAOS_CHECK_UINT_EQ(2, calls.table_copy);
    NAOS_CHECK_UINT_EQ(3, calls.table_deinit);
    NAOS_CHECK_UINT_EQ(1, calls.row_init);
    NAOS_CHECK_UINT_EQ(2, calls.row_copy);
    NAOS_CHECK_UINT_EQ(3, calls.row_deinit);
    NAOS_CHECK_UINT_EQ(0, calls.row_with_data);
    NAOS_CHECK_UINT_EQ(0, allocator.live_blocks);
    NAOS_CHECK_UINT_EQ(0, allocator.failures);
}

/* A failed copy function leaves no node and no block: the copy is not deinitialized
 * (it frees what it allocated), the original is unchanged. */
NAOS_TEST(a_failed_copy_function_leaves_nothing)
{
    static amark_test_allocator_t allocator;
    amark_syntax_set_t *set;
    amark_document_t *document = NULL;
    amark_node_t *table;
    amark_node_t *copy = NULL;
    amark_error_t err = AMARK_ERROR_NONE;
    size_t fail_at;

    amark_test_allocator_init(&allocator);
    amark_allocator_set(amark_test_allocator_activate(&allocator));
    set = create_set(&t_extension, &u_extension);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_create(set, &document));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_take_syntax_set(document, set));
    table = build_sample(document);

    /* Fail each allocation of the copy in turn, until one succeeds. */
    for (fail_at = 1; fail_at < 100; fail_at++)
    {
        size_t live = allocator.live_blocks;

        amark_test_allocator_reset_counters(&allocator);
        allocator.fail_at = fail_at;
        err = amark_node_copy(table, &copy);
        allocator.fail_at = 0;
        if (err == AMARK_ERROR_NONE)
        {
            break;
        }

        NAOS_CHECK_INT_EQ(AMARK_ERROR_OUT_OF_MEMORY, err);
        NAOS_CHECK_NULL(copy);
        NAOS_CHECK_UINT_EQ(live, allocator.live_blocks);
    }

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, err);
    NAOS_CHECK_TRUE(amark_node_equal(table, copy, 0));
    /* At least the failure in the caption of the copy function was seen. */
    NAOS_CHECK_TRUE(calls.table_copy >= 2);
    AMARK_CHECK_TREE_DUMP(amark_document_root(document), 0, SAMPLE_DUMP);
    amark_document_destroy(document);
    amark_allocator_set(NULL);
    NAOS_CHECK_UINT_EQ(calls.table_init + calls.table_copied, calls.table_deinit);
    NAOS_CHECK_UINT_EQ(0, allocator.live_blocks);
}

NAOS_SUITE(extension_nodes,
    NAOS_SET_SETUP(reset),
    NAOS_SET_TEARDOWN(restore),
    NAOS_ADD_TEST(the_containment_overrides_decide_the_insertions),
    NAOS_ADD_TEST(inline_text_follows_an_override_that_rejects_soft_breaks),
    NAOS_ADD_TEST(the_literal_check_of_the_type_is_called),
    NAOS_ADD_TEST(the_type_data_changes_are_recorded_by_mark_edited),
    NAOS_ADD_TEST(a_whole_lifecycle_calls_the_function_tables_and_leaks_nothing),
    NAOS_ADD_TEST(a_failed_copy_function_leaves_nothing))

/* --- Suite extension_round_trip -------------------------------------------------------- */

/* Builds sexpr in document with table_attr and requires success. */
static amark_node_t *build(amark_document_t *document, const char *sexpr, size_t length)
{
    amark_node_t *node = NULL;
    const char *reason = NULL;
    char *text = malloc(length + 1);
    amark_error_t err;

    NAOS_REQUIRE_NOT_NULL(text);
    memcpy(text, sexpr, length);
    text[length] = '\0';
    err = amark_test_tree_build(document, text, table_attr, NULL, &node, &reason);
    free(text);
    if (err != AMARK_ERROR_NONE)
    {
        NAOS_NOTE("%s", (reason != NULL) ? reason : "(no reason)");
    }

    amark_test_tree_free_reason(reason);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, err);
    NAOS_REQUIRE_NOT_NULL(node);

    return node;
}

/* Checks that the dumps of a and b with flags are the same text. */
static void check_same_dump(const amark_node_t *a, const amark_node_t *b, uint32_t flags)
{
    amark_buffer_t *x = dump_of(a, flags);
    amark_buffer_t *y = dump_of(b, flags);

    if ((x != NULL) && (y != NULL))
    {
        if ((amark_buffer_length(x) != amark_buffer_length(y))
            || (memcmp(amark_buffer_data(x), amark_buffer_data(y), amark_buffer_length(x)) != 0))
        {
            amark_test_tree_print_mismatch(amark_buffer_data(x), amark_buffer_data(y));
        }

        NAOS_CHECK_MEM_EQ(amark_buffer_data(x), amark_buffer_length(x), amark_buffer_data(y),
                          amark_buffer_length(y));
    }

    amark_buffer_destroy(x);
    amark_buffer_destroy(y);
}

NAOS_TEST(the_dump_of_extension_types_reads_back)
{
    amark_syntax_set_t *tu = create_set(&t_extension, &u_extension);
    amark_syntax_set_t *ut = create_set(&u_extension, &t_extension);
    amark_document_t *document = NULL;
    amark_document_t *same = NULL;
    amark_document_t *other = NULL;
    amark_node_t *table;
    amark_node_t *root;
    amark_buffer_t *dump;
    amark_source_id_t source = 0;

    NAOS_REQUIRE_NOT_NULL(tu);
    NAOS_REQUIRE_NOT_NULL(ut);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(tu, &document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(tu, &same));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(ut, &other));

    /* A tree made by the API, with a u.note, an empty caption and a negative
     * number, and a caption that needs escapes. */
    table = build_sample(document);
    NAOS_REQUIRE_NOT_NULL(table);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        table_set_caption(table, "A \"b\"\n\\c", 8));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, table_set_columns(table, -4));
    APPEND(amark_document_root(document), create(document, "u.note"));
    APPEND(amark_document_root(document), create(document, "t.table"));
    AMARK_CHECK_TREE_DUMP(amark_document_root(document), 0,
                          "(document\n"
                          "  (t.table :caption \"A \\\"b\\\"\\n\\\\c\" :columns -4\n"
                          "    (t.row\n"
                          "      (t.cell\n"
                          "        (text \"apple\")\n"
                          "        (t.mark \"ok\"))))\n"
                          "  (u.note)\n"
                          "  (t.table :caption \"\" :columns 0))\n");

    /* Its dump with the tokens, built in a document of the same set (the source
     * registered first) and of the other set, dumps the same. */
    dump = dump_of(amark_document_root(document), AMARK_DUMP_TOKENS);
    NAOS_REQUIRE_NOT_NULL(dump);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_add_source(same, "apple", 5, &source));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_add_source(other, "apple", 5, &source));
    root = build(same, amark_buffer_data(dump), amark_buffer_length(dump));
    check_same_dump(amark_document_root(document), root, AMARK_DUMP_TOKENS);
    NAOS_CHECK_TRUE(amark_node_equal(amark_document_root(document), root, 0));
    root = build(other, amark_buffer_data(dump), amark_buffer_length(dump));
    check_same_dump(amark_document_root(document), root, AMARK_DUMP_TOKENS);
    NAOS_CHECK_TRUE(amark_node_equal(amark_document_root(document), root, 0));
    NAOS_CHECK_UINT_EQ(type_of(other, "t.table"),
                       amark_node_get_type(amark_node_first_child(root)));
    amark_buffer_destroy(dump);

    amark_document_destroy(document);
    amark_document_destroy(same);
    amark_document_destroy(other);
    amark_syntax_set_destroy(tu);
    amark_syntax_set_destroy(ut);
    NAOS_CHECK_UINT_EQ(calls.table_init, calls.table_deinit);
}

NAOS_TEST(an_attribute_the_function_rejects_fails_the_building)
{
    amark_syntax_set_t *set = create_set(&t_extension, &u_extension);
    amark_document_t *document = NULL;
    amark_node_t *node = NULL;
    const char *reason = NULL;

    NAOS_REQUIRE_NOT_NULL(set);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(set, &document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_take_syntax_set(document, set));

    /* :caption on a row, an unknown attribute, a mark that the literal check
     * rejects, a row the root does not accept. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_test_tree_build(document, "(t.table (t.row :caption \"x\"))",
                                            table_attr, NULL, &node, &reason));
    NAOS_CHECK_NULL(node);
    amark_test_tree_free_reason(reason);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_test_tree_build(document, "(t.table :rows 2)", table_attr, NULL,
                                            &node, &reason));
    amark_test_tree_free_reason(reason);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_test_tree_build(document, "(t.mark \"no!\")", table_attr, NULL,
                                            &node, &reason));
    amark_test_tree_free_reason(reason);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_test_tree_build(document, "(document (t.row))", table_attr, NULL,
                                            &node, &reason));
    amark_test_tree_free_reason(reason);
    AMARK_CHECK_TREE_DUMP(amark_document_root(document), 0, "(document)\n");

    amark_document_destroy(document);
    NAOS_CHECK_UINT_EQ(calls.table_init, calls.table_deinit);
    NAOS_CHECK_UINT_EQ(calls.row_init, calls.row_deinit);
}

NAOS_SUITE(extension_round_trip,
    NAOS_SET_SETUP(reset),
    NAOS_SET_TEARDOWN(restore),
    NAOS_ADD_TEST(the_dump_of_extension_types_reads_back),
    NAOS_ADD_TEST(an_attribute_the_function_rejects_fails_the_building))

NAOS_MAIN(
    NAOS_ADD_SUITE(extension_registry),
    NAOS_ADD_SUITE(extension_nodes),
    NAOS_ADD_SUITE(extension_round_trip)
)
