/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The internal definition of the buffer (src/base/buffer_internal.h): a buffer
 * embedded by value in a structure, amark_buffer_init (allocates nothing) and
 * amark_buffer_deinit, used with the public amark_buffer_* functions.
 */
#include "base/buffer_internal.h"

#include <amark/amark.h>

#include <stddef.h>

#include <naos/naos.h>

#include "test_allocator.h"
#include "test_check.h"

static amark_test_allocator_t counting;

NAOS_TEARDOWN(restore)
{
    amark_allocator_set(NULL);
}

/* An internal structure that embeds two buffers by value. */
typedef struct holder
{
    int tag;
    amark_buffer_t first;
    amark_buffer_t second;
} holder_t;

NAOS_TEST(init_gives_an_empty_buffer_without_allocating)
{
    holder_t h;

    amark_test_allocator_init(&counting);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_allocator_set(amark_test_allocator_activate(&counting)));
    amark_buffer_init(&h.first);
    amark_allocator_set(NULL);

    NAOS_CHECK_UINT_EQ(0, counting.calls);
    NAOS_CHECK_UINT_EQ(0, h.first.length);
    NAOS_CHECK_UINT_EQ(0, h.first.capacity);
    NAOS_REQUIRE_NOT_NULL(h.first.data);
    NAOS_CHECK_INT_EQ(0, h.first.data[0]);
    NAOS_CHECK_NOT_NULL(amark_buffer_data(&h.first));
    AMARK_CHECK_BUFFER_EQ(&h.first, "");
    /* Deinit of a buffer that never allocated frees nothing. */
    amark_buffer_deinit(&h.first);
}

NAOS_TEST(embedded_buffers_work_with_the_public_functions)
{
    holder_t h;

    h.tag = 42;
    amark_buffer_init(&h.first);
    amark_buffer_init(&h.second);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append(&h.first, "a\0b", 3));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_repeat(&h.second, 'z', 100));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_codepoint(&h.first, 0x3042));
    AMARK_CHECK_BUFFER_EQ(&h.first, "a\0b\xE3\x81\x82");
    NAOS_CHECK_UINT_EQ(6, h.first.length);
    NAOS_CHECK_UINT_GT(h.first.capacity, h.first.length);
    NAOS_CHECK_UINT_EQ(100, amark_buffer_length(&h.second));
    NAOS_CHECK_UINT_GT(h.second.capacity, h.second.length);
    NAOS_CHECK_INT_EQ(0, h.second.data[h.second.length]);
    NAOS_CHECK_INT_EQ(42, h.tag);

    amark_buffer_deinit(&h.first);
    amark_buffer_deinit(&h.second);
}

NAOS_TEST(deinit_frees_the_content_and_leaves_an_empty_buffer)
{
    holder_t h;
    size_t live_before_deinit;

    amark_test_allocator_init(&counting);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_allocator_set(amark_test_allocator_activate(&counting)));
    amark_buffer_init(&h.first);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_cstr(&h.first, "content"));
    live_before_deinit = counting.live_blocks;
    amark_buffer_deinit(&h.first);
    amark_allocator_set(NULL);

    NAOS_CHECK_UINT_EQ(1, live_before_deinit);
    NAOS_CHECK_UINT_EQ(0, counting.live_blocks);
    NAOS_CHECK_UINT_EQ(0, h.first.length);
    NAOS_CHECK_UINT_EQ(0, h.first.capacity);
    AMARK_CHECK_BUFFER_EQ(&h.first, "");

    /* Usable again after deinit, and deinit is safe to repeat. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_cstr(&h.first, "again"));
    AMARK_CHECK_BUFFER_EQ(&h.first, "again");
    amark_buffer_deinit(&h.first);
    amark_buffer_deinit(&h.first);
    AMARK_CHECK_BUFFER_EQ(&h.first, "");
}

NAOS_TEST(deinit_null_does_nothing)
{
    amark_buffer_deinit(NULL);
    NAOS_PASS();
}

NAOS_TEST(detach_from_an_embedded_buffer)
{
    holder_t h;
    char *data = NULL;
    size_t length = 0;

    amark_buffer_init(&h.first);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append(&h.first, "out", 3));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_detach(&h.first, &data, &length));
    NAOS_CHECK_UINT_EQ(3, length);
    NAOS_CHECK_STR_EQ("out", data);
    /* Detaching leaves the buffer as after init. */
    NAOS_CHECK_UINT_EQ(0, h.first.length);
    NAOS_CHECK_UINT_EQ(0, h.first.capacity);
    AMARK_CHECK_BUFFER_EQ(&h.first, "");
    amark_free(data);
    amark_buffer_deinit(&h.first);
}

NAOS_TEST(growth_follows_the_doubling_rule)
{
    /* The capacity grows to max(2 * capacity, required), required counting the
     * terminating NUL; reserve follows the same rule; truncate and clear keep it. */
    amark_buffer_t b;

    amark_buffer_init(&b);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_byte(&b, 'a'));
    NAOS_CHECK_UINT_EQ(2, b.capacity); /* max(0, 1 + 1) */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_byte(&b, 'b'));
    NAOS_CHECK_UINT_EQ(4, b.capacity); /* max(4, 3) */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_byte(&b, 'c'));
    NAOS_CHECK_UINT_EQ(4, b.capacity); /* 4 bytes fit: no growth */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_repeat(&b, 'd', 20));
    NAOS_CHECK_UINT_EQ(24, b.capacity); /* max(8, 3 + 20 + 1) */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_reserve(&b, 0));
    NAOS_CHECK_UINT_EQ(24, b.capacity); /* nothing to add */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_reserve(&b, 1));
    NAOS_CHECK_UINT_EQ(48, b.capacity); /* max(48, 23 + 1 + 1) */
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_reserve(&b, 100));
    NAOS_CHECK_UINT_EQ(124, b.capacity); /* max(96, 23 + 100 + 1) */
    NAOS_CHECK_UINT_EQ(23, b.length);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_truncate(&b, 1));
    amark_buffer_clear(&b);
    NAOS_CHECK_UINT_EQ(124, b.capacity);
    amark_buffer_deinit(&b);
}

NAOS_SUITE(buffer_internal,
    NAOS_SET_TEARDOWN(restore),
    NAOS_ADD_TEST(init_gives_an_empty_buffer_without_allocating),
    NAOS_ADD_TEST(embedded_buffers_work_with_the_public_functions),
    NAOS_ADD_TEST(deinit_frees_the_content_and_leaves_an_empty_buffer),
    NAOS_ADD_TEST(deinit_null_does_nothing),
    NAOS_ADD_TEST(detach_from_an_embedded_buffer),
    NAOS_ADD_TEST(growth_follows_the_doubling_rule)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(buffer_internal)
)
