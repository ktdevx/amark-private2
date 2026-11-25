/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The public API of the growable byte buffer.
 *
 * Suite buffer (10.5, 10.6, 11.3, 12.1, 12.2, 12.3, 12.6, 12.8): the result of
 * each operation, content with NUL bytes, appending the buffer's own content, the
 * rejected truncation, and detached content freed with amark_free.
 *
 * Suite buffer_guarantees (10.4, 10.8, 11.4, 12.4, 12.5, 12.7): the number of
 * allocations when growing in small pieces, the out-of-memory sweep of every
 * operation that can grow, the overflow of the required capacity, and two
 * buffers used in turn.
 */
#include <amark/amark.h>

#include <stdint.h>
#include <string.h>

#include <naos/naos.h>

#include "test_allocator.h"
#include "test_check.h"

/* The test allocator, set by the tests that count allocations; static so that no
 * dangling pointer stays set if a test stops early. */
static amark_test_allocator_t counting;

NAOS_TEARDOWN(restore)
{
    amark_allocator_set(NULL);
}

/* Creates a buffer and stops the test if that fails. */
#define CREATE(b) NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_create(&(b)))

/* Creates a buffer without stopping the test, for use while the test allocator is
 * set (test_allocator.h). On failure b is NULL, which every function accepts. */
#define CHECK_CREATE(b) NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_create(&(b)))

/* Sets the counting test allocator and stops the test if that fails (it is then
 * not set). While it is set, the tests use no NAOS_REQUIRE_*. */
#define SET_COUNTING() \
    do { \
        amark_test_allocator_init(&counting); \
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, \
                            amark_allocator_set(amark_test_allocator_activate(&counting))); \
    } while (0)

/* --- Creation and the empty buffer --- */

NAOS_TEST(create_gives_an_empty_terminated_buffer)
{
    /* 12.2: never NULL, always NUL-terminated, even before any allocation. */
    amark_buffer_t *b = NULL;

    CREATE(b);
    NAOS_REQUIRE_NOT_NULL(b);
    NAOS_CHECK_NOT_NULL(amark_buffer_data(b));
    NAOS_CHECK_UINT_EQ(0, amark_buffer_length(b));
    NAOS_CHECK_STR_EQ("", amark_buffer_data(b));
    amark_buffer_destroy(b);
}

NAOS_TEST(create_allocates_only_the_buffer_itself)
{
    /* 11.3: through the global allocator; the content is allocated lazily. */
    amark_buffer_t *b = NULL;
    size_t calls;
    size_t live;

    SET_COUNTING();
    CHECK_CREATE(b);
    calls = counting.calls;
    live = counting.live_blocks;
    amark_buffer_destroy(b);
    NAOS_CHECK_UINT_EQ(1, calls);
    NAOS_CHECK_UINT_EQ(1, live);
    NAOS_CHECK_UINT_EQ(0, counting.live_blocks);
}

NAOS_TEST(create_rejects_null_out_buffer)
{
    /* 10.5 */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_buffer_create(NULL));
}

NAOS_TEST(create_out_of_memory_sets_out_buffer_to_null)
{
    /* 10.4 convention: the output object pointer is NULL on failure. */
    amark_buffer_t *b = (amark_buffer_t *)&counting;
    amark_error_t err;

    SET_COUNTING();
    counting.fail_at = 1;
    err = amark_buffer_create(&b);
    amark_allocator_set(NULL);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_OUT_OF_MEMORY, err);
    NAOS_CHECK_NULL(b);
    NAOS_CHECK_UINT_EQ(0, counting.live_blocks);
}

NAOS_TEST(destroy_null_does_nothing)
{
    /* 10.6 */
    amark_buffer_destroy(NULL);
    NAOS_PASS();
}

NAOS_TEST(queries_on_null_are_safe)
{
    /* 10.5: no crash; data is still never NULL. */
    NAOS_CHECK_NOT_NULL(amark_buffer_data(NULL));
    NAOS_CHECK_STR_EQ("", amark_buffer_data(NULL));
    NAOS_CHECK_UINT_EQ(0, amark_buffer_length(NULL));
    amark_buffer_clear(NULL);
}

NAOS_TEST(null_buffer_is_an_invalid_argument)
{
    /* 10.5 */
    char *data = (char *)"x";
    size_t length = 7;

    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_buffer_reserve(NULL, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_buffer_append(NULL, "a", 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_buffer_append(NULL, NULL, 0));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_buffer_append_byte(NULL, 'a'));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_buffer_append_repeat(NULL, 'a', 3));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_buffer_append_cstr(NULL, "a"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_buffer_append_codepoint(NULL, 0x41));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_buffer_truncate(NULL, 0));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_buffer_detach(NULL, &data, &length));
    NAOS_CHECK_NULL(data);
    NAOS_CHECK_UINT_EQ(0, length);
}

/* --- Appending --- */

NAOS_TEST(append_bytes)
{
    /* 12.1 */
    amark_buffer_t *b = NULL;

    CREATE(b);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append(b, "Hello", 5));
    AMARK_CHECK_BUFFER_EQ(b, "Hello");
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append(b, ", world", 7));
    AMARK_CHECK_BUFFER_EQ(b, "Hello, world");
    NAOS_CHECK_STR_EQ("Hello, world", amark_buffer_data(b));
    amark_buffer_destroy(b);
}

NAOS_TEST(append_keeps_nul_bytes_and_stays_terminated)
{
    /* 12.1, 12.2: any bytes, including NUL and invalid UTF-8. */
    amark_buffer_t *b = NULL;

    CREATE(b);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append(b, "a\0b", 3));
    AMARK_CHECK_BUFFER_EQ(b, "a\0b");
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append(b, "\0", 1));
    AMARK_CHECK_BUFFER_EQ(b, "a\0b\0");
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append(b, "\xC0\x80\xFF", 3));
    AMARK_CHECK_BUFFER_EQ(b, "a\0b\0\xC0\x80\xFF");
    NAOS_CHECK_UINT_EQ(7, amark_buffer_length(b));
    amark_buffer_destroy(b);
}

NAOS_TEST(append_of_nothing_succeeds_and_allocates_nothing)
{
    /* NULL data with size 0 is allowed; size 0 never allocates. */
    amark_buffer_t *b = NULL;
    size_t calls;

    SET_COUNTING();
    CHECK_CREATE(b);
    calls = counting.calls;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append(b, NULL, 0));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append(b, "x", 0));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_repeat(b, 'x', 0));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_cstr(b, ""));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_reserve(b, 0));
    NAOS_CHECK_UINT_EQ(calls, counting.calls);
    AMARK_CHECK_BUFFER_EQ(b, "");
    amark_buffer_destroy(b);
}

NAOS_TEST(append_null_data_with_size_is_an_invalid_argument)
{
    /* 10.5; the buffer is unchanged. */
    amark_buffer_t *b = NULL;

    CREATE(b);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append(b, "keep", 4));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_buffer_append(b, NULL, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_buffer_append_cstr(b, NULL));
    AMARK_CHECK_BUFFER_EQ(b, "keep");
    amark_buffer_destroy(b);
}

NAOS_TEST(append_byte)
{
    /* 12.3 */
    amark_buffer_t *b = NULL;

    CREATE(b);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_byte(b, 'x'));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_byte(b, 0x00));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_byte(b, 0xFF));
    AMARK_CHECK_BUFFER_EQ(b, "x\0\xFF");
    amark_buffer_destroy(b);
}

NAOS_TEST(append_many_bytes_one_by_one)
{
    /* 12.1: growth over many steps keeps every byte. */
    amark_buffer_t *b = NULL;
    const unsigned char *data;
    size_t i;

    CREATE(b);
    for (i = 0; i < 10000; i++)
    {
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_byte(b, (unsigned char)i));
    }

    NAOS_CHECK_UINT_EQ(10000, amark_buffer_length(b));
    data = (const unsigned char *)amark_buffer_data(b);
    for (i = 0; i < 10000; i++)
    {
        if (data[i] != (unsigned char)i)
        {
            NAOS_CHECK_UINT_EQ((unsigned char)i, data[i]);
            break;
        }
    }

    NAOS_CHECK_UINT_EQ(0, data[10000]);
    amark_buffer_destroy(b);
}

NAOS_TEST(append_repeat)
{
    /* 12.3: the same byte count times. */
    amark_buffer_t *b = NULL;

    CREATE(b);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_repeat(b, '-', 3));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_repeat(b, 0x00, 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_repeat(b, '>', 1));
    AMARK_CHECK_BUFFER_EQ(b, "---\0\0>");
    amark_buffer_destroy(b);
}

NAOS_TEST(append_cstr)
{
    /* 12.3: the string without its NUL. */
    amark_buffer_t *b = NULL;

    CREATE(b);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_cstr(b, "abc"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_cstr(b, "de\0f"));
    AMARK_CHECK_BUFFER_EQ(b, "abcde");
    amark_buffer_destroy(b);
}

NAOS_TEST(append_codepoint_encodes_utf8)
{
    /* 12.3: 1 to 4 bytes, by the rules of amark_utf8_encode. */
    amark_buffer_t *b = NULL;

    CREATE(b);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_codepoint(b, 0x41));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_codepoint(b, 0x00));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_codepoint(b, 0xE9));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_codepoint(b, 0x3042));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_codepoint(b, 0x1F600));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_codepoint(b, 0x10FFFF));
    AMARK_CHECK_BUFFER_EQ(b, "A\0\xC3\xA9\xE3\x81\x82\xF0\x9F\x98\x80\xF4\x8F\xBF\xBF");
    amark_buffer_destroy(b);
}

NAOS_TEST(append_codepoint_replaces_invalid_values)
{
    /* Surrogates and values above U+10FFFF become U+FFFD. */
    amark_buffer_t *b = NULL;

    CREATE(b);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_codepoint(b, 0xD800));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_codepoint(b, 0xDFFF));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_codepoint(b, 0x110000));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_codepoint(b, UINT32_MAX));
    AMARK_CHECK_BUFFER_EQ(b, "\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD");
    amark_buffer_destroy(b);
}

/* --- Appending the buffer's own content --- */

NAOS_TEST(append_own_content_while_growing)
{
    /* The source points into the buffer, and the append must grow it. */
    amark_buffer_t *b = NULL;
    int i;

    CREATE(b);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append(b, "ab\0c", 4));
    for (i = 0; i < 6; i++)
    {
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                            amark_buffer_append(b, amark_buffer_data(b), amark_buffer_length(b)));
    }

    NAOS_REQUIRE_UINT_EQ(4u << 6, amark_buffer_length(b));
    for (i = 0; i < (4 << 6); i += 4)
    {
        NAOS_CHECK_MEM_EQ(amark_buffer_data(b) + i, 4, "ab\0c", 4);
    }

    NAOS_CHECK_INT_EQ(0, amark_buffer_data(b)[4 << 6]);
    amark_buffer_destroy(b);
}

NAOS_TEST(append_part_of_own_content)
{
    amark_buffer_t *b = NULL;

    CREATE(b);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append(b, "0123456789", 10));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append(b, amark_buffer_data(b) + 3, 4));
    AMARK_CHECK_BUFFER_EQ(b, "01234567893456");
    amark_buffer_destroy(b);
}

NAOS_TEST(append_own_content_without_growing)
{
    /* With enough capacity reserved, the copy happens in place. */
    amark_buffer_t *b = NULL;

    CREATE(b);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_reserve(b, 64));
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append(b, "xyz", 3));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append(b, amark_buffer_data(b), 3));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_cstr(b, amark_buffer_data(b) + 4));
    AMARK_CHECK_BUFFER_EQ(b, "xyzxyzyz");
    amark_buffer_destroy(b);
}

NAOS_TEST(append_cstr_of_own_content)
{
    amark_buffer_t *b = NULL;
    int i;

    CREATE(b);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_cstr(b, "ha"));
    for (i = 0; i < 5; i++)
    {
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_cstr(b, amark_buffer_data(b)));
    }

    NAOS_CHECK_UINT_EQ(64, amark_buffer_length(b));
    NAOS_CHECK_STR_EQ("hahahahahahahahahahahahahahahahahahahahahahahahahahahahahahahaha",
                      amark_buffer_data(b));
    amark_buffer_destroy(b);
}

/* --- Reserving --- */

NAOS_TEST(reserve_keeps_the_content_and_avoids_later_allocations)
{
    /* 12.3 */
    amark_buffer_t *b = NULL;
    size_t calls;

    SET_COUNTING();
    CHECK_CREATE(b);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append(b, "ab", 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_reserve(b, 100));
    AMARK_CHECK_BUFFER_EQ(b, "ab");
    calls = counting.calls;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_repeat(b, 'c', 100));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_reserve(b, 0));
    NAOS_CHECK_UINT_EQ(calls, counting.calls);
    NAOS_CHECK_UINT_EQ(102, amark_buffer_length(b));
    NAOS_CHECK_INT_EQ(0, amark_buffer_data(b)[102]);
    amark_buffer_destroy(b);
    NAOS_CHECK_UINT_EQ(0, counting.live_blocks);
}

/* --- Truncating and clearing --- */

NAOS_TEST(truncate_shortens_and_terminates)
{
    /* 12.3 */
    amark_buffer_t *b = NULL;

    CREATE(b);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append(b, "abc\0def", 7));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_truncate(b, 7));
    AMARK_CHECK_BUFFER_EQ(b, "abc\0def");
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_truncate(b, 5));
    AMARK_CHECK_BUFFER_EQ(b, "abc\0d");
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_truncate(b, 2));
    AMARK_CHECK_BUFFER_EQ(b, "ab");
    NAOS_CHECK_STR_EQ("ab", amark_buffer_data(b));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append(b, "Z", 1));
    AMARK_CHECK_BUFFER_EQ(b, "abZ");
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_truncate(b, 0));
    AMARK_CHECK_BUFFER_EQ(b, "");
    amark_buffer_destroy(b);
}

NAOS_TEST(truncate_of_an_empty_buffer_to_zero)
{
    amark_buffer_t *b = NULL;

    CREATE(b);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_truncate(b, 0));
    AMARK_CHECK_BUFFER_EQ(b, "");
    amark_buffer_destroy(b);
}

NAOS_TEST(truncate_to_a_longer_length_is_rejected)
{
    /* 12.6: the buffer is unchanged. */
    amark_buffer_t *b = NULL;

    CREATE(b);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_buffer_truncate(b, 1));
    AMARK_CHECK_BUFFER_EQ(b, "");
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append(b, "a\0c", 3));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_buffer_truncate(b, 4));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_buffer_truncate(b, SIZE_MAX));
    AMARK_CHECK_BUFFER_EQ(b, "a\0c");
    amark_buffer_destroy(b);
}

NAOS_TEST(truncate_and_clear_keep_the_capacity)
{
    /* Refilling up to the earlier length allocates nothing. */
    amark_buffer_t *b = NULL;
    size_t calls;

    SET_COUNTING();
    CHECK_CREATE(b);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_repeat(b, 'a', 50));
    calls = counting.calls;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_truncate(b, 10));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_repeat(b, 'b', 40));
    amark_buffer_clear(b);
    AMARK_CHECK_BUFFER_EQ(b, "");
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_repeat(b, 'c', 50));
    NAOS_CHECK_UINT_EQ(calls, counting.calls);
    NAOS_CHECK_UINT_EQ(50, amark_buffer_length(b));
    amark_buffer_destroy(b);
    NAOS_CHECK_UINT_EQ(0, counting.live_blocks);
}

NAOS_TEST(clear_empties_the_content)
{
    /* 12.3 */
    amark_buffer_t *b = NULL;

    CREATE(b);
    amark_buffer_clear(b);
    AMARK_CHECK_BUFFER_EQ(b, "");
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append(b, "x\0y", 3));
    amark_buffer_clear(b);
    AMARK_CHECK_BUFFER_EQ(b, "");
    NAOS_CHECK_STR_EQ("", amark_buffer_data(b));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append(b, "z", 1));
    AMARK_CHECK_BUFFER_EQ(b, "z");
    amark_buffer_destroy(b);
}

/* --- Detaching --- */

NAOS_TEST(detach_returns_the_content_freed_with_amark_free)
{
    /* 12.8: NUL-terminated content and length; the buffer is empty again. The
     * counting allocator shows that amark_free releases the detached block. */
    amark_buffer_t *b = NULL;
    char *data = NULL;
    size_t length = 0;

    SET_COUNTING();
    CHECK_CREATE(b);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append(b, "ab\0cd", 5));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_detach(b, &data, &length));
    NAOS_CHECK_NOT_NULL(data);
    NAOS_CHECK_UINT_EQ(5, length);
    if (data != NULL)
    {
        AMARK_CHECK_BYTES_EQ(data, length, "ab\0cd");
        NAOS_CHECK_INT_EQ(0, data[length]);
    }

    AMARK_CHECK_BUFFER_EQ(b, "");
    NAOS_CHECK_UINT_EQ(2, counting.live_blocks);

    /* The buffer can be used again, independently of the detached content. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append(b, "new", 3));
    AMARK_CHECK_BUFFER_EQ(b, "new");
    if (data != NULL)
    {
        AMARK_CHECK_BYTES_EQ(data, length, "ab\0cd");
    }

    amark_buffer_destroy(b);

    amark_free(data);
    NAOS_CHECK_UINT_EQ(0, counting.live_blocks);
}

NAOS_TEST(detach_of_an_empty_buffer_allocates_an_empty_string)
{
    /* 12.8: the caller always owns a block, even for an empty content. */
    amark_buffer_t *b = NULL;
    char *data = NULL;
    size_t length = 99;

    CREATE(b);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_detach(b, &data, &length));
    NAOS_REQUIRE_NOT_NULL(data);
    NAOS_CHECK_UINT_EQ(0, length);
    NAOS_CHECK_STR_EQ("", data);
    AMARK_CHECK_BUFFER_EQ(b, "");
    amark_free(data);
    amark_buffer_destroy(b);
}

NAOS_TEST(detach_after_clear_keeps_the_owned_block)
{
    /* A cleared buffer still has capacity: its block is handed over as "". */
    amark_buffer_t *b = NULL;
    char *data = NULL;
    size_t length = 99;

    CREATE(b);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append(b, "abc", 3));
    amark_buffer_clear(b);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_detach(b, &data, &length));
    NAOS_REQUIRE_NOT_NULL(data);
    NAOS_CHECK_UINT_EQ(0, length);
    NAOS_CHECK_STR_EQ("", data);
    amark_free(data);
    AMARK_CHECK_BUFFER_EQ(b, "");
    amark_buffer_destroy(b);
}

NAOS_TEST(detach_rejects_null_output_arguments)
{
    /* 10.5: the buffer is unchanged and the other output argument is reset. */
    amark_buffer_t *b = NULL;
    char *data = (char *)"x";
    size_t length = 7;

    CREATE(b);
    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append(b, "keep", 4));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_buffer_detach(b, NULL, &length));
    NAOS_CHECK_UINT_EQ(0, length);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_buffer_detach(b, &data, NULL));
    NAOS_CHECK_NULL(data);
    AMARK_CHECK_BUFFER_EQ(b, "keep");
    amark_buffer_destroy(b);
}

NAOS_TEST(detach_out_of_memory_for_an_empty_buffer)
{
    /* The only allocation of detach: the "" of an empty buffer with no capacity. */
    amark_buffer_t *b = NULL;
    char *data = (char *)"x";
    size_t length = 7;
    amark_error_t err;

    SET_COUNTING();
    CHECK_CREATE(b);
    counting.fail_at = counting.calls + 1;
    err = amark_buffer_detach(b, &data, &length);
    amark_buffer_destroy(b);
    amark_allocator_set(NULL);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_OUT_OF_MEMORY, err);
    NAOS_CHECK_NULL(data);
    NAOS_CHECK_UINT_EQ(0, length);
    NAOS_CHECK_UINT_EQ(0, counting.live_blocks);
}

NAOS_TEST(every_operation_uses_the_global_allocator)
{
    /* 11.3: the content and the buffer are freed through the set allocator. */
    amark_buffer_t *b = NULL;
    char *data = NULL;
    size_t length = 0;

    SET_COUNTING();
    CHECK_CREATE(b);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_cstr(b, "grow"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_reserve(b, 1000));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_codepoint(b, 0x3042));
    NAOS_CHECK_UINT_GE(counting.calls, 3);
    NAOS_CHECK_UINT_EQ(2, counting.live_blocks);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_detach(b, &data, &length));
    amark_free(data);
    NAOS_CHECK_UINT_EQ(1, counting.live_blocks);
    amark_buffer_destroy(b);
    NAOS_CHECK_UINT_EQ(0, counting.live_blocks);
}

NAOS_SUITE(buffer,
    NAOS_SET_TEARDOWN(restore),
    NAOS_ADD_TEST(create_gives_an_empty_terminated_buffer),
    NAOS_ADD_TEST(create_allocates_only_the_buffer_itself),
    NAOS_ADD_TEST(create_rejects_null_out_buffer),
    NAOS_ADD_TEST(create_out_of_memory_sets_out_buffer_to_null),
    NAOS_ADD_TEST(destroy_null_does_nothing),
    NAOS_ADD_TEST(queries_on_null_are_safe),
    NAOS_ADD_TEST(null_buffer_is_an_invalid_argument),
    NAOS_ADD_TEST(append_bytes),
    NAOS_ADD_TEST(append_keeps_nul_bytes_and_stays_terminated),
    NAOS_ADD_TEST(append_of_nothing_succeeds_and_allocates_nothing),
    NAOS_ADD_TEST(append_null_data_with_size_is_an_invalid_argument),
    NAOS_ADD_TEST(append_byte),
    NAOS_ADD_TEST(append_many_bytes_one_by_one),
    NAOS_ADD_TEST(append_repeat),
    NAOS_ADD_TEST(append_cstr),
    NAOS_ADD_TEST(append_codepoint_encodes_utf8),
    NAOS_ADD_TEST(append_codepoint_replaces_invalid_values),
    NAOS_ADD_TEST(append_own_content_while_growing),
    NAOS_ADD_TEST(append_part_of_own_content),
    NAOS_ADD_TEST(append_own_content_without_growing),
    NAOS_ADD_TEST(append_cstr_of_own_content),
    NAOS_ADD_TEST(reserve_keeps_the_content_and_avoids_later_allocations),
    NAOS_ADD_TEST(truncate_shortens_and_terminates),
    NAOS_ADD_TEST(truncate_of_an_empty_buffer_to_zero),
    NAOS_ADD_TEST(truncate_to_a_longer_length_is_rejected),
    NAOS_ADD_TEST(truncate_and_clear_keep_the_capacity),
    NAOS_ADD_TEST(clear_empties_the_content),
    NAOS_ADD_TEST(detach_returns_the_content_freed_with_amark_free),
    NAOS_ADD_TEST(detach_of_an_empty_buffer_allocates_an_empty_string),
    NAOS_ADD_TEST(detach_after_clear_keeps_the_owned_block),
    NAOS_ADD_TEST(detach_rejects_null_output_arguments),
    NAOS_ADD_TEST(detach_out_of_memory_for_an_empty_buffer),
    NAOS_ADD_TEST(every_operation_uses_the_global_allocator)
)

/* === The failure guarantees and the growth properties === */

/* --- Growth: a logarithmic number of allocations (12.7) --- */

/* Returns the floor of log2(n), for n > 0. */
static size_t log2_floor(size_t n)
{
    size_t log = 0;

    while (n > 1)
    {
        n >>= 1;
        log++;
    }

    return log;
}

/*
 * The most allocation calls allowed for a content that grew to total bytes: the
 * doubling rule needs floor(log2(total)) + 1 of them (the first allocation, then
 * one per doubling); one more is allowed for a first piece that does not start
 * at a power of two.
 */
static size_t growth_bound(size_t total)
{
    return log2_floor(total) + 2;
}

/* The total of the small-piece tests: big enough that a linear growth is far
 * above the bound. */
#define GROWTH_TOTAL ((size_t)1 << 16)

NAOS_TEST(appending_byte_by_byte_allocates_logarithmically)
{
    /* 12.7: n bytes one at a time, checked against the bound at every length. */
    amark_buffer_t *b = NULL;
    size_t i;
    size_t calls = 0;
    size_t worst_at = 0;

    SET_COUNTING();
    CHECK_CREATE(b);
    amark_test_allocator_reset_counters(&counting);
    for (i = 1; i <= GROWTH_TOTAL; i++)
    {
        if (amark_buffer_append_byte(b, (unsigned char)i) != AMARK_ERROR_NONE)
        {
            break;
        }

        if ((worst_at == 0) && (counting.calls > growth_bound(i)))
        {
            worst_at = i;
            calls = counting.calls;
        }
    }

    NAOS_CHECK_UINT_EQ(GROWTH_TOTAL, amark_buffer_length(b));
    NAOS_NOTE("first length over the bound: %lu (%lu calls)", (unsigned long)worst_at,
              (unsigned long)calls);
    NAOS_CHECK_UINT_EQ(0, worst_at);
    NAOS_CHECK_UINT_LE(counting.calls, growth_bound(GROWTH_TOTAL));
    amark_buffer_destroy(b);
    NAOS_CHECK_UINT_EQ(0, counting.live_blocks);
}

NAOS_TEST(appending_mixed_small_pieces_allocates_logarithmically)
{
    /* 12.7: every kind of append, in pieces of 1 to 7 bytes. */
    amark_buffer_t *b = NULL;
    size_t total = 0;
    size_t step = 0;
    size_t worst_at = 0;
    size_t calls = 0;
    amark_error_t err = AMARK_ERROR_NONE;

    SET_COUNTING();
    CHECK_CREATE(b);
    amark_test_allocator_reset_counters(&counting);
    while ((total < GROWTH_TOTAL) && (err == AMARK_ERROR_NONE))
    {
        switch (step % 5)
        {
        case 0:
            err = amark_buffer_append_byte(b, 'x');
            total += 1;
            break;
        case 1:
            err = amark_buffer_append(b, "a\0b", 3);
            total += 3;
            break;
        case 2:
            err = amark_buffer_append_repeat(b, '-', 5);
            total += 5;
            break;
        case 3:
            err = amark_buffer_append_cstr(b, "seven77");
            total += 7;
            break;
        default:
            err = amark_buffer_append_codepoint(b, 0x1F600);
            total += 4;
            break;
        }

        step++;
        if ((worst_at == 0) && (counting.calls > growth_bound(total)))
        {
            worst_at = total;
            calls = counting.calls;
        }
    }

    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, err);
    NAOS_CHECK_UINT_EQ(total, amark_buffer_length(b));
    NAOS_NOTE("first length over the bound: %lu (%lu calls)", (unsigned long)worst_at,
              (unsigned long)calls);
    NAOS_CHECK_UINT_EQ(0, worst_at);
    amark_buffer_destroy(b);
    NAOS_CHECK_UINT_EQ(0, counting.live_blocks);
}

NAOS_TEST(reserving_a_little_more_each_time_allocates_logarithmically)
{
    /* 12.7: reserve follows the same growth rule as the appends. */
    amark_buffer_t *b = NULL;
    size_t i;
    size_t worst_at = 0;

    SET_COUNTING();
    CHECK_CREATE(b);
    amark_test_allocator_reset_counters(&counting);
    for (i = 1; i <= GROWTH_TOTAL; i++)
    {
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_reserve(b, i));
        if ((worst_at == 0) && (counting.calls > growth_bound(i)))
        {
            worst_at = i;
        }
    }

    NAOS_CHECK_UINT_EQ(0, worst_at);
    AMARK_CHECK_BUFFER_EQ(b, "");

    /* reserve(1) before each byte: the reservations do not add allocations. */
    amark_buffer_clear(b);
    amark_test_allocator_reset_counters(&counting);
    for (i = 0; i < (2 * GROWTH_TOTAL); i++)
    {
        if ((amark_buffer_reserve(b, 1) != AMARK_ERROR_NONE) ||
            (amark_buffer_append_byte(b, 'r') != AMARK_ERROR_NONE))
        {
            break;
        }
    }

    NAOS_CHECK_UINT_EQ(2 * GROWTH_TOTAL, amark_buffer_length(b));
    /* The capacity of the first loop holds GROWTH_TOTAL bytes: one doubling is
     * enough for twice as many. */
    NAOS_CHECK_UINT_LE(counting.calls, 2);
    amark_buffer_destroy(b);
    NAOS_CHECK_UINT_EQ(0, counting.live_blocks);
}

/* --- Out of memory: every operation that can grow (12.4, 11.4, 10.4) --- */

/* The operations that can grow a buffer. */
typedef enum grow_op
{
    GROW_RESERVE,
    GROW_APPEND,
    GROW_APPEND_OWN, /* append the buffer's own content */
    GROW_APPEND_BYTE,
    GROW_APPEND_REPEAT,
    GROW_APPEND_CSTR,
    GROW_APPEND_CODEPOINT
} grow_op_t;

/* The bytes appended by GROW_APPEND, with a NUL byte inside. */
static const char grow_bytes[] = "0123456789\0abcdefghijklmnopqrstuvwxyz";

/* Runs op on b, so that it needs more than a capacity of 8 bytes. */
static amark_error_t run_grow_op(amark_buffer_t *b, grow_op_t op)
{
    switch (op)
    {
    case GROW_RESERVE:
        return amark_buffer_reserve(b, 100);
    case GROW_APPEND:
        return amark_buffer_append(b, grow_bytes, sizeof(grow_bytes) - 1);
    case GROW_APPEND_OWN:
        return amark_buffer_append(b, amark_buffer_data(b), amark_buffer_length(b));
    case GROW_APPEND_BYTE:
        return amark_buffer_append_byte(b, 0xFF);
    case GROW_APPEND_REPEAT:
        return amark_buffer_append_repeat(b, '=', 20);
    case GROW_APPEND_CSTR:
        return amark_buffer_append_cstr(b, "a string longer than the capacity");
    default:
        return amark_buffer_append_codepoint(b, 0x1F600);
    }
}

/* The context of the sweep of one operation. */
typedef struct grow_case
{
    grow_op_t op;
    const char *name;
    const char *prefix; /* the content before op; filled exactly to its capacity */
    size_t prefix_length;
    size_t op_failures; /* the runs in which op itself returned OOM */
    size_t op_successes;
} grow_case_t;

/* The largest content the scenarios snapshot. */
#define SNAPSHOT_SIZE 256

/*
 * Creates a buffer holding the prefix, runs the operation and, when it fails,
 * checks that the content, the length and the data pointer are those from before
 * the call. Uses no NAOS_REQUIRE_* (the test allocator is set) and frees all it
 * created.
 */
static amark_error_t grow_scenario(void *context)
{
    grow_case_t *c = context;
    amark_buffer_t *b = NULL;
    char before[SNAPSHOT_SIZE];
    const char *data_before;
    size_t length_before;
    amark_error_t err;

    err = amark_buffer_create(&b);
    if (err != AMARK_ERROR_NONE)
    {
        return err;
    }

    err = amark_buffer_append(b, c->prefix, c->prefix_length);
    if (err != AMARK_ERROR_NONE)
    {
        amark_buffer_destroy(b);
        return err;
    }

    length_before = amark_buffer_length(b);
    data_before = amark_buffer_data(b);
    memcpy(before, data_before, length_before + 1);

    err = run_grow_op(b, c->op);
    if (err == AMARK_ERROR_OUT_OF_MEMORY)
    {
        c->op_failures++;
        NAOS_CHECK_UINT_EQ(length_before, amark_buffer_length(b));
        NAOS_CHECK_PTR_EQ(data_before, amark_buffer_data(b));
        NAOS_CHECK_MEM_EQ(amark_buffer_data(b), amark_buffer_length(b) + 1, before,
                          length_before + 1);
    }
    else if (err == AMARK_ERROR_NONE)
    {
        c->op_successes++;
        NAOS_CHECK_MEM_EQ(amark_buffer_data(b), length_before, before, length_before);
        NAOS_CHECK_INT_EQ(0, amark_buffer_data(b)[amark_buffer_length(b)]);
    }

    amark_buffer_destroy(b);
    return err;
}

/* Sweeps every operation from one prefix; each must fail exactly once (its only
 * allocation) and succeed once. */
static void sweep_every_operation(const char *prefix, size_t prefix_length)
{
    static const struct
    {
        grow_op_t op;
        const char *name;
    } ops[] = {
        {GROW_RESERVE, "reserve"},
        {GROW_APPEND, "append"},
        {GROW_APPEND_OWN, "append of its own content"},
        {GROW_APPEND_BYTE, "append_byte"},
        {GROW_APPEND_REPEAT, "append_repeat"},
        {GROW_APPEND_CSTR, "append_cstr"},
        {GROW_APPEND_CODEPOINT, "append_codepoint"},
    };
    size_t i;

    for (i = 0; i < (sizeof(ops) / sizeof(ops[0])); i++)
    {
        grow_case_t c;

        if ((ops[i].op == GROW_APPEND_OWN) && (prefix_length == 0))
        {
            continue; /* appends nothing, so cannot fail */
        }

        c.op = ops[i].op;
        c.name = ops[i].name;
        c.prefix = prefix;
        c.prefix_length = prefix_length;
        c.op_failures = 0;
        c.op_successes = 0;
        NAOS_TRACE_PUSH("operation %s", c.name);
        AMARK_CHECK_OOM_SWEEP(grow_scenario, &c);
        NAOS_CHECK_UINT_EQ(1, c.op_failures);
        NAOS_CHECK_UINT_EQ(1, c.op_successes);
        NAOS_TRACE_POP();
    }
}

NAOS_TEST(out_of_memory_when_growing_from_empty_changes_nothing)
{
    /* 12.4, 11.4: the first allocation of the content fails; the buffer stays "". */
    sweep_every_operation("", 0);
}

NAOS_TEST(out_of_memory_when_reallocating_changes_nothing)
{
    /* 12.4, 11.4: the reallocation fails; the content (with a NUL byte) is kept.
     * The 7 bytes fill the capacity of 8 exactly, so that every operation must
     * reallocate. */
    sweep_every_operation("ab\0cdef", 7);
}

/* A longer life: every operation in turn on one buffer, each at a different
 * capacity, checking after each failure that nothing changed. */
static amark_error_t sequence_scenario(void *context)
{
    size_t *failures = context;
    amark_buffer_t *b = NULL;
    char before[SNAPSHOT_SIZE];
    size_t length_before;
    grow_op_t op;
    amark_error_t err;

    err = amark_buffer_create(&b);
    if (err != AMARK_ERROR_NONE)
    {
        return err;
    }

    for (op = GROW_RESERVE; (op <= GROW_APPEND_CODEPOINT) && (err == AMARK_ERROR_NONE);
         op = (grow_op_t)(op + 1))
    {
        int round;

        for (round = 0; (round < 2) && (err == AMARK_ERROR_NONE); round++)
        {
            length_before = amark_buffer_length(b);
            if (length_before >= (SNAPSHOT_SIZE / 2))
            {
                (void)amark_buffer_truncate(b, SNAPSHOT_SIZE / 4);
                length_before = amark_buffer_length(b);
            }

            memcpy(before, amark_buffer_data(b), length_before + 1);
            err = run_grow_op(b, op);
            if (err == AMARK_ERROR_OUT_OF_MEMORY)
            {
                (*failures)++;
                NAOS_NOTE("operation %d, round %d", (int)op, round);
                NAOS_CHECK_MEM_EQ(amark_buffer_data(b), amark_buffer_length(b) + 1, before,
                                  length_before + 1);
            }
        }
    }

    amark_buffer_destroy(b);
    return err;
}

NAOS_TEST(out_of_memory_in_a_sequence_of_operations_changes_nothing)
{
    /* 12.4, 11.4 */
    size_t failures = 0;

    AMARK_CHECK_OOM_SWEEP(sequence_scenario, &failures);
    /* At least the first allocation of the content and a reallocation. */
    NAOS_CHECK_UINT_GE(failures, 2);
}

/* Creates and destroys a buffer; on failure, the output must be NULL. */
static amark_error_t create_scenario(void *context)
{
    amark_buffer_t *b = context; /* any non-NULL value, to see it set to NULL */
    amark_error_t err = amark_buffer_create(&b);

    if (err == AMARK_ERROR_OUT_OF_MEMORY)
    {
        NAOS_CHECK_NULL(b);
    }
    else
    {
        NAOS_CHECK_NOT_NULL(b);
        NAOS_CHECK_UINT_EQ(0, amark_buffer_length(b));
    }

    amark_buffer_destroy(b);
    return err;
}

NAOS_TEST(out_of_memory_in_create)
{
    /* 10.4, 11.4 */
    AMARK_CHECK_OOM_SWEEP(create_scenario, &counting);
}

/* Detaches an empty buffer with no capacity, then one with content. */
static amark_error_t detach_scenario(void *context)
{
    size_t *failures = context;
    amark_buffer_t *b = NULL;
    char *data = (char *)"x";
    size_t length = 7;
    amark_error_t err;

    err = amark_buffer_create(&b);
    if (err != AMARK_ERROR_NONE)
    {
        return err;
    }

    err = amark_buffer_detach(b, &data, &length);
    if (err == AMARK_ERROR_OUT_OF_MEMORY)
    {
        (*failures)++;
        NAOS_CHECK_NULL(data);
        NAOS_CHECK_UINT_EQ(0, length);
        AMARK_CHECK_BUFFER_EQ(b, "");
        amark_buffer_destroy(b);
        return err;
    }

    amark_free(data);
    if (err == AMARK_ERROR_NONE)
    {
        /* A buffer with content hands its block over: no allocation to fail. */
        err = amark_buffer_append(b, "kept", 4);
        if (err == AMARK_ERROR_NONE)
        {
            err = amark_buffer_detach(b, &data, &length);
            NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, err);
            if (err == AMARK_ERROR_NONE)
            {
                AMARK_CHECK_BYTES_EQ(data, length, "kept");
                amark_free(data);
            }
        }
    }

    amark_buffer_destroy(b);
    return err;
}

NAOS_TEST(out_of_memory_in_detach_of_an_empty_buffer)
{
    /* 12.4, 11.4, 10.4: the outputs are NULL and 0, and the buffer is unchanged. */
    size_t failures = 0;

    AMARK_CHECK_OOM_SWEEP(detach_scenario, &failures);
    NAOS_CHECK_UINT_EQ(1, failures);
}

/* --- Overflow of the required capacity (12.5) --- */

/* Checks that the content of b is the expected bytes, at the same address. */
#define CHECK_UNCHANGED(b, data_before, expected_literal) \
    do { \
        AMARK_CHECK_BUFFER_EQ((b), expected_literal); \
        NAOS_CHECK_PTR_EQ((data_before), amark_buffer_data(b)); \
    } while (0)

NAOS_TEST(overflowing_capacity_of_an_empty_buffer_calls_no_allocator)
{
    /* 12.5: length + SIZE_MAX + 1 does not fit in size_t. */
    static const char source[1] = {'s'};
    amark_buffer_t *b = NULL;
    const char *data_before;
    size_t calls;

    SET_COUNTING();
    CHECK_CREATE(b);
    data_before = amark_buffer_data(b);
    calls = counting.calls;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_OVERFLOW, amark_buffer_reserve(b, SIZE_MAX));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_OVERFLOW, amark_buffer_append_repeat(b, 'x', SIZE_MAX));
    /* The size is checked before any byte of source is read. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_OVERFLOW, amark_buffer_append(b, source, SIZE_MAX));
    NAOS_CHECK_UINT_EQ(calls, counting.calls);
    CHECK_UNCHANGED(b, data_before, "");
    amark_buffer_destroy(b);
    NAOS_CHECK_UINT_EQ(0, counting.live_blocks);
}

NAOS_TEST(overflowing_capacity_of_a_buffer_with_content_calls_no_allocator)
{
    /* 12.5: with 3 bytes, any addition from SIZE_MAX - 3 overflows. */
    static const char source[1] = {'s'};
    amark_buffer_t *b = NULL;
    const char *data_before;
    size_t calls;

    SET_COUNTING();
    CHECK_CREATE(b);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append(b, "a\0c", 3));
    data_before = amark_buffer_data(b);
    calls = counting.calls;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_OVERFLOW, amark_buffer_reserve(b, SIZE_MAX));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_OVERFLOW, amark_buffer_reserve(b, SIZE_MAX - 2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_OVERFLOW, amark_buffer_reserve(b, SIZE_MAX - 3));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_OVERFLOW, amark_buffer_append_repeat(b, 'x', SIZE_MAX - 3));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_OVERFLOW, amark_buffer_append(b, source, SIZE_MAX - 3));
    NAOS_CHECK_UINT_EQ(calls, counting.calls);
    CHECK_UNCHANGED(b, data_before, "a\0c");

    /* The buffer is still usable. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_byte(b, 'd'));
    AMARK_CHECK_BUFFER_EQ(b, "a\0cd");
    amark_buffer_destroy(b);
    NAOS_CHECK_UINT_EQ(0, counting.live_blocks);
}

NAOS_TEST(huge_reserve_that_fits_reports_the_allocation_failure)
{
    /* 12.4, 12.5: a required capacity of exactly SIZE_MAX does not overflow, so the
     * allocator is called; the injected failure gives OOM, and the buffer keeps
     * its content and its capacity. */
    amark_buffer_t *b = NULL;
    const char *data_before;
    size_t calls;

    SET_COUNTING();
    CHECK_CREATE(b);

    /* No capacity yet: the first allocation fails. */
    data_before = amark_buffer_data(b);
    counting.fail_at = counting.calls + 1;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_OUT_OF_MEMORY, amark_buffer_reserve(b, SIZE_MAX - 1));
    NAOS_CHECK_UINT_EQ(1, counting.failures);
    NAOS_CHECK_UINT_EQ(counting.fail_at, counting.calls);
    CHECK_UNCHANGED(b, data_before, "");

    /* A capacity of 4: the reallocation fails. */
    counting.fail_at = 0;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append(b, "a\0c", 3));
    data_before = amark_buffer_data(b);
    counting.fail_at = counting.calls + 1;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_OUT_OF_MEMORY, amark_buffer_reserve(b, SIZE_MAX - 4));
    counting.fail_at = counting.calls + 1;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_OUT_OF_MEMORY,
                      amark_buffer_append_repeat(b, 'x', SIZE_MAX - 4));
    NAOS_CHECK_UINT_EQ(3, counting.failures);
    CHECK_UNCHANGED(b, data_before, "a\0c");

    /* The capacity is still 4 (3 bytes and the NUL): one more byte reallocates,
     * which it would not if a failed call had kept a larger capacity. */
    counting.fail_at = 0;
    calls = counting.calls;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_byte(b, 'd'));
    NAOS_CHECK_UINT_EQ(calls + 1, counting.calls);
    AMARK_CHECK_BUFFER_EQ(b, "a\0cd");
    amark_buffer_destroy(b);
    NAOS_CHECK_UINT_EQ(0, counting.live_blocks);
}

/* --- Two buffers used in turn (10.8) --- */

/* Returns whether the content blocks of a and b (with their NUL) overlap. */
static int contents_overlap(const amark_buffer_t *a, const amark_buffer_t *b)
{
    uintptr_t a_start = (uintptr_t)amark_buffer_data(a);
    uintptr_t b_start = (uintptr_t)amark_buffer_data(b);
    uintptr_t a_end = a_start + amark_buffer_length(a) + 1;
    uintptr_t b_end = b_start + amark_buffer_length(b) + 1;

    return (a_start < b_end) && (b_start < a_end);
}

NAOS_TEST(two_buffers_in_turn_do_not_interfere)
{
    /* 10.8: each keeps its own content and its own blocks. */
    amark_buffer_t *a = NULL;
    amark_buffer_t *b = NULL;
    const unsigned char *data;
    char *detached = NULL;
    size_t detached_length = 0;
    size_t i;
    size_t a_bad = 0;
    size_t b_bad = 0;

    SET_COUNTING();
    CHECK_CREATE(a);
    CHECK_CREATE(b);
    for (i = 0; i < 3000; i++)
    {
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_byte(a, (unsigned char)i));
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                          amark_buffer_append_repeat(b, (unsigned char)(255 - i % 256), 2));
    }

    NAOS_CHECK_FALSE(contents_overlap(a, b));
    NAOS_CHECK_UINT_EQ(3000, amark_buffer_length(a));
    NAOS_CHECK_UINT_EQ(6000, amark_buffer_length(b));
    data = (const unsigned char *)amark_buffer_data(a);
    for (i = 0; (i < 3000) && (i < amark_buffer_length(a)); i++)
    {
        if (data[i] != (unsigned char)i)
        {
            a_bad++;
        }
    }

    data = (const unsigned char *)amark_buffer_data(b);
    for (i = 0; (i < 6000) && (i < amark_buffer_length(b)); i++)
    {
        if (data[i] != (unsigned char)(255 - (i / 2) % 256))
        {
            b_bad++;
        }
    }

    NAOS_CHECK_UINT_EQ(0, a_bad);
    NAOS_CHECK_UINT_EQ(0, b_bad);
    NAOS_CHECK_UINT_EQ(4, counting.live_blocks);

    /* A failure in a leaves b as it was, and b still grows. */
    counting.fail_at = counting.calls + 1;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_OUT_OF_MEMORY, amark_buffer_reserve(a, 100000));
    counting.fail_at = 0;
    NAOS_CHECK_UINT_EQ(3000, amark_buffer_length(a));
    NAOS_CHECK_UINT_EQ(6000, amark_buffer_length(b));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_cstr(b, "end"));
    NAOS_CHECK_MEM_EQ(amark_buffer_data(b) + 6000, 4, "end", 4);

    /* Clearing, detaching and destroying a do not touch b. */
    amark_buffer_clear(a);
    NAOS_CHECK_UINT_EQ(6003, amark_buffer_length(b));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append(a, "own", 3));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_detach(a, &detached, &detached_length));
    amark_buffer_destroy(a);
    NAOS_CHECK_UINT_EQ(3, counting.live_blocks); /* b, its content, the detached one */
    NAOS_CHECK_UINT_EQ(6003, amark_buffer_length(b));
    NAOS_CHECK_UINT_EQ(0xFF, (unsigned char)amark_buffer_data(b)[0]);
    NAOS_CHECK_MEM_EQ(amark_buffer_data(b) + 6000, 4, "end", 4);
    if (detached != NULL)
    {
        AMARK_CHECK_BYTES_EQ(detached, detached_length, "own");
    }

    amark_free(detached);
    amark_buffer_destroy(b);
    NAOS_CHECK_UINT_EQ(0, counting.live_blocks);
}

NAOS_SUITE(buffer_guarantees,
    NAOS_SET_TEARDOWN(restore),
    NAOS_ADD_TEST(appending_byte_by_byte_allocates_logarithmically),
    NAOS_ADD_TEST(appending_mixed_small_pieces_allocates_logarithmically),
    NAOS_ADD_TEST(reserving_a_little_more_each_time_allocates_logarithmically),
    NAOS_ADD_TEST(out_of_memory_when_growing_from_empty_changes_nothing),
    NAOS_ADD_TEST(out_of_memory_when_reallocating_changes_nothing),
    NAOS_ADD_TEST(out_of_memory_in_a_sequence_of_operations_changes_nothing),
    NAOS_ADD_TEST(out_of_memory_in_create),
    NAOS_ADD_TEST(out_of_memory_in_detach_of_an_empty_buffer),
    NAOS_ADD_TEST(overflowing_capacity_of_an_empty_buffer_calls_no_allocator),
    NAOS_ADD_TEST(overflowing_capacity_of_a_buffer_with_content_calls_no_allocator),
    NAOS_ADD_TEST(huge_reserve_that_fits_reports_the_allocation_failure),
    NAOS_ADD_TEST(two_buffers_in_turn_do_not_interfere)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(buffer),
    NAOS_ADD_SUITE(buffer_guarantees)
)
