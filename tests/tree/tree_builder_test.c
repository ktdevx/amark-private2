/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The test support that builds trees from S-expressions (tests/support/tree_builder.c,
 * 9.7), through the public API.
 *
 * Suite tree_builder_round_trip: the dump of a tree built from a dump is the same
 * text, for the example of the design, every built-in attribute, the tokens with
 * every field (with :edits ignored and :span recomputed), the escapes of the strings,
 * the extension attributes and a detached node; the children of (document ...) go
 * after those the root already has; the dump of new nodes reads back.
 * Suite tree_builder_values: the extension attributes reach the callback with
 * their values (strings decoded, other values as their text) and its failure is
 * returned.
 * Suite tree_builder_errors: each kind of error is AMARK_ERROR_INVALID_ARGUMENT with
 * a reason that starts with its position; a failure leaves the document as it was
 * (no node left, the same dump); the edit tracking is off while building and is
 * restored; NULL arguments; the out-of-memory sweep.
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

NAOS_TEARDOWN(restore)
{
    amark_allocator_set(NULL);
}

/* --- An extension type ---------------------------------------------------------- */

/* x.box: a block container whose type data owns a name and holds a number. */
typedef struct box_data
{
    char *name;
    long long number;
    bool has_number;
} box_data_t;

static void box_deinit(amark_node_t *node, void *data)
{
    (void)node;
    amark_free(((box_data_t *)data)->name);
}

static amark_error_t box_dump(const amark_node_t *node, const void *data,
                              amark_dump_writer_t *writer)
{
    const box_data_t *box = data;
    amark_error_t err = AMARK_ERROR_NONE;

    (void)node;
    if (box->name != NULL)
    {
        err = amark_dump_attr_string(writer, "name", box->name, strlen(box->name));
    }

    if ((err == AMARK_ERROR_NONE) && box->has_number)
    {
        err = amark_dump_attr_int(writer, "number", box->number);
    }

    return err;
}

static const amark_node_type_desc_t box_type = {
    sizeof(amark_node_type_desc_t), "x.box", AMARK_NODE_LEVEL_BLOCK, AMARK_CONTENT_BLOCK,
    AMARK_CONTENT_BLOCK, AMARK_LITERAL_NONE, sizeof(box_data_t), NULL, NULL, NULL, box_deinit,
    NULL, NULL, box_dump, NULL
};

static const amark_node_type_desc_t *const x_types[] = { &box_type };

static const amark_extension_t x_extension = {
    sizeof(amark_extension_t), "x", x_types, 1, NULL, 0, NULL
};

/* Sets :name and :number of an x.box (the attribute function of the round trips). */
static amark_error_t box_attr(amark_node_t *node, const char *name, const char *value,
                              size_t length, void *context)
{
    box_data_t *box = amark_node_data(node);

    (void)context;
    if ((strcmp(amark_node_type_name(node), "x.box") != 0) || (value[length] != '\0'))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    if (strcmp(name, "name") == 0)
    {
        char *copy = amark_malloc(length + 1);

        if (copy == NULL)
        {
            return AMARK_ERROR_OUT_OF_MEMORY;
        }

        memcpy(copy, value, length + 1);
        amark_free(box->name);
        box->name = copy;
        return AMARK_ERROR_NONE;
    }

    if (strcmp(name, "number") == 0)
    {
        box->number = strtoll(value, NULL, 10);
        box->has_number = true;
        return AMARK_ERROR_NONE;
    }

    return AMARK_ERROR_INVALID_ARGUMENT;
}

/* Creates a document whose syntax set (owned by the document) is core + x. */
static amark_document_t *create_x_document(void)
{
    amark_syntax_set_t *set = NULL;
    amark_document_t *document = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_create(&set));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &x_extension));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_freeze(set));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(set, &document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_take_syntax_set(document, set));

    return document;
}

/* --- Helpers -------------------------------------------------------------------- */

/* Builds sexpr in document with attr_fn (no context) and requires success. */
static amark_node_t *build(amark_document_t *document, const char *sexpr,
                           amark_test_tree_attr_fn_t attr_fn)
{
    amark_node_t *node = NULL;
    const char *reason = NULL;
    amark_error_t err = amark_test_tree_build(document, sexpr, attr_fn, NULL, &node, &reason);

    if (err != AMARK_ERROR_NONE)
    {
        NAOS_NOTE("%s", (reason != NULL) ? reason : "(no reason)");
    }

    amark_test_tree_free_reason(reason);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, err);
    NAOS_REQUIRE_NOT_NULL(node);

    return node;
}

/* Builds the dump sexpr in a new document of core + x with box_attr and checks
 * that the root is returned and that its dump with flags is sexpr again. */
#define CHECK_ROUND_TRIP(flags, sexpr) \
    do { \
        amark_document_t *rt_document_ = create_x_document(); \
        amark_node_t *rt_root_ = build(rt_document_, (sexpr), box_attr); \
        NAOS_CHECK_PTR_EQ(amark_document_root(rt_document_), rt_root_); \
        AMARK_CHECK_TREE_DUMP(rt_root_, (flags), sexpr); \
        amark_document_destroy(rt_document_); \
    } while (0)

/* --- Round trips ---------------------------------------------------------------- */

NAOS_TEST(the_example_of_the_design_reads_back)
{
    CHECK_ROUND_TRIP(0,
                     "(document\n"
                     "  (heading :level 2\n"
                     "    (text \"Hello\"))\n"
                     "  (list :type bullet :tight true\n"
                     "    (item\n"
                     "      (paragraph\n"
                     "        (text \"a\")\n"
                     "        (softbreak)\n"
                     "        (code \"x\")))))\n");
}

NAOS_TEST(every_builtin_attribute_reads_back)
{
    CHECK_ROUND_TRIP(0,
                     "(document\n"
                     "  (heading :level 6\n"
                     "    (strong\n"
                     "      (emph\n"
                     "        (text \"s\")))\n"
                     "    (linebreak)\n"
                     "    (html_inline \"<b>\\nc\"))\n"
                     "  (list :type ordered :start 999999999 :tight false\n"
                     "    (item\n"
                     "      (paragraph\n"
                     "        (link :destination \"d\" :title \"t\"\n"
                     "          (text \"l\"))\n"
                     "        (image :destination \"i\" :title \"\"\n"
                     "          (text \"alt\")))))\n"
                     "  (list :type ordered :start 0 :tight true\n"
                     "    (item))\n"
                     "  (block_quote\n"
                     "    (code_block :info \"c\" \"code\\n\")\n"
                     "    (html_block \"<div>\\n\"))\n"
                     "  (link_definition :label \"L\" :destination \"u\" :title \"t2\")\n"
                     "  (code_block :info \"\" \"\")\n"
                     "  (link_definition :label \"\" :destination \"\" :title \"\")\n"
                     "  (paragraph\n"
                     "    (text \"\"))\n"
                     "  (thematic_break))\n");
}

NAOS_TEST(the_extension_attributes_read_back)
{
    CHECK_ROUND_TRIP(0,
                     "(document\n"
                     "  (x.box :name \"outer\" :number -12\n"
                     "    (x.box :number -9223372036854775808\n"
                     "      (paragraph\n"
                     "        (text \"t\")))\n"
                     "    (x.box :name \"\\x01\" :number 9223372036854775807))\n"
                     "  (x.box))\n");
}

NAOS_TEST(strings_with_every_escape_read_back)
{
    amark_document_t *document = create_x_document();
    amark_node_t *node;

    CHECK_ROUND_TRIP(0,
                     "(document\n"
                     "  (code_block :info \"a\\\"\\\\\\t\\x7f\""
                     " \"\\t\\\"x\\\"\\\\\\x01\\n\\x7f\\n\")\n"
                     "  (x.box :name \"q\\\"b\\\\n\\nr\\rt\\tz\\x01\\x1f\\x7f"
                     " ~\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80|\\xff|\\xc0\\xafx\"))\n");
    /* Uppercase hexadecimal digits are read too, escaped valid UTF-8 is written as
     * it is, and a CR LF of a lines literal is normalized by the setter. */
    node = build(document, "(code_block :info \"\\x7F\\xc3\\xa9\" \"a\\r\\nb\")", NULL);
    AMARK_CHECK_TREE_DUMP(node, 0, "(code_block :info \"\\x7f\xc3\xa9\" \"a\\nb\\n\")\n");
    amark_document_destroy(document);
}

NAOS_TEST(tokens_with_every_field_read_back)
{
    static const char sexpr[] =
        "(document\n"
        "  (heading :level 1 :tokens ((marker 0 1) (space 1 1 :role 3)"
        " (line_ending 4 1 prefix) (space 0 1 prefix partial_tab :source 1 :tab 1 2)"
        " (space 0 1 partial_tab :role 65535 :source 1 :tab 3 1))\n"
        "    (text :tokens ((text 2 2)) \"Hi\"))\n"
        "  (thematic_break))\n";
    amark_document_t *document = NULL;
    amark_source_id_t source = 9;
    amark_node_t *root;
    amark_token_t token;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_add_source(document, "# Hi\n", 5, &source));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_add_source(document, "\tx", 2, &source));
    root = build(document, sexpr, NULL);
    AMARK_CHECK_TREE_DUMP(root, AMARK_DUMP_TOKENS,
                          "(document\n"
                          "  (heading :level 1 :tokens ((marker 0 1) (space 1 1 :role 3)"
                          " (line_ending 4 1 prefix) (space 0 1 prefix partial_tab :source 1"
                          " :tab 1 2) (space 0 1 partial_tab :role 65535 :source 1 :tab 3 1))\n"
                          "    (text :tokens ((text 2 2)) \"Hi\"))\n"
                          "  (thematic_break))\n");
    memset(&token, 0, sizeof(token));
    token.struct_size = sizeof(token);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_token_at(amark_node_first_child(root), 3, &token));
    NAOS_CHECK_INT_EQ(AMARK_TOKEN_SPACE, token.kind);
    NAOS_CHECK_UINT_EQ(AMARK_TOKEN_FLAG_CONTAINER_PREFIX | AMARK_TOKEN_FLAG_PARTIAL_TAB,
                       token.flags);
    NAOS_CHECK_UINT_EQ(1, token.source);
    NAOS_CHECK_UINT_EQ(1, token.tab_column_offset);
    NAOS_CHECK_UINT_EQ(2, token.tab_columns);
    amark_document_destroy(document);

    /* :span is ignored and computed again by the dump; :edits is ignored (the new
     * nodes have no records). Every token kind is read. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_add_source(document, "\xef\xbb\xbf\\*&amp;\0\n\n",
                                                  12, &source));
    root = build(document,
                 "(document :edits (children descendants) :span (9 9 9 9)\n"
                 "  (paragraph :edits (created bit7) :span (1 1 1 1)"
                 " :tokens ((bom 0 3) (escape 3 2) (char_ref 5 5) (replaced 10 1)"
                 " (line_ending 11 1) (lazy_continuation 12 0))\n"
                 "    (text :tokens ((text 3 1)) \"x\")))\n",
                 NULL);
    AMARK_CHECK_TREE_DUMP(root, AMARK_DUMP_TOKENS | AMARK_DUMP_EDITS | AMARK_DUMP_SPANS,
                          "(document :span (1 1 2 1)\n"
                          "  (paragraph :span (1 1 2 1)"
                          " :tokens ((bom 0 3) (escape 3 2) (char_ref 5 5)"
                          " (replaced 10 1) (line_ending 11 1) (lazy_continuation 12 0))\n"
                          "    (text :span (1 4 1 5) :tokens ((text 3 1)) \"x\")))\n");
    amark_document_destroy(document);
}

NAOS_TEST(a_detached_node_and_children_after_the_existing_ones)
{
    amark_document_t *document = NULL;
    amark_node_t *node;
    amark_node_t *root;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    root = amark_document_root(document);
    /* Any top node but document is detached; the indentation is not required. */
    node = build(document, "  (paragraph(text \"a\")\t(softbreak)\r\n(text\"b\"))  \n", NULL);
    NAOS_CHECK_NULL(amark_node_parent(node));
    NAOS_CHECK_NULL(amark_node_first_child(root));
    AMARK_CHECK_TREE_DUMP(node, 0,
                          "(paragraph\n"
                          "  (text \"a\")\n"
                          "  (softbreak)\n"
                          "  (text \"b\"))\n");
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(root, node));
    NAOS_CHECK_PTR_EQ(root, build(document, "(document (thematic_break) (paragraph))", NULL));
    NAOS_CHECK_PTR_EQ(root, build(document, "(document)", NULL));
    AMARK_CHECK_TREE_DUMP(root, 0,
                          "(document\n"
                          "  (paragraph\n"
                          "    (text \"a\")\n"
                          "    (softbreak)\n"
                          "    (text \"b\"))\n"
                          "  (thematic_break)\n"
                          "  (paragraph))\n");
    amark_document_destroy(document);
}

NAOS_TEST(the_dump_of_new_nodes_reads_back)
{
    amark_document_t *document = NULL;
    amark_node_t *node = NULL;
    amark_buffer_t *out = NULL;
    amark_node_type_t type;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_create(&out));
    /* Each built-in type with its defaults (an empty code included). */
    for (type = AMARK_NODE_BLOCK_QUOTE; type <= AMARK_NODE_IMAGE; type++)
    {
        amark_node_t *copy;

        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, type, &node));
        amark_buffer_clear(out);
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_dump(node, 0, out));
        copy = build(document, amark_buffer_data(out), NULL);
        NAOS_NOTE("%s", amark_buffer_data(out));
        NAOS_CHECK_TRUE(amark_node_equal(node, copy, 0));
    }

    amark_buffer_destroy(out);
    amark_document_destroy(document);
}

NAOS_SUITE(tree_builder_round_trip,
    NAOS_SET_TEARDOWN(restore),
    NAOS_ADD_TEST(the_example_of_the_design_reads_back),
    NAOS_ADD_TEST(every_builtin_attribute_reads_back),
    NAOS_ADD_TEST(the_extension_attributes_read_back),
    NAOS_ADD_TEST(strings_with_every_escape_read_back),
    NAOS_ADD_TEST(tokens_with_every_field_read_back),
    NAOS_ADD_TEST(a_detached_node_and_children_after_the_existing_ones),
    NAOS_ADD_TEST(the_dump_of_new_nodes_reads_back))

/* --- Values of the extension attributes ----------------------------------------- */

/* The calls recorded by record_attr. */
typedef struct record
{
    size_t calls;
    amark_node_t *nodes[8];
    char names[8][16];
    char values[8][32];
    size_t lengths[8];
    /* The result of the call with that number (1-based; 0 for none). */
    size_t fail_call;
    amark_error_t fail_error;
} record_t;

static amark_error_t record_attr(amark_node_t *node, const char *name, const char *value,
                                 size_t length, void *context)
{
    record_t *r = context;
    size_t i = r->calls;

    r->calls++;
    if ((i < 8) && (strlen(name) < 16) && (length < 32))
    {
        r->nodes[i] = node;
        memcpy(r->names[i], name, strlen(name) + 1);
        memcpy(r->values[i], value, length + 1);
        r->lengths[i] = length;
    }

    if (r->calls == r->fail_call)
    {
        return r->fail_error;
    }

    return AMARK_ERROR_NONE;
}

NAOS_TEST(the_extension_attributes_reach_the_callback)
{
    amark_document_t *document = create_x_document();
    record_t r;
    amark_node_t *node = NULL;
    const char *reason = "unchanged";

    memset(&r, 0, sizeof(r));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_test_tree_build(document,
                                              "(x.box :s \"a\\x00b\\n\" :i -12 :sym left_2"
                                              " :list (a (b -1) \"s\") :e \"\"\n"
                                              "  (heading :level 3 :custom 0))",
                                              record_attr, &r, &node, &reason));
    NAOS_CHECK_NULL(reason);
    NAOS_REQUIRE_UINT_EQ(6, r.calls);
    NAOS_CHECK_STR_EQ("s", r.names[0]);
    NAOS_CHECK_MEM_EQ(r.values[0], r.lengths[0] + 1, "a\0b\n", 5);
    NAOS_CHECK_STR_EQ("i", r.names[1]);
    NAOS_CHECK_STR_EQ("-12", r.values[1]);
    NAOS_CHECK_STR_EQ("sym", r.names[2]);
    NAOS_CHECK_STR_EQ("left_2", r.values[2]);
    NAOS_CHECK_STR_EQ("list", r.names[3]);
    NAOS_CHECK_STR_EQ("(a (b -1) \"s\")", r.values[3]);
    NAOS_CHECK_UINT_EQ(14, r.lengths[3]);
    NAOS_CHECK_STR_EQ("e", r.names[4]);
    NAOS_CHECK_UINT_EQ(0, r.lengths[4]);
    NAOS_CHECK_PTR_EQ(node, r.nodes[0]);
    /* An attribute a built-in type does not have goes to the callback too. */
    NAOS_CHECK_STR_EQ("custom", r.names[5]);
    NAOS_CHECK_STR_EQ("0", r.values[5]);
    NAOS_CHECK_PTR_EQ(amark_node_first_child(node), r.nodes[5]);
    NAOS_CHECK_INT_EQ(3, amark_node_heading_level(amark_node_first_child(node)));
    /* The built-in attributes and :tokens, :edits and :span do not. */
    NAOS_CHECK_UINT_EQ(6, r.calls);
    amark_document_destroy(document);
}

NAOS_TEST(the_failure_of_the_callback_is_returned)
{
    amark_document_t *document = create_x_document();
    record_t r;
    amark_node_t *node = NULL;
    const char *reason = NULL;

    memset(&r, 0, sizeof(r));
    r.fail_call = 2;
    r.fail_error = AMARK_ERROR_INVALID_STATE;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_STATE,
                      amark_test_tree_build(document,
                                            "(document (x.box :a 1) (x.box :b 2) (x.box :c 3))",
                                            record_attr, &r, &node, &reason));
    NAOS_CHECK_UINT_EQ(2, r.calls);
    NAOS_CHECK_NULL(node);
    NAOS_REQUIRE_NOT_NULL(reason);
    NAOS_CHECK_STR_EQ("offset 30 (line 1, column 31): the attribute function rejects :b"
                      " (invalid state)",
                      reason);
    amark_test_tree_free_reason(reason);
    NAOS_CHECK_NULL(amark_node_first_child(amark_document_root(document)));
    amark_document_destroy(document);
}

NAOS_SUITE(tree_builder_values,
    NAOS_SET_TEARDOWN(restore),
    NAOS_ADD_TEST(the_extension_attributes_reach_the_callback),
    NAOS_ADD_TEST(the_failure_of_the_callback_is_returned))

/* --- Errors --------------------------------------------------------------------- */

/* An input that fails and its reason. */
typedef struct error_case
{
    const char *sexpr;
    const char *reason;
} error_case_t;

static const error_case_t error_cases[] = {
    { "", "offset 0 (line 1, column 1): expected '('" },
    { "  paragraph", "offset 2 (line 1, column 3): expected '('" },
    { "(", "offset 1 (line 1, column 2): expected a type name" },
    { "(document\n  (paragrap))", "offset 13 (line 2, column 4): unknown type 'paragrap'" },
    { "(x.box)", "offset 1 (line 1, column 2): unknown type 'x.box'" },
    { "(Paragraph)", "offset 1 (line 1, column 2): expected a type name" },
    { "(document (document))",
      "offset 11 (line 1, column 12): creating a node of 'document' failed (invalid argument)" },
    { "(document (text \"a\"))",
      "offset 11 (line 1, column 12): 'text' cannot be a child of 'document' (invalid argument)" },
    { "(heading :level \"2\")", "offset 16 (line 1, column 17): :level expects an integer" },
    { "(heading :level 7)",
      "offset 16 (line 1, column 17): the value of :level is rejected (invalid argument)" },
    { "(heading :level 99999999999)", "offset 16 (line 1, column 17): :level expects an integer" },
    { "(heading :level 99999999999999999999)",
      "offset 16 (line 1, column 17): integer out of range" },
    { "(heading :level 2x)", "offset 16 (line 1, column 17): expected an integer" },
    { "(heading :level -)", "offset 16 (line 1, column 17): expected an integer" },
    { "(list :type numbered)", "offset 12 (line 1, column 13): :type expects bullet or ordered" },
    { "(list :start -1)",
      "offset 13 (line 1, column 14): the value of :start is rejected (invalid argument)" },
    { "(list :tight 1)", "offset 13 (line 1, column 14): :tight expects true or false" },
    { "(link :title x)", "offset 13 (line 1, column 14): :title expects a string" },
    { "(link_definition :label \"[\")",
      "offset 24 (line 1, column 25): the value of :label is rejected (invalid argument)" },
    { "(heading :Level 2)", "offset 10 (line 1, column 11): expected an attribute name" },
    { "(heading :level)", "offset 15 (line 1, column 16): expected a value" },
    { "(heading :custom 2)", "offset 9 (line 1, column 10): no attribute function for :custom" },
    { "(text \"abc)", "offset 6 (line 1, column 7): unterminated string" },
    { "(text \"abc\\", "offset 10 (line 1, column 11): unknown escape" },
    { "(text \"a\\q\")", "offset 8 (line 1, column 9): unknown escape" },
    { "(text \"a\\x4\")", "offset 8 (line 1, column 9): \\x needs two hexadecimal digits" },
    { "(text \"a\\xg0\")", "offset 8 (line 1, column 9): \\x needs two hexadecimal digits" },
    { "(text \"a\\nb\")",
      "offset 6 (line 1, column 7): the literal is rejected (invalid argument)" },
    { "(text \"a\\x00\")",
      "offset 6 (line 1, column 7): the literal is rejected (invalid argument)" },
    { "(paragraph \"a\")",
      "offset 11 (line 1, column 12): the literal is rejected (invalid argument)" },
    { "(text \"a\" \"b\")", "offset 10 (line 1, column 11): literal after a literal or a child" },
    { "(text \"a\" :x 1)",
      "offset 10 (line 1, column 11): attribute after the literal or a child" },
    { "(paragraph\n  (text \"a\") :x 1)",
      "offset 24 (line 2, column 14): attribute after the literal or a child" },
    { "(document\n  (paragraph\n    (text \"a\"))",
      "offset 38 (line 3, column 16): expected ')'" },
    { "(paragraph x)", "offset 11 (line 1, column 12): expected ')'" },
    { "(paragraph) x", "offset 12 (line 1, column 13): unexpected text after the tree" },
    { "(paragraph)(paragraph)", "offset 11 (line 1, column 12): unexpected text after the tree" },
    { "(text :tokens ((word 0 1)) \"a\")", "offset 16 (line 1, column 17): unknown token kind" },
    { "(text :tokens ((unknown 0 1)) \"a\")",
      "offset 16 (line 1, column 17): unknown token kind" },
    { "(text :tokens ((text 0 1 bold)) \"a\")",
      "offset 25 (line 1, column 26): unknown token flag" },
    { "(text :tokens ((text 0 1 :size 2)) \"a\")",
      "offset 26 (line 1, column 27): unknown token option :size" },
    { "(text :tokens ((text 0 -1)) \"a\")",
      "offset 23 (line 1, column 24): expected an integer from 0 to 18446744073709551615" },
    { "(text :tokens ((text 0 1 :role 65536)) \"a\")",
      "offset 31 (line 1, column 32): expected an integer from 0 to 65535" },
    { "(text :tokens ((text 0 1)) \"a\")",
      "offset 15 (line 1, column 16): the token is rejected (invalid argument)" },
    { "(text :tokens (text 0 1) \"a\")", "offset 15 (line 1, column 16): expected '(' or ')'" },
    { "(text :tokens () x)", "offset 17 (line 1, column 18): expected ')'" },
    { "(text :edits (content", "offset 21 (line 1, column 22): expected ')'" },
    { "(text :edits () \"a\")", "offset 14 (line 1, column 15): empty list" },
};

NAOS_TEST(each_error_is_reported_with_its_position)
{
    amark_document_t *document = NULL;
    size_t i;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    for (i = 0; i < (sizeof(error_cases) / sizeof(error_cases[0])); i++)
    {
        amark_node_t *node = amark_document_root(document);
        const char *reason = NULL;

        NAOS_TRACE_PUSH("case %u: %s", (unsigned)i, error_cases[i].sexpr);
        NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                          amark_test_tree_build(document, error_cases[i].sexpr, NULL, NULL,
                                                &node, &reason));
        NAOS_CHECK_NULL(node);
        NAOS_CHECK_STR_EQ(error_cases[i].reason, reason);
        amark_test_tree_free_reason(reason);
        NAOS_TRACE_POP();
    }

    NAOS_CHECK_NULL(amark_node_first_child(amark_document_root(document)));
    amark_document_destroy(document);
}

NAOS_TEST(a_failure_leaves_the_document_as_it_was)
{
    static amark_test_allocator_t t;
    amark_document_t *document = NULL;
    amark_node_t *root;
    amark_node_t *node = NULL;
    const char *reason = NULL;
    amark_error_t first;
    amark_error_t second;
    size_t live_before;
    size_t live_after_first;
    size_t live_after_second;
    amark_source_id_t source = 9;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    root = build(document, "(document (paragraph (text \"a\")))", NULL);
    amark_test_allocator_init(&t);
    amark_allocator_set(amark_test_allocator_activate(&t));
    live_before = t.live_blocks;
    /* The children appended to the root before the failure are destroyed. */
    first = amark_test_tree_build(document,
                                  "(document (heading :level 1 (text \"x\")) (block_quote"
                                  " (paragraph (text \"y\"))) (heading :level 9))",
                                  NULL, NULL, &node, &reason);
    live_after_first = t.live_blocks;
    amark_test_tree_free_reason(reason);
    reason = NULL;
    /* So is a detached subtree. */
    second = amark_test_tree_build(document,
                                   "(block_quote (paragraph (text \"y\") (emph (text \"z\")"
                                   " (bogus))))",
                                   NULL, NULL, &node, &reason);
    live_after_second = t.live_blocks;
    amark_test_tree_free_reason(reason);
    amark_allocator_set(NULL);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, first);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, second);
    NAOS_CHECK_NULL(node);
    NAOS_CHECK_UINT_EQ(live_before, live_after_first);
    NAOS_CHECK_UINT_EQ(live_before, live_after_second);
    AMARK_CHECK_TREE_DUMP(root, AMARK_DUMP_EDITS,
                          "(document\n"
                          "  (paragraph\n"
                          "    (text \"a\")))\n");
    amark_document_destroy(document);

    /* The tokens added to the root before the failure are removed, and those it
     * had are kept (an equal token added again included). */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    root = amark_document_root(document);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_add_source(document, "\xef\xbb\xbfxy", 5, &source));
    reason = NULL;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_test_tree_build(document,
                                            "(document :tokens ((bom 0 3)) (paragraph) (bogus))",
                                            NULL, NULL, &node, &reason));
    NAOS_CHECK_STR_EQ("offset 43 (line 1, column 44): unknown type 'bogus'", reason);
    amark_test_tree_free_reason(reason);
    reason = NULL;
    AMARK_CHECK_TREE_DUMP(root, AMARK_DUMP_TOKENS, "(document)\n");
    NAOS_CHECK_PTR_EQ(root, build(document, "(document :tokens ((text 4 1)))", NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_test_tree_build(document,
                                            "(document :tokens ((text 4 1) (bom 0 3) (text 3 1)"
                                            " (text 4 1)) (paragraph) (bogus))",
                                            NULL, NULL, &node, &reason));
    amark_test_tree_free_reason(reason);
    AMARK_CHECK_TREE_DUMP(root, AMARK_DUMP_TOKENS, "(document :tokens ((text 4 1)))\n");
    /* On success the tokens are added to those the root has. */
    NAOS_CHECK_PTR_EQ(root, build(document, "(document :tokens ((bom 0 3)))", NULL));
    AMARK_CHECK_TREE_DUMP(root, AMARK_DUMP_TOKENS, "(document :tokens ((bom 0 3) (text 4 1)))\n");
    amark_document_destroy(document);
}

NAOS_TEST(the_edit_tracking_is_off_while_building_and_restored)
{
    amark_document_t *document = NULL;
    amark_node_t *root;
    amark_node_t *node;
    const char *reason = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    root = amark_document_root(document);
    amark_document_set_edit_tracking(document, true);
    (void)build(document, "(document (paragraph (text \"a\")))", NULL);
    NAOS_CHECK_TRUE(amark_document_edit_tracking(document));
    node = build(document, "(paragraph (emph (text \"b\")))", NULL);
    NAOS_CHECK_TRUE(amark_document_edit_tracking(document));
    /* No node got a record. */
    AMARK_CHECK_TREE_DUMP(root, AMARK_DUMP_EDITS,
                          "(document\n"
                          "  (paragraph\n"
                          "    (text \"a\")))\n");
    AMARK_CHECK_TREE_DUMP(node, AMARK_DUMP_EDITS,
                          "(paragraph\n"
                          "  (emph\n"
                          "    (text \"b\")))\n");
    /* Restored after a failure too, and kept off when it was off. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_test_tree_build(document, "(document (paragraph) (oops))", NULL,
                                            NULL, NULL, &reason));
    amark_test_tree_free_reason(reason);
    NAOS_CHECK_TRUE(amark_document_edit_tracking(document));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(root));
    amark_document_set_edit_tracking(document, false);
    (void)build(document, "(document (thematic_break))", NULL);
    NAOS_CHECK_FALSE(amark_document_edit_tracking(document));
    amark_document_destroy(document);
}

NAOS_TEST(null_arguments_are_rejected)
{
    amark_document_t *document = NULL;
    amark_node_t *node = NULL;
    const char *reason = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_test_tree_build(NULL, "(paragraph)", NULL, NULL, &node, &reason));
    NAOS_CHECK_STR_EQ("offset 0 (line 1, column 1): NULL document or input", reason);
    amark_test_tree_free_reason(reason);
    reason = NULL;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_test_tree_build(document, NULL, NULL, NULL, &node, &reason));
    NAOS_CHECK_NOT_NULL(reason);
    amark_test_tree_free_reason(reason);
    NAOS_CHECK_NULL(node);
    /* out_node and out_reason may be NULL. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_test_tree_build(document, "(", NULL, NULL, NULL, NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_test_tree_build(document, "(document (paragraph))", NULL, NULL, NULL,
                                            NULL));
    NAOS_CHECK_NOT_NULL(amark_node_first_child(amark_document_root(document)));
    amark_test_tree_free_reason(NULL);
    amark_document_destroy(document);
}

/* Builds a tree with tokens and an extension attribute in a new document; on a
 * failure the reason is freed and the result must be OOM. */
static amark_error_t build_scenario(void *context)
{
    amark_document_t *document = NULL;
    amark_source_id_t source = 0;
    const char *reason = NULL;
    amark_error_t err;

    (void)context;
    err = amark_document_create(NULL, &document);
    if (err == AMARK_ERROR_NONE)
    {
        err = amark_document_add_source(document, "# Hi\n", 5, &source);
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = amark_test_tree_build(document,
                                    "(document\n"
                                    "  (heading :level 1 :tokens ((marker 0 1) (space 1 1)"
                                    " (line_ending 4 1))\n"
                                    "    (text :tokens ((text 2 2)) \"Hi\"))\n"
                                    "  (code_block :info \"c\" \"x\\ny\\n\")\n"
                                    "  (link_definition :label \"l\" :destination \"d\""
                                    " :title \"t\"))\n",
                                    NULL, NULL, NULL, &reason);
        amark_test_tree_free_reason(reason);
    }

    amark_document_destroy(document);

    return err;
}

NAOS_TEST(building_survives_every_allocation_failure)
{
    AMARK_CHECK_OOM_SWEEP(build_scenario, NULL);
}

NAOS_SUITE(tree_builder_errors,
    NAOS_SET_TEARDOWN(restore),
    NAOS_ADD_TEST(each_error_is_reported_with_its_position),
    NAOS_ADD_TEST(a_failure_leaves_the_document_as_it_was),
    NAOS_ADD_TEST(the_edit_tracking_is_off_while_building_and_restored),
    NAOS_ADD_TEST(null_arguments_are_rejected),
    NAOS_ADD_TEST(building_survives_every_allocation_failure))

NAOS_MAIN(
    NAOS_ADD_SUITE(tree_builder_round_trip),
    NAOS_ADD_SUITE(tree_builder_values),
    NAOS_ADD_SUITE(tree_builder_errors)
)
