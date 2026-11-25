/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The global allocator of Amark: every allocation of the library goes through it,
 * and the application can replace it.
 *
 * Until amark_allocator_set is called, Amark uses the default allocator, built on
 * malloc, calloc, realloc and free of the C standard library.
 *
 * When the allocator may be changed:
 *   Call amark_allocator_set only while no memory allocated by Amark is still
 *   alive (memory from one allocator must not be freed by another) and no other
 *   thread is calling an Amark function. Usually the application sets it once, at
 *   startup. Breaking these conditions is undefined behavior; Amark does not
 *   detect it, and uses no lock or atomic operation for the setting.
 *
 * Each copy of Amark has its own setting: when the static library is linked into
 * several modules, each module has an independent allocator, and memory allocated
 * through one of them must not be freed through another.
 *
 * Requirements on the callbacks:
 *   - They have the shape of malloc, calloc, realloc and free, and receive no
 *     user data; an allocator that needs state keeps it in its own variables.
 *   - When Amark is used from several threads, the callbacks must be thread-safe.
 *   - They must not call Amark functions.
 *   - The memory they return is suitably aligned for any object type, like
 *     malloc. calloc_fn returns count * size bytes that are all zero; Amark does
 *     not zero the memory itself. The contents of memory from malloc_fn, and from
 *     realloc_fn beyond what it carries over, are indeterminate.
 *   - When realloc_fn fails, it returns NULL and the original block stays valid.
 *
 * What Amark guarantees to the callbacks:
 *   - size, count and new_size are never 0, and count * size passed to calloc_fn
 *     does not overflow.
 *   - realloc_fn and free_fn never receive NULL.
 *
 * Allocations of size 0:
 *   amark_malloc(0), amark_calloc with count or size 0, and amark_realloc(NULL, 0)
 *   call no callback and return NULL. amark_realloc(ptr, 0) with a non-NULL ptr
 *   frees ptr with free_fn and returns NULL. These NULLs are not failures. A
 *   caller whose size may be 0 treats a NULL result as a failure only when the
 *   requested size (for amark_calloc, both count and size) is not 0.
 */
#ifndef AMARK_ALLOCATOR_H
#define AMARK_ALLOCATOR_H

#include <stddef.h>

#include <amark/export.h>
#include <amark/error.h>

AMARK_BEGIN_DECLS

/*
 * An allocator: the four functions of the C standard library. The layout is fixed
 * forever, so that programs built against an older version keep working: fields
 * are never added, removed, reordered or retyped. All four callbacks are
 * required.
 */
typedef struct amark_allocator
{
    void *(*malloc_fn)(size_t size);
    void *(*calloc_fn)(size_t count, size_t size);
    void *(*realloc_fn)(void *ptr, size_t new_size);
    void (*free_fn)(void *ptr);
} amark_allocator_t;

/*
 * Sets the global allocator to a copy of *allocator; allocator itself need not
 * outlive the call. NULL restores the default allocator. Returns
 * AMARK_ERROR_NONE, or AMARK_ERROR_INVALID_ARGUMENT when one of the callbacks is
 * NULL, and then the current setting is left unchanged. See the conditions above
 * for when it may be called.
 */
AMARK_API amark_error_t amark_allocator_set(const amark_allocator_t *allocator);

/* Allocates size bytes with the global allocator. Returns NULL on failure, and
 * also, without calling the allocator, when size is 0. */
AMARK_API void *amark_malloc(size_t size);

/* Allocates count * size bytes, all zero, with calloc_fn of the global allocator.
 * Returns NULL on failure, also without calling the allocator when count * size
 * overflows, and, not as a failure, without calling it when count or size is 0. */
AMARK_API void *amark_calloc(size_t count, size_t size);

/* Resizes ptr to new_size bytes, keeping its contents up to the smaller size.
 * amark_realloc(NULL, n) is amark_malloc(n). With a non-NULL ptr and new_size 0,
 * frees ptr and returns NULL. Returns NULL on failure, and ptr then stays valid. */
AMARK_API void *amark_realloc(void *ptr, size_t new_size);

/* Frees ptr, allocated by the functions above or by Amark. Does nothing for
 * NULL. */
AMARK_API void amark_free(void *ptr);

AMARK_END_DECLS

#endif /* AMARK_ALLOCATOR_H */
