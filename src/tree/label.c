/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * DefinitionIndex: the label normalization (9.1).
 */
#include "tree/tree.h"

#include <amark/buffer.h>
#include <amark/document.h>
#include <amark/unicode.h>
#include <amark/utf8.h>

/* The whitespace of a link label (CommonMark 0.31.2, 4.7: spaces, tabs and line
 * endings): U+0020, U+0009, U+000A and U+000D. Every other character, a
 * full-width space or a form feed included, is an ordinary character. */
static bool is_label_whitespace(uint32_t codepoint)
{
    return (codepoint == 0x20u) || (codepoint == 0x09u) || (codepoint == 0x0Au) ||
           (codepoint == 0x0Du);
}

amark_error_t amark_label_normalize(const char *label, size_t length, amark_buffer_t *out)
{
    size_t start_length;
    size_t position = 0;
    bool wrote = false;         /* a non-whitespace character was appended */
    bool pending_space = false; /* whitespace was read after it */
    amark_error_t error;

    if ((out == NULL) || ((label == NULL) && (length != 0)))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    start_length = amark_buffer_length(out);
    /* Most labels fold to about their own length: grow once up front. */
    error = amark_buffer_reserve(out, length);
    while ((error == AMARK_ERROR_NONE) && (position < length))
    {
        uint32_t codepoint = 0;
        uint32_t folded[3];
        size_t count;
        size_t i;

        position += amark_utf8_decode(label + position, length - position, &codepoint);
        if (is_label_whitespace(codepoint))
        {
            /* Leading whitespace is dropped; a run after a character becomes one
             * space only when another character follows (no trailing space). */
            pending_space = wrote;
            continue;
        }
        if (pending_space)
        {
            error = amark_buffer_append_byte(out, ' ');
            pending_space = false;
        }
        count = amark_unicode_case_fold(codepoint, folded);
        for (i = 0; (i < count) && (error == AMARK_ERROR_NONE); i++)
        {
            error = amark_buffer_append_codepoint(out, folded[i]);
        }
        wrote = true;
    }
    if (error != AMARK_ERROR_NONE)
    {
        /* Shortening never fails nor allocates: the content is as before. */
        (void)amark_buffer_truncate(out, start_length);
    }
    return error;
}
