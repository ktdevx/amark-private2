/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The traversal of a subtree with enter and exit events.
 */
#ifndef AMARK_ITERATOR_H
#define AMARK_ITERATOR_H

#include <amark/export.h>
#include <amark/error.h>
#include <amark/node.h>

AMARK_BEGIN_DECLS

/* The events of a traversal. */
typedef enum amark_event
{
    /* The traversal is over (it does not leave the root). */
    AMARK_EVENT_DONE = 1,
    /* Entering a node. */
    AMARK_EVENT_ENTER,
    /* Leaving a node whose type can have children (after its children). */
    AMARK_EVENT_EXIT
} amark_event_t;

/* A traversal of the subtree of a root (opaque). */
typedef struct amark_iter amark_iter_t;

/*
 * The traversal (4.2-4.4) returns the events of the subtree of its root in
 * document order: an enter and an exit event for a node whose type can have
 * children (it accepts block, list item or inline children), even when it has
 * none, and only an enter event for any other node. It does not leave the root:
 * after the exit event of the root (or the enter event of a root that cannot
 * have children) it returns AMARK_EVENT_DONE, and keeps returning it.
 *
 * The iterator holds only the current node and event, and computes the next
 * event from the links of the tree when amark_iter_next is called. So, during a
 * traversal, the user may change the facets, the layout, the spelling and the
 * children of the current node, and edit any node other than the current node
 * and its ancestors (up to the root): the traversal continues over the
 * unvisited part of the changed tree in document order (the children of the
 * current node after its enter event, then the following siblings of the
 * current node and of its ancestors). Removing, moving or destroying the
 * current node or one of its ancestors is not supported; reset the iterator to
 * a node still in the subtree first.
 */

/* Creates in *out a traversal of the subtree of root (any node: the root of a
 * document, a node in a tree or a detached node); the first call of
 * amark_iter_next returns the enter event of root. AMARK_ERROR_INVALID_ARGUMENT
 * for NULL; on failure *out is NULL. */
AMARK_API amark_error_t amark_iter_create(amark_node_t *root, amark_iter_t **out);

/* Frees iter (does nothing for NULL). The tree is not changed. */
AMARK_API void amark_iter_destroy(amark_iter_t *iter);

/* Moves to the next event and returns it (AMARK_EVENT_DONE for NULL). */
AMARK_API amark_event_t amark_iter_next(amark_iter_t *iter);

/* The node of the current event: NULL before the first amark_iter_next and for
 * NULL, the root once the traversal is over. */
AMARK_API amark_node_t *amark_iter_node(const amark_iter_t *iter);

/*
 * Sets the position of iter to the event event of node (4.4): amark_iter_node
 * then returns node, and the next amark_iter_next returns the event that
 * follows that position in document order. node must be the root or one of its
 * descendants, and event must be AMARK_EVENT_ENTER, or AMARK_EVENT_EXIT for a
 * node whose type can have children; AMARK_ERROR_INVALID_ARGUMENT, with no
 * change, otherwise and for NULL.
 */
AMARK_API amark_error_t amark_iter_reset(amark_iter_t *iter, amark_node_t *node,
                                         amark_event_t event);

AMARK_END_DECLS

#endif /* AMARK_ITERATOR_H */
