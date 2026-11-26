/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The tests of Document (syntax-tree).
 */
#include "tree/tree.h"

#include <naos/naos.h>

NAOS_TEST(headers_compile)
{
    NAOS_CHECK_TRUE(1);
}

NAOS_SUITE(document,
    NAOS_ADD_TEST(headers_compile)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(document)
)
