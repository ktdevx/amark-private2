/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The version macros and the runtime version functions agree (9.4, 9.5).
 *
 * Built twice: linked with amark (public API, static or shared as configured) and
 * with amark_internal, so both ways the tests link Amark are exercised.
 */
#include <amark/amark.h>

#include <naos/naos.h>

#include <stdlib.h>
#include <string.h>

#include "test_check.h"

/* The version macros can be used in #if (no casts in AMARK_VERSION_ENCODE). */
#if AMARK_VERSION_NUMBER != AMARK_VERSION_ENCODE(AMARK_VERSION_MAJOR, AMARK_VERSION_MINOR, AMARK_VERSION_PATCH)
#error "AMARK_VERSION_NUMBER does not match AMARK_VERSION_ENCODE in #if"
#endif
#if AMARK_VERSION_ENCODE(0, 999, 999) >= AMARK_VERSION_ENCODE(1, 0, 0)
#error "AMARK_VERSION_ENCODE does not order versions in #if"
#endif

NAOS_TEST(number_is_usable_in_preprocessor)
{
#if AMARK_VERSION_NUMBER >= AMARK_VERSION_ENCODE(0, 1, 0)
    NAOS_PASS();
#else
    NAOS_FAIL("AMARK_VERSION_NUMBER is below 0.1.0 in #if");
#endif
}

NAOS_TEST(number_matches_header)
{
    NAOS_CHECK_INT_EQ(AMARK_VERSION_NUMBER, amark_version_number());
}

NAOS_TEST(string_matches_header)
{
    const char *version = amark_version_string();

    NAOS_REQUIRE_NOT_NULL(version);
    NAOS_CHECK_STR_EQ(AMARK_VERSION_STRING, version);
    AMARK_CHECK_BYTES_EQ(version, strlen(version), AMARK_VERSION_STRING);
    /* The string is static: every call returns the same one. */
    NAOS_CHECK_PTR_EQ(version, amark_version_string());
}

NAOS_TEST(number_is_built_from_components)
{
    NAOS_CHECK_INT_EQ(AMARK_VERSION_MAJOR * 1000000L + AMARK_VERSION_MINOR * 1000L +
                          AMARK_VERSION_PATCH,
                      AMARK_VERSION_NUMBER);
    NAOS_CHECK_INT_EQ(1002003L, AMARK_VERSION_ENCODE(1, 2, 3));
    NAOS_CHECK_INT_EQ(999999999L, AMARK_VERSION_ENCODE(999, 999, 999));
}

NAOS_TEST(numbers_compare_in_version_order)
{
    NAOS_CHECK_INT_LT(AMARK_VERSION_ENCODE(0, 1, 0), AMARK_VERSION_ENCODE(0, 1, 1));
    NAOS_CHECK_INT_LT(AMARK_VERSION_ENCODE(0, 1, 999), AMARK_VERSION_ENCODE(0, 2, 0));
    NAOS_CHECK_INT_LT(AMARK_VERSION_ENCODE(0, 999, 999), AMARK_VERSION_ENCODE(1, 0, 0));
}

/* Reads the next decimal number of the version string at *p, and the character
 * after it. Returns -1 when *p does not start with a digit. */
static long read_component(const char **p)
{
    char *end;
    long value;

    if ((**p < '0') || (**p > '9'))
    {
        return -1;
    }

    value = strtol(*p, &end, 10);
    *p = end;
    return value;
}

NAOS_TEST(string_is_built_from_components)
{
    const char *p = AMARK_VERSION_STRING;

    NAOS_CHECK_INT_EQ(AMARK_VERSION_MAJOR, read_component(&p));
    NAOS_REQUIRE_INT_EQ('.', *p++);
    NAOS_CHECK_INT_EQ(AMARK_VERSION_MINOR, read_component(&p));
    NAOS_REQUIRE_INT_EQ('.', *p++);
    NAOS_CHECK_INT_EQ(AMARK_VERSION_PATCH, read_component(&p));
    NAOS_CHECK_INT_EQ('\0', *p);
}

NAOS_SUITE(version,
    NAOS_ADD_TEST(number_is_usable_in_preprocessor),
    NAOS_ADD_TEST(number_matches_header),
    NAOS_ADD_TEST(string_matches_header),
    NAOS_ADD_TEST(number_is_built_from_components),
    NAOS_ADD_TEST(numbers_compare_in_version_order),
    NAOS_ADD_TEST(string_is_built_from_components)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(version)
)
