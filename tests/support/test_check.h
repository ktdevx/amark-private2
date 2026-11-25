/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * Comparison macros for the Amark tests, on top of the Naos checks.
 *
 * The macros are expanded at the call site, so a failure is reported at the line
 * of the test, and they call no Amark function themselves.
 */
#ifndef AMARK_TEST_CHECK_H
#define AMARK_TEST_CHECK_H

#include <naos/naos.h>

/*
 * Checks that the size bytes at data are exactly the bytes of expected_literal, a
 * string literal, without its terminating NUL. The literal may hold any bytes,
 * including NUL ("a\0b") and invalid UTF-8 ("\xC0\x80"); a non-literal argument
 * does not compile. data may be NULL when size is 0.
 */
#define AMARK_CHECK_BYTES_EQ(data, size, expected_literal) \
    NAOS_CHECK_MEM_EQ((data), (size), ("" expected_literal), sizeof("" expected_literal) - 1)

#endif /* AMARK_TEST_CHECK_H */
