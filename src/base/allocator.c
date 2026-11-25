/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

#include <amark/allocator.h>

#include "base/allocator_internal.h"

#include <stdlib.h>

/* The default allocator: the only place in Amark that uses the allocation
 * functions of the C standard library directly. */
static const amark_allocator_t default_allocator = {malloc, calloc, realloc, free};

/* The global allocator setting: the only mutable global state of Amark. */
static amark_allocator_t current_allocator = {malloc, calloc, realloc, free};

amark_error_t amark_allocator_set(const amark_allocator_t *allocator)
{
    if (allocator == NULL) {
        current_allocator = default_allocator;
        return AMARK_ERROR_NONE;
    }
    if (allocator->malloc_fn == NULL || allocator->calloc_fn == NULL ||
        allocator->realloc_fn == NULL || allocator->free_fn == NULL) {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    current_allocator = *allocator;
    return AMARK_ERROR_NONE;
}

void *amark_malloc(size_t size)
{
    if (size == 0) {
        return NULL;
    }
    return current_allocator.malloc_fn(size);
}

void *amark_calloc(size_t count, size_t size)
{
    size_t total;

    if (count == 0 || size == 0) {
        return NULL;
    }
    if (!amark_size_mul(count, size, &total)) {
        return NULL;
    }
    return current_allocator.calloc_fn(count, size);
}

void *amark_realloc(void *ptr, size_t new_size)
{
    if (ptr == NULL) {
        return amark_malloc(new_size);
    }
    if (new_size == 0) {
        current_allocator.free_fn(ptr);
        return NULL;
    }
    return current_allocator.realloc_fn(ptr, new_size);
}

void amark_free(void *ptr)
{
    if (ptr != NULL) {
        current_allocator.free_fn(ptr);
    }
}
