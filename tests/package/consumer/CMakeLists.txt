# SPDX-License-Identifier: MIT
# Copyright (c) The Amark Authors
#
# The consumer project of the package tests. It takes Amark in the way given by
# AMARK_CONSUME_MODE, checks what that way promises, and builds amark_consumer
# (main.c) into <build>/bin, which cmake/scripts/run_package_test.cmake runs.
#
# Inputs (-D):
#   AMARK_CONSUME_MODE        find_package, pkg_config, add_subdirectory or
#                             fetchcontent.
#   AMARK_EXPECTED_VERSION    The version of the source tree under test (X.Y.Z).
#   AMARK_EXPECTED_KIND       "static" or "shared": the kind the consumer must get.
#   AMARK_CONSUME_COMPONENTS  find_package: the components to request (a list,
#                             may be empty).
#   AMARK_EXPECTED_PREFIX     find_package, pkg_config: the install prefix Amark
#                             must be found in.
#   AMARK_SOURCE_DIR          add_subdirectory, fetchcontent: the source tree.
#   AMARK_CONSUMER_SANITIZE   Sanitizers to apply to amark_consumer, joined with ","
#                             (when the installed library is instrumented).
#
# Checks, each a FATAL_ERROR at configure time:
#   find_package  Amark_VERSION is AMARK_EXPECTED_VERSION, the package comes from
#                 AMARK_EXPECTED_PREFIX, Amark_<kind>_FOUND is true for the
#                 expected kind only, and Amark::amark is a library of that kind.
#   pkg_config    The module version is AMARK_EXPECTED_VERSION and comes from the
#                 prefix; the flags carry -DAMARK_SHARED exactly for the shared kind.
#   add_subdirectory, fetchcontent (1.5, 3.3): Naos is not fetched, the tests,
#                 install rules and fuzzing of Amark are off, warnings are not
#                 errors, amark and amark_objects have no -Werror or /WX in their
#                 compile options, and the compile options and flags of this
#                 project are unchanged by Amark.

cmake_minimum_required(VERSION 3.21)
project(amark_consumer LANGUAGES C)

set(_modes find_package pkg_config add_subdirectory fetchcontent)
if(NOT AMARK_CONSUME_MODE IN_LIST _modes)
  message(FATAL_ERROR
    "AMARK_CONSUME_MODE must be one of ${_modes}, not \"${AMARK_CONSUME_MODE}\".")
endif()
foreach(_var IN ITEMS AMARK_EXPECTED_VERSION AMARK_EXPECTED_KIND)
  if(NOT ${_var})
    message(FATAL_ERROR "${_var} is required.")
  endif()
endforeach()
if(NOT AMARK_EXPECTED_KIND MATCHES "^(static|shared)$")
  message(FATAL_ERROR "AMARK_EXPECTED_KIND must be \"static\" or \"shared\".")
endif()
if(AMARK_EXPECTED_KIND STREQUAL "shared")
  set(_expected_type SHARED_LIBRARY)
else()
  set(_expected_type STATIC_LIBRARY)
endif()

string(REGEX MATCH "^[0-9]+\\.[0-9]+" _expected_major_minor "${AMARK_EXPECTED_VERSION}")

# What this project has before Amark is taken in, to check that Amark changes none
# of it (1.5).
get_directory_property(_dir_compile_options_before COMPILE_OPTIONS)
get_directory_property(_dir_link_options_before LINK_OPTIONS)
set(_c_flags_before "${CMAKE_C_FLAGS}")

function(_check_under_prefix what path)
  file(REAL_PATH "${path}" _path)
  file(REAL_PATH "${AMARK_EXPECTED_PREFIX}" _prefix)
  string(FIND "${_path}/" "${_prefix}/" _at)
  if(NOT _at EQUAL 0)
    message(FATAL_ERROR "${what} (${_path}) is not under the prefix ${_prefix}.")
  endif()
endfunction()

if(AMARK_CONSUME_MODE STREQUAL "find_package")
  if(AMARK_CONSUME_COMPONENTS)
    find_package(Amark ${_expected_major_minor} REQUIRED
      COMPONENTS ${AMARK_CONSUME_COMPONENTS})
  else()
    find_package(Amark ${_expected_major_minor} REQUIRED)
  endif()
  if(NOT Amark_VERSION STREQUAL AMARK_EXPECTED_VERSION)
    message(FATAL_ERROR
      "Amark_VERSION is \"${Amark_VERSION}\", expected \"${AMARK_EXPECTED_VERSION}\".")
  endif()
  _check_under_prefix("Amark_DIR" "${Amark_DIR}")
  foreach(_kind IN ITEMS static shared)
    if(_kind STREQUAL AMARK_EXPECTED_KIND AND NOT Amark_${_kind}_FOUND)
      message(FATAL_ERROR "Amark_${_kind}_FOUND is false for the installed kind.")
    elseif(NOT _kind STREQUAL AMARK_EXPECTED_KIND AND Amark_${_kind}_FOUND)
      message(FATAL_ERROR "Amark_${_kind}_FOUND is true but it is not installed.")
    endif()
  endforeach()
  set(_amark_target Amark::amark)
  message(STATUS "amark_consumer: found Amark ${Amark_VERSION} in ${Amark_DIR} "
    "(components: \"${AMARK_CONSUME_COMPONENTS}\")")

elseif(AMARK_CONSUME_MODE STREQUAL "pkg_config")
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(amark_pc REQUIRED IMPORTED_TARGET amark=${AMARK_EXPECTED_VERSION})
  _check_under_prefix("The pkg-config prefix" "${amark_pc_PREFIX}")
  if(AMARK_EXPECTED_KIND STREQUAL "shared")
    if(NOT "-DAMARK_SHARED" IN_LIST amark_pc_CFLAGS)
      message(FATAL_ERROR "pkg-config gives no -DAMARK_SHARED for the shared "
        "library (Cflags: ${amark_pc_CFLAGS}).")
    endif()
  elseif("-DAMARK_SHARED" IN_LIST amark_pc_CFLAGS)
    message(FATAL_ERROR "pkg-config gives -DAMARK_SHARED for the static library.")
  endif()
  set(_amark_target PkgConfig::amark_pc)
  message(STATUS "amark_consumer: pkg-config amark ${amark_pc_VERSION}: "
    "Cflags \"${amark_pc_CFLAGS}\", Libs \"${amark_pc_LDFLAGS}\"")

else()
  if(NOT IS_DIRECTORY "${AMARK_SOURCE_DIR}")
    message(FATAL_ERROR "AMARK_SOURCE_DIR is not a directory: \"${AMARK_SOURCE_DIR}\".")
  endif()
  if(AMARK_CONSUME_MODE STREQUAL "add_subdirectory")
    add_subdirectory("${AMARK_SOURCE_DIR}" amark-build)
  else()
    include(FetchContent)
    FetchContent_Declare(Amark SOURCE_DIR "${AMARK_SOURCE_DIR}")
    FetchContent_MakeAvailable(Amark)
  endif()

  # Naos is not fetched (3.3).
  include(FetchContent)
  FetchContent_GetProperties(Naos)
  if(naos_POPULATED OR TARGET Naos::naos OR TARGET naos
     OR EXISTS "${FETCHCONTENT_BASE_DIR}/naos-src")
    message(FATAL_ERROR "Naos was fetched by the embedded Amark.")
  endif()
  # The tests, install rules and fuzzing are off, and warnings are not errors (1.5).
  foreach(_option IN ITEMS AMARK_BUILD_TESTS AMARK_PACKAGE_TESTS AMARK_INSTALL
                            AMARK_FUZZ AMARK_WARNINGS_AS_ERRORS)
    if(${_option})
      message(FATAL_ERROR "${_option} is on in the embedded Amark.")
    endif()
  endforeach()
  if(TARGET amark_internal)
    message(FATAL_ERROR "amark_internal (tests or fuzzing) is defined.")
  endif()
  foreach(_target IN ITEMS amark amark_objects)
    if(NOT TARGET ${_target})
      continue()
    endif()
    foreach(_property IN ITEMS COMPILE_OPTIONS INTERFACE_COMPILE_OPTIONS)
      get_target_property(_options ${_target} ${_property})
      if(_options MATCHES "-Werror|/WX")
        message(FATAL_ERROR
          "${_property} of ${_target} turns warnings into errors: ${_options}")
      endif()
    endforeach()
  endforeach()
  get_target_property(_amark_type amark TYPE)
  if(NOT _amark_type STREQUAL _expected_type)
    message(FATAL_ERROR "amark is a ${_amark_type}, expected ${_expected_type}.")
  endif()
  set(_amark_target Amark::amark)
  message(STATUS "amark_consumer: embedded Amark checks passed")
endif()

# Amark changes neither the compile and link options of this directory nor the
# C flags (1.5).
get_directory_property(_dir_compile_options_after COMPILE_OPTIONS)
get_directory_property(_dir_link_options_after LINK_OPTIONS)
if(NOT "${_dir_compile_options_after}" STREQUAL "${_dir_compile_options_before}"
   OR NOT "${_dir_link_options_after}" STREQUAL "${_dir_link_options_before}"
   OR NOT "${CMAKE_C_FLAGS}" STREQUAL "${_c_flags_before}")
  message(FATAL_ERROR "Taking in Amark changed the options of the consumer: "
    "COMPILE_OPTIONS \"${_dir_compile_options_after}\", "
    "LINK_OPTIONS \"${_dir_link_options_after}\", CMAKE_C_FLAGS \"${CMAKE_C_FLAGS}\".")
endif()

if(AMARK_CONSUME_MODE STREQUAL "find_package")
  get_target_property(_amark_type Amark::amark TYPE)
  if(NOT _amark_type STREQUAL _expected_type)
    message(FATAL_ERROR "Amark::amark is a ${_amark_type}, expected ${_expected_type}.")
  endif()
endif()

add_executable(amark_consumer main.c)
target_link_libraries(amark_consumer PRIVATE ${_amark_target})
target_compile_definitions(amark_consumer PRIVATE
  "AMARK_CONSUMER_EXPECTED_VERSION=\"${AMARK_EXPECTED_VERSION}\"")
set_target_properties(amark_consumer PROPERTIES
  C_STANDARD 99
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF
  # The generator expression keeps multi-config generators from adding a
  # per-configuration subdirectory, so the driver finds the executable in bin/.
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin$<0:>"
)

if(AMARK_CONSUMER_SANITIZE)
  string(REPLACE "," ";" _sanitizers "${AMARK_CONSUMER_SANITIZE}")
  if(MSVC)
    target_compile_options(amark_consumer PRIVATE /fsanitize=address)
  else()
    list(JOIN _sanitizers "," _joined)
    target_compile_options(amark_consumer PRIVATE
      "-fsanitize=${_joined}" -fno-omit-frame-pointer)
    target_link_options(amark_consumer PRIVATE "-fsanitize=${_joined}")
  endif()
endif()

# A shared Amark on Windows: copy the DLL next to the executable, so that it runs
# without changing PATH. pkg-config gives no DLL; the driver adds <prefix>/bin to
# PATH for that mode instead.
if(WIN32 AND AMARK_EXPECTED_KIND STREQUAL "shared"
   AND NOT AMARK_CONSUME_MODE STREQUAL "pkg_config")
  add_custom_command(TARGET amark_consumer POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "$<TARGET_RUNTIME_DLLS:amark_consumer>" "$<TARGET_FILE_DIR:amark_consumer>"
    COMMAND_EXPAND_LISTS
  )
endif()
