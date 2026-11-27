/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The tests of the extension node types (syntax-tree), end to end through the
 * public API: a test type "test.check" with a facet of each kind (STRING, INT,
 * ENUM, BOOL), an on_change hook and extension data with its function table,
 * and the types of a GFM-like table row, "test.row" (SHARE_LINES) and
 * "test.cell" (a leaf with inline content). They go through the registration
 * (12.1, 12.2), the recording of their layout and spelling, the setters and
 * their hooks, the function table of the data (12.13), the comparison, the
 * dump, the lossless check and the verbatim judgment. The cells of a row share
 * its line in columns that do not overlap (7.8), and the backslash of an
 * escaped pipe `\|` in a cell is the own spelling of the cell between two
 * pieces of one line of its content (7.9): the cell `b \| c` has the content
 * "b | c", as the table extension of GFM gives it to the inline parser.
 */
#include "tree/tree.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <naos/naos.h>

#include "test_check.h"

/* ---- The test types --------------------------------------------------------------- */

/* The facets of "test.check". */
#define CHECK_TITLE 1u /* V STRING, no line break */
#define CHECK_COUNT 2u /* V INT 0..9 (default 1) */
#define CHECK_MODE 3u  /* S ENUM narrow wide */
#define CHECK_DONE 4u  /* V BOOL (default false) */
#define CHECK_FACETS 4u

#define CHECK_MODE_NARROW 1
#define CHECK_MODE_WIDE 2

/* The calls of the on_change hook, in order (facet and new number or text). */
#define HOOK_LOG_SIZE 8
typedef struct hook_call
{
    amark_facet_t facet;
    int64_t number;
    char text[16];
    /* What the node read for the facet during the call (the old value). */
    amark_value_state_t old_state;
    int64_t old_number;
} hook_call_t;

static hook_call_t hook_log[HOOK_LOG_SIZE];
static size_t hook_calls;
static void *hook_user_data;
static int user_data_marker;

/* The calls of the function table of the data. */
static size_t init_calls;
static size_t deinit_calls;
static size_t copy_calls;
static size_t equal_calls;
static size_t dump_calls;

static void reset_calls(void)
{
    memset(hook_log, 0, sizeof(hook_log));
    hook_calls = 0;
    hook_user_data = NULL;
    init_calls = 0;
    deinit_calls = 0;
    copy_calls = 0;
    equal_calls = 0;
    dump_calls = 0;
}

/*
 * The hook of every facet of "test.check": logs the call; refuses the title
 * "fail" (after it was validated); a count of 5 or more also sets the style mode
 * to wide on the node (a hook may set other facets).
 */
static amark_error_t check_on_change(amark_node_t *node, amark_facet_t facet,
                                     const amark_value_t *new_value, void *user_data)
{
    amark_value_t old;

    if (hook_calls < HOOK_LOG_SIZE)
    {
        hook_call_t *call = &hook_log[hook_calls];

        call->facet = facet;
        call->number = new_value->number;
        if ((new_value->text != NULL) && (new_value->length < sizeof(call->text)))
        {
            memcpy(call->text, new_value->text, new_value->length + 1);
        }
        old.struct_size = sizeof(old);
        if (amark_node_get(node, facet, &old) == AMARK_ERROR_NONE)
        {
            call->old_state = old.state;
            call->old_number = old.number;
        }
    }
    hook_calls++;
    hook_user_data = user_data;
    if ((facet == CHECK_TITLE) && (new_value->length == 4) &&
        (memcmp(new_value->text, "fail", 4) == 0))
    {
        return AMARK_ERROR_UNREPRESENTABLE;
    }
    if ((facet == CHECK_COUNT) && (new_value->number >= 5))
    {
        return amark_node_set_int(node, CHECK_MODE, CHECK_MODE_WIDE);
    }
    return AMARK_ERROR_NONE;
}

static const char *const check_mode_names[] = {"narrow", "wide"};

static const amark_facet_desc_t check_facets[CHECK_FACETS] = {
    {sizeof(amark_facet_desc_t), "title", AMARK_FACET_VALUE, AMARK_FACET_STRING,
     AMARK_FACET_NO_LINE_BREAK, 0, 0, NULL, 0, 0, NULL, NULL, check_on_change,
     &user_data_marker},
    {sizeof(amark_facet_desc_t), "count", AMARK_FACET_VALUE, AMARK_FACET_INT, 0, 0, 9, NULL, 0,
     1, NULL, NULL, check_on_change, &user_data_marker},
    {sizeof(amark_facet_desc_t), "mode", AMARK_FACET_STYLE, AMARK_FACET_ENUM, 0, 0, 0,
     check_mode_names, 2, 0, NULL, NULL, check_on_change, &user_data_marker},
    {sizeof(amark_facet_desc_t), "done", AMARK_FACET_VALUE, AMARK_FACET_BOOL, 0, 0, 0, NULL, 0,
     0, NULL, NULL, check_on_change, &user_data_marker},
};

/* The extension data of "test.check": a note it owns and a revision. */
typedef struct check_data
{
    char *note;
    int64_t revision;
} check_data_t;

static char *duplicate(const char *text)
{
    size_t length = strlen(text);
    char *copy = (char *)malloc(length + 1);

    if (copy != NULL)
    {
        memcpy(copy, text, length + 1);
    }
    return copy;
}

static amark_error_t check_init(amark_node_t *node, void *data)
{
    check_data_t *check = (check_data_t *)data;

    (void)node;
    init_calls++;
    check->note = duplicate("new");
    return (check->note != NULL) ? AMARK_ERROR_NONE : AMARK_ERROR_OUT_OF_MEMORY;
}

static void check_deinit(amark_node_t *node, void *data)
{
    (void)node;
    deinit_calls++;
    free(((check_data_t *)data)->note);
}

static amark_error_t check_copy(amark_node_t *dst, void *dst_data, const amark_node_t *src,
                                const void *src_data)
{
    const check_data_t *from = (const check_data_t *)src_data;
    check_data_t *to = (check_data_t *)dst_data;

    (void)dst;
    (void)src;
    copy_calls++;
    to->note = duplicate(from->note);
    to->revision = from->revision;
    return (to->note != NULL) ? AMARK_ERROR_NONE : AMARK_ERROR_OUT_OF_MEMORY;
}

static bool check_equal(const amark_node_t *a, const void *a_data, const amark_node_t *b,
                        const void *b_data)
{
    const check_data_t *x = (const check_data_t *)a_data;
    const check_data_t *y = (const check_data_t *)b_data;

    (void)a;
    (void)b;
    equal_calls++;
    return (x->revision == y->revision) && (strcmp(x->note, y->note) == 0);
}

static amark_error_t check_dump(const amark_node_t *node, const void *data,
                                amark_dump_writer_t *writer)
{
    const check_data_t *check = (const check_data_t *)data;
    amark_error_t error;

    (void)node;
    dump_calls++;
    error = amark_dump_write_string(writer, "note", check->note, strlen(check->note));
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_dump_write_int(writer, "revision", check->revision);
    }
    return error;
}

/* A leaf block without content: its facets are spelled by its layout tokens. */
static const amark_node_type_desc_t check_type = {
    sizeof(amark_node_type_desc_t), "test.check", AMARK_NODE_LEVEL_BLOCK, AMARK_CONTENT_BLOCK,
    0, 0, check_facets, CHECK_FACETS, sizeof(check_data_t), NULL, check_init, check_deinit,
    check_copy, check_equal, check_dump, NULL};

/* The roles of the layout tokens of a cell. */
#define CELL_ROLE_PAD 1u           /* the white space around the content */
#define CELL_ROLE_PIPE_ESCAPE 2u   /* the backslash of `\|` inside the content */
/* The role of the pipes of a row. */
#define ROW_ROLE_PIPE 1u

static bool row_can_contain(const amark_node_t *self, amark_node_type_t child);

/* A table row: its cells share its lines (7.8); its own spelling is its pipes. */
static const amark_node_type_desc_t row_type = {
    sizeof(amark_node_type_desc_t), "test.row", AMARK_NODE_LEVEL_BLOCK, AMARK_CONTENT_BLOCK,
    AMARK_CONTENT_BLOCK, AMARK_NODE_TYPE_SHARE_LINES, NULL, 0, 0, row_can_contain, NULL, NULL,
    NULL, NULL, NULL, NULL};

/* A table cell: a leaf with inline content. */
static const amark_node_type_desc_t cell_type = {
    sizeof(amark_node_type_desc_t), "test.cell", AMARK_NODE_LEVEL_BLOCK, AMARK_CONTENT_BLOCK,
    AMARK_CONTENT_INLINE, AMARK_NODE_TYPE_CONTENT, NULL, 0, 0, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL};

/* The same row without AMARK_NODE_TYPE_SHARE_LINES: its cells cannot share its
 * line. */
static const amark_node_type_desc_t plain_row_type = {
    sizeof(amark_node_type_desc_t), "test.plain_row", AMARK_NODE_LEVEL_BLOCK,
    AMARK_CONTENT_BLOCK, AMARK_CONTENT_BLOCK, 0, NULL, 0, 0, row_can_contain, NULL, NULL, NULL,
    NULL, NULL, NULL};

static const amark_node_type_desc_t *const test_types[] = {&check_type, &row_type, &cell_type,
                                                           &plain_row_type};
static const amark_extension_t test_extension = {
    sizeof(amark_extension_t), "test.ext", test_types, 4, NULL, 0, NULL};

/* The identifiers the frozen set assigns, in the order of the types. */
#define CHECK_TYPE (AMARK_NODE_DYNAMIC_FIRST + 0u)
#define ROW_TYPE (AMARK_NODE_DYNAMIC_FIRST + 1u)
#define CELL_TYPE (AMARK_NODE_DYNAMIC_FIRST + 2u)
#define PLAIN_ROW_TYPE (AMARK_NODE_DYNAMIC_FIRST + 3u)

/* A row (either type) holds only cells. */
static bool row_can_contain(const amark_node_t *self, amark_node_type_t child)
{
    (void)self;
    return child == CELL_TYPE;
}

/* ---- Helpers ------------------------------------------------------------------- */

#define CHECK_TREE_OK(document) \
    NAOS_CHECK_STR_EQ("none", amark_tree_violation_name(amark_tree_check((document), NULL)))

/* A frozen syntax set with test_extension. */
static amark_syntax_set_t *new_set(void)
{
    amark_syntax_set_t *set = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_create(&set));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &test_extension));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_freeze(set));
    return set;
}

/* A document of test_extension, which owns its set, with one finished source
 * holding text. */
static amark_document_t *new_document(const char *text, amark_source_id_t *source)
{
    amark_document_t *document = NULL;
    amark_syntax_set_t *set = new_set();

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(set, &document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_take_syntax_set(document, set));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_add_source(document, text, strlen(text),
                                                  AMARK_SOURCE_FINISHED, source));
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

#define CHECK_NUMBER(node, facet, expected) \
    do { \
        amark_value_t v_ = get((node), (facet)); \
        NAOS_CHECK_INT_EQ(AMARK_VALUE_PRESENT, v_.state); \
        NAOS_CHECK_INT_EQ((expected), v_.number); \
    } while (0)

#define CHECK_TEXT(node, facet, expected_literal) \
    do { \
        amark_value_t v_ = get((node), (facet)); \
        NAOS_CHECK_INT_EQ(AMARK_VALUE_PRESENT, v_.state); \
        AMARK_CHECK_BYTES_EQ(v_.text, v_.length, expected_literal); \
    } while (0)

static void add_token(amark_node_t *block, amark_line_id_t line, uint32_t start, uint32_t end,
                      amark_spell_kind_t kind, amark_facet_t facet, uint16_t role)
{
    amark_layout_token_t token;

    memset(&token, 0, sizeof(token));
    token.struct_size = sizeof(token);
    token.line = line;
    token.column_start = start;
    token.column_end = end;
    token.kind = kind;
    token.facet = facet;
    token.role = role;
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_add_token(block, &token));
}

/* Gives leaf a text child whose content token covers the whole content of the
 * leaf, as the inline parser records it. */
static amark_node_t *add_text(amark_node_t *leaf)
{
    amark_node_t *text = new_node(amark_node_document(leaf), AMARK_NODE_TEXT);
    amark_content_token_t token;
    const char *bytes = NULL;
    size_t length = 0;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(leaf, text));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_content_bytes(leaf, &bytes, &length));
    memset(&token, 0, sizeof(token));
    token.struct_size = sizeof(token);
    token.start = 0;
    token.end = length;
    token.kind = AMARK_SPELL_TEXT;
    token.facet = AMARK_TEXT_LITERAL;
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_add_content_token(text, leaf, &token));
    return text;
}

static bool verbatim(const amark_node_t *node)
{
    bool out = false;

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_verbatim(node, &out));
    return out;
}

static bool content_verbatim(const amark_node_t *leaf)
{
    bool out = false;

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_content_verbatim(leaf, &out));
    return out;
}

static amark_spelling_state_t spelling_state(const amark_node_t *node)
{
    amark_spelling_state_t state;

    memset(&state, 0xA5, sizeof(state));
    state.struct_size = sizeof(state);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_spelling_state(node, &state));
    return state;
}

/* Checks that the dump of node with flags is expected_literal. */
#define CHECK_DUMP(node, flags, expected_literal) \
    do { \
        amark_buffer_t *b_ = NULL; \
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_create(&b_)); \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_dump((node), (flags), b_)); \
        AMARK_CHECK_BUFFER_EQ(b_, expected_literal); \
        amark_buffer_destroy(b_); \
    } while (0)

/* Checks the result of the lossless check of source in document. */
#define CHECK_LOSSLESS(document, source, error_, problem_, line_, column_) \
    do { \
        amark_lossless_report_t r_; \
        memset(&r_, 0, sizeof(r_)); \
        r_.struct_size = sizeof(r_); \
        NAOS_CHECK_INT_EQ((error_), amark_document_check_lossless((document), (source), &r_)); \
        if ((error_) != AMARK_ERROR_NONE) \
        { \
            NAOS_CHECK_INT_EQ((problem_), r_.problem); \
            NAOS_CHECK_UINT_EQ((line_), r_.line); \
            NAOS_CHECK_UINT_EQ((column_), r_.column); \
        } \
    } while (0)

/*
 * The source of the documents: a check line, a blank line and a table row.
 * Columns of line 2:
 *   | a | b \| c |
 *   0123456789ABCD   (14 columns; the backslash is column 8, the escaped pipe 9)
 */
static const char test_source[] = "::: Hello 3\n\n| a | b \\| c |\n";

/* The nodes of the test document. */
typedef struct test_tree
{
    amark_document_t *document;
    amark_source_id_t source;
    amark_node_t *root;
    amark_node_t *check;
    amark_node_t *row;
    amark_node_t *cell_a;
    amark_node_t *cell_b;
    amark_node_t *text_a;
    amark_node_t *text_b;
} test_tree_t;

/*
 * Records the test document as a parser and the extension would:
 *   - the check `::: Hello 3`: the marker, the title (spelled by a token, from
 *     which its value is derived), the count (recorded, and spelled by a token);
 *   - the blank line, which the document holds;
 *   - the row `| a | b \| c |`: the pipes are the own spelling of the row, and
 *     the cells share its line between them. The cell `b \| c` has the pieces
 *     "b " and "| c" on the line, and the backslash between them is its own
 *     spelling (7.9), so its content is "b | c".
 * With escape_token false, the backslash is left out of every record. row_kind
 * is the type of the row (ROW_TYPE or PLAIN_ROW_TYPE).
 */
static test_tree_t build_tree(bool escape_token, amark_node_type_t row_kind)
{
    test_tree_t tree;

    memset(&tree, 0, sizeof(tree));
    tree.document = new_document(test_source, &tree.source);
    tree.root = amark_document_root(tree.document);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(tree.root, 0, 3));

    tree.check = new_node(tree.document, CHECK_TYPE);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(tree.root, tree.check));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(tree.check, 0, 1));
    add_token(tree.check, 0, 0, 3, AMARK_SPELL_MARKER, AMARK_FACET_NONE, 0);
    add_token(tree.check, 0, 3, 4, AMARK_SPELL_SPACE, AMARK_FACET_NONE, 0);
    add_token(tree.check, 0, 4, 9, AMARK_SPELL_TEXT, CHECK_TITLE, 0);
    add_token(tree.check, 0, 9, 10, AMARK_SPELL_SPACE, AMARK_FACET_NONE, 0);
    add_token(tree.check, 0, 10, 11, AMARK_SPELL_TEXT, CHECK_COUNT, 0);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_record_int(tree.check, CHECK_COUNT, 3));

    {
        amark_blank_line_t blank;

        memset(&blank, 0, sizeof(blank));
        blank.struct_size = sizeof(blank);
        blank.line = 1;
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_add_blank_line(tree.root, &blank));
    }

    tree.row = new_node(tree.document, row_kind);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(tree.root, tree.row));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(tree.row, 2, 1));
    add_token(tree.row, 2, 0, 1, AMARK_SPELL_MARKER, AMARK_FACET_NONE, ROW_ROLE_PIPE);
    add_token(tree.row, 2, 4, 5, AMARK_SPELL_MARKER, AMARK_FACET_NONE, ROW_ROLE_PIPE);
    add_token(tree.row, 2, 13, 14, AMARK_SPELL_MARKER, AMARK_FACET_NONE, ROW_ROLE_PIPE);

    tree.cell_a = new_node(tree.document, CELL_TYPE);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(tree.row, tree.cell_a));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(tree.cell_a, 2, 1));
    add_token(tree.cell_a, 2, 1, 2, AMARK_SPELL_SPACE, AMARK_FACET_NONE, CELL_ROLE_PAD);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(tree.cell_a, 2, 2, 3));
    add_token(tree.cell_a, 2, 3, 4, AMARK_SPELL_SPACE, AMARK_FACET_NONE, CELL_ROLE_PAD);
    tree.text_a = add_text(tree.cell_a);

    tree.cell_b = new_node(tree.document, CELL_TYPE);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(tree.row, tree.cell_b));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_set_lines(tree.cell_b, 2, 1));
    add_token(tree.cell_b, 2, 5, 6, AMARK_SPELL_SPACE, AMARK_FACET_NONE, CELL_ROLE_PAD);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(tree.cell_b, 2, 6, 8));
    if (escape_token)
    {
        add_token(tree.cell_b, 2, 8, 9, AMARK_SPELL_ESCAPE, AMARK_FACET_NONE,
                  CELL_ROLE_PIPE_ESCAPE);
    }
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(tree.cell_b, 2, 9, 12));
    add_token(tree.cell_b, 2, 12, 13, AMARK_SPELL_SPACE, AMARK_FACET_NONE, CELL_ROLE_PAD);
    tree.text_b = add_text(tree.cell_b);

    CHECK_TREE_OK(tree.document);
    return tree;
}

/* ---- Registration (12.1, 12.2) ----------------------------------------------------- */

NAOS_TEST(extension_types_are_registered_with_their_facets)
{
    amark_syntax_set_t *set = new_set();
    amark_node_type_t type = AMARK_NODE_NONE;
    const amark_facet_desc_t *facet;

    NAOS_CHECK_UINT_EQ(2, amark_syntax_set_extension_count(set));
    NAOS_CHECK_PTR_EQ(&test_extension, amark_syntax_set_extension_at(set, 1));
    NAOS_CHECK_PTR_EQ(&test_extension, amark_syntax_set_find_extension(set, "test.ext"));
    /* The types get identifiers from AMARK_NODE_DYNAMIC_FIRST, in their order. */
    NAOS_CHECK_TRUE(amark_syntax_set_find_node_type(set, "test.check", &type));
    NAOS_CHECK_UINT_EQ(CHECK_TYPE, type);
    NAOS_CHECK_TRUE(amark_syntax_set_find_node_type(set, "test.row", &type));
    NAOS_CHECK_UINT_EQ(ROW_TYPE, type);
    NAOS_CHECK_TRUE(amark_syntax_set_node_type_of(set, &cell_type, &type));
    NAOS_CHECK_UINT_EQ(CELL_TYPE, type);
    NAOS_CHECK_PTR_EQ(&check_type, amark_syntax_set_node_type_desc(set, CHECK_TYPE));
    NAOS_CHECK_PTR_EQ(&row_type, amark_syntax_set_node_type_desc(set, ROW_TYPE));
    NAOS_CHECK_TRUE(amark_syntax_set_find_node_type(set, "test.plain_row", &type));
    NAOS_CHECK_UINT_EQ(PLAIN_ROW_TYPE, type);

    /* The facets of each kind, by name and by number. */
    NAOS_CHECK_UINT_EQ(CHECK_TITLE, amark_node_type_find_facet(&check_type, "title"));
    NAOS_CHECK_UINT_EQ(CHECK_COUNT, amark_node_type_find_facet(&check_type, "count"));
    NAOS_CHECK_UINT_EQ(CHECK_MODE, amark_node_type_find_facet(&check_type, "mode"));
    NAOS_CHECK_UINT_EQ(CHECK_DONE, amark_node_type_find_facet(&check_type, "done"));
    NAOS_CHECK_UINT_EQ(AMARK_FACET_NONE, amark_node_type_find_facet(&check_type, "other"));
    facet = amark_node_type_facet(&check_type, CHECK_TITLE);
    NAOS_REQUIRE_NOT_NULL(facet);
    NAOS_CHECK_INT_EQ(AMARK_FACET_STRING, facet->kind);
    facet = amark_node_type_facet(&check_type, CHECK_COUNT);
    NAOS_REQUIRE_NOT_NULL(facet);
    NAOS_CHECK_INT_EQ(AMARK_FACET_INT, facet->kind);
    facet = amark_node_type_facet(&check_type, CHECK_MODE);
    NAOS_REQUIRE_NOT_NULL(facet);
    NAOS_CHECK_INT_EQ(AMARK_FACET_ENUM, facet->kind);
    NAOS_CHECK_INT_EQ(AMARK_FACET_STYLE, facet->role);
    facet = amark_node_type_facet(&check_type, CHECK_DONE);
    NAOS_REQUIRE_NOT_NULL(facet);
    NAOS_CHECK_INT_EQ(AMARK_FACET_BOOL, facet->kind);
    NAOS_CHECK_NULL(amark_node_type_facet(&check_type, CHECK_FACETS + 1));

    /* A frozen set cannot take the extension again. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_STATE, amark_syntax_set_append(set, &test_extension));
    amark_syntax_set_destroy(set);

    /* Nor can a set that already holds it. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_create(&set));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &test_extension));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_syntax_set_append(set, &test_extension));
    NAOS_CHECK_UINT_EQ(2, amark_syntax_set_extension_count(set));
    amark_syntax_set_destroy(set);
}

NAOS_TEST(extension_types_follow_their_containment_and_classes)
{
    amark_source_id_t source = 0;
    amark_document_t *document = new_document("", &source);
    amark_node_t *root = amark_document_root(document);
    amark_node_t *row = new_node(document, ROW_TYPE);
    amark_node_t *cell = new_node(document, CELL_TYPE);
    amark_node_t *check = new_node(document, CHECK_TYPE);

    NAOS_CHECK_PTR_EQ(&row_type, amark_node_type_desc(row));
    NAOS_CHECK_TRUE(amark_node_is_block(row));
    NAOS_CHECK_FALSE(amark_node_is_container(row)); /* no MARGINS */
    NAOS_CHECK_TRUE(amark_node_has_content(cell));
    NAOS_CHECK_FALSE(amark_node_has_content(check));
    NAOS_CHECK_TRUE(amark_node_can_contain(root, ROW_TYPE));
    NAOS_CHECK_TRUE(amark_node_can_contain(row, CELL_TYPE));
    NAOS_CHECK_FALSE(amark_node_can_contain(row, CHECK_TYPE)); /* can_contain */
    NAOS_CHECK_FALSE(amark_node_can_contain(row, AMARK_NODE_TEXT)); /* accepts */
    NAOS_CHECK_TRUE(amark_node_can_contain(cell, AMARK_NODE_TEXT));
    NAOS_CHECK_FALSE(amark_node_can_contain(check, AMARK_NODE_TEXT));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_append_child(row, check));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(row, cell));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(root, row));
    amark_node_destroy(check);
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

/* ---- Facets, hooks and data (12.2, 12.13) ------------------------------------------- */

NAOS_TEST(facets_of_each_kind_are_recorded_set_and_hooked)
{
    test_tree_t tree;
    amark_spelling_state_t state;

    reset_calls();
    tree = build_tree(true, ROW_TYPE);
    /* Creating the node called init; recording called no hook. */
    NAOS_CHECK_UINT_EQ(1, init_calls);
    NAOS_CHECK_UINT_EQ(0, hook_calls);

    /* The values: derived from the token (title), recorded (count), default (done)
     * and unset (mode, a style). */
    CHECK_TEXT(tree.check, CHECK_TITLE, "Hello");
    CHECK_NUMBER(tree.check, CHECK_COUNT, 3);
    CHECK_NUMBER(tree.check, CHECK_DONE, 0);
    NAOS_CHECK_INT_EQ(AMARK_VALUE_UNSET, get(tree.check, CHECK_MODE).state);
    NAOS_CHECK_TRUE(verbatim(tree.check));

    /* Setting the same value changes nothing and calls no hook (6.9). */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_int(tree.check, CHECK_COUNT, 3));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_string(tree.check, CHECK_TITLE, "Hello", 5));
    NAOS_CHECK_UINT_EQ(0, hook_calls);
    NAOS_CHECK_TRUE(verbatim(tree.check));

    /* Invalid values of each kind are refused before the hook. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_int(tree.check, CHECK_COUNT, 10));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_int(tree.check, CHECK_MODE, 3));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_int(tree.check, CHECK_DONE, 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_set_string(tree.check, CHECK_TITLE, "a\nb", 3));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_set_string(tree.check, CHECK_COUNT, "1", 1));
    NAOS_CHECK_UINT_EQ(0, hook_calls);

    /* A failing hook leaves the value and its spelling. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_UNREPRESENTABLE,
                      amark_node_set_string(tree.check, CHECK_TITLE, "fail", 4));
    NAOS_CHECK_UINT_EQ(1, hook_calls);
    NAOS_CHECK_UINT_EQ(CHECK_TITLE, hook_log[0].facet);
    NAOS_CHECK_STR_EQ("fail", hook_log[0].text);
    NAOS_CHECK_PTR_EQ(&user_data_marker, hook_user_data);
    CHECK_TEXT(tree.check, CHECK_TITLE, "Hello");
    NAOS_CHECK_UINT_EQ(5, amark_block_token_count(tree.check));
    NAOS_CHECK_TRUE(verbatim(tree.check));
    CHECK_TREE_OK(tree.document);

    /* Setting the count calls its hook with the old value still on the node; the
     * hook sets the style mode too (its own hook runs). The count token goes
     * and the count is marked as dropped; the title keeps its spelling. */
    reset_calls();
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_int(tree.check, CHECK_COUNT, 7));
    NAOS_CHECK_UINT_EQ(2, hook_calls);
    NAOS_CHECK_UINT_EQ(CHECK_COUNT, hook_log[0].facet);
    NAOS_CHECK_INT_EQ(7, hook_log[0].number);
    NAOS_CHECK_INT_EQ(AMARK_VALUE_PRESENT, hook_log[0].old_state);
    NAOS_CHECK_INT_EQ(3, hook_log[0].old_number);
    NAOS_CHECK_UINT_EQ(CHECK_MODE, hook_log[1].facet);
    NAOS_CHECK_INT_EQ(CHECK_MODE_WIDE, hook_log[1].number);
    NAOS_CHECK_INT_EQ(AMARK_VALUE_UNSET, hook_log[1].old_state);
    CHECK_NUMBER(tree.check, CHECK_COUNT, 7);
    CHECK_NUMBER(tree.check, CHECK_MODE, CHECK_MODE_WIDE);
    CHECK_TEXT(tree.check, CHECK_TITLE, "Hello");
    NAOS_CHECK_UINT_EQ(4, amark_block_token_count(tree.check));
    state = spelling_state(tree.check);
    NAOS_CHECK_FALSE(state.complete);
    NAOS_CHECK_UINT_EQ((1u << CHECK_COUNT) | (1u << CHECK_MODE), state.dropped_facets);
    NAOS_CHECK_FALSE(state.structure_dropped);

    /* The BOOL facet. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_int(tree.check, CHECK_DONE, 1));
    NAOS_CHECK_UINT_EQ(3, hook_calls);
    NAOS_CHECK_UINT_EQ(CHECK_DONE, hook_log[2].facet);
    NAOS_CHECK_INT_EQ(1, hook_log[2].number);
    CHECK_NUMBER(tree.check, CHECK_DONE, 1);

    /* Unsetting a style calls no hook. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_unset(tree.check, CHECK_MODE));
    NAOS_CHECK_UINT_EQ(3, hook_calls);
    NAOS_CHECK_INT_EQ(AMARK_VALUE_UNSET, get(tree.check, CHECK_MODE).state);

    /* The check, and the document above it, are no longer verbatim; the row
     * still is. The count's column is left without a record. */
    NAOS_CHECK_FALSE(verbatim(tree.check));
    NAOS_CHECK_FALSE(verbatim(tree.root));
    NAOS_CHECK_TRUE(verbatim(tree.row));
    CHECK_LOSSLESS(tree.document, tree.source, AMARK_ERROR_NOT_LOSSLESS, AMARK_LOSSLESS_GAP, 0,
                   10);
    CHECK_TREE_OK(tree.document);

    /* The title's token goes too when it is set. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_string(tree.check, CHECK_TITLE, "Bye", 3));
    CHECK_TEXT(tree.check, CHECK_TITLE, "Bye");
    NAOS_CHECK_UINT_EQ(3, amark_block_token_count(tree.check));
    CHECK_TREE_OK(tree.document);

    /* Freeing the document calls deinit for the check. */
    amark_document_destroy(tree.document);
    NAOS_CHECK_UINT_EQ(0, init_calls);
    NAOS_CHECK_UINT_EQ(1, deinit_calls);
}

NAOS_TEST(data_functions_are_called_by_copy_equal_dump_and_free)
{
    test_tree_t tree;
    amark_node_t *copy = NULL;
    check_data_t *data;
    check_data_t *copy_data;

    reset_calls();
    tree = build_tree(true, ROW_TYPE);
    data = (check_data_t *)amark_node_data(tree.check);
    NAOS_REQUIRE_NOT_NULL(data);
    NAOS_CHECK_STR_EQ("new", data->note);
    NAOS_CHECK_NULL(amark_node_data(tree.row));

    free(data->note);
    data->note = duplicate("first");
    NAOS_REQUIRE_NOT_NULL(data->note);
    data->revision = 4;

    /* The copy calls copy, not init. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_copy(tree.check, &copy));
    NAOS_CHECK_UINT_EQ(1, init_calls);
    NAOS_CHECK_UINT_EQ(1, copy_calls);
    copy_data = (check_data_t *)amark_node_data(copy);
    NAOS_REQUIRE_NOT_NULL(copy_data);
    NAOS_CHECK_TRUE(copy_data != data);
    NAOS_CHECK_TRUE(copy_data->note != data->note);
    NAOS_CHECK_STR_EQ("first", copy_data->note);
    NAOS_CHECK_INT_EQ(4, copy_data->revision);
    /* The copy keeps the spelling of the original. */
    NAOS_CHECK_UINT_EQ(5, amark_block_token_count(copy));
    CHECK_NUMBER(copy, CHECK_COUNT, 3);
    CHECK_TEXT(copy, CHECK_TITLE, "Hello");

    /* The comparison calls equal. */
    NAOS_CHECK_TRUE(amark_node_equal(tree.check, copy, 0));
    NAOS_CHECK_TRUE(amark_node_equal(tree.check, copy, AMARK_EQUAL_STYLE));
    NAOS_CHECK_UINT_GE(equal_calls, 2);
    copy_data->revision = 5;
    NAOS_CHECK_FALSE(amark_node_equal(tree.check, copy, 0));
    copy_data->revision = 4;
    NAOS_CHECK_TRUE(amark_node_equal(tree.check, copy, 0));

    /* A value facet of each kind is compared; a style only with
     * AMARK_EQUAL_STYLE. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_int(copy, CHECK_COUNT, 2));
    NAOS_CHECK_FALSE(amark_node_equal(tree.check, copy, 0));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_int(copy, CHECK_COUNT, 3));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_int(copy, CHECK_DONE, 1));
    NAOS_CHECK_FALSE(amark_node_equal(tree.check, copy, 0));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_int(copy, CHECK_DONE, 0));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_string(copy, CHECK_TITLE, "Other", 5));
    NAOS_CHECK_FALSE(amark_node_equal(tree.check, copy, 0));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_string(copy, CHECK_TITLE, "Hello", 5));
    NAOS_CHECK_TRUE(amark_node_equal(tree.check, copy, 0));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_set_int(copy, CHECK_MODE, CHECK_MODE_NARROW));
    NAOS_CHECK_TRUE(amark_node_equal(tree.check, copy, 0));
    NAOS_CHECK_FALSE(amark_node_equal(tree.check, copy, AMARK_EQUAL_STYLE));
    /* Setting the values dropped the spelling of the copy, not of the original. */
    NAOS_CHECK_TRUE(verbatim(tree.check));

    /* The dump writes the values, then the data, then the style and the
     * layout. */
    dump_calls = 0;
    CHECK_DUMP(tree.check, 0,
               "(test.check :title \"Hello\" :count 3 :done false :note \"first\" :revision 4)\n");
    CHECK_DUMP(copy, AMARK_DUMP_STYLE,
               "(test.check :title \"Hello\" :count 3 :done false :note \"first\" :revision 4"
               " :style (:mode narrow))\n");
    NAOS_CHECK_UINT_EQ(2, dump_calls);
    CHECK_DUMP(tree.check, AMARK_DUMP_LAYOUT | AMARK_DUMP_SPELLING,
               "(test.check :title \"Hello\" :count 3 :done false :note \"first\" :revision 4"
               " :lines (0 1) :tokens ((marker 0 0 3) (space 0 3 4) (text 0 4 9 :facet title)"
               " (space 0 9 10) (text 0 10 11 :facet count)))\n");
    CHECK_DUMP(copy, AMARK_DUMP_SPELLING,
               "(test.check :title \"Hello\" :count 3 :done false :note \"first\" :revision 4"
               " :dropped (title count mode done))\n");

    /* Freeing calls deinit once per node with data. */
    amark_node_destroy(copy);
    NAOS_CHECK_UINT_EQ(1, deinit_calls);
    CHECK_TREE_OK(tree.document);
    amark_document_destroy(tree.document);
    NAOS_CHECK_UINT_EQ(2, deinit_calls);
}

/* ---- The table row: shared lines and the escaped pipe (7.8, 7.9) ------------------- */

NAOS_TEST(cells_share_the_line_of_their_row)
{
    test_tree_t tree = build_tree(true, ROW_TYPE);
    amark_content_piece_t piece;
    const char *bytes = NULL;
    size_t length = 0;
    amark_line_id_t first = AMARK_LINE_NONE;
    uint32_t count = 0;

    /* Both cells have the row's line as their line range. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_lines(tree.cell_a, &first, &count));
    NAOS_CHECK_UINT_EQ(2, first);
    NAOS_CHECK_UINT_EQ(1, count);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_lines(tree.cell_b, &first, &count));
    NAOS_CHECK_UINT_EQ(2, first);
    NAOS_CHECK_UINT_EQ(1, count);

    /* The content of the first cell. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_content_bytes(tree.cell_a, &bytes, &length));
    AMARK_CHECK_BYTES_EQ(bytes, length, "a");
    CHECK_TEXT(tree.text_a, AMARK_TEXT_LITERAL, "a");

    /* The second: two pieces of one line, joined without the backslash between
     * them, which is the own spelling of the cell. */
    NAOS_CHECK_INT_EQ(AMARK_CONTENT_LAID, amark_block_content_state(tree.cell_b));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_content_bytes(tree.cell_b, &bytes, &length));
    AMARK_CHECK_BYTES_EQ(bytes, length, "b | c");
    NAOS_REQUIRE_UINT_EQ(2, amark_block_content_piece_count(tree.cell_b));
    memset(&piece, 0, sizeof(piece));
    piece.struct_size = sizeof(piece);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_content_piece_at(tree.cell_b, 0, &piece));
    NAOS_CHECK_UINT_EQ(2, piece.line);
    NAOS_CHECK_UINT_EQ(6, piece.column_start);
    NAOS_CHECK_UINT_EQ(8, piece.column_end);
    NAOS_CHECK_UINT_EQ(0, piece.content_start);
    NAOS_CHECK_UINT_EQ(2, piece.content_length);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_content_piece_at(tree.cell_b, 1, &piece));
    NAOS_CHECK_UINT_EQ(2, piece.line);
    NAOS_CHECK_UINT_EQ(9, piece.column_start);
    NAOS_CHECK_UINT_EQ(12, piece.column_end);
    NAOS_CHECK_UINT_EQ(2, piece.content_start);
    NAOS_CHECK_UINT_EQ(3, piece.content_length);
    CHECK_TEXT(tree.text_b, AMARK_TEXT_LITERAL, "b | c");

    /* The layout and the spelling of the row, in the dump. */
    CHECK_DUMP(tree.row, AMARK_DUMP_LAYOUT | AMARK_DUMP_SPELLING,
               "(test.row :lines (2 1)"
               " :tokens ((marker 2 0 1 :role 1) (marker 2 4 5 :role 1) (marker 2 13 14 :role 1))\n"
               "  (test.cell :lines (2 1) :tokens ((space 2 1 2 :role 1) (space 2 3 4 :role 1))"
               " :content laid ((2 2 3))\n"
               "    (text :spelling ((text 0 1 :facet literal)) \"a\"))\n"
               "  (test.cell :lines (2 1)"
               " :tokens ((space 2 5 6 :role 1) (escape 2 8 9 :role 2) (space 2 12 13 :role 1))"
               " :content laid ((2 6 8) (2 9 12))\n"
               "    (text :spelling ((text 0 5 :facet literal)) \"b | c\")))\n");

    /* Every byte of the source once, and every node verbatim but a cell alone,
     * which covers only a part of the line. */
    CHECK_LOSSLESS(tree.document, tree.source, AMARK_ERROR_NONE, 0, 0, 0);
    NAOS_CHECK_TRUE(content_verbatim(tree.cell_a));
    NAOS_CHECK_TRUE(content_verbatim(tree.cell_b));
    NAOS_CHECK_TRUE(verbatim(tree.text_b));
    NAOS_CHECK_TRUE(verbatim(tree.row));
    NAOS_CHECK_TRUE(verbatim(tree.check));
    NAOS_CHECK_TRUE(verbatim(tree.root));
    NAOS_CHECK_FALSE(verbatim(tree.cell_b));

    /* The cells in the reverse order of their columns do not follow each
     * other on the line. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_before(tree.cell_a, tree.cell_b));
    NAOS_CHECK_FALSE(verbatim(tree.row));
    NAOS_CHECK_FALSE(verbatim(tree.root));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_after(tree.cell_a, tree.cell_b));
    NAOS_CHECK_TRUE(verbatim(tree.row));
    NAOS_CHECK_TRUE(verbatim(tree.root));

    /* Editing the text of a cell makes the row, not the check, not verbatim. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_set_string(tree.text_b, AMARK_TEXT_LITERAL, "b | d", 5));
    NAOS_CHECK_FALSE(content_verbatim(tree.cell_b));
    NAOS_CHECK_FALSE(verbatim(tree.row));
    NAOS_CHECK_FALSE(verbatim(tree.root));
    NAOS_CHECK_TRUE(verbatim(tree.check));
    NAOS_CHECK_FALSE(verbatim(tree.cell_a)); /* still only a part of the line */
    CHECK_TREE_OK(tree.document);
    amark_document_destroy(tree.document);
}

NAOS_TEST(the_backslash_of_an_escaped_pipe_must_be_recorded)
{
    /* The same tree without the token of the backslash: its column is covered
     * by no record, and the cell does not cover its share of the line. */
    test_tree_t tree = build_tree(false, ROW_TYPE);
    const char *bytes = NULL;
    size_t length = 0;

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_content_bytes(tree.cell_b, &bytes, &length));
    AMARK_CHECK_BYTES_EQ(bytes, length, "b | c");
    CHECK_LOSSLESS(tree.document, tree.source, AMARK_ERROR_NOT_LOSSLESS, AMARK_LOSSLESS_GAP, 2,
                   8);
    NAOS_CHECK_TRUE(content_verbatim(tree.cell_b));
    NAOS_CHECK_FALSE(verbatim(tree.row));
    NAOS_CHECK_FALSE(verbatim(tree.root));
    CHECK_TREE_OK(tree.document);
    amark_document_destroy(tree.document);
}

NAOS_TEST(cells_share_a_line_only_under_a_share_lines_type)
{
    /* The same layout under a row without SHARE_LINES: the records are
     * consistent and lossless, and each cell's content is verbatim, but the
     * cells may not share the line, so the row and the document are not
     * verbatim (7.8). */
    test_tree_t plain = build_tree(true, PLAIN_ROW_TYPE);
    test_tree_t shared = build_tree(true, ROW_TYPE);

    NAOS_CHECK_UINT_EQ(PLAIN_ROW_TYPE, amark_node_get_type(plain.row));
    CHECK_TREE_OK(plain.document);
    CHECK_LOSSLESS(plain.document, plain.source, AMARK_ERROR_NONE, 0, 0, 0);
    NAOS_CHECK_TRUE(content_verbatim(plain.cell_a));
    NAOS_CHECK_TRUE(content_verbatim(plain.cell_b));
    NAOS_CHECK_TRUE(verbatim(plain.check));
    NAOS_CHECK_FALSE(verbatim(plain.row));
    NAOS_CHECK_FALSE(verbatim(plain.root));

    NAOS_CHECK_UINT_EQ(ROW_TYPE, amark_node_get_type(shared.row));
    CHECK_LOSSLESS(shared.document, shared.source, AMARK_ERROR_NONE, 0, 0, 0);
    NAOS_CHECK_TRUE(verbatim(shared.row));
    NAOS_CHECK_TRUE(verbatim(shared.root));

    /* Each cell alone, under either row, does not reach the end of the line. */
    NAOS_CHECK_FALSE(verbatim(plain.cell_a));
    NAOS_CHECK_FALSE(verbatim(shared.cell_a));
    CHECK_TREE_OK(plain.document);
    CHECK_TREE_OK(shared.document);
    amark_document_destroy(plain.document);
    amark_document_destroy(shared.document);
}

NAOS_TEST(a_row_with_its_cells_is_copied_and_compared)
{
    test_tree_t tree = build_tree(true, ROW_TYPE);
    amark_node_t *copy = NULL;
    amark_node_t *other = NULL;
    amark_node_t *other_text;
    amark_node_t *other_cell;

    /* The copy shares the lines and keeps the spelling: it is verbatim in its
     * own right, and equal. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_copy(tree.row, &copy));
    NAOS_CHECK_TRUE(amark_node_equal(tree.row, copy, AMARK_EQUAL_STYLE));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_insert_after(tree.row, copy));
    NAOS_CHECK_TRUE(verbatim(copy));
    /* The copy's columns overlap those of the original on the same line. */
    NAOS_CHECK_FALSE(verbatim(tree.root));
    amark_node_destroy(copy);
    NAOS_CHECK_TRUE(verbatim(tree.root));

    /* A row built without spelling, with the same meaning, is equal. */
    other = new_node(tree.document, ROW_TYPE);
    other_cell = new_node(tree.document, CELL_TYPE);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(other, other_cell));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_inline_text(other_cell, "a", 1));
    other_cell = new_node(tree.document, CELL_TYPE);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(other, other_cell));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_set_inline_text(other_cell, "b | c", 5));
    NAOS_CHECK_TRUE(amark_node_equal(tree.row, other, 0));
    NAOS_CHECK_TRUE(amark_node_is_new(other));
    CHECK_DUMP(other, AMARK_DUMP_LAYOUT | AMARK_DUMP_SPELLING,
               "(test.row\n"
               "  (test.cell\n"
               "    (text \"a\"))\n"
               "  (test.cell\n"
               "    (text \"b | c\")))\n");
    other_text = amark_node_first_child(other_cell);
    NAOS_REQUIRE_NOT_NULL(other_text);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_set_string(other_text, AMARK_TEXT_LITERAL, "b \\| c", 6));
    NAOS_CHECK_FALSE(amark_node_equal(tree.row, other, 0));
    amark_node_destroy(other);
    CHECK_TREE_OK(tree.document);
    amark_document_destroy(tree.document);
}

NAOS_SUITE(extension_type,
    NAOS_ADD_TEST(extension_types_are_registered_with_their_facets),
    NAOS_ADD_TEST(extension_types_follow_their_containment_and_classes),
    NAOS_ADD_TEST(facets_of_each_kind_are_recorded_set_and_hooked),
    NAOS_ADD_TEST(data_functions_are_called_by_copy_equal_dump_and_free),
    NAOS_ADD_TEST(cells_share_the_line_of_their_row),
    NAOS_ADD_TEST(the_backslash_of_an_escaped_pipe_must_be_recorded),
    NAOS_ADD_TEST(cells_share_a_line_only_under_a_share_lines_type),
    NAOS_ADD_TEST(a_row_with_its_cells_is_copied_and_compared)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(extension_type)
)
