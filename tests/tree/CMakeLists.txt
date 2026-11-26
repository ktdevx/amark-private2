# SPDX-License-Identifier: MIT
# Copyright (c) The Amark Authors
#
# The tests of the syntax tree (syntax-tree). Added by tests/CMakeLists.txt with
# the other test directories, so it is also added in a fuzz-only configuration:
# register the tests with amark_add_test(), which does nothing when
# AMARK_BUILD_TESTS is off, and define nothing here that depends on Naos.
# Tests that include the internal headers of src/tree/ (tree/tree.h,
# tree/syntax_set.h) use INTERNAL.

# The descriptors of the built-in node types and amark.core (internal tables).
amark_add_test(builtin_types_test SOURCES builtin_types_test.c INTERNAL)
