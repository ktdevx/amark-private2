/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The global allocator (11.1, 11.2, 11.3, 11.5, 11.6, 11.7, 11.8).
 *
 * A recording allocator, local to this file, counts the calls of each callback and
 * remembers their last arguments, then forwards them to the C standard library.
 * Every test that sets it restores the default allocator in the teardown, so a
 * failed check does not leave it set for the next test.
 */
#include <amark/amark.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <naos/naos.h>

typedef struct recorder
{
    int malloc_calls;
    int calloc_calls;
    int realloc_calls;
    int free_calls;
    size_t last_size;
    size_t last_count;
    void *last_ptr;
} recorder_t;

static recorder_t rec;

static void *rec_malloc(size_t size)
{
    rec.malloc_calls++;
    rec.last_size = size;
    return malloc(size);
}

static void *rec_calloc(size_t count, size_t size)
{
    rec.calloc_calls++;
    rec.last_count = count;
    rec.last_size = size;
    return calloc(count, size);
}

static void *rec_realloc(void *ptr, size_t new_size)
{
    rec.realloc_calls++;
    rec.last_ptr = ptr;
    rec.last_size = new_size;
    return realloc(ptr, new_size);
}

static void rec_free(void *ptr)
{
    rec.free_calls++;
    rec.last_ptr = ptr;
    free(ptr);
}

static const amark_allocator_t recording = {rec_malloc, rec_calloc, rec_realloc, rec_free};

static int total_calls(void)
{
    return rec.malloc_calls + rec.calloc_calls + rec.realloc_calls + rec.free_calls;
}

NAOS_SETUP(reset)
{
    memset(&rec, 0, sizeof(rec));
}

NAOS_TEARDOWN(restore)
{
    amark_allocator_set(NULL);
}

/* Sets the recording allocator and stops the test if that fails. */
#define SET_RECORDING() NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_allocator_set(&recording))

NAOS_TEST(layout_is_the_four_callbacks)
{
    /* 11.6: exactly malloc, calloc, realloc and free, in this order. */
    NAOS_CHECK_UINT_EQ(0, offsetof(amark_allocator_t, malloc_fn));
    NAOS_CHECK_UINT_EQ(sizeof(void (*)(void)), offsetof(amark_allocator_t, calloc_fn));
    NAOS_CHECK_UINT_EQ(2 * sizeof(void (*)(void)), offsetof(amark_allocator_t, realloc_fn));
    NAOS_CHECK_UINT_EQ(3 * sizeof(void (*)(void)), offsetof(amark_allocator_t, free_fn));
    NAOS_CHECK_UINT_EQ(4 * sizeof(void (*)(void)), sizeof(amark_allocator_t));
}

NAOS_TEST(default_allocator_works_without_setting)
{
    /* 11.2: the memory comes from malloc and calloc. */
    unsigned char *p = amark_malloc(16);
    unsigned char *z = amark_calloc(4, 8);
    size_t i;

    NAOS_REQUIRE_NOT_NULL(p);
    NAOS_REQUIRE_NOT_NULL(z);
    memset(p, 0xAB, 16);
    for (i = 0; i < 32; i++)
    {
        NAOS_CHECK_UINT_EQ(0, z[i]);
    }
    p = amark_realloc(p, 64);
    NAOS_REQUIRE_NOT_NULL(p);
    NAOS_CHECK_UINT_EQ(0xAB, p[15]);
    /* The default allocator is the C standard library: free() accepts its memory. */
    free(p);
    amark_free(z);
    NAOS_CHECK_INT_EQ(0, total_calls());
}

NAOS_TEST(set_routes_every_function_through_the_allocator)
{
    /* 11.1, 11.3, 11.8 */
    unsigned char *p;
    unsigned char *q;
    unsigned char *z;

    SET_RECORDING();

    p = amark_malloc(10);
    NAOS_REQUIRE_NOT_NULL(p);
    NAOS_CHECK_INT_EQ(1, rec.malloc_calls);
    NAOS_CHECK_UINT_EQ(10, rec.last_size);
    memcpy(p, "0123456789", 10);

    q = amark_realloc(p, 100);
    NAOS_REQUIRE_NOT_NULL(q);
    NAOS_CHECK_INT_EQ(1, rec.realloc_calls);
    NAOS_CHECK_PTR_EQ(p, rec.last_ptr);
    NAOS_CHECK_UINT_EQ(100, rec.last_size);
    NAOS_CHECK_MEM_EQ(q, 10, "0123456789", 10);

    z = amark_calloc(3, 5);
    NAOS_REQUIRE_NOT_NULL(z);
    NAOS_CHECK_INT_EQ(1, rec.calloc_calls);
    NAOS_CHECK_UINT_EQ(3, rec.last_count);
    NAOS_CHECK_UINT_EQ(5, rec.last_size);

    amark_free(q);
    NAOS_CHECK_INT_EQ(1, rec.free_calls);
    NAOS_CHECK_PTR_EQ(q, rec.last_ptr);
    amark_free(z);
    NAOS_CHECK_INT_EQ(2, rec.free_calls);
    NAOS_CHECK_PTR_EQ(z, rec.last_ptr);

    NAOS_CHECK_INT_EQ(1, rec.malloc_calls);
    NAOS_CHECK_INT_EQ(1, rec.calloc_calls);
    NAOS_CHECK_INT_EQ(1, rec.realloc_calls);
}

NAOS_TEST(calloc_uses_calloc_fn_and_returns_zeroed_memory)
{
    /* calloc_fn, never malloc_fn; every byte is zero. */
    unsigned char *z;
    size_t i;

    SET_RECORDING();
    z = amark_calloc(7, 9);
    NAOS_REQUIRE_NOT_NULL(z);
    NAOS_CHECK_INT_EQ(1, rec.calloc_calls);
    NAOS_CHECK_INT_EQ(0, rec.malloc_calls);
    for (i = 0; i < 63; i++)
    {
        NAOS_CHECK_UINT_EQ(0, z[i]);
    }
    amark_free(z);
}

NAOS_TEST(realloc_of_null_calls_malloc_fn)
{
    void *p;

    SET_RECORDING();
    p = amark_realloc(NULL, 24);
    NAOS_REQUIRE_NOT_NULL(p);
    NAOS_CHECK_INT_EQ(1, rec.malloc_calls);
    NAOS_CHECK_UINT_EQ(24, rec.last_size);
    NAOS_CHECK_INT_EQ(0, rec.realloc_calls);
    amark_free(p);
}

NAOS_TEST(free_of_null_calls_nothing)
{
    SET_RECORDING();
    amark_free(NULL);
    NAOS_CHECK_INT_EQ(0, total_calls());
}

NAOS_TEST(set_null_restores_the_default)
{
    /* 11.1, 11.2 */
    void *p;

    SET_RECORDING();
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_allocator_set(NULL));
    p = amark_malloc(8);
    NAOS_CHECK_NOT_NULL(p);
    p = amark_realloc(p, 16);
    NAOS_CHECK_NOT_NULL(p);
    amark_free(p);
    amark_free(amark_calloc(2, 2));
    NAOS_CHECK_INT_EQ(0, total_calls());
}

NAOS_TEST(set_copies_the_allocator)
{
    /* The setting is a copy: changing or losing the argument afterwards has no
     * effect. */
    amark_allocator_t local = recording;
    void *p;

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_allocator_set(&local));
    memset(&local, 0, sizeof(local));
    p = amark_malloc(4);
    NAOS_REQUIRE_NOT_NULL(p);
    NAOS_CHECK_INT_EQ(1, rec.malloc_calls);
    amark_free(p);
    NAOS_CHECK_INT_EQ(1, rec.free_calls);
}

NAOS_TEST(incomplete_allocator_is_rejected)
{
    /* 11.5: each missing callback is rejected, and the setting stays as it was. */
    int missing;
    void *p;

    SET_RECORDING();
    for (missing = 0; missing < 4; missing++)
    {
        amark_allocator_t partial = recording;

        switch (missing)
        {
        case 0: partial.malloc_fn = NULL; break;
        case 1: partial.calloc_fn = NULL; break;
        case 2: partial.realloc_fn = NULL; break;
        default: partial.free_fn = NULL; break;
        }
        NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_allocator_set(&partial));
    }
    {
        amark_allocator_t empty;

        memset(&empty, 0, sizeof(empty));
        NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_allocator_set(&empty));
    }

    /* Still the recording allocator. */
    p = amark_malloc(4);
    NAOS_REQUIRE_NOT_NULL(p);
    NAOS_CHECK_INT_EQ(1, rec.malloc_calls);
    amark_free(p);
    NAOS_CHECK_INT_EQ(1, rec.free_calls);
}

NAOS_TEST(incomplete_allocator_keeps_the_default)
{
    /* 11.5 from the default allocator: a rejected setting does not replace it. */
    amark_allocator_t partial = recording;
    void *p;

    partial.free_fn = NULL;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_allocator_set(&partial));
    p = amark_malloc(4);
    NAOS_CHECK_NOT_NULL(p);
    amark_free(p);
    NAOS_CHECK_INT_EQ(0, total_calls());
}

NAOS_TEST(calloc_overflow_calls_nothing)
{
    /* 11.7 */
    SET_RECORDING();
    NAOS_CHECK_NULL(amark_calloc(SIZE_MAX, 2));
    NAOS_CHECK_NULL(amark_calloc(2, SIZE_MAX));
    NAOS_CHECK_NULL(amark_calloc(SIZE_MAX / 2 + 1, 2));
    NAOS_CHECK_NULL(amark_calloc((size_t)1 << (sizeof(size_t) * 4),
                                 (size_t)1 << (sizeof(size_t) * 4)));
    NAOS_CHECK_INT_EQ(0, total_calls());
}

NAOS_TEST(zero_size_allocations_call_nothing)
{
    SET_RECORDING();
    NAOS_CHECK_NULL(amark_malloc(0));
    NAOS_CHECK_NULL(amark_calloc(0, 8));
    NAOS_CHECK_NULL(amark_calloc(8, 0));
    NAOS_CHECK_NULL(amark_calloc(0, 0));
    NAOS_CHECK_NULL(amark_calloc(0, SIZE_MAX));
    NAOS_CHECK_NULL(amark_calloc(SIZE_MAX, 0));
    NAOS_CHECK_NULL(amark_realloc(NULL, 0));
    NAOS_CHECK_INT_EQ(0, total_calls());
}

NAOS_TEST(realloc_to_zero_frees_only)
{
    void *p;

    SET_RECORDING();
    p = amark_malloc(32);
    NAOS_REQUIRE_NOT_NULL(p);
    memset(&rec, 0, sizeof(rec));

    NAOS_CHECK_NULL(amark_realloc(p, 0));
    NAOS_CHECK_INT_EQ(1, rec.free_calls);
    NAOS_CHECK_PTR_EQ(p, rec.last_ptr);
    NAOS_CHECK_INT_EQ(1, total_calls());
}

NAOS_SUITE(allocator,
    NAOS_SET_SETUP(reset),
    NAOS_SET_TEARDOWN(restore),
    NAOS_ADD_TEST(layout_is_the_four_callbacks),
    NAOS_ADD_TEST(default_allocator_works_without_setting),
    NAOS_ADD_TEST(set_routes_every_function_through_the_allocator),
    NAOS_ADD_TEST(calloc_uses_calloc_fn_and_returns_zeroed_memory),
    NAOS_ADD_TEST(realloc_of_null_calls_malloc_fn),
    NAOS_ADD_TEST(free_of_null_calls_nothing),
    NAOS_ADD_TEST(set_null_restores_the_default),
    NAOS_ADD_TEST(set_copies_the_allocator),
    NAOS_ADD_TEST(incomplete_allocator_is_rejected),
    NAOS_ADD_TEST(incomplete_allocator_keeps_the_default),
    NAOS_ADD_TEST(calloc_overflow_calls_nothing),
    NAOS_ADD_TEST(zero_size_allocations_call_nothing),
    NAOS_ADD_TEST(realloc_to_zero_frees_only)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(allocator)
)
