/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The tests of Dump (syntax-tree): the S-expression dump of INPUT.md 3.1 with
 * every flag and with fewer, the escapes of the strings (quotes, backslashes,
 * control characters, invalid UTF-8), the attributes of extension types
 * written through the dump writer, the round trip with the tree builder of the
 * tests (13.8), the arguments, a deep tree and the allocation failures.
 */
#include "tree/tree.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <naos/naos.h>

#include "test_allocator.h"
#include "tree_builder.h"

#define ALL_FLAGS \
    (AMARK_DUMP_STYLE | AMARK_DUMP_LAYOUT | AMARK_DUMP_SPELLING | AMARK_DUMP_POSITIONS | \
     AMARK_DUMP_LINES)

/* ---- Helpers ------------------------------------------------------------------- */

/* A document of set (the built-in set when NULL) with one finished source holding
 * text (none when NULL). */
static amark_document_t *new_document_of(amark_syntax_set_t *set, const char *text)
{
    amark_document_t *document = NULL;
    amark_source_id_t source = 0;

    if (amark_document_create(set, &document) != AMARK_ERROR_NONE)
    {
        return NULL;
    }
    if (set != NULL && amark_document_take_syntax_set(document, set) != AMARK_ERROR_NONE)
    {
        amark_document_destroy(document);
        return NULL;
    }
    if (text != NULL && amark_document_add_source(document, text, strlen(text),
                                                  AMARK_SOURCE_FINISHED,
                                                  &source) != AMARK_ERROR_NONE)
    {
        amark_document_destroy(document);
        return NULL;
    }
    return document;
}

static amark_document_t *new_document(const char *text)
{
    return new_document_of(NULL, text);
}

#define CHECK_TREE_OK(document) \
    NAOS_CHECK_STR_EQ("none", amark_tree_violation_name(amark_tree_check((document), NULL)))

/* The dump of node with flags in a new string (malloc), or NULL on failure. */
static char *dump_text(const amark_node_t *node, uint32_t flags)
{
    amark_buffer_t *buffer = NULL;
    char *text = NULL;

    if (amark_buffer_create(&buffer) != AMARK_ERROR_NONE)
    {
        return NULL;
    }
    if (amark_node_dump(node, flags, buffer) == AMARK_ERROR_NONE)
    {
        size_t length = amark_buffer_length(buffer);

        text = (char *)malloc(length + 1);
        if (text != NULL)
        {
            memcpy(text, amark_buffer_data(buffer), length + 1);
        }
    }
    amark_buffer_destroy(buffer);
    return text;
}

/* Builds tree on a document of source, dumps it with flags, builds the dump on a
 * second document of the same source and checks that it dumps the same and has
 * the same meaning (styles included). */
static void check_round_trip(const char *source, const char *tree, uint32_t flags)
{
    amark_document_t *first = new_document(source);
    amark_document_t *second = new_document(source);
    char *text;

    NAOS_REQUIRE_NOT_NULL(first);
    NAOS_REQUIRE_NOT_NULL(second);
    AMARK_CHECK_TREE_BUILD(first, NULL, tree);
    CHECK_TREE_OK(first);
    text = dump_text(amark_document_root(first), flags);
    NAOS_REQUIRE_NOT_NULL(text);
    AMARK_CHECK_TREE_BUILD(second, NULL, text);
    CHECK_TREE_OK(second);
    AMARK_CHECK_TREE_DUMP(amark_document_root(second), flags, text);
    /* The styles are kept when they are dumped. */
    NAOS_CHECK_TRUE(amark_node_equal(amark_document_root(first), amark_document_root(second),
                                     (flags & AMARK_DUMP_STYLE) != 0 ? AMARK_EQUAL_STYLE : 0));
    free(text);
    amark_document_destroy(first);
    amark_document_destroy(second);
}

/* ---- INPUT.md 3.1 ---------------------------------------------------------------- */

static const char input_3_1_source[] = "> - one *two\n>   three* four\n";

static const char input_3_1_tree[] =
    "(document\n"
    "  :lines (0 2)\n"
    "  (block_quote\n"
    "    :lines (0 2)\n"
    "    :margins ((0 0 2 present) (1 0 2 present))\n"
    "    (list :type bullet :tight true :style (:bullet \"-\")\n"
    "      :lines (0 2)\n"
    "      (item\n"
    "        :lines (0 2)\n"
    "        :margins ((0 2 4 present first) (1 2 4 present))\n"
    "        (paragraph\n"
    "          :lines (0 2)\n"
    "          :content laid ((0 4 12) (1 4 15))\n"
    "          (text :spelling ((text 0 4 :facet literal)))\n"
    "          (emph :style (:delimiter \"*\")\n"
    "                :spelling ((delimiter 4 5 :role 1) (delimiter 14 15 :role 2))\n"
    "            (text :spelling ((text 5 8 :facet literal)))\n"
    "            (softbreak :spelling ((line_break 8 9)))\n"
    "            (text :spelling ((text 9 14 :facet literal))))\n"
    "          (text :spelling ((text 15 20 :facet literal))))))))\n";

/* The notation of INPUT.md 3.1 as the dump writes it with every flag: the layout
 * (lines, margins, content) and the spelling on the content (the positions of
 * the content tokens are [start, end) bytes of the content). */
static const char input_3_1_full_dump[] =
    "(document :lines (0 2) :pos (1 1 2 15)"
    " :line_table ((0 1 0 12 lf 12) (0 2 13 15 lf 15))\n"
    "  (block_quote :lines (0 2) :margins ((0 0 2 present) (1 0 2 present)) :pos (1 1 2 15)\n"
    "    (list :type bullet :start 1 :tight true :style (:bullet \"-\") :lines (0 2)"
    " :pos (1 3 2 15)\n"
    "      (item :lines (0 2) :margins ((0 2 4 present first) (1 2 4 present))"
    " :pos (1 3 2 15)\n"
    "        (paragraph :lines (0 2) :content laid ((0 4 12) (1 4 15)) :pos (1 5 2 15)\n"
    "          (text :spelling ((text 0 4 :facet literal)) :pos (1 5 1 8) \"one \")\n"
    "          (emph :style (:delimiter \"*\")"
    " :spelling ((delimiter 4 5 :role 1) (delimiter 14 15 :role 2)) :pos (1 9 2 10)\n"
    "            (text :spelling ((text 5 8 :facet literal)) :pos (1 10 1 12) \"two\")\n"
    "            (softbreak :spelling ((line_break 8 9)) :pos (1 13 1 13))\n"
    "            (text :spelling ((text 9 14 :facet literal)) :pos (2 5 2 9) \"three\"))\n"
    "          (text :spelling ((text 15 20 :facet literal)) :pos (2 11 2 15) \" four\"))))))\n";

static amark_document_t *new_input_3_1(void)
{
    amark_document_t *document = new_document(input_3_1_source);

    if (document != NULL &&
        amark_test_tree_build(document, NULL, input_3_1_tree, NULL) != AMARK_ERROR_NONE)
    {
        amark_document_destroy(document);
        return NULL;
    }
    return document;
}

NAOS_TEST(input_3_1_dumps_in_its_notation_with_every_flag)
{
    amark_document_t *document = new_input_3_1();

    NAOS_REQUIRE_NOT_NULL(document);
    AMARK_CHECK_TREE_DUMP(amark_document_root(document), ALL_FLAGS, input_3_1_full_dump);
    /* The same text again: the dump is deterministic. */
    AMARK_CHECK_TREE_DUMP(amark_document_root(document), ALL_FLAGS, input_3_1_full_dump);
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(the_flags_choose_what_is_dumped)
{
    amark_document_t *document = new_input_3_1();
    amark_node_t *paragraph;

    NAOS_REQUIRE_NOT_NULL(document);
    /* Without flags: the types, the values and the literals. */
    AMARK_CHECK_TREE_DUMP(amark_document_root(document), 0,
                          "(document\n"
                          "  (block_quote\n"
                          "    (list :type bullet :start 1 :tight true\n"
                          "      (item\n"
                          "        (paragraph\n"
                          "          (text \"one \")\n"
                          "          (emph\n"
                          "            (text \"two\")\n"
                          "            (softbreak)\n"
                          "            (text \"three\"))\n"
                          "          (text \" four\"))))))\n");
    /* The styles that have a value. */
    AMARK_CHECK_TREE_DUMP(amark_document_root(document), AMARK_DUMP_STYLE,
                          "(document\n"
                          "  (block_quote\n"
                          "    (list :type bullet :start 1 :tight true :style (:bullet \"-\")\n"
                          "      (item\n"
                          "        (paragraph\n"
                          "          (text \"one \")\n"
                          "          (emph :style (:delimiter \"*\")\n"
                          "            (text \"two\")\n"
                          "            (softbreak)\n"
                          "            (text \"three\"))\n"
                          "          (text \" four\"))))))\n");
    /* A subtree is indented from its top; the line table is only on the document
     * node. */
    paragraph = amark_node_first_child(amark_node_first_child(
        amark_node_first_child(amark_node_first_child(amark_document_root(document)))));
    NAOS_REQUIRE_NOT_NULL(paragraph);
    AMARK_CHECK_TREE_DUMP(paragraph, AMARK_DUMP_LAYOUT | AMARK_DUMP_LINES,
                          "(paragraph :lines (0 2) :content laid ((0 4 12) (1 4 15))\n"
                          "  (text \"one \")\n"
                          "  (emph\n"
                          "    (text \"two\")\n"
                          "    (softbreak)\n"
                          "    (text \"three\"))\n"
                          "  (text \" four\"))\n");
    AMARK_CHECK_TREE_DUMP(amark_node_first_child(paragraph),
                          AMARK_DUMP_SPELLING | AMARK_DUMP_POSITIONS,
                          "(text :spelling ((text 0 4 :facet literal)) :pos (1 5 1 8) \"one \")\n");
    amark_document_destroy(document);
}

NAOS_TEST(the_layout_records_tokens_blank_lines_and_dropped_marks_are_dumped)
{
    /* L0 "# a #", L1 "  ", L2 "```", L3 "x", L4 "```". */
    amark_document_t *document = new_document("# a #\n  \n```\nx\n```\n");

    NAOS_REQUIRE_NOT_NULL(document);
    AMARK_CHECK_TREE_BUILD(
        document, NULL,
        "(document :lines (0 5) :blanks ((1 0 2))\n"
        "  (heading :level 2 :style (:form atx :closing true)\n"
        "    :lines (0 1)\n"
        "    :tokens ((marker 0 0 1 :role 1) (space 0 1 2) (marker 0 4 5 :role 2))\n"
        "    :content laid ((0 2 3))\n"
        "    (text :spelling ((text 0 1 :facet literal))))\n"
        "  (code_block :info \"\" :style (:form fenced :fence_char \"`\" :fence_length 3)\n"
        "    :lines (2 3)\n"
        "    :tokens ((marker 2 0 3) (marker 4 0 3))\n"
        "    :content laid ((3 0 1)))\n"
        "  (paragraph :content free \"p\\nq\" :dropped (structure)\n"
        "    (link :destination \"/u\" :style (:form inline) :dropped (destination title)\n"
        "      (text \"p\"))))");
    AMARK_CHECK_TREE_DUMP(
        amark_document_root(document), AMARK_DUMP_STYLE | AMARK_DUMP_LAYOUT | AMARK_DUMP_SPELLING,
        "(document :lines (0 5) :blanks ((1 0 2))\n"
        "  (heading :level 2 :style (:form atx :closing true) :lines (0 1)"
        " :tokens ((marker 0 0 1 :role 1) (space 0 1 2) (marker 0 4 5 :role 2))"
        " :content laid ((0 2 3))\n"
        "    (text :spelling ((text 0 1 :facet literal)) \"a\"))\n"
        "  (code_block :info \"\" :style (:form fenced :fence_char \"`\" :fence_length 3)"
        " :lines (2 3) :tokens ((marker 2 0 3) (marker 4 0 3)) :content laid ((3 0 1)) \"x\\n\")\n"
        "  (paragraph :content free \"p\\nq\" :dropped (structure)\n"
        "    (link :destination \"/u\" :title \"\" :label \"\" :style (:form inline)"
        " :dropped (destination title)\n"
        "      (text \"p\"))))\n");
    amark_document_destroy(document);
}

/* ---- Escapes ------------------------------------------------------------------------ */

NAOS_TEST(strings_escape_quotes_backslashes_controls_and_invalid_utf8)
{
    /* One line: invalid UTF-8 (a lone 0xFF, a truncated sequence 0xC3 before
     * "("), C0 controls, DEL, a quote, a backslash, a tab, a C1 control (U+0080)
     * and valid UTF-8 ("\xC3\xA9" and U+FFFD). */
    static const char line[] =
        "\xFF\xC3(\x01\x1F\x7F\"\\\t\xC2\x80\xC3\xA9\xEF\xBF\xBD\xED\xA0\x80z";
    amark_document_t *document = new_document(line);
    amark_node_t *code = NULL;
    amark_node_t *paragraph = NULL;
    amark_node_t *html = NULL;

    NAOS_REQUIRE_NOT_NULL(document);
    {
        amark_line_info_t info;

        memset(&info, 0, sizeof(info));
        info.struct_size = sizeof(info);
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_line(document, 0, &info));
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                            amark_node_create(document, AMARK_NODE_CODE_BLOCK, &code));
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                            amark_node_append_child(amark_document_root(document), code));
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(code, 0, 1));
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(code, 0, 0, info.width));
    }
    /* An html_inline literal with a CR and a LF (raw, without the line rules). */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(document, AMARK_NODE_PARAGRAPH, &paragraph));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_append_child(amark_document_root(document), paragraph));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(document, AMARK_NODE_HTML_INLINE, &html));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(paragraph, html));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_record_string(html, AMARK_HTML_INLINE_LITERAL, "<a\r\nb>", 6));

    AMARK_CHECK_TREE_DUMP(code, 0,
                          "(code_block :info \"\" "
                          "\"\\xFF\\xC3(\\x01\\x1F\\x7F\\\"\\\\\\t\\xC2\\x80\xC3\xA9\xEF\xBF\xBD"
                          "\\xED\\xA0\\x80z\\n\")\n");
    AMARK_CHECK_TREE_DUMP(paragraph, 0,
                          "(paragraph\n"
                          "  (html_inline \"<a\\r\\nb>\"))\n");
    /* The builder reads the escapes back to the same bytes. */
    {
        amark_document_t *other = new_document(line);
        char *text = dump_text(amark_document_root(document), AMARK_DUMP_LAYOUT);

        NAOS_REQUIRE_NOT_NULL(other);
        NAOS_REQUIRE_NOT_NULL(text);
        AMARK_CHECK_TREE_BUILD(other, NULL, text);
        AMARK_CHECK_TREE_DUMP(amark_document_root(other), AMARK_DUMP_LAYOUT, text);
        free(text);
        amark_document_destroy(other);
    }
    amark_document_destroy(document);
}

/* ---- Extension types and the dump writer ---------------------------------------- */

typedef struct box_data
{
    int64_t id;
    int fail;
} box_data_t;

static amark_error_t box_dump(const amark_node_t *node, const void *data,
                              amark_dump_writer_t *writer)
{
    const box_data_t *box = (const box_data_t *)data;
    amark_error_t error;

    (void)node;
    if (box->fail == 1)
    {
        /* An invalid name is refused; the error goes back through the dump. */
        return amark_dump_write_int(writer, "Bad", 1);
    }
    if (box->fail == 2)
    {
        return AMARK_ERROR_INVALID_STATE;
    }
    error = amark_dump_write_int(writer, "id", box->id);
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_dump_write_bool(writer, "ok", box->id > 0);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_dump_write_symbol(writer, "kind", "red_2");
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_dump_write_string(writer, "caption", "a\"b\n", 4);
    }
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_dump_write_string(writer, "empty", NULL, 0);
    }
    return error;
}

static const char *const box_colors[] = {"red", "Dark blue"};

static const amark_facet_desc_t box_facets[] = {
    {sizeof(amark_facet_desc_t), "title", AMARK_FACET_VALUE, AMARK_FACET_STRING, 0, 0, 0, NULL,
     0, 0, NULL, NULL, NULL, NULL},
    {sizeof(amark_facet_desc_t), "color", AMARK_FACET_VALUE, AMARK_FACET_ENUM, 0, 0, 0,
     box_colors, 2, 2, NULL, NULL, NULL, NULL},
    {sizeof(amark_facet_desc_t), "width", AMARK_FACET_STYLE, AMARK_FACET_INT, 0, -5, 100, NULL,
     0, 0, NULL, NULL, NULL, NULL},
    {sizeof(amark_facet_desc_t), "framed", AMARK_FACET_STYLE, AMARK_FACET_BOOL, 0, 0, 0, NULL,
     0, 0, NULL, NULL, NULL, NULL},
};

static const amark_node_type_desc_t box_type = {
    sizeof(amark_node_type_desc_t), "test.box", AMARK_NODE_LEVEL_BLOCK, AMARK_CONTENT_BLOCK,
    AMARK_CONTENT_BLOCK, 0, box_facets, 4, sizeof(box_data_t),
    NULL, NULL, NULL, NULL, NULL, box_dump, NULL};

static const amark_node_type_desc_t *const box_types[] = {&box_type};
static const amark_extension_t box_ext = {
    sizeof(amark_extension_t), "test.boxes", box_types, 1, NULL, 0, NULL};

NAOS_TEST(extension_types_dump_their_facets_and_write_their_data)
{
    amark_syntax_set_t *set = NULL;
    amark_document_t *document;
    amark_node_type_t box_id = AMARK_NODE_NONE;
    amark_node_t *box = NULL;
    amark_node_t *inner = NULL;
    amark_buffer_t *buffer = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_create(&set));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &box_ext));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_freeze(set));
    NAOS_REQUIRE_TRUE(amark_syntax_set_find_node_type(set, "test.box", &box_id));
    document = new_document_of(set, NULL);
    NAOS_REQUIRE_NOT_NULL(document);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, box_id, &box));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_append_child(amark_document_root(document), box));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, box_id, &inner));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(box, inner));
    ((box_data_t *)amark_node_data(box))->id = 7;
    ((box_data_t *)amark_node_data(inner))->id = -1;
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_record_string(box, 1, "T", 1));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_record_int(box, 2, 1));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_record_int(box, 3, -3));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_record_int(inner, 4, 0));

    /* The facets in the same format as the built-in types, and the data through
     * the writer after the values; an enumeration name that is not a symbol is a
     * string. */
    AMARK_CHECK_TREE_DUMP(
        amark_document_root(document), AMARK_DUMP_STYLE,
        "(document\n"
        "  (test.box :title \"T\" :color red :id 7 :ok true :kind red_2 :caption \"a\\\"b\\n\""
        " :empty \"\" :style (:width -3)\n"
        "    (test.box :title \"\" :color \"Dark blue\" :id -1 :ok false :kind red_2"
        " :caption \"a\\\"b\\n\" :empty \"\" :style (:framed false))))\n");

    /* An invalid name, and an error of the dump function: the error is returned
     * and the buffer keeps its length. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_create(&buffer));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_cstr(buffer, "kept"));
    ((box_data_t *)amark_node_data(inner))->fail = 1;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_dump(amark_document_root(document), 0, buffer));
    NAOS_CHECK_STR_EQ("kept", amark_buffer_data(buffer));
    ((box_data_t *)amark_node_data(inner))->fail = 2;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_STATE,
                      amark_node_dump(amark_document_root(document), 0, buffer));
    NAOS_CHECK_STR_EQ("kept", amark_buffer_data(buffer));
    amark_buffer_destroy(buffer);
    amark_document_destroy(document);
}

NAOS_TEST(the_writer_checks_its_arguments)
{
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_dump_write_int(NULL, "a", 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_dump_write_bool(NULL, "a", true));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_dump_write_symbol(NULL, "a", "b"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_dump_write_string(NULL, "a", "b", 1));
}

/* ---- The round trip with the builder (13.8) ------------------------------------- */

NAOS_TEST(the_dump_of_input_3_1_builds_back_to_the_same_tree)
{
    check_round_trip(input_3_1_source, input_3_1_tree, ALL_FLAGS);
    check_round_trip(input_3_1_source, input_3_1_tree, AMARK_DUMP_LAYOUT | AMARK_DUMP_SPELLING);
}

NAOS_TEST(the_dumps_of_input_3_3_and_3_4_build_back_to_the_same_trees)
{
    /* 3.3: a partial tab. */
    check_round_trip(">\t\tfoo",
                     "(block_quote :lines (0 1) :margins ((0 0 2 present))\n"
                     "  (code_block :style (:form indented)\n"
                     "    :lines (0 1)\n"
                     "    :tokens ((space 0 2 6))\n"
                     "    :content laid ((0 6 11))\n"
                     "    :spelling ((tab_remainder 0 2) (text 2 5))))",
                     ALL_FLAGS);
    /* 3.4: lazy continuation. */
    check_round_trip("> a\nb\n",
                     "(document :lines (0 2)\n"
                     "  (block_quote :lines (0 2) :margins ((0 0 2 present) (1 0 0 lazy))\n"
                     "    (paragraph :lines (0 2) :content laid ((0 2 3) (1 0 1))\n"
                     "      (text :spelling ((text 0 1 :facet literal)))\n"
                     "      (softbreak :spelling ((line_break 1 2)))\n"
                     "      (text :spelling ((text 2 3 :facet literal))))))",
                     ALL_FLAGS);
}

NAOS_TEST(the_dumps_of_other_records_build_back_to_the_same_trees)
{
    /* Blank lines, layout tokens with roles, a loose list, free content, values
     * without spelling, an empty code block and dropped marks. */
    check_round_trip(
        "# a #\n  \n```\nx\n```\n- p\n\n- q\r\n",
        "(document :lines (0 8) :blanks ((1 0 2))\n"
        "  (heading :level 1 :style (:form atx :closing true)\n"
        "    :lines (0 1)\n"
        "    :tokens ((marker 0 0 1 :role 1) (space 0 1 2) (marker 0 4 5 :role 2))\n"
        "    :content laid ((0 2 3))\n"
        "    (text :spelling ((text 0 1 :facet literal))))\n"
        "  (code_block :info \"\" :style (:form fenced :fence_char \"`\" :fence_length 3)\n"
        "    :lines (2 3)\n"
        "    :tokens ((marker 2 0 3) (marker 4 0 3))\n"
        "    :content laid ((3 0 1)))\n"
        "  (list :type bullet :style (:bullet \"-\") :lines (5 3) :blanks ((6 0 0))\n"
        "    (item :lines (5 1) :margins ((5 0 2 present first))\n"
        "      (paragraph :lines (5 1) :content laid ((5 2 3))\n"
        "        (text :spelling ((text 0 1 :facet literal)))))\n"
        "    (item :lines (7 1) :margins ((7 0 2 present first))\n"
        "      (paragraph :lines (7 1) :content laid ((7 2 3))\n"
        "        (text :spelling ((text 0 1 :facet literal))))))\n"
        "  (paragraph :content free \"p\\nq\" (text \"p\") (softbreak) (text \"q\"))\n"
        "  (code_block :info \"i\")\n"
        "  (code_block :info \"\" \"a\\tb\\n\\n\")\n"
        "  (paragraph :dropped (structure)\n"
        "    (link :destination \"/u\" :style (:form inline) :dropped (destination title)\n"
        "      (text \"t\")))\n"
        "  (code_block :info \"\" :dropped (structure literal) \"x\\n\"))",
        ALL_FLAGS);
}

NAOS_TEST(the_dumps_of_links_and_definitions_build_back_to_the_same_trees)
{
    /* A shortcut reference resolved by a definition, an unresolved one, an inline
     * link with an escape in its spelled destination, an image and inline
     * nodes with styles. */
    check_round_trip(
        "[t] [n] [a](\\*) *e* `c`\n",
        "(document :lines (0 1)\n"
        "  (paragraph :lines (0 1) :content laid ((0 0 23))\n"
        "    (link :label \"t\" :style (:form shortcut)\n"
        "      :spelling ((delimiter 0 1) (delimiter 2 3))\n"
        "      (text :spelling ((text 1 2 :facet literal))))\n"
        "    (text :spelling ((text 3 4 :facet literal)))\n"
        "    (link :label \"n\" :style (:form shortcut)\n"
        "      :spelling ((delimiter 4 5) (delimiter 6 7))\n"
        "      (text :spelling ((text 5 6 :facet literal))))\n"
        "    (text :spelling ((text 7 8 :facet literal)))\n"
        "    (link :style (:form inline)\n"
        "      :spelling ((delimiter 8 9) (delimiter 10 12) (escape 12 14 :facet destination)"
        " (delimiter 14 15))\n"
        "      (text :spelling ((text 9 10 :facet literal))))\n"
        "    (text :spelling ((text 15 16 :facet literal)))\n"
        "    (emph :style (:delimiter \"*\") :spelling ((delimiter 16 17) (delimiter 18 19))\n"
        "      (text :spelling ((text 17 18 :facet literal))))\n"
        "    (text :spelling ((text 19 20 :facet literal)))\n"
        "    (code :style (:backticks 1 :padded false)\n"
        "      :spelling ((delimiter 20 21) (text 21 22 :facet literal) (delimiter 22 23))))\n"
        "  (link_definition :label \"t\" :destination \"/u\" :title \"T\"\n"
        "    :style (:angle false :title_quote \"\\\"\")))",
        ALL_FLAGS);
}

/* ---- Arguments, deep trees and allocation failures ------------------------------ */

NAOS_TEST(invalid_arguments_leave_the_buffer_unchanged)
{
    amark_document_t *document = new_document(NULL);
    amark_buffer_t *buffer = NULL;

    NAOS_REQUIRE_NOT_NULL(document);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_create(&buffer));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_cstr(buffer, "x"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_dump(NULL, 0, buffer));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_dump(amark_document_root(document), 0, NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_dump(amark_document_root(document), 0x20u, buffer));
    NAOS_CHECK_STR_EQ("x", amark_buffer_data(buffer));
    /* The dump is appended. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_dump(amark_document_root(document), ALL_FLAGS, buffer));
    NAOS_CHECK_STR_EQ("x(document :line_table ())\n", amark_buffer_data(buffer));
    amark_buffer_destroy(buffer);
    amark_document_destroy(document);
}

/* A chain of depth block quotes under the document node of document, built
 * from the bottom up (each new quote wraps the previous one, so that each
 * insertion walks no ancestor; 14.1). */
static bool build_quote_chain(amark_document_t *document, size_t depth)
{
    amark_node_t *top = NULL;
    size_t i;

    for (i = 0; i < depth; i++)
    {
        amark_node_t *quote = NULL;

        if (amark_node_create(document, AMARK_NODE_BLOCK_QUOTE, &quote) != AMARK_ERROR_NONE ||
            (top != NULL && amark_node_append_child(quote, top) != AMARK_ERROR_NONE))
        {
            amark_node_destroy(quote);
            amark_node_destroy(top);
            return false;
        }
        top = quote;
    }
    if (top != NULL && amark_node_append_child(amark_document_root(document), top) !=
                           AMARK_ERROR_NONE)
    {
        amark_node_destroy(top);
        return false;
    }
    return true;
}

/* The length of the dump without flags of a document holding a chain of depth
 * block quotes: "(document" LF, then per level d 2 * min(d, 64) spaces,
 * "(block_quote" and LF (none after the last), then a ")" per node and the
 * final LF. */
static size_t quote_chain_dump_length(size_t depth)
{
    size_t length = strlen("(document") + 1;
    size_t d;

    for (d = 1; d <= depth; d++)
    {
        length += 2 * (d < 64 ? d : 64) + strlen("(block_quote") + (d < depth ? 1 : 0);
    }
    return length + depth + 1 + 1;
}

NAOS_TEST(a_deep_tree_is_dumped_without_recursion_in_linear_size)
{
    /* 100000 levels: a recursive walk would exhaust the stack, and without the
     * cap of the indentation the text would take about 10 GB. */
    enum
    {
        DEPTH = 100000
    };
    amark_document_t *document = new_document(NULL);
    amark_buffer_t *buffer = NULL;
    const char *data;
    size_t length;

    NAOS_REQUIRE_NOT_NULL(document);
    NAOS_REQUIRE_TRUE(build_quote_chain(document, DEPTH));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_create(&buffer));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_dump(amark_document_root(document), ALL_FLAGS, buffer));
    data = amark_buffer_data(buffer);
    length = amark_buffer_length(buffer);
    /* Linear: at most 128 spaces, the header and the line break per node. */
    NAOS_CHECK_TRUE(length < (size_t)(DEPTH + 1) * (128 + 32));
    NAOS_CHECK_TRUE(length > DEPTH + 2 && data[length - 1] == '\n' &&
                    data[length - DEPTH - 2] == ')' && data[length - 2] == ')');
    amark_buffer_clear(buffer);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_dump(amark_document_root(document), 0, buffer));
    NAOS_CHECK_UINT_EQ(quote_chain_dump_length(DEPTH), amark_buffer_length(buffer));
    amark_buffer_destroy(buffer);
    amark_document_destroy(document);
}

NAOS_TEST(the_capped_indentation_builds_back_to_the_same_tree)
{
    /* Deeper than 64 levels: the lines below level 64 have the same indentation,
     * and the builder reads the nesting from the parentheses. */
    enum
    {
        DEPTH = 200
    };
    amark_document_t *first = new_document(NULL);
    amark_document_t *second = new_document(NULL);
    char *text;
    const char *line;

    NAOS_REQUIRE_NOT_NULL(first);
    NAOS_REQUIRE_NOT_NULL(second);
    NAOS_REQUIRE_TRUE(build_quote_chain(first, DEPTH));
    text = dump_text(amark_document_root(first), ALL_FLAGS);
    NAOS_REQUIRE_NOT_NULL(text);
    NAOS_CHECK_UINT_EQ(quote_chain_dump_length(DEPTH) + strlen(" :line_table ()"),
                       strlen(text));
    /* The last line (level 200) is indented by 128 spaces. */
    line = strrchr(text, '\n');
    while (line > text && line[-1] != '\n')
    {
        line--;
    }
    NAOS_CHECK_UINT_EQ(128, strspn(line, " "));
    AMARK_CHECK_TREE_BUILD(second, NULL, text);
    CHECK_TREE_OK(second);
    AMARK_CHECK_TREE_DUMP(amark_document_root(second), ALL_FLAGS, text);
    NAOS_CHECK_TRUE(amark_node_equal(amark_document_root(first), amark_document_root(second),
                                     AMARK_EQUAL_STYLE));
    free(text);
    amark_document_destroy(first);
    amark_document_destroy(second);
}

NAOS_TEST(an_empty_literal_with_one_line_builds_back_to_the_same_content)
{
    /* The value "\n" of a CONTENT facet is empty free content with one line; the
     * dump leaves out its :content and the literal "\n" records it again. */
    amark_document_t *document = new_document(NULL);
    amark_node_t *code = NULL;
    amark_node_t *html = NULL;

    NAOS_REQUIRE_NOT_NULL(document);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(document, AMARK_NODE_CODE_BLOCK, &code));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_append_child(amark_document_root(document), code));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_set_string(code, AMARK_CODE_BLOCK_LITERAL, "\n", 1));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(document, AMARK_NODE_HTML_BLOCK, &html));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_append_child(amark_document_root(document), html));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_record_string(html, AMARK_HTML_BLOCK_LITERAL, "\n", 1));
    NAOS_REQUIRE_INT_EQ(AMARK_CONTENT_FREE, amark_block_content_state(code));
    AMARK_CHECK_TREE_DUMP(amark_document_root(document),
                          AMARK_DUMP_LAYOUT | AMARK_DUMP_SPELLING,
                          "(document\n"
                          "  (code_block :info \"\" :dropped (literal) \"\\n\")\n"
                          "  (html_block \"\\n\"))\n");
    amark_document_destroy(document);
    /* Recorded, set, empty and not empty literals round trip. */
    check_round_trip(NULL,
                     "(code_block :info \"\" \"\\n\")\n"
                     "(code_block :info \"\" :dropped (literal) \"\\n\")\n"
                     "(html_block \"\\n\")\n"
                     "(code_block :info \"\" \"\")\n"
                     "(code_block :info \"\" :content free \"\" \"\")\n"
                     "(code_block :info \"\" \"\\n\\n\")\n"
                     "(html_block \"a\\n\")",
                     ALL_FLAGS);
}

/* Checks the content of leaf: its state, its length and its line count. */
#define CHECK_LEAF_CONTENT(leaf, state_, length_, lines_) \
    do { \
        NAOS_REQUIRE_NOT_NULL(leaf); \
        NAOS_CHECK_INT_EQ((state_), amark_block_content_state(leaf)); \
        NAOS_REQUIRE_NOT_NULL((leaf)->content); \
        NAOS_CHECK_UINT_EQ((length_), (leaf)->content->length); \
        NAOS_CHECK_UINT_EQ((lines_), (leaf)->content->line_count); \
    } while (0)

NAOS_TEST(replaced_empty_content_keeps_its_line_count_through_the_round_trip)
{
    /* A fenced code block whose laid content is one empty line (L1), a
     * paragraph and a second code block. The paragraph and the second code
     * block take the content of the first: free and empty, with no line for
     * the paragraph (its type has no CONTENT facet) and one line for the code
     * block (its literal "\n"). */
    static const char source[] = "```\n\n```\nx\n```\ny\n```\n";
    amark_document_t *first = new_document(source);
    amark_document_t *second = new_document(source);
    amark_node_t *fence;
    amark_node_t *paragraph;
    amark_node_t *code;
    char *text;

    NAOS_REQUIRE_NOT_NULL(first);
    NAOS_REQUIRE_NOT_NULL(second);
    AMARK_CHECK_TREE_BUILD(
        first, NULL,
        "(code_block :info \"\" :style (:form fenced :fence_char \"`\" :fence_length 3)\n"
        "  :lines (0 3) :tokens ((marker 0 0 3) (marker 2 0 3)) :content laid ((1 0 0)))\n"
        "(paragraph :lines (3 1) :content laid ((3 0 1))\n"
        "  (text :spelling ((text 0 1 :facet literal))))\n"
        "(code_block :info \"\" :style (:form fenced :fence_char \"`\" :fence_length 3)\n"
        "  :lines (4 3) :tokens ((marker 4 0 3) (marker 6 0 3)) :content laid ((5 0 1)))");
    fence = amark_node_first_child(amark_document_root(first));
    NAOS_REQUIRE_NOT_NULL(fence);
    paragraph = amark_node_next(fence);
    NAOS_REQUIRE_NOT_NULL(paragraph);
    code = amark_node_next(paragraph);
    CHECK_LEAF_CONTENT(fence, AMARK_CONTENT_LAID, 0, 1);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_replace_content(paragraph, fence));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_replace_content(code, fence));
    CHECK_LEAF_CONTENT(paragraph, AMARK_CONTENT_FREE, 0, 0);
    CHECK_LEAF_CONTENT(code, AMARK_CONTENT_FREE, 0, 1);
    CHECK_TREE_OK(first);

    text = dump_text(amark_document_root(first), ALL_FLAGS);
    NAOS_REQUIRE_NOT_NULL(text);
    /* The paragraph keeps ":content free \"\"", the code block its literal. */
    NAOS_CHECK_TRUE(strstr(text, "(paragraph :lines (3 1) :content free \"\"") != NULL);
    NAOS_CHECK_TRUE(strstr(text, "(code_block :info \"\" :style (:form fenced :fence_char "
                                 "\"`\" :fence_length 3) :lines (4 3) :tokens ((marker 4 0 3) "
                                 "(marker 6 0 3)) :pos (5 1 7 3) \"\\n\")") != NULL);
    AMARK_CHECK_TREE_BUILD(second, NULL, text);
    CHECK_TREE_OK(second);
    AMARK_CHECK_TREE_DUMP(amark_document_root(second), ALL_FLAGS, text);
    fence = amark_node_first_child(amark_document_root(second));
    NAOS_REQUIRE_NOT_NULL(fence);
    paragraph = amark_node_next(fence);
    NAOS_REQUIRE_NOT_NULL(paragraph);
    CHECK_LEAF_CONTENT(paragraph, AMARK_CONTENT_FREE, 0, 0);
    CHECK_LEAF_CONTENT(amark_node_next(paragraph), AMARK_CONTENT_FREE, 0, 1);
    free(text);
    amark_document_destroy(first);
    amark_document_destroy(second);
}

static amark_test_allocator_t oom_allocator;

NAOS_TEST(an_allocation_failure_leaves_the_buffer_and_the_tree_unchanged)
{
    size_t k;
    bool done = false;

    for (k = 1; !done && k < 10000; k++)
    {
        amark_document_t *document;
        amark_buffer_t *buffer = NULL;
        amark_error_t error;
        size_t failures;
        amark_tree_violation_t violation;
        bool kept;

        amark_test_allocator_init(&oom_allocator);
        amark_allocator_set(amark_test_allocator_activate(&oom_allocator));
        document = new_input_3_1();
        if (document == NULL || amark_buffer_create(&buffer) != AMARK_ERROR_NONE ||
            amark_buffer_append_cstr(buffer, "x") != AMARK_ERROR_NONE)
        {
            amark_buffer_destroy(buffer);
            amark_document_destroy(document);
            amark_allocator_set(NULL);
            NAOS_FAIL("setup failed");
            return;
        }
        oom_allocator.fail_at = oom_allocator.calls + k;
        error = amark_node_dump(amark_document_root(document), ALL_FLAGS, buffer);
        failures = oom_allocator.failures;
        oom_allocator.fail_at = 0;
        kept = strcmp(amark_buffer_data(buffer), "x") == 0;
        violation = amark_tree_check(document, NULL);
        if (failures == 0)
        {
            NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, error);
            NAOS_CHECK_TRUE(strncmp(amark_buffer_data(buffer), "x", 1) == 0 &&
                            strcmp(amark_buffer_data(buffer) + 1, input_3_1_full_dump) == 0);
            done = true;
        }
        else
        {
            NAOS_CHECK_INT_EQ(AMARK_ERROR_OUT_OF_MEMORY, error);
            NAOS_CHECK_TRUE(kept);
            /* Without a failure, the dump succeeds after a failed one. */
            amark_buffer_clear(buffer);
            NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                              amark_node_dump(amark_document_root(document), ALL_FLAGS, buffer));
            NAOS_CHECK_STR_EQ(input_3_1_full_dump, amark_buffer_data(buffer));
        }
        NAOS_CHECK_STR_EQ("none", amark_tree_violation_name(violation));
        amark_buffer_destroy(buffer);
        amark_document_destroy(document);
        amark_allocator_set(NULL);
        NAOS_CHECK_UINT_EQ(0, oom_allocator.live_blocks);
    }
    NAOS_CHECK_TRUE(done);
    NAOS_CHECK_TRUE(k > 2);
}

NAOS_SUITE(dump,
    NAOS_ADD_TEST(input_3_1_dumps_in_its_notation_with_every_flag),
    NAOS_ADD_TEST(the_flags_choose_what_is_dumped),
    NAOS_ADD_TEST(the_layout_records_tokens_blank_lines_and_dropped_marks_are_dumped),
    NAOS_ADD_TEST(strings_escape_quotes_backslashes_controls_and_invalid_utf8),
    NAOS_ADD_TEST(extension_types_dump_their_facets_and_write_their_data),
    NAOS_ADD_TEST(the_writer_checks_its_arguments),
    NAOS_ADD_TEST(the_dump_of_input_3_1_builds_back_to_the_same_tree),
    NAOS_ADD_TEST(the_dumps_of_input_3_3_and_3_4_build_back_to_the_same_trees),
    NAOS_ADD_TEST(the_dumps_of_other_records_build_back_to_the_same_trees),
    NAOS_ADD_TEST(the_dumps_of_links_and_definitions_build_back_to_the_same_trees),
    NAOS_ADD_TEST(invalid_arguments_leave_the_buffer_unchanged),
    NAOS_ADD_TEST(a_deep_tree_is_dumped_without_recursion_in_linear_size),
    NAOS_ADD_TEST(the_capped_indentation_builds_back_to_the_same_tree),
    NAOS_ADD_TEST(an_empty_literal_with_one_line_builds_back_to_the_same_content),
    NAOS_ADD_TEST(replaced_empty_content_keeps_its_line_count_through_the_round_trip),
    NAOS_ADD_TEST(an_allocation_failure_leaves_the_buffer_and_the_tree_unchanged)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(dump)
)
