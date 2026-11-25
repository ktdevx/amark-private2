# SPDX-License-Identifier: MIT
# Copyright (c) The Amark Authors

cmake_minimum_required(VERSION 3.21)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# The version is defined only in include/amark/version.h.
include(AmarkVersion)
amark_read_version("${CMAKE_CURRENT_SOURCE_DIR}/include/amark/version.h")

project(amark
  VERSION "${AMARK_VERSION}"
  DESCRIPTION "A C99 library that converts between Markdown and a lossless syntax tree"
  LANGUAGES C)

# CTest (BUILD_TESTING, enable_testing) only when Amark is the top-level project.
if(PROJECT_IS_TOP_LEVEL)
  include(CTest)
endif()

# Options. "Top-level" defaults are on only when Amark is the top-level project, so
# that an embedding project (add_subdirectory, FetchContent) gets no tests, fuzzers,
# install rules or warnings-as-errors unless it asks for them.
if(PROJECT_IS_TOP_LEVEL)
  option(BUILD_SHARED_LIBS "Build amark as a shared library" OFF)
endif()

if(PROJECT_IS_TOP_LEVEL AND BUILD_TESTING)
  set(_amark_build_tests_default ON)
else()
  set(_amark_build_tests_default OFF)
endif()
option(AMARK_BUILD_TESTS "Build the tests of Amark (fetches Naos)" ${_amark_build_tests_default})
option(AMARK_PACKAGE_TESTS "Register the package consumption tests" ${AMARK_BUILD_TESTS})
option(AMARK_INSTALL "Generate the install rules of Amark" ${PROJECT_IS_TOP_LEVEL})
option(AMARK_WARNINGS_AS_ERRORS "Treat compiler warnings as errors in Amark's targets"
  ${PROJECT_IS_TOP_LEVEL})
set(AMARK_SANITIZE "" CACHE STRING
  "Sanitizers to enable, a list of \"address\" and \"undefined\" (top-level only)")
option(AMARK_FUZZ "Build the fuzz targets of Amark" OFF)
set(AMARK_FUZZ_SMOKE_SECONDS 60 CACHE STRING
  "Seconds each fuzz target runs in the fuzz smoke target")
unset(_amark_build_tests_default)

# Compiler settings and sanitizers; included before any target is created so that
# the sanitizers cover every target.
include(AmarkCompilerOptions)

# The testing and fuzzing modules are always included: they always define their
# functions (amark_add_test, amark_add_generated_tests, amark_add_fuzzer), which do
# nothing when testing or fuzzing is disabled. They are included before the library
# so that settings they apply to the directory reach the library as well.
# OPTIONAL and the EXISTS checks below let the build configure while these modules
# and directories are added by their own tasks.
include(AmarkTesting OPTIONAL)
include(AmarkFuzz OPTIONAL)

add_subdirectory(src)

# tests/ comes before fuzz/: it defines the support libraries the fuzz targets
# link, also in a fuzz-only configuration. Subdirectories added by later specs
# (such as bench/) go after fuzz/.
if(AMARK_BUILD_TESTS OR AMARK_FUZZ)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
  endif()
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/fuzz/CMakeLists.txt")
    add_subdirectory(fuzz)
  endif()
endif()
