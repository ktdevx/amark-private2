/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The tests of Facets (syntax-tree): the storage of the facets, the order of
 * the getter (a set or recorded value, then the default value of a value facet,
 * then UNSET for a style), the setters (the checks of the kind, the range, the
 * enumeration, the facet flags and the validate function; the same value that
 * changes nothing; the normalization of multi-line values to LF; the on_change
 * hook called with the prepared value, and the prepared value discarded when it
 * fails), the unsetting of styles and overrides, the recording without the hook,
 * the change notification, the internal check of the facet storage and the
 * allocation failures (3.2, 3.4, 3.6, 3.10, 5.1, 6.1-6.6, 6.9, 6.14).
 *
 * The hooks are tested with a test-local extension type; the change rules of
 * the built-in types are tested in builtin_types_test.c.
 */
#include "tree/tree.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <naos/naos.h>

#include "test_allocator.h"
#include "test_check.h"

/* ---- The test extension type ------------------------------------------------- */

/* The facets of the test type "test.box". */
#define BOX_TITLE 1u /* V STRING, no blank line, validate + on_change */
#define BOX_BODY 2u  /* V STRING, LINES */
#define BOX_COUNT 3u /* V INT -5..5 (default 2), on_change */
#define BOX_MODE 4u  /* S ENUM a b c, on_change */
#define BOX_FLAG 5u  /* V BOOL OVERRIDABLE (default 1) */
#define BOX_NAME 6u  /* V STRING, no line break, not empty (default "anon") */
#define BOX_WIDE 7u  /* S BOOL */
#define BOX_FACETS 7u

#define BOX_MODE_A 1
#define BOX_MODE_C 3

/* The calls of the hooks, what they saw, and how they answer. */
static size_t hook_calls;
static size_t validate_calls;
static amark_facet_t hook_facet;
static char hook_text[64];
static size_t hook_length;
static int64_t hook_number;
static void *hook_user_data;
/* The value the node held for the facet while the hook ran. */
static amark_value_state_t hook_old_state;
static int64_t hook_old_number;
static char hook_old_text[64];
/* When not AMARK_ERROR_NONE, the hook fails with this error. */
static amark_error_t hook_failure;
/* When set, the hook of BOX_COUNT also sets BOX_WIDE to 1 on the node. */
static int hook_sets_wide;

static int user_data_marker;

static void reset_hooks(void)
{
    hook_calls = 0;
    validate_calls = 0;
    hook_facet = AMARK_FACET_NONE;
    hook_text[0] = '\0';
    hook_length = 0;
    hook_number = 0;
    hook_user_data = NULL;
    hook_old_state = (amark_value_state_t)0;
    hook_old_number = 0;
    hook_old_text[0] = '\0';
    hook_failure = AMARK_ERROR_NONE;
    hook_sets_wide = 0;
}

static amark_error_t box_on_change(amark_node_t *node, amark_facet_t facet,
                                   const amark_value_t *new_value, void *user_data)
{
    amark_value_t old;

    hook_calls++;
    hook_facet = facet;
    hook_user_data = user_data;
    hook_number = new_value->number;
    hook_length = new_value->length;
    hook_text[0] = '\0';
    if (new_value->text != NULL && new_value->length < sizeof(hook_text))
    {
        memcpy(hook_text, new_value->text, new_value->length + 1);
    }
    old.struct_size = sizeof(old);
    if (amark_node_get(node, facet, &old) == AMARK_ERROR_NONE)
    {
        hook_old_state = old.state;
        hook_old_number = old.number;
        hook_old_text[0] = '\0';
        if (old.text != NULL && old.length < sizeof(hook_old_text))
        {
            memcpy(hook_old_text, old.text, old.length + 1);
        }
    }
    if (hook_failure != AMARK_ERROR_NONE)
    {
        return hook_failure;
    }
    if (hook_sets_wide && facet == BOX_COUNT)
    {
        return amark_node_set_int(node, BOX_WIDE, 1);
    }
    return AMARK_ERROR_NONE;
}

/* Refuses the title "bad". */
static amark_error_t box_validate(const amark_node_t *node, amark_facet_t facet,
                                  const amark_value_t *value, void *user_data)
{
    (void)node;
    (void)facet;
    (void)user_data;
    validate_calls++;
    if (value->length == 3 && memcmp(value->text, "bad", 3) == 0)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    return AMARK_ERROR_NONE;
}

static const char *const box_mode_names[] = {"a", "b", "c"};

static const amark_facet_desc_t box_facets[BOX_FACETS] = {
    {sizeof(amark_facet_desc_t), "title", AMARK_FACET_VALUE, AMARK_FACET_STRING,
     AMARK_FACET_NO_BLANK_LINE, 0, 0, NULL, 0, 0, NULL, box_validate, box_on_change,
     &user_data_marker},
    {sizeof(amark_facet_desc_t), "body", AMARK_FACET_VALUE, AMARK_FACET_STRING,
     AMARK_FACET_LINES, 0, 0, NULL, 0, 0, NULL, NULL, NULL, NULL},
    {sizeof(amark_facet_desc_t), "count", AMARK_FACET_VALUE, AMARK_FACET_INT, 0, -5, 5, NULL, 0,
     2, NULL, NULL, box_on_change, NULL},
    {sizeof(amark_facet_desc_t), "mode", AMARK_FACET_STYLE, AMARK_FACET_ENUM, 0, 0, 0,
     box_mode_names, 3, 0, NULL, NULL, box_on_change, NULL},
    {sizeof(amark_facet_desc_t), "flag", AMARK_FACET_VALUE, AMARK_FACET_BOOL,
     AMARK_FACET_OVERRIDABLE, 0, 0, NULL, 0, 1, NULL, NULL, NULL, NULL},
    {sizeof(amark_facet_desc_t), "name", AMARK_FACET_VALUE, AMARK_FACET_STRING,
     AMARK_FACET_NO_LINE_BREAK | AMARK_FACET_NONEMPTY, 0, 0, NULL, 0, 0, "anon", NULL, NULL,
     NULL},
    {sizeof(amark_facet_desc_t), "wide", AMARK_FACET_STYLE, AMARK_FACET_BOOL, 0, 0, 0, NULL, 0, 0,
     NULL, NULL, NULL, NULL},
};

static const amark_node_type_desc_t box_type = {
    sizeof(amark_node_type_desc_t), "test.box", AMARK_NODE_LEVEL_BLOCK, AMARK_CONTENT_BLOCK,
    0, 0, box_facets, BOX_FACETS, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
static const amark_node_type_desc_t *const test_types[] = {&box_type};
static const amark_extension_t test_extension = {
    sizeof(amark_extension_t), "test.facets", test_types, 1, NULL, 0, NULL};

#define BOX_TYPE AMARK_NODE_DYNAMIC_FIRST

/* ---- Helpers ------------------------------------------------------------------- */

/* A document with test_extension; the document owns its set. */
static amark_document_t *new_box_document(void)
{
    amark_syntax_set_t *set = NULL;
    amark_document_t *document = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_create(&set));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &test_extension));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_freeze(set));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(set, &document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_take_syntax_set(document, set));
    return document;
}

static amark_document_t *new_document(void)
{
    amark_document_t *document = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    return document;
}

static amark_node_t *new_node(amark_document_t *document, amark_node_type_t type)
{
    amark_node_t *node = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, type, &node));
    NAOS_REQUIRE_NOT_NULL(node);
    return node;
}

static amark_value_t get(const amark_node_t *node, amark_facet_t facet)
{
    amark_value_t value;

    memset(&value, 0xA5, sizeof(value));
    value.struct_size = sizeof(value);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_get(node, facet, &value));
    return value;
}

/* Checks a PRESENT number. */
#define CHECK_NUMBER(node, facet, expected) \
    do { \
        amark_value_t v_ = get((node), (facet)); \
        NAOS_CHECK_INT_EQ(AMARK_VALUE_PRESENT, v_.state); \
        NAOS_CHECK_INT_EQ((expected), v_.number); \
        NAOS_CHECK_NULL(v_.text); \
        NAOS_CHECK_UINT_EQ(0, v_.length); \
    } while (0)

/* Checks a PRESENT string (a string literal, which may hold any byte). */
#define CHECK_TEXT(node, facet, expected_literal) \
    do { \
        amark_value_t v_ = get((node), (facet)); \
        NAOS_CHECK_INT_EQ(AMARK_VALUE_PRESENT, v_.state); \
        NAOS_REQUIRE_NOT_NULL(v_.text); \
        AMARK_CHECK_BYTES_EQ(v_.text, v_.length, expected_literal); \
        NAOS_CHECK_INT_EQ(0, v_.text[v_.length]); \
        NAOS_CHECK_INT_EQ(0, v_.number); \
    } while (0)

#define CHECK_UNSET(node, facet) \
    do { \
        amark_value_t v_ = get((node), (facet)); \
        NAOS_CHECK_INT_EQ(AMARK_VALUE_UNSET, v_.state); \
        NAOS_CHECK_INT_EQ(0, v_.number); \
        NAOS_CHECK_NULL(v_.text); \
        NAOS_CHECK_UINT_EQ(0, v_.length); \
    } while (0)

/* Checks that document keeps the internal invariants (amark_tree_check). */
static void check_tree_ok(amark_document_t *document)
{
    amark_tree_check_report_t report;

    NAOS_CHECK_STR_EQ("none", amark_tree_violation_name(amark_tree_check(document, &report)));
    NAOS_CHECK_NULL(report.node);
}

/* A snapshot of the storage of one node: the raw facet slots and the cache
 * flags. An operation that leaves the node unchanged keeps it. */
typedef struct snapshot
{
    amark_tree_facet_t facets[AMARK_FACET_MAX];
    size_t count;
    uint32_t cache_flags;
    size_t invalidation_steps;
} snapshot_t;

static snapshot_t take_snapshot(const amark_node_t *node)
{
    snapshot_t s;

    memset(&s, 0, sizeof(s));
    s.count = node->desc->facet_count;
    if (s.count > 0)
    {
        memcpy(s.facets, node->facets, s.count * sizeof(amark_tree_facet_t));
    }
    s.cache_flags = node->cache_flags;
    s.invalidation_steps = node->document->stats.invalidation_steps;
    return s;
}

static int same_snapshot(const amark_node_t *node, const snapshot_t *s)
{
    size_t i;

    if (s->count != node->desc->facet_count || s->cache_flags != node->cache_flags ||
        s->invalidation_steps != node->document->stats.invalidation_steps)
    {
        return 0;
    }
    for (i = 0; i < s->count; i++)
    {
        const amark_tree_facet_t *a = &s->facets[i];
        const amark_tree_facet_t *b = &node->facets[i];

        if (a->state != b->state || a->number != b->number || a->text != b->text ||
            a->length != b->length || a->cache != b->cache || a->cache_length != b->cache_length)
        {
            return 0;
        }
    }
    return 1;
}

/* Marks node valid, so that a change notification is seen in its flags. */
static void validate_node(amark_node_t *node)
{
    node->cache_flags |= AMARK_TREE_CACHE_VALID;
}

/* Checks that a setter call fails with expected and leaves node unchanged. */
#define CHECK_REFUSED(node, expected, call) \
    do { \
        snapshot_t s_; \
        validate_node(node); \
        s_ = take_snapshot(node); \
        NAOS_CHECK_INT_EQ((expected), (call)); \
        NAOS_CHECK_TRUE(same_snapshot((node), &s_)); \
    } while (0)

#define SET_STRING(node, facet, literal) \
    amark_node_set_string((node), (facet), ("" literal), sizeof("" literal) - 1)
#define RECORD_STRING(node, facet, literal) \
    amark_node_record_string((node), (facet), ("" literal), sizeof("" literal) - 1)

/* ---- New nodes: defaults and unset styles (3.4, 3.6, 5.1) -------------------- */

NAOS_TEST(new_builtin_nodes_return_the_defaults_and_unset_styles)
{
    amark_document_t *document = new_document();
    amark_node_t *node;

    node = new_node(document, AMARK_NODE_LIST);
    CHECK_NUMBER(node, AMARK_LIST_TYPE, AMARK_LIST_TYPE_BULLET);
    CHECK_NUMBER(node, AMARK_LIST_START, 1);
    CHECK_NUMBER(node, AMARK_LIST_TIGHT, 1);
    CHECK_UNSET(node, AMARK_LIST_BULLET);
    CHECK_UNSET(node, AMARK_LIST_DELIMITER);

    node = new_node(document, AMARK_NODE_CODE_BLOCK);
    CHECK_TEXT(node, AMARK_CODE_BLOCK_INFO, "");
    CHECK_TEXT(node, AMARK_CODE_BLOCK_LITERAL, "");
    CHECK_UNSET(node, AMARK_CODE_BLOCK_FORM);
    CHECK_UNSET(node, AMARK_CODE_BLOCK_FENCE_CHAR);
    CHECK_UNSET(node, AMARK_CODE_BLOCK_FENCE_LENGTH);

    node = new_node(document, AMARK_NODE_HTML_BLOCK);
    CHECK_TEXT(node, AMARK_HTML_BLOCK_LITERAL, "");

    node = new_node(document, AMARK_NODE_HEADING);
    CHECK_NUMBER(node, AMARK_HEADING_LEVEL, 1);
    CHECK_UNSET(node, AMARK_HEADING_FORM);
    CHECK_UNSET(node, AMARK_HEADING_CLOSING);

    node = new_node(document, AMARK_NODE_THEMATIC_BREAK);
    CHECK_UNSET(node, AMARK_THEMATIC_BREAK_CHAR);
    CHECK_UNSET(node, AMARK_THEMATIC_BREAK_COUNT);
    CHECK_UNSET(node, AMARK_THEMATIC_BREAK_SPACED);

    node = new_node(document, AMARK_NODE_LINK_DEFINITION);
    CHECK_TEXT(node, AMARK_LINK_DEFINITION_LABEL, "");
    CHECK_TEXT(node, AMARK_LINK_DEFINITION_DESTINATION, "");
    CHECK_TEXT(node, AMARK_LINK_DEFINITION_TITLE, "");
    CHECK_UNSET(node, AMARK_LINK_DEFINITION_ANGLE);
    CHECK_UNSET(node, AMARK_LINK_DEFINITION_TITLE_QUOTE);

    node = new_node(document, AMARK_NODE_TEXT);
    CHECK_TEXT(node, AMARK_TEXT_LITERAL, "");

    node = new_node(document, AMARK_NODE_CODE);
    CHECK_TEXT(node, AMARK_CODE_LITERAL, "");
    CHECK_UNSET(node, AMARK_CODE_BACKTICKS);
    CHECK_UNSET(node, AMARK_CODE_PADDED);

    node = new_node(document, AMARK_NODE_HTML_INLINE);
    CHECK_TEXT(node, AMARK_HTML_INLINE_LITERAL, "");

    node = new_node(document, AMARK_NODE_LINEBREAK);
    CHECK_UNSET(node, AMARK_LINEBREAK_FORM);

    node = new_node(document, AMARK_NODE_EMPH);
    CHECK_UNSET(node, AMARK_EMPH_DELIMITER);
    node = new_node(document, AMARK_NODE_STRONG);
    CHECK_UNSET(node, AMARK_STRONG_DELIMITER);

    node = new_node(document, AMARK_NODE_LINK);
    CHECK_TEXT(node, AMARK_LINK_DESTINATION, "");
    CHECK_TEXT(node, AMARK_LINK_TITLE, "");
    CHECK_TEXT(node, AMARK_LINK_LABEL, "");
    CHECK_UNSET(node, AMARK_LINK_FORM);
    CHECK_UNSET(node, AMARK_LINK_ANGLE);
    CHECK_UNSET(node, AMARK_LINK_TITLE_QUOTE);

    node = new_node(document, AMARK_NODE_IMAGE);
    CHECK_TEXT(node, AMARK_IMAGE_DESTINATION, "");
    CHECK_UNSET(node, AMARK_IMAGE_FORM);

    check_tree_ok(document);
    amark_document_destroy(document);
}

NAOS_TEST(new_extension_nodes_return_the_declared_defaults)
{
    amark_document_t *document = new_box_document();
    amark_node_t *box = new_node(document, BOX_TYPE);

    CHECK_TEXT(box, BOX_TITLE, "");
    CHECK_TEXT(box, BOX_BODY, "");
    CHECK_NUMBER(box, BOX_COUNT, 2);
    CHECK_UNSET(box, BOX_MODE);
    CHECK_NUMBER(box, BOX_FLAG, 1);
    CHECK_TEXT(box, BOX_NAME, "anon");
    CHECK_UNSET(box, BOX_WIDE);
    check_tree_ok(document);
    amark_document_destroy(document);
}

/* ---- Facets the type does not have and invalid arguments (3.10) -------------- */

NAOS_TEST(facets_the_type_does_not_have_are_invalid_arguments)
{
    amark_document_t *document = new_document();
    amark_node_t *heading = new_node(document, AMARK_NODE_HEADING);
    amark_node_t *paragraph = new_node(document, AMARK_NODE_PARAGRAPH);
    amark_value_t value;

    memset(&value, 0, sizeof(value));
    value.struct_size = sizeof(value);
    value.number = 77;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_get(heading, 0, &value));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_get(heading, 4, &value));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_get(heading, 32, &value));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_get(paragraph, 1, &value));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_get(document->root, 1, &value));
    /* The output is not written on failure. */
    NAOS_CHECK_INT_EQ(77, value.number);

    CHECK_REFUSED(heading, AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_int(heading, 0, 1));
    CHECK_REFUSED(heading, AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_int(heading, 4, 1));
    CHECK_REFUSED(paragraph, AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_int(paragraph, 1, 1));
    CHECK_REFUSED(paragraph, AMARK_ERROR_INVALID_ARGUMENT, SET_STRING(paragraph, 1, "x"));
    CHECK_REFUSED(heading, AMARK_ERROR_INVALID_ARGUMENT, amark_node_unset(heading, 4));
    CHECK_REFUSED(heading, AMARK_ERROR_INVALID_ARGUMENT, amark_node_record_int(heading, 4, 1));
    CHECK_REFUSED(paragraph, AMARK_ERROR_INVALID_ARGUMENT, RECORD_STRING(paragraph, 1, "x"));

    /* NULL arguments and a short struct_size. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_get(NULL, 1, &value));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_get(heading, AMARK_HEADING_LEVEL, NULL));
    value.struct_size = sizeof(value) - 1;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_get(heading, AMARK_HEADING_LEVEL, &value));
    NAOS_CHECK_INT_EQ(77, value.number);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_int(NULL, 1, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_string(NULL, 1, "x", 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_unset(NULL, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_record_int(NULL, 1, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_record_string(NULL, 1, "x", 1));

    check_tree_ok(document);
    amark_document_destroy(document);
}

/* ---- Invalid values leave the node unchanged (6.2-6.6) ------------------------ */

NAOS_TEST(values_of_the_wrong_kind_are_refused)
{
    amark_document_t *document = new_document();
    amark_node_t *heading = new_node(document, AMARK_NODE_HEADING);
    amark_node_t *text = new_node(document, AMARK_NODE_TEXT);

    CHECK_REFUSED(heading, AMARK_ERROR_INVALID_ARGUMENT,
                  SET_STRING(heading, AMARK_HEADING_LEVEL, "2"));
    CHECK_REFUSED(heading, AMARK_ERROR_INVALID_ARGUMENT,
                  RECORD_STRING(heading, AMARK_HEADING_FORM, "atx"));
    CHECK_REFUSED(text, AMARK_ERROR_INVALID_ARGUMENT,
                  amark_node_set_int(text, AMARK_TEXT_LITERAL, 0));
    CHECK_REFUSED(text, AMARK_ERROR_INVALID_ARGUMENT,
                  amark_node_record_int(text, AMARK_TEXT_LITERAL, 0));
    /* NULL text with a length. */
    CHECK_REFUSED(text, AMARK_ERROR_INVALID_ARGUMENT,
                  amark_node_set_string(text, AMARK_TEXT_LITERAL, NULL, 1));
    CHECK_REFUSED(text, AMARK_ERROR_INVALID_ARGUMENT,
                  amark_node_record_string(text, AMARK_TEXT_LITERAL, NULL, 1));
    check_tree_ok(document);
    amark_document_destroy(document);
}

NAOS_TEST(nul_and_invalid_utf8_are_refused)
{
    amark_document_t *document = new_document();
    amark_node_t *text = new_node(document, AMARK_NODE_TEXT);
    amark_node_t *block = new_node(document, AMARK_NODE_CODE_BLOCK);

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, SET_STRING(text, AMARK_TEXT_LITERAL, "old"));
    CHECK_REFUSED(text, AMARK_ERROR_INVALID_ARGUMENT,
                  SET_STRING(text, AMARK_TEXT_LITERAL, "a\0b"));
    CHECK_REFUSED(text, AMARK_ERROR_INVALID_ARGUMENT, SET_STRING(text, AMARK_TEXT_LITERAL, "\0"));
    CHECK_REFUSED(text, AMARK_ERROR_INVALID_ARGUMENT,
                  SET_STRING(text, AMARK_TEXT_LITERAL, "\xC0\x80"));
    CHECK_REFUSED(text, AMARK_ERROR_INVALID_ARGUMENT,
                  SET_STRING(text, AMARK_TEXT_LITERAL, "a\xFF"));
    CHECK_REFUSED(text, AMARK_ERROR_INVALID_ARGUMENT,
                  SET_STRING(text, AMARK_TEXT_LITERAL, "\xED\xA0\x80"));
    CHECK_REFUSED(text, AMARK_ERROR_INVALID_ARGUMENT,
                  SET_STRING(text, AMARK_TEXT_LITERAL, "\xE3\x81"));
    CHECK_REFUSED(text, AMARK_ERROR_INVALID_ARGUMENT,
                  RECORD_STRING(text, AMARK_TEXT_LITERAL, "a\0b"));
    CHECK_REFUSED(text, AMARK_ERROR_INVALID_ARGUMENT,
                  RECORD_STRING(text, AMARK_TEXT_LITERAL, "\x80"));
    CHECK_TEXT(text, AMARK_TEXT_LITERAL, "old");
    /* Multi-line values are checked before they are normalized. */
    CHECK_REFUSED(block, AMARK_ERROR_INVALID_ARGUMENT,
                  SET_STRING(block, AMARK_CODE_BLOCK_LITERAL, "a\r\n\0"));
    CHECK_REFUSED(block, AMARK_ERROR_INVALID_ARGUMENT,
                  SET_STRING(block, AMARK_CODE_BLOCK_LITERAL, "a\n\xF5\x80\x80\x80\n"));
    /* Valid UTF-8 is accepted. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      SET_STRING(text, AMARK_TEXT_LITERAL, "\xE3\x81\x82\xF0\x9F\x98\x80"));
    CHECK_TEXT(text, AMARK_TEXT_LITERAL, "\xE3\x81\x82\xF0\x9F\x98\x80");
    check_tree_ok(document);
    amark_document_destroy(document);
}

NAOS_TEST(line_breaks_are_refused_where_the_value_cannot_hold_them)
{
    amark_document_t *document = new_document();
    amark_node_t *text = new_node(document, AMARK_NODE_TEXT);
    amark_node_t *code = new_node(document, AMARK_NODE_CODE);
    amark_node_t *link = new_node(document, AMARK_NODE_LINK);
    amark_node_t *block = new_node(document, AMARK_NODE_CODE_BLOCK);
    amark_node_t *definition = new_node(document, AMARK_NODE_LINK_DEFINITION);

    CHECK_REFUSED(text, AMARK_ERROR_INVALID_ARGUMENT, SET_STRING(text, AMARK_TEXT_LITERAL, "a\nb"));
    CHECK_REFUSED(text, AMARK_ERROR_INVALID_ARGUMENT, SET_STRING(text, AMARK_TEXT_LITERAL, "a\r"));
    CHECK_REFUSED(text, AMARK_ERROR_INVALID_ARGUMENT,
                  SET_STRING(text, AMARK_TEXT_LITERAL, "\r\nb"));
    CHECK_REFUSED(code, AMARK_ERROR_INVALID_ARGUMENT, SET_STRING(code, AMARK_CODE_LITERAL, "a\nb"));
    CHECK_REFUSED(link, AMARK_ERROR_INVALID_ARGUMENT,
                  SET_STRING(link, AMARK_LINK_DESTINATION, "/a\n"));
    CHECK_REFUSED(link, AMARK_ERROR_INVALID_ARGUMENT,
                  RECORD_STRING(link, AMARK_LINK_DESTINATION, "/a\r/b"));
    CHECK_REFUSED(block, AMARK_ERROR_INVALID_ARGUMENT,
                  SET_STRING(block, AMARK_CODE_BLOCK_INFO, "rust\n"));
    CHECK_REFUSED(definition, AMARK_ERROR_INVALID_ARGUMENT,
                  SET_STRING(definition, AMARK_LINK_DEFINITION_DESTINATION, "a\nb"));

    /* A value that may hold line breaks accepts one. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET_STRING(link, AMARK_LINK_TITLE, "a\nb"));
    CHECK_TEXT(link, AMARK_LINK_TITLE, "a\nb");
    check_tree_ok(document);
    amark_document_destroy(document);
}

NAOS_TEST(blank_lines_are_refused_where_the_value_cannot_hold_them)
{
    amark_document_t *document = new_document();
    amark_node_t *link = new_node(document, AMARK_NODE_LINK);
    amark_node_t *image = new_node(document, AMARK_NODE_IMAGE);
    amark_node_t *html = new_node(document, AMARK_NODE_HTML_INLINE);
    amark_node_t *definition = new_node(document, AMARK_NODE_LINK_DEFINITION);

    CHECK_REFUSED(link, AMARK_ERROR_INVALID_ARGUMENT,
                  SET_STRING(link, AMARK_LINK_TITLE, "a\n\nb"));
    CHECK_REFUSED(link, AMARK_ERROR_INVALID_ARGUMENT,
                  SET_STRING(link, AMARK_LINK_TITLE, "a\n \t\nb"));
    CHECK_REFUSED(link, AMARK_ERROR_INVALID_ARGUMENT,
                  SET_STRING(link, AMARK_LINK_TITLE, "a\r\n\r\nb"));
    CHECK_REFUSED(link, AMARK_ERROR_INVALID_ARGUMENT,
                  SET_STRING(link, AMARK_LINK_TITLE, "a\r\rb"));
    CHECK_REFUSED(image, AMARK_ERROR_INVALID_ARGUMENT,
                  RECORD_STRING(image, AMARK_IMAGE_TITLE, "a\n\n"));
    CHECK_REFUSED(html, AMARK_ERROR_INVALID_ARGUMENT,
                  SET_STRING(html, AMARK_HTML_INLINE_LITERAL, "<a\n\nb>"));
    CHECK_REFUSED(definition, AMARK_ERROR_INVALID_ARGUMENT,
                  SET_STRING(definition, AMARK_LINK_DEFINITION_TITLE, "t\n  \nu"));
    CHECK_REFUSED(definition, AMARK_ERROR_INVALID_ARGUMENT,
                  SET_STRING(definition, AMARK_LINK_DEFINITION_LABEL, "a\n\nb"));

    /* Line breaks that do not make a blank line are accepted, also at the ends. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET_STRING(link, AMARK_LINK_TITLE, "\na\r\nb\r"));
    CHECK_TEXT(link, AMARK_LINK_TITLE, "\na\r\nb\r");
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET_STRING(html, AMARK_HTML_INLINE_LITERAL, "<a\nb>"));
    CHECK_TEXT(html, AMARK_HTML_INLINE_LITERAL, "<a\nb>");
    check_tree_ok(document);
    amark_document_destroy(document);
}

NAOS_TEST(values_out_of_range_are_refused)
{
    amark_document_t *document = new_document();
    amark_node_t *heading = new_node(document, AMARK_NODE_HEADING);
    amark_node_t *list = new_node(document, AMARK_NODE_LIST);
    amark_node_t *code = new_node(document, AMARK_NODE_CODE);
    amark_node_t *block = new_node(document, AMARK_NODE_CODE_BLOCK);
    amark_node_t *rule = new_node(document, AMARK_NODE_THEMATIC_BREAK);
    amark_node_t *definition = new_node(document, AMARK_NODE_LINK_DEFINITION);

    CHECK_REFUSED(heading, AMARK_ERROR_INVALID_ARGUMENT,
                  amark_node_set_int(heading, AMARK_HEADING_LEVEL, 0));
    CHECK_REFUSED(heading, AMARK_ERROR_INVALID_ARGUMENT,
                  amark_node_set_int(heading, AMARK_HEADING_LEVEL, 7));
    CHECK_REFUSED(heading, AMARK_ERROR_INVALID_ARGUMENT,
                  amark_node_record_int(heading, AMARK_HEADING_LEVEL, -1));
    CHECK_REFUSED(list, AMARK_ERROR_INVALID_ARGUMENT,
                  amark_node_set_int(list, AMARK_LIST_START, -1));
    CHECK_REFUSED(list, AMARK_ERROR_INVALID_ARGUMENT,
                  amark_node_set_int(list, AMARK_LIST_START, 1000000000));
    CHECK_REFUSED(list, AMARK_ERROR_INVALID_ARGUMENT,
                  amark_node_set_int(list, AMARK_LIST_TYPE, 0));
    CHECK_REFUSED(list, AMARK_ERROR_INVALID_ARGUMENT,
                  amark_node_set_int(list, AMARK_LIST_TYPE, 3));
    CHECK_REFUSED(list, AMARK_ERROR_INVALID_ARGUMENT,
                  amark_node_set_int(list, AMARK_LIST_BULLET, 4));
    CHECK_REFUSED(list, AMARK_ERROR_INVALID_ARGUMENT,
                  amark_node_set_int(list, AMARK_LIST_TIGHT, 2));
    CHECK_REFUSED(list, AMARK_ERROR_INVALID_ARGUMENT,
                  amark_node_set_int(list, AMARK_LIST_TIGHT, -1));
    CHECK_REFUSED(block, AMARK_ERROR_INVALID_ARGUMENT,
                  amark_node_set_int(block, AMARK_CODE_BLOCK_FENCE_LENGTH, 2));
    CHECK_REFUSED(rule, AMARK_ERROR_INVALID_ARGUMENT,
                  amark_node_record_int(rule, AMARK_THEMATIC_BREAK_COUNT, 2));
    CHECK_REFUSED(code, AMARK_ERROR_INVALID_ARGUMENT,
                  amark_node_set_int(code, AMARK_CODE_BACKTICKS, 0));
    /* The empty code literal. */
    CHECK_REFUSED(code, AMARK_ERROR_INVALID_ARGUMENT, SET_STRING(code, AMARK_CODE_LITERAL, ""));
    CHECK_REFUSED(code, AMARK_ERROR_INVALID_ARGUMENT,
                  amark_node_set_string(code, AMARK_CODE_LITERAL, NULL, 0));
    CHECK_REFUSED(code, AMARK_ERROR_INVALID_ARGUMENT, RECORD_STRING(code, AMARK_CODE_LITERAL, ""));
    /* Labels CommonMark cannot write (the validate function of the type). */
    CHECK_REFUSED(definition, AMARK_ERROR_INVALID_ARGUMENT,
                  SET_STRING(definition, AMARK_LINK_DEFINITION_LABEL, ""));
    CHECK_REFUSED(definition, AMARK_ERROR_INVALID_ARGUMENT,
                  SET_STRING(definition, AMARK_LINK_DEFINITION_LABEL, " \t"));
    CHECK_REFUSED(definition, AMARK_ERROR_INVALID_ARGUMENT,
                  SET_STRING(definition, AMARK_LINK_DEFINITION_LABEL, "a]b"));
    CHECK_REFUSED(definition, AMARK_ERROR_INVALID_ARGUMENT,
                  RECORD_STRING(definition, AMARK_LINK_DEFINITION_LABEL, "a\\"));

    /* The bounds are accepted. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_int(heading, AMARK_HEADING_LEVEL, 6));
    CHECK_NUMBER(heading, AMARK_HEADING_LEVEL, 6);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_int(list, AMARK_LIST_START, 999999999));
    CHECK_NUMBER(list, AMARK_LIST_START, 999999999);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_int(list, AMARK_LIST_START, 0));
    CHECK_NUMBER(list, AMARK_LIST_START, 0);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_set_int(list, AMARK_LIST_TYPE, AMARK_LIST_TYPE_ORDERED));
    CHECK_NUMBER(list, AMARK_LIST_TYPE, AMARK_LIST_TYPE_ORDERED);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_set_int(block, AMARK_CODE_BLOCK_FENCE_LENGTH, INT64_MAX));
    CHECK_NUMBER(block, AMARK_CODE_BLOCK_FENCE_LENGTH, INT64_MAX);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET_STRING(code, AMARK_CODE_LITERAL, " "));
    CHECK_TEXT(code, AMARK_CODE_LITERAL, " ");
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      SET_STRING(definition, AMARK_LINK_DEFINITION_LABEL, "Foo \\] bar"));
    CHECK_TEXT(definition, AMARK_LINK_DEFINITION_LABEL, "Foo \\] bar");
    check_tree_ok(document);
    amark_document_destroy(document);
}

NAOS_TEST(the_validate_function_refuses_values_without_calling_the_hook)
{
    amark_document_t *document = new_box_document();
    amark_node_t *box = new_node(document, BOX_TYPE);

    reset_hooks();
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, SET_STRING(box, BOX_TITLE, "good"));
    NAOS_CHECK_UINT_EQ(1, validate_calls);
    NAOS_CHECK_UINT_EQ(1, hook_calls);
    CHECK_REFUSED(box, AMARK_ERROR_INVALID_ARGUMENT, SET_STRING(box, BOX_TITLE, "bad"));
    CHECK_REFUSED(box, AMARK_ERROR_INVALID_ARGUMENT, RECORD_STRING(box, BOX_TITLE, "bad"));
    NAOS_CHECK_UINT_EQ(3, validate_calls);
    NAOS_CHECK_UINT_EQ(1, hook_calls);
    /* The flags are checked before the validate function. */
    CHECK_REFUSED(box, AMARK_ERROR_INVALID_ARGUMENT, SET_STRING(box, BOX_TITLE, "a\n\nb"));
    NAOS_CHECK_UINT_EQ(3, validate_calls);
    CHECK_TEXT(box, BOX_TITLE, "good");
    /* The flags of the test type: no line break, not empty. */
    CHECK_REFUSED(box, AMARK_ERROR_INVALID_ARGUMENT, SET_STRING(box, BOX_NAME, ""));
    CHECK_REFUSED(box, AMARK_ERROR_INVALID_ARGUMENT, SET_STRING(box, BOX_NAME, "a\nb"));
    CHECK_REFUSED(box, AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_int(box, BOX_COUNT, 6));
    CHECK_REFUSED(box, AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_int(box, BOX_COUNT, -6));
    CHECK_REFUSED(box, AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_int(box, BOX_MODE, 4));
    CHECK_REFUSED(box, AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_int(box, BOX_WIDE, 2));
    NAOS_CHECK_UINT_EQ(1, hook_calls);
    check_tree_ok(document);
    amark_document_destroy(document);
}

/* ---- Setting values (6.1, 6.5, 6.9) ---------------------------------------- */

NAOS_TEST(set_copies_the_value_and_notifies_the_change)
{
    amark_document_t *document = new_document();
    amark_node_t *text = new_node(document, AMARK_NODE_TEXT);
    amark_node_t *paragraph = new_node(document, AMARK_NODE_PARAGRAPH);
    char buffer[8] = "hello";
    amark_value_t value;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(document->root, paragraph));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(paragraph, text));
    validate_node(document->root);
    validate_node(paragraph);
    validate_node(text);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_set_string(text, AMARK_TEXT_LITERAL, buffer, 3));
    /* The value is a copy (6.1). */
    buffer[0] = 'J';
    value = get(text, AMARK_TEXT_LITERAL);
    NAOS_CHECK_TRUE(value.text != buffer);
    AMARK_CHECK_BYTES_EQ(value.text, value.length, "hel");
    /* The change invalidated the node and its ancestors. */
    NAOS_CHECK_UINT_EQ(0, text->cache_flags & AMARK_TREE_CACHE_VALID);
    NAOS_CHECK_UINT_EQ(0, paragraph->cache_flags & AMARK_TREE_CACHE_VALID);
    NAOS_CHECK_UINT_EQ(0, document->root->cache_flags & AMARK_TREE_CACHE_VALID);

    /* Setting again replaces the value; the empty string is a value. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET_STRING(text, AMARK_TEXT_LITERAL, "x"));
    CHECK_TEXT(text, AMARK_TEXT_LITERAL, "x");
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_string(text, AMARK_TEXT_LITERAL, NULL, 0));
    CHECK_TEXT(text, AMARK_TEXT_LITERAL, "");
    check_tree_ok(document);
    amark_document_destroy(document);
}

NAOS_TEST(setting_the_same_value_changes_nothing)
{
    amark_document_t *document = new_box_document();
    amark_node_t *box = new_node(document, BOX_TYPE);
    amark_node_t *heading;
    amark_document_t *core = new_document();

    reset_hooks();
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, SET_STRING(box, BOX_TITLE, "same"));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_int(box, BOX_MODE, BOX_MODE_C));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, SET_STRING(box, BOX_BODY, "a\nb\n"));
    NAOS_CHECK_UINT_EQ(2, hook_calls);
    /* The same value: no hook, no change, no notification (6.9). */
    CHECK_REFUSED(box, AMARK_ERROR_NONE, SET_STRING(box, BOX_TITLE, "same"));
    CHECK_REFUSED(box, AMARK_ERROR_NONE, amark_node_set_int(box, BOX_MODE, BOX_MODE_C));
    /* The same value after normalization. */
    CHECK_REFUSED(box, AMARK_ERROR_NONE, SET_STRING(box, BOX_BODY, "a\r\nb"));
    /* The default value of a value facet is its current value. */
    CHECK_REFUSED(box, AMARK_ERROR_NONE, amark_node_set_int(box, BOX_COUNT, 2));
    CHECK_REFUSED(box, AMARK_ERROR_NONE, SET_STRING(box, BOX_NAME, "anon"));
    NAOS_CHECK_UINT_EQ(2, hook_calls);
    /* An unset style differs from every value. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_int(box, BOX_WIDE, 0));
    CHECK_NUMBER(box, BOX_WIDE, 0);
    /* A prefix is not the same value. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET_STRING(box, BOX_TITLE, "sam"));
    CHECK_TEXT(box, BOX_TITLE, "sam");

    heading = new_node(core, AMARK_NODE_HEADING);
    CHECK_REFUSED(heading, AMARK_ERROR_NONE, amark_node_set_int(heading, AMARK_HEADING_LEVEL, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_int(heading, AMARK_HEADING_LEVEL, 2));
    CHECK_REFUSED(heading, AMARK_ERROR_NONE, amark_node_set_int(heading, AMARK_HEADING_LEVEL, 2));
    check_tree_ok(document);
    check_tree_ok(core);
    amark_document_destroy(core);
    amark_document_destroy(document);
}

NAOS_TEST(multi_line_values_are_normalized_to_lf)
{
    amark_document_t *document = new_box_document();
    amark_node_t *box = new_node(document, BOX_TYPE);
    amark_document_t *core = new_document();
    amark_node_t *block = new_node(core, AMARK_NODE_CODE_BLOCK);
    amark_node_t *html = new_node(core, AMARK_NODE_HTML_BLOCK);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET_STRING(box, BOX_BODY, "a\r\nb\rc"));
    CHECK_TEXT(box, BOX_BODY, "a\nb\nc\n");
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET_STRING(box, BOX_BODY, "\r\r\n\n"));
    CHECK_TEXT(box, BOX_BODY, "\n\n\n");
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET_STRING(box, BOX_BODY, "x\n"));
    CHECK_TEXT(box, BOX_BODY, "x\n");
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET_STRING(box, BOX_BODY, "\r"));
    CHECK_TEXT(box, BOX_BODY, "\n");
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET_STRING(box, BOX_BODY, ""));
    CHECK_TEXT(box, BOX_BODY, "");
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, RECORD_STRING(box, BOX_BODY, "p\rq"));
    CHECK_TEXT(box, BOX_BODY, "p\nq\n");

    /* The literals of code_block and html_block (6.5). */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET_STRING(block, AMARK_CODE_BLOCK_LITERAL, "a"));
    CHECK_TEXT(block, AMARK_CODE_BLOCK_LITERAL, "a\n");
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET_STRING(block, AMARK_CODE_BLOCK_LITERAL, "a\r\n\r\n"));
    CHECK_TEXT(block, AMARK_CODE_BLOCK_LITERAL, "a\n\n");
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET_STRING(block, AMARK_CODE_BLOCK_LITERAL, "\n"));
    CHECK_TEXT(block, AMARK_CODE_BLOCK_LITERAL, "\n");
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET_STRING(html, AMARK_HTML_BLOCK_LITERAL, "<div>\r</div>"));
    CHECK_TEXT(html, AMARK_HTML_BLOCK_LITERAL, "<div>\n</div>\n");
    check_tree_ok(document);
    check_tree_ok(core);
    amark_document_destroy(core);
    amark_document_destroy(document);
}

/* ---- The on_change hook ------------------------------------------------------ */

NAOS_TEST(the_hook_sees_the_prepared_value_before_the_commit)
{
    amark_document_t *document = new_box_document();
    amark_node_t *box = new_node(document, BOX_TYPE);

    reset_hooks();
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, SET_STRING(box, BOX_TITLE, "first"));
    NAOS_CHECK_UINT_EQ(1, hook_calls);
    NAOS_CHECK_UINT_EQ(BOX_TITLE, hook_facet);
    NAOS_CHECK_PTR_EQ(&user_data_marker, hook_user_data);
    NAOS_CHECK_STR_EQ("first", hook_text);
    NAOS_CHECK_UINT_EQ(5, hook_length);
    /* During the hook the node still held the old value. */
    NAOS_CHECK_INT_EQ(AMARK_VALUE_PRESENT, hook_old_state);
    NAOS_CHECK_STR_EQ("", hook_old_text);
    CHECK_TEXT(box, BOX_TITLE, "first");

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_int(box, BOX_MODE, BOX_MODE_A));
    NAOS_CHECK_UINT_EQ(2, hook_calls);
    NAOS_CHECK_UINT_EQ(BOX_MODE, hook_facet);
    NAOS_CHECK_INT_EQ(BOX_MODE_A, hook_number);
    NAOS_CHECK_INT_EQ(AMARK_VALUE_UNSET, hook_old_state);
    CHECK_NUMBER(box, BOX_MODE, BOX_MODE_A);

    /* The hook may set another facet of the node. */
    hook_sets_wide = 1;
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_int(box, BOX_COUNT, -5));
    NAOS_CHECK_INT_EQ(2, hook_old_number);
    CHECK_NUMBER(box, BOX_COUNT, -5);
    CHECK_NUMBER(box, BOX_WIDE, 1);
    check_tree_ok(document);
    amark_document_destroy(document);
}

NAOS_TEST(a_failing_hook_leaves_the_value_unchanged)
{
    amark_document_t *document = new_box_document();
    amark_node_t *box = new_node(document, BOX_TYPE);

    reset_hooks();
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, SET_STRING(box, BOX_TITLE, "kept"));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_int(box, BOX_COUNT, 4));
    hook_failure = AMARK_ERROR_UNREPRESENTABLE;
    CHECK_REFUSED(box, AMARK_ERROR_UNREPRESENTABLE, SET_STRING(box, BOX_TITLE, "lost"));
    CHECK_REFUSED(box, AMARK_ERROR_UNREPRESENTABLE, amark_node_set_int(box, BOX_COUNT, 1));
    CHECK_REFUSED(box, AMARK_ERROR_UNREPRESENTABLE, amark_node_set_int(box, BOX_MODE, 2));
    hook_failure = AMARK_ERROR_OUT_OF_MEMORY;
    CHECK_REFUSED(box, AMARK_ERROR_OUT_OF_MEMORY, SET_STRING(box, BOX_TITLE, "lost"));
    NAOS_CHECK_UINT_EQ(6, hook_calls);
    CHECK_TEXT(box, BOX_TITLE, "kept");
    CHECK_NUMBER(box, BOX_COUNT, 4);
    CHECK_UNSET(box, BOX_MODE);

    /* The next set succeeds again. */
    hook_failure = AMARK_ERROR_NONE;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET_STRING(box, BOX_TITLE, "next"));
    CHECK_TEXT(box, BOX_TITLE, "next");
    check_tree_ok(document);
    amark_document_destroy(document);
}

/* ---- Unset and record (6.11, 6.14) ------------------------------------------- */

NAOS_TEST(unset_returns_styles_and_overrides_to_their_unset_state)
{
    amark_document_t *document = new_box_document();
    amark_node_t *box = new_node(document, BOX_TYPE);
    amark_document_t *core = new_document();
    amark_node_t *list = new_node(core, AMARK_NODE_LIST);
    amark_node_t *link = new_node(core, AMARK_NODE_LINK);

    reset_hooks();
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_int(box, BOX_MODE, 2));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_int(box, BOX_FLAG, 0));
    CHECK_NUMBER(box, BOX_FLAG, 0);
    NAOS_CHECK_UINT_EQ(1, hook_calls);
    validate_node(box);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_unset(box, BOX_MODE));
    CHECK_UNSET(box, BOX_MODE);
    NAOS_CHECK_UINT_EQ(0, box->cache_flags & AMARK_TREE_CACHE_VALID);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_unset(box, BOX_FLAG));
    CHECK_NUMBER(box, BOX_FLAG, 1);
    /* Unsetting calls no hook. */
    NAOS_CHECK_UINT_EQ(1, hook_calls);
    /* Unsetting what is unset changes nothing. */
    CHECK_REFUSED(box, AMARK_ERROR_NONE, amark_node_unset(box, BOX_MODE));
    CHECK_REFUSED(box, AMARK_ERROR_NONE, amark_node_unset(box, BOX_FLAG));
    /* A value facet that is not overridable cannot be unset. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, SET_STRING(box, BOX_TITLE, "t"));
    CHECK_REFUSED(box, AMARK_ERROR_INVALID_ARGUMENT, amark_node_unset(box, BOX_TITLE));
    CHECK_REFUSED(box, AMARK_ERROR_INVALID_ARGUMENT, amark_node_unset(box, BOX_COUNT));
    CHECK_TEXT(box, BOX_TITLE, "t");

    /* A recorded style can be unset too. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, RECORD_STRING(link, AMARK_LINK_DESTINATION, "/u"));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_record_int(link, AMARK_LINK_FORM, AMARK_LINK_FORM_FULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_unset(link, AMARK_LINK_FORM));
    CHECK_UNSET(link, AMARK_LINK_FORM);
    CHECK_REFUSED(link, AMARK_ERROR_INVALID_ARGUMENT,
                  amark_node_unset(link, AMARK_LINK_DESTINATION));
    CHECK_TEXT(link, AMARK_LINK_DESTINATION, "/u");

    /* The tight override of a list (6.11). */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_int(list, AMARK_LIST_TIGHT, 0));
    CHECK_NUMBER(list, AMARK_LIST_TIGHT, 0);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_unset(list, AMARK_LIST_TIGHT));
    CHECK_NUMBER(list, AMARK_LIST_TIGHT, 1);
    CHECK_REFUSED(list, AMARK_ERROR_INVALID_ARGUMENT, amark_node_unset(list, AMARK_LIST_START));
    check_tree_ok(document);
    check_tree_ok(core);
    amark_document_destroy(core);
    amark_document_destroy(document);
}

NAOS_TEST(an_override_is_kept_even_when_it_equals_the_current_value)
{
    amark_document_t *document = new_box_document();
    amark_node_t *box = new_node(document, BOX_TYPE);

    /* Without an override the value is derived (here the default, as the facet
     * has no derivation); setting the same value still holds it as an override
     * (6.11). */
    validate_node(box);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_int(box, BOX_FLAG, 1));
    NAOS_CHECK_INT_EQ(AMARK_TREE_FACET_SET, box->facets[BOX_FLAG - 1].state);
    CHECK_NUMBER(box, BOX_FLAG, 1);
    /* With the same override, nothing changes. */
    CHECK_REFUSED(box, AMARK_ERROR_NONE, amark_node_set_int(box, BOX_FLAG, 1));
    check_tree_ok(document);
    amark_document_destroy(document);
}

NAOS_TEST(record_stores_the_value_without_the_hook)
{
    amark_document_t *document = new_box_document();
    amark_node_t *box = new_node(document, BOX_TYPE);

    reset_hooks();
    validate_node(box);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, RECORD_STRING(box, BOX_TITLE, "parsed"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_record_int(box, BOX_COUNT, -3));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_record_int(box, BOX_MODE, 2));
    NAOS_CHECK_UINT_EQ(0, hook_calls);
    /* The validate function still checks the value. */
    NAOS_CHECK_UINT_EQ(1, validate_calls);
    CHECK_TEXT(box, BOX_TITLE, "parsed");
    CHECK_NUMBER(box, BOX_COUNT, -3);
    CHECK_NUMBER(box, BOX_MODE, 2);
    NAOS_CHECK_INT_EQ(AMARK_TREE_FACET_RECORDED, box->facets[BOX_TITLE - 1].state);
    NAOS_CHECK_INT_EQ(AMARK_TREE_FACET_RECORDED, box->facets[BOX_COUNT - 1].state);
    /* Recording is a change of the records: it is notified. */
    NAOS_CHECK_UINT_EQ(0, box->cache_flags & AMARK_TREE_CACHE_VALID);

    /* A set after a record makes the value set. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, SET_STRING(box, BOX_TITLE, "edited"));
    NAOS_CHECK_INT_EQ(AMARK_TREE_FACET_SET, box->facets[BOX_TITLE - 1].state);
    NAOS_CHECK_UINT_EQ(1, hook_calls);
    CHECK_TEXT(box, BOX_TITLE, "edited");
    check_tree_ok(document);
    amark_document_destroy(document);
}

/* ---- The internal check of the facet storage -------------------------------- */

static void check_facets_violation(amark_document_t *document, const amark_node_t *node)
{
    amark_tree_check_report_t report;

    NAOS_CHECK_STR_EQ("facets", amark_tree_violation_name(amark_tree_check(document, &report)));
    NAOS_CHECK_PTR_EQ(node, report.node);
    NAOS_CHECK_NOT_NULL(report.message);
}

NAOS_TEST(the_check_detects_a_broken_facet_storage)
{
    amark_document_t *document = new_box_document();
    amark_node_t *box = new_node(document, BOX_TYPE);
    amark_tree_facet_t saved;
    char *text;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, SET_STRING(box, BOX_TITLE, "abc"));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, SET_STRING(box, BOX_BODY, "a\nb"));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_int(box, BOX_COUNT, 3));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_int(box, BOX_MODE, 1));
    check_tree_ok(document);

    /* An unknown state. */
    saved = box->facets[BOX_COUNT - 1];
    box->facets[BOX_COUNT - 1].state = 9;
    check_facets_violation(document, box);
    box->facets[BOX_COUNT - 1] = saved;
    /* A number out of the range of the facet. */
    box->facets[BOX_COUNT - 1].number = 6;
    check_facets_violation(document, box);
    box->facets[BOX_COUNT - 1] = saved;
    box->facets[BOX_MODE - 1].number = 0;
    check_facets_violation(document, box);
    box->facets[BOX_MODE - 1].number = 1;
    /* A facet without a value that holds a number. */
    box->facets[BOX_WIDE - 1].number = 1;
    check_facets_violation(document, box);
    box->facets[BOX_WIDE - 1].number = 0;
    /* A string facet with a value but no text, or a text that does not end at
     * its length. */
    saved = box->facets[BOX_TITLE - 1];
    box->facets[BOX_TITLE - 1].text = NULL;
    check_facets_violation(document, box);
    box->facets[BOX_TITLE - 1] = saved;
    box->facets[BOX_TITLE - 1].length = 2;
    check_facets_violation(document, box);
    box->facets[BOX_TITLE - 1] = saved;
    text = box->facets[BOX_TITLE - 1].text;
    text[1] = '\0';
    check_facets_violation(document, box);
    text[1] = '\xFF';
    check_facets_violation(document, box);
    text[1] = 'b';
    /* A multi-line value that is not normalized. */
    text = box->facets[BOX_BODY - 1].text;
    text[1] = '\r';
    check_facets_violation(document, box);
    text[1] = '\n';
    box->facets[BOX_BODY - 1].length--;
    text[box->facets[BOX_BODY - 1].length] = '\0';
    check_facets_violation(document, box);
    text[box->facets[BOX_BODY - 1].length] = '\n';
    box->facets[BOX_BODY - 1].length++;
    /* A cache length without a cache. */
    box->facets[BOX_NAME - 1].cache_length = 1;
    check_facets_violation(document, box);
    box->facets[BOX_NAME - 1].cache_length = 0;
    check_tree_ok(document);
    amark_document_destroy(document);
}

/* ---- Allocation failures (6.1) ------------------------------------------------- */

/* Sets a string and checks that a failure leaves the facet as it was. */
static amark_error_t set_checked(amark_node_t *node, amark_facet_t facet, const char *text,
                                 int record)
{
    amark_tree_facet_t before = node->facets[facet - 1];
    uint32_t flags = node->cache_flags;
    amark_error_t error;

    error = record ? amark_node_record_string(node, facet, text, strlen(text))
                   : amark_node_set_string(node, facet, text, strlen(text));
    if (error != AMARK_ERROR_NONE &&
        (node->facets[facet - 1].state != before.state ||
         node->facets[facet - 1].text != before.text ||
         node->facets[facet - 1].length != before.length || node->cache_flags != flags))
    {
        return AMARK_ERROR_INVALID_STATE;
    }
    return error;
}

/* Every allocation of the scenario fails in turn: each failure returns
 * AMARK_ERROR_OUT_OF_MEMORY, leaves the facet unchanged and leaks nothing. */
static amark_error_t oom_scenario(void *context)
{
    amark_document_t *document = NULL;
    amark_node_t *box = NULL;
    amark_syntax_set_t *set = NULL;
    amark_error_t error;

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
        error = amark_document_create(set, &document);
    }
    if (error != AMARK_ERROR_NONE)
    {
        amark_syntax_set_destroy(set);
        return error;
    }
    error = amark_document_take_syntax_set(document, set);
    if (error != AMARK_ERROR_NONE)
    {
        amark_document_destroy(document);
        amark_syntax_set_destroy(set);
        return error;
    }
    error = amark_node_create(document, BOX_TYPE, &box);
    if (error == AMARK_ERROR_NONE)
    {
        box->cache_flags |= AMARK_TREE_CACHE_VALID;
        error = set_checked(box, BOX_TITLE, "one", 0);
    }
    if (error == AMARK_ERROR_NONE)
    {
        box->cache_flags |= AMARK_TREE_CACHE_VALID;
        error = set_checked(box, BOX_TITLE, "two", 0);
    }
    if (error == AMARK_ERROR_NONE)
    {
        box->cache_flags |= AMARK_TREE_CACHE_VALID;
        error = set_checked(box, BOX_BODY, "a\r\nb", 0);
    }
    if (error == AMARK_ERROR_NONE)
    {
        box->cache_flags |= AMARK_TREE_CACHE_VALID;
        error = set_checked(box, BOX_NAME, "rec", 1);
    }
    if (error == AMARK_ERROR_NONE)
    {
        /* A failing hook: the prepared value is freed (the sweep sees a leak
         * otherwise) and the facet is unchanged. */
        hook_failure = AMARK_ERROR_UNREPRESENTABLE;
        error = set_checked(box, BOX_TITLE, "three", 0);
        hook_failure = AMARK_ERROR_NONE;
        if (error == AMARK_ERROR_UNREPRESENTABLE)
        {
            error = AMARK_ERROR_NONE;
        }
        else if (error == AMARK_ERROR_NONE)
        {
            error = AMARK_ERROR_INVALID_STATE;
        }
    }
    if (error == AMARK_ERROR_NONE)
    {
        /* Numbers allocate nothing. */
        error = amark_node_set_int(box, BOX_COUNT, 1);
    }
    if (error == AMARK_ERROR_NONE &&
        (amark_tree_check(document, NULL) != AMARK_TREE_VIOLATION_NONE ||
         box->facets[BOX_BODY - 1].length != 4))
    {
        error = AMARK_ERROR_INVALID_STATE;
    }
    amark_document_destroy(document);
    return error;
}

NAOS_TEST(out_of_memory_leaves_the_facets_unchanged)
{
    reset_hooks();
    AMARK_CHECK_OOM_SWEEP(oom_scenario, NULL);
    /* A failed allocation never reached the hook: the hooks ran only in the
     * runs where the value could be prepared. */
    NAOS_CHECK_TRUE(hook_calls > 0);
}

NAOS_SUITE(facet,
    NAOS_ADD_TEST(new_builtin_nodes_return_the_defaults_and_unset_styles),
    NAOS_ADD_TEST(new_extension_nodes_return_the_declared_defaults),
    NAOS_ADD_TEST(facets_the_type_does_not_have_are_invalid_arguments),
    NAOS_ADD_TEST(values_of_the_wrong_kind_are_refused),
    NAOS_ADD_TEST(nul_and_invalid_utf8_are_refused),
    NAOS_ADD_TEST(line_breaks_are_refused_where_the_value_cannot_hold_them),
    NAOS_ADD_TEST(blank_lines_are_refused_where_the_value_cannot_hold_them),
    NAOS_ADD_TEST(values_out_of_range_are_refused),
    NAOS_ADD_TEST(the_validate_function_refuses_values_without_calling_the_hook),
    NAOS_ADD_TEST(set_copies_the_value_and_notifies_the_change),
    NAOS_ADD_TEST(setting_the_same_value_changes_nothing),
    NAOS_ADD_TEST(multi_line_values_are_normalized_to_lf),
    NAOS_ADD_TEST(the_hook_sees_the_prepared_value_before_the_commit),
    NAOS_ADD_TEST(a_failing_hook_leaves_the_value_unchanged),
    NAOS_ADD_TEST(unset_returns_styles_and_overrides_to_their_unset_state),
    NAOS_ADD_TEST(an_override_is_kept_even_when_it_equals_the_current_value),
    NAOS_ADD_TEST(record_stores_the_value_without_the_hook),
    NAOS_ADD_TEST(the_check_detects_a_broken_facet_storage),
    NAOS_ADD_TEST(out_of_memory_leaves_the_facets_unchanged)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(facet)
)
