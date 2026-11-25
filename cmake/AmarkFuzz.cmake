# SPDX-License-Identifier: MIT
# Copyright (c) The Amark Authors
#
# The fuzzing infrastructure of Amark: libFuzzer detection, fuzz targets, corpus
# replay tests and the fuzz smoke target.
#
# Always include this module, after AmarkCompilerOptions.cmake, AmarkTesting.cmake
# and the options AMARK_FUZZ and AMARK_FUZZ_SMOKE_SECONDS, and before the library
# targets are created: its functions are always defined, and the coverage
# instrumentation it adds to the directory must reach the library.
#
# libFuzzer (only when AMARK_FUZZ is on):
#   libFuzzer is available when a small program that defines LLVMFuzzerTestOneInput
#   can be compiled and linked with -fsanitize=fuzzer (AMARK_HAVE_LIBFUZZER, cached).
#   When it is, -fsanitize=fuzzer-no-link is added to the compile and link options
#   of the including directory, so that every target created after the include,
#   the library included, has coverage instrumentation. When it is not (GCC, MSVC,
#   AppleClang, or a Clang without the runtime), the fuzz targets are built with
#   the standalone main instead; this is not an error.
#
# amark_add_fuzzer(<name>
#     SOURCES <source>...      # defines
#                              #   int LLVMFuzzerTestOneInput(const uint8_t *data,
#                              #                              size_t size)
#     CORPUS <directory>       # the seed inputs, one file each
#     [LIBRARIES <library>...])
#   Creates the executable <name> in ${PROJECT_BINARY_DIR}/bin, applies
#   amark_target_defaults(), and links amark_internal (the internal functions can
#   be called, and the library is instrumented with the fuzz target) plus
#   LIBRARIES, and:
#     - with libFuzzer, -fsanitize=fuzzer (a libFuzzer executable);
#     - without libFuzzer (including when AMARK_FUZZ is off), the object library
#       amark_fuzz_standalone (fuzz/standalone_main.c), a main that reads each
#       file given as an argument and calls LLVMFuzzerTestOneInput once with it.
#   When AMARK_BUILD_TESTS is on, whether AMARK_FUZZ is on or off, the files of
#   CORPUS (file(GLOB ... CONFIGURE_DEPENDS), so a new file needs no edit of the
#   CMakeLists) are replayed by the CTest test fuzz.<short>.corpus (label "fuzz"),
#   which runs <name> once with every file as an argument; <short> is <name>
#   without a trailing "_fuzz" (utf8_fuzz -> fuzz.utf8.corpus). CORPUS must hold
#   at least one file. A relative CORPUS or source is relative to the current
#   source directory. Does nothing when neither AMARK_FUZZ nor AMARK_BUILD_TESTS
#   is on.
#   Every element of LIBRARIES must be a target that does not link Naos directly
#   (Naos::naos or naos in its LINK_LIBRARIES or INTERFACE_LINK_LIBRARIES);
#   otherwise the configuration fails with the names of the fuzz target and the
#   library. The check is made in every configuration that adds the fuzz target,
#   so a rule broken in a configuration with the tests fails there and not first
#   in the fuzz-only configuration of CI. Only direct dependencies are checked;
#   the fuzz-only configuration (AMARK_BUILD_TESTS off, no Naos) catches the rest.
#
# Rules for fuzzer support libraries (7.7; every spec that adds fuzz targets
# follows them):
#   - A support library that a fuzz target links with LIBRARIES (for example a
#     later spec's amark_test_gfm) must not depend on Naos: it does not link
#     Naos::naos and does not include Naos headers. Support code that depends on
#     Naos (checks, the test allocator of amark_test_support) goes into a
#     separate target that no fuzz target links.
#   - Such a support library is defined whenever AMARK_FUZZ or AMARK_BUILD_TESTS
#     is on. tests/ and its subdirectories are added in both cases, before fuzz/;
#     targets that depend on Naos (amark_test_support, amark_header_check, the
#     check tests) are defined only inside if(AMARK_BUILD_TESTS). amark_add_test()
#     and amark_add_generated_tests() do nothing when the tests are off, so they
#     can be called unconditionally.
#   - A fuzz target that needs an allocator that injects failures keeps it in its
#     own source and does not link amark_test_support (which links Naos).
#   - The fuzz-only configuration (-DAMARK_FUZZ=ON -DAMARK_BUILD_TESTS=OFF) must
#     configure and build without fetching Naos; the CI fuzz job checks it.
#
# amark_fuzz_smoke (only when libFuzzer is available):
#   A custom target that runs every fuzz target in turn, in the order they were
#   added, for AMARK_FUZZ_SMOKE_SECONDS seconds (-max_total_time), on a copy of its
#   corpus in ${PROJECT_BINARY_DIR}/fuzz_smoke/<name>/corpus; the corpus in the
#   source tree is never written. Crash reproducers are written to
#   ${PROJECT_BINARY_DIR}/fuzz_smoke/<name>/. A crash or a sanitizer report fails
#   the build of the target.

include_guard(DIRECTORY)

function(_amark_fuzz_detect_libfuzzer)
  if(CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"
     OR NOT CMAKE_C_COMPILER_ID MATCHES "^(Clang|AppleClang|GNU)$")
    set(AMARK_HAVE_LIBFUZZER OFF CACHE INTERNAL "libFuzzer is available")
    return()
  endif()
  include(CheckCSourceCompiles)
  set(CMAKE_REQUIRED_QUIET ON)
  set(CMAKE_REQUIRED_FLAGS "-fsanitize=fuzzer")
  set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=fuzzer")
  check_c_source_compiles("
#include <stddef.h>
#include <stdint.h>
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    (void)data;
    (void)size;
    return 0;
}" AMARK_HAVE_LIBFUZZER)
endfunction()

if(AMARK_FUZZ)
  _amark_fuzz_detect_libfuzzer()
  if(AMARK_HAVE_LIBFUZZER)
    add_compile_options(-fsanitize=fuzzer-no-link)
    add_link_options(-fsanitize=fuzzer-no-link)
    message(STATUS "Amark: fuzzing with libFuzzer")
  else()
    message(STATUS "Amark: libFuzzer is not available; the fuzz targets are built "
      "with the standalone main (corpus replay only)")
  endif()
  # The smoke target is created at the end of the including (top-level)
  # directory, when every fuzz target has been added.
  if(AMARK_HAVE_LIBFUZZER)
    cmake_language(DEFER CALL _amark_fuzz_create_smoke_target)
  endif()
endif()

function(_amark_fuzz_check_library name library)
  set(message_prefix "amark_add_fuzzer(${name}): LIBRARIES target '${library}'")
  if(library MATCHES "^(Naos::naos|naos)$")
    message(FATAL_ERROR "${message_prefix} is Naos; fuzz targets must not link "
      "Naos (see the rules for fuzzer support libraries in AmarkFuzz.cmake)")
  endif()
  if(NOT TARGET "${library}")
    message(FATAL_ERROR "${message_prefix} is not defined; fuzzer support libraries "
      "must be defined in tests/ whenever AMARK_FUZZ or AMARK_BUILD_TESTS is ON")
  endif()
  get_target_property(aliased "${library}" ALIASED_TARGET)
  if(aliased)
    set(library_target "${aliased}")
  else()
    set(library_target "${library}")
  endif()
  set(dependencies "")
  foreach(property IN ITEMS LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
    get_target_property(values "${library_target}" ${property})
    if(values)
      list(APPEND dependencies ${values})
    endif()
  endforeach()
  foreach(dependency IN LISTS dependencies)
    # A static library's PRIVATE dependencies appear as $<LINK_ONLY:...>.
    if(dependency MATCHES "^(\\$<LINK_ONLY:)?(Naos::naos|naos)>?$")
      message(FATAL_ERROR "${message_prefix} depends on Naos directly "
        "(${CMAKE_MATCH_2}); fuzzer support libraries must not depend on Naos, so "
        "that the fuzz-only configuration builds without it (see the rules for "
        "fuzzer support libraries in AmarkFuzz.cmake)")
    endif()
  endforeach()
endfunction()

function(amark_add_fuzzer name)
  if(NOT AMARK_FUZZ AND NOT AMARK_BUILD_TESTS)
    return()
  endif()

  cmake_parse_arguments(PARSE_ARGV 1 arg "" "CORPUS" "SOURCES;LIBRARIES")
  if(arg_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR
      "amark_add_fuzzer(${name}): unknown arguments: ${arg_UNPARSED_ARGUMENTS}")
  endif()
  if(NOT arg_SOURCES)
    message(FATAL_ERROR "amark_add_fuzzer(${name}): SOURCES is required")
  endif()
  if(NOT arg_CORPUS)
    message(FATAL_ERROR "amark_add_fuzzer(${name}): CORPUS is required")
  endif()
  get_filename_component(corpus "${arg_CORPUS}" ABSOLUTE)
  if(NOT IS_DIRECTORY "${corpus}")
    message(FATAL_ERROR "amark_add_fuzzer(${name}): no corpus directory '${corpus}'")
  endif()
  file(GLOB corpus_files LIST_DIRECTORIES false CONFIGURE_DEPENDS "${corpus}/*")
  list(SORT corpus_files)
  if(NOT corpus_files)
    # Without arguments a libFuzzer executable would fuzz without a time limit.
    message(FATAL_ERROR "amark_add_fuzzer(${name}): the corpus '${corpus}' is empty")
  endif()

  foreach(library IN LISTS arg_LIBRARIES)
    _amark_fuzz_check_library("${name}" "${library}")
  endforeach()

  add_executable("${name}" ${arg_SOURCES})
  amark_target_defaults("${name}")
  set_target_properties("${name}" PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
  target_link_libraries("${name}" PRIVATE amark_internal ${arg_LIBRARIES})

  if(AMARK_FUZZ AND AMARK_HAVE_LIBFUZZER)
    target_link_options("${name}" PRIVATE -fsanitize=fuzzer)
    set_property(GLOBAL APPEND PROPERTY AMARK_FUZZ_TARGETS "${name}")
    set_property(GLOBAL PROPERTY "AMARK_FUZZ_CORPUS_${name}" "${corpus}")
  else()
    if(NOT TARGET amark_fuzz_standalone)
      add_library(amark_fuzz_standalone OBJECT
        "${PROJECT_SOURCE_DIR}/fuzz/standalone_main.c")
      amark_target_defaults(amark_fuzz_standalone)
    endif()
    target_link_libraries("${name}" PRIVATE amark_fuzz_standalone)
  endif()

  if(AMARK_BUILD_TESTS)
    string(REGEX REPLACE "_fuzz$" "" short "${name}")
    add_test(NAME "fuzz.${short}.corpus" COMMAND "${name}" ${corpus_files})
    set_tests_properties("fuzz.${short}.corpus" PROPERTIES LABELS fuzz)
  endif()
endfunction()

function(_amark_fuzz_create_smoke_target)
  get_property(targets GLOBAL PROPERTY AMARK_FUZZ_TARGETS)
  set(commands "")
  foreach(target IN LISTS targets)
    get_property(corpus GLOBAL PROPERTY "AMARK_FUZZ_CORPUS_${target}")
    set(work "${PROJECT_BINARY_DIR}/fuzz_smoke/${target}")
    list(APPEND commands
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${work}/corpus"
      COMMAND "${CMAKE_COMMAND}" -E copy_directory "${corpus}" "${work}/corpus"
      COMMAND "$<TARGET_FILE:${target}>"
        "-max_total_time=${AMARK_FUZZ_SMOKE_SECONDS}"
        "-artifact_prefix=${work}/"
        "${work}/corpus")
  endforeach()
  if(NOT commands)
    set(commands COMMAND "${CMAKE_COMMAND}" -E echo "Amark: no fuzz targets")
  endif()
  add_custom_target(amark_fuzz_smoke
    ${commands}
    WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
    COMMENT "Running each fuzz target for ${AMARK_FUZZ_SMOKE_SECONDS} seconds"
    USES_TERMINAL
    VERBATIM
  )
  if(targets)
    add_dependencies(amark_fuzz_smoke ${targets})
  endif()
endfunction()
