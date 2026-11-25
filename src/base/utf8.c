/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

#include <amark/utf8.h>

/*
 * Decodes the first character of text[0..length), length > 0. Returns its number
 * of bytes and stores its code point, and whether the sequence is well-formed.
 *
 * Each byte after the lead byte must lie in a range: the second byte in a range
 * that depends on the lead byte (it excludes overlong forms, surrogates and values
 * above U+10FFFF), the others in 80..BF. Decoding stops at the first byte out of
 * its range or at the end of the input; the bytes read until then are the maximal
 * subpart, replaced by one U+FFFD. Only bytes before that point are read.
 */
static size_t decode_one(const unsigned char *s, size_t length, uint32_t *out_codepoint,
                         bool *out_valid)
{
    unsigned char lead = s[0];
    size_t needed;
    unsigned char low = 0x80;
    unsigned char high = 0xBF;
    uint32_t cp;
    size_t i;

    if (lead < 0x80)
    {
        *out_codepoint = lead;
        *out_valid = true;
        return 1;
    }

    if ((lead >= 0xC2) && (lead <= 0xDF))
    {
        needed = 2;
        cp = lead & 0x1Fu;
    }
    else if ((lead >= 0xE0) && (lead <= 0xEF))
    {
        needed = 3;
        cp = lead & 0x0Fu;
        if (lead == 0xE0)
        {
            low = 0xA0; /* no overlong forms */
        }
        else if (lead == 0xED)
        {
            high = 0x9F; /* no surrogates */
        }
    }
    else if ((lead >= 0xF0) && (lead <= 0xF4))
    {
        needed = 4;
        cp = lead & 0x07u;
        if (lead == 0xF0)
        {
            low = 0x90; /* no overlong forms */
        }
        else if (lead == 0xF4)
        {
            high = 0x8F; /* nothing above U+10FFFF */
        }
    }
    else
    {
        /* A continuation byte, C0, C1 or F5..FF: never the start of a character. */
        *out_codepoint = AMARK_UTF8_REPLACEMENT;
        *out_valid = false;
        return 1;
    }

    for (i = 1; i < needed; i++)
    {
        if ((i >= length) || (s[i] < low) || (s[i] > high))
        {
            *out_codepoint = AMARK_UTF8_REPLACEMENT;
            *out_valid = false;
            return i;
        }

        cp = (cp << 6) | (uint32_t)(s[i] & 0x3Fu);
        low = 0x80;
        high = 0xBF;
    }

    *out_codepoint = cp;
    *out_valid = true;
    return needed;
}

static bool is_continuation(unsigned char byte)
{
    return (byte & 0xC0u) == 0x80u;
}

size_t amark_utf8_decode(const char *text, size_t length, uint32_t *out_codepoint)
{
    uint32_t cp;
    bool valid;
    size_t n;

    if ((text == NULL) || (length == 0))
    {
        return 0;
    }

    n = decode_one((const unsigned char *)text, length, &cp, &valid);
    if (out_codepoint != NULL)
    {
        *out_codepoint = cp;
    }

    return n;
}

/*
 * Every byte that is not a continuation byte starts a character when decoding
 * from the start, because a character only takes continuation bytes after its
 * lead byte. So the character that ends at position starts at the nearest such
 * byte within the last AMARK_UTF8_MAX_LENGTH bytes, if decoding from there takes
 * exactly the bytes up to position; otherwise the last byte is a character of its
 * own (a lone continuation byte).
 */
size_t amark_utf8_decode_before(const char *text, size_t position, uint32_t *out_codepoint)
{
    const unsigned char *s = (const unsigned char *)text;
    size_t start;
    uint32_t cp = AMARK_UTF8_REPLACEMENT;
    size_t n = 1;

    if ((text == NULL) || (position == 0))
    {
        return 0;
    }

    start = position - 1;
    while ((start > 0) && ((position - start) < AMARK_UTF8_MAX_LENGTH) && is_continuation(s[start]))
    {
        start--;
    }

    if (!is_continuation(s[start]))
    {
        uint32_t lead_cp;
        bool valid;
        size_t lead_n = decode_one(s + start, position - start, &lead_cp, &valid);

        if (lead_n == (position - start))
        {
            cp = lead_cp;
            n = lead_n;
        }
    }

    if (out_codepoint != NULL)
    {
        *out_codepoint = cp;
    }

    return n;
}

bool amark_utf8_validate(const char *text, size_t length, size_t *out_error_offset)
{
    const unsigned char *s = (const unsigned char *)text;
    size_t offset = 0;

    if ((text == NULL) && (length != 0))
    {
        if (out_error_offset != NULL)
        {
            *out_error_offset = 0;
        }

        return false;
    }
    while (offset < length)
    {
        uint32_t cp;
        bool valid;
        size_t n;

        if (s[offset] < 0x80)
        {
            offset++;
            continue;
        }

        n = decode_one(s + offset, length - offset, &cp, &valid);
        if (!valid)
        {
            if (out_error_offset != NULL)
            {
                *out_error_offset = offset;
            }

            return false;
        }

        offset += n;
    }

    if (out_error_offset != NULL)
    {
        *out_error_offset = length;
    }

    return true;
}

size_t amark_utf8_encode(uint32_t codepoint, char out[AMARK_UTF8_MAX_LENGTH])
{
    if (out == NULL)
    {
        return 0;
    }

    if (((codepoint >= 0xD800u) && (codepoint <= 0xDFFFu)) || (codepoint > 0x10FFFFu))
    {
        codepoint = AMARK_UTF8_REPLACEMENT;
    }

    if (codepoint < 0x80u)
    {
        out[0] = (char)codepoint;
        return 1;
    }

    if (codepoint < 0x800u)
    {
        out[0] = (char)(0xC0u | (codepoint >> 6));
        out[1] = (char)(0x80u | (codepoint & 0x3Fu));
        return 2;
    }

    if (codepoint < 0x10000u)
    {
        out[0] = (char)(0xE0u | (codepoint >> 12));
        out[1] = (char)(0x80u | ((codepoint >> 6) & 0x3Fu));
        out[2] = (char)(0x80u | (codepoint & 0x3Fu));
        return 3;
    }

    out[0] = (char)(0xF0u | (codepoint >> 18));
    out[1] = (char)(0x80u | ((codepoint >> 12) & 0x3Fu));
    out[2] = (char)(0x80u | ((codepoint >> 6) & 0x3Fu));
    out[3] = (char)(0x80u | (codepoint & 0x3Fu));
    return 4;
}
