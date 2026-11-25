/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

#include <amark/unicode.h>

#include <stddef.h>
#include <stdint.h>

/* The element type of the generated tables, which unicode_tables.inc does not
 * define: an inclusive range of code points. */
typedef struct
{
    uint32_t first;
    uint32_t last;
} amark_unicode_range_t;

#include "base/unicode_tables.inc"

#define AMARK_UNICODE_MAX 0x10FFFFu
#define AMARK_UNICODE_COUNT_OF(table) (sizeof(table) / sizeof((table)[0]))

/*
 * Whether codepoint lies in one of the count ranges of table, which are sorted
 * and do not overlap. A binary search: at most log2(count) + 1 steps.
 */
static bool in_ranges(const amark_unicode_range_t *table, size_t count, uint32_t codepoint)
{
    size_t low = 0;
    size_t high = count;

    while (low < high)
    {
        size_t mid = low + (high - low) / 2;

        if (codepoint < table[mid].first)
        {
            high = mid;
        }
        else if (codepoint > table[mid].last)
        {
            low = mid + 1;
        }
        else
        {
            return true;
        }
    }
    return false;
}

/* A surrogate (U+D800..U+DFFF) or a value above U+10FFFF. */
static bool is_out_of_range(uint32_t codepoint)
{
    return codepoint > AMARK_UNICODE_MAX || (codepoint >= 0xD800u && codepoint <= 0xDFFFu);
}

bool amark_ascii_is_punctuation(uint32_t codepoint)
{
    return (codepoint >= 0x21u && codepoint <= 0x2Fu) || (codepoint >= 0x3Au && codepoint <= 0x40u)
        || (codepoint >= 0x5Bu && codepoint <= 0x60u) || (codepoint >= 0x7Bu && codepoint <= 0x7Eu);
}

bool amark_unicode_is_whitespace(uint32_t codepoint)
{
    if (codepoint < 0x80u)
    {
        /* Zs in ASCII is only U+0020. */
        return codepoint == 0x20u || codepoint == 0x09u || codepoint == 0x0Au || codepoint == 0x0Cu
            || codepoint == 0x0Du;
    }
    if (is_out_of_range(codepoint))
    {
        return false;
    }
    return in_ranges(amark_unicode_zs, AMARK_UNICODE_COUNT_OF(amark_unicode_zs), codepoint);
}

bool amark_unicode_is_punctuation(uint32_t codepoint)
{
    if (codepoint < 0x80u)
    {
        /* P and S in ASCII are exactly the ASCII punctuation characters. */
        return amark_ascii_is_punctuation(codepoint);
    }
    if (is_out_of_range(codepoint))
    {
        return false;
    }
    return in_ranges(amark_unicode_punct, AMARK_UNICODE_COUNT_OF(amark_unicode_punct), codepoint);
}
