# SPDX-License-Identifier: MIT
# Copyright (c) The Amark Authors
#
# The tests of Amark. This directory is added when AMARK_BUILD_TESTS or AMARK_FUZZ
# is on (FuzzInfra's rule for fuzzer support libraries):
#   - Targets that depend on Naos (amark_test_support, the check tests) are defined
#     only inside if(AMARK_BUILD_TESTS). A fuzz-only configuration has no Naos.
#   - Support libraries that fuzz targets link must not depend on Naos, and are
#     defined whenever this directory is added.
#   - amark_add_test() does nothing when AMARK_BUILD_TESTS is off, so the
#     subdirectories call it unconditionally.

if(AMARK_BUILD_TESTS)
  # amark_test_support: the support code every test links (see amark_add_test).
  # It uses only the types of the public headers and calls no Amark function, so it
  # goes with either amark or amark_internal: its include directories are those of
  # the public headers, not a link to either library. It links Naos, so fuzz
  # targets must not link it.
  #   support/test_check.h      comparison macros (AMARK_CHECK_BYTES_EQ,
  #                             AMARK_CHECK_BUFFER_EQ)
  #   support/test_allocator.h  the test allocator and AMARK_CHECK_OOM_SWEEP
  add_library(amark_test_support STATIC support/test_allocator.c)
  amark_target_defaults(amark_test_support)
  target_include_directories(amark_test_support PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/support"
    "${PROJECT_SOURCE_DIR}/include")
  target_link_libraries(amark_test_support PUBLIC Naos::naos)

  # The test allocator and the sweep, built twice: with amark and with
  # amark_internal, so that a duplicate symbol in either combination fails to link.
  amark_add_test(test_allocator_test SOURCES support/test_allocator_test.c)
  amark_add_test(test_allocator_internal_test SOURCES support/test_allocator_test.c INTERNAL)
endif()

add_subdirectory(base)
add_subdirectory(testgen)

# The package consumption tests (label "package"). They configure and build nested
# projects, so AMARK_PACKAGE_TESTS can turn them off on their own.
if(AMARK_BUILD_TESTS AND AMARK_PACKAGE_TESTS)
  add_subdirectory(package)
endif()
