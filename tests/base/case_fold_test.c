/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The tests of the Unicode case fold.
 */
#include <amark/unicode.h>

#include <naos/naos.h>

NAOS_TEST(header_compiles)
{
    NAOS_CHECK_TRUE(1);
}

NAOS_SUITE(case_fold,
    NAOS_ADD_TEST(header_compiles)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(case_fold)
)
