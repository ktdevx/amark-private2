/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The definition of amark_buffer_t, so that internal modules can embed a buffer
 * by value in their own structures and use the public amark_buffer_* functions on
 * it. Internal: not part of the public API and not exported.
 */
#ifndef AMARK_BASE_BUFFER_INTERNAL_H
#define AMARK_BASE_BUFFER_INTERNAL_H

#include <stddef.h>

#include <amark/buffer.h>

/*
 * Invariants: length < capacity and data[length] == '\0'; or, with capacity 0,
 * length is 0 and data points to a static, read-only "" that is never written to
 * nor freed.
 */
struct amark_buffer {
    char *data;
    size_t length;
    size_t capacity;
};

/* Initializes *buffer as an empty buffer. Allocates nothing and cannot fail. */
void amark_buffer_init(amark_buffer_t *buffer);

/* Frees the content of *buffer (not buffer itself) and leaves it empty, as after
 * amark_buffer_init. Does nothing for NULL. */
void amark_buffer_deinit(amark_buffer_t *buffer);

#endif /* AMARK_BASE_BUFFER_INTERNAL_H */
