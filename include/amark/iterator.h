/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The traversal of a subtree in document order, with enter and exit events: a
 * type that can have children is entered and exited, a leaf is only entered. The
 * traversal never leaves its root.
 *
 * The iterator holds only the current node and event, and decides the next step
 * from the links at the time of amark_iter_next. Changing the content or the
 * children of the current node, or any node other than the current node and its
 * ancestors, is safe during the traversal. After the current node or one of its
 * ancestors is detached, moved or destroyed, set the position again with
 * amark_iter_reset before the next amark_iter_next (otherwise the result is
 * undefined).
 */
#ifndef AMARK_ITERATOR_H
#define AMARK_ITERATOR_H

#include <amark/export.h>
#include <amark/error.h>
#include <amark/node.h>

AMARK_BEGIN_DECLS

/* An iterator (opaque). */
typedef struct amark_iter amark_iter_t;

/* An event of the traversal. */
typedef enum amark_event
{
    AMARK_EVENT_NONE = 0,
    AMARK_EVENT_DONE = 1,
    AMARK_EVENT_ENTER = 2,
    AMARK_EVENT_EXIT = 3
} amark_event_t;

/* Creates in *out_iter an iterator over the subtree of root (any node, attached
 * or not), before its first event: its node is NULL and its event
 * AMARK_EVENT_NONE. On a failure, *out_iter is set to NULL. */
AMARK_API amark_error_t amark_iter_create(amark_node_t *root, amark_iter_t **out_iter);

/* Destroys iter. Does nothing for NULL. */
AMARK_API void amark_iter_destroy(amark_iter_t *iter);

/* Moves to the next event and returns it: (root, ENTER) first, AMARK_EVENT_DONE at
 * the end (with a NULL node), and AMARK_EVENT_DONE again on every later call.
 * Returns AMARK_EVENT_DONE for NULL. */
AMARK_API amark_event_t amark_iter_next(amark_iter_t *iter);

/* The current node and event of iter (NULL and AMARK_EVENT_NONE for NULL). */
AMARK_API amark_node_t *amark_iter_node(const amark_iter_t *iter);
AMARK_API amark_event_t amark_iter_event(const amark_iter_t *iter);

/* The root of iter. */
AMARK_API amark_node_t *amark_iter_root(const amark_iter_t *iter);

/* Sets the current node and event of iter; the next amark_iter_next continues
 * from there. node must be in the subtree of the root, event must be
 * AMARK_EVENT_ENTER or AMARK_EVENT_EXIT, and a leaf (a node whose type cannot have
 * children) cannot get AMARK_EVENT_EXIT; otherwise returns
 * AMARK_ERROR_INVALID_ARGUMENT and iter is unchanged. */
AMARK_API amark_error_t amark_iter_reset(amark_iter_t *iter, amark_node_t *node,
                                         amark_event_t event);

AMARK_END_DECLS

#endif /* AMARK_ITERATOR_H */
