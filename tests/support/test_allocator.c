/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The test allocator and the exhaustive out-of-memory sweep.
 *
 * The callbacks receive no user data, so the current test allocator and the list
 * of the live blocks are static variables of this file (the rule against global
 * mutable state applies to the library, not to its test support). Each block
 * starts with a header holding a magic value, the requested size, the test
 * allocator it is counted in, and its links in the list of live blocks. A pointer
 * is looked up in that list before its header is read, so that an unknown or
 * freed pointer is reported without reading memory around it.
 *
 * This file calls no Amark function: amark_allocator_set reaches the sweep as an
 * argument.
 */
#include "test_allocator.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The magic value of a live block's header. */
#define BLOCK_MAGIC ((size_t)0x5AFE1A7Bu)

typedef union block block_t;

typedef struct block_header
{
    size_t magic;
    size_t size;                   /* the size requested by the caller */
    amark_test_allocator_t *owner; /* the test allocator the block is counted in */
    block_t *prev;                 /* toward the newer blocks */
    block_t *next;                 /* toward the older blocks */
} block_header_t;

/* The header, padded so that the memory after it is aligned for any type, as the
 * memory from malloc is. */
union block
{
    block_header_t h;
    long double ld;
    long long ll;
    double d;
    void *p;
    void (*f)(void);
};

static amark_test_allocator_t *current;
static block_t *newest; /* the head of the list of live blocks */

static void link_block(block_t *b)
{
    b->h.prev = NULL;
    b->h.next = newest;
    if (newest != NULL)
    {
        newest->h.prev = b;
    }

    newest = b;
}

static void unlink_block(block_t *b)
{
    if (b->h.prev != NULL)
    {
        b->h.prev->h.next = b->h.next;
    }
    else
    {
        newest = b->h.next;
    }

    if (b->h.next != NULL)
    {
        b->h.next->h.prev = b->h.prev;
    }
}

static void add_bytes(amark_test_allocator_t *t, size_t size)
{
    t->live_bytes += size;
    if (t->live_bytes > t->peak_bytes)
    {
        t->peak_bytes = t->live_bytes;
    }
}

/* Returns the block of ptr, or fails the test when ptr is not a live block. */
static block_t *find_block(void *ptr, const char *operation)
{
    block_t *b;

    for (b = newest; b != NULL; b = b->h.next)
    {
        if ((void *)(b + 1) == ptr)
        {
            if (b->h.magic != BLOCK_MAGIC)
            {
                NAOS_NOTE("%s of %p", operation, ptr);
                NAOS_FAIL("test allocator: the header of a live block is corrupted");
            }

            return b;
        }
    }

    NAOS_NOTE("%s of %p", operation, ptr);
    /* naos_fail does not return (NAOS_NORETURN), so no return statement follows:
     * one would be unreachable code, which MSVC reports (C4702). */
    NAOS_FAIL("test allocator: unknown pointer, not allocated by the test allocator "
              "or already freed");
}

/* Counts an allocation call; returns 1 when it must fail. */
static int inject_failure(void)
{
    if (current == NULL)
    {
        NAOS_FAIL("test allocator: used without amark_test_allocator_activate");
    }

    current->calls++;
    if ((current->fail_at != 0) && (current->calls == current->fail_at))
    {
        current->failures++;
        return 1;
    }

    return 0;
}

static void *new_block(size_t size, int zero)
{
    block_t *b;

    if (size > (SIZE_MAX - sizeof(block_t)))
    {
        return NULL;
    }

    b = zero ? calloc(1, sizeof(block_t) + size) : malloc(sizeof(block_t) + size);
    if (b == NULL)
    {
        return NULL;
    }

    b->h.magic = BLOCK_MAGIC;
    b->h.size = size;
    b->h.owner = current;
    link_block(b);
    current->live_blocks++;
    add_bytes(current, size);
    return b + 1;
}

static void *test_malloc(size_t size)
{
    if (inject_failure())
    {
        return NULL;
    }

    return new_block(size, 0);
}

static void *test_calloc(size_t count, size_t size)
{
    if (inject_failure())
    {
        return NULL;
    }

    if ((size != 0) && (count > (SIZE_MAX / size)))
    {
        return NULL;
    }

    return new_block(count * size, 1);
}

static void *test_realloc(void *ptr, size_t new_size)
{
    block_t *b = find_block(ptr, "realloc");
    block_t *moved;
    amark_test_allocator_t *owner = b->h.owner;
    size_t old_size = b->h.size;

    if (inject_failure())
    {
        return NULL;
    }

    if (new_size > (SIZE_MAX - sizeof(block_t)))
    {
        return NULL;
    }

    /* The neighbors point to b, which realloc may move: unlink it first. */
    unlink_block(b);
    moved = realloc(b, sizeof(block_t) + new_size);
    if (moved == NULL)
    {
        link_block(b);
        return NULL;
    }

    moved->h.size = new_size;
    link_block(moved);
    owner->live_bytes -= old_size;
    add_bytes(owner, new_size);
    return moved + 1;
}

static void test_free(void *ptr)
{
    block_t *b = find_block(ptr, "free");
    amark_test_allocator_t *owner = b->h.owner;

    unlink_block(b);
    owner->live_blocks--;
    owner->live_bytes -= b->h.size;
    b->h.magic = 0;
    free(b);
}

static const amark_allocator_t test_allocator = {test_malloc, test_calloc, test_realloc,
                                                 test_free};

void amark_test_allocator_init(amark_test_allocator_t *t)
{
    memset(t, 0, sizeof(*t));
}

const amark_allocator_t *amark_test_allocator_activate(amark_test_allocator_t *t)
{
    current = t;
    return &test_allocator;
}

void amark_test_allocator_reset_counters(amark_test_allocator_t *t)
{
    t->calls = 0;
    t->failures = 0;
    t->peak_bytes = t->live_bytes;
}

/* Frees the live blocks counted in t, as a run of the sweep leaked them. */
static void release_blocks(amark_test_allocator_t *t)
{
    block_t *b = newest;

    while (b != NULL)
    {
        block_t *next = b->h.next;
        if (b->h.owner == t)
        {
            unlink_block(b);
            b->h.magic = 0;
            free(b);
        }

        b = next;
    }

    t->live_blocks = 0;
    t->live_bytes = 0;
}

/* The test allocator of the sweep. Static, so that the global allocator never
 * points to a dead one. */
static amark_test_allocator_t sweep_allocator;

amark_test_oom_result_t amark_test_oom_sweep(amark_test_set_allocator_t set_allocator,
                                             amark_test_oom_scenario_t scenario,
                                             void *context, size_t max_iterations)
{
    amark_test_oom_result_t result;
    size_t fail_at;

    memset(&result, 0, sizeof(result));
    result.final_error = AMARK_ERROR_NONE;

    for (fail_at = 1;; fail_at++)
    {
        amark_error_t error;
        int injected;

        if (fail_at > max_iterations)
        {
            result.truncated = 1;
            break;
        }

        amark_test_allocator_init(&sweep_allocator);
        sweep_allocator.fail_at = fail_at;
        error = set_allocator(amark_test_allocator_activate(&sweep_allocator));
        if (error != AMARK_ERROR_NONE)
        {
            result.final_error = error;
            break;
        }

        error = scenario(context);
        injected = sweep_allocator.failures != 0;

        if (sweep_allocator.live_blocks != 0)
        {
            if (result.leak_at == 0)
            {
                result.leak_at = fail_at;
            }

            release_blocks(&sweep_allocator);
        }

        if (!injected)
        {
            result.final_error = error;
            break;
        }

        result.iterations++;
        if ((error != AMARK_ERROR_OUT_OF_MEMORY) && (result.wrong_error_at == 0))
        {
            result.wrong_error_at = fail_at;
        }
    }

    set_allocator(NULL);
    return result;
}

int amark_test_oom_check(amark_test_oom_result_t result, const char *expr, const char *file,
                         int line)
{
    int ok = (result.final_error == AMARK_ERROR_NONE) && (result.wrong_error_at == 0) &&
             (result.leak_at == 0) && !result.truncated;

    NAOS_NOTE("runs with a failure: %lu, wrong error at run: %lu, leak at run: %lu, "
              "final error: %d, truncated: %d",
              (unsigned long)result.iterations, (unsigned long)result.wrong_error_at,
              (unsigned long)result.leak_at, (int)result.final_error, result.truncated);
    return naos_check(ok, expr, file, line);
}
