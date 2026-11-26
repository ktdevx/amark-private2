/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * Syntax tokens (src/tree/token.c), through the public API; the internal
 * amark_tree_check checks the invariants after the operations and the internal
 * counter stat_token_moves shows the cost of the sorted insertions.
 *
 * Suite token_check (6.2, 6.3, 6.4, 6.5, 6.6, 6.8): a valid token of each kind is
 * kept with all its fields; each invalid case of each kind, the unknown kinds and
 * flags, an unregistered source, a range out of the source (an overflow included),
 * a small struct_size, tab fields without the partial tab flag and NULL arguments
 * are AMARK_ERROR_INVALID_ARGUMENT and leave the tokens unchanged; LF, CRLF and CR
 * stay distinct; the container prefix flag and the role are kept; a zero-length
 * lazy continuation; one tab split into two partial tab tokens and the invalid
 * partial tabs.
 * Suite token_store (6.1, 6.7, 10.5): the tokens come back sorted by (source,
 * offset, tab_column_offset, order of addition); adding in source order moves no
 * token; removal; the transfer between nodes merges into the order of the target,
 * across sources, and each rejection (another document, a range out of the
 * tokens, NULL) changes neither node; token_at and struct_size; the kind names;
 * no edit is recorded; adding and transferring pass the out-of-memory sweep with
 * the nodes unchanged on each failure.
 */
#include "tree/tree.h"

#include <amark/allocator.h>
#include <amark/document.h>
#include <amark/error.h>
#include <amark/node.h>
#include <amark/token.h>

#include <limits.h>
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

/* Expects the check of document to pass. */
#define CHECK_VALID(document) \
    do { \
        const char *check_reason_ = "unset"; \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_tree_check((document), &check_reason_)); \
        NAOS_CHECK_NULL(check_reason_); \
    } while (0)

/*
 * The first source of the fixture, with the offsets of its parts:
 *    0 BOM (3)       3 "#"     4 " "     5 "a"     6 "\\*" (2)   8 " "
 *    9 "&amp;" (5)  14 " "    15 "\t"   16 "\r\n"  18 ">"       19 "\t"
 *   20 "b"          21 "\r"   22 "c"    23 "\n"    24 NUL       25 0xFF
 */
static const char source_text[] = "\xEF\xBB\xBF# a\\* &amp; \t\r\n>\tb\rc\n\0\xFF";
#define SOURCE_LENGTH (sizeof(source_text) - 1)

/* The second source: a BOM that is not at offset 0, then "abc". */
static const char second_text[] = "x\xEF\xBB\xBF" "abc";
#define SECOND_LENGTH (sizeof(second_text) - 1)

typedef struct fixture
{
    amark_document_t *document;
    amark_node_t *paragraph;
    amark_node_t *text;
    amark_source_id_t first;
    amark_source_id_t second;
} fixture_t;

static void build(fixture_t *f)
{
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &f->document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_add_source(f->document, source_text, SOURCE_LENGTH,
                                                  &f->first));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_add_source(f->document, second_text, SECOND_LENGTH,
                                                  &f->second));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(f->document, AMARK_NODE_PARAGRAPH, &f->paragraph));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_create(f->document, AMARK_NODE_TEXT, &f->text));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_append_child(f->paragraph, f->text));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_append_child(amark_document_root(f->document),
                                                f->paragraph));
}

static amark_token_t make(amark_token_kind_t kind, uint32_t flags, amark_source_id_t source,
                          size_t offset, size_t length)
{
    amark_token_t token;

    memset(&token, 0, sizeof(token));
    token.struct_size = sizeof(token);
    token.kind = kind;
    token.flags = flags;
    token.source = source;
    token.offset = offset;
    token.length = length;

    return token;
}

static amark_token_t make_tab(uint32_t flags, size_t offset, unsigned column_offset,
                              unsigned columns)
{
    amark_token_t token = make(AMARK_TOKEN_SPACE, flags | AMARK_TOKEN_FLAG_PARTIAL_TAB, 0,
                               offset, 1);

    token.tab_column_offset = column_offset;
    token.tab_columns = columns;

    return token;
}

static amark_token_t token_at(const amark_node_t *node, size_t index)
{
    amark_token_t token;

    memset(&token, 0, sizeof(token));
    token.struct_size = sizeof(token);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_token_at(node, index, &token));

    return token;
}

/* Whether two tokens have the same fields. */
static bool same_token(const amark_token_t *a, const amark_token_t *b)
{
    return ((a->kind == b->kind) && (a->role == b->role) && (a->flags == b->flags)
            && (a->source == b->source) && (a->offset == b->offset)
            && (a->length == b->length) && (a->tab_column_offset == b->tab_column_offset)
            && (a->tab_columns == b->tab_columns));
}

/* Expects token to be accepted and stored as the token at index of node. */
#define CHECK_ADDED_AT(node, token, index) \
    do { \
        amark_token_t added_ = (token); \
        amark_token_t got_; \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_add_token((node), &added_)); \
        got_ = token_at((node), (index)); \
        NAOS_CHECK_TRUE(same_token(&added_, &got_)); \
    } while (0)

/* The tokens of a node, byte for byte. */
#define MAX_SNAPSHOT 64

typedef struct snapshot
{
    size_t count;
    amark_token_record_t tokens[MAX_SNAPSHOT];
} snapshot_t;

static void take_snapshot(const amark_node_t *node, snapshot_t *s)
{
    memset(s, 0, sizeof(*s));
    NAOS_REQUIRE_TRUE(node->token_count <= MAX_SNAPSHOT);
    s->count = node->token_count;
    if (s->count != 0)
    {
        memcpy(s->tokens, node->tokens, s->count * sizeof(amark_token_record_t));
    }
}

static bool snapshot_is(const amark_node_t *node, const snapshot_t *s)
{
    return ((node->token_count == s->count)
            && ((s->count == 0)
                || (memcmp(s->tokens, node->tokens, s->count * sizeof(amark_token_record_t))
                    == 0)));
}

/* Expects token to be rejected with AMARK_ERROR_INVALID_ARGUMENT and the tokens of
 * node to stay unchanged. */
#define CHECK_REJECTED(node, token) \
    do { \
        amark_token_t rejected_ = (token); \
        snapshot_t before_; \
        take_snapshot((node), &before_); \
        NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, \
                          amark_node_add_token((node), &rejected_)); \
        NAOS_CHECK_TRUE(snapshot_is((node), &before_)); \
    } while (0)

/* Expects the offsets and roles of the tokens of node, in order. */
static void check_order(const amark_node_t *node, const size_t *offsets, const uint16_t *roles,
                        size_t count)
{
    size_t i;

    NAOS_REQUIRE_UINT_EQ(count, amark_node_token_count(node));
    for (i = 0; i < count; i++)
    {
        amark_token_t token = token_at(node, i);

        NAOS_CHECK_UINT_EQ(offsets[i], token.offset);
        NAOS_CHECK_UINT_EQ(roles[i], token.role);
    }
}

/* --- Suite token_check ------------------------------------------------------------ */

NAOS_TEST(the_fixture_offsets_match_the_source)
{
    fixture_t f;
    const char *data;
    size_t length = 0;

    build(&f);
    data = amark_document_source_data(f.document, f.first, &length);
    NAOS_REQUIRE_UINT_EQ(26, length);
    NAOS_CHECK_INT_EQ('#', data[3]);
    NAOS_CHECK_INT_EQ('\\', data[6]);
    NAOS_CHECK_INT_EQ('&', data[9]);
    NAOS_CHECK_INT_EQ(';', data[13]);
    NAOS_CHECK_INT_EQ('\t', data[15]);
    NAOS_CHECK_INT_EQ('\r', data[16]);
    NAOS_CHECK_INT_EQ('>', data[18]);
    NAOS_CHECK_INT_EQ('\t', data[19]);
    NAOS_CHECK_INT_EQ('\r', data[21]);
    NAOS_CHECK_INT_EQ('\n', data[23]);
    NAOS_CHECK_INT_EQ('\0', data[24]);
    NAOS_CHECK_UINT_EQ(7, SECOND_LENGTH);

    amark_document_destroy(f.document);
}

NAOS_TEST(a_valid_token_of_each_kind_is_kept)
{
    fixture_t f;
    amark_token_t marker = make(AMARK_TOKEN_MARKER, 0, 0, 3, 1);

    build(&f);
    marker.role = 7;
    CHECK_ADDED_AT(f.paragraph, make(AMARK_TOKEN_BOM, 0, 0, 0, 3), 0);
    CHECK_ADDED_AT(f.paragraph, marker, 1);
    CHECK_ADDED_AT(f.paragraph, make(AMARK_TOKEN_SPACE, 0, 0, 4, 1), 2);
    CHECK_ADDED_AT(f.paragraph, make(AMARK_TOKEN_TEXT, 0, 0, 5, 1), 3);
    CHECK_ADDED_AT(f.paragraph, make(AMARK_TOKEN_ESCAPE, 0, 0, 6, 2), 4);
    CHECK_ADDED_AT(f.paragraph, make(AMARK_TOKEN_TEXT, 0, 0, 8, 1), 5);
    CHECK_ADDED_AT(f.paragraph, make(AMARK_TOKEN_CHAR_REF, 0, 0, 9, 5), 6);
    CHECK_ADDED_AT(f.paragraph, make(AMARK_TOKEN_SPACE, 0, 0, 14, 2), 7);
    CHECK_ADDED_AT(f.paragraph, make(AMARK_TOKEN_LINE_ENDING, 0, 0, 16, 2), 8);
    CHECK_ADDED_AT(f.paragraph, make(AMARK_TOKEN_LAZY_CONTINUATION, 0, 0, 22, 0), 9);
    CHECK_ADDED_AT(f.paragraph, make(AMARK_TOKEN_REPLACED, 0, 0, 24, 2), 10);
    /* TEXT takes any bytes, a zero-length one too, even at the end of the source. */
    CHECK_ADDED_AT(f.paragraph, make(AMARK_TOKEN_TEXT, 0, 0, 26, 0), 11);
    CHECK_ADDED_AT(f.paragraph, make(AMARK_TOKEN_TEXT, 0, 1, 0, 7), 12);
    NAOS_CHECK_UINT_EQ(13, amark_node_token_count(f.paragraph));
    NAOS_CHECK_UINT_EQ(0, amark_node_token_count(f.text));
    NAOS_CHECK_UINT_EQ(0, amark_node_token_count(NULL));
    CHECK_VALID(f.document);

    amark_document_destroy(f.document);
}

NAOS_TEST(an_invalid_token_of_each_kind_is_rejected)
{
    fixture_t f;

    build(&f);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_add_token(f.paragraph,
                                             &(amark_token_t){ sizeof(amark_token_t),
                                                               AMARK_TOKEN_TEXT, 0, 0, 0, 5, 1,
                                                               0, 0 }));

    /* MARKER and REPLACED: length 1 or more. */
    CHECK_REJECTED(f.paragraph, make(AMARK_TOKEN_MARKER, 0, 0, 3, 0));
    CHECK_REJECTED(f.paragraph, make(AMARK_TOKEN_REPLACED, 0, 0, 24, 0));

    /* SPACE: only spaces and tabs. */
    CHECK_REJECTED(f.paragraph, make(AMARK_TOKEN_SPACE, 0, 0, 4, 2));
    CHECK_REJECTED(f.paragraph, make(AMARK_TOKEN_SPACE, 0, 0, 16, 1));

    /* LINE_ENDING: "\n", "\r\n" or "\r". */
    CHECK_REJECTED(f.paragraph, make(AMARK_TOKEN_LINE_ENDING, 0, 0, 5, 1));
    CHECK_REJECTED(f.paragraph, make(AMARK_TOKEN_LINE_ENDING, 0, 0, 16, 0));
    CHECK_REJECTED(f.paragraph, make(AMARK_TOKEN_LINE_ENDING, 0, 0, 16, 3));
    CHECK_REJECTED(f.paragraph, make(AMARK_TOKEN_LINE_ENDING, 0, 0, 23, 2));
    CHECK_REJECTED(f.paragraph, make(AMARK_TOKEN_LINE_ENDING, 0, 0, 15, 2));

    /* ESCAPE: length 2, starting with a backslash. */
    CHECK_REJECTED(f.paragraph, make(AMARK_TOKEN_ESCAPE, 0, 0, 6, 1));
    CHECK_REJECTED(f.paragraph, make(AMARK_TOKEN_ESCAPE, 0, 0, 6, 3));
    CHECK_REJECTED(f.paragraph, make(AMARK_TOKEN_ESCAPE, 0, 0, 5, 2));

    /* CHAR_REF: starts with & and ends with ;. */
    CHECK_REJECTED(f.paragraph, make(AMARK_TOKEN_CHAR_REF, 0, 0, 9, 4));
    CHECK_REJECTED(f.paragraph, make(AMARK_TOKEN_CHAR_REF, 0, 0, 10, 4));
    CHECK_REJECTED(f.paragraph, make(AMARK_TOKEN_CHAR_REF, 0, 0, 9, 1));
    CHECK_REJECTED(f.paragraph, make(AMARK_TOKEN_CHAR_REF, 0, 0, 9, 0));

    /* BOM: at offset 0, the bytes EF BB BF. */
    CHECK_REJECTED(f.paragraph, make(AMARK_TOKEN_BOM, 0, 1, 1, 3));
    CHECK_REJECTED(f.paragraph, make(AMARK_TOKEN_BOM, 0, 0, 0, 2));
    CHECK_REJECTED(f.paragraph, make(AMARK_TOKEN_BOM, 0, 0, 0, 4));
    CHECK_REJECTED(f.paragraph, make(AMARK_TOKEN_BOM, 0, 1, 0, 3));

    /* LAZY_CONTINUATION: length 0. */
    CHECK_REJECTED(f.paragraph, make(AMARK_TOKEN_LAZY_CONTINUATION, 0, 0, 22, 1));

    CHECK_VALID(f.document);

    amark_document_destroy(f.document);
}

NAOS_TEST(the_common_checks_reject_the_token)
{
    fixture_t f;
    amark_token_t token;

    build(&f);

    /* Unknown kinds and flags. */
    CHECK_REJECTED(f.paragraph, make((amark_token_kind_t)0, 0, 0, 5, 1));
    CHECK_REJECTED(f.paragraph, make((amark_token_kind_t)10, 0, 0, 5, 1));
    CHECK_REJECTED(f.paragraph, make((amark_token_kind_t)-1, 0, 0, 5, 1));
    CHECK_REJECTED(f.paragraph, make(AMARK_TOKEN_TEXT, 0x04u, 0, 5, 1));
    CHECK_REJECTED(f.paragraph, make(AMARK_TOKEN_TEXT, 0x100u, 0, 5, 1));
    CHECK_REJECTED(f.paragraph, make(AMARK_TOKEN_TEXT, 0x80000000u, 0, 5, 1));

    /* A source that is not registered. */
    CHECK_REJECTED(f.paragraph, make(AMARK_TOKEN_TEXT, 0, 2, 0, 0));
    CHECK_REJECTED(f.paragraph, make(AMARK_TOKEN_TEXT, 0, UINT32_MAX, 0, 0));

    /* Out of the source, and offset + length beyond SIZE_MAX. */
    CHECK_REJECTED(f.paragraph, make(AMARK_TOKEN_TEXT, 0, 0, 27, 0));
    CHECK_REJECTED(f.paragraph, make(AMARK_TOKEN_TEXT, 0, 0, 25, 2));
    CHECK_REJECTED(f.paragraph, make(AMARK_TOKEN_TEXT, 0, 1, 0, 8));
    CHECK_REJECTED(f.paragraph, make(AMARK_TOKEN_TEXT, 0, 0, 1, SIZE_MAX));
    CHECK_REJECTED(f.paragraph, make(AMARK_TOKEN_TEXT, 0, 0, SIZE_MAX, 1));
    CHECK_REJECTED(f.paragraph, make(AMARK_TOKEN_TEXT, 0, 0, SIZE_MAX, SIZE_MAX));

    /* A struct_size below that of version 1. */
    token = make(AMARK_TOKEN_TEXT, 0, 0, 5, 1);
    token.struct_size = sizeof(amark_token_t) - 1;
    CHECK_REJECTED(f.paragraph, token);
    token.struct_size = 0;
    CHECK_REJECTED(f.paragraph, token);

    /* Tab fields without AMARK_TOKEN_FLAG_PARTIAL_TAB. */
    token = make(AMARK_TOKEN_SPACE, 0, 0, 15, 1);
    token.tab_columns = 4;
    CHECK_REJECTED(f.paragraph, token);
    token = make(AMARK_TOKEN_SPACE, 0, 0, 15, 1);
    token.tab_column_offset = 1;
    CHECK_REJECTED(f.paragraph, token);

    /* NULL. */
    token = make(AMARK_TOKEN_TEXT, 0, 0, 5, 1);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_add_token(NULL, &token));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_add_token(f.paragraph, NULL));

    /* A larger struct_size (a later version) is accepted. */
    token.struct_size = sizeof(amark_token_t) + 16;
    CHECK_ADDED_AT(f.paragraph, token, 0);
    NAOS_CHECK_UINT_EQ(1, amark_node_token_count(f.paragraph));
    CHECK_VALID(f.document);

    amark_document_destroy(f.document);
}

NAOS_TEST(an_empty_source_takes_zero_length_tokens_only)
{
    fixture_t f;
    amark_source_id_t empty = 0;

    build(&f);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_add_source(f.document, NULL, 0, &empty));
    CHECK_ADDED_AT(f.text, make(AMARK_TOKEN_LAZY_CONTINUATION, 0, empty, 0, 0), 0);
    CHECK_ADDED_AT(f.text, make(AMARK_TOKEN_SPACE, 0, empty, 0, 0), 1);
    CHECK_REJECTED(f.text, make(AMARK_TOKEN_MARKER, 0, empty, 0, 0));
    CHECK_REJECTED(f.text, make(AMARK_TOKEN_BOM, 0, empty, 0, 0));
    CHECK_REJECTED(f.text, make(AMARK_TOKEN_TEXT, 0, empty, 0, 1));
    CHECK_VALID(f.document);

    amark_document_destroy(f.document);
}

NAOS_TEST(the_three_line_endings_stay_distinct)
{
    fixture_t f;
    const char *data;
    amark_token_t token;

    build(&f);
    data = amark_document_source_data(f.document, f.first, NULL);
    CHECK_ADDED_AT(f.paragraph, make(AMARK_TOKEN_LINE_ENDING, 0, 0, 16, 2), 0);
    CHECK_ADDED_AT(f.paragraph, make(AMARK_TOKEN_LINE_ENDING, 0, 0, 21, 1), 1);
    CHECK_ADDED_AT(f.paragraph, make(AMARK_TOKEN_LINE_ENDING, 0, 0, 23, 1), 2);

    token = token_at(f.paragraph, 0);
    NAOS_CHECK_MEM_EQ(data + token.offset, token.length, "\r\n", 2);
    token = token_at(f.paragraph, 1);
    NAOS_CHECK_MEM_EQ(data + token.offset, token.length, "\r", 1);
    token = token_at(f.paragraph, 2);
    NAOS_CHECK_MEM_EQ(data + token.offset, token.length, "\n", 1);

    /* The two bytes of a CRLF are also line endings each. */
    CHECK_ADDED_AT(f.text, make(AMARK_TOKEN_LINE_ENDING, 0, 0, 16, 1), 0);
    CHECK_ADDED_AT(f.text, make(AMARK_TOKEN_LINE_ENDING, 0, 0, 17, 1), 1);
    CHECK_VALID(f.document);

    amark_document_destroy(f.document);
}

NAOS_TEST(the_container_prefix_flag_and_the_role_are_kept)
{
    fixture_t f;
    amark_token_t marker = make(AMARK_TOKEN_MARKER, AMARK_TOKEN_FLAG_CONTAINER_PREFIX, 0, 18, 1);
    amark_token_t token;

    build(&f);
    marker.role = UINT16_MAX;
    CHECK_ADDED_AT(f.paragraph, marker, 0);
    CHECK_ADDED_AT(f.paragraph, make(AMARK_TOKEN_TEXT, 0, 0, 20, 1), 1);
    token = token_at(f.paragraph, 0);
    NAOS_CHECK_UINT_EQ(AMARK_TOKEN_FLAG_CONTAINER_PREFIX, token.flags);
    NAOS_CHECK_UINT_EQ(UINT16_MAX, token.role);
    token = token_at(f.paragraph, 1);
    NAOS_CHECK_UINT_EQ(0, token.flags);
    NAOS_CHECK_UINT_EQ(0, token.role);

    amark_document_destroy(f.document);
}

NAOS_TEST(a_lazy_continuation_is_a_zero_length_token)
{
    fixture_t f;
    amark_token_t token;

    build(&f);

    /* The line "c" omits the prefix of a container that owns "> b". */
    CHECK_ADDED_AT(f.paragraph,
                   make(AMARK_TOKEN_MARKER, AMARK_TOKEN_FLAG_CONTAINER_PREFIX, 0, 18, 1), 0);
    CHECK_ADDED_AT(f.paragraph,
                   make(AMARK_TOKEN_LAZY_CONTINUATION, AMARK_TOKEN_FLAG_CONTAINER_PREFIX, 0, 22,
                        0),
                   1);
    CHECK_ADDED_AT(f.paragraph,
                   make(AMARK_TOKEN_LAZY_CONTINUATION, AMARK_TOKEN_FLAG_CONTAINER_PREFIX, 0, 26,
                        0),
                   2);
    token = token_at(f.paragraph, 1);
    NAOS_CHECK_INT_EQ(AMARK_TOKEN_LAZY_CONTINUATION, token.kind);
    NAOS_CHECK_UINT_EQ(0, token.length);
    NAOS_CHECK_UINT_EQ(22, token.offset);

    /* A zero-length token sorts before a token at the same offset added later. */
    CHECK_ADDED_AT(f.paragraph, make(AMARK_TOKEN_TEXT, 0, 0, 22, 1), 2);
    token = token_at(f.paragraph, 1);
    NAOS_CHECK_INT_EQ(AMARK_TOKEN_LAZY_CONTINUATION, token.kind);
    CHECK_VALID(f.document);

    amark_document_destroy(f.document);
}

NAOS_TEST(one_tab_splits_into_two_partial_tab_tokens)
{
    fixture_t f;
    amark_token_t token;

    build(&f);

    /* ">\tb": the tab after > covers columns 1-3; the block quote takes one of them
     * and the content the other two. Added in reverse order, they still sort by
     * the column offset. */
    CHECK_ADDED_AT(f.paragraph, make_tab(0, 19, 1, 2), 0);
    CHECK_ADDED_AT(f.paragraph, make_tab(AMARK_TOKEN_FLAG_CONTAINER_PREFIX, 19, 0, 1), 0);
    token = token_at(f.paragraph, 0);
    NAOS_CHECK_UINT_EQ(AMARK_TOKEN_FLAG_CONTAINER_PREFIX | AMARK_TOKEN_FLAG_PARTIAL_TAB,
                       token.flags);
    NAOS_CHECK_UINT_EQ(0, token.tab_column_offset);
    NAOS_CHECK_UINT_EQ(1, token.tab_columns);
    token = token_at(f.paragraph, 1);
    NAOS_CHECK_UINT_EQ(AMARK_TOKEN_FLAG_PARTIAL_TAB, token.flags);
    NAOS_CHECK_UINT_EQ(1, token.tab_column_offset);
    NAOS_CHECK_UINT_EQ(2, token.tab_columns);

    /* The limits: 1-4 columns, ending at column 4 at most. */
    CHECK_ADDED_AT(f.text, make_tab(0, 15, 0, 4), 0);
    CHECK_ADDED_AT(f.text, make_tab(0, 15, 3, 1), 1);
    CHECK_REJECTED(f.text, make_tab(0, 15, 0, 0));
    CHECK_REJECTED(f.text, make_tab(0, 15, 0, 5));
    CHECK_REJECTED(f.text, make_tab(0, 15, 3, 2));
    CHECK_REJECTED(f.text, make_tab(0, 15, 4, 0));
    CHECK_REJECTED(f.text, make_tab(0, 15, 256, 1));
    CHECK_REJECTED(f.text, make_tab(0, 15, UINT_MAX, 1));
    CHECK_REJECTED(f.text, make_tab(0, 15, 0, UINT_MAX));
    CHECK_REJECTED(f.text, make_tab(0, 15, 1, UINT_MAX));

    /* One byte, a tab. */
    CHECK_REJECTED(f.text, make_tab(0, 14, 0, 1));
    token = make_tab(0, 15, 0, 1);
    token.length = 2;
    CHECK_REJECTED(f.text, token);
    token.offset = 14;
    CHECK_REJECTED(f.text, token);
    token = make_tab(0, 19, 0, 1);
    token.length = 0;
    CHECK_REJECTED(f.text, token);

    /* The kind check still applies: a tab is not a line ending. */
    token = make_tab(0, 19, 0, 1);
    token.kind = AMARK_TOKEN_LINE_ENDING;
    CHECK_REJECTED(f.text, token);
    token.kind = AMARK_TOKEN_TEXT;
    CHECK_ADDED_AT(f.text, token, 2);
    CHECK_VALID(f.document);

    amark_document_destroy(f.document);
}

NAOS_SUITE(token_check,
    NAOS_SET_TEARDOWN(restore),
    NAOS_ADD_TEST(the_fixture_offsets_match_the_source),
    NAOS_ADD_TEST(a_valid_token_of_each_kind_is_kept),
    NAOS_ADD_TEST(an_invalid_token_of_each_kind_is_rejected),
    NAOS_ADD_TEST(the_common_checks_reject_the_token),
    NAOS_ADD_TEST(an_empty_source_takes_zero_length_tokens_only),
    NAOS_ADD_TEST(the_three_line_endings_stay_distinct),
    NAOS_ADD_TEST(the_container_prefix_flag_and_the_role_are_kept),
    NAOS_ADD_TEST(a_lazy_continuation_is_a_zero_length_token),
    NAOS_ADD_TEST(one_tab_splits_into_two_partial_tab_tokens))

/* --- Suite token_store ------------------------------------------------------------ */

/* Adds a TEXT token of length 1 at offset of source with role to node. */
static void add_text(amark_node_t *node, amark_source_id_t source, size_t offset, uint16_t role)
{
    amark_token_t token = make(AMARK_TOKEN_TEXT, 0, source, offset, 1);

    token.role = role;
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_add_token(node, &token));
}

NAOS_TEST(tokens_are_sorted_by_source_offset_column_and_addition)
{
    fixture_t f;
    amark_token_t token;

    build(&f);
    add_text(f.paragraph, 0, 10, 1);
    add_text(f.paragraph, 0, 20, 2);
    NAOS_CHECK_UINT_EQ(0, f.document->stat_token_moves);

    /* Before both, between them, at an equal key (after the earlier one). */
    add_text(f.paragraph, 0, 5, 3);
    NAOS_CHECK_UINT_EQ(2, f.document->stat_token_moves);
    add_text(f.paragraph, 0, 15, 4);
    NAOS_CHECK_UINT_EQ(3, f.document->stat_token_moves);
    add_text(f.paragraph, 0, 10, 5);
    NAOS_CHECK_UINT_EQ(5, f.document->stat_token_moves);
    add_text(f.paragraph, 0, 10, 6);
    NAOS_CHECK_UINT_EQ(7, f.document->stat_token_moves);

    /* At an equal key with the last token: appended without a move. */
    add_text(f.paragraph, 0, 20, 7);
    NAOS_CHECK_UINT_EQ(7, f.document->stat_token_moves);

    /* The second source comes after the whole first one, whatever the offset. */
    add_text(f.paragraph, 1, 0, 8);
    add_text(f.paragraph, 0, 25, 9);
    NAOS_CHECK_UINT_EQ(8, f.document->stat_token_moves);

    check_order(f.paragraph, (const size_t[]){ 5, 10, 10, 10, 15, 20, 20, 25, 0 },
                (const uint16_t[]){ 3, 1, 5, 6, 4, 2, 7, 9, 8 }, 9);
    token = token_at(f.paragraph, 8);
    NAOS_CHECK_UINT_EQ(1, token.source);
    CHECK_VALID(f.document);

    amark_document_destroy(f.document);
}

NAOS_TEST(adding_in_source_order_moves_no_token)
{
    static char bytes[1000];
    amark_document_t *document = NULL;
    amark_node_t *node = NULL;
    amark_source_id_t source = 0;
    size_t i;

    memset(bytes, 'a', sizeof(bytes));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_add_source(document, bytes, sizeof(bytes), &source));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(document, AMARK_NODE_TEXT, &node));

    /* Each offset twice (equal keys), then a zero-length token at the end. */
    for (i = 0; i < sizeof(bytes); i++)
    {
        add_text(node, source, i, 0);
        add_text(node, source, i, 1);
    }

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_node_add_token(node, &(amark_token_t){ sizeof(amark_token_t),
                                                                     AMARK_TOKEN_TEXT, 0, 0,
                                                                     source, sizeof(bytes), 0,
                                                                     0, 0 }));
    NAOS_CHECK_UINT_EQ((2 * sizeof(bytes)) + 1, amark_node_token_count(node));
    NAOS_CHECK_UINT_EQ(0, document->stat_token_moves);
    NAOS_CHECK_UINT_EQ(1, token_at(node, 1001).role);
    CHECK_VALID(document);

    amark_document_destroy(document);
}

NAOS_TEST(removing_keeps_the_order)
{
    fixture_t f;
    snapshot_t before;

    build(&f);
    add_text(f.paragraph, 0, 1, 1);
    add_text(f.paragraph, 0, 2, 2);
    add_text(f.paragraph, 0, 3, 3);
    add_text(f.paragraph, 0, 4, 4);
    add_text(f.paragraph, 0, 5, 5);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_remove_token(f.paragraph, 2));
    check_order(f.paragraph, (const size_t[]){ 1, 2, 4, 5 }, (const uint16_t[]){ 1, 2, 4, 5 },
                4);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_remove_token(f.paragraph, 0));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_remove_token(f.paragraph, 2));
    check_order(f.paragraph, (const size_t[]){ 2, 4 }, (const uint16_t[]){ 2, 4 }, 2);

    /* Out of range and NULL. */
    take_snapshot(f.paragraph, &before);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_remove_token(f.paragraph, 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_remove_token(f.paragraph, SIZE_MAX));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_remove_token(f.text, 0));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_remove_token(NULL, 0));
    NAOS_CHECK_TRUE(snapshot_is(f.paragraph, &before));

    /* Down to none, then adding again. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_remove_token(f.paragraph, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_remove_token(f.paragraph, 0));
    NAOS_CHECK_UINT_EQ(0, amark_node_token_count(f.paragraph));
    add_text(f.paragraph, 0, 7, 7);
    check_order(f.paragraph, (const size_t[]){ 7 }, (const uint16_t[]){ 7 }, 1);
    CHECK_VALID(f.document);

    amark_document_destroy(f.document);
}

NAOS_TEST(transferring_merges_into_the_order_of_the_target)
{
    fixture_t f;

    build(&f);
    add_text(f.paragraph, 0, 2, 1);
    add_text(f.paragraph, 0, 4, 2);
    add_text(f.paragraph, 0, 6, 3);
    add_text(f.paragraph, 0, 8, 4);
    add_text(f.paragraph, 1, 0, 5);
    add_text(f.text, 0, 1, 11);
    add_text(f.text, 0, 6, 12);
    add_text(f.text, 0, 20, 13);
    f.document->stat_token_moves = 0;

    /* Offsets 4, 6, 8 of source 0 and 0 of source 1: 6 goes after the equal key of
     * the target, the 6 and the 20 of the target shift (two moves) and the token of
     * source 1 goes at the end. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_transfer_tokens(f.paragraph, 1, 4, f.text));
    check_order(f.paragraph, (const size_t[]){ 2 }, (const uint16_t[]){ 1 }, 1);
    check_order(f.text, (const size_t[]){ 1, 4, 6, 6, 8, 20, 0 },
                (const uint16_t[]){ 11, 2, 12, 3, 4, 13, 5 }, 7);
    NAOS_CHECK_UINT_EQ(1, token_at(f.text, 6).source);
    NAOS_CHECK_UINT_EQ(2, f.document->stat_token_moves);
    CHECK_VALID(f.document);

    /* Back, to the empty end of the paragraph: appended. */
    f.document->stat_token_moves = 0;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_transfer_tokens(f.text, 5, 2, f.paragraph));
    check_order(f.paragraph, (const size_t[]){ 2, 20, 0 }, (const uint16_t[]){ 1, 13, 5 }, 3);
    NAOS_CHECK_UINT_EQ(0, f.document->stat_token_moves);

    /* Everything, into a node that has none. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_transfer_tokens(f.paragraph, 0, 3,
                                                 amark_document_root(f.document)));
    NAOS_CHECK_UINT_EQ(0, amark_node_token_count(f.paragraph));
    check_order(amark_document_root(f.document), (const size_t[]){ 2, 20, 0 },
                (const uint16_t[]){ 1, 13, 5 }, 3);
    CHECK_VALID(f.document);

    amark_document_destroy(f.document);
}

NAOS_TEST(an_empty_or_same_node_transfer_changes_nothing)
{
    fixture_t f;
    snapshot_t paragraph;
    snapshot_t text;

    build(&f);
    add_text(f.paragraph, 0, 2, 1);
    add_text(f.paragraph, 0, 4, 2);
    add_text(f.text, 0, 3, 3);
    take_snapshot(f.paragraph, &paragraph);
    take_snapshot(f.text, &text);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_transfer_tokens(f.paragraph, 0, 0, f.text));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_transfer_tokens(f.paragraph, 2, 0, f.text));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_node_transfer_tokens(f.paragraph, 0, 2, f.paragraph));
    NAOS_CHECK_TRUE(snapshot_is(f.paragraph, &paragraph));
    NAOS_CHECK_TRUE(snapshot_is(f.text, &text));

    amark_document_destroy(f.document);
}

NAOS_TEST(a_rejected_transfer_changes_neither_node)
{
    fixture_t f;
    amark_document_t *other = NULL;
    amark_node_t *foreign = NULL;
    snapshot_t paragraph;
    snapshot_t text;

    build(&f);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &other));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_node_create(other, AMARK_NODE_TEXT, &foreign));
    add_text(f.paragraph, 0, 2, 1);
    add_text(f.paragraph, 0, 4, 2);
    add_text(f.text, 0, 3, 3);
    take_snapshot(f.paragraph, &paragraph);
    take_snapshot(f.text, &text);

    /* Another document. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_transfer_tokens(f.paragraph, 0, 1, foreign));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_transfer_tokens(foreign, 0, 0, f.text));
    NAOS_CHECK_UINT_EQ(0, amark_node_token_count(foreign));

    /* Out of the tokens of from, an overflow included. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_transfer_tokens(f.paragraph, 0, 3, f.text));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_transfer_tokens(f.paragraph, 3, 0, f.text));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_transfer_tokens(f.paragraph, 1, SIZE_MAX, f.text));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_transfer_tokens(f.paragraph, SIZE_MAX, 2, f.text));

    /* NULL. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_transfer_tokens(NULL, 0, 0, f.text));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_transfer_tokens(f.paragraph, 0, 0, NULL));

    NAOS_CHECK_TRUE(snapshot_is(f.paragraph, &paragraph));
    NAOS_CHECK_TRUE(snapshot_is(f.text, &text));
    CHECK_VALID(f.document);

    amark_document_destroy(other);
    amark_document_destroy(f.document);
}

/* A struct of a later version: amark_token_t and a field after it. */
typedef struct later_token
{
    amark_token_t v1;
    uint32_t extra;
} later_token_t;

NAOS_TEST(token_at_writes_up_to_the_struct_size)
{
    fixture_t f;
    amark_token_t token;
    amark_token_t untouched;
    later_token_t later;

    build(&f);
    add_text(f.paragraph, 0, 5, 9);

    /* A later version: the fields of version 1, struct_size kept, the rest left. */
    memset(&later, 0xA5, sizeof(later));
    later.v1.struct_size = sizeof(later);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_token_at(f.paragraph, 0, &later.v1));
    NAOS_CHECK_UINT_EQ(sizeof(later), later.v1.struct_size);
    NAOS_CHECK_INT_EQ(AMARK_TOKEN_TEXT, later.v1.kind);
    NAOS_CHECK_UINT_EQ(9, later.v1.role);
    NAOS_CHECK_UINT_EQ(0, later.v1.flags);
    NAOS_CHECK_UINT_EQ(0, later.v1.source);
    NAOS_CHECK_UINT_EQ(5, later.v1.offset);
    NAOS_CHECK_UINT_EQ(1, later.v1.length);
    NAOS_CHECK_UINT_EQ(0, later.v1.tab_column_offset);
    NAOS_CHECK_UINT_EQ(0, later.v1.tab_columns);
    NAOS_CHECK_UINT_EQ(0xA5A5A5A5u, later.extra);

    /* Below version 1, out of range and NULL: nothing written. */
    memset(&token, 0x5A, sizeof(token));
    token.struct_size = sizeof(amark_token_t) - 1;
    untouched = token;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_token_at(f.paragraph, 0, &token));
    NAOS_CHECK_MEM_EQ(&untouched, sizeof(untouched), &token, sizeof(token));
    token.struct_size = sizeof(amark_token_t);
    untouched = token;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_token_at(f.paragraph, 1, &token));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_token_at(f.paragraph, SIZE_MAX, &token));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_token_at(f.text, 0, &token));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_token_at(NULL, 0, &token));
    NAOS_CHECK_MEM_EQ(&untouched, sizeof(untouched), &token, sizeof(token));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_token_at(f.paragraph, 0, NULL));

    amark_document_destroy(f.document);
}

NAOS_TEST(each_kind_has_a_name)
{
    NAOS_CHECK_STR_EQ("text", amark_token_kind_name(AMARK_TOKEN_TEXT));
    NAOS_CHECK_STR_EQ("marker", amark_token_kind_name(AMARK_TOKEN_MARKER));
    NAOS_CHECK_STR_EQ("space", amark_token_kind_name(AMARK_TOKEN_SPACE));
    NAOS_CHECK_STR_EQ("line_ending", amark_token_kind_name(AMARK_TOKEN_LINE_ENDING));
    NAOS_CHECK_STR_EQ("escape", amark_token_kind_name(AMARK_TOKEN_ESCAPE));
    NAOS_CHECK_STR_EQ("char_ref", amark_token_kind_name(AMARK_TOKEN_CHAR_REF));
    NAOS_CHECK_STR_EQ("replaced", amark_token_kind_name(AMARK_TOKEN_REPLACED));
    NAOS_CHECK_STR_EQ("bom", amark_token_kind_name(AMARK_TOKEN_BOM));
    NAOS_CHECK_STR_EQ("lazy_continuation",
                      amark_token_kind_name(AMARK_TOKEN_LAZY_CONTINUATION));
    NAOS_CHECK_STR_EQ("unknown", amark_token_kind_name((amark_token_kind_t)0));
    NAOS_CHECK_STR_EQ("unknown", amark_token_kind_name((amark_token_kind_t)10));
    NAOS_CHECK_STR_EQ("unknown", amark_token_kind_name((amark_token_kind_t)-1));
}

NAOS_TEST(token_operations_record_no_edit)
{
    fixture_t f;
    amark_node_t *root;

    build(&f);
    root = amark_document_root(f.document);
    amark_document_set_edit_tracking(f.document, true);

    add_text(f.text, 0, 5, 1);
    add_text(f.text, 0, 3, 2);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_transfer_tokens(f.text, 0, 1, f.paragraph));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_node_remove_token(f.text, 0));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.text));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(f.paragraph));
    NAOS_CHECK_UINT_EQ(0, amark_node_edits(root));
    CHECK_VALID(f.document);

    amark_document_destroy(f.document);
}

/* Whether the tokens of node are, in order, those at offsets of source 0. */
static bool offsets_are(const amark_node_t *node, const size_t *offsets, size_t count)
{
    size_t i;

    if (node->token_count != count)
    {
        return false;
    }

    for (i = 0; i < count; i++)
    {
        if (node->tokens[i].offset != offsets[i])
        {
            return false;
        }
    }

    return true;
}

/* Adds tokens out of order and then in order (the array grows several times); a
 * failure must leave the tokens as they were. */
static amark_error_t add_scenario(void *context)
{
    static const size_t order[] = { 5, 1, 9, 3, 7, 2, 8, 4, 6, 10, 11, 12, 13, 14, 15, 16, 17 };
    bool *unchanged = context;
    amark_document_t *document = NULL;
    amark_node_t *node = NULL;
    amark_source_id_t source = 0;
    size_t i;
    amark_error_t err;

    err = amark_document_create(NULL, &document);
    if (err == AMARK_ERROR_NONE)
    {
        err = amark_document_add_source(document, source_text, SOURCE_LENGTH, &source);
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = amark_node_create(document, AMARK_NODE_TEXT, &node);
    }

    for (i = 0; (err == AMARK_ERROR_NONE) && (i < (sizeof(order) / sizeof(order[0]))); i++)
    {
        amark_token_t token = make(AMARK_TOKEN_TEXT, 0, source, order[i], 1);
        size_t sorted[sizeof(order) / sizeof(order[0])];
        size_t count = node->token_count;
        size_t j;

        /* The offsets before the addition, sorted. */
        for (j = 0; j < count; j++)
        {
            sorted[j] = node->tokens[j].offset;
        }

        err = amark_node_add_token(node, &token);
        if ((err != AMARK_ERROR_NONE) && !offsets_are(node, sorted, count))
        {
            *unchanged = false;
        }
    }

    if ((err == AMARK_ERROR_NONE) && !offsets_are(node,
                                                  (const size_t[]){ 1, 2, 3, 4, 5, 6, 7, 8, 9,
                                                                    10, 11, 12, 13, 14, 15, 16,
                                                                    17 },
                                                  17))
    {
        *unchanged = false;
    }

    amark_document_destroy(document);

    return err;
}

NAOS_TEST(adding_passes_the_out_of_memory_sweep)
{
    bool unchanged = true;

    AMARK_CHECK_OOM_SWEEP(add_scenario, &unchanged);
    NAOS_CHECK_TRUE(unchanged);
}

/* Transfers tokens into a node whose array is full; a failure must leave both
 * nodes as they were. */
static amark_error_t transfer_scenario(void *context)
{
    bool *unchanged = context;
    amark_document_t *document = NULL;
    amark_node_t *from = NULL;
    amark_node_t *to = NULL;
    amark_source_id_t source = 0;
    size_t i;
    amark_error_t err;

    err = amark_document_create(NULL, &document);
    if (err == AMARK_ERROR_NONE)
    {
        err = amark_document_add_source(document, source_text, SOURCE_LENGTH, &source);
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = amark_node_create(document, AMARK_NODE_PARAGRAPH, &from);
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = amark_node_create(document, AMARK_NODE_TEXT, &to);
    }

    for (i = 0; (err == AMARK_ERROR_NONE) && (i < 6); i++)
    {
        amark_token_t token = make(AMARK_TOKEN_TEXT, 0, source, (2 * i) + 1, 1);

        err = amark_node_add_token(from, &token);
        if (err == AMARK_ERROR_NONE)
        {
            token.offset = 2 * i;
            err = amark_node_add_token(to, &token);
        }
    }

    /* Fills the array of to, so that the transfer must grow it. */
    while ((err == AMARK_ERROR_NONE) && (to->token_count < to->token_capacity))
    {
        amark_token_t token = make(AMARK_TOKEN_TEXT, 0, source, 20, 1);

        err = amark_node_add_token(to, &token);
    }

    if (err == AMARK_ERROR_NONE)
    {
        size_t to_count = to->token_count;

        err = amark_node_transfer_tokens(from, 1, 4, to);
        if ((err != AMARK_ERROR_NONE)
            && (!offsets_are(from, (const size_t[]){ 1, 3, 5, 7, 9, 11 }, 6)
                || (to->token_count != to_count)))
        {
            *unchanged = false;
        }
        else if ((err == AMARK_ERROR_NONE)
                 && (!offsets_are(from, (const size_t[]){ 1, 11 }, 2)
                     || (to->token_count != (to_count + 4))
                     || (amark_tree_check(document, NULL) != AMARK_ERROR_NONE)))
        {
            *unchanged = false;
        }
    }

    amark_document_destroy(document);

    return err;
}

NAOS_TEST(transferring_passes_the_out_of_memory_sweep)
{
    bool unchanged = true;

    AMARK_CHECK_OOM_SWEEP(transfer_scenario, &unchanged);
    NAOS_CHECK_TRUE(unchanged);
}

NAOS_SUITE(token_store,
    NAOS_SET_TEARDOWN(restore),
    NAOS_ADD_TEST(tokens_are_sorted_by_source_offset_column_and_addition),
    NAOS_ADD_TEST(adding_in_source_order_moves_no_token),
    NAOS_ADD_TEST(removing_keeps_the_order),
    NAOS_ADD_TEST(transferring_merges_into_the_order_of_the_target),
    NAOS_ADD_TEST(an_empty_or_same_node_transfer_changes_nothing),
    NAOS_ADD_TEST(a_rejected_transfer_changes_neither_node),
    NAOS_ADD_TEST(token_at_writes_up_to_the_struct_size),
    NAOS_ADD_TEST(each_kind_has_a_name),
    NAOS_ADD_TEST(token_operations_record_no_edit),
    NAOS_ADD_TEST(adding_passes_the_out_of_memory_sweep),
    NAOS_ADD_TEST(transferring_passes_the_out_of_memory_sweep))

NAOS_MAIN(
    NAOS_ADD_SUITE(token_check),
    NAOS_ADD_SUITE(token_store)
)
