/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The test allocator and the exhaustive out-of-memory sweep (4.5, 4.6).
 *
 * amark_test_support calls no Amark function and uses only the types of the public
 * headers, so it goes with either amark or amark_internal. The macros that need an
 * Amark function (amark_allocator_set) expand it at the call site.
 *
 * Using the test allocator:
 *     static amark_test_allocator_t t;      (static storage: see below)
 *     amark_test_allocator_init(&t);
 *     amark_allocator_set(amark_test_allocator_activate(&t));
 *     ... the code under test ...
 *     amark_allocator_set(NULL);
 *     ... check t.calls, t.live_blocks, ... ...
 *   - Keep t in static storage, and set it again at the start of every test that
 *     uses it, so that an interrupted test leaves no dangling pointer.
 *   - While it is set, use no assertion that stops the test (NAOS_REQUIRE_*):
 *     restore the default allocator first, then check. Restore it in the teardown
 *     too.
 *   - Only one t is current at a time; switch to another t (or init the current
 *     one) only when the current t has no live block.
 *   - While it is set, do not use Amark from several threads.
 *
 * Every block carries a small header with a magic value and its size. Freeing or
 * reallocating a pointer the test allocator did not return, or one already freed,
 * fails the running test with NAOS_FAIL (which stops it).
 */
#ifndef AMARK_TEST_ALLOCATOR_H
#define AMARK_TEST_ALLOCATOR_H

#include <stddef.h>

#include <amark/allocator.h>
#include <amark/error.h>

#include <naos/naos.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* A test allocator: its settings and counters. */
typedef struct amark_test_allocator
{
    size_t fail_at;     /* fail the allocation (malloc_fn, calloc_fn, realloc_fn) with
                           this 1-based number; 0 fails none */
    size_t calls;       /* the allocation calls (malloc_fn, calloc_fn, realloc_fn) */
    size_t failures;    /* the failures injected */
    size_t live_blocks; /* the blocks not freed */
    size_t live_bytes;  /* their requested sizes, summed */
    size_t peak_bytes;  /* the maximum of live_bytes */
} amark_test_allocator_t;

/* Sets every field of t to 0: no failure injected, all counters 0. */
void amark_test_allocator_init(amark_test_allocator_t *t);

/* Makes t the test allocator the callbacks use, and returns the allocator to pass
 * to amark_allocator_set. The returned allocator is the same constant for every
 * t. */
const amark_allocator_t *amark_test_allocator_activate(amark_test_allocator_t *t);

/* Sets calls and failures to 0 and peak_bytes to live_bytes. fail_at and the live
 * blocks are kept. */
void amark_test_allocator_reset_counters(amark_test_allocator_t *t);

/*
 * A scenario of the sweep. It frees everything it creates before it returns,
 * whether it succeeds or fails, and does not free or reallocate memory allocated
 * outside of it (such as an object in context). It returns AMARK_ERROR_OUT_OF_MEMORY
 * when an allocation fails.
 */
typedef amark_error_t (*amark_test_oom_scenario_t)(void *context);

/* amark_allocator_set, passed by the caller. */
typedef amark_error_t (*amark_test_set_allocator_t)(const amark_allocator_t *allocator);

typedef struct amark_test_oom_result
{
    size_t iterations;         /* the runs with a failure injected */
    size_t wrong_error_at;     /* the first run (its fail_at) that returned another
                                  error than OOM; 0 if none */
    size_t leak_at;            /* the first run that left a live block, counting the
                                  run without a failure as iterations + 1; 0 if none */
    amark_error_t final_error; /* the result of the run without a failure */
    int truncated;             /* 1 when the sweep stopped at max_iterations, before
                                  a run without a failure; final_error is then
                                  AMARK_ERROR_NONE and means nothing */
} amark_test_oom_result_t;

/*
 * Runs scenario(context) with fail_at = 1, 2, ..., until a run in which no failure
 * was injected, or until max_iterations runs with a failure. Each run uses a fresh
 * test allocator of the sweep, set with set_allocator; set_allocator(NULL) restores
 * the default before the sweep returns. Each run must return
 * AMARK_ERROR_OUT_OF_MEMORY (except the last one) and leave no live block; the
 * blocks it leaked are released after the run. Not to be nested, nor used while
 * another test allocator has live blocks.
 */
amark_test_oom_result_t amark_test_oom_sweep(amark_test_set_allocator_t set_allocator,
                                             amark_test_oom_scenario_t scenario,
                                             void *context, size_t max_iterations);

/* Records one assertion for result at file:line: it passes when final_error is
 * AMARK_ERROR_NONE, wrong_error_at and leak_at are 0, and truncated is 0. Returns 1
 * when it passes. Used by AMARK_CHECK_OOM_SWEEP. */
int amark_test_oom_check(amark_test_oom_result_t result, const char *expr, const char *file,
                         int line);

/* The max_iterations of AMARK_CHECK_OOM_SWEEP. */
#define AMARK_TEST_OOM_MAX_ITERATIONS ((size_t)100000)

/*
 * Sweeps scenario(context) with amark_allocator_set and reports, at the line of
 * the caller and without stopping the test, a leak, an error other than OOM, a
 * failing run without injection, or a sweep cut at AMARK_TEST_OOM_MAX_ITERATIONS.
 */
#define AMARK_CHECK_OOM_SWEEP(scenario, context) \
    ((void)amark_test_oom_check( \
        amark_test_oom_sweep(amark_allocator_set, (scenario), (context), \
                             AMARK_TEST_OOM_MAX_ITERATIONS), \
        "AMARK_CHECK_OOM_SWEEP(" #scenario ", " #context ")", __FILE__, __LINE__))

#ifdef __cplusplus
}
#endif

#endif /* AMARK_TEST_ALLOCATOR_H */
