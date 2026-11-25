# SPDX-License-Identifier: MIT
# Copyright (c) The Amark Authors
#
# The install rules, the export of the targets, the CMake package configuration
# and the pkg-config file of Amark. Included by the top-level CMakeLists.txt after
# the library targets are defined, and only when AMARK_INSTALL is on.
#
# Installed files (destinations from GNUInstallDirs):
#   <includedir>/amark/*.h                    The public headers.
#   <libdir>/                                 The library (the DLL goes to <bindir>,
#                                             with its PDB for MSVC Debug and
#                                             RelWithDebInfo).
#   <libdir>/cmake/Amark/                     AmarkConfig.cmake,
#                                             AmarkConfigVersion.cmake,
#                                             AmarkTargets.cmake and
#                                             AmarkTargets-<config>.cmake.
#   <libdir>/pkgconfig/amark.pc               The pkg-config file.
#   <datarootdir>/licenses/amark/LICENSE      The license.
#
# One prefix holds one kind of the library (static or shared). The kind is embedded
# in AmarkConfig.cmake, which matches it against the requested components.
# Nothing of the tests (executables, data, Naos) is installed.

include_guard(GLOBAL)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

get_target_property(_amark_target_type amark TYPE)
if(_amark_target_type STREQUAL "SHARED_LIBRARY")
  set(AMARK_PACKAGE_KIND shared)
else()
  set(AMARK_PACKAGE_KIND static)
endif()
unset(_amark_target_type)

set(_amark_cmake_dir "${CMAKE_INSTALL_LIBDIR}/cmake/Amark")

# Library and public headers.
install(TARGETS amark
  EXPORT AmarkTargets
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/amark"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  FILES_MATCHING PATTERN "*.h"
)
if(MSVC AND AMARK_PACKAGE_KIND STREQUAL "shared")
  install(FILES "$<TARGET_PDB_FILE:amark>"
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
    CONFIGURATIONS Debug RelWithDebInfo
    OPTIONAL
  )
endif()

# License.
install(FILES "${PROJECT_SOURCE_DIR}/LICENSE"
  DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/licenses/amark"
)

# CMake package: exported targets, configuration and version.
install(EXPORT AmarkTargets
  NAMESPACE Amark::
  FILE AmarkTargets.cmake
  DESTINATION "${_amark_cmake_dir}"
)
configure_package_config_file(
  "${PROJECT_SOURCE_DIR}/cmake/AmarkConfig.cmake.in"
  "${PROJECT_BINARY_DIR}/cmake/AmarkConfig.cmake"
  INSTALL_DESTINATION "${_amark_cmake_dir}"
)
write_basic_package_version_file(
  "${PROJECT_BINARY_DIR}/cmake/AmarkConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY "${AMARK_PACKAGE_COMPATIBILITY}"
)
install(FILES
  "${PROJECT_BINARY_DIR}/cmake/AmarkConfig.cmake"
  "${PROJECT_BINARY_DIR}/cmake/AmarkConfigVersion.cmake"
  DESTINATION "${_amark_cmake_dir}"
)

# pkg-config. The paths are relative to the location of amark.pc (${pcfiledir}),
# so that the prefix can be moved, unless the install directories are absolute.
set(_amark_pc_dir "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
if(IS_ABSOLUTE "${CMAKE_INSTALL_LIBDIR}")
  set(AMARK_PC_PREFIX "${CMAKE_INSTALL_PREFIX}")
else()
  file(RELATIVE_PATH _amark_pc_up "/amark_root/${_amark_pc_dir}" "/amark_root")
  string(REGEX REPLACE "/$" "" _amark_pc_up "${_amark_pc_up}")
  set(AMARK_PC_PREFIX "\${pcfiledir}/${_amark_pc_up}")
  unset(_amark_pc_up)
endif()
foreach(_amark_dir IN ITEMS LIBDIR INCLUDEDIR)
  if(IS_ABSOLUTE "${CMAKE_INSTALL_${_amark_dir}}")
    set(AMARK_PC_${_amark_dir} "${CMAKE_INSTALL_${_amark_dir}}")
  else()
    set(AMARK_PC_${_amark_dir} "\${prefix}/${CMAKE_INSTALL_${_amark_dir}}")
  endif()
endforeach()
unset(_amark_dir)
if(AMARK_PACKAGE_KIND STREQUAL "shared")
  set(AMARK_PC_CFLAGS "-I\${includedir} -DAMARK_SHARED")
else()
  set(AMARK_PC_CFLAGS "-I\${includedir}")
endif()
# The output name differs per configuration (DEBUG_POSTFIX "d", amark_static on
# MSVC-style toolchains): configure_file fills in the @...@ values, and
# file(GENERATE) fills in the output name of each configuration.
configure_file("${PROJECT_SOURCE_DIR}/cmake/amark.pc.in"
  "${PROJECT_BINARY_DIR}/cmake/amark.pc.genex" @ONLY)
file(GENERATE
  OUTPUT "${PROJECT_BINARY_DIR}/cmake/pkgconfig-$<CONFIG>/amark.pc"
  INPUT "${PROJECT_BINARY_DIR}/cmake/amark.pc.genex"
)
# The files of all configurations are generated at the same time, and install()
# skips a file whose time stamp equals that of the installed one. Removing the
# installed amark.pc first makes it always point to the last installed
# configuration.
if(IS_ABSOLUTE "${_amark_pc_dir}")
  set(_amark_pc_installed "\$ENV{DESTDIR}${_amark_pc_dir}/amark.pc")
else()
  set(_amark_pc_installed "\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${_amark_pc_dir}/amark.pc")
endif()
install(CODE "file(REMOVE \"${_amark_pc_installed}\")")
unset(_amark_pc_installed)
install(FILES "${PROJECT_BINARY_DIR}/cmake/pkgconfig-$<CONFIG>/amark.pc"
  DESTINATION "${_amark_pc_dir}"
)

unset(AMARK_PC_PREFIX)
unset(AMARK_PC_LIBDIR)
unset(AMARK_PC_INCLUDEDIR)
unset(AMARK_PC_CFLAGS)
unset(_amark_pc_dir)
unset(_amark_cmake_dir)
