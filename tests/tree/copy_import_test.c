/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The tests of CopyImport (syntax-tree): the copy within the same document
 * (5.6), with the content objects shared and copied on write (Content, 6.3),
 * the copied definitions in the definition index, and the extension data
 * copied by the copy function of the type (12.13), the import from another
 * document and of the children of a node (5.7–5.9).
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

/* ---- The import (5.7–5.9) -------------------------------------------------------------- */

/* Checks line of document: its source, number, length and line ending. */
static void check_line(const amark_document_t *document, amark_line_id_t line,
                       amark_source_id_t source, uint32_t number, size_t length, amark_eol_t eol)
{
    amark_line_info_t info;

    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_line(document, line, &info));
    NAOS_CHECK_UINT_EQ(source, info.source);
    NAOS_CHECK_UINT_EQ(number, info.number);
    NAOS_CHECK_UINT_EQ(length, info.length);
    NAOS_CHECK_INT_EQ(eol, info.eol);
}

/* Checks that the bytes of source of document are expected (of length bytes). */
static void check_source(const amark_document_t *document, amark_source_id_t source,
                         const char *expected, size_t expected_length)
{
    const char *data = NULL;
    size_t length = 0;
    bool has_bom = true;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_source_data(document, source, &data, &length, &has_bom));
    NAOS_CHECK_FALSE(has_bom);
    NAOS_REQUIRE_UINT_EQ(expected_length, length);
    NAOS_CHECK_INT_EQ(0, memcmp(expected, data, length));
}

NAOS_TEST(the_import_reproduces_the_subtree_with_its_lines)
{
    amark_document_t *source = new_document(input_3_1_source);
    amark_document_t *target = NULL;
    amark_node_t *quote;
    amark_node_t *imported = NULL;
    amark_node_t *paragraph;
    amark_tree_content_t *content;
    char *expected;
    size_t nodes;
    bool verbatim = false;

    AMARK_CHECK_TREE_BUILD(source, NULL, input_3_1_tree);
    quote = amark_node_first_child(amark_document_root(source));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_verbatim(quote, &verbatim));
    NAOS_CHECK_TRUE(verbatim);
    expected = fingerprint(quote);
    nodes = source->node_count;
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &target));

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_import(target, quote, &imported));
    NAOS_REQUIRE_NOT_NULL(imported);
    NAOS_CHECK_PTR_EQ(target, amark_node_document(imported));
    NAOS_CHECK_NULL(amark_node_parent(imported));
    NAOS_CHECK_UINT_EQ(1 + subtree_size(quote), target->node_count);
    NAOS_CHECK_UINT_EQ(nodes, source->node_count);
    check_no_cache(imported);

    /* The lines are in one new finished source of the target. */
    NAOS_CHECK_UINT_EQ(1, amark_document_source_count(target));
    NAOS_CHECK_UINT_EQ(2, amark_document_line_count(target));
    check_source(target, 0, input_3_1_source, strlen(input_3_1_source));
    check_line(target, 0, 0, 1, 12, AMARK_EOL_LF);
    check_line(target, 1, 0, 2, 15, AMARK_EOL_LF);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_STATE,
                      amark_document_append_source(target, 0, "x", 1));

    /* The content is copied once: a new object, owned by the imported
     * paragraph, the base of its six nodes with tokens. */
    paragraph = paragraph_of(imported);
    content = paragraph->content;
    NAOS_REQUIRE_NOT_NULL(content);
    NAOS_CHECK_TRUE(content != paragraph_of(quote)->content);
    NAOS_CHECK_UINT_EQ(1, content->owners);
    NAOS_CHECK_UINT_EQ(6, content->bases);
    NAOS_CHECK_PTR_EQ(content, amark_node_first_child(paragraph)->content_tokens->base);
    NAOS_CHECK_UINT_EQ(1, paragraph_of(quote)->content->owners);
    NAOS_CHECK_UINT_EQ(6, paragraph_of(quote)->content->bases);

    /* With the same lines at the same identifiers, everything reads the same:
     * values, spelling, judgment and positions. */
    CHECK_FINGERPRINT(expected, imported);
    CHECK_FINGERPRINT(expected, quote);
    CHECK_TREE_OK(target);
    CHECK_TREE_OK(source);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_append_child(amark_document_root(target), imported));
    CHECK_FINGERPRINT(expected, imported);
    free(expected);
    CHECK_TREE_OK(target);

    /* The two documents are independent. */
    amark_document_destroy(source);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_verbatim(imported, &verbatim));
    NAOS_CHECK_TRUE(verbatim);
    CHECK_TREE_OK(target);
    amark_document_destroy(target);
}

/* The lines of the line ending test: three sources, so that a CR line is
 * followed by an empty LF line and a line without a line ending by another
 * line, which dividing the joined bytes again would not give back. */
static amark_document_t *new_eol_document(void)
{
    amark_document_t *document = NULL;
    amark_source_id_t id = 0;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_add_source(
                                              document, "a\r", 2, AMARK_SOURCE_FINISHED, &id));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_add_source(
                                              document, "\nb", 2, AMARK_SOURCE_FINISHED, &id));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_add_source(
                                              document, "c", 1, AMARK_SOURCE_FINISHED, &id));
    NAOS_REQUIRE_UINT_EQ(4, amark_document_line_count(document));
    AMARK_CHECK_TREE_BUILD(document, NULL,
                           "(block_quote\n"
                           "  :lines (0 4)\n"
                           "  :blanks ((1 0 0))\n"
                           "  (paragraph :lines (0 1) :content laid ((0 0 1)))\n"
                           "  (paragraph :lines (2 1) :content laid ((2 0 1)))\n"
                           "  (paragraph :lines (3 1) :content laid ((3 0 1))))\n");
    return document;
}

NAOS_TEST(the_import_keeps_the_line_endings_of_each_line)
{
    amark_document_t *source = new_eol_document();
    amark_document_t *target = new_document("x\n");
    amark_node_t *quote = amark_node_first_child(amark_document_root(source));
    amark_node_t *imported = NULL;
    amark_node_t *child;
    amark_line_id_t first = 0;
    uint32_t count = 0;
    amark_content_piece_t piece;
    const char *bytes = NULL;
    size_t length = 0;

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_import(target, quote, &imported));
    NAOS_REQUIRE_NOT_NULL(imported);

    /* Four lines, renumbered after the line of the target, with their line
     * endings: the joined bytes "a\r\nbc" are not divided again. */
    NAOS_CHECK_UINT_EQ(2, amark_document_source_count(target));
    NAOS_CHECK_UINT_EQ(5, amark_document_line_count(target));
    check_source(target, 1, "a\r\nbc", 5);
    check_line(target, 0, 0, 1, 1, AMARK_EOL_LF);
    check_line(target, 1, 1, 1, 1, AMARK_EOL_CR);
    check_line(target, 2, 1, 2, 0, AMARK_EOL_LF);
    check_line(target, 3, 1, 3, 1, AMARK_EOL_NONE);
    check_line(target, 4, 1, 4, 1, AMARK_EOL_NONE);
    NAOS_CHECK_INT_EQ(AMARK_EOL_LF, amark_document_default_eol(target));

    /* The records follow the lines. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_lines(imported, &first, &count));
    NAOS_CHECK_UINT_EQ(1, first);
    NAOS_CHECK_UINT_EQ(4, count);
    NAOS_REQUIRE_UINT_EQ(1, amark_block_blank_line_count(imported));
    child = child_at(imported, 2);
    NAOS_REQUIRE_NOT_NULL(child);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_lines(child, &first, &count));
    NAOS_CHECK_UINT_EQ(4, first);
    piece.struct_size = sizeof(piece);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_content_piece_at(child, 0, &piece));
    NAOS_CHECK_UINT_EQ(4, piece.line);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_content_bytes(child, &bytes, &length));
    NAOS_CHECK_STR_EQ("c", bytes);
    CHECK_TREE_OK(target);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_append_child(amark_document_root(target), imported));
    CHECK_TREE_OK(target);

    /* The children only: the lines they refer to (not the blank line of the
     * quote) in a third source, in order. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_import_children(imported, NULL, quote));
    NAOS_CHECK_UINT_EQ(3, amark_document_source_count(target));
    NAOS_CHECK_UINT_EQ(8, amark_document_line_count(target));
    check_source(target, 2, "a\rbc", 4);
    check_line(target, 5, 2, 1, 1, AMARK_EOL_CR);
    check_line(target, 6, 2, 2, 1, AMARK_EOL_NONE);
    check_line(target, 7, 2, 3, 1, AMARK_EOL_NONE);
    NAOS_CHECK_UINT_EQ(6, subtree_size(imported) - 1);
    child = child_at(imported, 4);
    NAOS_REQUIRE_NOT_NULL(child);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_block_lines(child, &first, &count));
    NAOS_CHECK_UINT_EQ(6, first);
    CHECK_TREE_OK(target);
    CHECK_TREE_OK(source);
    amark_document_destroy(source);
    amark_document_destroy(target);
}

/* The counts of target that a refused import must keep. */
typedef struct target_state
{
    size_t nodes;
    size_t sources;
    size_t lines;
    size_t dirty;
    uint64_t generation;
} target_state_t;

static target_state_t state_of(const amark_document_t *document)
{
    target_state_t state;

    state.nodes = document->node_count;
    state.sources = document->source_count;
    state.lines = document->line_count;
    state.dirty = document->def_index.dirty_count;
    state.generation = document->structure_generation;
    return state;
}

static bool same_state(const amark_document_t *document, target_state_t state)
{
    target_state_t now = state_of(document);

    return (now.nodes == state.nodes) && (now.sources == state.sources) &&
           (now.lines == state.lines) && (now.dirty == state.dirty) &&
           (now.generation == state.generation);
}

NAOS_TEST(the_children_are_imported_in_order_at_once)
{
    amark_document_t *source = new_document(input_3_1_source);
    amark_document_t *target = new_document("");
    amark_node_t *root;
    amark_node_t *quote;
    amark_node_t *copy = NULL;
    amark_node_t *before = NULL;
    amark_node_t *first;
    amark_node_t *second;
    amark_tree_content_t *content;
    target_state_t state;
    uint64_t generation;

    AMARK_CHECK_TREE_BUILD(source, NULL, input_3_1_tree);
    quote = amark_node_first_child(amark_document_root(source));
    /* Two quotes sharing one content object. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_copy(quote, &copy));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_append_child(amark_document_root(source), copy));
    NAOS_REQUIRE_UINT_EQ(2, paragraph_of(quote)->content->owners);

    root = amark_document_root(target);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(target, AMARK_NODE_PARAGRAPH, &before));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(root, before));
    generation = target->structure_generation;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_import_children(root, before, amark_document_root(source)));
    /* One insertion: one structural change. */
    NAOS_CHECK_UINT_EQ(generation + 1, target->structure_generation);
    first = child_at(root, 0);
    second = child_at(root, 1);
    NAOS_REQUIRE_NOT_NULL(first);
    NAOS_REQUIRE_NOT_NULL(second);
    NAOS_CHECK_PTR_EQ(before, child_at(root, 2));
    NAOS_CHECK_UINT_EQ(AMARK_NODE_BLOCK_QUOTE, amark_node_get_type(first));
    NAOS_CHECK_UINT_EQ(AMARK_NODE_BLOCK_QUOTE, amark_node_get_type(second));

    /* The shared content object is copied once and stays shared. */
    content = paragraph_of(first)->content;
    NAOS_REQUIRE_NOT_NULL(content);
    NAOS_CHECK_PTR_EQ(content, paragraph_of(second)->content);
    NAOS_CHECK_TRUE(content != paragraph_of(quote)->content);
    NAOS_CHECK_UINT_EQ(2, content->owners);
    NAOS_CHECK_UINT_EQ(12, content->bases);
    /* The lines both quotes refer to are imported once. */
    NAOS_CHECK_UINT_EQ(2, amark_document_line_count(target));
    CHECK_TREE_OK(target);
    CHECK_TREE_OK(source);

    /* An edit of one import copies the shared content first. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_discard_spelling(paragraph_of(second)));
    NAOS_CHECK_UINT_EQ(1, content->owners);
    CHECK_TREE_OK(target);

    /* Without children nothing is imported. */
    state = state_of(target);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_import_children(root, NULL, before));
    NAOS_CHECK_TRUE(same_state(target, state));
    amark_document_destroy(source);
    amark_document_destroy(target);
}

NAOS_TEST(imported_definitions_are_found_once_attached)
{
    amark_document_t *source = new_document("");
    amark_document_t *target = new_document("");
    amark_node_t *root = amark_document_root(target);
    amark_node_t *imported = NULL;

    AMARK_CHECK_TREE_BUILD(source, NULL,
                           "(link_definition :label \"Foo\" :destination \"/a\" :title \"\")\n"
                           "(block_quote\n"
                           "  (link_definition :label \"bar\" :destination \"/b\" :title \"\"))\n");
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_import(target, amark_node_first_child(amark_document_root(source)),
                                        &imported));
    NAOS_REQUIRE_NOT_NULL(imported);
    NAOS_CHECK_TRUE(imported->def_dirty);
    NAOS_CHECK_UINT_EQ(1, target->def_index.dirty_count);
    NAOS_CHECK_NULL(find(target, "foo"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(root, imported));
    NAOS_CHECK_PTR_EQ(imported, find(target, "FOO"));
    NAOS_CHECK_NULL(find(target, "bar"));

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_import_children(root, imported, amark_document_root(source)));
    NAOS_CHECK_UINT_EQ(2, target->def_index.dirty_count);
    /* The first in document order is the imported child now. */
    NAOS_CHECK_PTR_EQ(amark_node_first_child(root), find(target, "foo"));
    NAOS_CHECK_PTR_EQ(amark_node_first_child(child_at(root, 1)), find(target, "bar"));
    CHECK_TREE_OK(target);
    CHECK_TREE_OK(source);
    amark_document_destroy(source);
    amark_document_destroy(target);
}

NAOS_TEST(refused_imports_change_nothing)
{
    amark_node_type_t note = AMARK_NODE_NONE;
    amark_node_type_t plain = AMARK_NODE_NONE;
    amark_document_t *notes = new_note_document(&note, &plain);
    amark_document_t *source = new_document("a [b](c) d\n");
    amark_document_t *target = new_document("x\n");
    amark_node_t *root = amark_document_root(target);
    amark_node_t *note_node = NULL;
    amark_node_t *paragraph;
    amark_node_t *link = NULL;
    amark_node_t *other = NULL;
    amark_node_t *out;
    amark_document_t *other_notes;
    target_state_t state;
    target_state_t notes_state;
    char *before;

    note_fail_value = -1;
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(notes, note, &note_node));
    AMARK_CHECK_TREE_BUILD(source, NULL,
                           "(paragraph :lines (0 1) :content laid ((0 0 10))\n"
                           "  (text \"a \")\n"
                           "  (link :destination \"c\" :title \"\" (text \"b\"))\n"
                           "  (text \" d\"))\n");
    paragraph = amark_node_first_child(amark_document_root(source));
    AMARK_CHECK_TREE_BUILD(target, NULL,
                           "(paragraph (link :destination \"z\" :title \"\" (text \"y\")))\n");
    link = amark_node_first_child(amark_node_first_child(root));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(target, AMARK_NODE_PARAGRAPH, &other));
    before = fingerprint(paragraph);
    state = state_of(target);

    /* NULL arguments and the document node. */
    out = root;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_import(NULL, paragraph, &out));
    NAOS_CHECK_NULL(out);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_import(target, NULL, &out));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_import(target, paragraph, NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_import(target, amark_document_root(source), &out));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_import_children(NULL, NULL, paragraph));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_import_children(root, NULL, NULL));

    /* A type the target does not have, alone or in the subtree. */
    out = root;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_import(target, note_node, &out));
    NAOS_CHECK_NULL(out);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_append_child(amark_document_root(notes), note_node));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_import_children(root, NULL, amark_document_root(notes)));
    NAOS_CHECK_TRUE(same_state(target, state));

    /* Containment: inline children under the document node, a link under a
     * link. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_import_children(root, NULL, paragraph));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_import_children(link, NULL, paragraph));
    NAOS_CHECK_TRUE(same_state(target, state));

    /* The position: before must be a child of the target parent. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_import_children(amark_node_first_child(root), other, paragraph));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_import_children(amark_node_first_child(root), root, paragraph));
    NAOS_CHECK_TRUE(same_state(target, state));
    NAOS_CHECK_NULL(amark_node_next(link));

    /* A failing copy function of a type: nothing is left in the target. */
    other_notes = new_note_document(&note, &plain);
    notes_state = state_of(other_notes);
    note_fail_value = 7;
    out = root;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_STATE, amark_node_import(other_notes, note_node, &out));
    NAOS_CHECK_NULL(out);
    NAOS_CHECK_TRUE(same_state(other_notes, notes_state));
    note_fail_value = -1;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_import(other_notes, note_node, &out));
    NAOS_REQUIRE_NOT_NULL(out);
    NAOS_CHECK_INT_EQ(7, ((note_data_t *)amark_node_data(out))->value);
    NAOS_CHECK_INT_EQ(1, *((note_data_t *)amark_node_data(out))->owned);
    CHECK_TREE_OK(other_notes);
    amark_document_destroy(other_notes);

    CHECK_FINGERPRINT(before, paragraph);
    free(before);
    CHECK_TREE_OK(target);
    CHECK_TREE_OK(source);
    CHECK_TREE_OK(notes);

    /* The same inline children fit in a paragraph, after the link. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_import_children(amark_node_first_child(root), NULL, paragraph));
    NAOS_CHECK_UINT_EQ(7, subtree_size(amark_node_first_child(root)));
    CHECK_TREE_OK(target);
    amark_document_destroy(notes);
    amark_document_destroy(source);
    amark_document_destroy(target);
}

/* An import from a document into itself is a copy (its lines are already
 * there). */
NAOS_TEST(an_import_within_the_same_document_is_a_copy)
{
    amark_document_t *document = new_document(input_3_1_source);
    amark_node_t *root = amark_document_root(document);
    amark_node_t *quote;
    amark_node_t *imported = NULL;
    size_t lines;

    AMARK_CHECK_TREE_BUILD(document, NULL, input_3_1_tree);
    quote = amark_node_first_child(root);
    lines = amark_document_line_count(document);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_import(document, quote, &imported));
    NAOS_REQUIRE_NOT_NULL(imported);
    NAOS_CHECK_PTR_EQ(paragraph_of(quote)->content, paragraph_of(imported)->content);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_import_children(imported, NULL, quote));
    NAOS_CHECK_UINT_EQ(2, paragraph_of(quote)->content->owners - 1);
    NAOS_CHECK_UINT_EQ(lines, amark_document_line_count(document));
    NAOS_CHECK_UINT_EQ(1, amark_document_source_count(document));
    CHECK_TREE_OK(document);
    amark_document_destroy(document);
}

/* A deep subtree is imported without recursion (5.10). */
NAOS_TEST(a_deep_subtree_is_imported_without_recursion)
{
    enum { DEPTH = 100000 };
    amark_document_t *source = new_document("");
    amark_document_t *target = new_document("");
    amark_node_t *inner = NULL;
    amark_node_t *imported = NULL;
    size_t i;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(source, AMARK_NODE_PARAGRAPH, &inner));
    for (i = 1; i < DEPTH; i++)
    {
        amark_node_t *outer = NULL;

        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                            amark_node_create(source, AMARK_NODE_BLOCK_QUOTE, &outer));
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(outer, inner));
        inner = outer;
    }
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_import(target, inner, &imported));
    NAOS_REQUIRE_NOT_NULL(imported);
    NAOS_CHECK_UINT_EQ(DEPTH + 1, target->node_count);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_append_child(amark_document_root(target), imported));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_import_children(imported, NULL, amark_node_first_child(imported)));
    NAOS_CHECK_UINT_EQ(2 * DEPTH - 1, target->node_count);
    CHECK_TREE_OK(target);
    amark_document_destroy(source);
    amark_document_destroy(target);
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


/* The import under out of memory: each refused import leaves the target as it
 * was (no node, source, line or dirty definition added), and the source
 * document is never changed. */
static amark_error_t import_oom_scenario(void *context)
{
    oom_context_t *c = (oom_context_t *)context;
    amark_document_t *source = c->document;
    amark_tree_content_t *content = paragraph_of(c->quote)->content;
    target_state_t source_state = state_of(source);
    size_t owners = content->owners;
    size_t bases = content->bases;
    amark_document_t *target = NULL;
    amark_node_t *imported = c->quote;
    amark_source_id_t id = 0;
    target_state_t state;
    amark_error_t error;

    error = amark_document_create(NULL, &target);
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    error = amark_document_add_source(target, "x\n", 2, AMARK_SOURCE_FINISHED, &id);
    if (error == AMARK_ERROR_NONE)
    {
        state = state_of(target);
        error = amark_node_import(target, c->quote, &imported);
        if (error != AMARK_ERROR_NONE)
        {
            if ((imported != NULL) || !same_state(target, state))
            {
                error = AMARK_ERROR_INVALID_STATE;
            }
        }
        else
        {
            error = amark_node_append_child(amark_document_root(target), imported);
        }
        if (error == AMARK_ERROR_NONE)
        {
            state = state_of(target);
            error = amark_node_import_children(amark_document_root(target), imported,
                                               amark_document_root(source));
            if ((error != AMARK_ERROR_NONE) && !same_state(target, state))
            {
                error = AMARK_ERROR_INVALID_STATE;
            }
        }
        if ((error == AMARK_ERROR_NONE) &&
            ((amark_document_line_count(target) != 5) ||
             (amark_node_first_child(amark_document_root(target)) == imported) ||
             (target->def_index.dirty_count != 1)))
        {
            error = AMARK_ERROR_INVALID_STATE;
        }
        if (amark_tree_check(target, NULL) != AMARK_TREE_VIOLATION_NONE)
        {
            error = AMARK_ERROR_INVALID_STATE;
        }
    }
    amark_document_destroy(target);
    if (!same_state(source, source_state) || (content->owners != owners) ||
        (content->bases != bases) ||
        (amark_tree_check(source, NULL) != AMARK_TREE_VIOLATION_NONE))
    {
        return AMARK_ERROR_INVALID_STATE;
    }
    return error;
}

NAOS_TEST(out_of_memory_in_an_import_leaves_both_documents_unchanged)
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
    AMARK_CHECK_OOM_SWEEP(import_oom_scenario, &context);
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
    NAOS_ADD_TEST(out_of_memory_leaks_nothing_and_leaves_the_document_unchanged),
    NAOS_ADD_TEST(the_import_reproduces_the_subtree_with_its_lines),
    NAOS_ADD_TEST(the_import_keeps_the_line_endings_of_each_line),
    NAOS_ADD_TEST(the_children_are_imported_in_order_at_once),
    NAOS_ADD_TEST(imported_definitions_are_found_once_attached),
    NAOS_ADD_TEST(refused_imports_change_nothing),
    NAOS_ADD_TEST(an_import_within_the_same_document_is_a_copy),
    NAOS_ADD_TEST(a_deep_subtree_is_imported_without_recursion),
    NAOS_ADD_TEST(out_of_memory_in_an_import_leaves_both_documents_unchanged)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(copy_import)
)
