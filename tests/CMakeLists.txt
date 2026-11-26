# SPDX-License-Identifier: MIT
# Copyright (c) The Amark Authors
#
# The tests of Amark. This directory is added when AMARK_BUILD_TESTS or AMARK_FUZZ
# is on (FuzzInfra's rule for fuzzer support libraries):
#   - Targets that depend on Naos (amark_test_support, amark_test_tree, the check
#     tests) are defined only inside if(AMARK_BUILD_TESTS). A fuzz-only
#     configuration has no Naos.
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

  # amark_test_tree: building trees from S-expressions (support/tree_builder.h,
  # amark_test_tree_build and AMARK_CHECK_TREE_DUMP). It calls the public API of
  # Amark, so it cannot go into amark_test_support: tree_builder.c is an INTERFACE
  # source, compiled with the settings of each test that links it (with amark or
  # amark_internal). It uses the Naos macros, so fuzz targets must not link it.
  add_library(amark_test_tree INTERFACE)
  target_sources(amark_test_tree INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/support/tree_builder.c")
  target_include_directories(amark_test_tree INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/support")
  target_link_libraries(amark_test_tree INTERFACE Naos::naos)

  # The test allocator and the sweep, built twice: with amark and with
  # amark_internal, so that a duplicate symbol in either combination fails to link.
  amark_add_test(test_allocator_test SOURCES support/test_allocator_test.c)
  amark_add_test(test_allocator_internal_test SOURCES support/test_allocator_test.c INTERNAL)

  # The license header check (8.3, 8.4, label "lint"): every C source and header of
  # the source tree begins with the two-line license header. It needs no build; see
  # cmake/scripts/check_license_headers.cmake for what it scans.
  add_test(NAME lint.license_headers
    COMMAND "${CMAKE_COMMAND}"
      "-DAMARK_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
      -P "${PROJECT_SOURCE_DIR}/cmake/scripts/check_license_headers.cmake")
  set_tests_properties(lint.license_headers PROPERTIES LABELS lint)

  # The export check of the shared library (2.4, 4.10, 4.12, 9.3, label "abi"):
  # every symbol the shared library exports is an amark_ function declared with
  # AMARK_API in include/amark/. Registered only for the shared library; see
  # cmake/scripts/check_exported_symbols.cmake for the listing tools. Without a
  # listing tool it prints "AMARK_SKIP:" and CTest reports it as skipped.
  if(BUILD_SHARED_LIBS)
    if(WIN32 OR CYGWIN)
      set(_amark_abi_format PE)
      # dumpbin is next to the MSVC linker; llvm-readobj comes with LLVM.
      set(_amark_abi_hints "")
      if(CMAKE_LINKER)
        get_filename_component(_amark_abi_linker_dir "${CMAKE_LINKER}" DIRECTORY)
        list(APPEND _amark_abi_hints "${_amark_abi_linker_dir}")
        unset(_amark_abi_linker_dir)
      endif()
      if(CMAKE_C_COMPILER)
        get_filename_component(_amark_abi_compiler_dir "${CMAKE_C_COMPILER}" DIRECTORY)
        list(APPEND _amark_abi_hints "${_amark_abi_compiler_dir}")
        unset(_amark_abi_compiler_dir)
      endif()
      find_program(AMARK_DUMPBIN NAMES dumpbin HINTS ${_amark_abi_hints})
      find_program(AMARK_LLVM_READOBJ NAMES llvm-readobj HINTS ${_amark_abi_hints})
      mark_as_advanced(AMARK_DUMPBIN AMARK_LLVM_READOBJ)
      unset(_amark_abi_hints)
    elseif(APPLE)
      set(_amark_abi_format MACHO)
    else()
      set(_amark_abi_format ELF)
    endif()
    add_test(NAME abi.exported_symbols
      COMMAND "${CMAKE_COMMAND}"
        "-DLIBRARY=$<TARGET_FILE:amark>"
        "-DFORMAT=${_amark_abi_format}"
        "-DHEADER_DIR=${PROJECT_SOURCE_DIR}/include/amark"
        "-DNM=${CMAKE_NM}"
        "-DDUMPBIN=${AMARK_DUMPBIN}"
        "-DLLVM_READOBJ=${AMARK_LLVM_READOBJ}"
        "-DOBJDUMP=${CMAKE_OBJDUMP}"
        -P "${PROJECT_SOURCE_DIR}/cmake/scripts/check_exported_symbols.cmake")
    set_tests_properties(abi.exported_symbols PROPERTIES
      LABELS abi
      SKIP_REGULAR_EXPRESSION "AMARK_SKIP:")
    unset(_amark_abi_format)
  endif()
endif()

add_subdirectory(base)
add_subdirectory(tree)
add_subdirectory(testgen)

# The checks of the public headers (amark_header_check, cxx_include_test,
# lint.public_headers). They need the tests (Naos), not the fuzz targets.
if(AMARK_BUILD_TESTS)
  add_subdirectory(headers)
endif()

# The package consumption tests (label "package"). They configure and build nested
# projects, so AMARK_PACKAGE_TESTS can turn them off on their own.
if(AMARK_BUILD_TESTS AND AMARK_PACKAGE_TESTS)
  add_subdirectory(package)
endif()
