/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * Syntax sets (src/tree/syntax_set.c).
 *
 * Suite syntax_set (1.2, 1.9, 8.2-8.11, 8.13): a new set holds amark.core first,
 * which cannot be removed, replaced or preceded; append, insert_before, remove and
 * replace keep the order; invalid descriptors and duplicate extension names are
 * rejected without changing the set; freezing detects duplicate type names, gives
 * the identifiers 256, 257, ... (the same for the same list of extensions) and
 * builds the indexes; a frozen set cannot change; the lookups by name, descriptor
 * and identifier find nothing before freezing; the slots are listed by kind in the
 * order of the set (with the user range of kinds); a clone is not frozen; the static
 * core set is frozen and its destruction does nothing; the list of the slot kinds
 * of the Amark specs has no overlap and fits in the Amark range; and the
 * out-of-memory sweeps of append, freeze and clone leave the set unchanged.
 */
#include "tree/syntax_set.h"

#include <amark/allocator.h>
#include <amark/error.h>
#include <amark/extension.h>
#include <amark/node.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <naos/naos.h>

#include "test_allocator.h"

NAOS_TEARDOWN(restore)
{
    amark_allocator_set(NULL);
}

/* --- Test descriptors ------------------------------------------------------------- */

/* A node type descriptor with the given name, level and literal mode. */
#define TYPE_DESC(name, level, literal_mode, data_size) \
    { sizeof(amark_node_type_desc_t), (name), (level), AMARK_CONTENT_BLOCK, 0, (literal_mode), \
      (data_size), NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL }

static const amark_node_type_desc_t a_block = TYPE_DESC("test.a_block", AMARK_NODE_LEVEL_BLOCK,
                                                        AMARK_LITERAL_NONE, 0);
static const amark_node_type_desc_t a_inline = TYPE_DESC("test.a_inline", AMARK_NODE_LEVEL_INLINE,
                                                         AMARK_LITERAL_LINE, 8);
static const amark_node_type_desc_t b_block = TYPE_DESC("test.b_block", AMARK_NODE_LEVEL_BLOCK,
                                                        AMARK_LITERAL_LINES,
                                                        AMARK_NODE_TYPE_MAX_DATA_SIZE);
/* The name of a_block, in another descriptor. */
static const amark_node_type_desc_t a_block_again = TYPE_DESC("test.a_block",
                                                              AMARK_NODE_LEVEL_BLOCK,
                                                              AMARK_LITERAL_NONE, 0);

/* A slot of a kind, as a spec or the application defines it: the base first. */
typedef struct test_slot
{
    amark_slot_t base;
    int value;
} test_slot_t;

#define USER_KIND (AMARK_SLOT_KIND_USER_FIRST + 44u)

static const test_slot_t a_slot_user = { { sizeof(test_slot_t), USER_KIND }, 1 };
static const test_slot_t a_slot_one = { { sizeof(test_slot_t), 1u }, 2 };
static const test_slot_t a_slot_user2 = { { sizeof(test_slot_t), USER_KIND }, 3 };
static const test_slot_t b_slot_user = { { sizeof(test_slot_t), USER_KIND }, 4 };
static const test_slot_t b_slot_last = { { sizeof(test_slot_t), AMARK_SLOT_KIND_AMARK_LAST }, 5 };

static const amark_node_type_desc_t *const a_types[] = { &a_block, &a_inline };
static const amark_slot_t *const a_slots[] = { &a_slot_user.base, &a_slot_one.base,
                                               &a_slot_user2.base };
static const amark_node_type_desc_t *const b_types[] = { &b_block };
static const amark_slot_t *const b_slots[] = { &b_slot_user.base, &b_slot_last.base };
static const amark_node_type_desc_t *const dup_types[] = { &a_block_again };

static const amark_extension_t ext_a = { sizeof(amark_extension_t), "test.a", a_types, 2, a_slots,
                                         3, NULL };
static const amark_extension_t ext_b = { sizeof(amark_extension_t), "test.b", b_types, 1, b_slots,
                                         2, NULL };
static const amark_extension_t ext_c = { sizeof(amark_extension_t), "test.c", NULL, 0, NULL, 0,
                                         NULL };
static const amark_extension_t ext_d = { sizeof(amark_extension_t), "test-d_9", NULL, 0, NULL, 0,
                                         NULL };
/* Another extension named test.a (no type). */
static const amark_extension_t ext_a_other = { sizeof(amark_extension_t), "test.a", NULL, 0, NULL,
                                               0, NULL };
/* An extension named amark.core. */
static const amark_extension_t ext_core_name = { sizeof(amark_extension_t), "amark.core", NULL, 0,
                                                 NULL, 0, NULL };
/* An extension whose type has the name of a type of ext_a. */
static const amark_extension_t ext_dup = { sizeof(amark_extension_t), "test.dup", dup_types, 1,
                                           NULL, 0, NULL };

/* Creates a set, stopping the test on failure. */
#define REQUIRE_CREATE(set) \
    do { \
        (set) = NULL; \
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_create(&(set))); \
        NAOS_REQUIRE_NOT_NULL(set); \
    } while (0)

/* Whether the extensions of set are exactly the count ones of expected. */
static bool has_extensions(const amark_syntax_set_t *set, const amark_extension_t *const *expected,
                           size_t count)
{
    size_t i;

    if (amark_syntax_set_extension_count(set) != count)
    {
        return false;
    }

    for (i = 0; i < count; i++)
    {
        if (amark_syntax_set_extension_at(set, i) != expected[i])
        {
            return false;
        }
    }

    return true;
}

/* --- Creation and order ------------------------------------------------------------- */

NAOS_TEST(a_new_set_holds_only_core_and_is_not_frozen)
{
    /* 8.3: amark.core comes first. */
    amark_syntax_set_t *set;
    amark_node_type_t type = 7;

    REQUIRE_CREATE(set);

    NAOS_CHECK_UINT_EQ(1, amark_syntax_set_extension_count(set));
    NAOS_CHECK_PTR_EQ(&amark_core_extension, amark_syntax_set_extension_at(set, 0));
    NAOS_CHECK_NULL(amark_syntax_set_extension_at(set, 1));
    NAOS_CHECK_PTR_EQ(&amark_core_extension, amark_syntax_set_find_extension(set, "amark.core"));
    NAOS_CHECK_NULL(amark_syntax_set_find_extension(set, "test.a"));
    NAOS_CHECK_FALSE(amark_syntax_set_is_frozen(set));
    NAOS_CHECK_FALSE(set->is_static);

    /* 8.10: the lookups find nothing before freezing. */
    NAOS_CHECK_FALSE(amark_syntax_set_find_node_type(set, "paragraph", &type));
    NAOS_CHECK_UINT_EQ(AMARK_NODE_NONE, type);
    NAOS_CHECK_FALSE(amark_syntax_set_node_type_of(set, amark_builtin_types[AMARK_NODE_TEXT],
                                                   NULL));
    NAOS_CHECK_NULL(amark_syntax_set_node_type_desc(set, AMARK_NODE_PARAGRAPH));

    amark_syntax_set_destroy(set);
}

NAOS_TEST(append_insert_remove_and_replace_keep_the_order)
{
    /* 8.4: the changes keep the order, and replace puts the new extension at the
     * same place. */
    amark_syntax_set_t *set;
    const amark_extension_t *after_append[] = { &amark_core_extension, &ext_a, &ext_b };
    const amark_extension_t *after_insert[] = { &amark_core_extension, &ext_a, &ext_c, &ext_b };
    const amark_extension_t *after_replace[] = { &amark_core_extension, &ext_a, &ext_d, &ext_b };
    const amark_extension_t *after_remove[] = { &amark_core_extension, &ext_d, &ext_b };
    const amark_extension_t *after_same[] = { &amark_core_extension, &ext_d, &ext_b, &ext_a_other };
    const amark_extension_t *after_front[] = { &amark_core_extension, &ext_c, &ext_d, &ext_b,
                                               &ext_a_other };

    REQUIRE_CREATE(set);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &ext_a));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &ext_b));
    NAOS_CHECK_TRUE(has_extensions(set, after_append, 3));

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_insert_before(set, "test.b", &ext_c));
    NAOS_CHECK_TRUE(has_extensions(set, after_insert, 4));

    /* The name may change. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_replace(set, "test.c", &ext_d));
    NAOS_CHECK_TRUE(has_extensions(set, after_replace, 4));
    NAOS_CHECK_NULL(amark_syntax_set_find_extension(set, "test.c"));
    NAOS_CHECK_PTR_EQ(&ext_d, amark_syntax_set_find_extension(set, "test-d_9"));

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_remove(set, "test.a"));
    NAOS_CHECK_TRUE(has_extensions(set, after_remove, 3));

    /* A removed name can be used again, and an extension can be replaced by one
     * with the same name. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &ext_a));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_replace(set, "test.a", &ext_a_other));
    NAOS_CHECK_TRUE(has_extensions(set, after_same, 4));

    /* Insert right after amark.core (before the second extension), which also
     * grows the array past its first capacity. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_insert_before(set, "test-d_9", &ext_c));
    NAOS_CHECK_TRUE(has_extensions(set, after_front, 5));

    amark_syntax_set_destroy(set);
}

NAOS_TEST(core_cannot_be_removed_replaced_or_preceded)
{
    /* 8.3 */
    amark_syntax_set_t *set;
    const amark_extension_t *expected[] = { &amark_core_extension, &ext_a };

    REQUIRE_CREATE(set);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &ext_a));

    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_remove(set, "amark.core"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_syntax_set_replace(set, "amark.core", &ext_c));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_syntax_set_insert_before(set, "amark.core", &ext_c));
    NAOS_CHECK_TRUE(has_extensions(set, expected, 2));

    amark_syntax_set_destroy(set);
}

NAOS_TEST(unknown_names_and_null_arguments_are_rejected)
{
    amark_syntax_set_t *set;
    const amark_extension_t *expected[] = { &amark_core_extension, &ext_a };

    REQUIRE_CREATE(set);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &ext_a));

    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_remove(set, "test.x"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_syntax_set_replace(set, "test.x", &ext_c));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_syntax_set_insert_before(set, "test.x", &ext_c));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_remove(set, NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_replace(set, NULL, &ext_c));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_syntax_set_insert_before(set, NULL, &ext_c));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_append(set, NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_append(NULL, &ext_c));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_remove(NULL, "test.a"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_freeze(NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_create(NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_clone(set, NULL));
    NAOS_CHECK_TRUE(has_extensions(set, expected, 2));

    NAOS_CHECK_UINT_EQ(0, amark_syntax_set_extension_count(NULL));
    NAOS_CHECK_NULL(amark_syntax_set_extension_at(NULL, 0));
    NAOS_CHECK_NULL(amark_syntax_set_find_extension(NULL, "amark.core"));
    NAOS_CHECK_NULL(amark_syntax_set_find_extension(set, NULL));
    NAOS_CHECK_FALSE(amark_syntax_set_is_frozen(NULL));
    NAOS_CHECK_UINT_EQ(0, amark_syntax_set_slot_count(NULL, USER_KIND));
    amark_syntax_set_destroy(NULL);

    amark_syntax_set_destroy(set);
}

NAOS_TEST(duplicate_extension_names_are_rejected)
{
    /* 8.5 */
    amark_syntax_set_t *set;
    const amark_extension_t *expected[] = { &amark_core_extension, &ext_a, &ext_c };

    REQUIRE_CREATE(set);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &ext_a));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &ext_c));

    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_append(set, &ext_a));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_append(set, &ext_a_other));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_append(set, &ext_core_name));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_syntax_set_append(set, &amark_core_extension));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_syntax_set_insert_before(set, "test.c", &ext_a_other));
    /* The new name of a replace is checked against the other extensions. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_syntax_set_replace(set, "test.c", &ext_a_other));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_syntax_set_replace(set, "test.c", &ext_core_name));
    NAOS_CHECK_TRUE(has_extensions(set, expected, 3));

    amark_syntax_set_destroy(set);
}

/* --- Invalid descriptors ------------------------------------------------------------ */

static const amark_node_type_desc_t bad_type_struct_size = {
    sizeof(amark_node_type_desc_t) - 1, "test.t", AMARK_NODE_LEVEL_BLOCK, 0, 0,
    AMARK_LITERAL_NONE, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};
static const amark_node_type_desc_t bad_type_no_dot = TYPE_DESC("paragraph2",
                                                                AMARK_NODE_LEVEL_BLOCK,
                                                                AMARK_LITERAL_NONE, 0);
static const amark_node_type_desc_t bad_type_null_name = TYPE_DESC(NULL, AMARK_NODE_LEVEL_BLOCK,
                                                                   AMARK_LITERAL_NONE, 0);
static const amark_node_type_desc_t bad_type_chars = TYPE_DESC("test.Table",
                                                               AMARK_NODE_LEVEL_BLOCK,
                                                               AMARK_LITERAL_NONE, 0);
static const amark_node_type_desc_t bad_type_level_zero = TYPE_DESC("test.t",
                                                                    (amark_node_level_t)0,
                                                                    AMARK_LITERAL_NONE, 0);
static const amark_node_type_desc_t bad_type_level_both = TYPE_DESC("test.t",
                                                                    (amark_node_level_t)3,
                                                                    AMARK_LITERAL_NONE, 0);
static const amark_node_type_desc_t bad_type_literal = TYPE_DESC("test.t", AMARK_NODE_LEVEL_BLOCK,
                                                                 (amark_literal_mode_t)4, 0);
static const amark_node_type_desc_t bad_type_data_size = TYPE_DESC(
    "test.t", AMARK_NODE_LEVEL_BLOCK, AMARK_LITERAL_NONE, AMARK_NODE_TYPE_MAX_DATA_SIZE + 1u);

static const test_slot_t bad_slot_kind = { { sizeof(test_slot_t), 0u }, 0 };
static const amark_slot_t bad_slot_size = { sizeof(amark_slot_t) - 1, 1u };

#define BAD_TYPE_LIST(name) static const amark_node_type_desc_t *const name##_list[] = { &name };
BAD_TYPE_LIST(bad_type_struct_size)
BAD_TYPE_LIST(bad_type_no_dot)
BAD_TYPE_LIST(bad_type_null_name)
BAD_TYPE_LIST(bad_type_chars)
BAD_TYPE_LIST(bad_type_level_zero)
BAD_TYPE_LIST(bad_type_level_both)
BAD_TYPE_LIST(bad_type_literal)
BAD_TYPE_LIST(bad_type_data_size)

static const amark_node_type_desc_t *const null_type_list[] = { &a_block, NULL };
static const amark_slot_t *const bad_slot_kind_list[] = { &bad_slot_kind.base };
static const amark_slot_t *const bad_slot_size_list[] = { &bad_slot_size };
static const amark_slot_t *const null_slot_list[] = { &a_slot_one.base, NULL };

/* An extension named test.bad with the given types and slots. */
#define BAD_EXT(types, type_count, slots, slot_count) \
    { sizeof(amark_extension_t), "test.bad", (types), (type_count), (slots), (slot_count), NULL }
#define BAD_NAME(name) { sizeof(amark_extension_t), (name), NULL, 0, NULL, 0, NULL }

static const amark_extension_t bad_extensions[] = {
    { sizeof(amark_extension_t) - 1, "test.bad", NULL, 0, NULL, 0, NULL },
    BAD_NAME(NULL),
    BAD_NAME(""),
    BAD_NAME("Test.bad"),
    BAD_NAME("_test"),
    BAD_NAME(".test"),
    BAD_NAME("-test"),
    BAD_NAME("test bad"),
    BAD_NAME("test/bad"),
    BAD_NAME("test.Bad"),
    BAD_EXT(bad_type_struct_size_list, 1, NULL, 0),
    BAD_EXT(bad_type_no_dot_list, 1, NULL, 0),
    BAD_EXT(bad_type_null_name_list, 1, NULL, 0),
    BAD_EXT(bad_type_chars_list, 1, NULL, 0),
    BAD_EXT(bad_type_level_zero_list, 1, NULL, 0),
    BAD_EXT(bad_type_level_both_list, 1, NULL, 0),
    BAD_EXT(bad_type_literal_list, 1, NULL, 0),
    BAD_EXT(bad_type_data_size_list, 1, NULL, 0),
    BAD_EXT(null_type_list, 2, NULL, 0),
    BAD_EXT(NULL, 1, NULL, 0),
    BAD_EXT(NULL, 0, bad_slot_kind_list, 1),
    BAD_EXT(NULL, 0, bad_slot_size_list, 1),
    BAD_EXT(NULL, 0, null_slot_list, 2),
    BAD_EXT(NULL, 0, NULL, 1),
};

#define BAD_COUNT (sizeof(bad_extensions) / sizeof(bad_extensions[0]))

NAOS_TEST(invalid_descriptors_are_rejected_without_change)
{
    /* 8.5: each invalid descriptor is rejected by append, insert_before and
     * replace, and the set stays as it was. */
    amark_syntax_set_t *set;
    const amark_extension_t *expected[] = { &amark_core_extension, &ext_a, &ext_c };
    size_t i;

    REQUIRE_CREATE(set);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &ext_a));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &ext_c));

    for (i = 0; i < BAD_COUNT; i++)
    {
        NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                          amark_syntax_set_append(set, &bad_extensions[i]));
        NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                          amark_syntax_set_insert_before(set, "test.c", &bad_extensions[i]));
        NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                          amark_syntax_set_replace(set, "test.c", &bad_extensions[i]));
        NAOS_CHECK_TRUE(has_extensions(set, expected, 3));
    }

    amark_syntax_set_destroy(set);
}

NAOS_TEST(boundary_descriptors_are_accepted)
{
    /* The largest data size, the user range of slot kinds, a name with digits,
     * '_', '.' and '-', and every known literal mode and level. */
    static const amark_node_type_desc_t span = TYPE_DESC("9x.span-type_1",
                                                         AMARK_NODE_LEVEL_INLINE,
                                                         AMARK_LITERAL_SPAN, 0);
    static const amark_node_type_desc_t *const types[] = { &b_block, &span };
    static const amark_slot_t minimal = { sizeof(amark_slot_t), UINT32_MAX };
    static const amark_slot_t *const slots[] = { &minimal };
    static const amark_extension_t ext = { sizeof(amark_extension_t), "0-ext_.x", types, 2,
                                           slots, 1, NULL };
    amark_syntax_set_t *set;

    REQUIRE_CREATE(set);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &ext));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_freeze(set));
    NAOS_CHECK_UINT_EQ(1, amark_syntax_set_slot_count(set, UINT32_MAX));

    amark_syntax_set_destroy(set);
}

/* --- Freezing ------------------------------------------------------------------------ */

NAOS_TEST(freezing_gives_identifiers_from_256_in_order)
{
    /* 8.6, 8.10 */
    amark_syntax_set_t *set;
    amark_node_type_t type = AMARK_NODE_NONE;
    amark_node_type_t i;

    REQUIRE_CREATE(set);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &ext_a));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &ext_c));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &ext_b));

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_freeze(set));
    NAOS_CHECK_TRUE(amark_syntax_set_is_frozen(set));

    /* Name -> identifier. */
    NAOS_CHECK_TRUE(amark_syntax_set_find_node_type(set, "test.a_block", &type));
    NAOS_CHECK_UINT_EQ(256, type);
    NAOS_CHECK_TRUE(amark_syntax_set_find_node_type(set, "test.a_inline", &type));
    NAOS_CHECK_UINT_EQ(257, type);
    NAOS_CHECK_TRUE(amark_syntax_set_find_node_type(set, "test.b_block", &type));
    NAOS_CHECK_UINT_EQ(258, type);
    NAOS_CHECK_TRUE(amark_syntax_set_find_node_type(set, "paragraph", &type));
    NAOS_CHECK_UINT_EQ(AMARK_NODE_PARAGRAPH, type);
    NAOS_CHECK_TRUE(amark_syntax_set_find_node_type(set, "test.b_block", NULL));
    NAOS_CHECK_FALSE(amark_syntax_set_find_node_type(set, "test.none", &type));
    NAOS_CHECK_UINT_EQ(AMARK_NODE_NONE, type);
    NAOS_CHECK_FALSE(amark_syntax_set_find_node_type(set, NULL, &type));
    NAOS_CHECK_FALSE(amark_syntax_set_find_node_type(set, "", &type));

    /* Descriptor -> identifier. */
    NAOS_CHECK_TRUE(amark_syntax_set_node_type_of(set, &a_inline, &type));
    NAOS_CHECK_UINT_EQ(257, type);
    NAOS_CHECK_TRUE(amark_syntax_set_node_type_of(set, &b_block, &type));
    NAOS_CHECK_UINT_EQ(258, type);
    NAOS_CHECK_TRUE(amark_syntax_set_node_type_of(set, amark_builtin_types[AMARK_NODE_IMAGE],
                                                  &type));
    NAOS_CHECK_UINT_EQ(AMARK_NODE_IMAGE, type);
    NAOS_CHECK_FALSE(amark_syntax_set_node_type_of(set, &a_block_again, &type));
    NAOS_CHECK_UINT_EQ(AMARK_NODE_NONE, type);
    NAOS_CHECK_FALSE(amark_syntax_set_node_type_of(set, NULL, &type));

    /* Identifier -> descriptor, for the public and the internal lookup. */
    NAOS_CHECK_PTR_EQ(&a_block, amark_syntax_set_node_type_desc(set, 256));
    NAOS_CHECK_PTR_EQ(&a_inline, amark_syntax_set_node_type_desc(set, 257));
    NAOS_CHECK_PTR_EQ(&b_block, amark_syntax_set_desc(set, 258));
    for (i = 1; i <= AMARK_BUILTIN_TYPE_COUNT; i++)
    {
        NAOS_CHECK_PTR_EQ(amark_builtin_types[i], amark_syntax_set_node_type_desc(set, i));
        NAOS_CHECK_PTR_EQ(amark_builtin_types[i], amark_syntax_set_desc(set, i));
    }

    NAOS_CHECK_NULL(amark_syntax_set_node_type_desc(set, AMARK_NODE_NONE));
    NAOS_CHECK_NULL(amark_syntax_set_node_type_desc(set, AMARK_BUILTIN_TYPE_COUNT + 1));
    NAOS_CHECK_NULL(amark_syntax_set_node_type_desc(set, AMARK_NODE_BUILTIN_LAST));
    NAOS_CHECK_NULL(amark_syntax_set_node_type_desc(set, 259));
    NAOS_CHECK_NULL(amark_syntax_set_desc(set, 259));
    NAOS_CHECK_NULL(amark_syntax_set_node_type_desc(set, UINT32_MAX));
    NAOS_CHECK_NULL(amark_syntax_set_node_type_desc(NULL, AMARK_NODE_TEXT));

    amark_syntax_set_destroy(set);
}

NAOS_TEST(a_frozen_set_cannot_change)
{
    /* 8.7 */
    amark_syntax_set_t *set;
    const amark_extension_t *expected[] = { &amark_core_extension, &ext_a, &ext_c };

    REQUIRE_CREATE(set);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &ext_a));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &ext_c));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_freeze(set));

    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_STATE, amark_syntax_set_append(set, &ext_b));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_STATE,
                      amark_syntax_set_insert_before(set, "test.c", &ext_b));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_STATE, amark_syntax_set_remove(set, "test.c"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_STATE, amark_syntax_set_replace(set, "test.c", &ext_b));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_STATE, amark_syntax_set_freeze(set));
    NAOS_CHECK_TRUE(has_extensions(set, expected, 3));
    NAOS_CHECK_TRUE(amark_syntax_set_is_frozen(set));

    amark_syntax_set_destroy(set);
}

NAOS_TEST(duplicate_type_names_fail_freezing)
{
    /* 8.6: the set stays not frozen and can still change. */
    amark_syntax_set_t *set;
    amark_node_type_t type;

    REQUIRE_CREATE(set);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &ext_a));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &ext_dup));

    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_freeze(set));
    NAOS_CHECK_FALSE(amark_syntax_set_is_frozen(set));
    NAOS_CHECK_FALSE(amark_syntax_set_find_node_type(set, "test.a_block", &type));

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_remove(set, "test.dup"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_freeze(set));
    NAOS_CHECK_TRUE(amark_syntax_set_find_node_type(set, "test.a_block", &type));
    NAOS_CHECK_UINT_EQ(256, type);

    amark_syntax_set_destroy(set);
}

NAOS_TEST(a_type_twice_in_one_extension_fails_freezing)
{
    static const amark_node_type_desc_t *const twice[] = { &b_block, &b_block };
    static const amark_extension_t ext = { sizeof(amark_extension_t), "test.twice", twice, 2,
                                           NULL, 0, NULL };
    amark_syntax_set_t *set;

    REQUIRE_CREATE(set);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &ext));

    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_freeze(set));
    NAOS_CHECK_FALSE(amark_syntax_set_is_frozen(set));

    amark_syntax_set_destroy(set);
}

NAOS_TEST(the_same_extensions_give_the_same_identifiers)
{
    /* 8.6: two sets built the same way agree; another order gives other ones. */
    amark_syntax_set_t *x;
    amark_syntax_set_t *y;
    amark_syntax_set_t *z;
    const char *names[] = { "test.a_block", "test.a_inline", "test.b_block", "text" };
    size_t i;

    REQUIRE_CREATE(x);
    REQUIRE_CREATE(y);
    REQUIRE_CREATE(z);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(x, &ext_a));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(x, &ext_b));
    /* y is built through other changes that end with the same list. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(y, &ext_b));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_insert_before(y, "test.b", &ext_c));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_replace(y, "test.c", &ext_a));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(z, &ext_b));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(z, &ext_a));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_freeze(x));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_freeze(y));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_freeze(z));

    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
    {
        amark_node_type_t tx = AMARK_NODE_NONE;
        amark_node_type_t ty = AMARK_NODE_NONE;

        NAOS_CHECK_TRUE(amark_syntax_set_find_node_type(x, names[i], &tx));
        NAOS_CHECK_TRUE(amark_syntax_set_find_node_type(y, names[i], &ty));
        NAOS_CHECK_UINT_EQ(tx, ty);
    }

    {
        amark_node_type_t tz = AMARK_NODE_NONE;

        NAOS_CHECK_TRUE(amark_syntax_set_find_node_type(z, "test.b_block", &tz));
        NAOS_CHECK_UINT_EQ(256, tz);
    }

    amark_syntax_set_destroy(z);
    amark_syntax_set_destroy(y);
    amark_syntax_set_destroy(x);
}

/* --- Slots ------------------------------------------------------------------------- */

NAOS_TEST(slots_are_listed_by_kind_in_the_order_of_the_set)
{
    /* 8.8, 8.9: in the order of the set, then of the slots of an extension, with
     * the extension of each slot; a user kind (256 and above) works the same. */
    amark_syntax_set_t *set;
    const amark_extension_t *owner = &ext_c;

    REQUIRE_CREATE(set);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &ext_b));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &ext_c));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &ext_a));

    /* Nothing before freezing. */
    NAOS_CHECK_UINT_EQ(0, amark_syntax_set_slot_count(set, USER_KIND));
    NAOS_CHECK_NULL(amark_syntax_set_slot_at(set, USER_KIND, 0, &owner));
    NAOS_CHECK_NULL(owner);

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_freeze(set));

    NAOS_CHECK_UINT_EQ(3, amark_syntax_set_slot_count(set, USER_KIND));
    NAOS_CHECK_PTR_EQ(&b_slot_user.base, amark_syntax_set_slot_at(set, USER_KIND, 0, &owner));
    NAOS_CHECK_PTR_EQ(&ext_b, owner);
    NAOS_CHECK_PTR_EQ(&a_slot_user.base, amark_syntax_set_slot_at(set, USER_KIND, 1, &owner));
    NAOS_CHECK_PTR_EQ(&ext_a, owner);
    NAOS_CHECK_PTR_EQ(&a_slot_user2.base, amark_syntax_set_slot_at(set, USER_KIND, 2, NULL));
    NAOS_CHECK_NULL(amark_syntax_set_slot_at(set, USER_KIND, 3, &owner));
    NAOS_CHECK_NULL(owner);

    NAOS_CHECK_UINT_EQ(1, amark_syntax_set_slot_count(set, 1u));
    NAOS_CHECK_PTR_EQ(&a_slot_one.base, amark_syntax_set_slot_at(set, 1u, 0, &owner));
    NAOS_CHECK_PTR_EQ(&ext_a, owner);
    NAOS_CHECK_UINT_EQ(1, amark_syntax_set_slot_count(set, AMARK_SLOT_KIND_AMARK_LAST));
    NAOS_CHECK_PTR_EQ(&b_slot_last.base,
                      amark_syntax_set_slot_at(set, AMARK_SLOT_KIND_AMARK_LAST, 0, &owner));
    NAOS_CHECK_PTR_EQ(&ext_b, owner);

    /* Kinds that no slot has, and 0. */
    NAOS_CHECK_UINT_EQ(0, amark_syntax_set_slot_count(set, 2u));
    NAOS_CHECK_UINT_EQ(0, amark_syntax_set_slot_count(set, AMARK_SLOT_KIND_USER_FIRST));
    NAOS_CHECK_UINT_EQ(0, amark_syntax_set_slot_count(set, UINT32_MAX));
    NAOS_CHECK_UINT_EQ(0, amark_syntax_set_slot_count(set, 0));
    NAOS_CHECK_NULL(amark_syntax_set_slot_at(set, 2u, 0, &owner));
    NAOS_CHECK_NULL(amark_syntax_set_slot_at(set, UINT32_MAX, 0, &owner));
    NAOS_CHECK_NULL(amark_syntax_set_slot_at(set, 0, 0, &owner));

    amark_syntax_set_destroy(set);
}

/* --- Clone and the static core set ---------------------------------------------------- */

NAOS_TEST(a_clone_has_the_same_extensions_and_is_not_frozen)
{
    /* 8.11 */
    amark_syntax_set_t *set;
    amark_syntax_set_t *clone = NULL;
    const amark_extension_t *expected[] = { &amark_core_extension, &ext_a, &ext_b };
    const amark_extension_t *changed[] = { &amark_core_extension, &ext_a, &ext_b, &ext_c };

    REQUIRE_CREATE(set);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &ext_a));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &ext_b));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_freeze(set));

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_clone(set, &clone));
    NAOS_REQUIRE_NOT_NULL(clone);
    NAOS_CHECK_FALSE(amark_syntax_set_is_frozen(clone));
    NAOS_CHECK_FALSE(clone->is_static);
    NAOS_CHECK_TRUE(has_extensions(clone, expected, 3));
    NAOS_CHECK_FALSE(amark_syntax_set_find_node_type(clone, "test.a_block", NULL));

    /* The clone changes on its own. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(clone, &ext_c));
    NAOS_CHECK_TRUE(has_extensions(clone, changed, 4));
    NAOS_CHECK_TRUE(has_extensions(set, expected, 3));

    amark_syntax_set_destroy(clone);

    /* A NULL set: *out_set is NULL. */
    clone = set;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_clone(NULL, &clone));
    NAOS_CHECK_NULL(clone);

    amark_syntax_set_destroy(set);
}

NAOS_TEST(the_core_set_is_static_frozen_and_holds_only_core)
{
    /* 1.2, 1.9, 8.10 */
    const amark_syntax_set_t *core = amark_syntax_set_core();
    amark_syntax_set_t *clone = NULL;
    amark_node_type_t i;

    NAOS_REQUIRE_NOT_NULL(core);
    NAOS_CHECK_PTR_EQ(core, amark_syntax_set_core());
    NAOS_CHECK_TRUE(amark_syntax_set_is_frozen(core));
    NAOS_CHECK_TRUE(core->is_static);
    NAOS_CHECK_UINT_EQ(1, amark_syntax_set_extension_count(core));
    NAOS_CHECK_PTR_EQ(&amark_core_extension, amark_syntax_set_extension_at(core, 0));
    NAOS_CHECK_UINT_EQ(0, amark_syntax_set_slot_count(core, 1u));

    /* Every built-in type is found by name and by descriptor (the const name index
     * is sorted and complete). */
    for (i = 1; i <= AMARK_BUILTIN_TYPE_COUNT; i++)
    {
        const amark_node_type_desc_t *d = amark_builtin_types[i];
        amark_node_type_t type = AMARK_NODE_NONE;

        NAOS_CHECK_PTR_EQ(d, amark_syntax_set_node_type_desc(core, i));
        NAOS_CHECK_PTR_EQ(d, amark_syntax_set_desc(core, i));
        NAOS_CHECK_TRUE(amark_syntax_set_find_node_type(core, d->name, &type));
        NAOS_CHECK_UINT_EQ(i, type);
        type = AMARK_NODE_NONE;
        NAOS_CHECK_TRUE(amark_syntax_set_node_type_of(core, d, &type));
        NAOS_CHECK_UINT_EQ(i, type);
    }

    NAOS_CHECK_UINT_EQ(AMARK_BUILTIN_TYPE_COUNT, core->name_index_count);
    for (i = 1; i < core->name_index_count; i++)
    {
        NAOS_CHECK_INT_LT(0, strcmp(core->name_index[i].name, core->name_index[i - 1].name));
    }

    NAOS_CHECK_NULL(amark_syntax_set_node_type_desc(core, AMARK_NODE_DYNAMIC_FIRST));
    NAOS_CHECK_FALSE(amark_syntax_set_find_node_type(core, "test.a_block", NULL));

    /* It cannot change, and destroying it does nothing. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_STATE,
                      amark_syntax_set_append((amark_syntax_set_t *)core, &ext_a));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_STATE,
                      amark_syntax_set_freeze((amark_syntax_set_t *)core));
    amark_syntax_set_destroy((amark_syntax_set_t *)core);
    amark_syntax_set_destroy((amark_syntax_set_t *)core);
    NAOS_CHECK_TRUE(amark_syntax_set_is_frozen(amark_syntax_set_core()));
    NAOS_CHECK_UINT_EQ(1, amark_syntax_set_extension_count(amark_syntax_set_core()));

    /* Its clone is a set on the heap that is not frozen. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_clone(core, &clone));
    NAOS_REQUIRE_NOT_NULL(clone);
    NAOS_CHECK_FALSE(amark_syntax_set_is_frozen(clone));
    NAOS_CHECK_FALSE(clone->is_static);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(clone, &ext_a));
    NAOS_CHECK_UINT_EQ(2, amark_syntax_set_extension_count(clone));
    amark_syntax_set_destroy(clone);
}

/* --- The slot kinds of the Amark specs ------------------------------------------------ */

/* The list of the design (SyntaxSet, 8.13): the kinds are named in
 * amark/extension.h by the specs that run them. */
typedef struct slot_kind_row
{
    uint32_t kind;
    const char *name;
} slot_kind_row_t;

static const slot_kind_row_t slot_kinds[] = {
    { 1u, "AMARK_SLOT_BLOCK_SYNTAX" },
    { 2u, "AMARK_SLOT_PARAGRAPH_CONSUMER" },
    { 3u, "AMARK_SLOT_POSTPROCESS" },
    { 4u, "AMARK_SLOT_PARSE_STATE" },
    { 5u, "AMARK_SLOT_INLINE_SYNTAX" },
    { 6u, "AMARK_SLOT_INLINE_DELIMITER" },
    { 7u, "AMARK_SLOT_INLINE_BRACKET" },
    { 8u, "AMARK_SLOT_INLINE_TEXT_SCANNER" },
    { 9u, "AMARK_SLOT_INLINE_NODE_TRAITS" },
    { 10u, "AMARK_SLOT_INLINE_SPECIAL" },
    { 11u, "AMARK_SLOT_HTML_NODE" },
    { 12u, "AMARK_SLOT_HTML_HOOKS" },
    { 13u, "AMARK_SLOT_MARKDOWN_NODE" },
    { 14u, "AMARK_SLOT_MARKDOWN_HOOKS" },
};

NAOS_TEST(the_amark_slot_kinds_do_not_overlap_and_fit_the_amark_range)
{
    /* 8.9, 8.13 */
    size_t count = sizeof(slot_kinds) / sizeof(slot_kinds[0]);
    size_t i;
    size_t j;

    NAOS_CHECK_UINT_EQ(1, AMARK_SLOT_KIND_AMARK_FIRST);
    NAOS_CHECK_UINT_EQ(255, AMARK_SLOT_KIND_AMARK_LAST);
    NAOS_CHECK_UINT_EQ(AMARK_SLOT_KIND_AMARK_LAST + 1u, AMARK_SLOT_KIND_USER_FIRST);

    for (i = 0; i < count; i++)
    {
        NAOS_CHECK_UINT_GE(slot_kinds[i].kind, AMARK_SLOT_KIND_AMARK_FIRST);
        NAOS_CHECK_UINT_LE(slot_kinds[i].kind, AMARK_SLOT_KIND_AMARK_LAST);
        for (j = i + 1; j < count; j++)
        {
            NAOS_CHECK_TRUE(slot_kinds[i].kind != slot_kinds[j].kind);
            NAOS_CHECK_STR_NE(slot_kinds[i].name, slot_kinds[j].name);
        }
    }
}

/* --- Out of memory ------------------------------------------------------------------- */

/* The context of the sweeps: set to false by a run that finds the set changed
 * after a failure. */
typedef struct oom_context
{
    bool unchanged;
} oom_context_t;

/* Creates a set with ext_a, then appends ext_b, ext_c and ext_d (which grows the
 * array); after a failed append, the set must be as before. */
static amark_error_t append_scenario(void *context)
{
    oom_context_t *ctx = context;
    const amark_extension_t *const all[] = { &amark_core_extension, &ext_a, &ext_b, &ext_c,
                                             &ext_d };
    const amark_extension_t *const *more = &all[2];
    amark_syntax_set_t *set = NULL;
    amark_error_t err;
    size_t i;

    err = amark_syntax_set_create(&set);
    if (err != AMARK_ERROR_NONE)
    {
        if (set != NULL)
        {
            ctx->unchanged = false;
        }

        return err;
    }

    err = amark_syntax_set_append(set, &ext_a);
    for (i = 0; (i < 3) && (err == AMARK_ERROR_NONE); i++)
    {
        err = amark_syntax_set_append(set, more[i]);
        if ((err != AMARK_ERROR_NONE) && !has_extensions(set, all, 2 + i))
        {
            ctx->unchanged = false;
        }
    }

    amark_syntax_set_destroy(set);

    return err;
}

/* Freezes a set; after a failure, the set must be not frozen and find nothing,
 * and freezing again (nothing fails twice) must succeed. */
static amark_error_t freeze_scenario(void *context)
{
    oom_context_t *ctx = context;
    amark_syntax_set_t *set = NULL;
    amark_node_type_t type;
    amark_error_t err;

    err = amark_syntax_set_create(&set);
    if (err == AMARK_ERROR_NONE)
    {
        err = amark_syntax_set_append(set, &ext_a);
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = amark_syntax_set_append(set, &ext_b);
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = amark_syntax_set_freeze(set);
        if (err != AMARK_ERROR_NONE)
        {
            if (amark_syntax_set_is_frozen(set)
                || amark_syntax_set_find_node_type(set, "test.a_block", &type)
                || (amark_syntax_set_slot_count(set, USER_KIND) != 0)
                || (amark_syntax_set_freeze(set) != AMARK_ERROR_NONE)
                || !amark_syntax_set_find_node_type(set, "test.b_block", &type) || (type != 258)
                || (amark_syntax_set_slot_count(set, USER_KIND) != 3))
            {
                ctx->unchanged = false;
            }
        }
    }

    amark_syntax_set_destroy(set);

    return err;
}

/* Clones a frozen set; after a failure, *out_set must be NULL and the source
 * unchanged. */
static amark_error_t clone_scenario(void *context)
{
    oom_context_t *ctx = context;
    const amark_extension_t *const expected[] = { &amark_core_extension, &ext_a, &ext_b, &ext_c,
                                                  &ext_d };
    amark_syntax_set_t *set = NULL;
    amark_syntax_set_t *clone = NULL;
    amark_error_t err;
    size_t i;

    err = amark_syntax_set_create(&set);
    for (i = 1; (i < 5) && (err == AMARK_ERROR_NONE); i++)
    {
        err = amark_syntax_set_append(set, expected[i]);
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = amark_syntax_set_freeze(set);
    }

    if (err == AMARK_ERROR_NONE)
    {
        /* Not NULL before the call, to see that a failure sets it to NULL. */
        clone = set;
        err = amark_syntax_set_clone(set, &clone);
        if (err != AMARK_ERROR_NONE)
        {
            if ((clone != NULL) || !has_extensions(set, expected, 5)
                || !amark_syntax_set_is_frozen(set))
            {
                ctx->unchanged = false;
            }

            clone = NULL;
        }
        else if (!has_extensions(clone, expected, 5) || amark_syntax_set_is_frozen(clone))
        {
            ctx->unchanged = false;
        }
    }

    amark_syntax_set_destroy(clone);
    amark_syntax_set_destroy(set);

    return err;
}

NAOS_TEST(append_passes_the_out_of_memory_sweep)
{
    oom_context_t ctx = { true };

    AMARK_CHECK_OOM_SWEEP(append_scenario, &ctx);
    NAOS_CHECK_TRUE(ctx.unchanged);
}

NAOS_TEST(freeze_passes_the_out_of_memory_sweep)
{
    oom_context_t ctx = { true };

    AMARK_CHECK_OOM_SWEEP(freeze_scenario, &ctx);
    NAOS_CHECK_TRUE(ctx.unchanged);
}

NAOS_TEST(clone_passes_the_out_of_memory_sweep)
{
    oom_context_t ctx = { true };

    AMARK_CHECK_OOM_SWEEP(clone_scenario, &ctx);
    NAOS_CHECK_TRUE(ctx.unchanged);
}

NAOS_SUITE(syntax_set,
    NAOS_SET_TEARDOWN(restore),
    NAOS_ADD_TEST(a_new_set_holds_only_core_and_is_not_frozen),
    NAOS_ADD_TEST(append_insert_remove_and_replace_keep_the_order),
    NAOS_ADD_TEST(core_cannot_be_removed_replaced_or_preceded),
    NAOS_ADD_TEST(unknown_names_and_null_arguments_are_rejected),
    NAOS_ADD_TEST(duplicate_extension_names_are_rejected),
    NAOS_ADD_TEST(invalid_descriptors_are_rejected_without_change),
    NAOS_ADD_TEST(boundary_descriptors_are_accepted),
    NAOS_ADD_TEST(freezing_gives_identifiers_from_256_in_order),
    NAOS_ADD_TEST(a_frozen_set_cannot_change),
    NAOS_ADD_TEST(duplicate_type_names_fail_freezing),
    NAOS_ADD_TEST(a_type_twice_in_one_extension_fails_freezing),
    NAOS_ADD_TEST(the_same_extensions_give_the_same_identifiers),
    NAOS_ADD_TEST(slots_are_listed_by_kind_in_the_order_of_the_set),
    NAOS_ADD_TEST(a_clone_has_the_same_extensions_and_is_not_frozen),
    NAOS_ADD_TEST(the_core_set_is_static_frozen_and_holds_only_core),
    NAOS_ADD_TEST(the_amark_slot_kinds_do_not_overlap_and_fit_the_amark_range),
    NAOS_ADD_TEST(append_passes_the_out_of_memory_sweep),
    NAOS_ADD_TEST(freeze_passes_the_out_of_memory_sweep),
    NAOS_ADD_TEST(clone_passes_the_out_of_memory_sweep))

NAOS_MAIN(
    NAOS_ADD_SUITE(syntax_set)
)
