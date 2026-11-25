/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The error codes of Amark and the conventions every public function follows.
 *
 * Conventions of the public API:
 *   - A function that can fail returns amark_error_t: AMARK_ERROR_NONE on
 *     success, another code on failure. Its result goes to an output argument,
 *     which is the last argument and whose name starts with out_.
 *   - On failure, the object the function operates on keeps the valid state it
 *     had before the call (strong guarantee), and an output argument that returns
 *     a pointer to an object is set to NULL. Each function documents what it does
 *     with its other output arguments on failure.
 *   - A NULL passed to a required pointer argument makes the function return
 *     AMARK_ERROR_INVALID_ARGUMENT; it does not crash.
 *   - A function that frees an object accepts NULL and then does nothing.
 *   - A function that cannot fail (a query, a classification, a decoding) returns
 *     its value directly. The allocation functions (amark_malloc and the others)
 *     follow malloc and report a failure with NULL.
 *   - Amark has no global mutable state other than the allocator setting, so
 *     different objects can be used from different threads at the same time.
 *     Using the same object from several threads at once needs synchronization
 *     by the application.
 *
 * Error codes:
 *   - The codes and their descriptions are defined once, in AMARK_ERROR_LIST, in
 *     the order of their values: AMARK_ERROR_NONE is 0, and each following code
 *     is one more. Compare a code by its name, not by its value.
 *   - The value of an existing code never changes in a future version. New codes
 *     are only added at the end of the list. Treat an unknown non-zero value as a
 *     general failure.
 */
#ifndef AMARK_ERROR_H
#define AMARK_ERROR_H

#include <amark/export.h>

/*
 * X(name, description) for every error code, in the order of their values. The
 * single definition of the codes and their descriptions: add a new code only at
 * the end, and never reorder, insert or remove one.
 *
 *   AMARK_ERROR_NONE              Success.
 *   AMARK_ERROR_OUT_OF_MEMORY     The allocator failed to allocate memory.
 *   AMARK_ERROR_INVALID_ARGUMENT  A required argument is NULL, a value is out of
 *                                 range, or an allocator is incomplete.
 *   AMARK_ERROR_OVERFLOW          A size calculation does not fit in size_t.
 *   AMARK_ERROR_LIMIT_EXCEEDED    A documented limit (a nesting depth, for
 *                                 example) is exceeded.
 *   AMARK_ERROR_INVALID_STATE     The state of the object does not allow the
 *                                 operation.
 *   AMARK_ERROR_UNREPRESENTABLE   The target cannot be represented in the
 *                                 requested output format without changing its
 *                                 meaning (Markdown output, for example). The
 *                                 functions that return it document when.
 */
#define AMARK_ERROR_LIST(X)                             \
    X(AMARK_ERROR_NONE, "no error")                     \
    X(AMARK_ERROR_OUT_OF_MEMORY, "out of memory")       \
    X(AMARK_ERROR_INVALID_ARGUMENT, "invalid argument") \
    X(AMARK_ERROR_OVERFLOW, "overflow")                 \
    X(AMARK_ERROR_LIMIT_EXCEEDED, "limit exceeded")     \
    X(AMARK_ERROR_INVALID_STATE, "invalid state")       \
    X(AMARK_ERROR_UNREPRESENTABLE, "unrepresentable")

AMARK_BEGIN_DECLS

/* The error codes. The values follow the order of AMARK_ERROR_LIST. */
typedef enum amark_error
{
#define AMARK_ERROR_ENUM(name, text) name,
    AMARK_ERROR_LIST(AMARK_ERROR_ENUM)
#undef AMARK_ERROR_ENUM
} amark_error_t;

/* Returns the description of error from AMARK_ERROR_LIST, or "unknown error" for
 * a value that is not in the list. The string is static; do not free it. Never
 * returns NULL. */
AMARK_API const char *amark_error_string(amark_error_t error);

AMARK_END_DECLS

#endif /* AMARK_ERROR_H */
