/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The decoding of character references (amark/entity.h).
 */
#include <amark/entity.h>

/* The element type of the generated table, which entity_tables.inc does not
 * define: a name without "&" and ";", and its 1 or 2 code points followed by a
 * zero. */
typedef struct
{
    const char *name;
    uint32_t codepoints[2];
} amark_entity_t;

#include "base/entity_tables.inc"

#define AMARK_ENTITY_COUNT (sizeof(amark_entities) / sizeof(amark_entities[0]))

/* The digit limits of the numeric references (CommonMark 0.31.2, 2.5). */
#define AMARK_CHAR_REF_DECIMAL_MAX_DIGITS 7
#define AMARK_CHAR_REF_HEX_MAX_DIGITS 6

#define AMARK_CHAR_REF_REPLACEMENT 0xFFFDu

/* The value of the ASCII hexadecimal digit c, or -1. */
static int hex_digit_value(char c)
{
    if ((c >= '0') && (c <= '9'))
    {
        return c - '0';
    }
    if ((c >= 'a') && (c <= 'f'))
    {
        return c - 'a' + 10;
    }
    if ((c >= 'A') && (c <= 'F'))
    {
        return c - 'A' + 10;
    }
    return -1;
}

/*
 * Decodes the digits[0..length) of a numeric reference in the base 10 or 16.
 * At most 7 decimal or 6 hexadecimal digits, so the value fits in 32 bits.
 */
static bool decode_numeric(const char *digits, size_t length, bool hex, uint32_t *codepoint)
{
    size_t max = hex ? AMARK_CHAR_REF_HEX_MAX_DIGITS : AMARK_CHAR_REF_DECIMAL_MAX_DIGITS;
    uint32_t value = 0;
    size_t i;

    if ((length == 0) || (length > max))
    {
        return false;
    }
    for (i = 0; i < length; i++)
    {
        int digit = hex_digit_value(digits[i]);

        if ((digit < 0) || (!hex && (digit > 9)))
        {
            return false;
        }
        value = value * (hex ? 16u : 10u) + (uint32_t)digit;
    }
    if ((value == 0) || ((value >= 0xD800u) && (value <= 0xDFFFu)) || (value > 0x10FFFFu))
    {
        value = AMARK_CHAR_REF_REPLACEMENT;
    }
    *codepoint = value;
    return true;
}

/*
 * Compares name[0..length), which may hold any bytes, with the terminated entry
 * in the byte order: negative, zero or positive. Reads no byte of entry past its
 * terminator.
 */
static int compare_name(const char *name, size_t length, const char *entry)
{
    size_t i;

    for (i = 0; i < length; i++)
    {
        unsigned char a = (unsigned char)name[i];
        unsigned char b = (unsigned char)entry[i];

        if (b == 0u)
        {
            return 1; /* entry is a proper prefix of name */
        }
        if (a != b)
        {
            return (a < b) ? -1 : 1;
        }
    }
    return (entry[length] == '\0') ? 0 : -1;
}

/*
 * Finds name[0..length) in the table, which is sorted by the bytes of the name:
 * a binary search, at most log2(count) + 1 steps.
 */
static const amark_entity_t *find_entity(const char *name, size_t length)
{
    size_t low = 0;
    size_t high = AMARK_ENTITY_COUNT;

    if ((length == 0) || (length > AMARK_ENTITY_NAME_MAX))
    {
        return NULL;
    }
    while (low < high)
    {
        size_t mid = low + (high - low) / 2;
        int order = compare_name(name, length, amark_entities[mid].name);

        if (order < 0)
        {
            high = mid;
        }
        else if (order > 0)
        {
            low = mid + 1;
        }
        else
        {
            return &amark_entities[mid];
        }
    }
    return NULL;
}

bool amark_char_ref_decode(const char *text, size_t length, uint32_t codepoints[2], size_t *count)
{
    const char *body;
    size_t body_length;

    /* The shortest form, "&" + one character + ";", has 3 bytes. */
    if ((length < 3) || (text[0] != '&') || (text[length - 1] != ';'))
    {
        return false;
    }
    body = text + 1;
    body_length = length - 2;

    if (body[0] == '#')
    {
        bool hex = (body[1] == 'x') || (body[1] == 'X');
        size_t skip = hex ? 2 : 1;
        uint32_t codepoint;

        if (!decode_numeric(body + skip, body_length - skip, hex, &codepoint))
        {
            return false;
        }
        codepoints[0] = codepoint;
        *count = 1;
        return true;
    }

    {
        const amark_entity_t *entity = find_entity(body, body_length);
        size_t n;

        if (entity == NULL)
        {
            return false;
        }
        n = (entity->codepoints[1] != 0u) ? 2 : 1;
        codepoints[0] = entity->codepoints[0];
        if (n == 2)
        {
            codepoints[1] = entity->codepoints[1];
        }
        *count = n;
        return true;
    }
}
