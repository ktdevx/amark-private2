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
 *      link rule, the cache invariant, the storage of the facets, the
 *      layout (the line range and the layout records), the content of a leaf
 *      and the content tokens.
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

#include <amark/utf8.h>

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

/* Whether the stored string of a facet (desc) with a value is well formed: an
 * owned text terminated by NUL at its length, of valid UTF-8 without NUL, and
 * within the flags that the setters and the records enforce (no line break,
 * not empty, normalized to LF and ending with LF for LINES). */
static bool stored_string_ok(const amark_facet_desc_t *desc, const amark_tree_facet_t *slot)
{
    if (slot->text == NULL || slot->text[slot->length] != '\0' ||
        memchr(slot->text, '\0', slot->length) != NULL ||
        !amark_utf8_validate(slot->text, slot->length, NULL))
    {
        return false;
    }
    if ((desc->flags & AMARK_FACET_NONEMPTY) != 0 && slot->length == 0)
    {
        return false;
    }
    if ((desc->flags & (AMARK_FACET_NO_LINE_BREAK | AMARK_FACET_LINES)) != 0 &&
        memchr(slot->text, '\r', slot->length) != NULL)
    {
        return false;
    }
    if ((desc->flags & AMARK_FACET_NO_LINE_BREAK) != 0 &&
        memchr(slot->text, '\n', slot->length) != NULL)
    {
        return false;
    }
    return (desc->flags & AMARK_FACET_LINES) == 0 || slot->length == 0 ||
           slot->text[slot->length - 1] == '\n';
}

/* Whether number is a value of an INT, ENUM or BOOL facet (desc). */
static bool stored_number_ok(const amark_facet_desc_t *desc, int64_t number)
{
    switch (desc->kind)
    {
    case AMARK_FACET_INT:
        return number >= desc->int_min && number <= desc->int_max;
    case AMARK_FACET_ENUM:
        return number >= 1 && (uint64_t)number <= (uint64_t)desc->enum_count;
    case AMARK_FACET_BOOL:
        return number == 0 || number == 1;
    default:
        return false;
    }
}

/* The storage of the facets (Facets): one slot per facet of the type, each in
 * a known state, holding a value of its kind only when it has one, and a cache
 * length only with a cache (terminated by NUL). Linear in the stored bytes. */
static bool check_facets(check_context_t *context, const amark_node_t *node)
{
    size_t count = node->desc->facet_count;
    size_t i;

    if ((count == 0) != (node->facets == NULL))
    {
        return fail(context, AMARK_TREE_VIOLATION_FACETS, node,
                    "the facet array does not match the facets of the type");
    }
    for (i = 0; i < count; i++)
    {
        const amark_facet_desc_t *desc = &node->desc->facets[i];
        const amark_tree_facet_t *slot = &node->facets[i];
        bool has_value = slot->state != AMARK_TREE_FACET_NONE;

        if (slot->state != AMARK_TREE_FACET_NONE && slot->state != AMARK_TREE_FACET_SET &&
            slot->state != AMARK_TREE_FACET_RECORDED)
        {
            return fail(context, AMARK_TREE_VIOLATION_FACETS, node,
                        "a facet has an unknown state");
        }
        if (desc->kind == AMARK_FACET_STRING)
        {
            if (slot->number != 0 ||
                (has_value ? !stored_string_ok(desc, slot)
                           : slot->text != NULL || slot->length != 0))
            {
                return fail(context, AMARK_TREE_VIOLATION_FACETS, node,
                            "a string facet holds a malformed value");
            }
        }
        else if (slot->text != NULL || slot->length != 0 ||
                 (has_value ? !stored_number_ok(desc, slot->number) : slot->number != 0))
        {
            return fail(context, AMARK_TREE_VIOLATION_FACETS, node,
                        "a number facet holds a value outside its range");
        }
        if (slot->cache == NULL ? slot->cache_length != 0
                                : desc->kind != AMARK_FACET_STRING ||
                                      slot->cache[slot->cache_length] != '\0')
        {
            return fail(context, AMARK_TREE_VIOLATION_FACETS, node,
                        "a facet has a malformed cache");
        }
    }
    return true;
}

/* Whether line is in the line range of node. */
static bool line_in_range(const amark_node_t *node, amark_line_id_t line)
{
    return (node->line_count > 0) && (line >= node->line_first) &&
           ((line - node->line_first) < node->line_count);
}

/* The layout of a node (Layout): its line range is in the line table, its
 * records are on the lines of its range, within their widths, in order, and
 * allowed by its type; its dropped marks name facets it has. Linear in the
 * records of the node. Runs before check_lines, so it only relies on the line
 * count and the widths of the lines. */
static bool check_layout(check_context_t *context, const amark_node_t *node)
{
    const amark_document_t *document = context->document;
    const amark_tree_records_t *records = node->records;
    size_t i;

    if ((node->line_count > 0) &&
        ((node->desc->level != AMARK_NODE_LEVEL_BLOCK) ||
         (node->line_first >= document->line_count) ||
         (node->line_count > (document->line_count - node->line_first))))
    {
        return fail(context, AMARK_TREE_VIOLATION_LAYOUT, node,
                    "a line range is outside the line table or on an inline node");
    }
    if ((node->desc->facet_count < 31u) &&
        ((node->dropped >> (node->desc->facet_count + 1u)) != 0))
    {
        return fail(context, AMARK_TREE_VIOLATION_LAYOUT, node,
                    "a dropped mark names a facet the type does not have");
    }
    if (records == NULL)
    {
        return true;
    }
    if ((records->margin_count > records->margin_capacity) ||
        (records->blank_line_count > records->blank_line_capacity) ||
        (records->token_count > records->token_capacity) ||
        ((records->margin_capacity > 0) && (records->margins == NULL)) ||
        ((records->blank_line_capacity > 0) && (records->blank_lines == NULL)) ||
        ((records->token_capacity > 0) && (records->tokens == NULL)))
    {
        return fail(context, AMARK_TREE_VIOLATION_LAYOUT, node,
                    "a record array exceeds its capacity");
    }
    if ((records->margin_count > 0) &&
        ((node->desc->level != AMARK_NODE_LEVEL_BLOCK) ||
         ((node->desc->flags & AMARK_NODE_TYPE_MARGINS) == 0)))
    {
        return fail(context, AMARK_TREE_VIOLATION_LAYOUT, node,
                    "a node that is not a container has margins");
    }
    if ((records->blank_line_count > 0) &&
        ((node->desc->level != AMARK_NODE_LEVEL_BLOCK) ||
         ((node->desc->accepts & (AMARK_CONTENT_BLOCK | AMARK_CONTENT_LIST_ITEM)) == 0)))
    {
        return fail(context, AMARK_TREE_VIOLATION_LAYOUT, node,
                    "a node that cannot have block children has blank lines");
    }
    if ((records->token_count > 0) && (node->desc->level != AMARK_NODE_LEVEL_BLOCK))
    {
        return fail(context, AMARK_TREE_VIOLATION_LAYOUT, node,
                    "an inline node has layout tokens");
    }
    for (i = 0; i < records->margin_count; i++)
    {
        const amark_tree_margin_t *margin = &records->margins[i];

        if (!line_in_range(node, margin->line) ||
            ((i > 0) && (margin->line <= records->margins[i - 1].line)))
        {
            return fail(context, AMARK_TREE_VIOLATION_LAYOUT, node,
                        "a margin is outside the line range or out of order");
        }
        if ((margin->start > margin->end) ||
            (margin->end > document->lines[margin->line].width) ||
            ((margin->state != AMARK_MARGIN_PRESENT) && (margin->state != AMARK_MARGIN_LAZY)) ||
            ((margin->flags & ~(unsigned)AMARK_MARGIN_FIRST) != 0) ||
            ((margin->state == AMARK_MARGIN_LAZY) && (margin->start != margin->end)))
        {
            return fail(context, AMARK_TREE_VIOLATION_LAYOUT, node, "a margin is malformed");
        }
    }
    for (i = 0; i < records->blank_line_count; i++)
    {
        const amark_tree_blank_line_t *blank = &records->blank_lines[i];

        if (!line_in_range(node, blank->line) ||
            ((i > 0) && (blank->line <= records->blank_lines[i - 1].line)))
        {
            return fail(context, AMARK_TREE_VIOLATION_LAYOUT, node,
                        "a blank line is outside the line range or out of order");
        }
        if ((blank->start > blank->end) || (blank->end != document->lines[blank->line].width))
        {
            return fail(context, AMARK_TREE_VIOLATION_LAYOUT, node,
                        "a blank line does not end at the end of its line");
        }
    }
    for (i = 0; i < records->token_count; i++)
    {
        const amark_tree_layout_token_t *token = &records->tokens[i];
        const amark_tree_layout_token_t *last = (i > 0) ? &records->tokens[i - 1] : NULL;

        if (!line_in_range(node, token->line) ||
            ((last != NULL) && ((token->line < last->line) ||
                                ((token->line == last->line) && (token->start < last->end)))))
        {
            return fail(context, AMARK_TREE_VIOLATION_LAYOUT, node,
                        "a layout token is outside the line range, out of order or overlapping");
        }
        if ((token->start >= token->end) || (token->end > document->lines[token->line].width) ||
            (token->kind < AMARK_SPELL_TEXT) || (token->kind > AMARK_SPELL_REPLACED) ||
            (token->facet > node->desc->facet_count))
        {
            return fail(context, AMARK_TREE_VIOLATION_LAYOUT, node,
                        "a layout token is malformed");
        }
    }
    return true;
}

/* The content of a node (Content), as far as one node can tell: only a block
 * with content has one; the object has a known state, its arrays fit their
 * capacities, its bytes are terminated and shorter than 4 GiB; free content has
 * no pieces, and the pieces of laid content are in order on the lines of the
 * range of the node, within their widths. The bytes and the counts are checked
 * by check_contents. Runs after check_layout. */
static bool check_content(check_context_t *context, const amark_node_t *node)
{
    const amark_document_t *document = context->document;
    const amark_tree_content_t *content = node->content;
    size_t i;

    if (content == NULL)
    {
        return true;
    }
    if ((node->desc->level != AMARK_NODE_LEVEL_BLOCK) ||
        ((node->desc->flags & AMARK_NODE_TYPE_CONTENT) == 0))
    {
        return fail(context, AMARK_TREE_VIOLATION_CONTENT, node,
                    "a node that is not a block with content has content");
    }
    if (((content->state != AMARK_CONTENT_LAID) && (content->state != AMARK_CONTENT_FREE)) ||
        (content->length > AMARK_TREE_MAX_CONTENT) ||
        ((content->capacity > 0) ? ((content->bytes == NULL) ||
                                    (content->length >= content->capacity) ||
                                    (content->bytes[content->length] != '\0'))
                                 : (content->length != 0)) ||
        (content->piece_count > content->piece_capacity) ||
        ((content->piece_capacity > 0) && (content->pieces == NULL)) ||
        ((content->state == AMARK_CONTENT_FREE) && (content->piece_count > 0)))
    {
        return fail(context, AMARK_TREE_VIOLATION_CONTENT, node, "a content object is malformed");
    }
    for (i = 0; i < content->piece_count; i++)
    {
        const amark_tree_piece_t *piece = &content->pieces[i];
        const amark_tree_piece_t *last = (i > 0) ? &content->pieces[i - 1] : NULL;

        if (!line_in_range(node, piece->line) || (piece->start > piece->end) ||
            (piece->end > document->lines[piece->line].width) ||
            ((last != NULL) &&
             ((piece->line < last->line) ||
              ((piece->line == last->line) &&
               ((piece->start < last->end) || (piece->start == piece->end) ||
                (last->start == last->end))))))
        {
            return fail(context, AMARK_TREE_VIOLATION_CONTENT, node,
                        "a content piece is outside the line range, beyond its line or out of "
                        "order");
        }
    }
    return true;
}

/* The content tokens of a node (Content), as far as one node can tell: only an
 * inline node or a block with content has them; they have a base exactly when
 * there are some; the array fits its capacity; each token is nonempty, within
 * the bytes of the base, after the end of the previous one, of a known kind and
 * of a facet of the type. The base counts are checked by check_contents. */
static bool check_content_tokens(check_context_t *context, const amark_node_t *node)
{
    const amark_tree_content_tokens_t *tokens = node->content_tokens;
    size_t i;

    if (tokens == NULL)
    {
        return true;
    }
    if ((node->desc->level != AMARK_NODE_LEVEL_INLINE) &&
        ((node->desc->flags & AMARK_NODE_TYPE_CONTENT) == 0))
    {
        return fail(context, AMARK_TREE_VIOLATION_CONTENT, node,
                    "a node that is neither inline nor a block with content has content "
                    "tokens");
    }
    if ((tokens->count > tokens->capacity) || ((tokens->capacity > 0) && (tokens->items == NULL)) ||
        ((tokens->count > 0) != (tokens->base != NULL)))
    {
        return fail(context, AMARK_TREE_VIOLATION_CONTENT, node,
                    "a content token array is malformed or its base does not match its tokens");
    }
    for (i = 0; i < tokens->count; i++)
    {
        const amark_tree_content_token_t *token = &tokens->items[i];

        if ((token->start >= token->end) || (token->end > tokens->base->length) ||
            ((i > 0) && (token->start < tokens->items[i - 1].end)) ||
            (token->kind < AMARK_SPELL_TEXT) || (token->kind > AMARK_SPELL_TAB_REMAINDER) ||
            (token->facet > node->desc->facet_count))
        {
            return fail(context, AMARK_TREE_VIOLATION_CONTENT, node,
                        "a content token is empty, beyond its base, out of order, overlapping "
                        "or malformed");
        }
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
    check_facets,
    check_layout,
    check_content,
    check_content_tokens,
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

/* Whether the bytes of laid content are its pieces joined: a LF before the
 * first piece of each line after the first, and for each piece the bytes of the
 * columns of its line (a tab whole as itself, a tab cut by a boundary as the
 * spaces of its columns in the piece), at the positions the piece records; and
 * whether its line count is the number of lines with a piece. Computed from the
 * bytes of the lines, without the tab index. */
static bool laid_content_ok(const amark_document_t *document, const amark_tree_content_t *content)
{
    size_t position = 0;
    size_t lines = 0;
    size_t i;

    for (i = 0; i < content->piece_count; i++)
    {
        const amark_tree_piece_t *piece = &content->pieces[i];
        const amark_tree_line_t *line = &document->lines[piece->line];
        const char *data = document->sources[line->source].data;
        const char *text = (data != NULL) ? data + line->offset : "";
        uint64_t column = 0;
        size_t b;

        if ((i == 0) || (piece->line != content->pieces[i - 1].line))
        {
            if (i > 0)
            {
                if ((position >= content->length) || (content->bytes[position] != '\n'))
                {
                    return false;
                }
                position++;
            }
            lines++;
        }
        if (piece->content_start != position)
        {
            return false;
        }
        for (b = 0; (b < line->length) && (column < piece->end); b++)
        {
            uint64_t width = (text[b] == '\t') ? 4u - (column % 4u) : 1u;
            uint64_t low = (column > piece->start) ? column : piece->start;
            uint64_t high = ((column + width) < piece->end) ? (column + width) : piece->end;

            if (low < high)
            {
                if ((low == column) && (high == column + width))
                {
                    if ((position >= content->length) || (content->bytes[position] != text[b]))
                    {
                        return false;
                    }
                    position++;
                }
                else
                {
                    for (; low < high; low++)
                    {
                        if ((position >= content->length) || (content->bytes[position] != ' '))
                        {
                            return false;
                        }
                        position++;
                    }
                }
            }
            column += width;
        }
        if (piece->content_length != position - piece->content_start)
        {
            return false;
        }
    }
    return (position == content->length) && (lines == content->line_count);
}

/* Whether free content holds no CR and its line count is 0 when it is empty,
 * else its LFs + 1. */
static bool free_content_ok(const amark_tree_content_t *content)
{
    size_t lines = 0;
    size_t i;

    for (i = 0; i < content->length; i++)
    {
        if (content->bytes[i] == '\r')
        {
            return false;
        }
        lines += (content->bytes[i] == '\n') ? 1u : 0u;
    }
    return content->line_count == ((content->length == 0) ? 0 : lines + 1);
}

/* Whether a content object is well formed: a known state, arrays within their
 * capacities, bytes terminated and shorter than 4 GiB, no pieces on free
 * content, and pieces in order on lines of the line table within their widths.
 * (check_content checks the same for the content of a leaf, with its line
 * range; an object reached only as a base of tokens is checked here.) */
static bool content_object_ok(const amark_document_t *document,
                              const amark_tree_content_t *content)
{
    size_t i;

    if (((content->state != AMARK_CONTENT_LAID) && (content->state != AMARK_CONTENT_FREE)) ||
        (content->length > AMARK_TREE_MAX_CONTENT) ||
        ((content->capacity > 0) ? ((content->bytes == NULL) ||
                                    (content->length >= content->capacity) ||
                                    (content->bytes[content->length] != '\0'))
                                 : (content->length != 0)) ||
        (content->piece_count > content->piece_capacity) ||
        ((content->piece_capacity > 0) && (content->pieces == NULL)) ||
        ((content->state == AMARK_CONTENT_FREE) && (content->piece_count > 0)))
    {
        return false;
    }
    for (i = 0; i < content->piece_count; i++)
    {
        const amark_tree_piece_t *piece = &content->pieces[i];
        const amark_tree_piece_t *last = (i > 0) ? &content->pieces[i - 1] : NULL;

        if ((piece->line >= document->line_count) || (piece->start > piece->end) ||
            (piece->end > document->lines[piece->line].width) ||
            ((last != NULL) &&
             ((piece->line < last->line) ||
              ((piece->line == last->line) &&
               ((piece->start < last->end) || (last->start == last->end))))))
        {
            return false;
        }
    }
    return true;
}

/* The content object a node refers to as an owner (which = 0) or as the base of
 * its tokens (which = 1), or NULL. */
static amark_tree_content_t *referred(const amark_node_t *node, int which)
{
    if (which == 0)
    {
        return node->content;
    }
    return (node->content_tokens != NULL) ? node->content_tokens->base : NULL;
}

/* The content objects (Content): the owner count of each object is the number
 * of leaves whose content it is, its base count the number of nodes whose
 * content tokens are on it, and its bytes are those of its state. The objects
 * are reached from the nodes, as contents and as bases. Each object is counted
 * with its check marks: the first pass over the ring resets the counters of
 * each object reached (mark in_tree), the second counts the references, the
 * third checks each object once (mark in_ring). Linear in the nodes and the
 * bytes of the contents. Runs after the node checks and check_lines. */
static bool check_contents(check_context_t *context)
{
    const amark_document_t *document = context->document;
    amark_node_t *root = document->root;
    amark_node_t *node = root;
    int which;

    do
    {
        for (which = 0; which < 2; which++)
        {
            amark_tree_content_t *content = referred(node, which);

            if ((content != NULL) && (content->check_mark != context->in_tree))
            {
                content->check_mark = context->in_tree;
                content->check_owners = 0;
                content->check_bases = 0;
            }
        }
        node = node->ring_next;
    } while (node != root);
    do
    {
        if (node->content != NULL)
        {
            node->content->check_owners++;
        }
        if ((node->content_tokens != NULL) && (node->content_tokens->base != NULL))
        {
            node->content_tokens->base->check_bases++;
        }
        node = node->ring_next;
    } while (node != root);
    do
    {
        for (which = 0; which < 2; which++)
        {
            amark_tree_content_t *content = referred(node, which);

            if ((content == NULL) || (content->check_mark != context->in_tree))
            {
                continue;
            }
            content->check_mark = context->in_ring;
            if ((content->owners != content->check_owners) ||
                (content->bases != content->check_bases))
            {
                return fail(context, AMARK_TREE_VIOLATION_CONTENT, node,
                            "the owner or base count of a content object differs from its "
                            "references");
            }
            if (!content_object_ok(document, content))
            {
                return fail(context, AMARK_TREE_VIOLATION_CONTENT, node,
                            "a content object is malformed");
            }
            if ((content->state == AMARK_CONTENT_LAID) ? !laid_content_ok(document, content)
                                                       : !free_content_ok(content))
            {
                return fail(context, AMARK_TREE_VIOLATION_CONTENT, node,
                            "the bytes or the line count of a content object do not match its "
                            "state");
            }
        }
        node = node->ring_next;
    } while (node != root);
    return true;
}

/* The checks of the document, in order. Add the invariants that span the
 * document (counts of references, the label index) after check_trees. */
static const document_check_t document_checks[] = {
    check_root,
    check_ring,
    check_trees,
    check_lines,
    check_contents,
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
    case AMARK_TREE_VIOLATION_FACETS:
        return "facets";
    case AMARK_TREE_VIOLATION_LAYOUT:
        return "layout";
    case AMARK_TREE_VIOLATION_CONTENT:
        return "content";
    default:
        return "unknown";
    }
}
