/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The tests of LineTable (syntax-tree): the registration of sources, the
 * division into lines (LF, CRLF, CR, no line ending), the widths, the BOM, the
 * incremental completion (the same line table for any split of the bytes), the
 * end of the input, the default line ending, the queries, the release, the
 * allocation failures and the line table invariant of the internal check
 * (1.4, 2.1, 2.2, 2.4-2.8).
 */
#include "tree/tree.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <naos/naos.h>

#include "test_allocator.h"
#include "test_check.h"

/* ---- Helpers ------------------------------------------------------------------- */

static amark_test_allocator_t test_allocator;

static void use_test_allocator(void)
{
    amark_test_allocator_init(&test_allocator);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_allocator_set(amark_test_allocator_activate(&test_allocator)));
}

/* Checks that document keeps the internal invariants (amark_tree_check). */
static void check_tree_ok(amark_document_t *document)
{
    amark_tree_check_report_t report;

    NAOS_CHECK_STR_EQ("none", amark_tree_violation_name(amark_tree_check(document, &report)));
    NAOS_CHECK_NULL(report.message);
}

static amark_document_t *new_document(void)
{
    amark_document_t *document = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_create(NULL, &document));
    NAOS_REQUIRE_NOT_NULL(document);
    return document;
}

/* The line line of document, through the public query. */
static amark_line_info_t get_line(const amark_document_t *document, amark_line_id_t line)
{
    amark_line_info_t info;

    memset(&info, 0xA5, sizeof(info));
    info.struct_size = sizeof(info);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_line(document, line, &info));
    return info;
}

/* Checks the line line of document. */
static void check_line(const amark_document_t *document, amark_line_id_t line,
                       amark_source_id_t source, uint32_t number, size_t offset,
                       size_t length, amark_eol_t eol, uint32_t width)
{
    amark_line_info_t info = get_line(document, line);

    NAOS_CHECK_UINT_EQ(sizeof(info), info.struct_size);
    NAOS_CHECK_UINT_EQ(source, info.source);
    NAOS_CHECK_UINT_EQ(number, info.number);
    NAOS_CHECK_UINT_EQ(offset, info.offset);
    NAOS_CHECK_UINT_EQ(length, info.length);
    NAOS_CHECK_INT_EQ(eol, info.eol);
    NAOS_CHECK_UINT_EQ(width, info.width);
}

/* Registers the literal s as one finished source and returns its identifier. */
#define ADD_FINISHED(document, s, out) \
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, \
                        amark_document_add_source((document), (s), sizeof(s) - 1, \
                                                  AMARK_SOURCE_FINISHED, (out)))

/* ---- Division into lines (2.1, 2.2) -------------------------------------------- */

NAOS_TEST(lf_crlf_cr_and_no_line_ending)
{
    amark_document_t *document = new_document();
    amark_source_id_t source = 99;

    ADD_FINISHED(document, "a\nbb\r\nccc\rd", &source);
    NAOS_CHECK_UINT_EQ(0, source);
    NAOS_CHECK_UINT_EQ(1, amark_document_source_count(document));
    NAOS_REQUIRE_UINT_EQ(4, amark_document_line_count(document));
    check_line(document, 0, 0, 1, 0, 1, AMARK_EOL_LF, 1);
    check_line(document, 1, 0, 2, 2, 2, AMARK_EOL_CRLF, 2);
    check_line(document, 2, 0, 3, 6, 3, AMARK_EOL_CR, 3);
    check_line(document, 3, 0, 4, 10, 1, AMARK_EOL_NONE, 1);
    check_tree_ok(document);
    amark_document_destroy(document);
}

NAOS_TEST(trailing_line_ending_and_empty_lines)
{
    amark_document_t *document = new_document();
    amark_source_id_t source;

    /* The line ending at the end adds no empty line after it. */
    ADD_FINISHED(document, "a\n", &source);
    NAOS_CHECK_UINT_EQ(1, amark_document_line_count(document));
    /* Empty lines: LF, then CR CR is a CR line and a CRLF line, and the last CR
     * is completed by the end of the input. */
    ADD_FINISHED(document, "\n\r\r\n\r", &source);
    NAOS_CHECK_UINT_EQ(1, source);
    NAOS_REQUIRE_UINT_EQ(5, amark_document_line_count(document));
    check_line(document, 1, 1, 1, 0, 0, AMARK_EOL_LF, 0);
    check_line(document, 2, 1, 2, 1, 0, AMARK_EOL_CR, 0);
    check_line(document, 3, 1, 3, 2, 0, AMARK_EOL_CRLF, 0);
    check_line(document, 4, 1, 4, 4, 0, AMARK_EOL_CR, 0);
    ADD_FINISHED(document, "\r", &source);
    NAOS_REQUIRE_UINT_EQ(6, amark_document_line_count(document));
    check_line(document, 5, 2, 1, 0, 0, AMARK_EOL_CR, 0);
    /* An empty source has no line. */
    ADD_FINISHED(document, "", &source);
    NAOS_CHECK_UINT_EQ(3, source);
    NAOS_CHECK_UINT_EQ(6, amark_document_line_count(document));
    NAOS_CHECK_UINT_EQ(4, amark_document_source_count(document));
    check_tree_ok(document);
    amark_document_destroy(document);
}

NAOS_TEST(nul_and_other_bytes_are_ordinary)
{
    amark_document_t *document = new_document();
    amark_source_id_t source;

    /* A NUL byte is an ordinary one-column byte. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_add_source(document, "a\0b\n", 4,
                                                  AMARK_SOURCE_FINISHED, &source));
    NAOS_REQUIRE_UINT_EQ(1, amark_document_line_count(document));
    check_line(document, 0, 0, 1, 0, 3, AMARK_EOL_LF, 3);
    amark_document_destroy(document);
}

NAOS_TEST(widths_use_tab_stops_of_four)
{
    amark_document_t *document = new_document();
    amark_source_id_t source;

    ADD_FINISHED(document, "\tab\na\tb\nab\t\t\nabcd\t\n\xC3\xA9\n", &source);
    NAOS_REQUIRE_UINT_EQ(5, amark_document_line_count(document));
    check_line(document, 0, 0, 1, 0, 3, AMARK_EOL_LF, 6);
    check_line(document, 1, 0, 2, 4, 3, AMARK_EOL_LF, 5);
    check_line(document, 2, 0, 3, 8, 4, AMARK_EOL_LF, 8);
    check_line(document, 3, 0, 4, 13, 5, AMARK_EOL_LF, 8);
    /* Every byte other than a tab is one column. */
    check_line(document, 4, 0, 5, 19, 2, AMARK_EOL_LF, 2);
    amark_document_destroy(document);
}

/* ---- The copy of the bytes (1.4) and the BOM (2.4) ------------------------------- */

NAOS_TEST(source_bytes_are_copied)
{
    amark_document_t *document = new_document();
    char bytes[] = "one\ntwo";
    amark_source_id_t source;
    const char *data = NULL;
    size_t length = 0;
    bool has_bom = true;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_add_source(document, bytes, 7, 0, &source));
    bytes[0] = 'X';
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_source_data(document, source, &data, &length, &has_bom));
    NAOS_CHECK_TRUE(data != bytes);
    AMARK_CHECK_BYTES_EQ(data, length, "one\ntwo");
    NAOS_CHECK_FALSE(has_bom);
    /* The outputs may be NULL. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_document_source_data(document, source, NULL, NULL, NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_append_source(document, source, "!", 1));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_source_data(document, source, &data, &length, NULL));
    AMARK_CHECK_BYTES_EQ(data, length, "one\ntwo!");
    /* An empty source has data too. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_add_source(document, NULL, 0, 0, &source));
    data = NULL;
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_source_data(document, source, &data, &length, &has_bom));
    NAOS_CHECK_NOT_NULL(data);
    NAOS_CHECK_UINT_EQ(0, length);
    check_tree_ok(document);
    amark_document_destroy(document);
}

NAOS_TEST(bom_is_a_mark_of_the_source)
{
    amark_document_t *document = new_document();
    amark_source_id_t source;
    const char *data = NULL;
    size_t length = 0;
    bool has_bom = false;

    ADD_FINISHED(document, "\xEF\xBB\xBF" "a\tb\n\xEF\xBB\xBF", &source);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_source_data(document, source, &data, &length, &has_bom));
    NAOS_CHECK_TRUE(has_bom);
    /* The data keeps the BOM; the first line starts after it. */
    AMARK_CHECK_BYTES_EQ(data, length, "\xEF\xBB\xBF" "a\tb\n\xEF\xBB\xBF");
    NAOS_REQUIRE_UINT_EQ(2, amark_document_line_count(document));
    check_line(document, 0, 0, 1, 3, 3, AMARK_EOL_LF, 5);
    /* Only the first bytes of a source can be the BOM. */
    check_line(document, 1, 0, 2, 7, 3, AMARK_EOL_NONE, 3);

    /* The BOM alone: no line. */
    ADD_FINISHED(document, "\xEF\xBB\xBF", &source);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_source_data(document, source, NULL, NULL, &has_bom));
    NAOS_CHECK_TRUE(has_bom);
    NAOS_CHECK_UINT_EQ(2, amark_document_line_count(document));

    /* A prefix of the BOM is not the BOM. */
    ADD_FINISHED(document, "\xEF\xBB", &source);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_source_data(document, source, NULL, NULL, &has_bom));
    NAOS_CHECK_FALSE(has_bom);
    NAOS_REQUIRE_UINT_EQ(3, amark_document_line_count(document));
    check_line(document, 2, 2, 1, 0, 2, AMARK_EOL_NONE, 2);
    check_tree_ok(document);
    amark_document_destroy(document);
}

/* ---- Incremental completion (2.5, 2.6) ------------------------------------------ */

NAOS_TEST(unfinished_lines_wait_for_the_next_bytes_or_the_end)
{
    amark_document_t *document = new_document();
    amark_source_id_t source;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_add_source(document, "a\nb", 3, 0, &source));
    NAOS_CHECK_UINT_EQ(1, amark_document_line_count(document));
    /* A CR at the end waits: it may be the start of CRLF. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_append_source(document, source, "c\r", 2));
    NAOS_CHECK_UINT_EQ(1, amark_document_line_count(document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_append_source(document, source, "\n", 1));
    NAOS_REQUIRE_UINT_EQ(2, amark_document_line_count(document));
    check_line(document, 1, 0, 2, 2, 2, AMARK_EOL_CRLF, 2);
    /* A CR followed by another byte is a CR line. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_append_source(document, source, "d\r", 2));
    NAOS_CHECK_UINT_EQ(2, amark_document_line_count(document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_append_source(document, source, "e", 1));
    NAOS_REQUIRE_UINT_EQ(3, amark_document_line_count(document));
    check_line(document, 2, 0, 3, 6, 1, AMARK_EOL_CR, 1);
    /* An empty append changes nothing. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_append_source(document, source, NULL, 0));
    NAOS_CHECK_UINT_EQ(3, amark_document_line_count(document));
    check_tree_ok(document);
    /* The end of the input completes the last line. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_finish_source(document, source));
    NAOS_REQUIRE_UINT_EQ(4, amark_document_line_count(document));
    check_line(document, 3, 0, 4, 8, 1, AMARK_EOL_NONE, 1);
    check_tree_ok(document);
    amark_document_destroy(document);
}

NAOS_TEST(finished_sources_reject_appends)
{
    amark_document_t *document = new_document();
    amark_source_id_t source;
    amark_source_id_t open;
    const char *data = NULL;
    size_t length = 0;

    ADD_FINISHED(document, "a\r", &source);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_STATE,
                      amark_document_append_source(document, source, "\n", 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_STATE,
                      amark_document_append_source(document, source, NULL, 0));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_STATE, amark_document_finish_source(document, source));
    NAOS_REQUIRE_UINT_EQ(1, amark_document_line_count(document));
    check_line(document, 0, 0, 1, 0, 1, AMARK_EOL_CR, 1);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_source_data(document, source, &data, &length, NULL));
    AMARK_CHECK_BYTES_EQ(data, length, "a\r");

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_add_source(document, "x", 1, 0, &open));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_finish_source(document, open));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_STATE,
                      amark_document_append_source(document, open, "y", 1));
    NAOS_CHECK_UINT_EQ(2, amark_document_line_count(document));
    check_tree_ok(document);
    amark_document_destroy(document);
}

/* The inputs of the split tests: every kind of line ending, a CR before an LF,
 * CR CR, the BOM, tabs, and endings without a line ending, with CR, with LF. */
static const struct
{
    const char *bytes;
    size_t length;
} split_inputs[] = {
#define INPUT(s) {s, sizeof(s) - 1}
    INPUT("a\nbb\r\nccc\rd"),
    INPUT("\r\n\r\r\n\n\r"),
    INPUT("\xEF\xBB\xBF\tx\r\ny\tz\r"),
    INPUT("\xEF\xBB\xBF"),
    INPUT("\xEF\xBB"),
    INPUT("\xEF\xBB\xBF\r\n"),
    INPUT("\xEF\xBBx\xBF\n"),
    INPUT(""),
    INPUT("no line ending"),
    INPUT("tab\tin\tline\n\tlead\r\r\rtail\n"),
#undef INPUT
};

/* A copy of the line table of a document, without the source identifiers. */
typedef struct snapshot
{
    size_t count;
    amark_line_info_t lines[64];
    amark_eol_t default_eol;
} snapshot_t;

static void take_snapshot(const amark_document_t *document, snapshot_t *out)
{
    size_t i;

    memset(out, 0, sizeof(*out));
    out->count = amark_document_line_count(document);
    NAOS_REQUIRE_TRUE(out->count <= 64);
    for (i = 0; i < out->count; i++)
    {
        out->lines[i] = get_line(document, (amark_line_id_t)i);
        out->lines[i].source = 0;
    }
    out->default_eol = amark_document_default_eol(document);
}

/* Checks that the lines of b start with the lines of a, the same. */
static void check_prefix(const snapshot_t *a, const snapshot_t *b, size_t input)
{
    size_t i;

    NAOS_CHECK_TRUE(a->count <= b->count);
    for (i = 0; i < a->count && i < b->count; i++)
    {
        const amark_line_info_t *x = &a->lines[i];
        const amark_line_info_t *y = &b->lines[i];

        NAOS_CHECK_TRUE(x->number == y->number && x->offset == y->offset &&
                        x->length == y->length && x->eol == y->eol && x->width == y->width);
        if (x->number != y->number || x->offset != y->offset || x->length != y->length ||
            x->eol != y->eol || x->width != y->width)
        {
            NAOS_CHECK_UINT_EQ(SIZE_MAX, input); /* names the input */
            NAOS_CHECK_UINT_EQ(SIZE_MAX, i);     /* and the line */
            return;
        }
    }
}

static void check_same(const snapshot_t *a, const snapshot_t *b, size_t input)
{
    NAOS_CHECK_UINT_EQ(a->count, b->count);
    NAOS_CHECK_INT_EQ(a->default_eol, b->default_eol);
    check_prefix(a->count <= b->count ? a : b, a->count <= b->count ? b : a, input);
}

/* The line table of one registration of input i with the end of the input. */
static void one_shot(size_t i, snapshot_t *out)
{
    amark_document_t *document = new_document();
    amark_source_id_t source;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_add_source(document, split_inputs[i].bytes,
                                                  split_inputs[i].length,
                                                  AMARK_SOURCE_FINISHED, &source));
    take_snapshot(document, out);
    check_tree_ok(document);
    amark_document_destroy(document);
}

NAOS_TEST(byte_by_byte_appends_give_the_same_lines)
{
    static snapshot_t expected;
    static snapshot_t before;
    static snapshot_t after;
    size_t i;
    size_t k;

    for (i = 0; i < sizeof(split_inputs) / sizeof(split_inputs[0]); i++)
    {
        amark_document_t *document = new_document();
        amark_source_id_t source;

        one_shot(i, &expected);
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                            amark_document_add_source(document, NULL, 0, 0, &source));
        take_snapshot(document, &before);
        for (k = 0; k < split_inputs[i].length; k++)
        {
            NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                                amark_document_append_source(document, source,
                                                             split_inputs[i].bytes + k, 1));
            take_snapshot(document, &after);
            /* The completed lines never change, and they are those of the
             * one-shot registration. */
            check_prefix(&before, &after, i);
            check_prefix(&after, &expected, i);
            check_tree_ok(document);
            before = after;
        }
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_finish_source(document, source));
        take_snapshot(document, &after);
        check_prefix(&before, &after, i);
        check_same(&expected, &after, i);
        check_tree_ok(document);
        amark_document_destroy(document);
    }
}

NAOS_TEST(every_split_point_gives_the_same_lines)
{
    static snapshot_t expected;
    static snapshot_t split;
    size_t i;
    size_t at;
    size_t again;

    for (i = 0; i < sizeof(split_inputs) / sizeof(split_inputs[0]); i++)
    {
        const char *bytes = split_inputs[i].bytes;
        size_t length = split_inputs[i].length;

        one_shot(i, &expected);
        for (at = 0; at <= length; at++)
        {
            for (again = at; again <= length; again++)
            {
                amark_document_t *document = new_document();
                amark_source_id_t source;

                /* Three pieces, the last one with the end of the input
                 * notified separately. */
                NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                                    amark_document_add_source(document, bytes, at, 0, &source));
                NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                                    amark_document_append_source(document, source, bytes + at,
                                                                 again - at));
                NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                                    amark_document_append_source(document, source,
                                                                 bytes + again, length - again));
                NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                                    amark_document_finish_source(document, source));
                take_snapshot(document, &split);
                check_same(&expected, &split, i);
                check_tree_ok(document);
                amark_document_destroy(document);
            }
        }
    }
}

NAOS_TEST(interleaved_sources_number_their_own_lines)
{
    amark_document_t *document = new_document();
    amark_source_id_t first;
    amark_source_id_t second;
    const char *data = NULL;
    size_t length = 0;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_add_source(document, "a\r", 2, 0, &first));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_add_source(document, "x\ny", 3, 0, &second));
    NAOS_CHECK_UINT_EQ(0, first);
    NAOS_CHECK_UINT_EQ(1, second);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_append_source(document, first, "\nb\n", 3));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_append_source(document, second, "\n", 1));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_finish_source(document, first));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_finish_source(document, second));
    /* The line table is in the order of completion. */
    NAOS_REQUIRE_UINT_EQ(4, amark_document_line_count(document));
    check_line(document, 0, 1, 1, 0, 1, AMARK_EOL_LF, 1);
    check_line(document, 1, 0, 1, 0, 1, AMARK_EOL_CRLF, 1);
    check_line(document, 2, 0, 2, 3, 1, AMARK_EOL_LF, 1);
    check_line(document, 3, 1, 2, 2, 1, AMARK_EOL_LF, 1);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_source_data(document, first, &data, &length, NULL));
    AMARK_CHECK_BYTES_EQ(data, length, "a\r\nb\n");
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_source_data(document, second, &data, &length, NULL));
    AMARK_CHECK_BYTES_EQ(data, length, "x\ny\n");
    check_tree_ok(document);
    amark_document_destroy(document);
}

/* ---- The default line ending (2.8) ---------------------------------------------- */

NAOS_TEST(default_eol_is_the_first_line_ending)
{
    amark_document_t *document = new_document();
    amark_source_id_t source;

    NAOS_CHECK_INT_EQ(AMARK_EOL_LF, amark_document_default_eol(NULL));
    NAOS_CHECK_INT_EQ(AMARK_EOL_LF, amark_document_default_eol(document));
    /* A line without a line ending does not count. */
    ADD_FINISHED(document, "no ending", &source);
    NAOS_CHECK_INT_EQ(AMARK_EOL_LF, amark_document_default_eol(document));
    /* A CR that waits for the next byte is not a line yet. */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_add_source(document, "a\r", 2, 0, &source));
    NAOS_CHECK_INT_EQ(AMARK_EOL_LF, amark_document_default_eol(document));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_document_append_source(document, source, "\n", 1));
    NAOS_CHECK_INT_EQ(AMARK_EOL_CRLF, amark_document_default_eol(document));
    /* Later line endings do not change it. */
    ADD_FINISHED(document, "b\rc\n", &source);
    NAOS_CHECK_INT_EQ(AMARK_EOL_CRLF, amark_document_default_eol(document));
    amark_document_destroy(document);

    document = new_document();
    ADD_FINISHED(document, "a\rb\r\n", &source);
    NAOS_CHECK_INT_EQ(AMARK_EOL_CR, amark_document_default_eol(document));
    amark_document_destroy(document);
}

/* ---- Arguments ---------------------------------------------------------------------- */

NAOS_TEST(invalid_arguments_change_nothing)
{
    amark_document_t *document = new_document();
    amark_source_id_t source = 7;
    amark_line_info_t info;
    const char *data = NULL;

    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_document_add_source(NULL, "a", 1, 0, &source));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_document_add_source(document, "a", 1, 0, NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_document_add_source(document, NULL, 1, 0, &source));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_document_add_source(document, "a", 1, 0x02u, &source));
    NAOS_CHECK_UINT_EQ(7, source);
    NAOS_CHECK_UINT_EQ(0, amark_document_source_count(document));

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_add_source(document, "a\n", 2, 0, &source));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_document_append_source(NULL, source, "b", 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_document_append_source(document, source + 1, "b", 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_document_append_source(document, source, NULL, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_document_finish_source(NULL, source));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_document_finish_source(document, source + 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_document_source_data(NULL, source, &data, NULL, NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_document_source_data(document, source + 1, &data, NULL, NULL));
    NAOS_CHECK_NULL(data);

    info.struct_size = sizeof(info);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_document_line(NULL, 0, &info));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_document_line(document, 0, NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_document_line(document, 1, &info));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_document_line(document, AMARK_LINE_NONE, &info));
    info.struct_size = sizeof(info) - 1;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_document_line(document, 0, &info));

    NAOS_CHECK_UINT_EQ(0, amark_document_source_count(NULL));
    NAOS_CHECK_UINT_EQ(0, amark_document_line_count(NULL));
    NAOS_CHECK_UINT_EQ(1, amark_document_source_count(document));
    NAOS_CHECK_UINT_EQ(1, amark_document_line_count(document));
    check_tree_ok(document);
    amark_document_destroy(document);
}

/* ---- Release (1.3) and allocation failures (1.7) ------------------------------------ */

NAOS_TEST(destroy_frees_the_sources_and_the_lines)
{
    amark_document_t *document = NULL;
    amark_source_id_t source;
    amark_error_t error;
    size_t i;

    use_test_allocator();
    error = amark_document_create(NULL, &document);
    for (i = 0; i < 40 && error == AMARK_ERROR_NONE; i++)
    {
        error = amark_document_add_source(document, "line\r\nline\rline\n", 16, 0, &source);
        if (error == AMARK_ERROR_NONE && i % 2 == 0)
        {
            error = amark_document_append_source(document, source, "more\r", 5);
        }
        if (error == AMARK_ERROR_NONE && i % 3 == 0)
        {
            error = amark_document_finish_source(document, source);
        }
    }
    amark_document_destroy(document);
    (void)amark_allocator_set(NULL);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, error);
    NAOS_CHECK_UINT_EQ(0, test_allocator.live_blocks);
    NAOS_CHECK_UINT_EQ(0, test_allocator.live_bytes);
}

/* The state of a document that a failed call must keep. */
typedef struct state
{
    size_t source_count;
    size_t line_count;
    size_t length;
    uint8_t default_eol;
} state_t;

static state_t state_of(const amark_document_t *document, amark_source_id_t source)
{
    state_t state;

    state.source_count = document->source_count;
    state.line_count = document->line_count;
    state.length = source < document->source_count ? document->sources[source].length : 0;
    state.default_eol = document->default_eol;
    return state;
}

static int same_state(const amark_document_t *document, amark_source_id_t source,
                      state_t before)
{
    state_t after = state_of(document, source);

    return before.source_count == after.source_count && before.line_count == after.line_count &&
           before.length == after.length && before.default_eol == after.default_eol &&
           amark_tree_check((amark_document_t *)document, NULL) == AMARK_TREE_VIOLATION_NONE;
}

/* Every allocation of the scenario fails in turn: each failure returns
 * AMARK_ERROR_OUT_OF_MEMORY, keeps the sources and the line table as they were,
 * and leaks nothing. */
static amark_error_t oom_scenario(void *context)
{
    static const char text[] = "\xEF\xBB\xBF" "first\r\nsecond\tline\rthird";
    amark_document_t *document = NULL;
    amark_source_id_t source = 0;
    amark_source_id_t other = 0;
    amark_error_t error;
    state_t before;
    size_t i;

    (void)context;
    error = amark_document_create(NULL, &document);
    if (error != AMARK_ERROR_NONE)
    {
        return error;
    }
    before = state_of(document, 0);
    error = amark_document_add_source(document, text, 10, 0, &source);
    if (error != AMARK_ERROR_NONE && !same_state(document, 0, before))
    {
        error = AMARK_ERROR_INVALID_STATE;
    }
    /* Appends of a few bytes, so that the arrays grow several times. */
    for (i = 10; i < sizeof(text) - 1 && error == AMARK_ERROR_NONE; i += 3)
    {
        size_t n = sizeof(text) - 1 - i < 3 ? sizeof(text) - 1 - i : 3;

        before = state_of(document, source);
        error = amark_document_append_source(document, source, text + i, n);
        if (error != AMARK_ERROR_NONE && !same_state(document, source, before))
        {
            error = AMARK_ERROR_INVALID_STATE;
        }
    }
    if (error == AMARK_ERROR_NONE)
    {
        before = state_of(document, source);
        error = amark_document_finish_source(document, source);
        if (error != AMARK_ERROR_NONE &&
            (!same_state(document, source, before) || document->sources[source].finished))
        {
            error = AMARK_ERROR_INVALID_STATE;
        }
    }
    for (i = 0; i < 5 && error == AMARK_ERROR_NONE; i++)
    {
        before = state_of(document, 0);
        error = amark_document_add_source(document, "a\nb\nc\n", 6, AMARK_SOURCE_FINISHED,
                                          &other);
        if (error != AMARK_ERROR_NONE && !same_state(document, 0, before))
        {
            error = AMARK_ERROR_INVALID_STATE;
        }
    }
    if (error == AMARK_ERROR_NONE &&
        (document->line_count != 18 || document->default_eol != AMARK_EOL_CRLF))
    {
        error = AMARK_ERROR_INVALID_STATE;
    }
    amark_document_destroy(document);
    return error;
}

NAOS_TEST(out_of_memory_leaves_the_line_table_unchanged)
{
    AMARK_CHECK_OOM_SWEEP(oom_scenario, NULL);
}

/* ---- The internal check of the line table ---------------------------------------- */

static void check_lines_violation(amark_document_t *document)
{
    amark_tree_check_report_t report;

    NAOS_CHECK_STR_EQ("lines", amark_tree_violation_name(amark_tree_check(document, &report)));
    NAOS_CHECK_NOT_NULL(report.message);
}

NAOS_TEST(the_check_detects_a_broken_line_table)
{
    amark_document_t *document = new_document();
    amark_source_id_t source;
    amark_tree_line_t saved;
    uint8_t saved_eol;

    ADD_FINISHED(document, "\xEF\xBB\xBF" "a\tb\r\ncd\n", &source);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_document_add_source(document, "x\ny", 3, 0, &source));
    check_tree_ok(document);
    NAOS_REQUIRE_UINT_EQ(3, document->line_count);

    saved = document->lines[1];
    document->lines[1].width++;
    check_lines_violation(document);
    document->lines[1] = saved;
    document->lines[1].offset--;
    check_lines_violation(document);
    document->lines[1] = saved;
    document->lines[1].length++;
    check_lines_violation(document);
    document->lines[1] = saved;
    document->lines[1].eol = AMARK_EOL_CR;
    check_lines_violation(document);
    document->lines[1] = saved;
    document->lines[1].number = 3;
    check_lines_violation(document);
    document->lines[1] = saved;
    document->lines[1].source = 1;
    check_lines_violation(document);
    document->lines[1] = saved;
    /* The first line does not start after the BOM. */
    saved = document->lines[0];
    document->lines[0].offset = 0;
    document->lines[0].length = 6;
    check_lines_violation(document);
    document->lines[0] = saved;
    /* A line without a line ending before the end of the input. */
    saved = document->lines[2];
    document->lines[2].eol = AMARK_EOL_NONE;
    check_lines_violation(document);
    document->lines[2] = saved;
    /* A line of the table that no source lists. */
    document->sources[1].line_count--;
    check_lines_violation(document);
    document->sources[1].line_count++;
    /* The unfinished line does not follow the last completed one. */
    document->sources[1].line_start--;
    check_lines_violation(document);
    document->sources[1].line_start++;
    saved_eol = document->default_eol;
    document->default_eol = AMARK_EOL_LF;
    check_lines_violation(document);
    document->default_eol = saved_eol;
    check_tree_ok(document);
    amark_document_destroy(document);
}

NAOS_TEARDOWN(restore_allocator)
{
    (void)amark_allocator_set(NULL);
}

NAOS_SUITE(line_table,
    NAOS_SET_TEARDOWN(restore_allocator),
    NAOS_ADD_TEST(lf_crlf_cr_and_no_line_ending),
    NAOS_ADD_TEST(trailing_line_ending_and_empty_lines),
    NAOS_ADD_TEST(nul_and_other_bytes_are_ordinary),
    NAOS_ADD_TEST(widths_use_tab_stops_of_four),
    NAOS_ADD_TEST(source_bytes_are_copied),
    NAOS_ADD_TEST(bom_is_a_mark_of_the_source),
    NAOS_ADD_TEST(unfinished_lines_wait_for_the_next_bytes_or_the_end),
    NAOS_ADD_TEST(finished_sources_reject_appends),
    NAOS_ADD_TEST(byte_by_byte_appends_give_the_same_lines),
    NAOS_ADD_TEST(every_split_point_gives_the_same_lines),
    NAOS_ADD_TEST(interleaved_sources_number_their_own_lines),
    NAOS_ADD_TEST(default_eol_is_the_first_line_ending),
    NAOS_ADD_TEST(invalid_arguments_change_nothing),
    NAOS_ADD_TEST(destroy_frees_the_sources_and_the_lines),
    NAOS_ADD_TEST(out_of_memory_leaves_the_line_table_unchanged),
    NAOS_ADD_TEST(the_check_detects_a_broken_line_table)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(line_table)
)
