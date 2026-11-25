# SPDX-License-Identifier: MIT
# Copyright (c) The Amark Authors
#
# The package consumption tests (label "package"): a consumer project
# (consumer/) is configured, built and run against Amark taken in each of the four
# ways (3.2-3.4, 3.6, 3.7, 1.5, 4.11). Each test runs
#   cmake -DMODE=<mode> ... -P cmake/scripts/run_package_test.cmake
# with the generator, the C compiler and the configuration of this build, in its
# own work directory under ${CMAKE_CURRENT_BINARY_DIR}. The run_package_test.cmake
# header lists its inputs, and how to run it against an existing install prefix.
#
#   package.find_package               install, then find_package without and with
#                                      the installed kind as a component
#   package.find_package_wrong_variant install, then find_package with the kind that
#                                      is not installed must fail as "not found"
#   package.pkg_config                 install, then pkg-config (skipped, reported
#                                      by "AMARK_SKIP:", without pkg-config; 4.12)
#   package.add_subdirectory           the source tree with add_subdirectory
#   package.fetchcontent               the source tree with FetchContent (SOURCE_DIR,
#                                      no network)
#
# Added by tests/CMakeLists.txt when AMARK_BUILD_TESTS and AMARK_PACKAGE_TESTS are
# on. The install-based tests need the install rules (AMARK_INSTALL).

set(_amark_package_script "${PROJECT_SOURCE_DIR}/cmake/scripts/run_package_test.cmake")

set(_amark_package_modes add_subdirectory fetchcontent)
if(AMARK_INSTALL)
  list(PREPEND _amark_package_modes find_package find_package_wrong_variant pkg_config)
else()
  message(STATUS "Amark: AMARK_INSTALL is off; the install-based package tests "
    "(find_package, pkg_config) are not registered.")
endif()

if(BUILD_SHARED_LIBS)
  set(_amark_package_shared ON)
else()
  set(_amark_package_shared OFF)
endif()

# The sanitizers of the top-level build are also applied to the consumer
# executable, which links the instrumented library. A list cannot be passed in one
# add_test() argument, so it is joined with ",".
set(_amark_package_sanitize "")
if(PROJECT_IS_TOP_LEVEL AND AMARK_SANITIZE)
  list(JOIN AMARK_SANITIZE "," _amark_package_sanitize)
endif()
# The coverage instrumentation of libFuzzer (AmarkFuzz.cmake) is in the library as
# well; the consumer needs its runtime callbacks.
if(AMARK_FUZZ AND AMARK_HAVE_LIBFUZZER)
  if(_amark_package_sanitize STREQUAL "")
    set(_amark_package_sanitize "fuzzer-no-link")
  else()
    string(APPEND _amark_package_sanitize ",fuzzer-no-link")
  endif()
endif()

set(_amark_package_args
  "-DAMARK_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
  "-DAMARK_BINARY_DIR=${PROJECT_BINARY_DIR}"
  "-DGENERATOR=${CMAKE_GENERATOR}"
  "-DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
  "-DGENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
  "-DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
  "-DC_COMPILER=${CMAKE_C_COMPILER}"
  "-DCONFIG=$<CONFIG>"
  "-DBUILD_SHARED_LIBS=${_amark_package_shared}"
  "-DSANITIZE=${_amark_package_sanitize}"
)
if(CMAKE_OSX_ARCHITECTURES)
  list(JOIN CMAKE_OSX_ARCHITECTURES "," _amark_package_archs)
  list(APPEND _amark_package_args "-DOSX_ARCHITECTURES=${_amark_package_archs}")
  unset(_amark_package_archs)
endif()
if(DEFINED PKG_CONFIG_EXECUTABLE)
  list(APPEND _amark_package_args "-DPKG_CONFIG_EXECUTABLE=${PKG_CONFIG_EXECUTABLE}")
endif()

foreach(_amark_package_mode IN LISTS _amark_package_modes)
  add_test(NAME package.${_amark_package_mode}
    COMMAND "${CMAKE_COMMAND}"
      "-DMODE=${_amark_package_mode}"
      ${_amark_package_args}
      "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/${_amark_package_mode}"
      -P "${_amark_package_script}"
  )
  set_tests_properties(package.${_amark_package_mode} PROPERTIES
    LABELS package
    SKIP_REGULAR_EXPRESSION "AMARK_SKIP:"
    TIMEOUT 900
  )
endforeach()

unset(_amark_package_mode)
unset(_amark_package_modes)
unset(_amark_package_args)
unset(_amark_package_shared)
unset(_amark_package_sanitize)
unset(_amark_package_script)
