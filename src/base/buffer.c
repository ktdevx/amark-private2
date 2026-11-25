/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

#include "base/buffer_internal.h"

#include <amark/allocator.h>
#include <amark/buffer.h>
#include <amark/utf8.h>

#include "base/allocator_internal.h"

#include <stdint.h>
#include <string.h>

/* The content of a buffer with no capacity. Read-only: nothing writes to it (a
 * buffer with capacity 0 has length 0 and is never written to) and nothing frees
 * it. */
static const char empty_content[1] = "";

void amark_buffer_init(amark_buffer_t *buffer)
{
    buffer->data = (char *)empty_content;
    buffer->length = 0;
    buffer->capacity = 0;
}

void amark_buffer_deinit(amark_buffer_t *buffer)
{
    if (buffer == NULL)
    {
        return;
    }
    if (buffer->capacity > 0)
    {
        amark_free(buffer->data);
    }
    amark_buffer_init(buffer);
}

/*
 * Makes room for additional more bytes after the content and the terminating
 * NUL. Grows only when the capacity is not enough, to the larger of twice the
 * capacity and the required capacity (the required one when twice does not fit in
 * size_t). On failure, the buffer is unchanged.
 */
static amark_error_t ensure_room(amark_buffer_t *buffer, size_t additional)
{
    size_t required;
    size_t new_capacity;
    char *new_data;

    if (additional == 0)
    {
        return AMARK_ERROR_NONE;
    }
    if (!amark_size_add(buffer->length, additional, &required) ||
        !amark_size_add(required, 1, &required))
    {
        return AMARK_ERROR_OVERFLOW;
    }
    if (required <= buffer->capacity)
    {
        return AMARK_ERROR_NONE;
    }
    if (!amark_size_mul(buffer->capacity, 2, &new_capacity) || (new_capacity < required))
    {
        new_capacity = required;
    }
    if (buffer->capacity == 0)
    {
        new_data = amark_malloc(new_capacity);
        if (new_data == NULL)
        {
            return AMARK_ERROR_OUT_OF_MEMORY;
        }
        new_data[0] = '\0';
    }
    else
    {
        new_data = amark_realloc(buffer->data, new_capacity);
        if (new_data == NULL)
        {
            return AMARK_ERROR_OUT_OF_MEMORY;
        }
    }
    buffer->data = new_data;
    buffer->capacity = new_capacity;
    return AMARK_ERROR_NONE;
}

/* Returns whether data lies in the allocated block of buffer, so that its offset
 * survives a reallocation. The comparison goes through uintptr_t because
 * comparing pointers into different objects is undefined. */
static int points_into(const amark_buffer_t *buffer, const void *data)
{
    uintptr_t start = (uintptr_t)buffer->data;
    uintptr_t p = (uintptr_t)data;

    return (buffer->capacity > 0) && (p >= start) && ((p - start) < buffer->capacity);
}

amark_error_t amark_buffer_create(amark_buffer_t **out_buffer)
{
    amark_buffer_t *buffer;

    if (out_buffer == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    buffer = amark_malloc(sizeof(*buffer));
    if (buffer == NULL)
    {
        *out_buffer = NULL;
        return AMARK_ERROR_OUT_OF_MEMORY;
    }
    amark_buffer_init(buffer);
    *out_buffer = buffer;
    return AMARK_ERROR_NONE;
}

void amark_buffer_destroy(amark_buffer_t *buffer)
{
    if (buffer == NULL)
    {
        return;
    }
    amark_buffer_deinit(buffer);
    amark_free(buffer);
}

const char *amark_buffer_data(const amark_buffer_t *buffer)
{
    if (buffer == NULL)
    {
        return empty_content;
    }
    return buffer->data;
}

size_t amark_buffer_length(const amark_buffer_t *buffer)
{
    if (buffer == NULL)
    {
        return 0;
    }
    return buffer->length;
}

amark_error_t amark_buffer_reserve(amark_buffer_t *buffer, size_t additional)
{
    if (buffer == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    return ensure_room(buffer, additional);
}

amark_error_t amark_buffer_append(amark_buffer_t *buffer, const void *data, size_t size)
{
    amark_error_t err;
    const char *source = data;

    if ((buffer == NULL) || ((data == NULL) && (size != 0)))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    if (size == 0)
    {
        return AMARK_ERROR_NONE;
    }
    if (points_into(buffer, data))
    {
        /* The source is the buffer's own memory: find it again after growing. */
        size_t offset = (size_t)((uintptr_t)data - (uintptr_t)buffer->data);

        err = ensure_room(buffer, size);
        if (err != AMARK_ERROR_NONE)
        {
            return err;
        }
        source = buffer->data + offset;
    }
    else
    {
        err = ensure_room(buffer, size);
        if (err != AMARK_ERROR_NONE)
        {
            return err;
        }
    }
    /* memmove: a source inside the buffer may extend past the current length. */
    memmove(buffer->data + buffer->length, source, size);
    buffer->length += size;
    buffer->data[buffer->length] = '\0';
    return AMARK_ERROR_NONE;
}

amark_error_t amark_buffer_append_byte(amark_buffer_t *buffer, unsigned char byte)
{
    return amark_buffer_append_repeat(buffer, byte, 1);
}

amark_error_t amark_buffer_append_repeat(amark_buffer_t *buffer, unsigned char byte,
                                         size_t count)
{
    amark_error_t err;

    if (buffer == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    if (count == 0)
    {
        return AMARK_ERROR_NONE;
    }
    err = ensure_room(buffer, count);
    if (err != AMARK_ERROR_NONE)
    {
        return err;
    }
    memset(buffer->data + buffer->length, byte, count);
    buffer->length += count;
    buffer->data[buffer->length] = '\0';
    return AMARK_ERROR_NONE;
}

amark_error_t amark_buffer_append_cstr(amark_buffer_t *buffer, const char *text)
{
    if ((buffer == NULL) || (text == NULL))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    return amark_buffer_append(buffer, text, strlen(text));
}

amark_error_t amark_buffer_append_codepoint(amark_buffer_t *buffer, uint32_t codepoint)
{
    char encoded[AMARK_UTF8_MAX_LENGTH];
    size_t size;

    if (buffer == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    size = amark_utf8_encode(codepoint, encoded);
    return amark_buffer_append(buffer, encoded, size);
}

amark_error_t amark_buffer_truncate(amark_buffer_t *buffer, size_t length)
{
    if ((buffer == NULL) || (length > buffer->length))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    if (buffer->capacity > 0)
    {
        buffer->length = length;
        buffer->data[length] = '\0';
    }
    return AMARK_ERROR_NONE;
}

void amark_buffer_clear(amark_buffer_t *buffer)
{
    if (buffer == NULL)
    {
        return;
    }
    (void)amark_buffer_truncate(buffer, 0);
}

amark_error_t amark_buffer_detach(amark_buffer_t *buffer, char **out_data, size_t *out_length)
{
    char *data;

    if ((buffer == NULL) || (out_data == NULL) || (out_length == NULL))
    {
        if (out_data != NULL)
        {
            *out_data = NULL;
        }
        if (out_length != NULL)
        {
            *out_length = 0;
        }
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    if (buffer->capacity == 0)
    {
        data = amark_malloc(1);
        if (data == NULL)
        {
            *out_data = NULL;
            *out_length = 0;
            return AMARK_ERROR_OUT_OF_MEMORY;
        }
        data[0] = '\0';
    }
    else
    {
        data = buffer->data;
    }
    *out_data = data;
    *out_length = buffer->length;
    amark_buffer_init(buffer);
    return AMARK_ERROR_NONE;
}
