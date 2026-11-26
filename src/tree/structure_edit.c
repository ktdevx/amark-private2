/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/* Structural editing: insertion, unlinking, replacement, public destruction,
 * containment rule, cycle check. */

#include <amark/node.h>

#include <amark/extension.h>

#include "tree/tree.h"

#include <stdbool.h>

/* --- Containment rule ------------------------------------------------------------ */

bool amark_node_allows_child(const amark_node_t *parent, const amark_node_type_desc_t *child_desc)
{
    if (parent->desc->can_contain != NULL)
    {
        return parent->desc->can_contain(parent->desc, child_desc);
    }

    return (parent->desc->accepts & child_desc->provides) != 0;
}
