/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * A growable byte buffer, to build byte sequences of any content (including NUL
 * bytes) safely.
 *
 * The content is always followed by a NUL byte, so amark_buffer_data can be used
 * as a C string when the content holds no NUL byte; amark_buffer_length gives the
 * length without that terminating NUL.
 *
 * Growth:
 *   The required capacity of an operation that adds n bytes is length + n + 1 (the
 *   terminating NUL). Nothing is allocated when n is 0 or the current capacity is
 *   enough. Otherwise the capacity grows to the larger of twice the current
 *   capacity and the required capacity (the required capacity when twice the
 *   current one does not fit in size_t), so that appending n bytes in small pieces
 *   reallocates only a logarithmic number of times. amark_buffer_reserve follows
 *   the same rule.
 *
 * Failures:
 *   - When the required capacity does not fit in size_t, the operation returns
 *     AMARK_ERROR_OVERFLOW without calling the allocator.
 *   - When the allocator fails, the operation returns AMARK_ERROR_OUT_OF_MEMORY.
 *   In both cases the content, the length and the capacity are left unchanged.
 *
 * The memory comes from the global allocator (amark/allocator.h).
 */
#ifndef AMARK_BUFFER_H
#define AMARK_BUFFER_H

#include <stddef.h>
#include <stdint.h>

#include <amark/export.h>
#include <amark/error.h>

AMARK_BEGIN_DECLS

/* A growable byte buffer (opaque). */
typedef struct amark_buffer amark_buffer_t;

/*
 * Creates an empty buffer in *out_buffer; it allocates no content until bytes are
 * added. Returns AMARK_ERROR_INVALID_ARGUMENT when out_buffer is NULL, or
 * AMARK_ERROR_OUT_OF_MEMORY, and then *out_buffer is set to NULL.
 */
AMARK_API amark_error_t amark_buffer_create(amark_buffer_t **out_buffer);

/* Destroys buffer and its content. Does nothing for NULL. */
AMARK_API void amark_buffer_destroy(amark_buffer_t *buffer);

/*
 * Returns the content of buffer, followed by a NUL byte. Never returns NULL: an
 * empty buffer (and a NULL buffer) gives "". The pointer is valid until the next
 * operation that changes buffer.
 */
AMARK_API const char *amark_buffer_data(const amark_buffer_t *buffer);

/* Returns the length of the content in bytes, without the terminating NUL; 0 for
 * a NULL buffer. */
AMARK_API size_t amark_buffer_length(const amark_buffer_t *buffer);

/*
 * Makes sure that `additional` more bytes can be added without allocating. Does
 * not change the content.
 */
AMARK_API amark_error_t amark_buffer_reserve(amark_buffer_t *buffer, size_t additional);

/*
 * Appends the size bytes at data, which may hold any bytes including NUL. data
 * may point into the content of buffer itself. data may be NULL when size is 0;
 * a NULL data with a size other than 0 is AMARK_ERROR_INVALID_ARGUMENT.
 */
AMARK_API amark_error_t amark_buffer_append(amark_buffer_t *buffer, const void *data,
                                            size_t size);

/* Appends the single byte byte. */
AMARK_API amark_error_t amark_buffer_append_byte(amark_buffer_t *buffer, unsigned char byte);

/* Appends count copies of byte. */
AMARK_API amark_error_t amark_buffer_append_repeat(amark_buffer_t *buffer, unsigned char byte,
                                                   size_t count);

/* Appends the NUL-terminated string text, without its NUL. text may point into
 * the content of buffer itself. */
AMARK_API amark_error_t amark_buffer_append_cstr(amark_buffer_t *buffer, const char *text);

/*
 * Appends codepoint encoded as UTF-8, by the rules of amark_utf8_encode: a
 * surrogate or a value above U+10FFFF is appended as U+FFFD.
 */
AMARK_API amark_error_t amark_buffer_append_codepoint(amark_buffer_t *buffer,
                                                      uint32_t codepoint);

/*
 * Shortens the content to its first length bytes; the capacity is kept. Returns
 * AMARK_ERROR_INVALID_ARGUMENT, leaving buffer unchanged, when length is greater
 * than the current length.
 */
AMARK_API amark_error_t amark_buffer_truncate(amark_buffer_t *buffer, size_t length);

/* Empties the content; the capacity is kept. Does nothing for NULL. */
AMARK_API void amark_buffer_clear(amark_buffer_t *buffer);

/*
 * Takes the content out of buffer: stores the content, followed by a NUL byte, in
 * *out_data and its length in *out_length, and gives its ownership to the caller,
 * who frees it with amark_free. buffer is then empty, with no capacity, and can
 * be used again. An empty buffer with no capacity gives a newly allocated "".
 * Both output arguments are required. On failure (AMARK_ERROR_INVALID_ARGUMENT,
 * or AMARK_ERROR_OUT_OF_MEMORY when an empty buffer cannot allocate its ""),
 * *out_data is set to NULL and *out_length to 0 when they are not NULL, and
 * buffer is left unchanged.
 */
AMARK_API amark_error_t amark_buffer_detach(amark_buffer_t *buffer, char **out_data,
                                            size_t *out_length);

AMARK_END_DECLS

#endif /* AMARK_BUFFER_H */
