/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * CommonMark character classes (14.1-14.6) and the pinned Unicode version.
 */
#include <amark/amark.h>

#include <stddef.h>
#include <stdint.h>

#include <naos/naos.h>

#define COUNT_OF(array) (sizeof(array) / sizeof((array)[0]))

/* The 17 code points of the general category Zs in Unicode 17.0.0. */
static const uint32_t zs_chars[] = {
    0x0020, 0x00A0, 0x1680, 0x2000, 0x2001, 0x2002, 0x2003, 0x2004, 0x2005,
    0x2006, 0x2007, 0x2008, 0x2009, 0x200A, 0x202F, 0x205F, 0x3000,
};

/* Tab, line feed, form feed and carriage return. */
static const uint32_t whitespace_controls[] = {0x0009, 0x000A, 0x000C, 0x000D};

/* The 32 ASCII punctuation characters of CommonMark. */
static const char ascii_punctuation[] = "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";

/* A surrogate or a value above U+10FFFF: no class (14.4). */
static const uint32_t out_of_range[] = {
    0xD800, 0xD801, 0xDB7F, 0xDB80, 0xDBFF, 0xDC00, 0xDFFE, 0xDFFF,
    0x110000, 0x110020, 0x1FFFFF, 0x7FFFFFFF, 0x80000000u, 0xFFFFFFFFu,
};

static int is_ascii_punctuation_char(uint32_t c)
{
    size_t i;

    for (i = 0; ascii_punctuation[i] != '\0'; i++) {
        if ((uint32_t)(unsigned char)ascii_punctuation[i] == c) {
            return 1;
        }
    }
    return 0;
}

/* --- Version (14.5) --- */

NAOS_TEST(version_macros_are_17_0_0)
{
    NAOS_CHECK_INT_EQ(17, AMARK_UNICODE_VERSION_MAJOR);
    NAOS_CHECK_INT_EQ(0, AMARK_UNICODE_VERSION_MINOR);
    NAOS_CHECK_INT_EQ(0, AMARK_UNICODE_VERSION_UPDATE);
}

/* --- Whitespace (14.1) --- */

NAOS_TEST(every_zs_character_is_whitespace)
{
    size_t i;

    for (i = 0; i < COUNT_OF(zs_chars); i++) {
        NAOS_CHECK_TRUE(amark_unicode_is_whitespace(zs_chars[i]));
    }
}

NAOS_TEST(tab_lf_ff_cr_are_whitespace)
{
    size_t i;

    for (i = 0; i < COUNT_OF(whitespace_controls); i++) {
        NAOS_CHECK_TRUE(amark_unicode_is_whitespace(whitespace_controls[i]));
    }
}

NAOS_TEST(other_characters_are_not_whitespace)
{
    static const uint32_t others[] = {
        0x0000, 0x0008, 0x000B, 0x000E, 0x001F, 0x0021, 0x0041, 0x007F,
        0x0085, /* NEL (Cc) */
        0x009F, 0x00A1, 0x167F, 0x1681, 0x180E, /* MONGOLIAN VOWEL SEPARATOR (Cf) */
        0x1FFF, 0x200B, /* ZERO WIDTH SPACE (Cf) */
        0x2028, /* LINE SEPARATOR (Zl) */
        0x2029, /* PARAGRAPH SEPARATOR (Zp) */
        0x202E, 0x2030, 0x205E, 0x2060, 0x2FFF, 0x3001, 0xFEFF, 0x10FFFF,
    };
    size_t i;

    for (i = 0; i < COUNT_OF(others); i++) {
        NAOS_CHECK_FALSE(amark_unicode_is_whitespace(others[i]));
    }
}

/* Exactly the 17 Zs characters and the 4 controls in the whole code space. */
NAOS_TEST(whitespace_has_exactly_21_code_points)
{
    uint32_t c;
    unsigned long count = 0;

    for (c = 0; c <= 0x10FFFF; c++) {
        if (amark_unicode_is_whitespace(c)) {
            count++;
        }
    }
    NAOS_CHECK_UINT_EQ(21, count);
}

/* --- Unicode punctuation (14.2) --- */

NAOS_TEST(representative_p_and_s_are_punctuation)
{
    static const uint32_t punct[] = {
        0x0024, /* $ (Sc) */
        0x002B, /* + (Sm) */
        0x005E, /* ^ (Sk) */
        0x005F, /* _ (Pc) */
        0x00A1, /* INVERTED EXCLAMATION MARK (Po) */
        0x00A7, /* SECTION SIGN (Po) */
        0x00D7, /* MULTIPLICATION SIGN (Sm) */
        0x00F7, /* DIVISION SIGN (Sm) */
        0x02C2, /* MODIFIER LETTER LEFT ARROWHEAD (Sk) */
        0x2010, /* HYPHEN (Pd) */
        0x2018, /* LEFT SINGLE QUOTATION MARK (Pi) */
        0x2019, /* RIGHT SINGLE QUOTATION MARK (Pf) */
        0x20AC, /* EURO SIGN (Sc) */
        0x2E5D, /* OBLIQUE HYPHEN (Pd) */
        0x300C, /* LEFT CORNER BRACKET (Ps) */
        0x300D, /* RIGHT CORNER BRACKET (Pe) */
        0xFF5E, /* FULLWIDTH TILDE (Sm) */
        0x1F3FB, /* EMOJI MODIFIER FITZPATRICK TYPE-1-2 (Sk) */
        0x1F600, /* GRINNING FACE (So) */
        0x1FBFA, /* the last P or S code point of Unicode 17.0.0 (So) */
    };
    size_t i;

    for (i = 0; i < COUNT_OF(punct); i++) {
        NAOS_CHECK_TRUE(amark_unicode_is_punctuation(punct[i]));
    }
}

NAOS_TEST(letters_digits_and_marks_are_not_punctuation)
{
    static const uint32_t others[] = {
        0x0000, 0x0009, 0x0020, 0x0030, 0x0039, 0x0041, 0x005A, 0x0061, 0x007A,
        0x007F, 0x00A0, /* NO-BREAK SPACE (Zs) */
        0x00AA, /* FEMININE ORDINAL INDICATOR (Lo) */
        0x00E9, /* e WITH ACUTE (Ll) */
        0x0300, /* COMBINING GRAVE ACCENT (Mn) */
        0x0660, /* ARABIC-INDIC DIGIT ZERO (Nd) */
        0x2028, /* LINE SEPARATOR (Zl) */
        0x3000, /* IDEOGRAPHIC SPACE (Zs) */
        0x3042, /* HIRAGANA LETTER A (Lo) */
        0x4E00, /* CJK UNIFIED IDEOGRAPH (Lo) */
        0xE000, /* private use (Co) */
        0xFFFF, /* noncharacter (Cn) */
        0x1FBF0, /* SEGMENTED DIGIT ZERO (Nd), just after the range U+1FB94..U+1FBEF */
        0x1FBFB, /* unassigned, after the last P or S code point */
        0x1FFFF, 0x10FFFF,
    };
    size_t i;

    for (i = 0; i < COUNT_OF(others); i++) {
        NAOS_CHECK_FALSE(amark_unicode_is_punctuation(others[i]));
    }
}

/* The number of P and S code points of Unicode 17.0.0, as stated in the
 * generated table: every entry of the table is found by the search. */
NAOS_TEST(punctuation_has_exactly_9473_code_points)
{
    uint32_t c;
    unsigned long count = 0;

    for (c = 0; c <= 0x10FFFF; c++) {
        if (amark_unicode_is_punctuation(c)) {
            count++;
        }
    }
    NAOS_CHECK_UINT_EQ(9473, count);
}

/* In ASCII, P and S are exactly the 32 ASCII punctuation characters. */
NAOS_TEST(ascii_part_of_unicode_punctuation)
{
    uint32_t c;

    for (c = 0; c < 0x80; c++) {
        NAOS_CHECK_INT_EQ(is_ascii_punctuation_char(c), amark_unicode_is_punctuation(c) ? 1 : 0);
    }
}

/* --- ASCII punctuation (14.3) --- */

NAOS_TEST(all_32_ascii_punctuation_characters)
{
    size_t i;

    NAOS_CHECK_UINT_EQ(32, sizeof(ascii_punctuation) - 1);
    for (i = 0; ascii_punctuation[i] != '\0'; i++) {
        NAOS_CHECK_TRUE(amark_ascii_is_punctuation((uint32_t)(unsigned char)ascii_punctuation[i]));
    }
}

NAOS_TEST(values_next_to_ascii_punctuation_are_not)
{
    static const uint32_t adjacent[] = {
        0x0020, /* before ! */
        0x0030, /* after / */
        0x0039, /* before : */
        0x0041, /* after @ */
        0x005A, /* before [ */
        0x0061, /* after ` */
        0x007A, /* before { */
        0x007F, /* after ~ */
        0x0080,
        0x00A1, /* Unicode punctuation, but not ASCII */
        0xFF01, /* FULLWIDTH EXCLAMATION MARK */
        0x0121, /* ! + 0x100 */
        0x10021, /* ! + 0x10000 */
    };
    size_t i;

    for (i = 0; i < COUNT_OF(adjacent); i++) {
        NAOS_CHECK_FALSE(amark_ascii_is_punctuation(adjacent[i]));
    }
}

NAOS_TEST(ascii_punctuation_over_the_whole_code_space)
{
    uint32_t c;
    unsigned long count = 0;

    for (c = 0; c < 0x80; c++) {
        NAOS_CHECK_INT_EQ(is_ascii_punctuation_char(c), amark_ascii_is_punctuation(c) ? 1 : 0);
    }
    for (c = 0; c <= 0x10FFFF; c++) {
        if (amark_ascii_is_punctuation(c)) {
            count++;
        }
    }
    NAOS_CHECK_UINT_EQ(32, count);
}

/* --- Out of range (14.4) --- */

NAOS_TEST(surrogates_and_values_above_10ffff_have_no_class)
{
    size_t i;

    for (i = 0; i < COUNT_OF(out_of_range); i++) {
        NAOS_CHECK_FALSE(amark_unicode_is_whitespace(out_of_range[i]));
        NAOS_CHECK_FALSE(amark_unicode_is_punctuation(out_of_range[i]));
        NAOS_CHECK_FALSE(amark_ascii_is_punctuation(out_of_range[i]));
    }
}

/* Values whose low bits are a whitespace or punctuation character. */
NAOS_TEST(large_values_do_not_alias_small_ones)
{
    static const uint32_t bases[] = {0x110000, 0x200000, 0x1000000, 0x80000000u, 0xFFFF0000u};
    static const uint32_t lows[] = {0x09, 0x20, 0x21, 0x7E, 0xA0, 0x3000, 0x20AC};
    size_t i;
    size_t j;

    for (i = 0; i < COUNT_OF(bases); i++) {
        for (j = 0; j < COUNT_OF(lows); j++) {
            uint32_t c = bases[i] + lows[j];

            NAOS_CHECK_FALSE(amark_unicode_is_whitespace(c));
            NAOS_CHECK_FALSE(amark_unicode_is_punctuation(c));
            NAOS_CHECK_FALSE(amark_ascii_is_punctuation(c));
        }
    }
}

NAOS_SUITE(unicode,
    NAOS_ADD_TEST(version_macros_are_17_0_0),
    NAOS_ADD_TEST(every_zs_character_is_whitespace),
    NAOS_ADD_TEST(tab_lf_ff_cr_are_whitespace),
    NAOS_ADD_TEST(other_characters_are_not_whitespace),
    NAOS_ADD_TEST(whitespace_has_exactly_21_code_points),
    NAOS_ADD_TEST(representative_p_and_s_are_punctuation),
    NAOS_ADD_TEST(letters_digits_and_marks_are_not_punctuation),
    NAOS_ADD_TEST(punctuation_has_exactly_9473_code_points),
    NAOS_ADD_TEST(ascii_part_of_unicode_punctuation),
    NAOS_ADD_TEST(all_32_ascii_punctuation_characters),
    NAOS_ADD_TEST(values_next_to_ascii_punctuation_are_not),
    NAOS_ADD_TEST(ascii_punctuation_over_the_whole_code_space),
    NAOS_ADD_TEST(surrogates_and_values_above_10ffff_have_no_class),
    NAOS_ADD_TEST(large_values_do_not_alias_small_ones)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(unicode)
)
