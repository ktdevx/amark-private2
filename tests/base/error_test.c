/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The error codes and their descriptions (10.2, 10.3, 10.7, 10.9).
 *
 * The values are fixed by the order of AMARK_ERROR_LIST and never change (10.7),
 * so the tests pin each of them.
 */
#include <amark/amark.h>

#include <naos/naos.h>

NAOS_TEST(values_follow_the_list_order)
{
    NAOS_CHECK_INT_EQ(0, AMARK_ERROR_NONE);
    NAOS_CHECK_INT_EQ(1, AMARK_ERROR_OUT_OF_MEMORY);
    NAOS_CHECK_INT_EQ(2, AMARK_ERROR_INVALID_ARGUMENT);
    NAOS_CHECK_INT_EQ(3, AMARK_ERROR_OVERFLOW);
    NAOS_CHECK_INT_EQ(4, AMARK_ERROR_LIMIT_EXCEEDED);
    NAOS_CHECK_INT_EQ(5, AMARK_ERROR_INVALID_STATE);
    NAOS_CHECK_INT_EQ(6, AMARK_ERROR_UNREPRESENTABLE);
}

/* Counts the codes of AMARK_ERROR_LIST, so that a code added to the list without
 * a test here is noticed. */
#define COUNT_CODE(name, text) +1
enum { CODE_COUNT = 0 AMARK_ERROR_LIST(COUNT_CODE) };
#undef COUNT_CODE

NAOS_TEST(list_has_seven_codes)
{
    NAOS_CHECK_INT_EQ(7, CODE_COUNT);
}

NAOS_TEST(each_code_has_its_description)
{
    NAOS_CHECK_STR_EQ("no error", amark_error_string(AMARK_ERROR_NONE));
    NAOS_CHECK_STR_EQ("out of memory", amark_error_string(AMARK_ERROR_OUT_OF_MEMORY));
    NAOS_CHECK_STR_EQ("invalid argument", amark_error_string(AMARK_ERROR_INVALID_ARGUMENT));
    NAOS_CHECK_STR_EQ("overflow", amark_error_string(AMARK_ERROR_OVERFLOW));
    NAOS_CHECK_STR_EQ("limit exceeded", amark_error_string(AMARK_ERROR_LIMIT_EXCEEDED));
    NAOS_CHECK_STR_EQ("invalid state", amark_error_string(AMARK_ERROR_INVALID_STATE));
    NAOS_CHECK_STR_EQ("unrepresentable", amark_error_string(AMARK_ERROR_UNREPRESENTABLE));
}

NAOS_TEST(descriptions_match_the_list)
{
#define CHECK_CODE(name, text) NAOS_CHECK_STR_EQ(text, amark_error_string(name));
    AMARK_ERROR_LIST(CHECK_CODE)
#undef CHECK_CODE
}

NAOS_TEST(descriptions_are_distinct)
{
    /* Every code is told apart from the others by its description, too. */
    int i;
    int j;

    for (i = 0; i < CODE_COUNT; i++)
    {
        for (j = i + 1; j < CODE_COUNT; j++)
        {
            NAOS_CHECK_STR_NE(amark_error_string((amark_error_t)i),
                              amark_error_string((amark_error_t)j));
        }
    }
}

NAOS_TEST(unknown_codes_have_a_description)
{
    NAOS_REQUIRE_NOT_NULL(amark_error_string((amark_error_t)7));
    NAOS_CHECK_STR_EQ("unknown error", amark_error_string((amark_error_t)7));
    NAOS_CHECK_STR_EQ("unknown error", amark_error_string((amark_error_t)8));
    NAOS_CHECK_STR_EQ("unknown error", amark_error_string((amark_error_t)1000));
    NAOS_CHECK_STR_EQ("unknown error", amark_error_string((amark_error_t)-1));
}

NAOS_TEST(descriptions_are_static)
{
    /* The same static string on every call; nothing to free. */
    NAOS_CHECK_PTR_EQ(amark_error_string(AMARK_ERROR_OVERFLOW),
                      amark_error_string(AMARK_ERROR_OVERFLOW));
    NAOS_CHECK_PTR_EQ(amark_error_string((amark_error_t)7),
                      amark_error_string((amark_error_t)7));
}

NAOS_SUITE(error,
    NAOS_ADD_TEST(values_follow_the_list_order),
    NAOS_ADD_TEST(list_has_seven_codes),
    NAOS_ADD_TEST(each_code_has_its_description),
    NAOS_ADD_TEST(descriptions_match_the_list),
    NAOS_ADD_TEST(descriptions_are_distinct),
    NAOS_ADD_TEST(unknown_codes_have_a_description),
    NAOS_ADD_TEST(descriptions_are_static)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(error)
)
