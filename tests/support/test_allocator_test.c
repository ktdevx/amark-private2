/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The test allocator and the exhaustive out-of-memory sweep (4.5, 4.6).
 *
 * This file is built twice, once linked with amark and once with amark_internal,
 * which also checks that amark_test_support goes with either without duplicate
 * symbols. While the test allocator is set, the tests use no assertion that stops
 * the test; they restore the default allocator first and check afterwards. The
 * teardown restores it as well, for the tests that fail on purpose.
 *
 * The tests marked XFAIL must fail: they show that a misuse of the allocator, or a
 * bad scenario given to AMARK_CHECK_OOM_SWEEP, is reported as a test failure.
 */
#include <amark/amark.h>

#include <stdlib.h>
#include <string.h>

#include <naos/naos.h>

#include "test_allocator.h"

static amark_test_allocator_t t;

NAOS_TEARDOWN(restore)
{
    amark_allocator_set(NULL);
}

/* Sets the test allocator t, freshly initialized. */
static amark_error_t use_test_allocator(void)
{
    amark_test_allocator_init(&t);
    return amark_allocator_set(amark_test_allocator_activate(&t));
}

/* --- The test allocator (4.5) --- */

NAOS_TEST(counts_calls_blocks_and_bytes)
{
    amark_error_t set = use_test_allocator();
    unsigned char *p = amark_malloc(10);
    unsigned char *z = amark_calloc(4, 5);
    size_t after_two_blocks = t.live_bytes;
    unsigned char *q;
    int zeroed = 1;
    size_t i;
    size_t blocks_before_free;
    size_t calls;
    size_t peak;

    if (p != NULL)
    {
        memcpy(p, "0123456789", 10);
    }
    if (z != NULL)
    {
        for (i = 0; i < 20; i++)
        {
            zeroed = zeroed && z[i] == 0;
        }
    }
    q = amark_realloc(p, 100);
    blocks_before_free = t.live_blocks;
    peak = t.peak_bytes;
    calls = t.calls;
    amark_free(z);
    amark_free(q);
    amark_allocator_set(NULL);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, set);
    NAOS_CHECK_NOT_NULL(p);
    NAOS_CHECK_NOT_NULL(z);
    NAOS_CHECK_NOT_NULL(q);
    NAOS_CHECK_TRUE(zeroed);
    NAOS_CHECK_UINT_EQ(30, after_two_blocks);
    NAOS_CHECK_UINT_EQ(2, blocks_before_free);
    NAOS_CHECK_UINT_EQ(3, calls);
    NAOS_CHECK_UINT_EQ(120, peak);
    NAOS_CHECK_UINT_EQ(0, t.live_blocks);
    NAOS_CHECK_UINT_EQ(0, t.live_bytes);
    NAOS_CHECK_UINT_EQ(120, t.peak_bytes);
    NAOS_CHECK_UINT_EQ(0, t.failures);
}

NAOS_TEST(realloc_keeps_contents_and_shrinks_bytes)
{
    amark_error_t set = use_test_allocator();
    unsigned char *p = amark_malloc(8);
    unsigned char *q = NULL;
    int kept = 0;
    size_t bytes_after_grow = 0;
    size_t bytes_after_shrink = 0;

    if (p != NULL)
    {
        memcpy(p, "abcdefgh", 8);
        q = amark_realloc(p, 4096);
        bytes_after_grow = t.live_bytes;
    }
    if (q != NULL)
    {
        p = q;
        q = amark_realloc(p, 3);
        bytes_after_shrink = t.live_bytes;
    }
    if (q != NULL)
    {
        kept = memcmp(q, "abc", 3) == 0;
        amark_free(q);
    }
    else
    {
        amark_free(p);
    }
    amark_allocator_set(NULL);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, set);
    NAOS_CHECK_TRUE(kept);
    NAOS_CHECK_UINT_EQ(4096, bytes_after_grow);
    NAOS_CHECK_UINT_EQ(3, bytes_after_shrink);
    NAOS_CHECK_UINT_EQ(4096, t.peak_bytes);
    NAOS_CHECK_UINT_EQ(0, t.live_blocks);
    NAOS_CHECK_UINT_EQ(0, t.live_bytes);
}

NAOS_TEST(fail_at_fails_only_that_malloc)
{
    void *a;
    void *b;
    void *c;

    use_test_allocator();
    t.fail_at = 2;
    a = amark_malloc(1);
    b = amark_malloc(1);
    c = amark_malloc(1);
    amark_free(a);
    amark_free(c);
    amark_allocator_set(NULL);

    NAOS_CHECK_NOT_NULL(a);
    NAOS_CHECK_NULL(b);
    NAOS_CHECK_NOT_NULL(c);
    NAOS_CHECK_UINT_EQ(3, t.calls);
    NAOS_CHECK_UINT_EQ(1, t.failures);
    NAOS_CHECK_UINT_EQ(0, t.live_blocks);
}

NAOS_TEST(fail_at_fails_calloc)
{
    void *z;

    use_test_allocator();
    t.fail_at = 1;
    z = amark_calloc(2, 2);
    amark_allocator_set(NULL);

    NAOS_CHECK_NULL(z);
    NAOS_CHECK_UINT_EQ(1, t.calls);
    NAOS_CHECK_UINT_EQ(1, t.failures);
    NAOS_CHECK_UINT_EQ(0, t.live_blocks);
}

NAOS_TEST(fail_at_fails_realloc_and_keeps_the_block)
{
    unsigned char *p;
    unsigned char *q = NULL;
    int kept = 0;
    size_t blocks;
    size_t bytes;

    use_test_allocator();
    t.fail_at = 2;
    p = amark_malloc(4);
    if (p != NULL)
    {
        memcpy(p, "wxyz", 4);
        q = amark_realloc(p, 1000);
        kept = memcmp(p, "wxyz", 4) == 0;
    }
    blocks = t.live_blocks;
    bytes = t.live_bytes;
    amark_free(q != NULL ? q : p);
    amark_allocator_set(NULL);

    NAOS_CHECK_NOT_NULL(p);
    NAOS_CHECK_NULL(q);
    NAOS_CHECK_TRUE(kept);
    NAOS_CHECK_UINT_EQ(1, blocks);
    NAOS_CHECK_UINT_EQ(4, bytes);
    NAOS_CHECK_UINT_EQ(1, t.failures);
    NAOS_CHECK_UINT_EQ(0, t.live_blocks);
}

NAOS_TEST(fail_at_zero_never_fails)
{
    void *p[5];
    size_t i;
    int all = 1;

    use_test_allocator();
    for (i = 0; i < 5; i++)
    {
        p[i] = amark_malloc(i + 1);
        all = all && p[i] != NULL;
    }
    for (i = 0; i < 5; i++)
    {
        amark_free(p[i]);
    }
    amark_allocator_set(NULL);

    NAOS_CHECK_TRUE(all);
    NAOS_CHECK_UINT_EQ(0, t.failures);
    NAOS_CHECK_UINT_EQ(15, t.peak_bytes);
}

NAOS_TEST(reset_counters_keeps_the_live_blocks)
{
    void *a;
    void *b;
    size_t live_blocks;
    size_t live_bytes;

    use_test_allocator();
    t.fail_at = 1;
    a = amark_malloc(5);   /* fails */
    a = amark_malloc(100);
    b = amark_malloc(7);
    amark_free(a);
    amark_test_allocator_reset_counters(&t);
    live_blocks = t.live_blocks;
    live_bytes = t.live_bytes;
    amark_free(b);
    amark_allocator_set(NULL);

    NAOS_CHECK_UINT_EQ(0, t.calls);
    NAOS_CHECK_UINT_EQ(0, t.failures);
    NAOS_CHECK_UINT_EQ(1, live_blocks);
    NAOS_CHECK_UINT_EQ(7, live_bytes);
    /* The peak starts again from the live bytes. */
    NAOS_CHECK_UINT_EQ(7, t.peak_bytes);
    NAOS_CHECK_UINT_EQ(1, t.fail_at);
}

/* A pointer the test allocator never returned. */
static unsigned char not_allocated[64];

NAOS_TEST(free_of_unknown_pointer_fails)
{
    use_test_allocator();
    amark_free(not_allocated + 32);
    amark_allocator_set(NULL);
}

NAOS_TEST(double_free_fails)
{
    void *p;

    use_test_allocator();
    p = amark_malloc(16);
    amark_free(p);
    amark_free(p);
    amark_allocator_set(NULL);
}

NAOS_TEST(realloc_of_unknown_pointer_fails)
{
    use_test_allocator();
    (void)amark_realloc(not_allocated + 32, 128);
    amark_allocator_set(NULL);
}

/* --- The exhaustive out-of-memory sweep (4.6) --- */

/* Records the arguments of the set_allocator calls, and forwards them. */
static size_t set_calls;
static const amark_allocator_t *last_set;

static amark_error_t recording_set(const amark_allocator_t *allocator)
{
    set_calls++;
    last_set = allocator;
    return amark_allocator_set(allocator);
}

/* Allocates three blocks, frees them all, and reports a failure as OOM. */
static amark_error_t correct_scenario(void *context)
{
    void *a;
    void *b;
    void *c;

    (void)context;
    a = amark_malloc(10);
    if (a == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    b = amark_calloc(3, 4);
    if (b == NULL)
    {
        amark_free(a);
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    c = amark_realloc(a, 100);
    if (c == NULL)
    {
        amark_free(a);
        amark_free(b);
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    amark_free(b);
    amark_free(c);
    return AMARK_ERROR_NONE;
}

/* Like correct_scenario, but leaks the first block when the second fails. */
static amark_error_t leaking_scenario(void *context)
{
    void *a;
    void *b;

    (void)context;
    a = amark_malloc(10);
    if (a == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    b = amark_malloc(20);
    if (b == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY; /* a leaks */
    }
    amark_free(a);
    amark_free(b);
    return AMARK_ERROR_NONE;
}

/* Leaks only when every allocation succeeds. */
static amark_error_t leaking_on_success_scenario(void *context)
{
    void *a;

    (void)context;
    a = amark_malloc(10);
    if (a == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    return AMARK_ERROR_NONE; /* a leaks */
}

/* Reports the failure of its second allocation as something other than OOM. */
static amark_error_t wrong_error_scenario(void *context)
{
    void *a;
    void *b;

    (void)context;
    a = amark_malloc(10);
    if (a == NULL)
    {
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    b = amark_malloc(20);
    if (b == NULL)
    {
        amark_free(a);
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    amark_free(a);
    amark_free(b);
    return AMARK_ERROR_NONE;
}

/* Fails on success: the run without an injected failure must return NONE. */
static amark_error_t failing_scenario(void *context)
{
    (void)context;
    return AMARK_ERROR_INVALID_STATE;
}

/* Allocates nothing. */
static amark_error_t no_allocation_scenario(void *context)
{
    int *runs = context;
    (*runs)++;
    return AMARK_ERROR_NONE;
}

/* Allocates and frees count blocks, one at a time. */
static amark_error_t many_allocations_scenario(void *context)
{
    size_t count = *(const size_t *)context;
    size_t i;

    for (i = 0; i < count; i++)
    {
        void *p = amark_malloc(1);
        if (p == NULL)
        {
            return AMARK_ERROR_OUT_OF_MEMORY;
        }
        amark_free(p);
    }
    return AMARK_ERROR_NONE;
}

NAOS_SETUP(reset_set_recorder)
{
    set_calls = 0;
    last_set = NULL;
}

NAOS_TEST(sweep_of_correct_scenario_succeeds)
{
    amark_test_oom_result_t r = amark_test_oom_sweep(recording_set, correct_scenario, NULL, 100);

    NAOS_CHECK_UINT_EQ(3, r.iterations);
    NAOS_CHECK_UINT_EQ(0, r.wrong_error_at);
    NAOS_CHECK_UINT_EQ(0, r.leak_at);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, r.final_error);
    NAOS_CHECK_FALSE(r.truncated);
}

NAOS_TEST(sweep_sets_the_allocator_each_run_and_restores_the_default)
{
    void *p;

    amark_test_oom_result_t r = amark_test_oom_sweep(recording_set, correct_scenario, NULL, 100);

    /* Each of the 4 runs sets the test allocator, and the end restores NULL. */
    NAOS_CHECK_UINT_EQ(3, r.iterations);
    NAOS_CHECK_UINT_GE(set_calls, 5);
    NAOS_CHECK_NULL(last_set);
    /* The default allocator is set again: its memory can go to free(). */
    p = amark_malloc(8);
    NAOS_CHECK_NOT_NULL(p);
    free(p);
}

NAOS_TEST(sweep_reports_a_leak)
{
    amark_test_oom_result_t r = amark_test_oom_sweep(recording_set, leaking_scenario, NULL, 100);

    NAOS_CHECK_UINT_EQ(2, r.iterations);
    NAOS_CHECK_UINT_EQ(2, r.leak_at);
    NAOS_CHECK_UINT_EQ(0, r.wrong_error_at);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, r.final_error);
    NAOS_CHECK_NULL(last_set);
}

NAOS_TEST(sweep_reports_a_leak_of_the_successful_run)
{
    amark_test_oom_result_t r =
        amark_test_oom_sweep(recording_set, leaking_on_success_scenario, NULL, 100);

    /* One injected run, then the run without a failure, the second, leaks. */
    NAOS_CHECK_UINT_EQ(1, r.iterations);
    NAOS_CHECK_UINT_EQ(2, r.leak_at);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, r.final_error);
}

NAOS_TEST(sweep_reports_an_error_other_than_oom)
{
    amark_test_oom_result_t r =
        amark_test_oom_sweep(recording_set, wrong_error_scenario, NULL, 100);

    NAOS_CHECK_UINT_EQ(2, r.iterations);
    NAOS_CHECK_UINT_EQ(2, r.wrong_error_at);
    NAOS_CHECK_UINT_EQ(0, r.leak_at);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, r.final_error);
}

NAOS_TEST(sweep_reports_the_final_error)
{
    amark_test_oom_result_t r = amark_test_oom_sweep(recording_set, failing_scenario, NULL, 100);

    NAOS_CHECK_UINT_EQ(0, r.iterations);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_STATE, r.final_error);
}

NAOS_TEST(sweep_of_scenario_without_allocation_runs_once)
{
    int runs = 0;
    amark_test_oom_result_t r =
        amark_test_oom_sweep(recording_set, no_allocation_scenario, &runs, 100);

    NAOS_CHECK_INT_EQ(1, runs);
    NAOS_CHECK_UINT_EQ(0, r.iterations);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, r.final_error);
    NAOS_CHECK_FALSE(r.truncated);
}

NAOS_TEST(sweep_stops_after_max_iterations)
{
    size_t count = 10;
    amark_test_oom_result_t r =
        amark_test_oom_sweep(recording_set, many_allocations_scenario, &count, 4);

    NAOS_CHECK_UINT_EQ(4, r.iterations);
    NAOS_CHECK_TRUE(r.truncated);
    NAOS_CHECK_NULL(last_set);
}

NAOS_TEST(check_macro_passes_for_correct_scenario)
{
    size_t count = 50;

    AMARK_CHECK_OOM_SWEEP(correct_scenario, NULL);
    AMARK_CHECK_OOM_SWEEP(many_allocations_scenario, &count);
}

NAOS_TEST(check_macro_fails_for_leaking_scenario)
{
    AMARK_CHECK_OOM_SWEEP(leaking_scenario, NULL);
}

NAOS_TEST(check_macro_fails_for_wrong_error)
{
    AMARK_CHECK_OOM_SWEEP(wrong_error_scenario, NULL);
}

NAOS_TEST(check_macro_fails_for_final_error)
{
    AMARK_CHECK_OOM_SWEEP(failing_scenario, NULL);
}

NAOS_SUITE(test_allocator,
           NAOS_SET_TEARDOWN(restore),
           NAOS_ADD_TEST(counts_calls_blocks_and_bytes),
           NAOS_ADD_TEST(realloc_keeps_contents_and_shrinks_bytes),
           NAOS_ADD_TEST(fail_at_fails_only_that_malloc),
           NAOS_ADD_TEST(fail_at_fails_calloc),
           NAOS_ADD_TEST(fail_at_fails_realloc_and_keeps_the_block),
           NAOS_ADD_TEST(fail_at_zero_never_fails),
           NAOS_ADD_TEST(reset_counters_keeps_the_live_blocks),
           NAOS_ADD_TEST_XFAIL(free_of_unknown_pointer_fails,
                               "freeing a pointer the test allocator never returned is a failure"),
           NAOS_ADD_TEST_XFAIL(double_free_fails, "a double free is a failure"),
           NAOS_ADD_TEST_XFAIL(realloc_of_unknown_pointer_fails,
                               "reallocating an unknown pointer is a failure"))

NAOS_SUITE(oom_sweep,
           NAOS_SET_SETUP(reset_set_recorder),
           NAOS_SET_TEARDOWN(restore),
           NAOS_ADD_TEST(sweep_of_correct_scenario_succeeds),
           NAOS_ADD_TEST(sweep_sets_the_allocator_each_run_and_restores_the_default),
           NAOS_ADD_TEST(sweep_reports_a_leak),
           NAOS_ADD_TEST(sweep_reports_a_leak_of_the_successful_run),
           NAOS_ADD_TEST(sweep_reports_an_error_other_than_oom),
           NAOS_ADD_TEST(sweep_reports_the_final_error),
           NAOS_ADD_TEST(sweep_of_scenario_without_allocation_runs_once),
           NAOS_ADD_TEST(sweep_stops_after_max_iterations),
           NAOS_ADD_TEST(check_macro_passes_for_correct_scenario),
           NAOS_ADD_TEST_XFAIL(check_macro_fails_for_leaking_scenario,
                               "AMARK_CHECK_OOM_SWEEP reports a leak"),
           NAOS_ADD_TEST_XFAIL(check_macro_fails_for_wrong_error,
                               "AMARK_CHECK_OOM_SWEEP reports an error other than OOM"),
           NAOS_ADD_TEST_XFAIL(check_macro_fails_for_final_error,
                               "AMARK_CHECK_OOM_SWEEP reports a failing run without injection"))

NAOS_MAIN(NAOS_ADD_SUITE(test_allocator), NAOS_ADD_SUITE(oom_sweep))
