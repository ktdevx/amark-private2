/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The fuzz target of the byte buffer (FuzzTargets).
 *
 * The input is read as a sequence of operations on two buffers: one created with
 * amark_buffer_create, and one embedded by value and set up with
 * amark_buffer_init (buffer_internal.h). Every operation starts with a byte:
 *   bit 7     inject allocation failures: every call of the allocator during the
 *             operation fails;
 *   bit 6     the buffer to operate on (0: created, 1: embedded);
 *   bits 0-5  the operation (modulo OP_COUNT), followed by its arguments, read
 *             from the input (missing bytes read as 0).
 *
 * A model of each buffer (its content and the capacity predicted by the growth
 * rule of amark/buffer.h) predicts the exact result of every operation: the error
 * code, whether the allocator is called, and the content, length and capacity
 * after it. After every operation the target checks that the buffer matches its
 * model, that the content is followed by a NUL byte, and the invariants of
 * buffer_internal.h. So a failed operation (out of memory, overflow, invalid
 * argument) must leave the buffer unchanged, and an overflow must not call the
 * allocator.
 *
 * The allocator that injects the failures lives in this file (it must not come
 * from amark_test_support, which links Naos). It is set as the global allocator
 * with amark_allocator_set for each input and the default allocator is restored
 * at the end, after checking that every block was freed.
 *
 * A violated property aborts, which libFuzzer and the corpus replay report.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <amark/allocator.h>
#include <amark/buffer.h>
#include <amark/error.h>
#include <amark/utf8.h>

#include "base/buffer_internal.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static void check_failed(const char *expression, int line)
{
    fprintf(stderr, "buffer_fuzz.c:%d: property violated: %s\n", line, expression);
    abort();
}

#define CHECK(expression)                         \
    do {                                          \
        if (!(expression)) {                      \
            check_failed(#expression, __LINE__);  \
        }                                         \
    } while (0)

/* The content is kept below this size, so that the checks after every operation
 * stay fast. An operation that would succeed beyond it is skipped; one that
 * fails (overflow, injected failure) is still run. */
#define MAX_CONTENT ((size_t)1 << 16)

/* ---- The allocator that injects failures ---- */

static struct
{
    bool fail;      /* every call fails while set */
    size_t calls;   /* calls of malloc_fn, calloc_fn and realloc_fn */
    size_t live;    /* blocks allocated and not yet freed */
} fuzz_allocator;

static void *fuzz_malloc(size_t size)
{
    void *block;

    fuzz_allocator.calls++;
    if (fuzz_allocator.fail)
    {
        return NULL;
    }
    block = malloc(size);
    if (block != NULL)
    {
        fuzz_allocator.live++;
    }
    return block;
}

static void *fuzz_calloc(size_t count, size_t size)
{
    void *block;

    fuzz_allocator.calls++;
    if (fuzz_allocator.fail)
    {
        return NULL;
    }
    block = calloc(count, size);
    if (block != NULL)
    {
        fuzz_allocator.live++;
    }
    return block;
}

static void *fuzz_realloc(void *ptr, size_t new_size)
{
    fuzz_allocator.calls++;
    if (fuzz_allocator.fail)
    {
        return NULL;
    }
    return realloc(ptr, new_size);
}

static void fuzz_free(void *ptr)
{
    CHECK(ptr != NULL);
    CHECK(fuzz_allocator.live > 0);
    fuzz_allocator.live--;
    free(ptr);
}

/* ---- The input ---- */

typedef struct
{
    const uint8_t *data;
    size_t size;
    size_t position;
} reader_t;

static uint8_t read_byte(reader_t *reader)
{
    if (reader->position >= reader->size)
    {
        return 0;
    }
    return reader->data[reader->position++];
}

/* Reads a 16-bit little-endian value. */
static size_t read_u16(reader_t *reader)
{
    size_t low = read_byte(reader);
    return low | ((size_t)read_byte(reader) << 8);
}

static uint32_t read_u32(reader_t *reader)
{
    uint32_t value = 0;
    int i;

    for (i = 0; i < 4; i++)
    {
        value |= (uint32_t)read_byte(reader) << (8 * i);
    }
    return value;
}

/* ---- The model ---- */

typedef struct
{
    char *bytes;     /* the content and a NUL byte (malloc of the C library) */
    size_t length;
    size_t capacity; /* the capacity the buffer must have */
} model_t;

static void model_init(model_t *model)
{
    model->bytes = (char *)malloc(MAX_CONTENT + 1);
    CHECK(model->bytes != NULL);
    model->bytes[0] = '\0';
    model->length = 0;
    model->capacity = 0;
}

static void model_append(model_t *model, const char *bytes, size_t size)
{
    CHECK((model->length + size) <= MAX_CONTENT);
    if (size == 0)
    {
        return; /* bytes may be NULL */
    }
    memmove(model->bytes + model->length, bytes, size);
    model->length += size;
    model->bytes[model->length] = '\0';
}

static void model_truncate(model_t *model, size_t length)
{
    model->length = length;
    model->bytes[length] = '\0';
}

/* What an operation that needs room for `additional` more bytes must do. */
typedef struct
{
    amark_error_t error;  /* the expected result */
    size_t calls;         /* the expected number of calls of the allocator */
    size_t capacity;      /* the capacity after it */
    bool too_large;       /* it would succeed beyond MAX_CONTENT: skip it */
} expectation_t;

static expectation_t expect_room(const model_t *model, size_t additional, bool inject)
{
    expectation_t expected;
    size_t required;

    expected.error = AMARK_ERROR_NONE;
    expected.calls = 0;
    expected.capacity = model->capacity;
    expected.too_large = false;
    if (additional == 0)
    {
        return expected;
    }
    if ((additional > (SIZE_MAX - model->length)) || ((model->length + additional) == SIZE_MAX))
    {
        expected.error = AMARK_ERROR_OVERFLOW;
        return expected;
    }
    required = model->length + additional + 1;
    if (required <= model->capacity)
    {
        expected.too_large = (model->length + additional) > MAX_CONTENT;
        return expected;
    }
    expected.calls = 1;
    if (inject)
    {
        expected.error = AMARK_ERROR_OUT_OF_MEMORY;
        return expected;
    }
    if ((model->capacity <= (SIZE_MAX / 2)) && ((model->capacity * 2) >= required))
    {
        expected.capacity = model->capacity * 2;
    }
    else
    {
        expected.capacity = required;
    }
    expected.too_large = (model->length + additional) > MAX_CONTENT;
    return expected;
}

/* Checks that buffer matches model and the invariants of a buffer. */
static void check_buffer(const amark_buffer_t *buffer, const model_t *model)
{
    const char *content = amark_buffer_data(buffer);
    size_t length = amark_buffer_length(buffer);

    CHECK(content != NULL);
    CHECK(length == model->length);
    CHECK(content[length] == '\0');
    CHECK(memcmp(content, model->bytes, length) == 0);
    CHECK((buffer->data == content) && (buffer->length == length));
    CHECK(buffer->capacity == model->capacity);
    if (buffer->capacity == 0)
    {
        CHECK(length == 0);
    }
    else
    {
        CHECK(length < buffer->capacity);
    }
}

/* ---- The operations ---- */

enum
{
    OP_RESERVE,
    OP_APPEND,
    OP_APPEND_BYTE,
    OP_APPEND_REPEAT,
    OP_APPEND_CSTR,
    OP_APPEND_CODEPOINT,
    OP_TRUNCATE,
    OP_CLEAR,
    OP_DETACH,
    OP_APPEND_SELF,
    OP_APPEND_CSTR_SELF,
    OP_OVERFLOW,
    OP_INVALID_ARGUMENT,
    OP_RECREATE,
    OP_COUNT
};

typedef struct
{
    amark_buffer_t *created;  /* never NULL between operations */
    amark_buffer_t embedded;
    model_t models[2];
} state_t;

/* Runs an operation whose result expect_room predicts, with failures injected
 * when inject is set. Returns the error. */
#define RUN(expected, call)                                \
    do {                                                   \
        amark_error_t run_error_;                          \
        fuzz_allocator.calls = 0;                          \
        fuzz_allocator.fail = inject;                      \
        run_error_ = (call);                               \
        fuzz_allocator.fail = false;                       \
        CHECK(run_error_ == (expected).error);             \
        CHECK(fuzz_allocator.calls == (expected).calls);   \
    } while (0)

static void run_append(amark_buffer_t *buffer, model_t *model, const char *bytes,
                       size_t size, bool inject)
{
    expectation_t expected = expect_room(model, size, inject);

    if (expected.too_large)
    {
        return;
    }
    RUN(expected, amark_buffer_append(buffer, bytes, size));
    if (expected.error == AMARK_ERROR_NONE)
    {
        model->capacity = expected.capacity;
        model_append(model, bytes, size);
    }
}

static void run_operation(state_t *state, reader_t *reader)
{
    uint8_t head = read_byte(reader);
    bool inject = (head & 0x80u) != 0;
    int which = (head >> 6) & 1;
    amark_buffer_t *buffer = which == 0 ? state->created : &state->embedded;
    model_t *model = &state->models[which];
    expectation_t expected;

    switch ((head & 0x3Fu) % OP_COUNT)
    {
    case OP_RESERVE:
    {
        uint8_t variant = read_byte(reader);
        size_t additional = read_u16(reader);
        if (variant & 1u)
        {
            /* Near SIZE_MAX: an overflow, or a huge request that only an injected
             * failure lets run. */
            additional = SIZE_MAX - (size_t)(variant >> 1);
        }
        expected = expect_room(model, additional, inject);
        if (expected.too_large)
        {
            break;
        }
        RUN(expected, amark_buffer_reserve(buffer, additional));
        if (expected.error == AMARK_ERROR_NONE)
        {
            model->capacity = expected.capacity;
        }
        break;
    }
    case OP_APPEND:
    {
        size_t size = read_byte(reader);
        size_t available = reader->size - reader->position;
        const char *bytes;
        if (size > available)
        {
            size = available;
        }
        bytes = size == 0 ? NULL : (const char *)reader->data + reader->position;
        reader->position += size;
        run_append(buffer, model, bytes, size, inject);
        break;
    }
    case OP_APPEND_BYTE:
    {
        char byte = (char)read_byte(reader);
        expected = expect_room(model, 1, inject);
        if (expected.too_large)
        {
            break;
        }
        RUN(expected, amark_buffer_append_byte(buffer, (unsigned char)byte));
        if (expected.error == AMARK_ERROR_NONE)
        {
            model->capacity = expected.capacity;
            model_append(model, &byte, 1);
        }
        break;
    }
    case OP_APPEND_REPEAT:
    {
        uint8_t byte = read_byte(reader);
        size_t count = read_u16(reader) & 0x0FFFu;
        expected = expect_room(model, count, inject);
        if (expected.too_large)
        {
            break;
        }
        RUN(expected, amark_buffer_append_repeat(buffer, byte, count));
        if (expected.error == AMARK_ERROR_NONE)
        {
            model->capacity = expected.capacity;
            CHECK((model->length + count) <= MAX_CONTENT);
            memset(model->bytes + model->length, byte, count);
            model_truncate(model, model->length + count);
        }
        break;
    }
    case OP_APPEND_CSTR:
    {
        /* The next bytes, up to the first NUL, as a C string of its own. */
        char text[256];
        size_t size = read_byte(reader);
        size_t i;
        for (i = 0; (i < size) && (i < (sizeof(text) - 1)); i++)
        {
            text[i] = (char)read_byte(reader);
        }
        text[i] = '\0';
        expected = expect_room(model, strlen(text), inject);
        if (expected.too_large)
        {
            break;
        }
        RUN(expected, amark_buffer_append_cstr(buffer, text));
        if (expected.error == AMARK_ERROR_NONE)
        {
            model->capacity = expected.capacity;
            model_append(model, text, strlen(text));
        }
        break;
    }
    case OP_APPEND_CODEPOINT:
    {
        uint32_t codepoint = read_u32(reader);
        char encoded[AMARK_UTF8_MAX_LENGTH];
        size_t size;
        if (codepoint & 0x80000000u)
        {
            /* Mostly in the Unicode range, sometimes beyond it. */
            codepoint &= 0x1FFFFFu;
        }
        size = amark_utf8_encode(codepoint, encoded);
        expected = expect_room(model, size, inject);
        if (expected.too_large)
        {
            break;
        }
        RUN(expected, amark_buffer_append_codepoint(buffer, codepoint));
        if (expected.error == AMARK_ERROR_NONE)
        {
            model->capacity = expected.capacity;
            model_append(model, encoded, size);
        }
        break;
    }
    case OP_TRUNCATE:
    {
        uint8_t variant = read_byte(reader);
        size_t length = read_u16(reader);
        if ((variant & 1u) == 0)
        {
            length %= model->length + 1;
        }
        expected.error =
            length > model->length ? AMARK_ERROR_INVALID_ARGUMENT : AMARK_ERROR_NONE;
        expected.calls = 0;
        RUN(expected, amark_buffer_truncate(buffer, length));
        if (expected.error == AMARK_ERROR_NONE)
        {
            model_truncate(model, length);
        }
        break;
    }
    case OP_CLEAR:
        fuzz_allocator.calls = 0;
        fuzz_allocator.fail = inject;
        amark_buffer_clear(buffer);
        fuzz_allocator.fail = false;
        CHECK(fuzz_allocator.calls == 0);
        model_truncate(model, 0);
        break;
    case OP_DETACH:
    {
        char sentinel = 'x';
        char *detached = &sentinel;
        size_t detached_length = 1;
        expected.error = (model->capacity == 0) && inject ? AMARK_ERROR_OUT_OF_MEMORY
                                                        : AMARK_ERROR_NONE;
        expected.calls = model->capacity == 0 ? 1 : 0;
        RUN(expected, amark_buffer_detach(buffer, &detached, &detached_length));
        if (expected.error == AMARK_ERROR_NONE)
        {
            CHECK(detached != NULL);
            CHECK(detached_length == model->length);
            CHECK(memcmp(detached, model->bytes, model->length) == 0);
            CHECK(detached[detached_length] == '\0');
            amark_free(detached);
            model_truncate(model, 0);
            model->capacity = 0;
        }
        else
        {
            CHECK((detached == NULL) && (detached_length == 0));
        }
        break;
    }
    case OP_APPEND_SELF:
    {
        /* A part of the buffer's own content, which may move when it grows. */
        size_t offset = read_u16(reader) % (model->length + 1);
        size_t size = read_u16(reader) % (model->length - offset + 1);
        expected = expect_room(model, size, inject);
        if (expected.too_large)
        {
            break;
        }
        RUN(expected, amark_buffer_append(buffer, amark_buffer_data(buffer) + offset, size));
        if (expected.error == AMARK_ERROR_NONE)
        {
            model->capacity = expected.capacity;
            model_append(model, model->bytes + offset, size);
        }
        break;
    }
    case OP_APPEND_CSTR_SELF:
    {
        /* The buffer's own content from an offset, as a C string: up to the first
         * NUL byte, or the terminating NUL. */
        size_t offset = read_u16(reader) % (model->length + 1);
        size_t size = strlen(model->bytes + offset);
        expected = expect_room(model, size, inject);
        if (expected.too_large)
        {
            break;
        }
        RUN(expected, amark_buffer_append_cstr(buffer, amark_buffer_data(buffer) + offset));
        if (expected.error == AMARK_ERROR_NONE)
        {
            model->capacity = expected.capacity;
            model_append(model, model->bytes + offset, size);
        }
        break;
    }
    case OP_OVERFLOW:
    {
        /* A count near SIZE_MAX: an overflow, or a huge request that only an
         * injected failure lets run. */
        uint8_t variant = read_byte(reader);
        size_t count = SIZE_MAX - (size_t)(variant >> 1);
        expected = expect_room(model, count, inject);
        if (expected.too_large)
        {
            break;
        }
        if (variant & 1u)
        {
            RUN(expected, amark_buffer_reserve(buffer, count));
        }
        else
        {
            RUN(expected, amark_buffer_append_repeat(buffer, 'x', count));
        }
        CHECK(expected.error != AMARK_ERROR_NONE);
        break;
    }
    case OP_INVALID_ARGUMENT:
    {
        char sentinel = 'x';
        char *detached = &sentinel;
        size_t detached_length = 1;
        expected.error = AMARK_ERROR_INVALID_ARGUMENT;
        expected.calls = 0;
        switch (read_byte(reader) % 9u)
        {
        case 0:
            RUN(expected, amark_buffer_append(buffer, NULL, 1 + read_byte(reader)));
            break;
        case 1:
            RUN(expected, amark_buffer_append_cstr(buffer, NULL));
            break;
        case 2:
            RUN(expected, amark_buffer_detach(buffer, NULL, &detached_length));
            CHECK(detached_length == 0);
            break;
        case 3:
            RUN(expected, amark_buffer_detach(buffer, &detached, NULL));
            CHECK(detached == NULL);
            break;
        case 4:
            RUN(expected, amark_buffer_append_byte(NULL, 'x'));
            break;
        case 5:
            RUN(expected, amark_buffer_reserve(NULL, 1));
            break;
        case 6:
            RUN(expected, amark_buffer_append_codepoint(NULL, 0x41u));
            break;
        case 7:
            /* The functions that free or empty accept NULL. */
            amark_buffer_destroy(NULL);
            amark_buffer_clear(NULL);
            amark_buffer_deinit(NULL);
            RUN(expected, amark_buffer_truncate(NULL, 0));
            break;
        default:
            RUN(expected, amark_buffer_create(NULL));
            break;
        }
        break;
    }
    case OP_RECREATE:
    default:
        if (which == 0)
        {
            /* Any non-NULL value, to see that a failure sets it to NULL. */
            amark_buffer_t *created = &state->embedded;
            amark_buffer_destroy(state->created);
            state->created = NULL;
            expected.error = inject ? AMARK_ERROR_OUT_OF_MEMORY : AMARK_ERROR_NONE;
            expected.calls = 1;
            RUN(expected, amark_buffer_create(&created));
            if (inject)
            {
                CHECK(created == NULL);
                CHECK(amark_buffer_create(&created) == AMARK_ERROR_NONE);
            }
            CHECK(created != NULL);
            state->created = created;
        }
        else
        {
            amark_buffer_deinit(&state->embedded);
            amark_buffer_init(&state->embedded);
        }
        model_truncate(model, 0);
        model->capacity = 0;
        break;
    }

    check_buffer(state->created, &state->models[0]);
    check_buffer(&state->embedded, &state->models[1]);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static const amark_allocator_t allocator = {fuzz_malloc, fuzz_calloc, fuzz_realloc,
                                                fuzz_free};
    reader_t reader;
    state_t state;

    reader.data = data;
    reader.size = size;
    reader.position = 0;

    fuzz_allocator.fail = false;
    fuzz_allocator.calls = 0;
    fuzz_allocator.live = 0;
    CHECK(amark_allocator_set(&allocator) == AMARK_ERROR_NONE);

    CHECK(amark_buffer_create(&state.created) == AMARK_ERROR_NONE);
    CHECK(state.created != NULL);
    amark_buffer_init(&state.embedded);
    model_init(&state.models[0]);
    model_init(&state.models[1]);
    check_buffer(state.created, &state.models[0]);
    check_buffer(&state.embedded, &state.models[1]);
    CHECK((amark_buffer_data(NULL)[0] == '\0') && (amark_buffer_length(NULL) == 0));

    while (reader.position < reader.size)
    {
        run_operation(&state, &reader);
    }

    amark_buffer_destroy(state.created);
    amark_buffer_deinit(&state.embedded);
    free(state.models[0].bytes);
    free(state.models[1].bytes);
    /* Every block allocated through the allocator was freed. */
    CHECK(fuzz_allocator.live == 0);
    CHECK(amark_allocator_set(NULL) == AMARK_ERROR_NONE);
    return 0;
}
