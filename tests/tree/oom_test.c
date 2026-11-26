/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The out-of-memory sweep of every operation of the syntax tree that allocates
 * (1.8, 4.11), in one place. Each scenario builds a shared fixture (a document of
 * two sources whose tokens cover them, every built-in attribute, an extension type
 * whose data owns a string, edit records with tracking on), takes a snapshot of its
 * targets, runs one operation and, when the operation returns
 * AMARK_ERROR_OUT_OF_MEMORY, compares the targets with the snapshot:
 *   - a document: its dump with AMARK_DUMP_TOKENS | AMARK_DUMP_EDITS, the number
 *     and the bytes of its sources with the position of their end, the number of
 *     the nodes of its ring (no node left behind, attached or not), its tracking
 *     and amark_tree_check;
 *   - a syntax set: its extensions in order and its frozen state;
 *   - an output: the pointer set to NULL, the source identifier not written, the
 *     buffer of the dump with its earlier bytes, the report of the lossless check
 *     NONE.
 * The fixture is built inside each scenario (its own allocations are swept too and
 * the scenario frees everything it made), and every run must leave no block. On a
 * success, the invariants are checked and the result compared with its original
 * where there is one. Each case must see at least one failure inside the
 * operation itself, so that the sweep reaches it.
 *
 * Suite oom_documents: the creation of documents, the addition of sources and
 * the appending to a source (one ending with CR too), the creation of nodes.
 * Suite oom_content: literals, string attributes, inline text, the addition and
 * the transfer of tokens.
 * Suite oom_copies: copies, imports into another syntax set, imports of children
 * into another document and into the same one.
 * Suite oom_syntax_sets: the appending of an extension, freezing, cloning.
 * Suite oom_reading: the creation of an iterator, the dump, the lossless check.
 */
#include "tree/syntax_set.h"
#include "tree/tree.h"

#include <amark/allocator.h>
#include <amark/buffer.h>
#include <amark/document.h>
#include <amark/dump.h>
#include <amark/error.h>
#include <amark/extension.h>
#include <amark/iterator.h>
#include <amark/node.h>
#include <amark/token.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <naos/naos.h>

#include "test_allocator.h"
#include "tree_builder.h"

NAOS_TEARDOWN(restore)
{
    amark_allocator_set(NULL);
}

/* --- The extension types ------------------------------------------------------- */

/* The data of o.box: a label it owns (NULL is none). */
typedef struct box_data
{
    char *label;
    size_t length;
} box_data_t;

static amark_error_t box_set_label(box_data_t *data, const char *text, size_t length)
{
    char *label = amark_malloc(length + 1);

    if (label == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }

    memcpy(label, text, length);
    label[length] = '\0';
    amark_free(data->label);
    data->label = label;
    data->length = length;

    return AMARK_ERROR_NONE;
}

static void box_deinit(amark_node_t *node, void *data)
{
    (void)node;
    amark_free(((box_data_t *)data)->label);
}

static amark_error_t box_copy(amark_node_t *dst, void *dst_data, const amark_node_t *src,
                              const void *src_data)
{
    const box_data_t *from = src_data;

    (void)dst;
    (void)src;
    if (from->label == NULL)
    {
        return AMARK_ERROR_NONE;
    }

    return box_set_label(dst_data, from->label, from->length);
}

static bool box_equal(const amark_node_t *a, const void *a_data, const amark_node_t *b,
                      const void *b_data)
{
    const box_data_t *x = a_data;
    const box_data_t *y = b_data;

    (void)a;
    (void)b;
    if ((x->label == NULL) || (y->label == NULL))
    {
        return (x->label == y->label);
    }

    return (x->length == y->length) && (memcmp(x->label, y->label, x->length) == 0);
}

static amark_error_t box_dump(const amark_node_t *node, const void *data,
                              amark_dump_writer_t *writer)
{
    const box_data_t *box = data;

    (void)node;
    if (box->label == NULL)
    {
        return AMARK_ERROR_NONE;
    }

    return amark_dump_attr_string(writer, "label", box->label, box->length);
}

/* A block that holds blocks, with an owned label. */
static const amark_node_type_desc_t box_type = {
    sizeof(amark_node_type_desc_t), "o.box", AMARK_NODE_LEVEL_BLOCK, AMARK_CONTENT_BLOCK,
    AMARK_CONTENT_BLOCK, AMARK_LITERAL_NONE, sizeof(box_data_t), NULL, NULL, NULL, box_deinit,
    box_copy, box_equal, box_dump, NULL
};

/* A leaf block, placed before o.box in the set of the import destination so that
 * o.box has another identifier there. */
static const amark_node_type_desc_t pad_type = {
    sizeof(amark_node_type_desc_t), "p.pad", AMARK_NODE_LEVEL_BLOCK, AMARK_CONTENT_BLOCK, 0,
    AMARK_LITERAL_NONE, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

static const amark_node_type_desc_t *const o_types[] = { &box_type };

static const amark_extension_t o_extension = {
    sizeof(amark_extension_t), "o", o_types, 1, NULL, 0, NULL
};

static const amark_node_type_desc_t *const p_types[] = { &pad_type };

static const amark_extension_t p_extension = {
    sizeof(amark_extension_t), "p", p_types, 1, NULL, 0, NULL
};

/* The attribute function of the builder: :label of o.box. */
static amark_error_t box_attr(amark_node_t *node, const char *name, const char *value,
                              size_t length, void *context)
{
    (void)context;
    if ((amark_node_desc(node) != &box_type) || (strcmp(name, "label") != 0))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    return box_set_label(amark_node_data(node), value, length);
}

/* Creates in *out_set a syntax set of amark.core and the extensions (NULL ends
 * them), frozen when freeze is true. */
static amark_error_t create_set(const amark_extension_t *first, const amark_extension_t *second,
                                bool freeze, amark_syntax_set_t **out_set)
{
    amark_syntax_set_t *set = NULL;
    amark_error_t err = amark_syntax_set_create(&set);

    if ((err == AMARK_ERROR_NONE) && (first != NULL))
    {
        err = amark_syntax_set_append(set, first);
    }

    if ((err == AMARK_ERROR_NONE) && (second != NULL))
    {
        err = amark_syntax_set_append(set, second);
    }

    if ((err == AMARK_ERROR_NONE) && freeze)
    {
        err = amark_syntax_set_freeze(set);
    }

    if (err != AMARK_ERROR_NONE)
    {
        amark_syntax_set_destroy(set);
        set = NULL;
    }

    *out_set = set;
    return err;
}

/* --- The fixture ------------------------------------------------------------------- */

/* Source 0: an ATX heading, a blank line and a block quote ending with CRLF. */
static const char source0[] = "# Hi\n\n> a\r\n";

/* Source 1: a fenced code block. */
static const char source1[] = "```c\ncode\n```\n";

/* The tokens of the two sources cover every byte once (lossless). */
static const char fixture_tree[] =
    "(document :tokens ((line_ending 5 1))\n"
    "  (heading :level 1 :tokens ((marker 0 1) (space 1 1) (line_ending 4 1))\n"
    "    (text :tokens ((text 2 2)) \"Hi\"))\n"
    "  (block_quote :tokens ((marker 6 1 prefix) (space 7 1 prefix))\n"
    "    (paragraph :tokens ((line_ending 9 2))\n"
    "      (text :tokens ((text 8 1)) \"a\")))\n"
    "  (o.box :label \"b\\\"ox\"\n"
    "    (code_block :info \"c\" :tokens ((marker 0 3 :source 1) (text 3 1 :source 1)"
    " (line_ending 4 1 :source 1) (text 5 4 :source 1) (line_ending 9 1 :source 1)"
    " (marker 10 3 :source 1) (line_ending 13 1 :source 1)) \"code\\n\"))\n"
    "  (list :type ordered :start 3 :tight true\n"
    "    (item\n"
    "      (paragraph (text \"x\") (emph (text \"y\"))\n"
    "        (link :destination \"/u\" :title \"t\" (text \"l\")) (softbreak) (code \"k\")"
    " (html_inline \"<b>\"))))\n"
    "  (link_definition :label \"foo\" :destination \"/d\" :title \"T\")\n"
    "  (thematic_break)\n"
    "  (html_block \"<p>\\n\"))";

typedef struct fixture
{
    amark_syntax_set_t *set;
    amark_document_t *document;
    amark_node_t *root;
    amark_node_t *heading;
    amark_node_t *heading_text;
    amark_node_t *quote;
    amark_node_t *box;
    amark_node_t *code_block;
    amark_node_t *list;
    amark_node_t *item;
    amark_node_t *item_paragraph;
    amark_node_t *link;
    amark_node_t *definition;
    amark_node_t *html;
} fixture_t;

static void fixture_free(fixture_t *f)
{
    amark_document_destroy(f->document);
    amark_syntax_set_destroy(f->set);
    memset(f, 0, sizeof(*f));
}

/* Builds the fixture: the frozen set of amark.core and o, the document with its
 * sources and tree, then tracking on and one edit (records on the heading and the
 * root). On a failure, frees what it made. When out_reason is not NULL, it gets the
 * reason of a failed build (to free with amark_test_tree_free_reason). */
static amark_error_t fixture_build(fixture_t *f, const char **out_reason)
{
    amark_source_id_t id;
    amark_node_t *node = NULL;
    const char *reason = NULL;
    amark_error_t err;

    memset(f, 0, sizeof(*f));
    err = create_set(&o_extension, NULL, true, &f->set);
    if (err == AMARK_ERROR_NONE)
    {
        err = amark_document_create(f->set, &f->document);
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = amark_document_add_source(f->document, source0, sizeof(source0) - 1, &id);
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = amark_document_add_source(f->document, source1, sizeof(source1) - 1, &id);
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = amark_test_tree_build(f->document, fixture_tree, box_attr, NULL, &node, &reason);
        if (out_reason != NULL)
        {
            *out_reason = reason;
        }
        else
        {
            amark_test_tree_free_reason(reason);
        }
    }

    if (err != AMARK_ERROR_NONE)
    {
        fixture_free(f);
        return err;
    }

    f->root = amark_document_root(f->document);
    f->heading = amark_node_first_child(f->root);
    f->heading_text = amark_node_first_child(f->heading);
    f->quote = amark_node_next(f->heading);
    f->box = amark_node_next(f->quote);
    f->code_block = amark_node_first_child(f->box);
    f->list = amark_node_next(f->box);
    f->item = amark_node_first_child(f->list);
    f->item_paragraph = amark_node_first_child(f->item);
    f->link = amark_node_next(amark_node_next(amark_node_first_child(f->item_paragraph)));
    f->definition = amark_node_next(f->list);
    f->html = amark_node_last_child(f->root);
    amark_document_set_edit_tracking(f->document, true);
    err = amark_node_set_heading_level(f->heading, 2);
    if (err != AMARK_ERROR_NONE)
    {
        fixture_free(f);
    }

    return err;
}

/* --- Snapshots ------------------------------------------------------------------------ */

static size_t ring_count(const amark_document_t *document)
{
    const amark_node_t *node;
    size_t count = 1;

    if (document->ring == NULL)
    {
        return 0;
    }

    for (node = document->ring->ring_next; node != document->ring; node = node->ring_next)
    {
        count++;
    }

    return count;
}

static amark_error_t append_number(amark_buffer_t *out, const char *label, size_t value)
{
    char text[64];

    snprintf(text, sizeof(text), "\n%s %lu", label, (unsigned long)value);

    return amark_buffer_append_cstr(out, text);
}

/* Writes the state of document that an operation must keep on a failure. */
static amark_error_t write_state(const amark_document_t *document, amark_buffer_t *out)
{
    const char *reason = NULL;
    size_t count = amark_document_source_count(document);
    size_t i;
    amark_error_t err;

    err = amark_node_dump(amark_document_root(document), AMARK_DUMP_TOKENS | AMARK_DUMP_EDITS,
                          out);
    if (err == AMARK_ERROR_NONE)
    {
        err = append_number(out, "sources", count);
    }

    for (i = 0; (err == AMARK_ERROR_NONE) && (i < count); i++)
    {
        size_t length = 0;
        size_t line = 0;
        size_t column = 0;
        const char *data = amark_document_source_data(document, (amark_source_id_t)i, &length);

        err = append_number(out, "source", length);
        if (err == AMARK_ERROR_NONE)
        {
            err = amark_buffer_append(out, data, length);
        }

        if (err == AMARK_ERROR_NONE)
        {
            (void)amark_document_position(document, (amark_source_id_t)i, length, &line, &column);
            err = append_number(out, "end line", line);
        }

        if (err == AMARK_ERROR_NONE)
        {
            err = append_number(out, "end column", column);
        }
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = append_number(out, "ring", ring_count(document));
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = append_number(out, "tracking", amark_document_edit_tracking(document) ? 1u : 0u);
    }

    if (err == AMARK_ERROR_NONE)
    {
        (void)amark_tree_check(document, &reason);
        err = amark_buffer_append_cstr(out, "\ncheck ");
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = amark_buffer_append_cstr(out, (reason != NULL) ? reason : "ok");
    }

    return err;
}

static amark_error_t snapshot_take(const amark_document_t *document, amark_buffer_t **out)
{
    amark_buffer_t *buffer = NULL;
    amark_error_t err = amark_buffer_create(&buffer);

    if (err == AMARK_ERROR_NONE)
    {
        err = write_state(document, buffer);
    }

    if (err != AMARK_ERROR_NONE)
    {
        amark_buffer_destroy(buffer);
        buffer = NULL;
    }

    *out = buffer;
    return err;
}

/* True when document is as snapshot says (the failure injected is already used up,
 * so the new snapshot does not fail). */
static bool snapshot_same(const amark_document_t *document, const amark_buffer_t *snapshot)
{
    amark_buffer_t *now = NULL;
    bool same = false;

    if (snapshot_take(document, &now) == AMARK_ERROR_NONE)
    {
        same = (amark_buffer_length(now) == amark_buffer_length(snapshot))
               && (memcmp(amark_buffer_data(now), amark_buffer_data(snapshot),
                          amark_buffer_length(now)) == 0);
    }

    amark_buffer_destroy(now);
    return same;
}

#define MAX_EXTENSIONS 4u

/* The extensions of a syntax set in order and its frozen state. */
typedef struct set_state
{
    size_t count;
    const amark_extension_t *extensions[MAX_EXTENSIONS];
    bool frozen;
} set_state_t;

static void set_state_take(const amark_syntax_set_t *set, set_state_t *out)
{
    size_t i;

    memset(out, 0, sizeof(*out));
    out->count = amark_syntax_set_extension_count(set);
    out->frozen = amark_syntax_set_is_frozen(set);
    for (i = 0; (i < out->count) && (i < MAX_EXTENSIONS); i++)
    {
        out->extensions[i] = amark_syntax_set_extension_at(set, i);
    }
}

static bool set_state_same(const amark_syntax_set_t *set, const set_state_t *state)
{
    set_state_t now;

    set_state_take(set, &now);

    return (now.count == state->count) && (now.frozen == state->frozen)
           && (memcmp(now.extensions, state->extensions, sizeof(now.extensions)) == 0);
}

/* --- The cases -------------------------------------------------------------------------- */

/* The objects of one run of a case, besides the fixture. */
typedef struct op
{
    amark_document_t *document;    /* a created document (destroyed) */
    amark_node_t *node;            /* a created node, copy or import (in its document) */
    amark_iter_t *iter;            /* a created iterator (destroyed) */
    amark_buffer_t *buffer;        /* the output of the dump (destroyed) */
    amark_syntax_set_t *set;       /* a syntax set made by prepare (destroyed) */
    amark_syntax_set_t *clone;     /* a cloned syntax set (destroyed) */
    amark_syntax_set_t *other_set; /* the set of other (destroyed after it) */
    amark_document_t *other;       /* a second document, a target too (destroyed) */
    amark_node_t *other_before;    /* a child of the root of other */
    const amark_syntax_set_t *watched; /* a syntax set that must not change */
    amark_source_id_t source;      /* an added source */
    amark_token_report_t report;   /* the report of the lossless check */
    amark_buffer_t *snapshot;      /* the snapshot of the fixture document */
    amark_buffer_t *other_snapshot;
    set_state_t set_state;
} op_t;

typedef struct oom_case oom_case_t;

struct oom_case
{
    const char *name;
    /* Makes the other objects of the run (allocations swept too); optional. */
    amark_error_t (*prepare)(fixture_t *f, op_t *op);
    /* The operation under test. */
    amark_error_t (*run)(fixture_t *f, op_t *op);
    /* The checks of the outputs after a failure, besides the snapshots; optional. */
    bool (*failed_cleanly)(fixture_t *f, op_t *op);
    /* The checks of the result after a success, besides the invariants; optional. */
    bool (*succeeded)(fixture_t *f, op_t *op);
    /* The counters of the sweep. */
    size_t op_failures;
    size_t mismatches;
    const char *first_mismatch;
};

/* A recognizable value of the output pointers, which a failure must set to NULL. */
static union
{
    long double ld;
    long long ll;
    void *p;
} sentinel;

#define SENTINEL(type) ((type *)(void *)&sentinel)
#define SOURCE_SENTINEL ((amark_source_id_t)12345u)

static void mismatch(oom_case_t *c, const char *what)
{
    if (c->first_mismatch == NULL)
    {
        c->first_mismatch = what;
    }

    c->mismatches++;
}

/* The outputs still at the sentinel are not objects. */
static void clear_sentinels(op_t *op)
{
    if (op->document == SENTINEL(amark_document_t))
    {
        op->document = NULL;
    }

    if (op->node == SENTINEL(amark_node_t))
    {
        op->node = NULL;
    }

    if (op->iter == SENTINEL(amark_iter_t))
    {
        op->iter = NULL;
    }

    if (op->clone == SENTINEL(amark_syntax_set_t))
    {
        op->clone = NULL;
    }
}

static void op_free(op_t *op)
{
    amark_iter_destroy(op->iter);
    amark_buffer_destroy(op->buffer);
    amark_document_destroy(op->document);
    amark_document_destroy(op->other);
    amark_syntax_set_destroy(op->other_set);
    amark_syntax_set_destroy(op->set);
    amark_syntax_set_destroy(op->clone);
    amark_buffer_destroy(op->snapshot);
    amark_buffer_destroy(op->other_snapshot);
}

/* Takes the snapshots of the targets of op. */
static amark_error_t take_snapshots(fixture_t *f, op_t *op)
{
    amark_error_t err = snapshot_take(f->document, &op->snapshot);

    if ((err == AMARK_ERROR_NONE) && (op->other != NULL))
    {
        err = snapshot_take(op->other, &op->other_snapshot);
    }

    if (op->watched != NULL)
    {
        set_state_take(op->watched, &op->set_state);
    }

    return err;
}

/* After a failure of the operation: every target as before. */
static void check_failure(oom_case_t *c, fixture_t *f, op_t *op)
{
    c->op_failures++;
    if (!snapshot_same(f->document, op->snapshot))
    {
        mismatch(c, "the fixture document changed");
    }

    if ((op->other != NULL) && !snapshot_same(op->other, op->other_snapshot))
    {
        mismatch(c, "the other document changed");
    }

    if ((op->watched != NULL) && !set_state_same(op->watched, &op->set_state))
    {
        mismatch(c, "the syntax set changed");
    }

    if ((c->failed_cleanly != NULL) && !c->failed_cleanly(f, op))
    {
        mismatch(c, "an output was written");
    }
}

static void check_success(oom_case_t *c, fixture_t *f, op_t *op)
{
    if (amark_tree_check(f->document, NULL) != AMARK_ERROR_NONE)
    {
        mismatch(c, "the fixture document is broken after a success");
    }

    if ((op->other != NULL) && (amark_tree_check(op->other, NULL) != AMARK_ERROR_NONE))
    {
        mismatch(c, "the other document is broken after a success");
    }

    if ((c->succeeded != NULL) && !c->succeeded(f, op))
    {
        mismatch(c, "the result of the success is wrong");
    }
}

/* The scenario of the sweep: context is the oom_case_t. */
static amark_error_t run_case(void *context)
{
    oom_case_t *c = context;
    fixture_t f;
    op_t op;
    amark_error_t err;

    memset(&op, 0, sizeof(op));
    op.source = SOURCE_SENTINEL;
    op.report.struct_size = sizeof(op.report);
    err = fixture_build(&f, NULL);
    if ((err == AMARK_ERROR_NONE) && (c->prepare != NULL))
    {
        err = c->prepare(&f, &op);
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = take_snapshots(&f, &op);
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = c->run(&f, &op);
        if (err == AMARK_ERROR_OUT_OF_MEMORY)
        {
            check_failure(c, &f, &op);
        }
        else if (err == AMARK_ERROR_NONE)
        {
            check_success(c, &f, &op);
        }

        clear_sentinels(&op);
    }

    op_free(&op);
    fixture_free(&f);
    return err;
}

static void check_case(oom_case_t *c)
{
    c->op_failures = 0;
    c->mismatches = 0;
    c->first_mismatch = NULL;
    NAOS_TRACE_PUSH("case %s", c->name);
    AMARK_CHECK_OOM_SWEEP(run_case, c);
    NAOS_NOTE("%s", (c->first_mismatch != NULL) ? c->first_mismatch : "");
    NAOS_CHECK_UINT_EQ(0, c->mismatches);
    /* The sweep reached the operation itself. */
    NAOS_CHECK_TRUE(c->op_failures > 0);
    NAOS_TRACE_POP();
}

/* The common output checks. */
static bool document_is_null(fixture_t *f, op_t *op)
{
    (void)f;
    return (op->document == NULL);
}

static bool node_is_null(fixture_t *f, op_t *op)
{
    (void)f;
    return (op->node == NULL);
}

/* --- Suite oom_documents ---------------------------------------------------------- */

static amark_error_t create_core_document(fixture_t *f, op_t *op)
{
    (void)f;
    op->document = SENTINEL(amark_document_t);

    return amark_document_create(NULL, &op->document);
}

static amark_error_t create_extended_document(fixture_t *f, op_t *op)
{
    op->document = SENTINEL(amark_document_t);

    return amark_document_create(f->set, &op->document);
}

static bool document_is_empty(fixture_t *f, op_t *op)
{
    (void)f;
    return (op->document != NULL) && (amark_node_first_child(amark_document_root(op->document))
                                      == NULL)
           && (amark_document_source_count(op->document) == 0)
           && (amark_tree_check(op->document, NULL) == AMARK_ERROR_NONE);
}

static amark_error_t add_source(fixture_t *f, op_t *op)
{
    static const char text[] = "new\r\nsource\rof\nlines";

    return amark_document_add_source(f->document, text, sizeof(text) - 1, &op->source);
}

static bool source_not_written(fixture_t *f, op_t *op)
{
    (void)f;
    return (op->source == SOURCE_SENTINEL);
}

static bool source_added(fixture_t *f, op_t *op)
{
    return (op->source == 2) && (amark_document_source_count(f->document) == 3);
}

static amark_error_t append_source(fixture_t *f, op_t *op)
{
    static const char text[] = "more\r\nlines\nof\rtext and a longer tail to grow the buffer\n";

    (void)op;
    return amark_document_append_source(f->document, 0, text, sizeof(text) - 1);
}

/* Adds a third source ending with CR, so that the appended LF makes a CRLF. */
static amark_error_t prepare_cr_source(fixture_t *f, op_t *op)
{
    return amark_document_add_source(f->document, "a\r", 2, &op->source);
}

static amark_error_t append_after_cr(fixture_t *f, op_t *op)
{
    return amark_document_append_source(f->document, op->source, "\nb\nc\r\n", 6);
}

static amark_error_t create_paragraph(fixture_t *f, op_t *op)
{
    op->node = SENTINEL(amark_node_t);

    return amark_node_create(f->document, AMARK_NODE_PARAGRAPH, &op->node);
}

static amark_error_t create_box(fixture_t *f, op_t *op)
{
    op->node = SENTINEL(amark_node_t);

    return amark_node_create(f->document, amark_node_get_type(f->box), &op->node);
}

static oom_case_t create_core_document_case = {
    "document create (core)", NULL, create_core_document, document_is_null, document_is_empty,
    0, 0, NULL
};

static oom_case_t create_extended_document_case = {
    "document create (extension set)", NULL, create_extended_document, document_is_null,
    document_is_empty, 0, 0, NULL
};

static oom_case_t add_source_case = {
    "source add", NULL, add_source, source_not_written, source_added, 0, 0, NULL
};

static oom_case_t append_source_case = {
    "source append", NULL, append_source, NULL, NULL, 0, 0, NULL
};

static oom_case_t append_after_cr_case = {
    "source append after CR", prepare_cr_source, append_after_cr, NULL, NULL, 0, 0, NULL
};

static oom_case_t create_paragraph_case = {
    "node create (built-in)", NULL, create_paragraph, node_is_null, NULL, 0, 0, NULL
};

static oom_case_t create_box_case = {
    "node create (extension)", NULL, create_box, node_is_null, NULL, 0, 0, NULL
};

NAOS_TEST(the_fixture_builds_and_is_lossless)
{
    fixture_t f;
    amark_token_report_t report;
    const char *reason = NULL;
    amark_error_t err = fixture_build(&f, &reason);

    NAOS_NOTE("%s", (reason != NULL) ? reason : "");
    amark_test_tree_free_reason(reason);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, err);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_tree_check(f.document, NULL));
    NAOS_CHECK_STR_EQ("o.box", amark_node_type_name(f.box));
    NAOS_CHECK_STR_EQ("link", amark_node_type_name(f.link));
    NAOS_CHECK_STR_EQ("link_definition", amark_node_type_name(f.definition));
    NAOS_CHECK_STR_EQ("html_block", amark_node_type_name(f.html));
    memset(&report, 0, sizeof(report));
    report.struct_size = sizeof(report);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_check_tokens(f.document, 0, &report));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_document_check_tokens(f.document, 1, &report));
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_DESCENDANTS, amark_node_edits(f.root));
    NAOS_CHECK_UINT_EQ(AMARK_EDIT_CONTENT, amark_node_edits(f.heading));
    fixture_free(&f);
}

NAOS_TEST(document_creation_survives_every_allocation_failure)
{
    check_case(&create_core_document_case);
    check_case(&create_extended_document_case);
}

NAOS_TEST(sources_survive_every_allocation_failure)
{
    check_case(&add_source_case);
    check_case(&append_source_case);
    check_case(&append_after_cr_case);
}

NAOS_TEST(node_creation_survives_every_allocation_failure)
{
    check_case(&create_paragraph_case);
    check_case(&create_box_case);
}

NAOS_SUITE(oom_documents,
    NAOS_SET_TEARDOWN(restore),
    NAOS_ADD_TEST(the_fixture_builds_and_is_lossless),
    NAOS_ADD_TEST(document_creation_survives_every_allocation_failure),
    NAOS_ADD_TEST(sources_survive_every_allocation_failure),
    NAOS_ADD_TEST(node_creation_survives_every_allocation_failure))

/* --- Suite oom_content ------------------------------------------------------------- */

static amark_error_t set_text_literal(fixture_t *f, op_t *op)
{
    (void)op;
    return amark_node_set_literal(f->heading_text, "Hello", 5);
}

static amark_error_t set_code_block_literal(fixture_t *f, op_t *op)
{
    (void)op;
    return amark_node_set_literal(f->code_block, "one\r\ntwo\rthree", 14);
}

static amark_error_t set_code_info(fixture_t *f, op_t *op)
{
    (void)op;
    return amark_node_set_code_info(f->code_block, "rust", 4);
}

static amark_error_t set_link_destination(fixture_t *f, op_t *op)
{
    (void)op;
    return amark_node_set_link_destination(f->link, "/v/w", 4);
}

static amark_error_t set_link_title(fixture_t *f, op_t *op)
{
    (void)op;
    return amark_node_set_link_title(f->link, "a\r\nb", 4);
}

static amark_error_t set_definition_label(fixture_t *f, op_t *op)
{
    (void)op;
    return amark_node_set_link_label(f->definition, "bar \\]", 6);
}

static amark_error_t set_definition_destination(fixture_t *f, op_t *op)
{
    (void)op;
    return amark_node_set_link_destination(f->definition, "/e", 2);
}

static amark_error_t set_definition_title(fixture_t *f, op_t *op)
{
    (void)op;
    return amark_node_set_link_title(f->definition, "U", 1);
}

static amark_error_t set_paragraph_inline_text(fixture_t *f, op_t *op)
{
    (void)op;
    return amark_node_set_inline_text(f->item_paragraph, "a\nb\r\nc\rd", 8);
}

static amark_error_t set_heading_inline_text(fixture_t *f, op_t *op)
{
    (void)op;
    return amark_node_set_inline_text(f->heading, "New", 3);
}

static amark_token_t make_token(amark_token_kind_t kind, amark_source_id_t source, size_t offset,
                                size_t length)
{
    amark_token_t token;

    memset(&token, 0, sizeof(token));
    token.struct_size = sizeof(token);
    token.kind = kind;
    token.source = source;
    token.offset = offset;
    token.length = length;

    return token;
}

/* The first token of a node without tokens. */
static amark_error_t add_first_token(fixture_t *f, op_t *op)
{
    amark_token_t token = make_token(AMARK_TOKEN_TEXT, 0, 8, 1);

    (void)op;
    return amark_node_add_token(f->html, &token);
}

/* Adds zero-length lazy continuation tokens at the end of source 1 to node until
 * its token array is full, so that the next token grows it. */
static amark_error_t fill_tokens(amark_node_t *node)
{
    amark_token_t token = make_token(AMARK_TOKEN_LAZY_CONTINUATION, 1, sizeof(source1) - 1, 0);
    amark_error_t err = AMARK_ERROR_NONE;

    while ((err == AMARK_ERROR_NONE) && (node->token_count < node->token_capacity))
    {
        err = amark_node_add_token(node, &token);
    }

    return err;
}

static amark_error_t prepare_full_code_block(fixture_t *f, op_t *op)
{
    (void)op;
    return fill_tokens(f->code_block);
}

static amark_error_t prepare_full_heading_text(fixture_t *f, op_t *op)
{
    (void)op;
    return fill_tokens(f->heading_text);
}

/* A token before the tokens of the code block, whose array is full. */
static amark_error_t add_front_token(fixture_t *f, op_t *op)
{
    amark_token_t token = make_token(AMARK_TOKEN_MARKER, 0, 0, 1);

    (void)op;
    return amark_node_add_token(f->code_block, &token);
}

/* The three tokens of the heading, merged into those of the block quote. */
static amark_error_t transfer_heading_tokens(fixture_t *f, op_t *op)
{
    (void)op;
    return amark_node_transfer_tokens(f->heading, 0, 3, f->quote);
}

/* Two tokens of the code block (source 1) to a text node (source 0) whose array is
 * full. */
static amark_error_t transfer_code_tokens(fixture_t *f, op_t *op)
{
    (void)op;
    return amark_node_transfer_tokens(f->code_block, 2, 2, f->heading_text);
}

static oom_case_t set_text_literal_case = {
    "literal (text)", NULL, set_text_literal, NULL, NULL, 0, 0, NULL
};

static oom_case_t set_code_block_literal_case = {
    "literal (code block)", NULL, set_code_block_literal, NULL, NULL, 0, 0, NULL
};

static oom_case_t attribute_cases[] = {
    { "code info", NULL, set_code_info, NULL, NULL, 0, 0, NULL },
    { "link destination", NULL, set_link_destination, NULL, NULL, 0, 0, NULL },
    { "link title", NULL, set_link_title, NULL, NULL, 0, 0, NULL },
    { "definition label", NULL, set_definition_label, NULL, NULL, 0, 0, NULL },
    { "definition destination", NULL, set_definition_destination, NULL, NULL, 0, 0, NULL },
    { "definition title", NULL, set_definition_title, NULL, NULL, 0, 0, NULL }
};

static oom_case_t inline_text_cases[] = {
    { "inline text (paragraph)", NULL, set_paragraph_inline_text, NULL, NULL, 0, 0, NULL },
    { "inline text (heading)", NULL, set_heading_inline_text, NULL, NULL, 0, 0, NULL }
};

static oom_case_t token_cases[] = {
    { "token add (first)", NULL, add_first_token, NULL, NULL, 0, 0, NULL },
    { "token add (front)", prepare_full_code_block, add_front_token, NULL, NULL, 0, 0, NULL },
    { "token transfer (merge)", NULL, transfer_heading_tokens, NULL, NULL, 0, 0, NULL },
    { "token transfer (other source)", prepare_full_heading_text, transfer_code_tokens, NULL,
      NULL, 0, 0, NULL }
};

#define COUNT_OF(array) (sizeof(array) / sizeof((array)[0]))

NAOS_TEST(literals_survive_every_allocation_failure)
{
    check_case(&set_text_literal_case);
    check_case(&set_code_block_literal_case);
}

NAOS_TEST(string_attributes_survive_every_allocation_failure)
{
    size_t i;

    for (i = 0; i < COUNT_OF(attribute_cases); i++)
    {
        check_case(&attribute_cases[i]);
    }
}

NAOS_TEST(inline_text_survives_every_allocation_failure)
{
    size_t i;

    for (i = 0; i < COUNT_OF(inline_text_cases); i++)
    {
        check_case(&inline_text_cases[i]);
    }
}

NAOS_TEST(tokens_survive_every_allocation_failure)
{
    size_t i;

    for (i = 0; i < COUNT_OF(token_cases); i++)
    {
        check_case(&token_cases[i]);
    }
}

NAOS_SUITE(oom_content,
    NAOS_SET_TEARDOWN(restore),
    NAOS_ADD_TEST(literals_survive_every_allocation_failure),
    NAOS_ADD_TEST(string_attributes_survive_every_allocation_failure),
    NAOS_ADD_TEST(inline_text_survives_every_allocation_failure),
    NAOS_ADD_TEST(tokens_survive_every_allocation_failure))

/* --- Suite oom_copies --------------------------------------------------------------- */

static amark_error_t copy_list(fixture_t *f, op_t *op)
{
    op->node = SENTINEL(amark_node_t);

    return amark_node_copy(f->list, &op->node);
}

static amark_error_t copy_box(fixture_t *f, op_t *op)
{
    op->node = SENTINEL(amark_node_t);

    return amark_node_copy(f->box, &op->node);
}

static amark_error_t copy_quote(fixture_t *f, op_t *op)
{
    op->node = SENTINEL(amark_node_t);

    return amark_node_copy(f->quote, &op->node);
}

static bool copy_of_list(fixture_t *f, op_t *op)
{
    return amark_node_equal(f->list, op->node, 0);
}

static bool copy_of_box(fixture_t *f, op_t *op)
{
    return amark_node_equal(f->box, op->node, 0);
}

static bool copy_of_quote(fixture_t *f, op_t *op)
{
    return amark_node_equal(f->quote, op->node, 0)
           && (amark_node_token_count(op->node) == amark_node_token_count(f->quote));
}

/* A second document in the set of amark.core, p and o (o.box has another
 * identifier there), with a source and a paragraph, tracking on. */
static amark_error_t prepare_other(fixture_t *f, op_t *op)
{
    amark_source_id_t id;
    amark_error_t err = create_set(&p_extension, &o_extension, true, &op->other_set);

    (void)f;
    if (err == AMARK_ERROR_NONE)
    {
        err = amark_document_create(op->other_set, &op->other);
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = amark_document_add_source(op->other, "pre", 3, &id);
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = amark_node_create(op->other, AMARK_NODE_PARAGRAPH, &op->other_before);
    }

    if (err == AMARK_ERROR_NONE)
    {
        err = amark_node_append_child(amark_document_root(op->other), op->other_before);
    }

    if (err == AMARK_ERROR_NONE)
    {
        amark_document_set_edit_tracking(op->other, true);
    }

    return err;
}

static amark_error_t import_box(fixture_t *f, op_t *op)
{
    op->node = SENTINEL(amark_node_t);

    return amark_node_import(op->other, f->box, &op->node);
}

static amark_error_t import_quote(fixture_t *f, op_t *op)
{
    op->node = SENTINEL(amark_node_t);

    return amark_node_import(op->other, f->quote, &op->node);
}

static amark_error_t import_list(fixture_t *f, op_t *op)
{
    op->node = SENTINEL(amark_node_t);

    return amark_node_import(op->other, f->list, &op->node);
}

/* The import has the type of the destination set and the referenced source. */
static bool import_of_box(fixture_t *f, op_t *op)
{
    return amark_node_equal(f->box, op->node, 0)
           && (amark_node_get_type(op->node) != amark_node_get_type(f->box))
           && (amark_document_source_count(op->other) == 2);
}

static bool import_of_quote(fixture_t *f, op_t *op)
{
    return amark_node_equal(f->quote, op->node, 0)
           && (amark_document_source_count(op->other) == 2);
}

static bool import_of_list(fixture_t *f, op_t *op)
{
    return amark_node_equal(f->list, op->node, 0)
           && (amark_document_source_count(op->other) == 1);
}

/* The whole fixture document into the other one, before its paragraph. */
static amark_error_t import_all_children(fixture_t *f, op_t *op)
{
    return amark_node_import_children(amark_document_root(op->other), op->other_before,
                                      f->root);
}

static bool all_children_imported(fixture_t *f, op_t *op)
{
    const amark_node_t *original = amark_node_first_child(f->root);
    const amark_node_t *copy = amark_node_first_child(amark_document_root(op->other));

    for (; original != NULL; original = amark_node_next(original))
    {
        if ((copy == NULL) || !amark_node_equal(original, copy, 0))
        {
            return false;
        }

        copy = amark_node_next(copy);
    }

    return (copy == op->other_before) && (amark_document_source_count(op->other) == 3);
}

/* The code block of the box into the block quote, in the same document. */
static amark_error_t import_box_children(fixture_t *f, op_t *op)
{
    (void)op;
    return amark_node_import_children(f->quote, NULL, f->box);
}

static bool box_children_imported(fixture_t *f, op_t *op)
{
    (void)op;
    return amark_node_equal(f->code_block, amark_node_last_child(f->quote), 0)
           && (amark_document_source_count(f->document) == 2);
}

/* The children of the root into the item, a descendant of the root. */
static amark_error_t import_root_children_into_item(fixture_t *f, op_t *op)
{
    (void)op;
    return amark_node_import_children(f->item, f->item_paragraph, f->root);
}

static oom_case_t copy_cases[] = {
    { "copy (list)", NULL, copy_list, node_is_null, copy_of_list, 0, 0, NULL },
    { "copy (extension type)", NULL, copy_box, node_is_null, copy_of_box, 0, 0, NULL },
    { "copy (tokens)", NULL, copy_quote, node_is_null, copy_of_quote, 0, 0, NULL }
};

static oom_case_t import_cases[] = {
    { "import (extension type)", prepare_other, import_box, node_is_null, import_of_box, 0, 0,
      NULL },
    { "import (tokens)", prepare_other, import_quote, node_is_null, import_of_quote, 0, 0,
      NULL },
    { "import (no tokens)", prepare_other, import_list, node_is_null, import_of_list, 0, 0,
      NULL }
};

static oom_case_t import_children_cases[] = {
    { "import children (other document)", prepare_other, import_all_children, NULL,
      all_children_imported, 0, 0, NULL },
    { "import children (same document)", NULL, import_box_children, NULL,
      box_children_imported, 0, 0, NULL },
    { "import children (into a descendant)", NULL, import_root_children_into_item, NULL, NULL,
      0, 0, NULL }
};

NAOS_TEST(copies_survive_every_allocation_failure)
{
    size_t i;

    for (i = 0; i < COUNT_OF(copy_cases); i++)
    {
        check_case(&copy_cases[i]);
    }
}

NAOS_TEST(imports_survive_every_allocation_failure)
{
    size_t i;

    for (i = 0; i < COUNT_OF(import_cases); i++)
    {
        check_case(&import_cases[i]);
    }
}

NAOS_TEST(imports_of_children_survive_every_allocation_failure)
{
    size_t i;

    for (i = 0; i < COUNT_OF(import_children_cases); i++)
    {
        check_case(&import_children_cases[i]);
    }
}

NAOS_SUITE(oom_copies,
    NAOS_SET_TEARDOWN(restore),
    NAOS_ADD_TEST(copies_survive_every_allocation_failure),
    NAOS_ADD_TEST(imports_survive_every_allocation_failure),
    NAOS_ADD_TEST(imports_of_children_survive_every_allocation_failure))

/* --- Suite oom_syntax_sets ---------------------------------------------------------- */

/* Extensions without types, to fill the array of a syntax set. */
static const amark_extension_t fill_extensions[] = {
    { sizeof(amark_extension_t), "q0", NULL, 0, NULL, 0, NULL },
    { sizeof(amark_extension_t), "q1", NULL, 0, NULL, 0, NULL },
    { sizeof(amark_extension_t), "q2", NULL, 0, NULL, 0, NULL },
    { sizeof(amark_extension_t), "q3", NULL, 0, NULL, 0, NULL },
    { sizeof(amark_extension_t), "q4", NULL, 0, NULL, 0, NULL },
    { sizeof(amark_extension_t), "q5", NULL, 0, NULL, 0, NULL },
    { sizeof(amark_extension_t), "q6", NULL, 0, NULL, 0, NULL },
    { sizeof(amark_extension_t), "q7", NULL, 0, NULL, 0, NULL }
};

/* An unfrozen set of amark.core, p and extensions without types up to the capacity
 * of its array (so that the next extension grows it), watched. */
static amark_error_t prepare_open_set(fixture_t *f, op_t *op)
{
    amark_error_t err = create_set(&p_extension, NULL, false, &op->set);
    size_t i;

    (void)f;
    op->watched = op->set;
    for (i = 0; (err == AMARK_ERROR_NONE) && (i < COUNT_OF(fill_extensions))
                && (op->set->extension_count < op->set->extension_capacity);
         i++)
    {
        err = amark_syntax_set_append(op->set, &fill_extensions[i]);
    }

    return err;
}

/* An unfrozen set of amark.core, p and o, watched. */
static amark_error_t prepare_full_set(fixture_t *f, op_t *op)
{
    amark_error_t err = create_set(&p_extension, &o_extension, false, &op->set);

    (void)f;
    op->watched = op->set;

    return err;
}

static amark_error_t prepare_frozen_watch(fixture_t *f, op_t *op)
{
    op->watched = f->set;

    return AMARK_ERROR_NONE;
}

static amark_error_t append_extension(fixture_t *f, op_t *op)
{
    (void)f;
    return amark_syntax_set_append(op->set, &o_extension);
}

static bool extension_appended(fixture_t *f, op_t *op)
{
    (void)f;
    size_t count = amark_syntax_set_extension_count(op->set);

    return (count >= 3) && (amark_syntax_set_extension_at(op->set, count - 1) == &o_extension);
}

static amark_error_t freeze_set(fixture_t *f, op_t *op)
{
    (void)f;
    return amark_syntax_set_freeze(op->set);
}

static bool set_frozen(fixture_t *f, op_t *op)
{
    amark_node_type_t type = AMARK_NODE_NONE;

    (void)f;
    return amark_syntax_set_is_frozen(op->set)
           && amark_syntax_set_find_node_type(op->set, "o.box", &type)
           && (amark_syntax_set_node_type_desc(op->set, type) == &box_type);
}

static amark_error_t clone_set(fixture_t *f, op_t *op)
{
    (void)f;
    op->clone = SENTINEL(amark_syntax_set_t);

    return amark_syntax_set_clone(op->watched, &op->clone);
}

static bool clone_is_null(fixture_t *f, op_t *op)
{
    (void)f;
    return (op->clone == NULL);
}

static bool set_cloned(fixture_t *f, op_t *op)
{
    set_state_t original;
    set_state_t clone;

    (void)f;
    set_state_take(op->watched, &original);
    set_state_take(op->clone, &clone);

    return !clone.frozen && (clone.count == original.count)
           && (memcmp(clone.extensions, original.extensions, sizeof(clone.extensions)) == 0);
}

static oom_case_t syntax_set_cases[] = {
    { "syntax set append", prepare_open_set, append_extension, NULL, extension_appended, 0, 0,
      NULL },
    { "syntax set freeze", prepare_full_set, freeze_set, NULL, set_frozen, 0, 0, NULL },
    { "syntax set clone (open)", prepare_full_set, clone_set, clone_is_null, set_cloned, 0, 0,
      NULL },
    { "syntax set clone (frozen)", prepare_frozen_watch, clone_set, clone_is_null, set_cloned,
      0, 0, NULL }
};

NAOS_TEST(syntax_sets_survive_every_allocation_failure)
{
    size_t i;

    for (i = 0; i < COUNT_OF(syntax_set_cases); i++)
    {
        check_case(&syntax_set_cases[i]);
    }
}

NAOS_SUITE(oom_syntax_sets,
    NAOS_SET_TEARDOWN(restore),
    NAOS_ADD_TEST(syntax_sets_survive_every_allocation_failure))

/* --- Suite oom_reading -------------------------------------------------------------- */

static amark_error_t create_iter(fixture_t *f, op_t *op)
{
    op->iter = SENTINEL(amark_iter_t);

    return amark_iter_create(f->root, &op->iter);
}

static bool iter_is_null(fixture_t *f, op_t *op)
{
    (void)f;
    return (op->iter == NULL);
}

static bool iter_starts(fixture_t *f, op_t *op)
{
    return (amark_iter_next(op->iter) == AMARK_EVENT_ENTER)
           && (amark_iter_node(op->iter) == f->root);
}

#define DUMP_PREFIX "prefix"

static amark_error_t prepare_buffer(fixture_t *f, op_t *op)
{
    amark_error_t err = amark_buffer_create(&op->buffer);

    (void)f;
    if (err == AMARK_ERROR_NONE)
    {
        err = amark_buffer_append_cstr(op->buffer, DUMP_PREFIX);
    }

    return err;
}

static amark_error_t dump_all(fixture_t *f, op_t *op)
{
    return amark_node_dump(f->root, AMARK_DUMP_TOKENS | AMARK_DUMP_EDITS | AMARK_DUMP_SPANS,
                           op->buffer);
}

static amark_error_t dump_box(fixture_t *f, op_t *op)
{
    return amark_node_dump(f->box, 0, op->buffer);
}

static bool buffer_kept(fixture_t *f, op_t *op)
{
    (void)f;
    return (amark_buffer_length(op->buffer) == (sizeof(DUMP_PREFIX) - 1))
           && (memcmp(amark_buffer_data(op->buffer), DUMP_PREFIX, sizeof(DUMP_PREFIX) - 1) == 0)
           && (amark_buffer_data(op->buffer)[sizeof(DUMP_PREFIX) - 1] == '\0');
}

static bool buffer_grown(fixture_t *f, op_t *op)
{
    (void)f;
    return (amark_buffer_length(op->buffer) > (sizeof(DUMP_PREFIX) - 1))
           && (memcmp(amark_buffer_data(op->buffer), DUMP_PREFIX "(", sizeof(DUMP_PREFIX)) == 0);
}

static amark_error_t check_source0(fixture_t *f, op_t *op)
{
    return amark_document_check_tokens(f->document, 0, &op->report);
}

static amark_error_t check_source1(fixture_t *f, op_t *op)
{
    return amark_document_check_tokens(f->document, 1, &op->report);
}

static bool report_none(fixture_t *f, op_t *op)
{
    (void)f;
    return (op->report.problem == AMARK_TOKEN_PROBLEM_NONE) && (op->report.node == NULL);
}

static oom_case_t reading_cases[] = {
    { "iterator create", NULL, create_iter, iter_is_null, iter_starts, 0, 0, NULL },
    { "dump (tokens, edits, spans)", prepare_buffer, dump_all, buffer_kept, buffer_grown, 0, 0,
      NULL },
    { "dump (extension attribute)", prepare_buffer, dump_box, buffer_kept, buffer_grown, 0, 0,
      NULL },
    { "lossless check (source 0)", NULL, check_source0, report_none, report_none, 0, 0, NULL },
    { "lossless check (source 1)", NULL, check_source1, report_none, report_none, 0, 0, NULL }
};

NAOS_TEST(reading_survives_every_allocation_failure)
{
    size_t i;

    for (i = 0; i < COUNT_OF(reading_cases); i++)
    {
        check_case(&reading_cases[i]);
    }
}

NAOS_SUITE(oom_reading,
    NAOS_SET_TEARDOWN(restore),
    NAOS_ADD_TEST(reading_survives_every_allocation_failure))

NAOS_MAIN(
    NAOS_ADD_SUITE(oom_documents),
    NAOS_ADD_SUITE(oom_content),
    NAOS_ADD_SUITE(oom_copies),
    NAOS_ADD_SUITE(oom_syntax_sets),
    NAOS_ADD_SUITE(oom_reading)
)
