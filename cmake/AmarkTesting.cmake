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
#
# amark_add_generated_tests(<target>
#     GENERATOR <script.cmake>
#     DATA <file>...
#     [SOURCES <source>...]      # hand-written suites (no NAOS_MAIN)
#     [EXTRA_SUITES <suite>...]  # the names of the suites in SOURCES
#     [ARGS <name>=<value>...]   # passed to the generator with -D
#     [INTERNAL] [LIBRARIES <library>...] [LABELS <label>...])
#   Creates the test executable <target> with amark_add_test() from sources
#   generated from test data (4.7, 4.8). For each DATA file, the suite name is
#   "<target>_<file name without its last extension>" made a C identifier
#   (amark_testgen_c_identifier), and a custom command runs at BUILD time
#     cmake -DAMARK_TESTGEN_INPUT=<data> -DAMARK_TESTGEN_OUTPUT=<c>
#           -DAMARK_TESTGEN_SUITE=<suite> -DAMARK_TESTGEN_MODULE=<AmarkTestGen.cmake>
#           [-D<ARGS>...] -P <script.cmake>
#   to write ${CMAKE_CURRENT_BINARY_DIR}/<target>_gen/<suite>.c, which must
#   define the suite <suite> with NAOS_SUITE (<suite>.stamp beside it records the
#   run). It runs again when the data, the generator or AmarkTestGen.cmake
#   changes; the generator script starts with cmake_minimum_required(VERSION 3.21)
#   and includes ${AMARK_TESTGEN_MODULE}. At CONFIGURE time, the main
#   ${CMAKE_CURRENT_BINARY_DIR}/<target>_gen/main.c is written: it declares and
#   adds the generated suites and the EXTRA_SUITES, in that order. The list of
#   data files is fixed at configure time; a change to the content of a data file
#   needs only a build, which regenerates the suite, and the tests are registered
#   again after the build (Naos lists them from the executable). Relative paths
#   are relative to the current source directory. Does nothing when
#   AMARK_BUILD_TESTS is off.

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

function(amark_add_generated_tests target)
  if(NOT AMARK_BUILD_TESTS)
    return()
  endif()

  cmake_parse_arguments(PARSE_ARGV 1 arg "INTERNAL" "GENERATOR"
    "DATA;SOURCES;EXTRA_SUITES;ARGS;LIBRARIES;LABELS")
  if(arg_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR
      "amark_add_generated_tests(${target}): unknown arguments: ${arg_UNPARSED_ARGUMENTS}")
  endif()
  if(NOT arg_GENERATOR)
    message(FATAL_ERROR "amark_add_generated_tests(${target}): GENERATOR is required")
  endif()
  if(NOT arg_DATA)
    message(FATAL_ERROR "amark_add_generated_tests(${target}): DATA is required")
  endif()

  get_filename_component(generator "${arg_GENERATOR}" ABSOLUTE)
  if(NOT EXISTS "${generator}")
    message(FATAL_ERROR
      "amark_add_generated_tests(${target}): no generator '${arg_GENERATOR}'")
  endif()
  set(module "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/AmarkTestGen.cmake")
  set(gen_dir "${CMAKE_CURRENT_BINARY_DIR}/${target}_gen")
  file(MAKE_DIRECTORY "${gen_dir}")

  set(defines "")
  foreach(definition IN LISTS arg_ARGS)
    if(NOT definition MATCHES "^[A-Za-z_][A-Za-z0-9_]*=")
      message(FATAL_ERROR
        "amark_add_generated_tests(${target}): ARGS must be <name>=<value>: '${definition}'")
    endif()
    list(APPEND defines "-D${definition}")
  endforeach()

  set(suites "")
  set(generated "")
  foreach(data IN LISTS arg_DATA)
    get_filename_component(data "${data}" ABSOLUTE)
    if(NOT EXISTS "${data}" OR IS_DIRECTORY "${data}")
      message(FATAL_ERROR "amark_add_generated_tests(${target}): no data file '${data}'")
    endif()
    # The same conversion as amark_testgen_c_identifier() in AmarkTestGen.cmake.
    get_filename_component(stem "${data}" NAME_WLE)
    string(REGEX REPLACE "[^A-Za-z0-9_]" "_" suite "${target}_${stem}")
    if(suite MATCHES "^[0-9]")
      set(suite "_${suite}")
    endif()
    if(suite IN_LIST suites OR suite IN_LIST arg_EXTRA_SUITES)
      message(FATAL_ERROR
        "amark_add_generated_tests(${target}): two suites are named '${suite}' ('${data}')")
    endif()
    list(APPEND suites "${suite}")

    # The generator leaves an unchanged source as it is (amark_testgen_write), so
    # that it is not compiled again; the stamp records the run, so that make does
    # not run the generator again on every build (ninja restats the byproduct).
    set(output "${gen_dir}/${suite}.c")
    set(stamp "${gen_dir}/${suite}.stamp")
    file(RELATIVE_PATH shown "${PROJECT_SOURCE_DIR}" "${data}")
    add_custom_command(
      OUTPUT "${stamp}"
      BYPRODUCTS "${output}"
      COMMAND "${CMAKE_COMMAND}"
        "-DAMARK_TESTGEN_INPUT=${data}"
        "-DAMARK_TESTGEN_OUTPUT=${output}"
        "-DAMARK_TESTGEN_SUITE=${suite}"
        "-DAMARK_TESTGEN_MODULE=${module}"
        ${defines}
        -P "${generator}"
      COMMAND "${CMAKE_COMMAND}" -E touch "${stamp}"
      DEPENDS "${data}" "${generator}" "${module}"
      COMMENT "Generating the tests of ${target} from ${shown}"
      VERBATIM
    )
    list(APPEND generated "${stamp}" "${output}")
  endforeach()

  # The main, at configure time: the suites follow the list of data files, which
  # only a configure changes. file(CONFIGURE) leaves an unchanged file as it is.
  set(declarations "")
  set(additions "")
  foreach(suite IN LISTS suites arg_EXTRA_SUITES)
    string(APPEND declarations "NAOS_DECLARE_SUITE(${suite});\n")
    if(NOT additions STREQUAL "")
      string(APPEND additions ",\n")
    endif()
    string(APPEND additions "    NAOS_ADD_SUITE(${suite})")
  endforeach()
  set(main "${gen_dir}/main.c")
  file(CONFIGURE OUTPUT "${main}" CONTENT "/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/* Generated by amark_add_generated_tests(${target}). Do not edit. */
#include <naos/naos.h>

${declarations}
NAOS_MAIN(
${additions}
)
" @ONLY)

  set(internal "")
  if(arg_INTERNAL)
    set(internal INTERNAL)
  endif()
  amark_add_test("${target}"
    SOURCES "${main}" ${generated} ${arg_SOURCES}
    ${internal}
    LIBRARIES ${arg_LIBRARIES}
    LABELS ${arg_LABELS}
  )
endfunction()
