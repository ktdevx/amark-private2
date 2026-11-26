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
