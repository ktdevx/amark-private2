/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The size arithmetic with overflow checks of src/base/allocator_internal.h.
 */
#include "base/allocator_internal.h"

#include <stdint.h>

#include <naos/naos.h>

NAOS_TEST(add_in_range)
{
    size_t out = 0;

    NAOS_CHECK_TRUE(amark_size_add(0, 0, &out));
    NAOS_CHECK_UINT_EQ(0, out);
    NAOS_CHECK_TRUE(amark_size_add(3, 4, &out));
    NAOS_CHECK_UINT_EQ(7, out);
    NAOS_CHECK_TRUE(amark_size_add(SIZE_MAX, 0, &out));
    NAOS_CHECK_UINT_EQ(SIZE_MAX, out);
    NAOS_CHECK_TRUE(amark_size_add(SIZE_MAX - 1, 1, &out));
    NAOS_CHECK_UINT_EQ(SIZE_MAX, out);
}

NAOS_TEST(add_overflow_leaves_out_unchanged)
{
    size_t out = 42;

    NAOS_CHECK_FALSE(amark_size_add(SIZE_MAX, 1, &out));
    NAOS_CHECK_FALSE(amark_size_add(1, SIZE_MAX, &out));
    NAOS_CHECK_FALSE(amark_size_add(SIZE_MAX, SIZE_MAX, &out));
    NAOS_CHECK_FALSE(amark_size_add(SIZE_MAX / 2 + 1, SIZE_MAX / 2 + 1, &out));
    NAOS_CHECK_UINT_EQ(42, out);
}

NAOS_TEST(mul_in_range)
{
    size_t out = 1;

    NAOS_CHECK_TRUE(amark_size_mul(0, SIZE_MAX, &out));
    NAOS_CHECK_UINT_EQ(0, out);
    NAOS_CHECK_TRUE(amark_size_mul(SIZE_MAX, 0, &out));
    NAOS_CHECK_UINT_EQ(0, out);
    NAOS_CHECK_TRUE(amark_size_mul(6, 7, &out));
    NAOS_CHECK_UINT_EQ(42, out);
    NAOS_CHECK_TRUE(amark_size_mul(SIZE_MAX, 1, &out));
    NAOS_CHECK_UINT_EQ(SIZE_MAX, out);
    NAOS_CHECK_TRUE(amark_size_mul(SIZE_MAX / 2, 2, &out));
    NAOS_CHECK_UINT_EQ(SIZE_MAX - 1, out);
}

NAOS_TEST(mul_overflow_leaves_out_unchanged)
{
    size_t half = (size_t)1 << (sizeof(size_t) * 4);
    size_t out = 42;

    NAOS_CHECK_FALSE(amark_size_mul(SIZE_MAX, 2, &out));
    NAOS_CHECK_FALSE(amark_size_mul(2, SIZE_MAX, &out));
    NAOS_CHECK_FALSE(amark_size_mul(SIZE_MAX / 2 + 1, 2, &out));
    NAOS_CHECK_FALSE(amark_size_mul(half, half, &out));
    NAOS_CHECK_UINT_EQ(42, out);
}

NAOS_SUITE(size_arithmetic,
    NAOS_ADD_TEST(add_in_range),
    NAOS_ADD_TEST(add_overflow_leaves_out_unchanged),
    NAOS_ADD_TEST(mul_in_range),
    NAOS_ADD_TEST(mul_overflow_leaves_out_unchanged)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(size_arithmetic)
)
