/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The tests of the decoding of character references.
 */
#include <amark/entity.h>

#include <naos/naos.h>

NAOS_TEST(header_compiles)
{
    NAOS_CHECK_TRUE(1);
}

NAOS_SUITE(entity,
    NAOS_ADD_TEST(header_compiles)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(entity)
)
