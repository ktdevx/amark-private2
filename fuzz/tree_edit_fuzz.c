/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The fuzz target of the editing of the syntax tree (TreeFuzz).
 *
 * The input is read as a header byte and a sequence of operations on two
 * documents. The header gives the syntax set of each document (bits 0-1 for the
 * first, bits 2-3 for the second, modulo 3): the core set, core + f, or
 * core + g + f, where f has the block container f.box (its type data owns a
 * caption string, with init/deinit/copy/equal/dump) and the inline literal type
 * f.tag, and g has the block leaf g.note (so that the types of f have other
 * identifiers in the third set). Bit 4 turns on the edit tracking of the first
 * document, bit 5 that of the second.
 *
 * Every operation starts with a byte:
 *   bit 7     inject allocation failures during the operation; the next byte
 *             gives the first call of the allocator that fails (bits 0-5) and
 *             whether every later call fails too (bit 6) or only that one;
 *   bits 0-6  the operation (modulo OP_COUNT), followed by its arguments, read
 *             from the input (missing bytes read as 0).
 * The operations: creation of a node, the four insertions, the move by
 * replacement, unlinking, destruction, the literal, the inline text, the addition,
 * removal and transfer of tokens, the copy, the import into either document, the
 * import of the children, the edit tracking, the addition and the appending of
 * sources, the attributes (and the caption of f.box), the dump and the lossless
 * check. Nodes are named by a bounded pool of handles (a byte: its index modulo
 * the number of handles, or NULL for 0xff); the roots are always in it. A handle
 * is removed before the node it names is freed (destroying a node frees its
 * subtree, setting the inline text frees the children).
 *
 * After every operation the target checks:
 *   - amark_tree_check on both documents;
 *   - AMARK_ERROR_OUT_OF_MEMORY only when a failure was injected, and an operation
 *     that succeeded saw no injected failure;
 *   - a failed operation (and a dump or a lossless check, whatever its result)
 *     left both documents unchanged: their tracking, their sources, the number of
 *     nodes of their rings and the dump with the tokens and the edit records of
 *     every detached subtree (the root included), in the order of the ring;
 *   - the visible effect of a successful structural edit, copy or import.
 * At the end, without failures, each detached subtree and each child of each root
 * is copied and the copy must equal its original and dump the same (tokens and
 * edit records included); each detached subtree is imported into the other
 * document (or refused with AMARK_ERROR_INVALID_ARGUMENT for a missing type) and
 * the import must equal its original; the children of each root are imported
 * into a new document of the same syntax set, whose root must equal the original
 * root and dump the same.
 *
 * The allocator that injects the failures lives in this file (it must not come
 * from amark_test_support or amark_test_tree, which link Naos). It is set as the
 * global allocator with amark_allocator_set for each input and the default
 * allocator is restored at the end, after checking that every block was freed.
 *
 * A violated property aborts, which libFuzzer and the corpus replay report.
 */
#include <amark/allocator.h>
#include <amark/buffer.h>
#include <amark/document.h>
#include <amark/dump.h>
#include <amark/error.h>
#include <amark/extension.h>
#include <amark/node.h>
#include <amark/token.h>

#include "tree/tree.h"

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static void check_failed(const char *expression, int line)
{
    fprintf(stderr, "tree_edit_fuzz.c:%d: property violated: %s\n", line, expression);
    abort();
}

#define CHECK(expression)                         \
    do {                                          \
        if (!(expression)) {                      \
            check_failed(#expression, __LINE__);  \
        }                                         \
    } while (0)

/* The bounds that keep the checks after every operation fast: the handles of the
 * pool, the nodes of a document (an operation that would go beyond is skipped),
 * the operations of an input, the length of a text argument and the sources of a
 * document. */
#define MAX_HANDLES 32
#define MAX_NODES   160
#define MAX_OPS     1024
#define MAX_TEXT    64
#define MAX_SOURCES 8

/* The flags of the dumps that compare states. */
#define FULL_DUMP (AMARK_DUMP_TOKENS | AMARK_DUMP_EDITS)

/* ---- The allocator that injects failures ---- */

static struct
{
    bool injecting;   /* failures are injected during the current operation */
    bool persistent;  /* every call from fail_at fails, not only that one */
    size_t fail_at;   /* the index of the first call that fails */
    size_t calls;     /* calls of malloc_fn, calloc_fn and realloc_fn */
    size_t failures;  /* calls that failed */
    size_t live;      /* blocks allocated and not yet freed */
} fuzz_allocator;

static bool should_fail(void)
{
    size_t index = fuzz_allocator.calls++;

    if (!fuzz_allocator.injecting || (index < fuzz_allocator.fail_at)
        || (!fuzz_allocator.persistent && (index != fuzz_allocator.fail_at)))
    {
        return false;
    }

    fuzz_allocator.failures++;
    return true;
}

static void *fuzz_malloc(size_t size)
{
    void *block;

    if (should_fail())
    {
        return NULL;
    }

    block = malloc(size);
    if (block != NULL)
    {
        fuzz_allocator.live++;
    }

    return block;
}

static void *fuzz_calloc(size_t count, size_t size)
{
    void *block;

    if (should_fail())
    {
        return NULL;
    }

    block = calloc(count, size);
    if (block != NULL)
    {
        fuzz_allocator.live++;
    }

    return block;
}

/* amark_realloc never passes a NULL ptr or a size of 0 (amark/allocator.h). */
static void *fuzz_realloc(void *ptr, size_t new_size)
{
    CHECK((ptr != NULL) && (new_size != 0));
    if (should_fail())
    {
        return NULL;
    }

    return realloc(ptr, new_size);
}

static void fuzz_free(void *ptr)
{
    CHECK(ptr != NULL);
    CHECK(fuzz_allocator.live > 0);
    fuzz_allocator.live--;
    free(ptr);
}

static const amark_allocator_t fuzz_allocator_table = {
    fuzz_malloc, fuzz_calloc, fuzz_realloc, fuzz_free
};

/* ---- The extensions f and g ---- */

typedef struct box_data
{
    char *caption;  /* NUL-terminated, never NULL once made */
    size_t caption_length;
} box_data_t;

/* Copies text[0..length) into a new NUL-terminated block of Amark's allocator. */
static char *duplicate(const char *text, size_t length)
{
    char *copy = amark_malloc(length + 1);

    if (copy != NULL)
    {
        if (length != 0)
        {
            memcpy(copy, text, length);
        }

        copy[length] = '\0';
    }

    return copy;
}

static amark_error_t box_init(amark_node_t *node, void *data)
{
    box_data_t *box = data;

    (void)node;
    box->caption = duplicate("", 0);

    return (box->caption != NULL) ? AMARK_ERROR_NONE : AMARK_ERROR_OUT_OF_MEMORY;
}

static void box_deinit(amark_node_t *node, void *data)
{
    (void)node;
    amark_free(((box_data_t *)data)->caption);
}

static amark_error_t box_copy(amark_node_t *dst, void *dst_data, const amark_node_t *src,
                              const void *src_data)
{
    box_data_t *to = dst_data;
    const box_data_t *from = src_data;

    (void)dst;
    (void)src;
    to->caption = duplicate(from->caption, from->caption_length);
    if (to->caption == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }

    to->caption_length = from->caption_length;

    return AMARK_ERROR_NONE;
}

static bool box_equal(const amark_node_t *a, const void *a_data, const amark_node_t *b,
                      const void *b_data)
{
    const box_data_t *x = a_data;
    const box_data_t *y = b_data;

    (void)a;
    (void)b;

    return (x->caption_length == y->caption_length)
           && (memcmp(x->caption, y->caption, x->caption_length) == 0);
}

static amark_error_t box_dump(const amark_node_t *node, const void *data,
                              amark_dump_writer_t *writer)
{
    const box_data_t *box = data;

    (void)node;

    return amark_dump_attr_string(writer, "caption", box->caption, box->caption_length);
}

static const amark_node_type_desc_t box_type = {
    sizeof(amark_node_type_desc_t), "f.box", AMARK_NODE_LEVEL_BLOCK, AMARK_CONTENT_BLOCK,
    AMARK_CONTENT_BLOCK, AMARK_LITERAL_NONE, sizeof(box_data_t), NULL, NULL, box_init,
    box_deinit, box_copy, box_equal, box_dump, NULL
};

static const amark_node_type_desc_t tag_type = {
    sizeof(amark_node_type_desc_t), "f.tag", AMARK_NODE_LEVEL_INLINE, AMARK_CONTENT_INLINE, 0,
    AMARK_LITERAL_LINE, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

static const amark_node_type_desc_t note_type = {
    sizeof(amark_node_type_desc_t), "g.note", AMARK_NODE_LEVEL_BLOCK, AMARK_CONTENT_BLOCK, 0,
    AMARK_LITERAL_NONE, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

static const amark_node_type_desc_t *const f_types[] = { &box_type, &tag_type };

static const amark_extension_t f_extension = {
    sizeof(amark_extension_t), "f", f_types, 2, NULL, 0, NULL
};

static const amark_node_type_desc_t *const g_types[] = { &note_type };

static const amark_extension_t g_extension = {
    sizeof(amark_extension_t), "g", g_types, 1, NULL, 0, NULL
};

/* Creates the frozen syntax set core + first (+ second when not NULL). */
static amark_syntax_set_t *create_set(const amark_extension_t *first,
                                      const amark_extension_t *second)
{
    amark_syntax_set_t *set = NULL;

    CHECK(amark_syntax_set_create(&set) == AMARK_ERROR_NONE);
    CHECK(amark_syntax_set_append(set, first) == AMARK_ERROR_NONE);
    if (second != NULL)
    {
        CHECK(amark_syntax_set_append(set, second) == AMARK_ERROR_NONE);
    }

    CHECK(amark_syntax_set_freeze(set) == AMARK_ERROR_NONE);

    return set;
}

/* ---- The input ---- */

typedef struct
{
    const uint8_t *data;
    size_t size;
    size_t position;
} reader_t;

static uint8_t read_byte(reader_t *reader)
{
    if (reader->position >= reader->size)
    {
        return 0;
    }

    return reader->data[reader->position++];
}

/* Reads a text argument: a length byte (modulo MAX_TEXT + 1) and that many bytes
 * of the input (fewer at its end). */
static const char *read_text(reader_t *reader, size_t *out_length)
{
    size_t length = read_byte(reader) % (MAX_TEXT + 1);
    const char *text = (const char *)reader->data + reader->position;

    if (length > (reader->size - reader->position))
    {
        length = reader->size - reader->position;
    }

    reader->position += length;
    *out_length = length;

    return text;
}

/* ---- The state of an input ---- */

static struct
{
    amark_document_t *documents[2];
    amark_node_t *pool[MAX_HANDLES];
    size_t pool_count;
    size_t replaced;          /* the rotation of the handles replaced in a full pool */
    amark_buffer_t *before[2];
    amark_buffer_t *after[2];
    amark_buffer_t *scratch;  /* the output of the dump operation */
    bool expect_unchanged;    /* the operation must not change the documents */
} state;

/* The bytes the scratch buffer holds before a dump. */
#define SCRATCH_PREFIX        "prefix"
#define SCRATCH_PREFIX_LENGTH 6u

static amark_document_t *pick_document(reader_t *reader)
{
    return state.documents[read_byte(reader) & 1u];
}

/* Picks a handle of the pool (the byte modulo the number of handles), or NULL for
 * the byte 0xff. */
static amark_node_t *pick_node(reader_t *reader)
{
    uint8_t byte = read_byte(reader);

    return (byte == 0xffu) ? NULL : state.pool[byte % state.pool_count];
}

static void pool_add(amark_node_t *node)
{
    if (state.pool_count < MAX_HANDLES)
    {
        state.pool[state.pool_count++] = node;
        return;
    }

    /* The roots (0 and 1) stay. */
    state.pool[2 + (state.replaced % (MAX_HANDLES - 2))] = node;
    state.replaced++;
}

/* Removes the handles of the subtree of node (node itself when include_self), so
 * that no handle outlives the node it names. */
static void pool_forget(const amark_node_t *node, bool include_self)
{
    size_t i = 2;

    if (node == NULL)
    {
        return;
    }

    while (i < state.pool_count)
    {
        const amark_node_t *ancestor = state.pool[i];
        bool inside = false;

        if (!include_self)
        {
            ancestor = ancestor->parent;
        }

        for (; ancestor != NULL; ancestor = ancestor->parent)
        {
            if (ancestor == node)
            {
                inside = true;
                break;
            }
        }

        if (inside)
        {
            state.pool[i] = state.pool[--state.pool_count];
        }
        else
        {
            i++;
        }
    }
}

static size_t ring_count(const amark_document_t *document)
{
    const amark_node_t *node = document->ring;
    size_t count = 0;

    do
    {
        count++;
        node = node->ring_next;
    } while (node != document->ring);

    return count;
}

/* The number of nodes of the subtree of node (0 for NULL). */
static size_t subtree_size(const amark_node_t *node)
{
    const amark_node_t *current = node;
    size_t count = 0;

    while (current != NULL)
    {
        count++;
        if (current->first_child != NULL)
        {
            current = current->first_child;
            continue;
        }

        while ((current != node) && (current->next == NULL))
        {
            current = current->parent;
        }

        current = (current == node) ? NULL : current->next;
    }

    return count;
}

/* Whether adding `added` nodes to document stays within MAX_NODES. */
static bool has_room(const amark_document_t *document, size_t added)
{
    return added <= (MAX_NODES - ring_count(document));
}

/* Writes the state of document into out (cleared first): its tracking, its
 * sources, the number of nodes of its ring and the full dump of every detached
 * subtree in the order of the ring. Called without injected failures. */
static void snapshot(const amark_document_t *document, amark_buffer_t *out)
{
    const amark_node_t *node = document->ring;
    size_t count = amark_document_source_count(document);
    char line[64];
    size_t i;

    amark_buffer_clear(out);
    snprintf(line, sizeof(line), "tracking %d sources %lu nodes %lu\n",
             amark_document_edit_tracking(document) ? 1 : 0, (unsigned long)count,
             (unsigned long)ring_count(document));
    CHECK(amark_buffer_append_cstr(out, line) == AMARK_ERROR_NONE);

    for (i = 0; i < count; i++)
    {
        size_t length = 0;
        const char *bytes = amark_document_source_data(document, (amark_source_id_t)i, &length);

        snprintf(line, sizeof(line), "source %lu\n", (unsigned long)length);
        CHECK(amark_buffer_append_cstr(out, line) == AMARK_ERROR_NONE);
        CHECK(amark_buffer_append(out, bytes, length) == AMARK_ERROR_NONE);
    }

    do
    {
        if (node->parent == NULL)
        {
            CHECK(amark_node_dump(node, FULL_DUMP, out) == AMARK_ERROR_NONE);
        }

        node = node->ring_next;
    } while (node != document->ring);
}

static bool buffers_equal(const amark_buffer_t *a, const amark_buffer_t *b)
{
    return (amark_buffer_length(a) == amark_buffer_length(b))
           && (memcmp(amark_buffer_data(a), amark_buffer_data(b), amark_buffer_length(a)) == 0);
}

static void check_tree(const amark_document_t *document)
{
    const char *reason = NULL;

    if (amark_tree_check(document, &reason) != AMARK_ERROR_NONE)
    {
        fprintf(stderr, "tree_edit_fuzz.c: amark_tree_check: %s\n",
                (reason != NULL) ? reason : "(no reason)");
        abort();
    }
}

/* ---- The operations ---- */

enum
{
    OP_CREATE,
    OP_INSERT_BEFORE,
    OP_INSERT_AFTER,
    OP_PREPEND_CHILD,
    OP_APPEND_CHILD,
    OP_REPLACE,
    OP_UNLINK,
    OP_DESTROY,
    OP_SET_LITERAL,
    OP_SET_INLINE_TEXT,
    OP_ADD_TOKEN,
    OP_REMOVE_TOKEN,
    OP_TRANSFER_TOKENS,
    OP_COPY,
    OP_IMPORT,
    OP_IMPORT_CHILDREN,
    OP_TRACKING,
    OP_ADD_SOURCE,
    OP_APPEND_SOURCE,
    OP_ATTRIBUTE,
    OP_DUMP,
    OP_CHECK_TOKENS,
    OP_COUNT
};

/* The type of a new node: a built-in type (0, none, and 1, document, included),
 * a type of f or g (an unknown identifier when the set lacks it) or an identifier
 * of no type. */
static amark_node_type_t pick_type(const amark_document_t *document, uint8_t byte)
{
    static const char *const names[] = { "f.box", "f.tag", "g.note" };
    unsigned choice = byte % 24u;
    amark_node_type_t type = AMARK_NODE_DYNAMIC_FIRST + 100u;

    if (choice <= AMARK_NODE_IMAGE)
    {
        return (amark_node_type_t)choice;
    }

    if (choice == 23u)
    {
        return AMARK_NODE_BUILTIN_LAST;
    }

    if (!amark_syntax_set_find_node_type(amark_document_syntax_set(document),
                                         names[choice - 20u], &type))
    {
        type = AMARK_NODE_DYNAMIC_FIRST + 100u;
    }

    return type;
}

static amark_error_t op_create(reader_t *reader)
{
    amark_document_t *document = pick_document(reader);
    amark_node_type_t type = pick_type(document, read_byte(reader));
    amark_node_t *node = state.pool[0];
    amark_error_t err;

    if (!has_room(document, 1))
    {
        return AMARK_ERROR_NONE;
    }

    err = amark_node_create(document, type, &node);
    if (err != AMARK_ERROR_NONE)
    {
        CHECK(node == NULL);
        return err;
    }

    CHECK((node != NULL) && (amark_node_document(node) == document));
    CHECK((amark_node_get_type(node) == type) && (amark_node_parent(node) == NULL));
    pool_add(node);

    return AMARK_ERROR_NONE;
}

static amark_error_t op_insert(reader_t *reader, int op)
{
    amark_node_t *anchor = pick_node(reader);
    amark_node_t *node = pick_node(reader);
    amark_error_t err;

    switch (op)
    {
    case OP_INSERT_BEFORE:
        err = amark_node_insert_before(anchor, node);
        break;
    case OP_INSERT_AFTER:
        err = amark_node_insert_after(anchor, node);
        break;
    case OP_PREPEND_CHILD:
        err = amark_node_prepend_child(anchor, node);
        break;
    default:
        err = amark_node_append_child(anchor, node);
        break;
    }

    if ((err != AMARK_ERROR_NONE) || (anchor == node))
    {
        return err;
    }

    switch (op)
    {
    case OP_INSERT_BEFORE:
        CHECK((node->parent == anchor->parent) && (node->next == anchor));
        break;
    case OP_INSERT_AFTER:
        CHECK((node->parent == anchor->parent) && (anchor->next == node));
        break;
    case OP_PREPEND_CHILD:
        CHECK((node->parent == anchor) && (anchor->first_child == node));
        break;
    default:
        CHECK((node->parent == anchor) && (anchor->last_child == node));
        break;
    }

    return AMARK_ERROR_NONE;
}

static amark_error_t op_replace(reader_t *reader)
{
    amark_node_t *old_node = pick_node(reader);
    amark_node_t *new_node = pick_node(reader);
    amark_node_t *parent = (old_node != NULL) ? old_node->parent : NULL;
    amark_error_t err;

    err = amark_node_replace(old_node, new_node);
    if ((err == AMARK_ERROR_NONE) && (old_node != new_node))
    {
        CHECK((old_node->parent == NULL) && (new_node->parent == parent));
    }

    return err;
}

static amark_error_t op_unlink(reader_t *reader)
{
    amark_node_t *node = pick_node(reader);
    amark_error_t err;

    err = amark_node_unlink(node);
    if ((err == AMARK_ERROR_NONE) && (node != NULL))
    {
        CHECK(node->parent == NULL);
    }

    return err;
}

static amark_error_t op_destroy(reader_t *reader)
{
    amark_node_t *node = pick_node(reader);
    amark_document_t *document;
    size_t count;
    size_t size;

    if (node == NULL)
    {
        amark_node_destroy(NULL);
        return AMARK_ERROR_NONE;
    }

    document = node->document;
    count = ring_count(document);
    if (node == document->root)
    {
        /* Does nothing. */
        amark_node_destroy(node);
        CHECK(ring_count(document) == count);
        return AMARK_ERROR_NONE;
    }

    size = subtree_size(node);
    pool_forget(node, true);
    amark_node_destroy(node);
    CHECK(ring_count(document) == (count - size));

    return AMARK_ERROR_NONE;
}

static amark_error_t op_set_literal(reader_t *reader)
{
    amark_node_t *node = pick_node(reader);
    size_t length;
    const char *text = read_text(reader, &length);
    amark_error_t err;

    err = amark_node_set_literal(node, text, length);
    if (err == AMARK_ERROR_NONE)
    {
        CHECK(node != NULL);
    }

    return err;
}

static amark_error_t op_set_inline_text(reader_t *reader)
{
    amark_node_t *node = pick_node(reader);
    size_t length;
    const char *text = read_text(reader, &length);

    /* At most one text node and one soft break for each byte. */
    if ((node != NULL) && !has_room(node->document, length + 1))
    {
        return AMARK_ERROR_NONE;
    }

    /* The children are freed when it succeeds. */
    pool_forget(node, false);

    return amark_node_set_inline_text(node, text, length);
}

static amark_error_t op_add_token(reader_t *reader)
{
    amark_node_t *node = pick_node(reader);
    amark_token_t token;
    uint8_t shape;

    memset(&token, 0, sizeof(token));
    token.struct_size = sizeof(token);
    token.kind = (amark_token_kind_t)(read_byte(reader) % 11u);
    shape = read_byte(reader);
    token.flags = shape & 0x07u;
    token.role = read_byte(reader);
    token.offset = read_byte(reader);
    token.length = read_byte(reader);
    if ((shape & 0x08u) != 0)
    {
        token.tab_column_offset = read_byte(reader) % 5u;
        token.tab_columns = read_byte(reader) % 6u;
    }

    if (node != NULL)
    {
        size_t count = amark_document_source_count(node->document);

        token.source = (amark_source_id_t)(read_byte(reader) % (count + 1));
        if (token.source < count)
        {
            size_t source_length = 0;

            (void)amark_document_source_data(node->document, token.source, &source_length);
            if ((shape & 0x10u) == 0)
            {
                /* In range. */
                token.offset %= source_length + 1;
                token.length %= (source_length - token.offset) + 1;
            }
        }
    }

    return amark_node_add_token(node, &token);
}

static amark_error_t op_remove_token(reader_t *reader)
{
    amark_node_t *node = pick_node(reader);
    size_t index = read_byte(reader) % (amark_node_token_count(node) + 1);

    return amark_node_remove_token(node, index);
}

static amark_error_t op_transfer_tokens(reader_t *reader)
{
    amark_node_t *from = pick_node(reader);
    amark_node_t *to = pick_node(reader);
    size_t count = amark_node_token_count(from);
    size_t first = read_byte(reader) % (count + 2);
    size_t moved = read_byte(reader) % (count + 2);
    size_t from_count = count;
    size_t to_count = amark_node_token_count(to);
    amark_error_t err;

    err = amark_node_transfer_tokens(from, first, moved, to);
    if ((err == AMARK_ERROR_NONE) && (from != to))
    {
        CHECK(amark_node_token_count(from) == (from_count - moved));
        CHECK(amark_node_token_count(to) == (to_count + moved));
    }

    return err;
}

/* The copy (document NULL) or the import into document of node. */
static amark_error_t op_copy(amark_document_t *document, amark_node_t *node)
{
    amark_node_t *copy = state.pool[0];
    amark_document_t *to = document;
    amark_error_t err;

    if (node != NULL)
    {
        if (to == NULL)
        {
            to = node->document;
        }

        if (!has_room(to, subtree_size(node)))
        {
            return AMARK_ERROR_NONE;
        }
    }

    if (document == NULL)
    {
        err = amark_node_copy(node, &copy);
    }
    else
    {
        err = amark_node_import(document, node, &copy);
    }

    if (err != AMARK_ERROR_NONE)
    {
        CHECK(copy == NULL);
        return err;
    }

    CHECK((copy != NULL) && (copy->document == to) && (copy->parent == NULL));
    CHECK(amark_node_equal(copy, node, 0));
    pool_add(copy);

    return AMARK_ERROR_NONE;
}

static amark_error_t op_import_children(reader_t *reader)
{
    amark_node_t *parent = pick_node(reader);
    amark_node_t *before = pick_node(reader);
    amark_node_t *source_parent = pick_node(reader);
    size_t added = 0;
    size_t children = 0;
    size_t parent_children = 0;
    const amark_node_t *child;
    amark_error_t err;

    if ((parent != NULL) && (source_parent != NULL))
    {
        for (child = source_parent->first_child; child != NULL; child = child->next)
        {
            added += subtree_size(child);
            children++;
        }

        if (!has_room(parent->document, added))
        {
            return AMARK_ERROR_NONE;
        }

        for (child = parent->first_child; child != NULL; child = child->next)
        {
            parent_children++;
        }
    }

    err = amark_node_import_children(parent, before, source_parent);
    if (err == AMARK_ERROR_NONE)
    {
        size_t count = 0;

        CHECK((parent != NULL) && (source_parent != NULL));
        for (child = parent->first_child; child != NULL; child = child->next)
        {
            count++;
        }

        CHECK(count == (parent_children + children));
    }

    return err;
}

static amark_error_t op_tracking(reader_t *reader)
{
    amark_document_t *document = pick_document(reader);
    bool enabled = (read_byte(reader) & 1u) != 0;

    amark_document_set_edit_tracking(document, enabled);
    CHECK(amark_document_edit_tracking(document) == enabled);

    return AMARK_ERROR_NONE;
}

static amark_error_t op_add_source(reader_t *reader)
{
    amark_document_t *document = pick_document(reader);
    size_t length;
    const char *text = read_text(reader, &length);
    size_t count = amark_document_source_count(document);
    amark_source_id_t id = 1000u;
    amark_error_t err;

    if (count >= MAX_SOURCES)
    {
        return AMARK_ERROR_NONE;
    }

    err = amark_document_add_source(document, text, length, &id);
    if (err == AMARK_ERROR_NONE)
    {
        CHECK((id == count) && (amark_document_source_count(document) == (count + 1)));
    }
    else
    {
        CHECK(id == 1000u);
    }

    return err;
}

static amark_error_t op_append_source(reader_t *reader)
{
    amark_document_t *document = pick_document(reader);
    size_t count = amark_document_source_count(document);
    amark_source_id_t source = (amark_source_id_t)(read_byte(reader) % (count + 1));
    size_t length;
    const char *text = read_text(reader, &length);

    return amark_document_append_source(document, source, text, length);
}

/* Sets the caption of a f.box, as a setter of an extension type does. */
static amark_error_t set_box_caption(amark_node_t *node, const char *text, size_t length)
{
    box_data_t *box;
    char *caption;

    if ((node == NULL) || (amark_node_desc(node) != &box_type)
        || (memchr(text, '\0', length) != NULL))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    caption = duplicate(text, length);
    if (caption == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }

    box = amark_node_data(node);
    amark_free(box->caption);
    box->caption = caption;
    box->caption_length = length;
    amark_node_mark_edited(node);

    return AMARK_ERROR_NONE;
}

static amark_error_t op_attribute(reader_t *reader)
{
    amark_node_t *node = pick_node(reader);
    uint8_t which = read_byte(reader);
    uint8_t value = read_byte(reader);
    size_t length;
    const char *text;

    switch (which % 10u)
    {
    case 0:
        return amark_node_set_heading_level(node, (int)(value % 9u) - 1);
    case 1:
        return amark_node_set_list_type(node, (amark_list_type_t)(value % 4u));
    case 2:
        return amark_node_set_list_start(node, ((value & 0x80u) != 0) ? LONG_MAX
                                                                      : (long)value - 64);
    case 3:
        return amark_node_set_list_tight(node, (value & 1u) != 0);
    case 4:
        amark_node_mark_edited(node);
        return AMARK_ERROR_NONE;
    default:
        break;
    }

    text = read_text(reader, &length);
    switch (which % 10u)
    {
    case 5:
        return amark_node_set_code_info(node, text, length);
    case 6:
        return amark_node_set_link_destination(node, text, length);
    case 7:
        return amark_node_set_link_title(node, text, length);
    case 8:
        return amark_node_set_link_label(node, text, length);
    default:
        return set_box_caption(node, text, length);
    }
}

static amark_error_t op_dump(reader_t *reader)
{
    amark_node_t *node = pick_node(reader);
    uint32_t flags = read_byte(reader) & 0x07u;
    amark_error_t err;

    state.expect_unchanged = true;
    err = amark_node_dump(node, flags, state.scratch);
    if (err != AMARK_ERROR_NONE)
    {
        CHECK(amark_buffer_length(state.scratch) == SCRATCH_PREFIX_LENGTH);
        CHECK(memcmp(amark_buffer_data(state.scratch), SCRATCH_PREFIX,
                     SCRATCH_PREFIX_LENGTH) == 0);
    }

    /* Does not allocate (the length only shrinks). */
    CHECK(amark_buffer_truncate(state.scratch, SCRATCH_PREFIX_LENGTH) == AMARK_ERROR_NONE);

    return err;
}

static amark_error_t op_check_tokens(reader_t *reader)
{
    amark_document_t *document = pick_document(reader);
    size_t count = amark_document_source_count(document);
    amark_source_id_t source = (amark_source_id_t)(read_byte(reader) % (count + 1));
    amark_token_report_t report;
    amark_error_t err;

    state.expect_unchanged = true;
    memset(&report, 0, sizeof(report));
    report.struct_size = sizeof(report);
    report.problem = AMARK_TOKEN_PROBLEM_GAP;
    err = amark_document_check_tokens(document, source, &report);
    if (err == AMARK_ERROR_NOT_LOSSLESS)
    {
        CHECK(report.problem != AMARK_TOKEN_PROBLEM_NONE);
    }
    else if (err != AMARK_ERROR_NONE)
    {
        CHECK((report.problem == AMARK_TOKEN_PROBLEM_NONE) && (report.node == NULL));
    }

    return err;
}

static amark_error_t run_op(int op, reader_t *reader)
{
    switch (op)
    {
    case OP_CREATE:
        return op_create(reader);
    case OP_INSERT_BEFORE:
    case OP_INSERT_AFTER:
    case OP_PREPEND_CHILD:
    case OP_APPEND_CHILD:
        return op_insert(reader, op);
    case OP_REPLACE:
        return op_replace(reader);
    case OP_UNLINK:
        return op_unlink(reader);
    case OP_DESTROY:
        return op_destroy(reader);
    case OP_SET_LITERAL:
        return op_set_literal(reader);
    case OP_SET_INLINE_TEXT:
        return op_set_inline_text(reader);
    case OP_ADD_TOKEN:
        return op_add_token(reader);
    case OP_REMOVE_TOKEN:
        return op_remove_token(reader);
    case OP_TRANSFER_TOKENS:
        return op_transfer_tokens(reader);
    case OP_COPY:
        return op_copy(NULL, pick_node(reader));
    case OP_IMPORT:
    {
        amark_document_t *document = pick_document(reader);

        return op_copy(document, pick_node(reader));
    }
    case OP_IMPORT_CHILDREN:
        return op_import_children(reader);
    case OP_TRACKING:
        return op_tracking(reader);
    case OP_ADD_SOURCE:
        return op_add_source(reader);
    case OP_APPEND_SOURCE:
        return op_append_source(reader);
    case OP_ATTRIBUTE:
        return op_attribute(reader);
    case OP_DUMP:
        return op_dump(reader);
    default:
        return op_check_tokens(reader);
    }
}

/* Runs one operation and checks the properties stated at the top of the file. */
static void step(reader_t *reader)
{
    uint8_t op = read_byte(reader);
    bool inject = (op & 0x80u) != 0;
    amark_error_t err;
    int i;

    fuzz_allocator.persistent = false;
    fuzz_allocator.fail_at = 0;
    if (inject)
    {
        uint8_t spec = read_byte(reader);

        fuzz_allocator.fail_at = spec & 0x3fu;
        fuzz_allocator.persistent = (spec & 0x40u) != 0;
    }

    for (i = 0; i < 2; i++)
    {
        snapshot(state.documents[i], state.before[i]);
    }

    state.expect_unchanged = false;
    fuzz_allocator.calls = 0;
    fuzz_allocator.failures = 0;
    fuzz_allocator.injecting = inject;
    err = run_op((op & 0x7fu) % OP_COUNT, reader);
    fuzz_allocator.injecting = false;

    for (i = 0; i < 2; i++)
    {
        check_tree(state.documents[i]);
    }

    if (err == AMARK_ERROR_OUT_OF_MEMORY)
    {
        CHECK(fuzz_allocator.failures > 0);
    }
    else if (err == AMARK_ERROR_NONE)
    {
        CHECK(fuzz_allocator.failures == 0);
    }

    if ((err != AMARK_ERROR_NONE) || state.expect_unchanged)
    {
        for (i = 0; i < 2; i++)
        {
            snapshot(state.documents[i], state.after[i]);
            if (!buffers_equal(state.before[i], state.after[i]))
            {
                fprintf(stderr, "tree_edit_fuzz.c: operation %d (error %d) changed document %d\n"
                        "before:\n%s\nafter:\n%s\n", (op & 0x7f) % OP_COUNT, (int)err, i,
                        amark_buffer_data(state.before[i]), amark_buffer_data(state.after[i]));
                abort();
            }
        }
    }
}

/* ---- The final checks ---- */

/* The full dumps of a and b, with flags, are the same. */
static void check_same_dump(const amark_node_t *a, const amark_node_t *b, uint32_t flags)
{
    amark_buffer_clear(state.before[0]);
    amark_buffer_clear(state.after[0]);
    CHECK(amark_node_dump(a, flags, state.before[0]) == AMARK_ERROR_NONE);
    CHECK(amark_node_dump(b, flags, state.after[0]) == AMARK_ERROR_NONE);
    if (!buffers_equal(state.before[0], state.after[0]))
    {
        fprintf(stderr, "tree_edit_fuzz.c: the dumps differ:\n%s\n%s\n",
                amark_buffer_data(state.before[0]), amark_buffer_data(state.after[0]));
        abort();
    }
}

/* Copies node in its document and compares the copy with it. */
static void check_copy(const amark_node_t *node)
{
    amark_node_t *copy = NULL;

    CHECK(amark_node_copy(node, &copy) == AMARK_ERROR_NONE);
    CHECK(amark_node_equal(copy, node, 0));
    CHECK(amark_node_equal(copy, node, AMARK_EQUAL_NORMALIZE_TEXT));
    check_same_dump(copy, node, FULL_DUMP);
    amark_node_destroy(copy);
}

/* Imports node into document and compares the import with it, unless a type of
 * its subtree is not in the syntax set of document. */
static void check_import(amark_document_t *document, const amark_node_t *node)
{
    amark_node_t *copy = NULL;
    amark_error_t err;

    err = amark_node_import(document, node, &copy);
    if (err == AMARK_ERROR_INVALID_ARGUMENT)
    {
        CHECK(copy == NULL);
        return;
    }

    CHECK(err == AMARK_ERROR_NONE);
    CHECK(amark_node_equal(copy, node, 0));
    check_same_dump(copy, node, 0);
    amark_node_destroy(copy);
}

/* Imports the children of the root of document into a new document of the same
 * syntax set and compares the roots. */
static void check_root(const amark_document_t *document)
{
    amark_document_t *other = NULL;

    CHECK(amark_document_create(amark_document_syntax_set(document), &other)
          == AMARK_ERROR_NONE);
    CHECK(amark_node_import_children(amark_document_root(other), NULL,
                                     amark_document_root(document)) == AMARK_ERROR_NONE);
    check_tree(other);
    CHECK(amark_node_equal(amark_document_root(other), amark_document_root(document), 0));
    check_same_dump(amark_document_root(other), amark_document_root(document), 0);
    amark_document_destroy(other);
}

static void check_final(void)
{
    static const amark_node_t *tops[MAX_NODES];
    int d;

    for (d = 0; d < 2; d++)
    {
        amark_document_t *document = state.documents[d];
        const amark_node_t *node = document->ring;
        size_t count = 0;
        size_t i;

        /* The detached subtrees, before the copies add more. */
        do
        {
            if ((node->parent == NULL) && (node != document->root))
            {
                CHECK(count < MAX_NODES);
                tops[count++] = node;
            }

            node = node->ring_next;
        } while (node != document->ring);

        for (i = 0; i < count; i++)
        {
            check_copy(tops[i]);
            check_import(state.documents[1 - d], tops[i]);
        }

        for (node = document->root->first_child; node != NULL; node = node->next)
        {
            check_copy(node);
        }

        check_root(document);
        check_tree(state.documents[0]);
        check_tree(state.documents[1]);
    }
}

/* ---- The entry point ---- */

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    reader_t reader;
    amark_syntax_set_t *f_set;
    amark_syntax_set_t *gf_set;
    const amark_syntax_set_t *sets[3];
    uint8_t header;
    size_t ops = 0;
    int i;

    reader.data = data;
    reader.size = size;
    reader.position = 0;
    memset(&fuzz_allocator, 0, sizeof(fuzz_allocator));
    memset(&state, 0, sizeof(state));
    CHECK(amark_allocator_set(&fuzz_allocator_table) == AMARK_ERROR_NONE);

    f_set = create_set(&f_extension, NULL);
    gf_set = create_set(&g_extension, &f_extension);
    sets[0] = amark_syntax_set_core();
    sets[1] = f_set;
    sets[2] = gf_set;
    header = read_byte(&reader);
    for (i = 0; i < 2; i++)
    {
        CHECK(amark_document_create(sets[((header >> (2 * i)) & 3u) % 3u], &state.documents[i])
              == AMARK_ERROR_NONE);
        amark_document_set_edit_tracking(state.documents[i],
                                         ((header >> (4 + i)) & 1u) != 0);
        state.pool[state.pool_count++] = amark_document_root(state.documents[i]);
        CHECK(amark_buffer_create(&state.before[i]) == AMARK_ERROR_NONE);
        CHECK(amark_buffer_create(&state.after[i]) == AMARK_ERROR_NONE);
    }

    CHECK(amark_buffer_create(&state.scratch) == AMARK_ERROR_NONE);
    CHECK(amark_buffer_append_cstr(state.scratch, SCRATCH_PREFIX) == AMARK_ERROR_NONE);

    while ((reader.position < reader.size) && (ops < MAX_OPS))
    {
        step(&reader);
        ops++;
    }

    check_final();

    for (i = 0; i < 2; i++)
    {
        amark_document_destroy(state.documents[i]);
        amark_buffer_destroy(state.before[i]);
        amark_buffer_destroy(state.after[i]);
    }

    amark_buffer_destroy(state.scratch);
    amark_syntax_set_destroy(gf_set);
    amark_syntax_set_destroy(f_set);
    CHECK(fuzz_allocator.live == 0);
    CHECK(amark_allocator_set(NULL) == AMARK_ERROR_NONE);

    return 0;
}
