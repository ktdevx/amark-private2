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
  # goes with either amark or amark_internal. It links Naos, so fuzz targets must
  # not link it.
  #   support/test_check.h  comparison macros (AMARK_CHECK_BYTES_EQ)
  add_library(amark_test_support INTERFACE)
  target_include_directories(amark_test_support INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/support")
  target_link_libraries(amark_test_support INTERFACE Naos::naos)
endif()

add_subdirectory(base)
