# SPDX-License-Identifier: MIT
# Copyright (c) The Amark Authors
#
# The library targets:
#   amark_objects  OBJECT library with every source of the library, compiled once.
#                  Later specs add their sources here:
#                    target_sources(amark_objects PRIVATE tree/node.c ...)
#   amark          The static or shared library (BUILD_SHARED_LIBS), and its alias
#                  Amark::amark.
#   amark_internal A static library from the same objects, for the tests and the
#                  fuzz targets that use internal functions. Defined only when
#                  AMARK_BUILD_TESTS or AMARK_FUZZ is on.
# The object library is used through $<TARGET_OBJECTS>, not target_link_libraries,
# so that it does not become part of the exported targets.

include(GNUInstallDirs)

if(BUILD_SHARED_LIBS)
  set(_amark_library_type SHARED)
else()
  set(_amark_library_type STATIC)
endif()

add_library(amark_objects OBJECT)
target_sources(amark_objects PRIVATE
  base/version.c
  base/error.c
)
amark_target_defaults(amark_objects)
target_include_directories(amark_objects PRIVATE
  "${PROJECT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_compile_definitions(amark_objects PRIVATE AMARK_BUILDING)
if(_amark_library_type STREQUAL "SHARED")
  target_compile_definitions(amark_objects PRIVATE AMARK_SHARED)
  set_target_properties(amark_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

add_library(amark ${_amark_library_type} $<TARGET_OBJECTS:amark_objects>)
add_library(Amark::amark ALIAS amark)
target_include_directories(amark PUBLIC
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
set_target_properties(amark PROPERTIES
  LINKER_LANGUAGE C
  C_VISIBILITY_PRESET hidden
  DEBUG_POSTFIX d
)
if(_amark_library_type STREQUAL "SHARED")
  target_compile_definitions(amark INTERFACE AMARK_SHARED)
  set_target_properties(amark PROPERTIES
    VERSION "${PROJECT_VERSION}"
    SOVERSION "${AMARK_SOVERSION}"
  )
elseif(CMAKE_STATIC_LIBRARY_SUFFIX STREQUAL CMAKE_IMPORT_LIBRARY_SUFFIX
       AND CMAKE_STATIC_LIBRARY_PREFIX STREQUAL CMAKE_IMPORT_LIBRARY_PREFIX)
  # MSVC-style toolchains name both the static library and the import library of
  # the DLL amark.lib; a distinct name keeps them from overwriting each other.
  set_target_properties(amark PROPERTIES OUTPUT_NAME amark_static)
endif()
if(AMARK_BUILD_TESTS)
  # The tests are built into the same directory, so they find the DLL on Windows.
  set_target_properties(amark PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
endif()

if(AMARK_BUILD_TESTS OR AMARK_FUZZ)
  # Always static, and without AMARK_SHARED: the declarations seen by the tests are
  # not dllimport, and internal functions can be linked.
  add_library(amark_internal STATIC $<TARGET_OBJECTS:amark_objects>)
  target_include_directories(amark_internal PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}"
  )
  set_target_properties(amark_internal PROPERTIES LINKER_LANGUAGE C)
endif()

unset(_amark_library_type)
