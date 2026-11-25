# SPDX-License-Identifier: MIT
# Copyright (c) The Amark Authors
#
# The test infrastructure of Amark: Naos and the registration of the tests.
#
# Always include this module, after AmarkCompilerOptions.cmake and the option
# AMARK_BUILD_TESTS: its functions are always defined and do nothing when the tests
# are not built.
#
# When AMARK_BUILD_TESTS is on, Naos is fetched with FetchContent at the tag below,
# the only place the tag is written, and testing is enabled. When it is off,
# including a fuzz-only configuration (AMARK_FUZZ on), Naos is not fetched.
#
# amark_add_test(<target>
#     SOURCES <source>...      # one of them defines main() with NAOS_MAIN
#     [INTERNAL]               # link amark_internal (src/ can be included)
#     [LIBRARIES <library>...]
#     [LABELS <label>...])
#   Creates the test executable <target> with naos_add_executable(), applies
#   amark_target_defaults(), writes it to ${PROJECT_BINARY_DIR}/bin (where amark
#   puts its DLL, so the tests find it on Windows), and links either amark or, with
#   INTERNAL, amark_internal (never both), plus amark_test_support and LIBRARIES.
#   Each test is registered with CTest as <target>.<suite>.<test> after every
#   build. Does nothing when AMARK_BUILD_TESTS is off.

include_guard(DIRECTORY)

if(AMARK_BUILD_TESTS)
  include(FetchContent)
  FetchContent_Declare(Naos
    GIT_REPOSITORY https://github.com/vabelorg/naos.git
    GIT_TAG v0.4.0
  )
  FetchContent_MakeAvailable(Naos)
  # include(CTest) enables testing only with BUILD_TESTING; AMARK_BUILD_TESTS may
  # be on without it.
  enable_testing()
endif()

function(amark_add_test target)
  if(NOT AMARK_BUILD_TESTS)
    return()
  endif()

  cmake_parse_arguments(PARSE_ARGV 1 arg "INTERNAL" "" "SOURCES;LIBRARIES;LABELS")
  if(arg_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR
      "amark_add_test(${target}): unknown arguments: ${arg_UNPARSED_ARGUMENTS}")
  endif()
  if(NOT arg_SOURCES)
    message(FATAL_ERROR "amark_add_test(${target}): SOURCES is required")
  endif()

  if(arg_INTERNAL)
    set(amark_library amark_internal)
  else()
    set(amark_library amark)
  endif()

  naos_add_executable("${target}"
    SOURCES ${arg_SOURCES}
    LIBRARIES ${amark_library} amark_test_support ${arg_LIBRARIES}
    LABELS ${arg_LABELS}
  )
  amark_target_defaults("${target}")
  set_target_properties("${target}" PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
endfunction()
