/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The tests of BuiltinTypes (syntax-tree): the identifiers and the names of the
 * 19 built-in node types, their levels, flags and containment rules, the facets
 * they declare, the label check, the built-in extension "amark.core" and the
 * static built-in syntax set (3.1, 3.5, 3.8, 3.9, 6.6, 12.4).
 */
#include "tree/tree.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <naos/naos.h>

/* ---- The expected types ---------------------------------------------------- */

typedef struct expected_type
{
    amark_node_type_t id;
    const char *name;
    amark_node_level_t level;
    uint32_t provides;
    uint32_t accepts;
    uint32_t flags;
    size_t facet_count;
} expected_type_t;

#define B AMARK_NODE_LEVEL_BLOCK
#define I AMARK_NODE_LEVEL_INLINE
#define CB AMARK_CONTENT_BLOCK
#define CI AMARK_CONTENT_INLINE
#define CL AMARK_CONTENT_LIST_ITEM

static const expected_type_t expected_types[] = {
    {AMARK_NODE_DOCUMENT, "document", B, CB, CB, 0, 0},
    {AMARK_NODE_BLOCK_QUOTE, "block_quote", B, CB, CB, AMARK_NODE_TYPE_MARGINS, 0},
    {AMARK_NODE_LIST, "list", B, CB, CL, 0, 5},
    {AMARK_NODE_ITEM, "item", B, CL, CB, AMARK_NODE_TYPE_MARGINS, 0},
    {AMARK_NODE_CODE_BLOCK, "code_block", B, CB, 0, AMARK_NODE_TYPE_CONTENT, 5},
    {AMARK_NODE_HTML_BLOCK, "html_block", B, CB, 0, AMARK_NODE_TYPE_CONTENT, 1},
    {AMARK_NODE_PARAGRAPH, "paragraph", B, CB, CI, AMARK_NODE_TYPE_CONTENT, 0},
    {AMARK_NODE_HEADING, "heading", B, CB, CI, AMARK_NODE_TYPE_CONTENT, 3},
    {AMARK_NODE_THEMATIC_BREAK, "thematic_break", B, CB, 0, 0, 3},
    {AMARK_NODE_LINK_DEFINITION, "link_definition", B, CB, 0, AMARK_NODE_TYPE_CONTENT, 5},
    {AMARK_NODE_TEXT, "text", I, CI, 0, 0, 1},
    {AMARK_NODE_SOFTBREAK, "softbreak", I, CI, 0, 0, 0},
    {AMARK_NODE_LINEBREAK, "linebreak", I, CI, 0, 0, 1},
    {AMARK_NODE_CODE, "code", I, CI, 0, 0, 3},
    {AMARK_NODE_HTML_INLINE, "html_inline", I, CI, 0, 0, 1},
    {AMARK_NODE_EMPH, "emph", I, CI, CI, 0, 1},
    {AMARK_NODE_STRONG, "strong", I, CI, CI, 0, 1},
    {AMARK_NODE_LINK, "link", I, CI, CI, 0, 6},
    {AMARK_NODE_IMAGE, "image", I, CI, CI, 0, 6},
};

#define TYPE_COUNT (sizeof(expected_types) / sizeof(expected_types[0]))

#undef B
#undef I
#undef CB
#undef CI
#undef CL

static const amark_syntax_set_t *core_set(void)
{
    const amark_syntax_set_t *set = amark_syntax_set_core();

    NAOS_REQUIRE_NOT_NULL(set);
    return set;
}

/* ---- Identifiers and names (3.1, 3.9) ------------------------------------- */

NAOS_TEST(identifiers_are_fixed)
{
    NAOS_CHECK_UINT_EQ(0, AMARK_NODE_NONE);
    NAOS_CHECK_UINT_EQ(1, AMARK_NODE_DOCUMENT);
    NAOS_CHECK_UINT_EQ(2, AMARK_NODE_BLOCK_QUOTE);
    NAOS_CHECK_UINT_EQ(3, AMARK_NODE_LIST);
    NAOS_CHECK_UINT_EQ(4, AMARK_NODE_ITEM);
    NAOS_CHECK_UINT_EQ(5, AMARK_NODE_CODE_BLOCK);
    NAOS_CHECK_UINT_EQ(6, AMARK_NODE_HTML_BLOCK);
    NAOS_CHECK_UINT_EQ(7, AMARK_NODE_PARAGRAPH);
    NAOS_CHECK_UINT_EQ(8, AMARK_NODE_HEADING);
    NAOS_CHECK_UINT_EQ(9, AMARK_NODE_THEMATIC_BREAK);
    NAOS_CHECK_UINT_EQ(10, AMARK_NODE_LINK_DEFINITION);
    NAOS_CHECK_UINT_EQ(11, AMARK_NODE_TEXT);
    NAOS_CHECK_UINT_EQ(12, AMARK_NODE_SOFTBREAK);
    NAOS_CHECK_UINT_EQ(13, AMARK_NODE_LINEBREAK);
    NAOS_CHECK_UINT_EQ(14, AMARK_NODE_CODE);
    NAOS_CHECK_UINT_EQ(15, AMARK_NODE_HTML_INLINE);
    NAOS_CHECK_UINT_EQ(16, AMARK_NODE_EMPH);
    NAOS_CHECK_UINT_EQ(17, AMARK_NODE_STRONG);
    NAOS_CHECK_UINT_EQ(18, AMARK_NODE_LINK);
    NAOS_CHECK_UINT_EQ(19, AMARK_NODE_IMAGE);
    NAOS_CHECK_UINT_EQ(255, AMARK_NODE_BUILTIN_LAST);
    NAOS_CHECK_UINT_EQ(256, AMARK_NODE_DYNAMIC_FIRST);
}

/* Checks that the frozen set set has the 19 built-in types with their
 * identifiers, names, levels, content classes and flags. */
static void check_builtin_types(const amark_syntax_set_t *set)
{
    size_t i;

    NAOS_REQUIRE_TRUE(amark_syntax_set_is_frozen(set));
    for (i = 0; i < TYPE_COUNT; i++)
    {
        const expected_type_t *expected = &expected_types[i];
        const amark_node_type_desc_t *desc = amark_syntax_set_node_type_desc(set, expected->id);
        amark_node_type_t type = AMARK_NODE_NONE;

        NAOS_CHECK_UINT_EQ(i + 1, expected->id);
        NAOS_REQUIRE_NOT_NULL(desc);
        NAOS_CHECK_STR_EQ(expected->name, desc->name);
        NAOS_CHECK_TRUE(amark_syntax_set_find_node_type(set, expected->name, &type));
        NAOS_CHECK_UINT_EQ(expected->id, type);
        NAOS_CHECK_TRUE(amark_syntax_set_node_type_of(set, desc, &type));
        NAOS_CHECK_UINT_EQ(expected->id, type);
        NAOS_CHECK_INT_EQ(expected->level, desc->level);
        NAOS_CHECK_UINT_EQ(expected->provides, desc->provides);
        NAOS_CHECK_UINT_EQ(expected->accepts, desc->accepts);
        NAOS_CHECK_UINT_EQ(expected->flags, desc->flags);
        NAOS_CHECK_UINT_EQ(expected->facet_count, desc->facet_count);
        NAOS_CHECK_UINT_EQ(0, desc->data_size);
        NAOS_CHECK_UINT_EQ(sizeof(amark_node_type_desc_t), desc->struct_size);
    }
    NAOS_CHECK_NULL(amark_syntax_set_node_type_desc(set, AMARK_NODE_NONE));
    NAOS_CHECK_NULL(amark_syntax_set_node_type_desc(set, TYPE_COUNT + 1));
}

NAOS_TEST(core_set_has_the_builtin_types)
{
    const amark_syntax_set_t *set = core_set();
    const amark_extension_t *core;

    NAOS_CHECK_PTR_EQ(set, amark_syntax_set_core());
    check_builtin_types(set);
    NAOS_REQUIRE_UINT_EQ(1, amark_syntax_set_extension_count(set));
    core = amark_syntax_set_extension_at(set, 0);
    NAOS_REQUIRE_NOT_NULL(core);
    NAOS_CHECK_PTR_EQ(amark_tree_core_extension(), core);
    NAOS_CHECK_STR_EQ("amark.core", core->name);
    NAOS_CHECK_UINT_EQ(TYPE_COUNT, core->node_type_count);
    NAOS_CHECK_UINT_EQ(0, core->slot_count);
    NAOS_CHECK_PTR_EQ(core, amark_syntax_set_find_extension(set, "amark.core"));
    NAOS_CHECK_UINT_EQ(0, amark_syntax_set_slot_count(set, UINT32_MAX));
}

NAOS_TEST(created_set_starts_with_the_builtin_extension)
{
    amark_syntax_set_t *set = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_create(&set));
    NAOS_REQUIRE_NOT_NULL(set);
    NAOS_CHECK_PTR_EQ(amark_tree_core_extension(), amark_syntax_set_extension_at(set, 0));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_freeze(set));
    check_builtin_types(set);
    amark_syntax_set_destroy(set);
}

/* 12.4: the built-in extension cannot be removed, replaced or moved. */
NAOS_TEST(builtin_extension_cannot_be_removed)
{
    static const amark_extension_t other = {
        sizeof(amark_extension_t), "test.other", NULL, 0, NULL, 0, NULL};
    amark_syntax_set_t *set = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_create(&set));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_remove(set, "amark.core"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_syntax_set_replace(set, "amark.core", &other));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_syntax_set_insert_before(set, "amark.core", &other));
    NAOS_CHECK_UINT_EQ(1, amark_syntax_set_extension_count(set));
    NAOS_CHECK_PTR_EQ(amark_tree_core_extension(), amark_syntax_set_extension_at(set, 0));
    amark_syntax_set_destroy(set);
}

/* The static set is frozen: changes are refused, a clone is a normal set, and
 * destroying it does nothing. */
NAOS_TEST(core_set_is_frozen_and_static)
{
    static const amark_extension_t other = {
        sizeof(amark_extension_t), "test.other", NULL, 0, NULL, 0, NULL};
    const amark_syntax_set_t *set = core_set();
    amark_syntax_set_t *mutable_set = (amark_syntax_set_t *)(uintptr_t)set;
    amark_syntax_set_t *copy = NULL;

    NAOS_CHECK_TRUE(amark_syntax_set_is_frozen(set));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_require_frozen(set));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_STATE, amark_syntax_set_append(mutable_set, &other));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_STATE,
                      amark_syntax_set_remove(mutable_set, "amark.core"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_freeze(mutable_set));

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_clone(set, &copy));
    NAOS_CHECK_FALSE(amark_syntax_set_is_frozen(copy));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(copy, &other));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_freeze(copy));
    check_builtin_types(copy);
    amark_syntax_set_destroy(copy);

    amark_syntax_set_destroy(mutable_set);
    check_builtin_types(set);
}

/* 3.9: the built-in descriptors pass the same checks as the descriptors of an
 * extension. */
NAOS_TEST(builtin_descriptors_are_valid_extension_descriptors)
{
    const amark_extension_t *core = amark_tree_core_extension();
    amark_extension_t copy;
    amark_syntax_set_t *set = NULL;

    NAOS_REQUIRE_NOT_NULL(core);
    copy = *core;
    copy.name = "test.copy";
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_create(&set));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &copy));
    /* The names now appear twice. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_freeze(set));
    amark_syntax_set_destroy(set);
}

/* ---- Containment (3.8) ----------------------------------------------------- */

/* A block that can be a child of a block container (an item is only in a list). */
static bool is_block(amark_node_type_t type)
{
    return type >= AMARK_NODE_DOCUMENT && type <= AMARK_NODE_LINK_DEFINITION &&
           type != AMARK_NODE_ITEM;
}

static bool is_inline(amark_node_type_t type)
{
    return type >= AMARK_NODE_TEXT && type <= AMARK_NODE_IMAGE;
}

/* The CommonMark structure, written out independently of the descriptors. */
static bool expected_can_contain(amark_node_type_t parent, amark_node_type_t child)
{
    if (child == AMARK_NODE_DOCUMENT)
    {
        return false;
    }
    switch (parent)
    {
    case AMARK_NODE_DOCUMENT:
    case AMARK_NODE_BLOCK_QUOTE:
    case AMARK_NODE_ITEM:
        return is_block(child);
    case AMARK_NODE_LIST:
        return child == AMARK_NODE_ITEM;
    case AMARK_NODE_PARAGRAPH:
    case AMARK_NODE_HEADING:
    case AMARK_NODE_EMPH:
    case AMARK_NODE_STRONG:
    case AMARK_NODE_IMAGE:
        return is_inline(child);
    case AMARK_NODE_LINK:
        return is_inline(child) && child != AMARK_NODE_LINK;
    default:
        return false;
    }
}

NAOS_TEST(containment_follows_commonmark)
{
    const amark_syntax_set_t *set = core_set();
    amark_node_type_t parent;
    amark_node_type_t child;

    for (parent = AMARK_NODE_DOCUMENT; parent <= AMARK_NODE_IMAGE; parent++)
    {
        for (child = AMARK_NODE_DOCUMENT; child <= AMARK_NODE_IMAGE; child++)
        {
            bool expected = expected_can_contain(parent, child);
            bool actual = amark_registry_can_contain(set, NULL, parent, child);

            /* On a mismatch, the message shows 10000 * result + 100 * parent + child. */
            if (expected != actual)
            {
                NAOS_CHECK_UINT_EQ((expected ? 10000u : 0u) + parent * 100u + child,
                                   (actual ? 10000u : 0u) + parent * 100u + child);
            }
        }
    }
    /* Unknown types are never contained and contain nothing. */
    NAOS_CHECK_FALSE(amark_registry_can_contain(set, NULL, AMARK_NODE_NONE, AMARK_NODE_TEXT));
    NAOS_CHECK_FALSE(amark_registry_can_contain(set, NULL, AMARK_NODE_PARAGRAPH, AMARK_NODE_NONE));
    NAOS_CHECK_FALSE(amark_registry_can_contain(set, NULL, AMARK_NODE_PARAGRAPH, 300));
    NAOS_CHECK_FALSE(amark_registry_can_contain(NULL, NULL, AMARK_NODE_PARAGRAPH, AMARK_NODE_TEXT));
}

/* The link rule is the can_contain function of link: only link has one. */
NAOS_TEST(only_link_refines_containment)
{
    const amark_syntax_set_t *set = core_set();
    amark_node_type_t type;

    for (type = AMARK_NODE_DOCUMENT; type <= AMARK_NODE_IMAGE; type++)
    {
        const amark_node_type_desc_t *desc = amark_syntax_set_node_type_desc(set, type);

        NAOS_REQUIRE_NOT_NULL(desc);
        if (type == AMARK_NODE_LINK)
        {
            NAOS_REQUIRE_TRUE(desc->can_contain != NULL);
            NAOS_CHECK_FALSE(desc->can_contain(NULL, AMARK_NODE_LINK));
            NAOS_CHECK_TRUE(desc->can_contain(NULL, AMARK_NODE_IMAGE));
            NAOS_CHECK_TRUE(desc->can_contain(NULL, AMARK_NODE_TEXT));
        }
        else
        {
            NAOS_CHECK_TRUE(desc->can_contain == NULL);
        }
        NAOS_CHECK_TRUE(desc->init == NULL);
        NAOS_CHECK_TRUE(desc->deinit == NULL);
        NAOS_CHECK_TRUE(desc->copy == NULL);
        NAOS_CHECK_TRUE(desc->equal == NULL);
        NAOS_CHECK_TRUE(desc->dump == NULL);
    }
}

/* ---- Facets (3.5, 6.6) ----------------------------------------------------- */

typedef struct expected_facet
{
    amark_node_type_t type;
    amark_facet_t number;
    const char *name;
    amark_facet_role_t role;
    amark_facet_kind_t kind;
    uint32_t flags;
    int64_t int_min, int_max;
    /* The enumeration names joined by '|', or NULL. */
    const char *enum_names;
    int64_t default_number;
    bool has_validate;
} expected_facet_t;

#define V AMARK_FACET_VALUE
#define S AMARK_FACET_STYLE
#define STR AMARK_FACET_STRING
#define INT AMARK_FACET_INT
#define ENUM AMARK_FACET_ENUM
#define BOOL AMARK_FACET_BOOL
#define NLB AMARK_FACET_NO_LINE_BREAK
#define NBL AMARK_FACET_NO_BLANK_LINE

static const expected_facet_t expected_facets[] = {
    {AMARK_NODE_LIST, AMARK_LIST_TYPE, "type", V, ENUM, 0, 0, 0, "bullet|ordered", 1, false},
    {AMARK_NODE_LIST, AMARK_LIST_START, "start", V, INT, 0, 0, 999999999, NULL, 1, false},
    {AMARK_NODE_LIST, AMARK_LIST_TIGHT, "tight", V, BOOL, AMARK_FACET_OVERRIDABLE, 0, 0, NULL, 1,
     false},
    {AMARK_NODE_LIST, AMARK_LIST_BULLET, "bullet", S, ENUM, 0, 0, 0, "-|*|+", 0, false},
    {AMARK_NODE_LIST, AMARK_LIST_DELIMITER, "delimiter", S, ENUM, 0, 0, 0, ".|)", 0, false},

    {AMARK_NODE_CODE_BLOCK, AMARK_CODE_BLOCK_INFO, "info", V, STR, NLB, 0, 0, NULL, 0, false},
    {AMARK_NODE_CODE_BLOCK, AMARK_CODE_BLOCK_LITERAL, "literal", V, STR,
     AMARK_FACET_LINES | AMARK_FACET_CONTENT, 0, 0, NULL, 0, false},
    {AMARK_NODE_CODE_BLOCK, AMARK_CODE_BLOCK_FORM, "form", S, ENUM, 0, 0, 0, "indented|fenced", 0,
     false},
    {AMARK_NODE_CODE_BLOCK, AMARK_CODE_BLOCK_FENCE_CHAR, "fence_char", S, ENUM, 0, 0, 0, "`|~", 0,
     false},
    {AMARK_NODE_CODE_BLOCK, AMARK_CODE_BLOCK_FENCE_LENGTH, "fence_length", S, INT, 0, 3,
     INT64_MAX, NULL, 0, false},

    {AMARK_NODE_HTML_BLOCK, AMARK_HTML_BLOCK_LITERAL, "literal", V, STR,
     AMARK_FACET_LINES | AMARK_FACET_RAW | AMARK_FACET_CONTENT, 0, 0, NULL, 0, false},

    {AMARK_NODE_HEADING, AMARK_HEADING_LEVEL, "level", V, INT, 0, 1, 6, NULL, 1, false},
    {AMARK_NODE_HEADING, AMARK_HEADING_FORM, "form", S, ENUM, 0, 0, 0, "atx|setext", 0, false},
    {AMARK_NODE_HEADING, AMARK_HEADING_CLOSING, "closing", S, BOOL, 0, 0, 0, NULL, 0, false},

    {AMARK_NODE_THEMATIC_BREAK, AMARK_THEMATIC_BREAK_CHAR, "char", S, ENUM, 0, 0, 0, "*|-|_", 0,
     false},
    {AMARK_NODE_THEMATIC_BREAK, AMARK_THEMATIC_BREAK_COUNT, "count", S, INT, 0, 3, INT64_MAX, NULL,
     0, false},
    {AMARK_NODE_THEMATIC_BREAK, AMARK_THEMATIC_BREAK_SPACED, "spaced", S, BOOL, 0, 0, 0, NULL, 0,
     false},

    {AMARK_NODE_LINK_DEFINITION, AMARK_LINK_DEFINITION_LABEL, "label", V, STR,
     AMARK_FACET_RAW | AMARK_FACET_NONEMPTY, 0, 0, NULL, 0, true},
    {AMARK_NODE_LINK_DEFINITION, AMARK_LINK_DEFINITION_DESTINATION, "destination", V, STR, NLB, 0,
     0, NULL, 0, false},
    {AMARK_NODE_LINK_DEFINITION, AMARK_LINK_DEFINITION_TITLE, "title", V, STR, NBL, 0, 0, NULL, 0,
     false},
    {AMARK_NODE_LINK_DEFINITION, AMARK_LINK_DEFINITION_ANGLE, "angle", S, BOOL, 0, 0, 0, NULL, 0,
     false},
    {AMARK_NODE_LINK_DEFINITION, AMARK_LINK_DEFINITION_TITLE_QUOTE, "title_quote", S, ENUM, 0, 0,
     0, "\"|'|(", 0, false},

    {AMARK_NODE_TEXT, AMARK_TEXT_LITERAL, "literal", V, STR, NLB, 0, 0, NULL, 0, false},

    {AMARK_NODE_CODE, AMARK_CODE_LITERAL, "literal", V, STR,
     NLB | AMARK_FACET_NONEMPTY | AMARK_FACET_FOLD_LINE_BREAKS, 0, 0, NULL, 0, false},
    {AMARK_NODE_CODE, AMARK_CODE_BACKTICKS, "backticks", S, INT, 0, 1, INT64_MAX, NULL, 0, false},
    {AMARK_NODE_CODE, AMARK_CODE_PADDED, "padded", S, BOOL, 0, 0, 0, NULL, 0, false},

    {AMARK_NODE_HTML_INLINE, AMARK_HTML_INLINE_LITERAL, "literal", V, STR,
     NBL | AMARK_FACET_RAW, 0, 0, NULL, 0, false},

    {AMARK_NODE_LINEBREAK, AMARK_LINEBREAK_FORM, "form", S, ENUM, 0, 0, 0, "spaces|backslash", 0,
     false},

    {AMARK_NODE_EMPH, AMARK_EMPH_DELIMITER, "delimiter", S, ENUM, 0, 0, 0, "*|_", 0, false},
    {AMARK_NODE_STRONG, AMARK_STRONG_DELIMITER, "delimiter", S, ENUM, 0, 0, 0, "*|_", 0, false},

    {AMARK_NODE_LINK, AMARK_LINK_DESTINATION, "destination", V, STR, NLB, 0, 0, NULL, 0, false},
    {AMARK_NODE_LINK, AMARK_LINK_TITLE, "title", V, STR, NBL, 0, 0, NULL, 0, false},
    {AMARK_NODE_LINK, AMARK_LINK_LABEL, "label", V, STR, AMARK_FACET_RAW, 0, 0, NULL, 0, false},
    {AMARK_NODE_LINK, AMARK_LINK_FORM, "form", S, ENUM, 0, 0, 0,
     "inline|full|collapsed|shortcut|autolink", 0, false},
    {AMARK_NODE_LINK, AMARK_LINK_ANGLE, "angle", S, BOOL, 0, 0, 0, NULL, 0, false},
    {AMARK_NODE_LINK, AMARK_LINK_TITLE_QUOTE, "title_quote", S, ENUM, 0, 0, 0, "\"|'|(", 0, false},

    {AMARK_NODE_IMAGE, AMARK_IMAGE_DESTINATION, "destination", V, STR, NLB, 0, 0, NULL, 0, false},
    {AMARK_NODE_IMAGE, AMARK_IMAGE_TITLE, "title", V, STR, NBL, 0, 0, NULL, 0, false},
    {AMARK_NODE_IMAGE, AMARK_IMAGE_LABEL, "label", V, STR, AMARK_FACET_RAW, 0, 0, NULL, 0, false},
    {AMARK_NODE_IMAGE, AMARK_IMAGE_FORM, "form", S, ENUM, 0, 0, 0,
     "inline|full|collapsed|shortcut|autolink", 0, false},
    {AMARK_NODE_IMAGE, AMARK_IMAGE_ANGLE, "angle", S, BOOL, 0, 0, 0, NULL, 0, false},
    {AMARK_NODE_IMAGE, AMARK_IMAGE_TITLE_QUOTE, "title_quote", S, ENUM, 0, 0, 0, "\"|'|(", 0,
     false},
};

#undef V
#undef S
#undef STR
#undef INT
#undef ENUM
#undef BOOL
#undef NLB
#undef NBL

#define FACET_COUNT (sizeof(expected_facets) / sizeof(expected_facets[0]))

/* Checks the enumeration names of facet against names joined by '|'. */
static void check_enum_names(const amark_facet_desc_t *facet, const char *names)
{
    size_t count = 0;
    const char *p = names;

    NAOS_REQUIRE_NOT_NULL(facet->enum_names);
    for (;;)
    {
        const char *end = strchr(p, '|');
        size_t length = end == NULL ? strlen(p) : (size_t)(end - p);

        NAOS_REQUIRE_TRUE(count < facet->enum_count);
        NAOS_REQUIRE_NOT_NULL(facet->enum_names[count]);
        NAOS_CHECK_UINT_EQ(length, strlen(facet->enum_names[count]));
        NAOS_CHECK_INT_EQ(0, strncmp(p, facet->enum_names[count], length));
        count++;
        if (end == NULL)
        {
            break;
        }
        p = end + 1;
    }
    NAOS_CHECK_UINT_EQ(count, facet->enum_count);
}

NAOS_TEST(facets_follow_the_design_table)
{
    const amark_syntax_set_t *set = core_set();
    size_t declared = 0;
    size_t i;

    for (i = 0; i < FACET_COUNT; i++)
    {
        const expected_facet_t *expected = &expected_facets[i];
        const amark_node_type_desc_t *desc = amark_syntax_set_node_type_desc(set, expected->type);
        const amark_facet_desc_t *facet;

        NAOS_REQUIRE_NOT_NULL(desc);
        facet = amark_node_type_facet(desc, expected->number);
        NAOS_REQUIRE_NOT_NULL(facet);
        NAOS_CHECK_UINT_EQ(sizeof(amark_facet_desc_t), facet->struct_size);
        NAOS_CHECK_STR_EQ(expected->name, facet->name);
        NAOS_CHECK_UINT_EQ(expected->number, amark_node_type_find_facet(desc, expected->name));
        NAOS_CHECK_INT_EQ(expected->role, facet->role);
        NAOS_CHECK_INT_EQ(expected->kind, facet->kind);
        NAOS_CHECK_UINT_EQ(expected->flags, facet->flags);
        if (expected->kind == AMARK_FACET_INT)
        {
            NAOS_CHECK_TRUE(expected->int_min == facet->int_min);
            NAOS_CHECK_TRUE(expected->int_max == facet->int_max);
        }
        if (expected->enum_names != NULL)
        {
            check_enum_names(facet, expected->enum_names);
        }
        else
        {
            NAOS_CHECK_NULL(facet->enum_names);
            NAOS_CHECK_UINT_EQ(0, facet->enum_count);
        }
        if (expected->role == AMARK_FACET_VALUE)
        {
            NAOS_CHECK_TRUE(expected->default_number == facet->default_number);
        }
        NAOS_CHECK_NULL(facet->default_text);
        NAOS_CHECK_TRUE(expected->has_validate == (facet->validate != NULL));
        /* The on_change hooks come with the facet editing. */
        NAOS_CHECK_TRUE(facet->on_change == NULL);
        declared++;
    }

    /* Every facet of every type is in the table. */
    {
        size_t total = 0;
        amark_node_type_t type;

        for (type = AMARK_NODE_DOCUMENT; type <= AMARK_NODE_IMAGE; type++)
        {
            total += amark_syntax_set_node_type_desc(set, type)->facet_count;
        }
        NAOS_CHECK_UINT_EQ(declared, total);
    }
}

NAOS_TEST(enumeration_values_match_the_names)
{
    const amark_syntax_set_t *set = core_set();
    const amark_node_type_desc_t *list = amark_syntax_set_node_type_desc(set, AMARK_NODE_LIST);
    const amark_node_type_desc_t *link = amark_syntax_set_node_type_desc(set, AMARK_NODE_LINK);
    const amark_node_type_desc_t *code_block =
        amark_syntax_set_node_type_desc(set, AMARK_NODE_CODE_BLOCK);
    const amark_node_type_desc_t *heading =
        amark_syntax_set_node_type_desc(set, AMARK_NODE_HEADING);
    const amark_node_type_desc_t *thematic =
        amark_syntax_set_node_type_desc(set, AMARK_NODE_THEMATIC_BREAK);
    const amark_node_type_desc_t *definition =
        amark_syntax_set_node_type_desc(set, AMARK_NODE_LINK_DEFINITION);
    const amark_node_type_desc_t *linebreak =
        amark_syntax_set_node_type_desc(set, AMARK_NODE_LINEBREAK);
    const amark_node_type_desc_t *emph = amark_syntax_set_node_type_desc(set, AMARK_NODE_EMPH);
    const amark_node_type_desc_t *strong = amark_syntax_set_node_type_desc(set, AMARK_NODE_STRONG);

#define NAME_OF(desc, facet, value) (amark_node_type_facet(desc, facet)->enum_names[(value) - 1])

    NAOS_CHECK_STR_EQ("bullet", NAME_OF(list, AMARK_LIST_TYPE, AMARK_LIST_TYPE_BULLET));
    NAOS_CHECK_STR_EQ("ordered", NAME_OF(list, AMARK_LIST_TYPE, AMARK_LIST_TYPE_ORDERED));
    NAOS_CHECK_STR_EQ("-", NAME_OF(list, AMARK_LIST_BULLET, AMARK_LIST_BULLET_DASH));
    NAOS_CHECK_STR_EQ("*", NAME_OF(list, AMARK_LIST_BULLET, AMARK_LIST_BULLET_ASTERISK));
    NAOS_CHECK_STR_EQ("+", NAME_OF(list, AMARK_LIST_BULLET, AMARK_LIST_BULLET_PLUS));
    NAOS_CHECK_STR_EQ(".", NAME_OF(list, AMARK_LIST_DELIMITER, AMARK_LIST_DELIMITER_PERIOD));
    NAOS_CHECK_STR_EQ(")", NAME_OF(list, AMARK_LIST_DELIMITER, AMARK_LIST_DELIMITER_PAREN));

    NAOS_CHECK_STR_EQ("indented",
                      NAME_OF(code_block, AMARK_CODE_BLOCK_FORM, AMARK_CODE_BLOCK_FORM_INDENTED));
    NAOS_CHECK_STR_EQ("fenced",
                      NAME_OF(code_block, AMARK_CODE_BLOCK_FORM, AMARK_CODE_BLOCK_FORM_FENCED));
    NAOS_CHECK_STR_EQ("`", NAME_OF(code_block, AMARK_CODE_BLOCK_FENCE_CHAR,
                                   AMARK_CODE_BLOCK_FENCE_CHAR_BACKTICK));
    NAOS_CHECK_STR_EQ("~", NAME_OF(code_block, AMARK_CODE_BLOCK_FENCE_CHAR,
                                   AMARK_CODE_BLOCK_FENCE_CHAR_TILDE));

    NAOS_CHECK_STR_EQ("atx", NAME_OF(heading, AMARK_HEADING_FORM, AMARK_HEADING_FORM_ATX));
    NAOS_CHECK_STR_EQ("setext", NAME_OF(heading, AMARK_HEADING_FORM, AMARK_HEADING_FORM_SETEXT));

    NAOS_CHECK_STR_EQ("*", NAME_OF(thematic, AMARK_THEMATIC_BREAK_CHAR,
                                   AMARK_THEMATIC_BREAK_CHAR_ASTERISK));
    NAOS_CHECK_STR_EQ("-", NAME_OF(thematic, AMARK_THEMATIC_BREAK_CHAR,
                                   AMARK_THEMATIC_BREAK_CHAR_DASH));
    NAOS_CHECK_STR_EQ("_", NAME_OF(thematic, AMARK_THEMATIC_BREAK_CHAR,
                                   AMARK_THEMATIC_BREAK_CHAR_UNDERSCORE));

    NAOS_CHECK_STR_EQ("\"", NAME_OF(definition, AMARK_LINK_DEFINITION_TITLE_QUOTE,
                                    AMARK_LINK_DEFINITION_TITLE_QUOTE_DOUBLE));
    NAOS_CHECK_STR_EQ("'", NAME_OF(definition, AMARK_LINK_DEFINITION_TITLE_QUOTE,
                                   AMARK_LINK_DEFINITION_TITLE_QUOTE_SINGLE));
    NAOS_CHECK_STR_EQ("(", NAME_OF(definition, AMARK_LINK_DEFINITION_TITLE_QUOTE,
                                   AMARK_LINK_DEFINITION_TITLE_QUOTE_PAREN));

    NAOS_CHECK_STR_EQ("spaces",
                      NAME_OF(linebreak, AMARK_LINEBREAK_FORM, AMARK_LINEBREAK_FORM_SPACES));
    NAOS_CHECK_STR_EQ("backslash",
                      NAME_OF(linebreak, AMARK_LINEBREAK_FORM, AMARK_LINEBREAK_FORM_BACKSLASH));

    NAOS_CHECK_STR_EQ("*", NAME_OF(emph, AMARK_EMPH_DELIMITER, AMARK_EMPH_DELIMITER_ASTERISK));
    NAOS_CHECK_STR_EQ("_", NAME_OF(emph, AMARK_EMPH_DELIMITER, AMARK_EMPH_DELIMITER_UNDERSCORE));
    NAOS_CHECK_STR_EQ("*",
                      NAME_OF(strong, AMARK_STRONG_DELIMITER, AMARK_STRONG_DELIMITER_ASTERISK));
    NAOS_CHECK_STR_EQ("_",
                      NAME_OF(strong, AMARK_STRONG_DELIMITER, AMARK_STRONG_DELIMITER_UNDERSCORE));

    NAOS_CHECK_STR_EQ("inline", NAME_OF(link, AMARK_LINK_FORM, AMARK_LINK_FORM_INLINE));
    NAOS_CHECK_STR_EQ("full", NAME_OF(link, AMARK_LINK_FORM, AMARK_LINK_FORM_FULL));
    NAOS_CHECK_STR_EQ("collapsed", NAME_OF(link, AMARK_LINK_FORM, AMARK_LINK_FORM_COLLAPSED));
    NAOS_CHECK_STR_EQ("shortcut", NAME_OF(link, AMARK_LINK_FORM, AMARK_LINK_FORM_SHORTCUT));
    NAOS_CHECK_STR_EQ("autolink", NAME_OF(link, AMARK_LINK_FORM, AMARK_LINK_FORM_AUTOLINK));
    NAOS_CHECK_STR_EQ("\"", NAME_OF(link, AMARK_LINK_TITLE_QUOTE, AMARK_LINK_TITLE_QUOTE_DOUBLE));
    NAOS_CHECK_STR_EQ("'", NAME_OF(link, AMARK_LINK_TITLE_QUOTE, AMARK_LINK_TITLE_QUOTE_SINGLE));
    NAOS_CHECK_STR_EQ("(", NAME_OF(link, AMARK_LINK_TITLE_QUOTE, AMARK_LINK_TITLE_QUOTE_PAREN));

#undef NAME_OF

    /* image uses the facet numbers of link. */
    NAOS_CHECK_UINT_EQ(AMARK_LINK_DESTINATION, AMARK_IMAGE_DESTINATION);
    NAOS_CHECK_UINT_EQ(AMARK_LINK_TITLE, AMARK_IMAGE_TITLE);
    NAOS_CHECK_UINT_EQ(AMARK_LINK_LABEL, AMARK_IMAGE_LABEL);
    NAOS_CHECK_UINT_EQ(AMARK_LINK_FORM, AMARK_IMAGE_FORM);
    NAOS_CHECK_UINT_EQ(AMARK_LINK_ANGLE, AMARK_IMAGE_ANGLE);
    NAOS_CHECK_UINT_EQ(AMARK_LINK_TITLE_QUOTE, AMARK_IMAGE_TITLE_QUOTE);
}

/* ---- The label check (6.6) ------------------------------------------------- */

static amark_error_t check_label(const char *text, size_t length)
{
    const amark_node_type_desc_t *desc =
        amark_syntax_set_node_type_desc(core_set(), AMARK_NODE_LINK_DEFINITION);
    const amark_facet_desc_t *facet = amark_node_type_facet(desc, AMARK_LINK_DEFINITION_LABEL);
    amark_value_t value;

    NAOS_REQUIRE_NOT_NULL(facet);
    NAOS_REQUIRE_TRUE(facet->validate != NULL);
    memset(&value, 0, sizeof(value));
    value.struct_size = sizeof(value);
    value.state = AMARK_VALUE_PRESENT;
    value.text = text;
    value.length = length;
    return facet->validate(NULL, AMARK_LINK_DEFINITION_LABEL, &value, facet->user_data);
}

#define LABEL_OK(s) NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, check_label(s, sizeof(s) - 1))
#define LABEL_BAD(s) NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, check_label(s, sizeof(s) - 1))

NAOS_TEST(label_check_follows_commonmark)
{
    static char long_label[1000 * 3 + 1];
    size_t i;

    LABEL_OK("foo");
    LABEL_OK(" foo ");
    LABEL_OK("Foo *bar*");
    LABEL_OK("a\\[b\\]c");
    LABEL_OK("a\\\\");
    LABEL_OK("foo\nbar");
    LABEL_OK("foo\r\nbar");
    LABEL_OK("\nfoo\n");
    LABEL_OK("\xCE\xB1");

    /* At least one character that is not a space, a tab or a line ending. */
    LABEL_BAD("");
    LABEL_BAD(" ");
    LABEL_BAD(" \t\n ");
    /* No unescaped bracket, and no escape of the closing bracket. */
    LABEL_BAD("a[b");
    LABEL_BAD("a]b");
    LABEL_BAD("[");
    LABEL_BAD("a\\");
    LABEL_BAD("a\\\\\\");
    /* No blank line. */
    LABEL_BAD("a\n\nb");
    LABEL_BAD("a\n \t\nb");
    LABEL_BAD("a\r\n\r\nb");
    LABEL_BAD("a\r\rb");

    /* At most 999 characters (code points, not bytes). */
    for (i = 0; i < 999; i++)
    {
        memcpy(long_label + i * 2, "\xCE\xB1", 2);
    }
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, check_label(long_label, 999 * 2));
    memcpy(long_label + 999 * 2, "\xCE\xB1", 2);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, check_label(long_label, 1000 * 2));
    memset(long_label, 'a', 999);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, check_label(long_label, 999));
    long_label[999] = 'a';
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, check_label(long_label, 1000));

    /* No text. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, check_label(NULL, 3));
}

#undef LABEL_OK
#undef LABEL_BAD

NAOS_SUITE(builtin_types,
    NAOS_ADD_TEST(identifiers_are_fixed),
    NAOS_ADD_TEST(core_set_has_the_builtin_types),
    NAOS_ADD_TEST(created_set_starts_with_the_builtin_extension),
    NAOS_ADD_TEST(builtin_extension_cannot_be_removed),
    NAOS_ADD_TEST(core_set_is_frozen_and_static),
    NAOS_ADD_TEST(builtin_descriptors_are_valid_extension_descriptors),
    NAOS_ADD_TEST(containment_follows_commonmark),
    NAOS_ADD_TEST(only_link_refines_containment),
    NAOS_ADD_TEST(facets_follow_the_design_table),
    NAOS_ADD_TEST(enumeration_values_match_the_names),
    NAOS_ADD_TEST(label_check_follows_commonmark)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(builtin_types)
)
