/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The tests of CopyImport (syntax-tree): the copy within the same document
 * (5.6), with the content objects shared and copied on write (Content, 6.3),
 * the copied definitions in the definition index, and the extension data
 * copied by the copy function of the type (12.13).
 */
#include "tree/tree.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <naos/naos.h>

#include "test_allocator.h"
#include "tree_builder.h"

/* ---- Helpers ------------------------------------------------------------------- */

/* A document of the built-in syntax set with one finished source. */
static amark_document_t *new_document(const char *text)
{
    amark_document_t *document = NULL;
    amark_source_id_t source = 0;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_add_source(document, text, strlen(text),
                                                  AMARK_SOURCE_FINISHED, &source));
    return document;
}

/* The child index of node (NULL when it has fewer children). */
static amark_node_t *child_at(const amark_node_t *node, size_t index)
{
    amark_node_t *child = amark_node_first_child(node);

    while (child != NULL && index > 0)
    {
        child = amark_node_next(child);
        index--;
    }
    return child;
}

/* The next node of the subtree of top in preorder after node, or NULL. */
static amark_node_t *preorder_next(const amark_node_t *top, const amark_node_t *node)
{
    if (amark_node_first_child(node) != NULL)
    {
        return amark_node_first_child(node);
    }
    while (node != top && amark_node_next(node) == NULL)
    {
        node = amark_node_parent(node);
    }
    return node == top ? NULL : amark_node_next(node);
}

static size_t subtree_size(const amark_node_t *top)
{
    const amark_node_t *node;
    size_t count = 0;

    for (node = top; node != NULL; node = preorder_next(top, node))
    {
        count++;
    }
    return count;
}

#define CHECK_TREE_OK(document) \
    NAOS_CHECK_STR_EQ("none", amark_tree_violation_name(amark_tree_check((document), NULL)))

/* A growing text buffer for the fingerprints. */
typedef struct text_buffer
{
    char *data;
    size_t length;
    size_t capacity;
} text_buffer_t;

static void append(text_buffer_t *buffer, const char *format, ...)
{
    va_list args;
    int needed;

    va_start(args, format);
    needed = vsnprintf(NULL, 0, format, args);
    va_end(args);
    NAOS_REQUIRE_TRUE(needed >= 0);
    if (buffer->length + (size_t)needed + 1 > buffer->capacity)
    {
        size_t capacity = (buffer->capacity == 0) ? 256 : buffer->capacity;
        char *data;

        while (buffer->length + (size_t)needed + 1 > capacity)
        {
            capacity *= 2;
        }
        data = (char *)realloc(buffer->data, capacity);
        NAOS_REQUIRE_NOT_NULL(data);
        buffer->data = data;
        buffer->capacity = capacity;
    }
    va_start(args, format);
    (void)vsnprintf(buffer->data + buffer->length, buffer->capacity - buffer->length, format,
                    args);
    va_end(args);
    buffer->length += (size_t)needed;
}

/* Appends what the public API tells of node: its type, values, spelling state,
 * line range, records, content, content tokens and their base, source range
 * and verbatim judgment. */
static void fingerprint_node(text_buffer_t *buffer, const amark_node_t *node, size_t depth)
{
    const amark_node_type_desc_t *desc = amark_node_type_desc(node);
    amark_spelling_state_t state;
    amark_source_range_t range;
    amark_line_id_t first = 0;
    uint32_t count = 0;
    bool has = false;
    bool verbatim = false;
    const char *bytes = NULL;
    size_t length = 0;
    amark_content_state_t content_state = AMARK_CONTENT_NONE;
    bool own = false;
    size_t i;

    append(buffer, "%*s(%s", (int)(depth * 2), "", desc->name);
    for (i = 1; i <= desc->facet_count; i++)
    {
        amark_value_t value;

        memset(&value, 0, sizeof(value));
        value.struct_size = sizeof(value);
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_get(node, (amark_facet_t)i, &value));
        append(buffer, " :%s %d/%lld/\"%s\"", desc->facets[i - 1].name, (int)value.state,
               (long long)value.number, value.text != NULL ? value.text : "");
    }
    memset(&state, 0, sizeof(state));
    state.struct_size = sizeof(state);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_spelling_state(node, &state));
    append(buffer, " :state %d/%d/%x/%d", (int)state.has_spelling, (int)state.complete,
           (unsigned)state.dropped_facets, (int)state.structure_dropped);
    if (amark_node_is_block(node))
    {
        if (amark_block_lines(node, &first, &count) == AMARK_ERROR_NONE)
        {
            append(buffer, " :lines %u/%u", (unsigned)first, (unsigned)count);
        }
        for (i = 0; i < amark_block_margin_count(node); i++)
        {
            amark_margin_t margin;

            margin.struct_size = sizeof(margin);
            NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_margin_at(node, i, &margin));
            append(buffer, " m(%u %u %u %d %u %u)", (unsigned)margin.line,
                   (unsigned)margin.column_start, (unsigned)margin.column_end, (int)margin.state,
                   (unsigned)margin.flags, (unsigned)margin.role);
        }
        for (i = 0; i < amark_block_blank_line_count(node); i++)
        {
            amark_blank_line_t blank;

            blank.struct_size = sizeof(blank);
            NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_blank_line_at(node, i, &blank));
            append(buffer, " b(%u %u %u)", (unsigned)blank.line, (unsigned)blank.column_start,
                   (unsigned)blank.column_end);
        }
        for (i = 0; i < amark_block_token_count(node); i++)
        {
            amark_layout_token_t token;

            token.struct_size = sizeof(token);
            NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_token_at(node, i, &token));
            append(buffer, " t(%u %u %u %d %u %u)", (unsigned)token.line,
                   (unsigned)token.column_start, (unsigned)token.column_end, (int)token.kind,
                   (unsigned)token.facet, (unsigned)token.role);
        }
        if (amark_node_has_content(node))
        {
            append(buffer, " :content %d", (int)amark_block_content_state(node));
            if (amark_block_content_bytes(node, &bytes, &length) == AMARK_ERROR_NONE)
            {
                append(buffer, " \"%.*s\"", (int)length, bytes);
            }
            for (i = 0; i < amark_block_content_piece_count(node); i++)
            {
                amark_content_piece_t piece;

                piece.struct_size = sizeof(piece);
                NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                                  amark_block_content_piece_at(node, i, &piece));
                append(buffer, " p(%u %u %u %zu %zu)", (unsigned)piece.line,
                       (unsigned)piece.column_start, (unsigned)piece.column_end,
                       piece.content_start, piece.content_length);
            }
        }
    }
    for (i = 0; i < amark_node_content_token_count(node); i++)
    {
        amark_content_token_t token;

        token.struct_size = sizeof(token);
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_content_token_at(node, i, &token));
        append(buffer, " c(%zu %zu %d %u %u)", token.start, token.end, (int)token.kind,
               (unsigned)token.facet, (unsigned)token.role);
    }
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_content_base(node, &bytes, &length, &content_state, &own));
    append(buffer, " :base %d/%d/\"%.*s\"", (int)content_state, (int)own, (int)length, bytes);
    memset(&range, 0, sizeof(range));
    range.struct_size = sizeof(range);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_source_range(node, &range, &has));
    if (has)
    {
        append(buffer, " :pos %u/%zu-%zu/%u:%u-%u:%u", (unsigned)range.source,
               range.start_offset, range.end_offset, (unsigned)range.start_line,
               (unsigned)range.start_column, (unsigned)range.end_line,
               (unsigned)range.end_column);
    }
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_verbatim(node, &verbatim));
    append(buffer, " :verbatim %d)\n", (int)verbatim);
}

/* The fingerprint of the subtree of top, to be freed with free(). */
static char *fingerprint(const amark_node_t *top)
{
    text_buffer_t buffer = {NULL, 0, 0};
    const amark_node_t *node;

    NAOS_REQUIRE_NOT_NULL(top);
    for (node = top; node != NULL; node = preorder_next(top, node))
    {
        size_t depth = 0;
        const amark_node_t *up;

        for (up = node; up != top; up = amark_node_parent(up))
        {
            depth++;
        }
        fingerprint_node(&buffer, node, depth);
    }
    append(&buffer, "%s", "");
    return buffer.data;
}

/* Checks that the subtree of top has the fingerprint expected. */
#define CHECK_FINGERPRINT(expected, top)               \
    do                                                 \
    {                                                  \
        char *actual_ = fingerprint(top);              \
        NAOS_CHECK_STR_EQ((expected), actual_);        \
        free(actual_);                                 \
    } while (0)

/* Checks that every node of the subtree of top starts without the caches of
 * the verbatim judgment (Implementation Notes, 12.2). */
static void check_no_cache(const amark_node_t *top)
{
    const amark_node_t *node;

    for (node = top; node != NULL; node = preorder_next(top, node))
    {
        NAOS_CHECK_UINT_EQ(0, node->cache_flags);
        NAOS_CHECK_NULL(node->verbatim_columns);
        NAOS_CHECK_NULL(node->leaf_memo);
        NAOS_CHECK_UINT_EQ(0, node->leaf_memo_generation);
    }
}

/* ---- INPUT.md 3.1 -------------------------------------------------------------------- */

static const char input_3_1_source[] = "> - one *two\n>   three* four\n";

static const char input_3_1_tree[] =
    "(document\n"
    "  :lines (0 2)\n"
    "  (block_quote\n"
    "    :lines (0 2)\n"
    "    :margins ((0 0 2 present) (1 0 2 present))\n"
    "    (list :type bullet :start 1 :tight true :style (:bullet \"-\")\n"
    "      :lines (0 2)\n"
    "      (item\n"
    "        :lines (0 2)\n"
    "        :margins ((0 2 4 present first) (1 2 4 present))\n"
    "        (paragraph\n"
    "          :lines (0 2)\n"
    "          :content laid ((0 4 12) (1 4 15))\n"
    "          (text :spelling ((text 0 4 :facet literal)) \"one \")\n"
    "          (emph :style (:delimiter \"*\")\n"
    "                :spelling ((delimiter 4 5 :role 1) (delimiter 14 15 :role 2))\n"
    "            (text :spelling ((text 5 8 :facet literal)) \"two\")\n"
    "            (softbreak :spelling ((line_break 8 9)))\n"
    "            (text :spelling ((text 9 14 :facet literal)) \"three\"))\n"
    "          (text :spelling ((text 15 20 :facet literal)) \" four\"))))))\n";

/* The paragraph of the block quote of INPUT.md 3.1 (or of its copy). */
static amark_node_t *paragraph_of(const amark_node_t *quote)
{
    return amark_node_first_child(amark_node_first_child(amark_node_first_child(quote)));
}

/* ---- The copy ------------------------------------------------------------------------ */

NAOS_TEST(invalid_arguments_change_nothing)
{
    amark_document_t *document = new_document(input_3_1_source);
    amark_node_t *root = amark_document_root(document);
    amark_node_t *out = root;
    size_t nodes;

    AMARK_CHECK_TREE_BUILD(document, NULL, input_3_1_tree);
    nodes = document->node_count;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_copy(NULL, &out));
    NAOS_CHECK_NULL(out);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_copy(amark_node_first_child(root), NULL));
    out = root;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_copy(root, &out));
    NAOS_CHECK_NULL(out);
    NAOS_CHECK_UINT_EQ(nodes, document->node_count);
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(the_copy_reproduces_the_subtree_detached_with_shared_content)
{
    amark_document_t *document = new_document(input_3_1_source);
    amark_node_t *root = amark_document_root(document);
    amark_node_t *quote;
    amark_node_t *copy = NULL;
    amark_node_t *paragraph;
    amark_node_t *copied_paragraph;
    amark_tree_content_t *content;
    const amark_node_t *a;
    const amark_node_t *b;
    char *expected;
    size_t nodes;
    bool verbatim = false;

    AMARK_CHECK_TREE_BUILD(document, NULL, input_3_1_tree);
    quote = amark_node_first_child(root);
    paragraph = paragraph_of(quote);
    content = paragraph->content;
    NAOS_REQUIRE_NOT_NULL(content);
    NAOS_CHECK_UINT_EQ(1, content->owners);
    NAOS_CHECK_UINT_EQ(6, content->bases);
    /* Judge first, so that the original holds caches the copy must not take. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_verbatim(quote, &verbatim));
    NAOS_CHECK_TRUE(verbatim);
    expected = fingerprint(quote);
    nodes = document->node_count;

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_copy(quote, &copy));
    NAOS_REQUIRE_NOT_NULL(copy);
    NAOS_CHECK_TRUE(copy != quote);
    NAOS_CHECK_PTR_EQ(document, amark_node_document(copy));
    NAOS_CHECK_NULL(amark_node_parent(copy));
    NAOS_CHECK_NULL(amark_node_next(copy));
    NAOS_CHECK_NULL(amark_node_previous(copy));
    NAOS_CHECK_UINT_EQ(nodes + subtree_size(quote), document->node_count);
    NAOS_CHECK_UINT_EQ(subtree_size(quote), subtree_size(copy));
    check_no_cache(copy);
    CHECK_TREE_OK(document);

    /* Every node is a new one, with the same type, user pointer not copied. */
    for (a = quote, b = copy; a != NULL && b != NULL;
         a = preorder_next(quote, a), b = preorder_next(copy, b))
    {
        NAOS_CHECK_TRUE(a != b);
        NAOS_CHECK_UINT_EQ(amark_node_get_type(a), amark_node_get_type(b));
        NAOS_CHECK_NULL(amark_node_user_data(b));
    }
    /* The content object is shared: one more owner, one more base per node
     * with tokens. */
    copied_paragraph = paragraph_of(copy);
    NAOS_CHECK_PTR_EQ(content, copied_paragraph->content);
    NAOS_CHECK_UINT_EQ(2, content->owners);
    NAOS_CHECK_UINT_EQ(12, content->bases);
    NAOS_CHECK_PTR_EQ(content, amark_node_first_child(copied_paragraph)->content_tokens->base);

    /* The values, the spelling, the judgment and the positions are the same. */
    CHECK_FINGERPRINT(expected, copy);
    CHECK_FINGERPRINT(expected, quote);
    free(expected);
    CHECK_TREE_OK(document);

    /* The copy can be inserted, and destroyed: the content stays with the
     * original. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(root, copy));
    CHECK_TREE_OK(document);
    amark_node_destroy(copy);
    NAOS_CHECK_UINT_EQ(1, content->owners);
    NAOS_CHECK_UINT_EQ(6, content->bases);
    NAOS_CHECK_UINT_EQ(nodes, document->node_count);
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(an_inline_node_is_copied_with_its_tokens_on_the_same_base)
{
    amark_document_t *document = new_document(input_3_1_source);
    amark_node_t *emph;
    amark_node_t *copy = NULL;
    const char *bytes = NULL;
    size_t length = 0;
    amark_content_state_t state = AMARK_CONTENT_NONE;
    bool own = true;
    amark_value_t value;

    AMARK_CHECK_TREE_BUILD(document, NULL, input_3_1_tree);
    emph = child_at(paragraph_of(amark_node_first_child(amark_document_root(document))), 1);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_copy(emph, &copy));
    NAOS_REQUIRE_NOT_NULL(copy);
    NAOS_CHECK_UINT_EQ(4, subtree_size(copy));
    NAOS_CHECK_UINT_EQ(2, amark_node_content_token_count(copy));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_content_base(copy, &bytes, &length, &state, &own));
    NAOS_CHECK_UINT_EQ(AMARK_CONTENT_LAID, state);
    NAOS_CHECK_FALSE(own);
    NAOS_CHECK_STR_EQ("one *two\nthree* four", bytes);
    memset(&value, 0, sizeof(value));
    value.struct_size = sizeof(value);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_get(amark_node_first_child(copy), AMARK_TEXT_LITERAL, &value));
    NAOS_CHECK_STR_EQ("two", value.text);
    check_no_cache(copy);
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

/* ---- Edits of one copy do not change the other (5.6, 6.3) ----------------------------- */

NAOS_TEST(discarding_the_spelling_of_one_copy_keeps_the_other)
{
    amark_document_t *document = new_document(input_3_1_source);
    amark_node_t *quote;
    amark_node_t *copy = NULL;
    char *original;
    char *copied;
    size_t copies;

    AMARK_CHECK_TREE_BUILD(document, NULL, input_3_1_tree);
    quote = amark_node_first_child(amark_document_root(document));
    original = fingerprint(quote);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_copy(quote, &copy));
    copies = document->stats.content_copies;

    /* Discard on the copy: the original keeps everything. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_discard_spelling(paragraph_of(copy)));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_discard_spelling(child_at(paragraph_of(copy), 1)));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_discard_spelling(copy));
    NAOS_CHECK_UINT_EQ(AMARK_CONTENT_FREE, amark_block_content_state(paragraph_of(copy)));
    NAOS_CHECK_UINT_EQ(AMARK_CONTENT_LAID, amark_block_content_state(paragraph_of(quote)));
    CHECK_FINGERPRINT(original, quote);
    CHECK_TREE_OK(document);
    amark_node_destroy(copy);

    /* Discard on the original: the copy keeps everything. */
    copy = NULL;
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_copy(quote, &copy));
    copied = fingerprint(copy);
    NAOS_CHECK_STR_EQ(original, copied);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_discard_spelling(paragraph_of(quote)));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_discard_spelling(amark_node_first_child(paragraph_of(quote))));
    CHECK_FINGERPRINT(copied, copy);
    /* Dropping the spelling makes new free content; nothing is copied on write. */
    NAOS_CHECK_UINT_EQ(copies, document->stats.content_copies);
    CHECK_TREE_OK(document);
    free(original);
    free(copied);
    amark_document_destroy(document);
}

/* "a *b*" on line 0 and "c" on line 1, with the content of line 0 only. */
static const char two_lines_source[] = "a *b*\nc\n";

static const char two_lines_tree[] =
    "(paragraph\n"
    "  :lines (0 2)\n"
    "  :content laid ((0 0 5))\n"
    "  (text :spelling ((text 0 2 :facet literal)) \"a \")\n"
    "  (emph :style (:delimiter \"*\")\n"
    "        :spelling ((delimiter 2 3 :role 1) (delimiter 4 5 :role 2))\n"
    "    (text :spelling ((text 3 4 :facet literal)) \"b\")))\n";

NAOS_TEST(adding_content_to_one_copy_keeps_the_other)
{
    amark_document_t *document = new_document(two_lines_source);
    amark_node_t *paragraph;
    amark_node_t *copy = NULL;
    amark_tree_content_t *shared;
    char *original;
    bool verbatim = false;

    AMARK_CHECK_TREE_BUILD(document, NULL, two_lines_tree);
    paragraph = amark_node_first_child(amark_document_root(document));
    shared = paragraph->content;
    original = fingerprint(paragraph);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_copy(paragraph, &copy));
    NAOS_CHECK_UINT_EQ(0, document->stats.content_copies);

    /* Shared by two owners: the piece goes to a copy of the content. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(copy, 1, 0, 1));
    NAOS_CHECK_UINT_EQ(1, document->stats.content_copies);
    NAOS_CHECK_TRUE(copy->content != shared);
    NAOS_CHECK_PTR_EQ(shared, paragraph->content);
    NAOS_CHECK_UINT_EQ(1, shared->owners);
    NAOS_CHECK_UINT_EQ(3, shared->bases);
    /* The tokens of the copy and its descendants moved to the new content. */
    NAOS_CHECK_PTR_EQ(copy->content, amark_node_first_child(copy)->content_tokens->base);
    NAOS_CHECK_PTR_EQ(copy->content,
                      amark_node_first_child(child_at(copy, 1))->content_tokens->base);
    CHECK_FINGERPRINT(original, paragraph);
    {
        const char *bytes = NULL;
        size_t length = 0;

        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_content_bytes(copy, &bytes, &length));
        NAOS_CHECK_STR_EQ("a *b*\nc", bytes);
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                          amark_block_content_bytes(paragraph, &bytes, &length));
        NAOS_CHECK_STR_EQ("a *b*", bytes);
    }
    /* The copy is not verbatim (its new piece has no token), the original is. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_content_verbatim(copy, &verbatim));
    NAOS_CHECK_FALSE(verbatim);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_content_verbatim(paragraph, &verbatim));
    NAOS_CHECK_TRUE(verbatim);
    CHECK_TREE_OK(document);

    /* No longer shared by owners: neither leaf copies again (tokens of other
     * nodes do not prevent an addition at the end). */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_add_content(paragraph, 1, 0, 1));
    NAOS_CHECK_UINT_EQ(1, document->stats.content_copies);
    NAOS_CHECK_PTR_EQ(shared, paragraph->content);
    CHECK_TREE_OK(document);
    free(original);
    amark_document_destroy(document);
}

/* Three lines of a paragraph without tokens. */
static const char three_lines_source[] = "a\nb\nc\n";

static const char three_lines_tree[] =
    "(paragraph :lines (0 3) :content laid ((0 0 1) (1 0 1) (2 0 1)))\n";

NAOS_TEST(removing_lines_copies_content_shared_by_owners_or_with_bases)
{
    amark_document_t *document = new_document(three_lines_source);
    amark_node_t *root = amark_document_root(document);
    amark_node_t *paragraph;
    amark_node_t *copy = NULL;
    amark_node_t *text = NULL;
    amark_node_t *text_copy = NULL;
    amark_tree_content_t *content;
    amark_content_token_t token;
    char *original;
    char *copied;
    const char *bytes = NULL;
    size_t length = 0;

    AMARK_CHECK_TREE_BUILD(document, NULL, three_lines_tree);
    paragraph = amark_node_first_child(root);

    /* Two owners: removing a line of one copy copies its content first. */
    original = fingerprint(paragraph);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_copy(paragraph, &copy));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_remove_lines(copy, 2, 1));
    NAOS_CHECK_UINT_EQ(1, document->stats.content_copies);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_content_bytes(copy, &bytes, &length));
    NAOS_CHECK_STR_EQ("a\nb", bytes);
    CHECK_FINGERPRINT(original, paragraph);
    free(original);
    CHECK_TREE_OK(document);

    /* One owner and no base: no copy. */
    content = copy->content;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_remove_lines(copy, 1, 1));
    NAOS_CHECK_UINT_EQ(1, document->stats.content_copies);
    NAOS_CHECK_PTR_EQ(content, copy->content);
    amark_node_destroy(copy);

    /* One owner and a base: the copy of an inline node keeps its tokens on
     * the content of the paragraph after its original is gone. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, AMARK_NODE_TEXT, &text));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(paragraph, text));
    memset(&token, 0, sizeof(token));
    token.struct_size = sizeof(token);
    token.start = 2;
    token.end = 3;
    token.kind = AMARK_SPELL_TEXT;
    token.facet = AMARK_TEXT_LITERAL;
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_add_content_token(text, paragraph, &token));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_copy(text, &text_copy));
    amark_node_destroy(text);
    content = paragraph->content;
    NAOS_CHECK_UINT_EQ(1, content->owners);
    NAOS_CHECK_UINT_EQ(1, content->bases);
    copied = fingerprint(text_copy);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_remove_lines(paragraph, 0, 1));
    NAOS_CHECK_UINT_EQ(2, document->stats.content_copies);
    NAOS_CHECK_TRUE(paragraph->content != content);
    NAOS_CHECK_PTR_EQ(content, text_copy->content_tokens->base);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_content_bytes(paragraph, &bytes, &length));
    NAOS_CHECK_STR_EQ("b\nc", bytes);
    CHECK_FINGERPRINT(copied, text_copy);
    free(copied);
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_TEST(trimming_and_moving_lines_copy_shared_content)
{
    amark_document_t *document = new_document("- [ ] a\n  b\n");
    amark_node_t *paragraph;
    amark_node_t *copy = NULL;
    amark_node_t *target = NULL;
    char *original;
    const char *bytes = NULL;
    size_t length = 0;

    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(paragraph :lines (0 2) :content laid ((0 2 7) (1 2 3)))\n");
    paragraph = amark_node_first_child(amark_document_root(document));
    original = fingerprint(paragraph);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_copy(paragraph, &copy));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_block_trim_content_start(copy, 4, AMARK_SPELL_MARKER,
                                                     AMARK_FACET_NONE, 0));
    NAOS_CHECK_UINT_EQ(1, document->stats.content_copies);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_content_bytes(copy, &bytes, &length));
    NAOS_CHECK_STR_EQ("a\nb", bytes);
    CHECK_FINGERPRINT(original, paragraph);
    amark_node_destroy(copy);

    copy = NULL;
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_copy(paragraph, &copy));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(document, AMARK_NODE_PARAGRAPH, &target));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_move_lines(copy, target, 0, 1));
    NAOS_CHECK_UINT_EQ(2, document->stats.content_copies);
    CHECK_FINGERPRINT(original, paragraph);
    free(original);
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

/* ---- Definitions (9.2) ----------------------------------------------------------------- */

/* The definition found for label (NULL when there is none; failed_lookup when
 * the lookup fails). */
static amark_node_t *const failed_lookup = (amark_node_t *)&failed_lookup;

static amark_node_t *find(amark_document_t *document, const char *label)
{
    amark_node_t *found = NULL;

    if (amark_document_find_definition(document, label, strlen(label), &found) !=
        AMARK_ERROR_NONE)
    {
        return failed_lookup;
    }
    return found;
}

NAOS_TEST(copied_definitions_are_found_once_attached)
{
    amark_document_t *document = new_document("");
    amark_node_t *root = amark_document_root(document);
    amark_node_t *definition;
    amark_node_t *quote;
    amark_node_t *copy = NULL;
    amark_node_t *quote_copy = NULL;

    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(link_definition :label \"Foo\" :destination \"/a\" :title \"\")\n"
                           "(block_quote\n"
                           "  (link_definition :label \"bar\" :destination \"/b\" :title \"\"))\n");
    definition = amark_node_first_child(root);
    quote = amark_node_next(definition);
    NAOS_CHECK_PTR_EQ(definition, find(document, "foo"));
    NAOS_CHECK_UINT_EQ(0, document->def_index.dirty_count);

    /* The copies are on the dirty list; detached, they are not found. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_copy(definition, &copy));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_copy(quote, &quote_copy));
    NAOS_CHECK_UINT_EQ(2, document->def_index.dirty_count);
    NAOS_CHECK_TRUE(copy->def_dirty);
    NAOS_CHECK_TRUE(amark_node_first_child(quote_copy)->def_dirty);
    CHECK_TREE_OK(document);
    NAOS_CHECK_PTR_EQ(definition, find(document, "FOO"));
    NAOS_CHECK_PTR_EQ(amark_node_first_child(quote), find(document, "bar"));
    CHECK_TREE_OK(document);

    /* Attached first in document order, the copies win. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_prepend_child(root, copy));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_prepend_child(root, quote_copy));
    NAOS_CHECK_PTR_EQ(copy, find(document, "foo"));
    NAOS_CHECK_PTR_EQ(amark_node_first_child(quote_copy), find(document, "bar"));

    /* After the originals are gone, the copies are still found. */
    amark_node_destroy(quote);
    amark_node_destroy(definition);
    NAOS_CHECK_PTR_EQ(copy, find(document, "foo"));
    NAOS_CHECK_PTR_EQ(amark_node_first_child(quote_copy), find(document, "bar"));
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

/* ---- Extension data (12.13) ------------------------------------------------------------ */

typedef struct note_data
{
    int value;
    int *owned;
} note_data_t;

static int note_inits;
static int note_deinits;
static int note_copies;
static int note_fail_value;

static amark_error_t note_init(amark_node_t *node, void *data)
{
    note_data_t *note = (note_data_t *)data;

    (void)node;
    note->value = 7;
    note->owned = (int *)amark_malloc(sizeof(int));
    if (note->owned == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    *note->owned = 0;
    note_inits++;
    return AMARK_ERROR_NONE;
}

static void note_deinit(amark_node_t *node, void *data)
{
    note_data_t *note = (note_data_t *)data;

    (void)node;
    amark_free(note->owned);
    note_deinits++;
}

static amark_error_t note_copy(amark_node_t *dst, void *dst_data, const amark_node_t *src,
                               const void *src_data)
{
    note_data_t *to = (note_data_t *)dst_data;
    const note_data_t *from = (const note_data_t *)src_data;

    /* The data is zero-filled and the node is of the type of src. */
    if ((to->value != 0) || (to->owned != NULL) ||
        (amark_node_get_type(dst) != amark_node_get_type(src)) ||
        (amark_node_data(dst) != dst_data))
    {
        return AMARK_ERROR_INVALID_STATE;
    }
    if (from->value == note_fail_value)
    {
        return AMARK_ERROR_INVALID_STATE;
    }
    to->owned = (int *)amark_malloc(sizeof(int));
    if (to->owned == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    *to->owned = *from->owned + 1;
    to->value = from->value;
    note_copies++;
    return AMARK_ERROR_NONE;
}

static const amark_node_type_desc_t note_type = {
    sizeof(amark_node_type_desc_t), "test.note", AMARK_NODE_LEVEL_BLOCK, AMARK_CONTENT_BLOCK,
    AMARK_CONTENT_BLOCK, 0, NULL, 0, sizeof(note_data_t),
    NULL, note_init, note_deinit, note_copy, NULL, NULL, NULL};

/* A type without a copy function: its data is copied byte for byte. */
static const amark_node_type_desc_t plain_type = {
    sizeof(amark_node_type_desc_t), "test.plain", AMARK_NODE_LEVEL_BLOCK, AMARK_CONTENT_BLOCK,
    AMARK_CONTENT_BLOCK, 0, NULL, 0, 8, NULL, NULL, NULL, NULL, NULL, NULL, NULL};

static const amark_node_type_desc_t *const note_types[] = {&note_type, &plain_type};
static const amark_extension_t note_ext = {
    sizeof(amark_extension_t), "test.notes", note_types, 2, NULL, 0, NULL};

static amark_document_t *new_note_document(amark_node_type_t *note, amark_node_type_t *plain)
{
    amark_syntax_set_t *set = NULL;
    amark_document_t *document = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_create(&set));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_append(set, &note_ext));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_syntax_set_freeze(set));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(set, &document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_take_syntax_set(document, set));
    NAOS_REQUIRE_TRUE(amark_syntax_set_find_node_type(set, "test.note", note));
    NAOS_REQUIRE_TRUE(amark_syntax_set_find_node_type(set, "test.plain", plain));
    return document;
}

NAOS_TEST(extension_data_is_copied_by_the_copy_function)
{
    amark_node_type_t note = AMARK_NODE_NONE;
    amark_node_type_t plain = AMARK_NODE_NONE;
    amark_document_t *document = new_note_document(&note, &plain);
    amark_node_t *a = NULL;
    amark_node_t *b = NULL;
    amark_node_t *c = NULL;
    amark_node_t *copy = NULL;
    note_data_t *data;
    size_t nodes;

    note_inits = 0;
    note_deinits = 0;
    note_copies = 0;
    note_fail_value = -1;
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, note, &a));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, plain, &b));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, note, &c));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(a, b));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(b, c));
    ((note_data_t *)amark_node_data(a))->value = 1;
    *((note_data_t *)amark_node_data(a))->owned = 10;
    ((note_data_t *)amark_node_data(c))->value = 3;
    memcpy(amark_node_data(b), "abcdefgh", 8);
    NAOS_CHECK_INT_EQ(2, note_inits);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_copy(a, &copy));
    NAOS_REQUIRE_NOT_NULL(copy);
    NAOS_CHECK_INT_EQ(2, note_inits); /* init is not called for a copy */
    NAOS_CHECK_INT_EQ(2, note_copies);
    data = (note_data_t *)amark_node_data(copy);
    NAOS_CHECK_INT_EQ(1, data->value);
    NAOS_CHECK_INT_EQ(11, *data->owned);
    NAOS_CHECK_TRUE(data->owned != ((note_data_t *)amark_node_data(a))->owned);
    NAOS_CHECK_INT_EQ(0, memcmp("abcdefgh", amark_node_data(amark_node_first_child(copy)), 8));
    data = (note_data_t *)amark_node_data(amark_node_first_child(amark_node_first_child(copy)));
    NAOS_CHECK_INT_EQ(3, data->value);
    NAOS_CHECK_INT_EQ(1, *data->owned);
    CHECK_TREE_OK(document);

    /* A failing copy function: the error is returned, the nodes already copied
     * are released (deinit) and the document is unchanged. */
    amark_node_destroy(copy);
    NAOS_CHECK_INT_EQ(2, note_deinits);
    nodes = document->node_count;
    note_fail_value = 3;
    copy = a;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_STATE, amark_node_copy(a, &copy));
    NAOS_CHECK_NULL(copy);
    NAOS_CHECK_INT_EQ(3, note_copies);
    NAOS_CHECK_INT_EQ(3, note_deinits);
    NAOS_CHECK_UINT_EQ(nodes, document->node_count);
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
    NAOS_CHECK_INT_EQ(5, note_deinits);
}

/* ---- Depth (5.10) ---------------------------------------------------------------------- */

NAOS_TEST(a_deep_subtree_is_copied_without_recursion)
{
    enum { DEPTH = 100000 };
    amark_document_t *document = new_document("");
    amark_node_t *inner = NULL;
    amark_node_t *copy = NULL;
    amark_node_t *node;
    size_t i;
    size_t depth = 0;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(document, AMARK_NODE_PARAGRAPH, &inner));
    for (i = 1; i < DEPTH; i++)
    {
        amark_node_t *outer = NULL;

        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                            amark_node_create(document, AMARK_NODE_BLOCK_QUOTE, &outer));
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(outer, inner));
        inner = outer;
    }
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_copy(inner, &copy));
    NAOS_REQUIRE_NOT_NULL(copy);
    for (node = copy; node != NULL; node = amark_node_first_child(node))
    {
        depth++;
        if (amark_node_first_child(node) == NULL)
        {
            NAOS_CHECK_UINT_EQ(AMARK_NODE_PARAGRAPH, amark_node_get_type(node));
        }
    }
    NAOS_CHECK_UINT_EQ(DEPTH, depth);
    NAOS_CHECK_UINT_EQ(2 * DEPTH + 1, document->node_count);
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

/* ---- Out of memory --------------------------------------------------------------------- */

typedef struct oom_context
{
    amark_document_t *document;
    amark_node_t *quote;
    amark_node_t *definition;
} oom_context_t;

static amark_error_t copy_oom_scenario(void *context)
{
    oom_context_t *c = (oom_context_t *)context;
    amark_document_t *document = c->document;
    amark_tree_content_t *content = paragraph_of(c->quote)->content;
    size_t nodes = document->node_count;
    size_t owners = content->owners;
    size_t bases = content->bases;
    size_t dirty = document->def_index.dirty_count;
    amark_node_t *quote_copy = c->quote;
    amark_node_t *definition_copy = NULL;
    amark_error_t error;

    error = amark_node_copy(c->quote, &quote_copy);
    if (error == AMARK_ERROR_NONE)
    {
        error = amark_node_copy(c->definition, &definition_copy);
    }
    else if (quote_copy != NULL)
    {
        return AMARK_ERROR_INVALID_STATE;
    }
    if (amark_tree_check(document, NULL) != AMARK_TREE_VIOLATION_NONE)
    {
        error = AMARK_ERROR_INVALID_STATE;
    }
    if (error == AMARK_ERROR_NONE)
    {
        if ((content->owners != owners + 1) || (content->bases != bases + 6) ||
            (document->def_index.dirty_count != dirty + 1))
        {
            error = AMARK_ERROR_INVALID_STATE;
        }
        amark_node_destroy(definition_copy);
    }
    amark_node_destroy(quote_copy);
    /* The document is as it was. */
    if ((document->node_count != nodes) || (content->owners != owners) ||
        (content->bases != bases) || (document->def_index.dirty_count != dirty) ||
        (amark_tree_check(document, NULL) != AMARK_TREE_VIOLATION_NONE))
    {
        return AMARK_ERROR_INVALID_STATE;
    }
    return error;
}

NAOS_TEST(out_of_memory_leaks_nothing_and_leaves_the_document_unchanged)
{
    amark_document_t *document = new_document(input_3_1_source);
    oom_context_t context;
    char *before;

    AMARK_CHECK_TREE_BUILD(document, NULL, input_3_1_tree);
    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(link_definition :label \"foo\" :destination \"/a\" :title \"t\")\n");
    context.document = document;
    context.quote = amark_node_first_child(amark_document_root(document));
    context.definition = amark_node_next(context.quote);
    NAOS_REQUIRE_PTR_EQ(context.definition, find(document, "foo"));
    before = fingerprint(amark_document_root(document));
    AMARK_CHECK_OOM_SWEEP(copy_oom_scenario, &context);
    CHECK_FINGERPRINT(before, amark_document_root(document));
    free(before);
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

NAOS_SUITE(copy_import,
    NAOS_ADD_TEST(invalid_arguments_change_nothing),
    NAOS_ADD_TEST(the_copy_reproduces_the_subtree_detached_with_shared_content),
    NAOS_ADD_TEST(an_inline_node_is_copied_with_its_tokens_on_the_same_base),
    NAOS_ADD_TEST(discarding_the_spelling_of_one_copy_keeps_the_other),
    NAOS_ADD_TEST(adding_content_to_one_copy_keeps_the_other),
    NAOS_ADD_TEST(removing_lines_copies_content_shared_by_owners_or_with_bases),
    NAOS_ADD_TEST(trimming_and_moving_lines_copy_shared_content),
    NAOS_ADD_TEST(copied_definitions_are_found_once_attached),
    NAOS_ADD_TEST(extension_data_is_copied_by_the_copy_function),
    NAOS_ADD_TEST(a_deep_subtree_is_copied_without_recursion),
    NAOS_ADD_TEST(out_of_memory_leaks_nothing_and_leaves_the_document_unchanged)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(copy_import)
)
