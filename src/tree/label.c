/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * DefinitionIndex: the label normalization (9.1), the comparison of a label
 * with a normalized key (without allocation) and the hash of the keys.
 */
#include "tree/tree.h"

#include <amark/buffer.h>
#include <amark/document.h>
#include <amark/unicode.h>
#include <amark/utf8.h>

#include <string.h>

/* The whitespace of a link label (CommonMark 0.31.2, 4.7: spaces, tabs and line
 * endings): U+0020, U+0009, U+000A and U+000D. Every other character, a
 * full-width space or a form feed included, is an ordinary character. */
static bool is_label_whitespace(uint32_t codepoint)
{
    return (codepoint == 0x20u) || (codepoint == 0x09u) || (codepoint == 0x0Au) ||
           (codepoint == 0x0Du);
}

/* Receives count bytes of the normalized label; a result other than
 * AMARK_ERROR_NONE stops the normalization and is returned. */
typedef amark_error_t (*emit_t)(void *context, const char *bytes, size_t count);

/* Normalizes the length bytes at label (not NULL unless length is 0), giving
 * the bytes of the result in order to emit. */
static amark_error_t normalize(const char *label, size_t length, emit_t emit, void *context)
{
    size_t position = 0;
    bool wrote = false;         /* a non-whitespace character was given */
    bool pending_space = false; /* whitespace was read after it */
    amark_error_t error = AMARK_ERROR_NONE;

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
            error = emit(context, " ", 1);
            pending_space = false;
        }
        count = amark_unicode_case_fold(codepoint, folded);
        for (i = 0; (i < count) && (error == AMARK_ERROR_NONE); i++)
        {
            char bytes[AMARK_UTF8_MAX_LENGTH];

            error = emit(context, bytes, amark_utf8_encode(folded[i], bytes));
        }
        wrote = true;
    }
    return error;
}

static amark_error_t emit_to_buffer(void *context, const char *bytes, size_t count)
{
    return amark_buffer_append((amark_buffer_t *)context, bytes, count);
}

amark_error_t amark_label_normalize(const char *label, size_t length, amark_buffer_t *out)
{
    size_t start_length;
    amark_error_t error;

    if ((out == NULL) || ((label == NULL) && (length != 0)))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    start_length = amark_buffer_length(out);
    /* Most labels fold to about their own length: grow once up front. */
    error = amark_buffer_reserve(out, length);
    if (error == AMARK_ERROR_NONE)
    {
        error = normalize(label, length, emit_to_buffer, out);
    }
    if (error != AMARK_ERROR_NONE)
    {
        /* Shortening never fails nor allocates: the content is as before. */
        (void)amark_buffer_truncate(out, start_length);
    }
    return error;
}

/* The comparison of a normalized label with a key: the bytes of the key
 * matched so far. */
typedef struct match
{
    const char *key;
    size_t key_length;
    size_t matched;
} match_t;

static amark_error_t emit_to_match(void *context, const char *bytes, size_t count)
{
    match_t *match = (match_t *)context;

    if ((count > match->key_length - match->matched) ||
        (memcmp(match->key + match->matched, bytes, count) != 0))
    {
        /* Any error stops the normalization: a mismatch. */
        return AMARK_ERROR_INVALID_STATE;
    }
    match->matched += count;
    return AMARK_ERROR_NONE;
}

bool amark_tree_label_matches(const char *label, size_t length, const char *key,
                              size_t key_length)
{
    match_t match;

    match.key = key;
    match.key_length = key_length;
    match.matched = 0;
    return (normalize(label, length, emit_to_match, &match) == AMARK_ERROR_NONE) &&
           (match.matched == key_length);
}

uint64_t amark_tree_label_hash(const char *key, size_t length)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    size_t i;

    for (i = 0; i < length; i++)
    {
        hash ^= (unsigned char)key[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}
