/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The values of the facets: getting, setting, unsetting and recording them, the
 * facet numbers and the enumeration values of the built-in types, and the setting
 * of a text as a whole.
 */
#ifndef AMARK_VALUE_H
#define AMARK_VALUE_H

#include <stddef.h>
#include <stdint.h>

#include <amark/export.h>
#include <amark/error.h>
#include <amark/node.h>

AMARK_BEGIN_DECLS

/* The state of a value read from a facet. */
typedef enum amark_value_state
{
    /* The facet has a value: a set or recorded value, a value derived from the
     * spelling, or the default value of a value facet. */
    AMARK_VALUE_PRESENT = 1,
    /* A style facet that has no value. */
    AMARK_VALUE_UNSET = 2,
    /* The destination or the title of a reference link or image whose label has
     * no definition. */
    AMARK_VALUE_UNRESOLVED = 3
} amark_value_state_t;

/* A value of a facet. The caller sets struct_size to sizeof(amark_value_t). */
typedef struct amark_value
{
    size_t struct_size;
    amark_value_state_t state;
    /* An INT, an ENUM (from 1) or a BOOL (0 or 1). */
    int64_t number;
    /* A STRING, terminated by NUL, and its length in bytes. */
    const char *text;
    size_t length;
} amark_value_t;

/*
 * Getting, setting, unsetting and recording the facets of a node. facet is a
 * facet number of the type of node (1 to its facet count; the macros below for
 * the built-in types). A NULL node, a facet the type does not have and a value
 * of the wrong kind are AMARK_ERROR_INVALID_ARGUMENT, and the node is not
 * changed (3.10).
 */

/*
 * Reads the value of facet into *out. The caller sets out->struct_size to
 * sizeof(amark_value_t). The value is, in order:
 *   - the value set or recorded for the facet;
 *   - for a value facet, the default value of its type (state PRESENT);
 *   - for a style, the state UNSET (3.6).
 * A PRESENT INT, ENUM or BOOL has its number and a NULL text; a PRESENT STRING
 * has its text, terminated by NUL, and its length, and a number of 0. An UNSET
 * value has a number of 0, a NULL text and a length of 0. The text belongs to
 * the node or to its type: it stays valid until the next change of the facet
 * or the release of the node, and the caller does not free it. On failure *out is not
 * written; AMARK_ERROR_INVALID_ARGUMENT also for a NULL out and a struct_size
 * smaller than sizeof(amark_value_t).
 */
AMARK_API amark_error_t amark_node_get(const amark_node_t *node, amark_facet_t facet,
                                       amark_value_t *out);

/*
 * Set the value of facet (6.1-6.9): number for an INT, ENUM or BOOL facet,
 * the length bytes at text for a STRING facet (text may be NULL when length is
 * 0; the bytes are copied). In order:
 *   1. The value is checked: an INT within the range of the facet, an ENUM
 *      from 1 to its count of names, a BOOL 0 or 1; a STRING of valid UTF-8
 *      without NUL, without a line break (LF or CR) for a NO_LINE_BREAK
 *      facet, without a blank line (a line of only spaces and tabs between two
 *      line breaks) for a NO_BLANK_LINE facet, and not empty for a NONEMPTY
 *      facet; then the validate function of the facet, if any. An invalid
 *      value is AMARK_ERROR_INVALID_ARGUMENT.
 *   2. When the value equals the current value (amark_node_get), nothing
 *      changes (6.9). For an OVERRIDABLE facet, only an override that holds
 *      the same value is the same value: otherwise the value becomes the
 *      override (6.11).
 *   3. A LINES facet's value has its CRLF and CR changed to LF and, when not
 *      empty, ends with LF (6.5).
 *   4. The new value is prepared and the on_change function of the facet, if
 *      any, is called with it; the node still holds the old value.
 *   5. When on_change succeeds the value is committed and the spelling of the
 *      facet is dropped as by amark_node_discard_facet_spelling, without its
 *      copy of the value: the tokens that spell the facet are removed and the
 *      facet is marked as dropped; the other facets of the node, its layout
 *      and the spelling of the other nodes stay (6.7, 6.8). When on_change
 *      fails, the prepared value is discarded and its error is returned.
 *   A CONTENT facet (the literal of code_block and html_block) stores no value:
 *   the leaf gets new free content from the value (the value without its last
 *   LF; no line for "", else the LFs of the content + 1), without content
 *   tokens; the own spelling of the leaf (its layout tokens, such as the fence
 *   lines) stays (8.7). So "", "\n", "a" and "a\n\n" read back as "", "\n",
 *   "a\n" and "a\n\n".
 * On failure (also AMARK_ERROR_OUT_OF_MEMORY and AMARK_ERROR_OVERFLOW) the
 * value is not changed.
 */
AMARK_API amark_error_t amark_node_set_int(amark_node_t *node, amark_facet_t facet,
                                           int64_t number);
AMARK_API amark_error_t amark_node_set_string(amark_node_t *node, amark_facet_t facet,
                                              const char *text, size_t length);

/*
 * Unsets facet: a style returns to UNSET and its spelling is dropped (its
 * tokens are removed and the facet is marked as dropped), and an OVERRIDABLE
 * facet loses its override and returns to its derived value (6.11). Calls no
 * on_change. Unsetting a facet that has no value does nothing. Any other
 * facet is AMARK_ERROR_INVALID_ARGUMENT.
 */
AMARK_API amark_error_t amark_node_unset(amark_node_t *node, amark_facet_t facet);

/*
 * Record the value of facet, for parsers and the implementers of extensions
 * (6.14): the value is checked and normalized like a set value, but on_change
 * is not called, the value is stored even when it equals the current one, and
 * recording is not an edit (the spelling is kept). On failure the value is not
 * changed.
 */
AMARK_API amark_error_t amark_node_record_int(amark_node_t *node, amark_facet_t facet,
                                              int64_t number);
AMARK_API amark_error_t amark_node_record_string(amark_node_t *node, amark_facet_t facet,
                                                 const char *text, size_t length);

/*
 * Sets the inline children of node from text (6.12): the children of node are
 * unlinked and destroyed, and text, of length bytes (NULL when length is 0),
 * is divided at its line breaks (LF, CRLF and CR) into text nodes, one per
 * nonempty line with the line as its literal, with a softbreak node for each
 * line break between them, in order. The new nodes have no spelling; "" leaves
 * node without children. AMARK_ERROR_INVALID_ARGUMENT for a NULL node, NULL
 * text with a length, text with NUL or invalid UTF-8, and a node that cannot
 * contain a text node (or a softbreak node, when text has a line break); on
 * failure (also AMARK_ERROR_OUT_OF_MEMORY) node and its children are not
 * changed.
 */
AMARK_API amark_error_t amark_node_set_inline_text(amark_node_t *node, const char *text,
                                                   size_t length);

/*
 * Drops the spelling of facet of node, keeping its value: the tokens of node
 * that spell facet (layout and content tokens) are removed and the facet is
 * marked as dropped.
 *   - A STRING facet that was derived from those tokens keeps the derived
 *     value as its set value.
 *   - A CONTENT facet keeps its value as new free content of the leaf (the
 *     replaced bytes become U+FFFD), as when the facet is set; the own layout
 *     tokens of the leaf stay.
 * The change is notified. Dropping the spelling of a facet that has no token
 * and is already marked does nothing. AMARK_ERROR_INVALID_ARGUMENT for a NULL
 * node and a facet its type does not have; on failure (also
 * AMARK_ERROR_OUT_OF_MEMORY) node is not changed.
 */
AMARK_API amark_error_t amark_node_discard_facet_spelling(amark_node_t *node,
                                                          amark_facet_t facet);

/*
 * Drops all of the own spelling of node, keeping its values and styles (6.13):
 *   1. the value of each STRING facet derived from its tokens becomes its set
 *      value; and the values derived from the blank lines stay: for a list,
 *      or an item directly under a list, each OVERRIDABLE facet of that list
 *      without an override (tight) takes its current value as the override;
 *   2. the layout tokens, the content tokens, the margins and the blank lines
 *      of node are removed;
 *   3. laid content of a leaf becomes new free content with the same bytes and
 *      line count (the old object, which other leaves may share, is not
 *      changed), and the content tokens of the descendants of the leaf on the
 *      old content move to it. For a leaf with a CONTENT facet, laid content
 *      and content with tokens of the leaf on it become the value of that
 *      facet as set (the replaced bytes as U+FFFD), and the tokens of the
 *      descendants move only when the bytes stay the same;
 *   4. the structural spelling, and the spelling of each facet of node that
 *      had tokens, are marked as dropped. The line range stays (for the
 *      position queries).
 * The spelling of the children of node stays. The change is notified.
 * AMARK_ERROR_INVALID_ARGUMENT for a NULL node; on failure (also
 * AMARK_ERROR_OUT_OF_MEMORY) node is not changed.
 */
AMARK_API amark_error_t amark_node_discard_spelling(amark_node_t *node);

/*
 * The facet numbers of the built-in node types (AMARK_<TYPE>_<FACET>) and the
 * values of their enumerations (AMARK_<TYPE>_<FACET>_<NAME>, from 1). V marks a
 * value, S a style. document, block_quote, item, paragraph and softbreak have no
 * facet.
 */

/* list */
#define AMARK_LIST_TYPE 1u      /* V ENUM: bullet, ordered (default bullet) */
#define AMARK_LIST_START 2u     /* V INT 0-999999999 (default 1) */
#define AMARK_LIST_TIGHT 3u     /* V BOOL, derived from the blank lines unless overridden */
#define AMARK_LIST_BULLET 4u    /* S ENUM: - * + */
#define AMARK_LIST_DELIMITER 5u /* S ENUM: . ) */
#define AMARK_LIST_TYPE_BULLET 1
#define AMARK_LIST_TYPE_ORDERED 2
#define AMARK_LIST_BULLET_DASH 1
#define AMARK_LIST_BULLET_ASTERISK 2
#define AMARK_LIST_BULLET_PLUS 3
#define AMARK_LIST_DELIMITER_PERIOD 1
#define AMARK_LIST_DELIMITER_PAREN 2

/* code_block */
#define AMARK_CODE_BLOCK_INFO 1u         /* V STRING, no line break */
#define AMARK_CODE_BLOCK_LITERAL 2u      /* V STRING, lines; the content of the leaf */
#define AMARK_CODE_BLOCK_FORM 3u         /* S ENUM: indented, fenced */
#define AMARK_CODE_BLOCK_FENCE_CHAR 4u   /* S ENUM: ` ~ */
#define AMARK_CODE_BLOCK_FENCE_LENGTH 5u /* S INT 3- */
#define AMARK_CODE_BLOCK_FORM_INDENTED 1
#define AMARK_CODE_BLOCK_FORM_FENCED 2
#define AMARK_CODE_BLOCK_FENCE_CHAR_BACKTICK 1
#define AMARK_CODE_BLOCK_FENCE_CHAR_TILDE 2

/* html_block */
#define AMARK_HTML_BLOCK_LITERAL 1u /* V STRING, lines, raw; the content of the leaf */

/* heading */
#define AMARK_HEADING_LEVEL 1u   /* V INT 1-6 (default 1) */
#define AMARK_HEADING_FORM 2u    /* S ENUM: atx, setext */
#define AMARK_HEADING_CLOSING 3u /* S BOOL: the closing sequence of # */
#define AMARK_HEADING_FORM_ATX 1
#define AMARK_HEADING_FORM_SETEXT 2

/* thematic_break */
#define AMARK_THEMATIC_BREAK_CHAR 1u   /* S ENUM: * - _ */
#define AMARK_THEMATIC_BREAK_COUNT 2u  /* S INT 3- */
#define AMARK_THEMATIC_BREAK_SPACED 3u /* S BOOL: spaces between the characters */
#define AMARK_THEMATIC_BREAK_CHAR_ASTERISK 1
#define AMARK_THEMATIC_BREAK_CHAR_DASH 2
#define AMARK_THEMATIC_BREAK_CHAR_UNDERSCORE 3

/* link_definition */
#define AMARK_LINK_DEFINITION_LABEL 1u       /* V STRING, raw, a CommonMark link label */
#define AMARK_LINK_DEFINITION_DESTINATION 2u /* V STRING, no line break */
#define AMARK_LINK_DEFINITION_TITLE 3u       /* V STRING, no blank line */
#define AMARK_LINK_DEFINITION_ANGLE 4u       /* S BOOL: the destination in <> */
#define AMARK_LINK_DEFINITION_TITLE_QUOTE 5u /* S ENUM: " ' ( */
#define AMARK_LINK_DEFINITION_TITLE_QUOTE_DOUBLE 1
#define AMARK_LINK_DEFINITION_TITLE_QUOTE_SINGLE 2
#define AMARK_LINK_DEFINITION_TITLE_QUOTE_PAREN 3

/* text */
#define AMARK_TEXT_LITERAL 1u /* V STRING, no line break */

/* code */
#define AMARK_CODE_LITERAL 1u   /* V STRING, no line break, not empty */
#define AMARK_CODE_BACKTICKS 2u /* S INT 1- */
#define AMARK_CODE_PADDED 3u    /* S BOOL: a space inside each backtick string */

/* html_inline */
#define AMARK_HTML_INLINE_LITERAL 1u /* V STRING, no blank line, raw */

/* linebreak */
#define AMARK_LINEBREAK_FORM 1u /* S ENUM: spaces, backslash */
#define AMARK_LINEBREAK_FORM_SPACES 1
#define AMARK_LINEBREAK_FORM_BACKSLASH 2

/* emph */
#define AMARK_EMPH_DELIMITER 1u /* S ENUM: * _ */
#define AMARK_EMPH_DELIMITER_ASTERISK 1
#define AMARK_EMPH_DELIMITER_UNDERSCORE 2

/* strong */
#define AMARK_STRONG_DELIMITER 1u /* S ENUM: * _ */
#define AMARK_STRONG_DELIMITER_ASTERISK 1
#define AMARK_STRONG_DELIMITER_UNDERSCORE 2

/* link */
#define AMARK_LINK_DESTINATION 1u /* V STRING, no line break */
#define AMARK_LINK_TITLE 2u       /* V STRING, no blank line */
#define AMARK_LINK_LABEL 3u       /* V STRING, raw: the label of a reference */
#define AMARK_LINK_FORM 4u        /* S ENUM: inline, full, collapsed, shortcut, autolink */
#define AMARK_LINK_ANGLE 5u       /* S BOOL: the destination in <> */
#define AMARK_LINK_TITLE_QUOTE 6u /* S ENUM: " ' ( */
#define AMARK_LINK_FORM_INLINE 1
#define AMARK_LINK_FORM_FULL 2
#define AMARK_LINK_FORM_COLLAPSED 3
#define AMARK_LINK_FORM_SHORTCUT 4
#define AMARK_LINK_FORM_AUTOLINK 5
#define AMARK_LINK_TITLE_QUOTE_DOUBLE 1
#define AMARK_LINK_TITLE_QUOTE_SINGLE 2
#define AMARK_LINK_TITLE_QUOTE_PAREN 3

/* image: the facets of link, with the same numbers and enumeration values
 * (AMARK_LINK_FORM_*, AMARK_LINK_TITLE_QUOTE_*). */
#define AMARK_IMAGE_DESTINATION AMARK_LINK_DESTINATION
#define AMARK_IMAGE_TITLE AMARK_LINK_TITLE
#define AMARK_IMAGE_LABEL AMARK_LINK_LABEL
#define AMARK_IMAGE_FORM AMARK_LINK_FORM
#define AMARK_IMAGE_ANGLE AMARK_LINK_ANGLE
#define AMARK_IMAGE_TITLE_QUOTE AMARK_LINK_TITLE_QUOTE

AMARK_END_DECLS

#endif /* AMARK_VALUE_H */
