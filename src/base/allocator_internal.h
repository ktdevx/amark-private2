/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * Size arithmetic with overflow checks, for the code of the library. Internal:
 * not part of the public API and not exported.
 */
#ifndef AMARK_BASE_ALLOCATOR_INTERNAL_H
#define AMARK_BASE_ALLOCATOR_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Stores a + b in *out and returns true, or returns false, leaving *out
 * unchanged, when the sum does not fit in size_t. */
static inline bool amark_size_add(size_t a, size_t b, size_t *out)
{
    if (a > SIZE_MAX - b)
    {
        return false;
    }
    *out = a + b;
    return true;
}

/* Stores a * b in *out and returns true, or returns false, leaving *out
 * unchanged, when the product does not fit in size_t. */
static inline bool amark_size_mul(size_t a, size_t b, size_t *out)
{
    if (b != 0 && a > SIZE_MAX / b)
    {
        return false;
    }
    *out = a * b;
    return true;
}

#endif /* AMARK_BASE_ALLOCATOR_INTERNAL_H */
