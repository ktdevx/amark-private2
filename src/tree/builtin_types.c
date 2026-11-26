/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * BuiltinTypes: the descriptors of the 19 built-in node types, their facets, the
 * label check, the built-in extension "amark.core" and the static built-in
 * syntax set.
 *
 * The built-in types use the same descriptors as the types of extensions (3.9).
 * Their identifiers are their positions in the built-in extension plus 1, which
 * are the AMARK_NODE_* macros of amark/node.h; their facet numbers are the
 * positions in their facet arrays plus 1, which are the macros of amark/value.h.
 */
#include "tree/tree.h"


/* ---- The label check ------------------------------------------------------- */

/* The longest link label, in characters (CommonMark 0.31.2, 4.7). */
#define LABEL_MAX_CHARACTERS 999u

static bool is_ascii_punctuation(unsigned char c)
{
    return (c >= 0x21 && c <= 0x2F) || (c >= 0x3A && c <= 0x40) || (c >= 0x5B && c <= 0x60) ||
           (c >= 0x7B && c <= 0x7E);
}

/*
 * Checks that a value can be written as a CommonMark link label between
 * brackets: at most 999 characters, at least one that is not a space, a tab or a
 * line ending, no unescaped bracket (a backslash escapes the next ASCII
 * punctuation character, so a final backslash would escape the closing bracket)
 * and no blank line. The value is the raw label (AMARK_FACET_RAW).
 */
static amark_error_t check_label(const amark_node_t *node, amark_facet_t facet,
                                 const amark_value_t *value, void *user_data)
{
    const unsigned char *text;
    size_t length;
    size_t characters = 0;
    bool has_content = false;
    bool after_line_ending = false;
    bool line_blank = false;
    size_t i;

    (void)node;
    (void)facet;
    (void)user_data;
    if (value == NULL || value->text == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    text = (const unsigned char *)value->text;
    length = value->length;
    for (i = 0; i < length; i++)
    {
        unsigned char c = text[i];

        /* Each byte that does not continue a UTF-8 sequence starts a character. */
        if ((c & 0xC0u) != 0x80u)
        {
            characters++;
        }
        if (c == '\n' || c == '\r')
        {
            if (after_line_ending && line_blank)
            {
                return AMARK_ERROR_INVALID_ARGUMENT;
            }
            if (c == '\r' && i + 1 < length && text[i + 1] == '\n')
            {
                i++;
            }
            after_line_ending = true;
            line_blank = true;
            continue;
        }
        if (c == ' ' || c == '\t')
        {
            continue;
        }
        has_content = true;
        line_blank = false;
        if (c == '[' || c == ']')
        {
            return AMARK_ERROR_INVALID_ARGUMENT;
        }
        if (c == '\\')
        {
            if (i + 1 == length)
            {
                return AMARK_ERROR_INVALID_ARGUMENT;
            }
            if (is_ascii_punctuation(text[i + 1]))
            {
                i++;
                characters++;
            }
        }
    }
    if (!has_content || characters > LABEL_MAX_CHARACTERS)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    return AMARK_ERROR_NONE;
}

/* ---- Facets ---------------------------------------------------------------- */

/* The largest INT for a range without an upper bound. */
#define NO_MAX INT64_MAX

#define FACET_STRING(name, role, flags) \
    {sizeof(amark_facet_desc_t), name, role, AMARK_FACET_STRING, flags, 0, 0, NULL, 0, 0, NULL, \
     NULL, NULL, NULL}
#define FACET_INT(name, role, min, max, default_number) \
    {sizeof(amark_facet_desc_t), name, role, AMARK_FACET_INT, 0, min, max, NULL, 0, \
     default_number, NULL, NULL, NULL, NULL}
#define FACET_ENUM(name, role, names, default_number) \
    {sizeof(amark_facet_desc_t), name, role, AMARK_FACET_ENUM, 0, 0, 0, names, \
     sizeof(names) / sizeof(names[0]), default_number, NULL, NULL, NULL, NULL}
#define FACET_BOOL(name, role, flags, default_number) \
    {sizeof(amark_facet_desc_t), name, role, AMARK_FACET_BOOL, flags, 0, 0, NULL, 0, \
     default_number, NULL, NULL, NULL, NULL}

#define V AMARK_FACET_VALUE
#define S AMARK_FACET_STYLE

static const char *const list_type_names[] = {"bullet", "ordered"};
static const char *const list_bullet_names[] = {"-", "*", "+"};
static const char *const list_delimiter_names[] = {".", ")"};
static const char *const code_block_form_names[] = {"indented", "fenced"};
static const char *const fence_char_names[] = {"`", "~"};
static const char *const heading_form_names[] = {"atx", "setext"};
static const char *const thematic_char_names[] = {"*", "-", "_"};
static const char *const title_quote_names[] = {"\"", "'", "("};
static const char *const linebreak_form_names[] = {"spaces", "backslash"};
static const char *const emphasis_delimiter_names[] = {"*", "_"};
static const char *const link_form_names[] = {"inline", "full", "collapsed", "shortcut",
                                              "autolink"};

static const amark_facet_desc_t list_facets[] = {
    FACET_ENUM("type", V, list_type_names, AMARK_LIST_TYPE_BULLET),
    FACET_INT("start", V, 0, 999999999, 1),
    /* Derived from the blank lines; the default is not used. */
    FACET_BOOL("tight", V, AMARK_FACET_OVERRIDABLE, 1),
    FACET_ENUM("bullet", S, list_bullet_names, 0),
    FACET_ENUM("delimiter", S, list_delimiter_names, 0),
};

static const amark_facet_desc_t code_block_facets[] = {
    FACET_STRING("info", V, AMARK_FACET_NO_LINE_BREAK),
    FACET_STRING("literal", V, AMARK_FACET_LINES | AMARK_FACET_CONTENT),
    FACET_ENUM("form", S, code_block_form_names, 0),
    FACET_ENUM("fence_char", S, fence_char_names, 0),
    FACET_INT("fence_length", S, 3, NO_MAX, 0),
};

static const amark_facet_desc_t html_block_facets[] = {
    FACET_STRING("literal", V, AMARK_FACET_LINES | AMARK_FACET_RAW | AMARK_FACET_CONTENT),
};

static const amark_facet_desc_t heading_facets[] = {
    FACET_INT("level", V, 1, 6, 1),
    FACET_ENUM("form", S, heading_form_names, 0),
    FACET_BOOL("closing", S, 0, 0),
};

static const amark_facet_desc_t thematic_break_facets[] = {
    FACET_ENUM("char", S, thematic_char_names, 0),
    FACET_INT("count", S, 3, NO_MAX, 0),
    FACET_BOOL("spaced", S, 0, 0),
};

static const amark_facet_desc_t link_definition_facets[] = {
    {sizeof(amark_facet_desc_t), "label", V, AMARK_FACET_STRING,
     AMARK_FACET_RAW | AMARK_FACET_NONEMPTY, 0, 0, NULL, 0, 0, NULL, check_label, NULL, NULL},
    FACET_STRING("destination", V, AMARK_FACET_NO_LINE_BREAK),
    FACET_STRING("title", V, AMARK_FACET_NO_BLANK_LINE),
    FACET_BOOL("angle", S, 0, 0),
    FACET_ENUM("title_quote", S, title_quote_names, 0),
};

static const amark_facet_desc_t text_facets[] = {
    FACET_STRING("literal", V, AMARK_FACET_NO_LINE_BREAK),
};

static const amark_facet_desc_t code_facets[] = {
    FACET_STRING("literal", V,
                 AMARK_FACET_NO_LINE_BREAK | AMARK_FACET_NONEMPTY | AMARK_FACET_FOLD_LINE_BREAKS),
    FACET_INT("backticks", S, 1, NO_MAX, 0),
    FACET_BOOL("padded", S, 0, 0),
};

static const amark_facet_desc_t html_inline_facets[] = {
    FACET_STRING("literal", V, AMARK_FACET_NO_BLANK_LINE | AMARK_FACET_RAW),
};

static const amark_facet_desc_t linebreak_facets[] = {
    FACET_ENUM("form", S, linebreak_form_names, 0),
};

/* emph and strong. */
static const amark_facet_desc_t emphasis_facets[] = {
    FACET_ENUM("delimiter", S, emphasis_delimiter_names, 0),
};

/* link and image. */
static const amark_facet_desc_t link_facets[] = {
    FACET_STRING("destination", V, AMARK_FACET_NO_LINE_BREAK),
    FACET_STRING("title", V, AMARK_FACET_NO_BLANK_LINE),
    FACET_STRING("label", V, AMARK_FACET_RAW),
    FACET_ENUM("form", S, link_form_names, 0),
    FACET_BOOL("angle", S, 0, 0),
    FACET_ENUM("title_quote", S, title_quote_names, 0),
};

#undef V
#undef S
#undef FACET_STRING
#undef FACET_INT
#undef FACET_ENUM
#undef FACET_BOOL

/* ---- Node types ------------------------------------------------------------ */

/* A link has no link among its descendants. This function refuses a link child;
 * the structural edits also check the ancestors of the parent and the
 * descendants of the inserted node. */
static bool link_can_contain(const amark_node_t *self, amark_node_type_t child)
{
    (void)self;
    return child != AMARK_NODE_LINK;
}

#define TYPE(name, level, provides, accepts, flags, facets, facet_count, can_contain) \
    {sizeof(amark_node_type_desc_t), name, level, provides, accepts, flags, facets, \
     facet_count, 0, can_contain, NULL, NULL, NULL, NULL, NULL, NULL}
#define COUNT(array) (sizeof(array) / sizeof(array[0]))
#define BLOCK AMARK_NODE_LEVEL_BLOCK
#define INLINE AMARK_NODE_LEVEL_INLINE
#define C_BLOCK AMARK_CONTENT_BLOCK
#define C_INLINE AMARK_CONTENT_INLINE
#define C_ITEM AMARK_CONTENT_LIST_ITEM
#define CONTENT AMARK_NODE_TYPE_CONTENT
#define MARGINS AMARK_NODE_TYPE_MARGINS

static const amark_node_type_desc_t document_type =
    TYPE("document", BLOCK, C_BLOCK, C_BLOCK, 0, NULL, 0, NULL);
static const amark_node_type_desc_t block_quote_type =
    TYPE("block_quote", BLOCK, C_BLOCK, C_BLOCK, MARGINS, NULL, 0, NULL);
static const amark_node_type_desc_t list_type =
    TYPE("list", BLOCK, C_BLOCK, C_ITEM, 0, list_facets, COUNT(list_facets), NULL);
static const amark_node_type_desc_t item_type =
    TYPE("item", BLOCK, C_ITEM, C_BLOCK, MARGINS, NULL, 0, NULL);
static const amark_node_type_desc_t code_block_type =
    TYPE("code_block", BLOCK, C_BLOCK, 0, CONTENT,
         code_block_facets, COUNT(code_block_facets), NULL);
static const amark_node_type_desc_t html_block_type =
    TYPE("html_block", BLOCK, C_BLOCK, 0, CONTENT,
         html_block_facets, COUNT(html_block_facets), NULL);
static const amark_node_type_desc_t paragraph_type =
    TYPE("paragraph", BLOCK, C_BLOCK, C_INLINE, CONTENT, NULL, 0, NULL);
static const amark_node_type_desc_t heading_type =
    TYPE("heading", BLOCK, C_BLOCK, C_INLINE, CONTENT, heading_facets, COUNT(heading_facets), NULL);
static const amark_node_type_desc_t thematic_break_type =
    TYPE("thematic_break", BLOCK, C_BLOCK, 0, 0,
         thematic_break_facets, COUNT(thematic_break_facets), NULL);
static const amark_node_type_desc_t link_definition_type =
    TYPE("link_definition", BLOCK, C_BLOCK, 0, CONTENT,
         link_definition_facets, COUNT(link_definition_facets), NULL);
static const amark_node_type_desc_t text_type =
    TYPE("text", INLINE, C_INLINE, 0, 0, text_facets, COUNT(text_facets), NULL);
static const amark_node_type_desc_t softbreak_type =
    TYPE("softbreak", INLINE, C_INLINE, 0, 0, NULL, 0, NULL);
static const amark_node_type_desc_t linebreak_type =
    TYPE("linebreak", INLINE, C_INLINE, 0, 0, linebreak_facets, COUNT(linebreak_facets), NULL);
static const amark_node_type_desc_t code_type =
    TYPE("code", INLINE, C_INLINE, 0, 0, code_facets, COUNT(code_facets), NULL);
static const amark_node_type_desc_t html_inline_type =
    TYPE("html_inline", INLINE, C_INLINE, 0, 0,
         html_inline_facets, COUNT(html_inline_facets), NULL);
static const amark_node_type_desc_t emph_type =
    TYPE("emph", INLINE, C_INLINE, C_INLINE, 0, emphasis_facets, COUNT(emphasis_facets), NULL);
static const amark_node_type_desc_t strong_type =
    TYPE("strong", INLINE, C_INLINE, C_INLINE, 0, emphasis_facets, COUNT(emphasis_facets), NULL);
static const amark_node_type_desc_t link_type =
    TYPE("link", INLINE, C_INLINE, C_INLINE, 0, link_facets, COUNT(link_facets), link_can_contain);
static const amark_node_type_desc_t image_type =
    TYPE("image", INLINE, C_INLINE, C_INLINE, 0, link_facets, COUNT(link_facets), NULL);

#undef TYPE
#undef COUNT
#undef BLOCK
#undef INLINE
#undef C_BLOCK
#undef C_INLINE
#undef C_ITEM
#undef CONTENT
#undef MARGINS

#define BUILTIN_TYPE_COUNT 19u

/* In the order of the identifiers: identifier i + 1 is core_types[i]. */
static const amark_node_type_desc_t *const core_types[BUILTIN_TYPE_COUNT] = {
    &document_type,       &block_quote_type, &list_type,       &item_type,
    &code_block_type,     &html_block_type,  &paragraph_type,  &heading_type,
    &thematic_break_type, &link_definition_type, &text_type,   &softbreak_type,
    &linebreak_type,      &code_type,        &html_inline_type, &emph_type,
    &strong_type,         &link_type,        &image_type,
};

static const amark_extension_t core_extension = {
    sizeof(amark_extension_t), AMARK_TREE_CORE_EXTENSION_NAME, core_types, BUILTIN_TYPE_COUNT,
    NULL, 0, NULL};

const amark_extension_t *amark_tree_core_extension(void)
{
    return &core_extension;
}

/* ---- The static built-in syntax set ---------------------------------------- */

/* The tables that freezing would build for a set with only amark.core. They are
 * not const because the set structure points to mutable arrays; nothing writes
 * them: every change refuses a frozen set and destroying a static set does
 * nothing. */
static const amark_extension_t *core_set_extensions[1] = {&core_extension};

#define CORE(type) {&type, &core_extension}

static amark_registry_type_t core_set_types[BUILTIN_TYPE_COUNT] = {
    CORE(document_type),       CORE(block_quote_type), CORE(list_type),
    CORE(item_type),           CORE(code_block_type),  CORE(html_block_type),
    CORE(paragraph_type),      CORE(heading_type),     CORE(thematic_break_type),
    CORE(link_definition_type), CORE(text_type),       CORE(softbreak_type),
    CORE(linebreak_type),      CORE(code_type),        CORE(html_inline_type),
    CORE(emph_type),           CORE(strong_type),      CORE(link_type),
    CORE(image_type),
};

#undef CORE

/* The name index, sorted by strcmp. */
static amark_registry_name_t core_set_names[BUILTIN_TYPE_COUNT] = {
    {"block_quote", AMARK_NODE_BLOCK_QUOTE},
    {"code", AMARK_NODE_CODE},
    {"code_block", AMARK_NODE_CODE_BLOCK},
    {"document", AMARK_NODE_DOCUMENT},
    {"emph", AMARK_NODE_EMPH},
    {"heading", AMARK_NODE_HEADING},
    {"html_block", AMARK_NODE_HTML_BLOCK},
    {"html_inline", AMARK_NODE_HTML_INLINE},
    {"image", AMARK_NODE_IMAGE},
    {"item", AMARK_NODE_ITEM},
    {"linebreak", AMARK_NODE_LINEBREAK},
    {"link", AMARK_NODE_LINK},
    {"link_definition", AMARK_NODE_LINK_DEFINITION},
    {"list", AMARK_NODE_LIST},
    {"paragraph", AMARK_NODE_PARAGRAPH},
    {"softbreak", AMARK_NODE_SOFTBREAK},
    {"strong", AMARK_NODE_STRONG},
    {"text", AMARK_NODE_TEXT},
    {"thematic_break", AMARK_NODE_THEMATIC_BREAK},
};

static amark_syntax_set_t core_set = {
    core_set_extensions,
    1,
    1,
    true,
    true,
    core_set_types,
    BUILTIN_TYPE_COUNT,
    NULL,
    0,
    core_set_names,
    BUILTIN_TYPE_COUNT,
    NULL,
    0,
};

const amark_syntax_set_t *amark_syntax_set_core(void)
{
    return &core_set;
}
