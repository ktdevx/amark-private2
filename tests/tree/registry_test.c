/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The tests of Registry (syntax-tree): the checks of the facet, node type and
 * extension descriptors, the operations on syntax sets, freezing and the
 * identifiers, the lookups and the slots (12.1-12.3, 12.5-12.12, 14.3).
 */
#include "tree/tree.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <naos/naos.h>

#include "test_allocator.h"

/* ---- Descriptors used by the tests --------------------------------------- */

static const char *const align_names[] = {"left", "center", "right"};

/* A type with one facet of each kind. */
static const amark_facet_desc_t box_facets[] = {
    {sizeof(amark_facet_desc_t), "title", AMARK_FACET_VALUE, AMARK_FACET_STRING,
     AMARK_FACET_NO_LINE_BREAK, 0, 0, NULL, 0, 0, NULL, NULL, NULL, NULL},
    {sizeof(amark_facet_desc_t), "width", AMARK_FACET_VALUE, AMARK_FACET_INT, 0, 1, 100, NULL, 0,
     10, NULL, NULL, NULL, NULL},
    {sizeof(amark_facet_desc_t), "align", AMARK_FACET_STYLE, AMARK_FACET_ENUM, 0, 0, 0,
     align_names, 3, 0, NULL, NULL, NULL, NULL},
    {sizeof(amark_facet_desc_t), "tight", AMARK_FACET_VALUE, AMARK_FACET_BOOL,
     AMARK_FACET_OVERRIDABLE, 0, 0, NULL, 0, 0, NULL, NULL, NULL, NULL},
};

static const amark_node_type_desc_t box_type = {
    sizeof(amark_node_type_desc_t), "test.box", AMARK_NODE_LEVEL_BLOCK, AMARK_CONTENT_BLOCK,
    AMARK_CONTENT_BLOCK, AMARK_NODE_TYPE_MARGINS, box_facets, 4, 16,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL};

static const amark_node_type_desc_t span_type = {
    sizeof(amark_node_type_desc_t), "test.span", AMARK_NODE_LEVEL_INLINE, AMARK_CONTENT_INLINE,
    AMARK_CONTENT_INLINE, 0, NULL, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL};

static const amark_node_type_desc_t row_type = {
    sizeof(amark_node_type_desc_t), "test.row", AMARK_NODE_LEVEL_BLOCK, AMARK_CONTENT_BLOCK,
    AMARK_CONTENT_BLOCK, AMARK_NODE_TYPE_SHARE_LINES, NULL, 0, 0,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL};

/* Another descriptor with the name of box_type. */
static const amark_node_type_desc_t box_again_type = {
    sizeof(amark_node_type_desc_t), "test.box", AMARK_NODE_LEVEL_INLINE, AMARK_CONTENT_INLINE,
    0, 0, NULL, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL};

/* Slots: a structure of a slot kind embeds amark_slot_t first. */
typedef struct test_slot
{
    amark_slot_t base;
    int tag;
} test_slot_t;

#define KIND_A (AMARK_SLOT_KIND_USER_FIRST + 1u)
#define KIND_B (AMARK_SLOT_KIND_USER_FIRST + 2u)

static const test_slot_t slot_a1 = {{sizeof(test_slot_t), KIND_A}, 1};
static const test_slot_t slot_b1 = {{sizeof(test_slot_t), KIND_B}, 2};
static const test_slot_t slot_a2 = {{sizeof(test_slot_t), KIND_A}, 3};
static const test_slot_t slot_a3 = {{sizeof(test_slot_t), KIND_A}, 4};

static const amark_node_type_desc_t *const first_types[] = {&box_type, &span_type};
static const amark_slot_t *const first_slots[] = {&slot_a1.base, &slot_b1.base, &slot_a2.base};
static const amark_extension_t first_ext = {
    sizeof(amark_extension_t), "test.first", first_types, 2, first_slots, 3, NULL};

static const amark_node_type_desc_t *const second_types[] = {&row_type};
static const amark_slot_t *const second_slots[] = {&slot_a3.base};
static const amark_extension_t second_ext = {
    sizeof(amark_extension_t), "test.second", second_types, 1, second_slots, 1, NULL};

static const amark_extension_t third_ext = {
    sizeof(amark_extension_t), "test.third", NULL, 0, NULL, 0, NULL};

/* An extension whose name is that of first_ext. */
static const amark_extension_t first_again_ext = {
    sizeof(amark_extension_t), "test.first", NULL, 0, NULL, 0, NULL};

/* An extension with a type named like one of first_ext. */
static const amark_node_type_desc_t *const clash_types[] = {&box_again_type};
static const amark_extension_t clash_ext = {
    sizeof(amark_extension_t), "test.clash", clash_types, 1, NULL, 0, NULL};

/* ---- Helpers --------------------------------------------------------------- */

static amark_syntax_set_t *new_set(void)
{
    amark_syntax_set_t *set = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_create(&set));
    NAOS_REQUIRE_NOT_NULL(set);
    return set;
}

/* Checks that the extensions of set are names[0..count), in order. */
static void check_names(const amark_syntax_set_t *set, const char *const *names, size_t count)
{
    size_t i;

    NAOS_REQUIRE_UINT_EQ(count, amark_syntax_set_extension_count(set));
    for (i = 0; i < count; i++)
    {
        const amark_extension_t *extension = amark_syntax_set_extension_at(set, i);

        NAOS_REQUIRE_NOT_NULL(extension);
        NAOS_CHECK_STR_EQ(names[i], extension->name);
    }
    NAOS_CHECK_NULL(amark_syntax_set_extension_at(set, count));
}

/* An extension with the single node type type and no slot. */
typedef struct one_type_ext
{
    const amark_node_type_desc_t *types[1];
    amark_extension_t extension;
} one_type_ext_t;

static void make_one_type_ext(one_type_ext_t *e, const amark_node_type_desc_t *type)
{
    e->types[0] = type;
    memset(&e->extension, 0, sizeof(e->extension));
    e->extension.struct_size = sizeof(amark_extension_t);
    e->extension.name = "test.one";
    e->extension.node_types = e->types;
    e->extension.node_type_count = 1;
}

/* Checks that appending an extension with the type type is rejected as an invalid
 * argument and leaves a new set with only the built-in extension. */
static void check_type_rejected(const amark_node_type_desc_t *type)
{
    static const char *const core_only[] = {"amark.core"};
    amark_syntax_set_t *set = new_set();
    one_type_ext_t e;

    make_one_type_ext(&e, type);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_append(set, &e.extension));
    check_names(set, core_only, 1);
    amark_syntax_set_destroy(set);
}

/* Checks that an extension with a type that has the single facet facet is
 * rejected. */
static void check_facet_rejected(const amark_facet_desc_t *facet)
{
    amark_node_type_desc_t type = box_type;

    type.facets = facet;
    type.facet_count = 1;
    check_type_rejected(&type);
}

static void check_type_accepted(const amark_node_type_desc_t *type)
{
    amark_syntax_set_t *set = new_set();
    one_type_ext_t e;

    make_one_type_ext(&e, type);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &e.extension));
    amark_syntax_set_destroy(set);
}

/* ---- Creation and the built-in extension ---------------------------------- */

NAOS_TEST(create_holds_only_the_builtin_extension)
{
    static const char *const core_only[] = {"amark.core"};
    amark_syntax_set_t *set = new_set();

    check_names(set, core_only, 1);
    NAOS_CHECK_FALSE(amark_syntax_set_is_frozen(set));
    NAOS_CHECK_NOT_NULL(amark_syntax_set_find_extension(set, "amark.core"));
    NAOS_CHECK_NULL(amark_syntax_set_find_extension(set, "test.first"));
    amark_syntax_set_destroy(set);
}

NAOS_TEST(null_arguments_are_invalid)
{
    amark_syntax_set_t *set = new_set();
    amark_syntax_set_t *out = set;
    amark_node_type_t type = 7;

    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_create(NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_clone(NULL, &out));
    NAOS_CHECK_NULL(out);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_clone(set, NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_append(NULL, &first_ext));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_append(set, NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_syntax_set_insert_before(set, NULL, &first_ext));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_syntax_set_insert_before(set, "amark.core", NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_remove(set, NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_replace(set, NULL, &first_ext));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_freeze(NULL));
    NAOS_CHECK_FALSE(amark_syntax_set_is_frozen(NULL));
    NAOS_CHECK_UINT_EQ(0, amark_syntax_set_extension_count(NULL));
    NAOS_CHECK_NULL(amark_syntax_set_extension_at(NULL, 0));
    NAOS_CHECK_NULL(amark_syntax_set_find_extension(set, NULL));
    NAOS_CHECK_FALSE(amark_syntax_set_find_node_type(NULL, "test.box", &type));
    NAOS_CHECK_UINT_EQ(AMARK_NODE_NONE, type);
    NAOS_CHECK_UINT_EQ(0, amark_syntax_set_slot_count(NULL, KIND_A));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_require_frozen(NULL));
    amark_syntax_set_destroy(NULL);
    amark_syntax_set_destroy(set);
}

/* ---- Adding, inserting, removing and replacing (12.5, 12.6) ---------------- */

NAOS_TEST(append_keeps_the_order)
{
    static const char *const names[] = {"amark.core", "test.first", "test.second", "test.third"};
    amark_syntax_set_t *set = new_set();

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &first_ext));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &second_ext));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &third_ext));
    check_names(set, names, 4);
    NAOS_CHECK_PTR_EQ(&second_ext, amark_syntax_set_find_extension(set, "test.second"));
    amark_syntax_set_destroy(set);
}

NAOS_TEST(insert_remove_and_replace_by_name)
{
    static const char *const inserted[] = {"amark.core", "test.second", "test.first"};
    static const char *const replaced[] = {"amark.core", "test.third", "test.first"};
    static const char *const removed[] = {"amark.core", "test.third"};
    amark_syntax_set_t *set = new_set();

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &first_ext));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_syntax_set_insert_before(set, "test.first", &second_ext));
    check_names(set, inserted, 3);

    /* The replacement takes the same place. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_replace(set, "test.second", &third_ext));
    check_names(set, replaced, 3);

    /* A replacement by an extension of the same name. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_syntax_set_replace(set, "test.first", &first_again_ext));
    NAOS_CHECK_PTR_EQ(&first_again_ext, amark_syntax_set_extension_at(set, 2));

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_remove(set, "test.first"));
    check_names(set, removed, 2);
    amark_syntax_set_destroy(set);
}

NAOS_TEST(unknown_names_and_the_builtin_extension_are_rejected)
{
    static const char *const names[] = {"amark.core", "test.first"};
    amark_syntax_set_t *set = new_set();

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &first_ext));

    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_syntax_set_insert_before(set, "test.none", &second_ext));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_remove(set, "test.none"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_syntax_set_replace(set, "test.none", &second_ext));

    /* The built-in extension stays first and cannot be removed or replaced. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_syntax_set_insert_before(set, "amark.core", &second_ext));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_remove(set, "amark.core"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_syntax_set_replace(set, "amark.core", &second_ext));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_syntax_set_append(set, amark_syntax_set_extension_at(set, 0)));

    check_names(set, names, 2);
    amark_syntax_set_destroy(set);
}

NAOS_TEST(duplicate_extension_is_rejected_and_the_set_is_unchanged)
{
    static const char *const names[] = {"amark.core", "test.first", "test.second"};
    amark_syntax_set_t *set = new_set();

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &first_ext));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &second_ext));

    /* The same extension, and another one with the same name. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_append(set, &first_ext));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_syntax_set_append(set, &first_again_ext));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_syntax_set_insert_before(set, "test.second", &first_again_ext));
    /* A replacement whose name is that of another extension. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_syntax_set_replace(set, "test.second", &first_again_ext));

    check_names(set, names, 3);
    NAOS_CHECK_PTR_EQ(&first_ext, amark_syntax_set_extension_at(set, 1));
    amark_syntax_set_destroy(set);
}

/* ---- Checks of the descriptors (12.1-12.3, 12.6) --------------------------- */

NAOS_TEST(valid_descriptors_are_accepted)
{
    amark_node_type_desc_t type = box_type;
    amark_facet_desc_t facets[AMARK_FACET_MAX];
    char names[AMARK_FACET_MAX][8];
    size_t i;

    check_type_accepted(&box_type);
    check_type_accepted(&span_type);
    check_type_accepted(&row_type);

    /* 31 facets is the maximum. */
    for (i = 0; i < AMARK_FACET_MAX; i++)
    {
        facets[i] = box_facets[0];
        names[i][0] = 'f';
        names[i][1] = (char)('a' + i / 10);
        names[i][2] = (char)('0' + i % 10);
        names[i][3] = '\0';
        facets[i].name = names[i];
    }
    type.facets = facets;
    type.facet_count = AMARK_FACET_MAX;
    check_type_accepted(&type);
}

NAOS_TEST(more_than_31_facets_is_invalid)
{
    amark_node_type_desc_t type = box_type;
    amark_facet_desc_t facets[AMARK_FACET_MAX + 1];
    char names[AMARK_FACET_MAX + 1][8];
    size_t i;

    for (i = 0; i < AMARK_FACET_MAX + 1; i++)
    {
        facets[i] = box_facets[0];
        names[i][0] = 'f';
        names[i][1] = (char)('a' + i / 10);
        names[i][2] = (char)('0' + i % 10);
        names[i][3] = '\0';
        facets[i].name = names[i];
    }
    type.facets = facets;
    type.facet_count = AMARK_FACET_MAX + 1;
    check_type_rejected(&type);
}

NAOS_TEST(enum_without_names_is_invalid)
{
    amark_facet_desc_t facet = box_facets[2];

    facet.enum_names = NULL;
    check_facet_rejected(&facet);

    facet = box_facets[2];
    facet.enum_count = 0;
    check_facet_rejected(&facet);
}

NAOS_TEST(enum_with_a_null_name_is_invalid)
{
    static const char *const holes[] = {"a", NULL};
    amark_facet_desc_t facet = box_facets[2];

    facet.enum_names = holes;
    facet.enum_count = 2;
    check_facet_rejected(&facet);
}

NAOS_TEST(reversed_int_range_is_invalid)
{
    amark_facet_desc_t facet = box_facets[1];

    facet.int_min = 5;
    facet.int_max = 4;
    facet.default_number = 5;
    check_facet_rejected(&facet);
}

NAOS_TEST(style_with_overridable_is_invalid)
{
    amark_facet_desc_t facet = box_facets[3];

    facet.role = AMARK_FACET_STYLE;
    check_facet_rejected(&facet);
}

NAOS_TEST(other_invalid_facets)
{
    amark_facet_desc_t facet;
    amark_facet_desc_t two[2];
    amark_node_type_desc_t type = box_type;

    facet = box_facets[0];
    facet.struct_size = sizeof(amark_facet_desc_t) - 1;
    check_facet_rejected(&facet);

    facet = box_facets[0];
    facet.name = NULL;
    check_facet_rejected(&facet);

    facet = box_facets[0];
    facet.name = "Title";
    check_facet_rejected(&facet);

    facet = box_facets[0];
    facet.name = "1st";
    check_facet_rejected(&facet);

    facet = box_facets[0];
    facet.name = "";
    check_facet_rejected(&facet);

    facet = box_facets[0];
    facet.role = (amark_facet_role_t)0;
    check_facet_rejected(&facet);

    facet = box_facets[0];
    facet.kind = (amark_facet_kind_t)5;
    check_facet_rejected(&facet);

    facet = box_facets[0];
    facet.flags = 0x0100u;
    check_facet_rejected(&facet);

    /* A STRING flag on an INT facet. */
    facet = box_facets[1];
    facet.flags = AMARK_FACET_NO_LINE_BREAK;
    check_facet_rejected(&facet);

    /* CONTENT on a type without AMARK_NODE_TYPE_CONTENT. */
    facet = box_facets[0];
    facet.flags = AMARK_FACET_CONTENT | AMARK_FACET_LINES;
    check_facet_rejected(&facet);

    /* Default values out of range. */
    facet = box_facets[1];
    facet.default_number = 101;
    check_facet_rejected(&facet);

    facet = box_facets[2];
    facet.role = AMARK_FACET_VALUE;
    facet.default_number = 4;
    check_facet_rejected(&facet);

    facet = box_facets[3];
    facet.default_number = 2;
    check_facet_rejected(&facet);

    /* Two facets with the same name. */
    two[0] = box_facets[0];
    two[1] = box_facets[1];
    two[1].name = "title";
    type.facets = two;
    type.facet_count = 2;
    check_type_rejected(&type);
}

NAOS_TEST(content_facet_on_a_content_type_is_valid)
{
    amark_facet_desc_t facet = box_facets[0];
    amark_node_type_desc_t type = box_type;

    facet.flags = AMARK_FACET_CONTENT | AMARK_FACET_LINES;
    type.flags = AMARK_NODE_TYPE_CONTENT;
    type.accepts = 0;
    type.facets = &facet;
    type.facet_count = 1;
    check_type_accepted(&type);
}

NAOS_TEST(invalid_node_types)
{
    amark_node_type_desc_t type;

    type = box_type;
    type.struct_size = sizeof(amark_node_type_desc_t) - 1;
    check_type_rejected(&type);

    type = box_type;
    type.name = NULL;
    check_type_rejected(&type);

    type = box_type;
    type.name = "Test.box";
    check_type_rejected(&type);

    type = box_type;
    type.name = ".box";
    check_type_rejected(&type);

    type = box_type;
    type.level = (amark_node_level_t)0;
    check_type_rejected(&type);

    type = box_type;
    type.provides = 0;
    check_type_rejected(&type);

    type = box_type;
    type.provides = AMARK_CONTENT_BLOCK | AMARK_CONTENT_INLINE;
    check_type_rejected(&type);

    type = box_type;
    type.accepts = 0x08u;
    check_type_rejected(&type);

    type = box_type;
    type.flags = 0x08u;
    check_type_rejected(&type);

    /* The node type flags are for block types only. */
    type = span_type;
    type.flags = AMARK_NODE_TYPE_CONTENT;
    check_type_rejected(&type);

    type = box_type;
    type.facets = NULL;
    check_type_rejected(&type);

    type = box_type;
    type.data_size = AMARK_NODE_TYPE_MAX_DATA_SIZE + 1;
    check_type_rejected(&type);

    type = box_type;
    type.data_size = AMARK_NODE_TYPE_MAX_DATA_SIZE;
    check_type_accepted(&type);
}

NAOS_TEST(invalid_extensions)
{
    static const char *const core_only[] = {"amark.core"};
    static const amark_node_type_desc_t *const null_type[] = {NULL};
    static const amark_slot_t *const null_slot[] = {NULL};
    static const amark_slot_t zero_kind = {sizeof(amark_slot_t), 0};
    static const amark_slot_t small_slot = {sizeof(amark_slot_t) - 1, KIND_A};
    static const amark_slot_t *const zero_kind_slots[] = {&zero_kind};
    static const amark_slot_t *const small_slots[] = {&small_slot};
    amark_syntax_set_t *set = new_set();
    amark_extension_t extension;

    extension = third_ext;
    extension.struct_size = sizeof(amark_extension_t) - 1;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_append(set, &extension));

    extension = third_ext;
    extension.name = NULL;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_append(set, &extension));

    extension = third_ext;
    extension.name = "Bad";
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_append(set, &extension));

    extension = third_ext;
    extension.node_type_count = 1;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_append(set, &extension));

    extension = third_ext;
    extension.node_types = null_type;
    extension.node_type_count = 1;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_append(set, &extension));

    extension = third_ext;
    extension.slot_count = 1;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_append(set, &extension));

    extension = third_ext;
    extension.slots = null_slot;
    extension.slot_count = 1;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_append(set, &extension));

    extension = third_ext;
    extension.slots = zero_kind_slots;
    extension.slot_count = 1;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_append(set, &extension));

    extension = third_ext;
    extension.slots = small_slots;
    extension.slot_count = 1;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_append(set, &extension));

    /* An invalid descriptor is rejected by every operation that adds one. */
    extension = third_ext;
    extension.name = "Bad";
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &first_ext));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_syntax_set_insert_before(set, "test.first", &extension));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_syntax_set_replace(set, "test.first", &extension));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_remove(set, "test.first"));

    check_names(set, core_only, 1);
    amark_syntax_set_destroy(set);
}

/* ---- Freezing (12.7, 12.8) ------------------------------------------------- */

NAOS_TEST(frozen_set_cannot_be_changed)
{
    static const char *const names[] = {"amark.core", "test.first"};
    amark_syntax_set_t *set = new_set();

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &first_ext));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_freeze(set));
    NAOS_CHECK_TRUE(amark_syntax_set_is_frozen(set));

    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_STATE, amark_syntax_set_append(set, &second_ext));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_STATE,
                      amark_syntax_set_insert_before(set, "test.first", &second_ext));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_STATE, amark_syntax_set_remove(set, "test.first"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_STATE,
                      amark_syntax_set_replace(set, "test.first", &second_ext));
    check_names(set, names, 2);

    /* Freezing again does nothing. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_freeze(set));
    NAOS_CHECK_TRUE(amark_syntax_set_is_frozen(set));
    amark_syntax_set_destroy(set);
}

NAOS_TEST(unfrozen_set_cannot_be_used)
{
    amark_syntax_set_t *set = new_set();
    const amark_extension_t *extension = &third_ext;
    amark_node_type_t type = 7;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &first_ext));

    /* A document cannot be created with it (the check document creation uses). */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_STATE, amark_syntax_set_require_frozen(set));

    /* The queries find nothing before freezing. */
    NAOS_CHECK_FALSE(amark_syntax_set_find_node_type(set, "test.box", &type));
    NAOS_CHECK_UINT_EQ(AMARK_NODE_NONE, type);
    type = 7;
    NAOS_CHECK_FALSE(amark_syntax_set_node_type_of(set, &box_type, &type));
    NAOS_CHECK_UINT_EQ(AMARK_NODE_NONE, type);
    NAOS_CHECK_NULL(amark_syntax_set_node_type_desc(set, AMARK_NODE_DYNAMIC_FIRST));
    NAOS_CHECK_UINT_EQ(0, amark_syntax_set_slot_count(set, KIND_A));
    NAOS_CHECK_NULL(amark_syntax_set_slot_at(set, KIND_A, 0, &extension));
    NAOS_CHECK_NULL(extension);

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_freeze(set));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_require_frozen(set));
    NAOS_CHECK_TRUE(amark_syntax_set_find_node_type(set, "test.box", &type));
    amark_syntax_set_destroy(set);
}

NAOS_TEST(duplicate_type_names_fail_to_freeze)
{
    amark_syntax_set_t *set = new_set();
    amark_node_type_t type = 7;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &first_ext));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &clash_ext));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_freeze(set));
    NAOS_CHECK_FALSE(amark_syntax_set_is_frozen(set));
    NAOS_CHECK_FALSE(amark_syntax_set_find_node_type(set, "test.box", &type));

    /* The set can still be changed, then frozen. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_remove(set, "test.clash"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_freeze(set));
    NAOS_CHECK_TRUE(amark_syntax_set_is_frozen(set));
    amark_syntax_set_destroy(set);
}

NAOS_TEST(the_same_type_twice_fails_to_freeze)
{
    static const amark_node_type_desc_t *const box_only[] = {&box_type};
    static const amark_extension_t box_ext = {
        sizeof(amark_extension_t), "test.box_ext", box_only, 1, NULL, 0, NULL};
    amark_syntax_set_t *set = new_set();

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &first_ext));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &box_ext));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_freeze(set));
    NAOS_CHECK_FALSE(amark_syntax_set_is_frozen(set));
    amark_syntax_set_destroy(set);
}

/* ---- Identifiers and lookups (12.7, 12.11) --------------------------------- */

NAOS_TEST(freeze_assigns_identifiers_in_the_order_of_the_set)
{
    amark_syntax_set_t *set = new_set();
    amark_node_type_t type = AMARK_NODE_NONE;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &first_ext));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &second_ext));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_freeze(set));

    NAOS_CHECK_TRUE(amark_syntax_set_find_node_type(set, "test.box", &type));
    NAOS_CHECK_UINT_EQ(AMARK_NODE_DYNAMIC_FIRST, type);
    NAOS_CHECK_TRUE(amark_syntax_set_find_node_type(set, "test.span", &type));
    NAOS_CHECK_UINT_EQ(AMARK_NODE_DYNAMIC_FIRST + 1u, type);
    NAOS_CHECK_TRUE(amark_syntax_set_find_node_type(set, "test.row", &type));
    NAOS_CHECK_UINT_EQ(AMARK_NODE_DYNAMIC_FIRST + 2u, type);

    type = 7;
    NAOS_CHECK_FALSE(amark_syntax_set_find_node_type(set, "test.none", &type));
    NAOS_CHECK_UINT_EQ(AMARK_NODE_NONE, type);
    NAOS_CHECK_FALSE(amark_syntax_set_find_node_type(set, "test.box", NULL));

    NAOS_CHECK_TRUE(amark_syntax_set_node_type_of(set, &row_type, &type));
    NAOS_CHECK_UINT_EQ(AMARK_NODE_DYNAMIC_FIRST + 2u, type);
    /* Another descriptor with the same contents is not the type. */
    {
        amark_node_type_desc_t copy = row_type;

        NAOS_CHECK_FALSE(amark_syntax_set_node_type_of(set, &copy, &type));
        NAOS_CHECK_UINT_EQ(AMARK_NODE_NONE, type);
    }

    NAOS_CHECK_PTR_EQ(&box_type, amark_syntax_set_node_type_desc(set, AMARK_NODE_DYNAMIC_FIRST));
    NAOS_CHECK_PTR_EQ(&row_type,
                      amark_syntax_set_node_type_desc(set, AMARK_NODE_DYNAMIC_FIRST + 2u));
    NAOS_CHECK_NULL(amark_syntax_set_node_type_desc(set, AMARK_NODE_DYNAMIC_FIRST + 3u));
    NAOS_CHECK_NULL(amark_syntax_set_node_type_desc(set, AMARK_NODE_NONE));
    NAOS_CHECK_NULL(amark_syntax_set_node_type_desc(set, AMARK_NODE_BUILTIN_LAST));
    amark_syntax_set_destroy(set);
}

/* Builds and freezes a set with the extensions in order. */
static amark_syntax_set_t *build_frozen(const amark_extension_t *const *extensions, size_t count)
{
    amark_syntax_set_t *set = new_set();
    size_t i;

    for (i = 0; i < count; i++)
    {
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, extensions[i]));
    }
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_freeze(set));
    return set;
}

static amark_node_type_t type_of(const amark_syntax_set_t *set, const char *name)
{
    amark_node_type_t type = AMARK_NODE_NONE;

    NAOS_CHECK_TRUE(amark_syntax_set_find_node_type(set, name, &type));
    return type;
}

NAOS_TEST(the_same_composition_assigns_the_same_identifiers)
{
    static const amark_extension_t *const order[] = {&first_ext, &second_ext, &third_ext};
    static const amark_extension_t *const swapped[] = {&second_ext, &first_ext};
    static const char *const names[] = {"test.box", "test.span", "test.row"};
    amark_syntax_set_t *a = build_frozen(order, 3);
    amark_syntax_set_t *b = build_frozen(order, 3);
    amark_syntax_set_t *c = NULL;
    amark_syntax_set_t *d = build_frozen(swapped, 2);
    size_t i;

    /* A set built by insertions into another order, then frozen. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_create(&c));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(c, &third_ext));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_insert_before(c, "test.third", &second_ext));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_insert_before(c, "test.second", &first_ext));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_freeze(c));

    for (i = 0; i < 3; i++)
    {
        NAOS_CHECK_UINT_EQ(type_of(a, names[i]), type_of(b, names[i]));
        NAOS_CHECK_UINT_EQ(type_of(a, names[i]), type_of(c, names[i]));
    }

    /* Another order assigns other identifiers. */
    NAOS_CHECK_UINT_EQ(AMARK_NODE_DYNAMIC_FIRST, type_of(d, "test.row"));
    NAOS_CHECK_UINT_EQ(AMARK_NODE_DYNAMIC_FIRST + 1u, type_of(d, "test.box"));

    amark_syntax_set_destroy(a);
    amark_syntax_set_destroy(b);
    amark_syntax_set_destroy(c);
    amark_syntax_set_destroy(d);
}

NAOS_TEST(facets_are_found_by_name_and_number)
{
    NAOS_CHECK_UINT_EQ(1, amark_node_type_find_facet(&box_type, "title"));
    NAOS_CHECK_UINT_EQ(4, amark_node_type_find_facet(&box_type, "tight"));
    NAOS_CHECK_UINT_EQ(AMARK_FACET_NONE, amark_node_type_find_facet(&box_type, "none"));
    NAOS_CHECK_UINT_EQ(AMARK_FACET_NONE, amark_node_type_find_facet(&box_type, NULL));
    NAOS_CHECK_UINT_EQ(AMARK_FACET_NONE, amark_node_type_find_facet(NULL, "title"));
    NAOS_CHECK_UINT_EQ(AMARK_FACET_NONE, amark_node_type_find_facet(&span_type, "title"));

    NAOS_CHECK_PTR_EQ(&box_facets[0], amark_node_type_facet(&box_type, 1));
    NAOS_CHECK_PTR_EQ(&box_facets[3], amark_node_type_facet(&box_type, 4));
    NAOS_CHECK_NULL(amark_node_type_facet(&box_type, AMARK_FACET_NONE));
    NAOS_CHECK_NULL(amark_node_type_facet(&box_type, 5));
    NAOS_CHECK_NULL(amark_node_type_facet(NULL, 1));
}

/* ---- Slots (12.3, 12.9) ---------------------------------------------------- */

NAOS_TEST(slots_are_enumerated_by_kind_in_the_order_of_the_set)
{
    static const amark_extension_t *const order[] = {&second_ext, &first_ext};
    amark_syntax_set_t *set = build_frozen(order, 2);
    const amark_extension_t *extension = NULL;
    const amark_slot_t *slot;

    NAOS_REQUIRE_UINT_EQ(3, amark_syntax_set_slot_count(set, KIND_A));
    slot = amark_syntax_set_slot_at(set, KIND_A, 0, &extension);
    NAOS_CHECK_PTR_EQ(&slot_a3.base, slot);
    NAOS_CHECK_PTR_EQ(&second_ext, extension);
    slot = amark_syntax_set_slot_at(set, KIND_A, 1, &extension);
    NAOS_CHECK_PTR_EQ(&slot_a1.base, slot);
    NAOS_CHECK_PTR_EQ(&first_ext, extension);
    slot = amark_syntax_set_slot_at(set, KIND_A, 2, NULL);
    NAOS_CHECK_PTR_EQ(&slot_a2.base, slot);
    NAOS_CHECK_INT_EQ(3, ((const test_slot_t *)slot)->tag);

    NAOS_CHECK_NULL(amark_syntax_set_slot_at(set, KIND_A, 3, &extension));
    NAOS_CHECK_NULL(extension);

    NAOS_REQUIRE_UINT_EQ(1, amark_syntax_set_slot_count(set, KIND_B));
    NAOS_CHECK_PTR_EQ(&slot_b1.base, amark_syntax_set_slot_at(set, KIND_B, 0, &extension));
    NAOS_CHECK_PTR_EQ(&first_ext, extension);

    NAOS_CHECK_UINT_EQ(0, amark_syntax_set_slot_count(set, AMARK_SLOT_BLOCK_SYNTAX));
    NAOS_CHECK_UINT_EQ(0, amark_syntax_set_slot_count(set, 0));
    NAOS_CHECK_NULL(amark_syntax_set_slot_at(set, AMARK_SLOT_HTML_NODE, 0, NULL));
    amark_syntax_set_destroy(set);
}

NAOS_TEST(slot_kinds_are_one_serial_list)
{
    static const uint32_t kinds[] = {
        AMARK_SLOT_BLOCK_SYNTAX,       AMARK_SLOT_PARAGRAPH_CONSUMER, AMARK_SLOT_POSTPROCESS,
        AMARK_SLOT_PARSE_STATE,        AMARK_SLOT_INLINE_SYNTAX,      AMARK_SLOT_INLINE_DELIMITER,
        AMARK_SLOT_INLINE_BRACKET,     AMARK_SLOT_INLINE_TEXT_SCANNER,
        AMARK_SLOT_INLINE_NODE_TRAITS, AMARK_SLOT_INLINE_SPECIAL,     AMARK_SLOT_HTML_NODE,
        AMARK_SLOT_HTML_HOOKS,         AMARK_SLOT_MARKDOWN_NODE,      AMARK_SLOT_MARKDOWN_HOOKS};
    size_t i;

    for (i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++)
    {
        NAOS_CHECK_UINT_EQ(i + 1, kinds[i]);
        NAOS_CHECK_TRUE(kinds[i] >= AMARK_SLOT_KIND_AMARK_FIRST);
        NAOS_CHECK_TRUE(kinds[i] <= AMARK_SLOT_KIND_AMARK_LAST);
    }
    NAOS_CHECK_UINT_EQ(AMARK_SLOT_KIND_AMARK_LAST + 1u, AMARK_SLOT_KIND_USER_FIRST);
}

/* ---- Cloning (12.12) ------------------------------------------------------- */

NAOS_TEST(clone_copies_the_extensions_unfrozen)
{
    static const char *const names[] = {"amark.core", "test.first", "test.second"};
    static const char *const changed[] = {"amark.core", "test.first", "test.second",
                                          "test.third"};
    static const amark_extension_t *const order[] = {&first_ext, &second_ext};
    amark_syntax_set_t *set = build_frozen(order, 2);
    amark_syntax_set_t *copy = NULL;
    amark_syntax_set_t *copy2 = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_clone(set, &copy));
    NAOS_REQUIRE_NOT_NULL(copy);
    NAOS_CHECK_FALSE(amark_syntax_set_is_frozen(copy));
    check_names(copy, names, 3);

    /* The copy can be changed; the original is not. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(copy, &third_ext));
    check_names(copy, changed, 4);
    check_names(set, names, 3);

    /* A copy of a set that is not frozen. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_clone(copy, &copy2));
    check_names(copy2, changed, 4);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_freeze(copy2));
    NAOS_CHECK_UINT_EQ(type_of(set, "test.row"), type_of(copy2, "test.row"));

    amark_syntax_set_destroy(set);
    amark_syntax_set_destroy(copy);
    amark_syntax_set_destroy(copy2);
}

/* ---- Out of memory (1.7) --------------------------------------------------- */

/* Runs one change of set and checks that a failure leaves the extensions as they
 * were: count extensions, the last of them named last. */
static amark_error_t check_unchanged(amark_error_t error, const amark_syntax_set_t *set,
                                     size_t count, const char *last)
{
    const amark_extension_t *extension;

    if (error == AMARK_ERROR_NONE)
    {
        return error;
    }
    extension = amark_syntax_set_extension_at(set, count - 1);
    if (amark_syntax_set_extension_count(set) != count || extension == NULL ||
        strcmp(extension->name, last) != 0 || amark_syntax_set_is_frozen(set))
    {
        return AMARK_ERROR_INVALID_STATE;
    }
    return error;
}

static amark_error_t oom_scenario(void *context)
{
    amark_syntax_set_t *set = NULL;
    amark_syntax_set_t *copy = NULL;
    amark_error_t error;

    (void)context;
    error = amark_syntax_set_create(&set);
    if (error != AMARK_ERROR_NONE)
    {
        return set == NULL ? error : AMARK_ERROR_INVALID_STATE;
    }
    error = check_unchanged(amark_syntax_set_append(set, &first_ext), set, 1, "amark.core");
    if (error == AMARK_ERROR_NONE)
    {
        error = check_unchanged(amark_syntax_set_append(set, &third_ext), set, 2, "test.first");
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = check_unchanged(amark_syntax_set_insert_before(set, "test.third", &second_ext),
                                set, 3, "test.third");
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_syntax_set_clone(set, &copy);
        if (error != AMARK_ERROR_NONE && copy != NULL)
        {
            error = AMARK_ERROR_INVALID_STATE;
        }
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = check_unchanged(amark_syntax_set_freeze(copy), copy, 4, "test.third");
    }
    if (error == AMARK_ERROR_NONE && (amark_syntax_set_slot_count(copy, KIND_A) != 3 ||
                                      type_of(copy, "test.row") != AMARK_NODE_DYNAMIC_FIRST + 2u))
    {
        error = AMARK_ERROR_INVALID_STATE;
    }
    if (error == AMARK_ERROR_NONE)
    {
        /* The original can still be changed after the copy is frozen. */
        error = check_unchanged(amark_syntax_set_replace(set, "test.second", &clash_ext), set, 4,
                                "test.third");
    }
    amark_syntax_set_destroy(copy);
    amark_syntax_set_destroy(set);
    return error;
}

NAOS_TEST(out_of_memory_leaves_the_sets_unchanged)
{
    AMARK_CHECK_OOM_SWEEP(oom_scenario, NULL);
}

NAOS_SUITE(registry,
    NAOS_ADD_TEST(create_holds_only_the_builtin_extension),
    NAOS_ADD_TEST(null_arguments_are_invalid),
    NAOS_ADD_TEST(append_keeps_the_order),
    NAOS_ADD_TEST(insert_remove_and_replace_by_name),
    NAOS_ADD_TEST(unknown_names_and_the_builtin_extension_are_rejected),
    NAOS_ADD_TEST(duplicate_extension_is_rejected_and_the_set_is_unchanged),
    NAOS_ADD_TEST(valid_descriptors_are_accepted),
    NAOS_ADD_TEST(more_than_31_facets_is_invalid),
    NAOS_ADD_TEST(enum_without_names_is_invalid),
    NAOS_ADD_TEST(enum_with_a_null_name_is_invalid),
    NAOS_ADD_TEST(reversed_int_range_is_invalid),
    NAOS_ADD_TEST(style_with_overridable_is_invalid),
    NAOS_ADD_TEST(other_invalid_facets),
    NAOS_ADD_TEST(content_facet_on_a_content_type_is_valid),
    NAOS_ADD_TEST(invalid_node_types),
    NAOS_ADD_TEST(invalid_extensions),
    NAOS_ADD_TEST(frozen_set_cannot_be_changed),
    NAOS_ADD_TEST(unfrozen_set_cannot_be_used),
    NAOS_ADD_TEST(duplicate_type_names_fail_to_freeze),
    NAOS_ADD_TEST(the_same_type_twice_fails_to_freeze),
    NAOS_ADD_TEST(freeze_assigns_identifiers_in_the_order_of_the_set),
    NAOS_ADD_TEST(the_same_composition_assigns_the_same_identifiers),
    NAOS_ADD_TEST(facets_are_found_by_name_and_number),
    NAOS_ADD_TEST(slots_are_enumerated_by_kind_in_the_order_of_the_set),
    NAOS_ADD_TEST(slot_kinds_are_one_serial_list),
    NAOS_ADD_TEST(clone_copies_the_extensions_unfrozen),
    NAOS_ADD_TEST(out_of_memory_leaves_the_sets_unchanged)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(registry)
)
