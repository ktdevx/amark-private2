/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The check of the internal invariants of a document, for the tests and the
 * fuzz targets (amark_tree_check).
 *
 * The checks run in order and stop at the first violation:
 *   1. the document checks (document_checks[]): the document node, the ring of
 *      every node, the walk of every tree, then the sources and the line
 *      table;
 *   2. the walk visits each node of each tree once and runs the node checks
 *      (node_checks[]) on it: the relations, the containment rules, the link in
 *      link rule and the cache invariant.
 * A component that adds records or caches adds its invariant as a node check
 * (records of one node: facets, margins, tokens, content tokens) or as a
 * document check after the walk (whole-document counts: the owners of the
 * content objects, the label index). A node check runs after the relations of
 * the node are known to be symmetric, and a document check after the walk can
 * rely on every node being in the ring and in exactly one tree.
 *
 * The marks: each run takes two new values of document->check_epoch. The walk
 * of the ring marks each node with the first (in the ring), the walk of the
 * trees with the second (reached in a tree), so that a node of a tree that is
 * not in the ring, or that is reached twice, is seen without any allocation.
 */
#include "tree/tree.h"

#include <string.h>

/* The state of one run. */
typedef struct check_context
{
    amark_document_t *document;
    /* The mark of the nodes of the ring, and of the nodes reached in a tree. */
    uint64_t in_ring;
    uint64_t in_tree;
    /* The nodes reached in the trees. */
    size_t reached;
    /* The walk: the parent and the previous sibling the current node must link
     * to, and the links among the ancestors of the current node. */
    const amark_node_t *parent;
    const amark_node_t *prev;
    size_t links_above;
    amark_tree_check_report_t *report;
} check_context_t;

typedef bool (*document_check_t)(check_context_t *context);
typedef bool (*node_check_t)(check_context_t *context, const amark_node_t *node);

/* Records the first violation; returns false so that the checks stop. */
static bool fail(check_context_t *context, amark_tree_violation_t kind,
                 const amark_node_t *node, const char *message)
{
    context->report->kind = kind;
    context->report->node = node;
    context->report->message = message;
    return false;
}

/* ---- Node checks ------------------------------------------------------------- */

/* The links of node agree with the place where the walk reached it, and with
 * its children and its next sibling. The walk follows them only after this. */
static bool check_relations(check_context_t *context, const amark_node_t *node)
{
    if (node->parent != context->parent)
    {
        return fail(context, AMARK_TREE_VIOLATION_RELATION, node,
                    "the parent link differs from the parent that lists the node");
    }
    if (node->prev != context->prev)
    {
        return fail(context, AMARK_TREE_VIOLATION_RELATION, node,
                    "the previous sibling link differs from the sibling before the node");
    }
    if (node->parent == NULL)
    {
        if (node->next != NULL)
        {
            return fail(context, AMARK_TREE_VIOLATION_RELATION, node,
                        "a node without a parent has a sibling");
        }
    }
    else if (node->next == NULL && node->parent->last_child != node)
    {
        return fail(context, AMARK_TREE_VIOLATION_RELATION, node,
                    "the last child link of the parent misses its last child");
    }
    if ((node->first_child == NULL) != (node->last_child == NULL))
    {
        return fail(context, AMARK_TREE_VIOLATION_RELATION, node,
                    "only one of the first and the last child links is set");
    }
    return true;
}

/* The parent accepts the node (the descriptors and the can_contain hook). */
static bool check_containment(check_context_t *context, const amark_node_t *node)
{
    const amark_node_t *parent = node->parent;

    if (parent != NULL && !amark_registry_can_contain(context->document->set, parent,
                                                      parent->type, node->type))
    {
        return fail(context, AMARK_TREE_VIOLATION_CONTAINMENT, node,
                    "the parent does not accept the node");
    }
    return true;
}

/* No link among the descendants of a link (the can_contain hook only sees the
 * direct children). */
static bool check_link_in_link(check_context_t *context, const amark_node_t *node)
{
    if (node->type == AMARK_NODE_LINK && context->links_above > 0)
    {
        return fail(context, AMARK_TREE_VIOLATION_LINK_IN_LINK, node,
                    "a link among the descendants of a link");
    }
    return true;
}

/* A node whose cache is valid has only children whose cache is valid: checked
 * from the child, and reported at the parent. */
static bool check_cache(check_context_t *context, const amark_node_t *node)
{
    const amark_node_t *parent = node->parent;

    if (parent != NULL && (parent->cache_flags & AMARK_TREE_CACHE_VALID) != 0 &&
        (node->cache_flags & AMARK_TREE_CACHE_VALID) == 0)
    {
        return fail(context, AMARK_TREE_VIOLATION_CACHE, parent,
                    "a node with a valid cache has a child with an invalid cache");
    }
    return true;
}

/* The checks of one node, in order. Add the invariants of the records and the
 * caches of a node here. */
static const node_check_t node_checks[] = {
    check_relations,
    check_containment,
    check_link_in_link,
    check_cache,
};

/* ---- The walk of the trees ----------------------------------------------------- */

/* Visits node, reached in a tree from context->parent after context->prev:
 * the node is of the document, in the ring and reached once; then the node
 * checks. */
static bool visit(check_context_t *context, amark_node_t *node)
{
    size_t i;

    if (node->document != context->document)
    {
        return fail(context, AMARK_TREE_VIOLATION_RING, node,
                    "a node of another document is in a tree");
    }
    if (node->check_mark == context->in_tree)
    {
        return fail(context, AMARK_TREE_VIOLATION_RELATION, node,
                    "a node is reached twice in the trees");
    }
    if (node->check_mark != context->in_ring)
    {
        return fail(context, AMARK_TREE_VIOLATION_RING, node,
                    "a node of a tree is not in the ring");
    }
    node->check_mark = context->in_tree;
    context->reached++;
    for (i = 0; i < sizeof(node_checks) / sizeof(node_checks[0]); i++)
    {
        if (!node_checks[i](context, node))
        {
            return false;
        }
    }
    return true;
}

/* Walks the tree of top (a node without a parent) in document order, without
 * recursion or stack. Each node is visited before its links are followed, so
 * the walk only follows links already checked. */
static bool walk_tree(check_context_t *context, amark_node_t *top)
{
    amark_node_t *current = top;

    context->parent = NULL;
    context->prev = NULL;
    context->links_above = 0;
    if (!visit(context, current))
    {
        return false;
    }
    for (;;)
    {
        if (current->first_child != NULL)
        {
            if (current->type == AMARK_NODE_LINK)
            {
                context->links_above++;
            }
            context->parent = current;
            context->prev = NULL;
            current = current->first_child;
        }
        else
        {
            /* Up to the first ancestor with a next sibling. */
            while (current != top && current->next == NULL)
            {
                current = current->parent;
                if (current->type == AMARK_NODE_LINK)
                {
                    context->links_above--;
                }
            }
            if (current == top)
            {
                return true;
            }
            context->parent = current->parent;
            context->prev = current;
            current = current->next;
        }
        if (!visit(context, current))
        {
            return false;
        }
    }
}

/* ---- Document checks ------------------------------------------------------------ */

/* The document node: of the document type, of this document, without a parent
 * or siblings. */
static bool check_root(check_context_t *context)
{
    const amark_node_t *root = context->document->root;

    if (root == NULL || root->document != context->document)
    {
        return fail(context, AMARK_TREE_VIOLATION_RING, root,
                    "the document node is missing or of another document");
    }
    if (root->type != AMARK_NODE_DOCUMENT)
    {
        return fail(context, AMARK_TREE_VIOLATION_CONTAINMENT, root,
                    "the document node is not of the document type");
    }
    if (root->parent != NULL || root->prev != NULL || root->next != NULL)
    {
        return fail(context, AMARK_TREE_VIOLATION_RELATION, root,
                    "the document node has a parent or a sibling");
    }
    return true;
}

/* The ring from the document node: symmetric, of this document, each node
 * once, and back to the document node after exactly node_count nodes. */
static bool check_ring(check_context_t *context)
{
    amark_document_t *document = context->document;
    amark_node_t *node = document->root;
    size_t count = 0;

    do
    {
        if (node->document != document)
        {
            return fail(context, AMARK_TREE_VIOLATION_RING, node,
                        "a node of another document is in the ring");
        }
        if (node->check_mark == context->in_ring)
        {
            return fail(context, AMARK_TREE_VIOLATION_RING, node,
                        "a node is twice in the ring");
        }
        if (node->ring_next->ring_prev != node)
        {
            return fail(context, AMARK_TREE_VIOLATION_RING, node->ring_next,
                        "the ring links are not symmetric");
        }
        node->check_mark = context->in_ring;
        count++;
        node = node->ring_next;
    } while (node != document->root && count <= document->node_count);
    if (node != document->root || count != document->node_count)
    {
        return fail(context, AMARK_TREE_VIOLATION_RING, document->root,
                    "the nodes of the ring differ from the node count");
    }
    return true;
}

/* Every tree, from each node of the ring without a parent (the document node
 * first); every node of the ring must be reached. */
static bool check_trees(check_context_t *context)
{
    amark_document_t *document = context->document;
    amark_node_t *node = document->root;

    context->reached = 0;
    do
    {
        if (node->parent == NULL && node->check_mark == context->in_ring &&
            !walk_tree(context, node))
        {
            return false;
        }
        node = node->ring_next;
    } while (node != document->root);
    if (context->reached == document->node_count)
    {
        return true;
    }
    /* A node that no top reaches: its parent does not list it, or it is in a
     * cycle of parents. Report the first one in the ring. */
    do
    {
        if (node->check_mark != context->in_tree)
        {
            break;
        }
        node = node->ring_next;
    } while (node != document->root);
    return fail(context, AMARK_TREE_VIOLATION_RELATION, node,
                "a node is not reachable from the top of its tree");
}

/* The sources and the line table: the lines of each source are, in order of
 * their identifiers, the division of its bytes from the first byte after the
 * BOM (contiguous ranges without CR or LF, followed by the bytes of their line
 * ending, with their number in the source and their width), up to its
 * unfinished line; every line of the table belongs to exactly one source; and
 * the default line ending is the one of the first line that has one. Linear in
 * the bytes of the sources. */
static bool check_lines(check_context_t *context)
{
    const amark_document_t *document = context->document;
    uint8_t default_eol = 0;
    size_t listed = 0;
    size_t s;
    size_t i;

    for (s = 0; s < document->source_count; s++)
    {
        const amark_tree_source_t *source = &document->sources[s];
        size_t offset = source->has_bom ? 3 : 0;
        size_t k;

        if (source->length > source->capacity || source->line_count > source->line_capacity ||
            (!source->bom_known && (source->has_bom || source->line_count > 0)) ||
            (source->has_bom && (source->length < 3 ||
                                 memcmp(source->data, "\xEF\xBB\xBF", 3) != 0)) ||
            (source->given_lines && (!source->finished || !source->bom_known || source->has_bom)))
        {
            return fail(context, AMARK_TREE_VIOLATION_LINES, NULL,
                        "a source has an inconsistent size or BOM");
        }
        for (k = 0; k < source->line_count; k++)
        {
            amark_line_id_t id = source->lines[k];
            const amark_tree_line_t *line;
            uint64_t width = 0;
            size_t eol_length;
            uint32_t tabs = 0;
            size_t b;

            if (id >= document->line_count || (k > 0 && id <= source->lines[k - 1]))
            {
                return fail(context, AMARK_TREE_VIOLATION_LINES, NULL,
                            "the lines of a source are not in the order of the table");
            }
            line = &document->lines[id];
            if (line->source != s || line->number != k + 1)
            {
                return fail(context, AMARK_TREE_VIOLATION_LINES, NULL,
                            "a line has a wrong source or number");
            }
            eol_length = amark_line_table_eol_length(line->eol);
            if (line->eol < AMARK_EOL_NONE || line->eol > AMARK_EOL_CR ||
                line->offset != offset || line->length > source->length - offset ||
                eol_length > source->length - offset - line->length)
            {
                return fail(context, AMARK_TREE_VIOLATION_LINES, NULL,
                            "a line does not follow the previous one in its source");
            }
            for (b = 0; b < line->length; b++)
            {
                char c = source->data[offset + b];

                if (c == '\n' || c == '\r')
                {
                    return fail(context, AMARK_TREE_VIOLATION_LINES, NULL,
                                "a line holds a line ending");
                }
                if (c == '\t')
                {
                    /* The index, when built, lists every tab in order. */
                    if (line->tab_index != NULL &&
                        (tabs >= line->tab_count || line->tab_index[tabs].byte != b ||
                         line->tab_index[tabs].column != width))
                    {
                        return fail(context, AMARK_TREE_VIOLATION_LINES, NULL,
                                    "the index of the tabs of a line does not list its tabs");
                    }
                    tabs++;
                }
                width += c == '\t' ? 4 - width % 4 : 1;
            }
            if (line->has_tab != (tabs > 0) ||
                (line->tab_index != NULL ? line->tab_count != tabs : line->tab_count != 0))
            {
                return fail(context, AMARK_TREE_VIOLATION_LINES, NULL,
                            "the index of the tabs of a line does not list its tabs");
            }
            offset += line->length;
            /* Given lines (amark_line_table_append_lines) are not the
             * division of their bytes: a line without a line ending may come
             * before another, and a CR line before a line that starts with
             * LF. */
            if (width != line->width ||
                (line->eol == AMARK_EOL_NONE && !source->given_lines &&
                 (!source->finished || k + 1 != source->line_count ||
                  offset != source->length)) ||
                (line->eol == AMARK_EOL_LF && source->data[offset] != '\n') ||
                (line->eol != AMARK_EOL_NONE && line->eol != AMARK_EOL_LF &&
                 source->data[offset] != '\r') ||
                (line->eol == AMARK_EOL_CRLF && source->data[offset + 1] != '\n') ||
                (line->eol == AMARK_EOL_CR && !source->given_lines &&
                 offset + 1 < source->length && source->data[offset + 1] == '\n'))
            {
                return fail(context, AMARK_TREE_VIOLATION_LINES, NULL,
                            "a line has a wrong width or line ending");
            }
            offset += eol_length;
        }
        /* The unfinished line starts after the last completed line; at the
         * end of the input, nothing is left. */
        if (source->bom_known &&
            (source->line_start != offset || source->scanned < offset ||
             source->scanned > source->length ||
             (source->finished && offset != source->length)))
        {
            return fail(context, AMARK_TREE_VIOLATION_LINES, NULL,
                        "the unfinished line does not follow the completed lines");
        }
        listed += source->line_count;
    }
    if (listed != document->line_count)
    {
        return fail(context, AMARK_TREE_VIOLATION_LINES, NULL,
                    "a line of the table belongs to no source");
    }
    for (i = 0; i < document->line_count && default_eol == 0; i++)
    {
        if (document->lines[i].eol != AMARK_EOL_NONE)
        {
            default_eol = document->lines[i].eol;
        }
    }
    if (default_eol != document->default_eol)
    {
        return fail(context, AMARK_TREE_VIOLATION_LINES, NULL,
                    "the default line ending is not the one of the first line");
    }
    return true;
}

/* The checks of the document, in order. Add the invariants that span the
 * document (counts of references, the label index) after check_trees. */
static const document_check_t document_checks[] = {
    check_root,
    check_ring,
    check_trees,
    check_lines,
};

/* ---- Entry points ---------------------------------------------------------------- */

amark_tree_violation_t amark_tree_check(amark_document_t *document,
                                        amark_tree_check_report_t *report)
{
    amark_tree_check_report_t local;
    check_context_t context;
    size_t i;

    if (report == NULL)
    {
        report = &local;
    }
    report->kind = AMARK_TREE_VIOLATION_NONE;
    report->node = NULL;
    report->message = NULL;
    document->check_epoch += 2;
    context.document = document;
    context.in_ring = document->check_epoch - 1;
    context.in_tree = document->check_epoch;
    context.reached = 0;
    context.parent = NULL;
    context.prev = NULL;
    context.links_above = 0;
    context.report = report;
    for (i = 0; i < sizeof(document_checks) / sizeof(document_checks[0]); i++)
    {
        if (!document_checks[i](&context))
        {
            break;
        }
    }
    return report->kind;
}

const char *amark_tree_violation_name(amark_tree_violation_t kind)
{
    switch (kind)
    {
    case AMARK_TREE_VIOLATION_NONE:
        return "none";
    case AMARK_TREE_VIOLATION_RING:
        return "ring";
    case AMARK_TREE_VIOLATION_RELATION:
        return "relation";
    case AMARK_TREE_VIOLATION_CONTAINMENT:
        return "containment";
    case AMARK_TREE_VIOLATION_LINK_IN_LINK:
        return "link_in_link";
    case AMARK_TREE_VIOLATION_CACHE:
        return "cache";
    case AMARK_TREE_VIOLATION_LINES:
        return "lines";
    default:
        return "unknown";
    }
}
