/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * Equality: the semantic comparison (13.1-13.3).
 *
 * The two subtrees are walked in parallel, in preorder, without recursion and
 * without a stack: each step moves both walks the same way (to the first child,
 * to the next sibling, or up to the parent), so the positions stay at the same
 * depth. The walk compares units: a node, or, with AMARK_EQUAL_NORMALIZE_TEXT,
 * a run of adjacent text siblings, whose literals are compared as one string
 * (a run whose literals are all empty is skipped).
 */
#include "tree/tree.h"

#include <string.h>

#include <amark/value.h>

/* ---- Facets -------------------------------------------------------------------- */

/* Whether the facets of the type of a, numbered as in desc, are compared: the
 * value facets, but the label of a link or an image only as a style, and the
 * style facets with AMARK_EQUAL_STYLE (13.1, 13.2). */
static bool facet_compared(const amark_node_t *a, amark_facet_t facet,
                           const amark_facet_desc_t *desc, uint32_t flags)
{
    bool style = desc->role == AMARK_FACET_STYLE;

    if ((a->type == AMARK_NODE_LINK || a->type == AMARK_NODE_IMAGE) && facet == AMARK_LINK_LABEL)
    {
        style = true;
    }
    return !style || (flags & AMARK_EQUAL_STYLE) != 0u;
}

/* Whether the values of facet of a and b are the same. *failed is set when a
 * value cannot be read. */
static bool facet_equal(const amark_node_t *a, const amark_node_t *b, amark_facet_t facet,
                        bool *failed)
{
    amark_value_t va;
    amark_value_t vb;

    va.struct_size = sizeof(va);
    vb.struct_size = sizeof(vb);
    if (amark_node_get(a, facet, &va) != AMARK_ERROR_NONE ||
        amark_node_get(b, facet, &vb) != AMARK_ERROR_NONE)
    {
        *failed = true;
        return false;
    }
    /* Reading b does not change the value of a: a derived value is cached in
     * its own node, and a resolved value belongs to its definition. */
    return va.state == vb.state && va.number == vb.number && va.length == vb.length &&
           (va.length == 0 || memcmp(va.text, vb.text, va.length) == 0);
}

/* Whether a and b have the same type: the same descriptor, or descriptors of
 * the same name and shape in different syntax sets. */
static bool type_equal(const amark_node_type_desc_t *a, const amark_node_type_desc_t *b)
{
    size_t i;

    if (a == b)
    {
        return true;
    }
    if (strcmp(a->name, b->name) != 0 || a->level != b->level ||
        a->facet_count != b->facet_count || a->data_size != b->data_size)
    {
        return false;
    }
    for (i = 0; i < a->facet_count; i++)
    {
        if (a->facets[i].role != b->facets[i].role || a->facets[i].kind != b->facets[i].kind)
        {
            return false;
        }
    }
    return true;
}

/* Whether the nodes a and b, without their children, have the same meaning
 * under flags. */
static bool node_equal(const amark_node_t *a, const amark_node_t *b, uint32_t flags, bool *failed)
{
    const amark_node_type_desc_t *desc = a->desc;
    size_t i;

    if (!type_equal(desc, b->desc))
    {
        return false;
    }
    for (i = 0; i < desc->facet_count; i++)
    {
        amark_facet_t facet = (amark_facet_t)(i + 1);

        if (facet_compared(a, facet, &desc->facets[i], flags) &&
            !facet_equal(a, b, facet, failed))
        {
            return false;
        }
    }
    if (desc->data_size > 0)
    {
        /* 12.13: the equal function of the type, else the bytes. */
        if (desc->equal != NULL)
        {
            return desc->equal(a, a->data, b, b->data);
        }
        return memcmp(a->data, b->data, desc->data_size) == 0;
    }
    return true;
}

/* ---- Units --------------------------------------------------------------------- */

static bool is_text(const amark_node_t *node)
{
    return node->type == AMARK_NODE_TEXT;
}

/* Reads the literal of the text node into *text and *length. */
static bool text_literal(const amark_node_t *node, const char **text, size_t *length,
                         bool *failed)
{
    amark_value_t value;

    value.struct_size = sizeof(value);
    if (amark_node_get(node, AMARK_TEXT_LITERAL, &value) != AMARK_ERROR_NONE)
    {
        *failed = true;
        return false;
    }
    *text = value.text;
    *length = value.length;
    return true;
}

/*
 * The first unit at node or after it among its next siblings, or NULL: its
 * first node is returned and its last node is written to *last. Without text
 * normalization a unit is one node; with it, a text node starts a run of
 * adjacent text siblings, and a run whose literals are all empty is skipped.
 * NULL also when a literal cannot be read (*failed is set).
 */
static const amark_node_t *unit_at(const amark_node_t *node, uint32_t flags,
                                   const amark_node_t **last, bool *failed)
{
    while (node != NULL)
    {
        const amark_node_t *run;
        const amark_node_t *end = node;
        bool empty = true;

        if ((flags & AMARK_EQUAL_NORMALIZE_TEXT) == 0u || !is_text(node))
        {
            *last = node;
            return node;
        }
        for (run = node; run != NULL && is_text(run); run = run->next)
        {
            end = run;
            if (empty)
            {
                const char *text = NULL;
                size_t length = 0;

                if (!text_literal(run, &text, &length, failed))
                {
                    return NULL;
                }
                empty = length == 0;
            }
        }
        if (!empty)
        {
            *last = end;
            return node;
        }
        node = end->next;
    }
    return NULL;
}

/* Whether the runs of text nodes first_a-last_a and first_b-last_b have the
 * same concatenated literal. */
static bool text_runs_equal(const amark_node_t *first_a, const amark_node_t *last_a,
                            const amark_node_t *first_b, const amark_node_t *last_b, bool *failed)
{
    const amark_node_t *next_a = first_a;
    const amark_node_t *next_b = first_b;
    const char *text_a = NULL;
    const char *text_b = NULL;
    size_t length_a = 0;
    size_t length_b = 0;

    for (;;)
    {
        size_t n;

        while (length_a == 0 && next_a != NULL)
        {
            if (!text_literal(next_a, &text_a, &length_a, failed))
            {
                return false;
            }
            next_a = (next_a == last_a) ? NULL : next_a->next;
        }
        while (length_b == 0 && next_b != NULL)
        {
            if (!text_literal(next_b, &text_b, &length_b, failed))
            {
                return false;
            }
            next_b = (next_b == last_b) ? NULL : next_b->next;
        }
        if (length_a == 0 || length_b == 0)
        {
            /* Both runs end together only when both are exhausted. */
            return length_a == length_b;
        }
        n = (length_a < length_b) ? length_a : length_b;
        if (memcmp(text_a, text_b, n) != 0)
        {
            return false;
        }
        text_a += n;
        text_b += n;
        length_a -= n;
        length_b -= n;
    }
}

/* Whether the units first_a-last_a and first_b-last_b (unit_at) are equal,
 * without the children of a one-node unit. */
static bool unit_equal(const amark_node_t *first_a, const amark_node_t *last_a,
                       const amark_node_t *first_b, const amark_node_t *last_b, uint32_t flags,
                       bool *failed)
{
    if ((flags & AMARK_EQUAL_NORMALIZE_TEXT) != 0u && (is_text(first_a) || is_text(first_b)))
    {
        return is_text(first_a) && is_text(first_b) &&
               text_runs_equal(first_a, last_a, first_b, last_b, failed);
    }
    return node_equal(first_a, first_b, flags, failed);
}

/* ---- The walk ------------------------------------------------------------------ */

bool amark_node_equal(const amark_node_t *a, const amark_node_t *b, uint32_t flags)
{
    const amark_node_t *at_a = a;
    const amark_node_t *at_b = b;
    bool failed = false;
    bool descend = true;

    if (a == NULL || b == NULL)
    {
        return false;
    }
    /* The roots compare as nodes, even text nodes with normalization (a run
     * of one text node compares as its literal). */
    if (!node_equal(a, b, flags, &failed))
    {
        return false;
    }
    /* at_a and at_b are the last nodes of the units compared last: descend
     * into their children, or else move to the units after them. */
    for (;;)
    {
        const amark_node_t *first_a;
        const amark_node_t *first_b;
        const amark_node_t *last_a = NULL;
        const amark_node_t *last_b = NULL;

        if (descend)
        {
            first_a = unit_at(at_a->first_child, flags, &last_a, &failed);
            first_b = failed ? NULL : unit_at(at_b->first_child, flags, &last_b, &failed);
        }
        else
        {
            if (at_a == a)
            {
                return true;
            }
            first_a = unit_at(at_a->next, flags, &last_a, &failed);
            first_b = failed ? NULL : unit_at(at_b->next, flags, &last_b, &failed);
        }
        if (failed)
        {
            return false;
        }
        if (first_a == NULL && first_b == NULL)
        {
            if (!descend)
            {
                /* No more siblings: back to the parents. */
                at_a = at_a->parent;
                at_b = at_b->parent;
            }
            descend = false;
            continue;
        }
        if (first_a == NULL || first_b == NULL ||
            !unit_equal(first_a, last_a, first_b, last_b, flags, &failed))
        {
            return false;
        }
        at_a = last_a;
        at_b = last_b;
        descend = true;
    }
}
