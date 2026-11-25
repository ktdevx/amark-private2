# SPDX-License-Identifier: MIT
# Copyright (c) The Amark Authors

# The self-test of the test generation (4.7, 4.8): gen_bytes.cmake turns
# data/bytes.bin into the suite testgen_test_bytes at build time, and
# testgen_test.c (the suite testgen_expected) compares what it generated with the
# content of the data, byte for byte.
amark_add_generated_tests(testgen_test
  GENERATOR gen_bytes.cmake
  DATA data/bytes.bin
  SOURCES testgen_test.c
  EXTRA_SUITES testgen_expected
)
