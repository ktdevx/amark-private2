# SPDX-License-Identifier: MIT
# Copyright (c) The Amark Authors

# The same source, once through the public API (amark, static or shared) and once
# through amark_internal.
amark_add_test(version_test SOURCES version_test.c)
amark_add_test(version_internal_test SOURCES version_test.c INTERNAL)

amark_add_test(error_test SOURCES error_test.c)

# The public API (amark), and the internal overflow-checked arithmetic
# (amark_internal).
amark_add_test(allocator_test SOURCES allocator_test.c)
amark_add_test(allocator_internal_test SOURCES allocator_internal_test.c INTERNAL)

amark_add_test(utf8_test SOURCES utf8_test.c)

amark_add_test(unicode_test SOURCES unicode_test.c)

# The public API (amark), and the embedding functions of the internal definition
# (amark_internal).
amark_add_test(buffer_test SOURCES buffer_test.c)
amark_add_test(buffer_internal_test SOURCES buffer_internal_test.c INTERNAL)

# The data-driven UTF-8 test vectors (4.7, 13.1, 13.2): gen_utf8_cases.cmake
# turns each data line of data/utf8_cases.txt into one test of the suite
# utf8_cases_test_utf8_cases.
amark_add_generated_tests(utf8_cases_test
  GENERATOR gen_utf8_cases.cmake
  DATA data/utf8_cases.txt
)
