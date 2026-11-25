/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The public API of the growable byte buffer (10.5, 10.6, 11.3, 12.1, 12.2, 12.3,
 * 12.6, 12.8): the result of each operation, content with NUL bytes, appending
 * the buffer's own content, the rejected truncation, and detached content freed
 * with amark_free.
 *
 * The failure guarantees under allocation failure and overflow, and the growth
 * properties, are tested separately.
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
    for (i = 0; i < 10000; i++) {
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append_byte(b, (unsigned char)i));
    }
    NAOS_CHECK_UINT_EQ(10000, amark_buffer_length(b));
    data = (const unsigned char *)amark_buffer_data(b);
    for (i = 0; i < 10000; i++) {
        if (data[i] != (unsigned char)i) {
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
    for (i = 0; i < 6; i++) {
        NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                            amark_buffer_append(b, amark_buffer_data(b), amark_buffer_length(b)));
    }
    NAOS_REQUIRE_UINT_EQ(4u << 6, amark_buffer_length(b));
    for (i = 0; i < (4 << 6); i += 4) {
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
    for (i = 0; i < 5; i++) {
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
    if (data != NULL) {
        AMARK_CHECK_BYTES_EQ(data, length, "ab\0cd");
        NAOS_CHECK_INT_EQ(0, data[length]);
    }
    AMARK_CHECK_BUFFER_EQ(b, "");
    NAOS_CHECK_UINT_EQ(2, counting.live_blocks);

    /* The buffer can be used again, independently of the detached content. */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_buffer_append(b, "new", 3));
    AMARK_CHECK_BUFFER_EQ(b, "new");
    if (data != NULL) {
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

NAOS_MAIN(
    NAOS_ADD_SUITE(buffer)
)
