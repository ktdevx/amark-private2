/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * Character classes of CommonMark 0.31.2.
 *
 * The classification follows the definitions of the CommonMark specification:
 *   - Unicode whitespace character: a character of the Unicode general category
 *     Zs, or tab (U+0009), line feed (U+000A), form feed (U+000C) or carriage
 *     return (U+000D).
 *   - Unicode punctuation character: a character of a Unicode general category P
 *     (Pc, Pd, Pe, Pf, Pi, Po, Ps) or S (Sc, Sk, Sm, So).
 *   - ASCII punctuation character: U+0021..U+002F, U+003A..U+0040,
 *     U+005B..U+0060 and U+007B..U+007E.
 *
 * The data is that of the Unicode version given by the AMARK_UNICODE_VERSION_*
 * macros. A surrogate (U+D800..U+DFFF) or a value above U+10FFFF belongs to no
 * class. Each function takes a bounded time whatever its argument (a binary
 * search of a fixed table at most).
 */
#ifndef AMARK_UNICODE_H
#define AMARK_UNICODE_H

#include <stdbool.h>
#include <stdint.h>

#include <amark/export.h>

/* The version of the Unicode Character Database the classes are generated from.
 * tools/gen_unicode_tables.py fails when these differ from the version it pins. */
#define AMARK_UNICODE_VERSION_MAJOR 17
#define AMARK_UNICODE_VERSION_MINOR 0
#define AMARK_UNICODE_VERSION_UPDATE 0

AMARK_BEGIN_DECLS

/* Whether codepoint is a Unicode whitespace character: Zs, U+0009, U+000A,
 * U+000C or U+000D. */
AMARK_API bool amark_unicode_is_whitespace(uint32_t codepoint);

/* Whether codepoint is a Unicode punctuation character: general category P* or
 * S*. Every ASCII punctuation character is one. */
AMARK_API bool amark_unicode_is_punctuation(uint32_t codepoint);

/* Whether codepoint is an ASCII punctuation character: U+0021..U+002F,
 * U+003A..U+0040, U+005B..U+0060 or U+007B..U+007E. */
AMARK_API bool amark_ascii_is_punctuation(uint32_t codepoint);

AMARK_END_DECLS

#endif /* AMARK_UNICODE_H */
