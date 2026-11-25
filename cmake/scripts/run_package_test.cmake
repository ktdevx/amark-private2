# SPDX-License-Identifier: MIT
# Copyright (c) The Amark Authors
#
# Runs one package consumption test (4.11): takes Amark into the consumer project
# tests/package/consumer in one way, configures and builds it, and runs it.
# Registered by tests/package/CMakeLists.txt as package.<mode> (label "package").
#
#   cmake -DMODE=<mode> -DAMARK_SOURCE_DIR=<source> -DWORK_DIR=<dir>
#         [-DAMARK_BINARY_DIR=<build>] [-DPREFIX=<prefix>]
#         [-DBUILD_SHARED_LIBS=ON|OFF] [-DCONFIG=<config>]
#         [-DGENERATOR=<generator>] [-DGENERATOR_PLATFORM=<platform>]
#         [-DGENERATOR_TOOLSET=<toolset>] [-DMAKE_PROGRAM=<program>]
#         [-DC_COMPILER=<compiler>] [-DOSX_ARCHITECTURES=<a>,<b>]
#         [-DSANITIZE=<sanitizer>,...] [-DPKG_CONFIG_EXECUTABLE=<program>]
#         -P run_package_test.cmake
#
# Modes:
#   find_package               find_package(Amark <major>.<minor> REQUIRED), once
#                              without components (with the other kind in
#                              BUILD_SHARED_LIBS, which must not matter; 3.6) and
#                              once with the installed kind as a component (3.2).
#   find_package_wrong_variant find_package(Amark REQUIRED COMPONENTS <other kind>)
#                              must fail to configure as "NOT FOUND", with a reason
#                              that names the requested and the installed kinds
#                              (3.7).
#   pkg_config                 pkg-config with PKG_CONFIG_PATH set to the prefix
#                              (3.4). Without pkg-config, prints
#                              "AMARK_SKIP: pkg-config not found" and succeeds,
#                              which CTest reports as skipped (4.12).
#   add_subdirectory           add_subdirectory(<source>) (3.3, 1.5).
#   fetchcontent               FetchContent_Declare(Amark SOURCE_DIR <source>),
#                              with FETCHCONTENT_FULLY_DISCONNECTED=ON: no network
#                              (3.3, 1.5).
#
# Inputs:
#   AMARK_SOURCE_DIR   The Amark source tree; its include/amark/version.h gives the
#                      expected version.
#   WORK_DIR           Removed and recreated on every run. Holds prefix/ (the
#                      install prefix), and one build directory per consumer run.
#   AMARK_BINARY_DIR   The Amark build tree to install from (find_package,
#                      find_package_wrong_variant, pkg_config), with
#                      cmake --install <build> --prefix <WORK_DIR>/prefix
#                      [--config <CONFIG>]. Not used with PREFIX.
#   PREFIX             An existing install prefix to test instead: the install is
#                      skipped. For example, to check the contents of a release
#                      archive extracted to /tmp/amark:
#                        cmake -DMODE=find_package -DPREFIX=/tmp/amark
#                              -DBUILD_SHARED_LIBS=ON -DCONFIG=Release
#                              -DAMARK_SOURCE_DIR=. -DWORK_DIR=/tmp/amark-test
#                              -P cmake/scripts/run_package_test.cmake
#   BUILD_SHARED_LIBS  The kind under test: ON for shared, OFF (default) for static.
#                      The kind installed in PREFIX, or built in AMARK_BINARY_DIR.
#   CONFIG             The configuration (empty: the generator's default). Used for
#                      the install and for the consumer build.
#   GENERATOR, GENERATOR_PLATFORM, GENERATOR_TOOLSET, MAKE_PROGRAM, C_COMPILER,
#   OSX_ARCHITECTURES  Passed to the consumer configuration when not empty, so that
#                      it uses the toolchain of the Amark build.
#   SANITIZE           Sanitizers of the Amark build, joined with ","; applied to
#                      the consumer executable in the install-based modes.
#   PKG_CONFIG_EXECUTABLE  The pkg-config program; searched for (pkg-config,
#                      pkgconf) when not given. A given program that does not exist
#                      counts as "not found".
#
# Output: exit code 0 on success. On failure, the output of the nested
# configuration, build and run is shown as is, followed by a FATAL_ERROR naming the
# step.

cmake_minimum_required(VERSION 3.21)

set(_modes find_package find_package_wrong_variant pkg_config add_subdirectory
  fetchcontent)
if(NOT MODE IN_LIST _modes)
  message(FATAL_ERROR "run_package_test: MODE must be one of ${_modes}, not \"${MODE}\".")
endif()
foreach(_var IN ITEMS AMARK_SOURCE_DIR WORK_DIR)
  if(NOT ${_var})
    message(FATAL_ERROR "run_package_test: ${_var} is required.")
  endif()
endforeach()
file(REAL_PATH "${AMARK_SOURCE_DIR}" AMARK_SOURCE_DIR)
set(_consumer_source "${AMARK_SOURCE_DIR}/tests/package/consumer")

include("${AMARK_SOURCE_DIR}/cmake/AmarkVersion.cmake")
amark_read_version("${AMARK_SOURCE_DIR}/include/amark/version.h")

if(BUILD_SHARED_LIBS)
  set(BUILD_SHARED_LIBS ON)
  set(_kind shared)
  set(_other_kind static)
else()
  set(BUILD_SHARED_LIBS OFF)
  set(_kind static)
  set(_other_kind shared)
endif()

set(_install_modes find_package find_package_wrong_variant pkg_config)

# pkg-config is looked for before anything is done, so that a missing tool is a
# skip and not a failure (4.12).
if(MODE STREQUAL "pkg_config")
  if(PKG_CONFIG_EXECUTABLE)
    if(NOT EXISTS "${PKG_CONFIG_EXECUTABLE}")
      find_program(_pkg_config_found NAMES "${PKG_CONFIG_EXECUTABLE}" NO_CACHE)
      if(_pkg_config_found)
        set(PKG_CONFIG_EXECUTABLE "${_pkg_config_found}")
      else()
        set(PKG_CONFIG_EXECUTABLE "")
      endif()
    endif()
  else()
    find_program(PKG_CONFIG_EXECUTABLE NAMES pkg-config pkgconf NO_CACHE)
  endif()
  if(NOT PKG_CONFIG_EXECUTABLE)
    message("AMARK_SKIP: pkg-config not found")
    return()
  endif()
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

# The install prefix.
if(MODE IN_LIST _install_modes)
  if(PREFIX)
    file(REAL_PATH "${PREFIX}" _prefix)
    if(NOT IS_DIRECTORY "${_prefix}")
      message(FATAL_ERROR "run_package_test: PREFIX is not a directory: ${_prefix}")
    endif()
    message(STATUS "run_package_test: using the existing prefix ${_prefix}")
  else()
    if(NOT AMARK_BINARY_DIR)
      message(FATAL_ERROR "run_package_test: AMARK_BINARY_DIR or PREFIX is required "
        "for MODE=${MODE}.")
    endif()
    set(_prefix "${WORK_DIR}/prefix")
    set(_install_command "${CMAKE_COMMAND}" --install "${AMARK_BINARY_DIR}"
      --prefix "${_prefix}")
    if(CONFIG)
      list(APPEND _install_command --config "${CONFIG}")
    endif()
    message(STATUS "run_package_test: installing to ${_prefix}")
    execute_process(COMMAND ${_install_command} RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0)
      message(FATAL_ERROR "run_package_test: the install failed (${_result}).")
    endif()
  endif()
endif()

# The arguments every consumer configuration gets.
set(_configure_args
  "-DAMARK_EXPECTED_VERSION=${AMARK_VERSION}"
  "-DAMARK_EXPECTED_KIND=${_kind}"
)
if(GENERATOR)
  list(APPEND _configure_args -G "${GENERATOR}")
endif()
if(GENERATOR_PLATFORM)
  list(APPEND _configure_args -A "${GENERATOR_PLATFORM}")
endif()
if(GENERATOR_TOOLSET)
  list(APPEND _configure_args -T "${GENERATOR_TOOLSET}")
endif()
if(MAKE_PROGRAM)
  list(APPEND _configure_args "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()
if(C_COMPILER)
  list(APPEND _configure_args "-DCMAKE_C_COMPILER=${C_COMPILER}")
endif()
if(OSX_ARCHITECTURES)
  string(REPLACE "," ";" _archs "${OSX_ARCHITECTURES}")
  list(APPEND _configure_args "-DCMAKE_OSX_ARCHITECTURES=${_archs}")
endif()
if(CONFIG)
  list(APPEND _configure_args "-DCMAKE_BUILD_TYPE=${CONFIG}")
endif()
if(MODE IN_LIST _install_modes)
  list(APPEND _configure_args
    "-DAMARK_EXPECTED_PREFIX=${_prefix}"
    "-DAMARK_CONSUMER_SANITIZE=${SANITIZE}"
    # Only the prefix under test: no package registry, no system package.
    -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF
    -DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF
  )
else()
  list(APPEND _configure_args
    "-DAMARK_SOURCE_DIR=${AMARK_SOURCE_DIR}"
    -DFETCHCONTENT_FULLY_DISCONNECTED=ON
  )
endif()

if(CMAKE_HOST_WIN32)
  set(_exe_suffix ".exe")
else()
  set(_exe_suffix "")
endif()

# Configures the consumer in <WORK_DIR>/<name> with the common arguments and
# ARGN. Sets <result_var> to the exit code and, with CAPTURE, <output_var> to the
# output (otherwise the output is shown as it comes).
function(_configure_consumer name result_var)
  cmake_parse_arguments(PARSE_ARGV 2 arg "" "CAPTURE" "ARGS")
  set(build_dir "${WORK_DIR}/${name}")
  message(STATUS "run_package_test: configuring the consumer (${name})")
  set(command "${CMAKE_COMMAND}" -S "${_consumer_source}" -B "${build_dir}"
    ${_configure_args} ${arg_ARGS})
  if(arg_CAPTURE)
    execute_process(COMMAND ${command}
      RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
    set(${arg_CAPTURE} "${output}" PARENT_SCOPE)
  else()
    execute_process(COMMAND ${command} RESULT_VARIABLE result)
  endif()
  set(${result_var} "${result}" PARENT_SCOPE)
endfunction()

# Configures (with ARGN, "ARGS <arg>..."), builds and runs the consumer in
# <WORK_DIR>/<name>; any failure stops the test.
function(_run_consumer name)
  _configure_consumer("${name}" result ${ARGN})
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "run_package_test: configuring the consumer (${name}) failed.")
  endif()

  set(build_dir "${WORK_DIR}/${name}")
  set(command "${CMAKE_COMMAND}" --build "${build_dir}")
  if(CONFIG)
    list(APPEND command --config "${CONFIG}")
  endif()
  message(STATUS "run_package_test: building the consumer (${name})")
  execute_process(COMMAND ${command} RESULT_VARIABLE result)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "run_package_test: building the consumer (${name}) failed.")
  endif()

  set(executable "${build_dir}/bin/amark_consumer${_exe_suffix}")
  if(NOT EXISTS "${executable}")
    message(FATAL_ERROR "run_package_test: the consumer was not built: ${executable}")
  endif()
  message(STATUS "run_package_test: running ${executable}")
  execute_process(COMMAND "${executable}" RESULT_VARIABLE result)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "run_package_test: the consumer (${name}) failed (${result}).")
  endif()
endfunction()

if(MODE STREQUAL "find_package")
  # Without components: the installed kind, whatever BUILD_SHARED_LIBS says (3.6).
  if(_kind STREQUAL "shared")
    set(_other_shared OFF)
  else()
    set(_other_shared ON)
  endif()
  _run_consumer(no_components ARGS
    -DAMARK_CONSUME_MODE=find_package
    "-DCMAKE_PREFIX_PATH=${_prefix}"
    "-DBUILD_SHARED_LIBS=${_other_shared}")
  # With the installed kind as a component.
  _run_consumer(with_component ARGS
    -DAMARK_CONSUME_MODE=find_package
    "-DCMAKE_PREFIX_PATH=${_prefix}"
    "-DAMARK_CONSUME_COMPONENTS=${_kind}")

elseif(MODE STREQUAL "find_package_wrong_variant")
  _configure_consumer(wrong_variant _result CAPTURE _output ARGS
    -DAMARK_CONSUME_MODE=find_package
    "-DCMAKE_PREFIX_PATH=${_prefix}"
    "-DAMARK_CONSUME_COMPONENTS=${_other_kind}")
  message("${_output}")
  if(_result EQUAL 0)
    message(FATAL_ERROR "run_package_test: requesting the \"${_other_kind}\" kind "
      "from a prefix with the \"${_kind}\" kind configured successfully; it must "
      "fail as not found.")
  endif()
  # The message of CMake for a package that sets Amark_FOUND to FALSE, and the
  # reason given by AmarkConfig.cmake, which names both kinds. Line breaks in the
  # output are folded first.
  string(REGEX REPLACE "[ \t\r\n]+" " " _folded "${_output}")
  foreach(_expected IN ITEMS
      "considered to be NOT FOUND"
      "the requested kind of Amark \\(components: ${_other_kind}\\) is not installed"
      "the installed kind is \"${_kind}\"")
    if(NOT _folded MATCHES "${_expected}")
      message(FATAL_ERROR "run_package_test: the configuration failed, but its "
        "output does not contain \"${_expected}\".")
    endif()
  endforeach()
  message(STATUS "run_package_test: the \"${_other_kind}\" kind is reported as not found")

elseif(MODE STREQUAL "pkg_config")
  # Only the .pc files of the prefix (the libdir can be lib, lib64 or lib/<arch>).
  file(GLOB_RECURSE _pc_files "${_prefix}/amark.pc")
  set(_pc_dirs "")
  foreach(_pc_file IN LISTS _pc_files)
    get_filename_component(_pc_dir "${_pc_file}" DIRECTORY)
    list(APPEND _pc_dirs "${_pc_dir}")
  endforeach()
  if(NOT _pc_dirs)
    message(FATAL_ERROR "run_package_test: no amark.pc in ${_prefix}.")
  endif()
  if(CMAKE_HOST_WIN32)
    list(JOIN _pc_dirs ";" _pc_path)
    # A shared Amark: the DLL is found through PATH.
    set(ENV{PATH} "${_prefix}/bin;$ENV{PATH}")
  else()
    list(JOIN _pc_dirs ":" _pc_path)
  endif()
  set(ENV{PKG_CONFIG_PATH} "${_pc_path}")
  message(STATUS "run_package_test: pkg-config ${PKG_CONFIG_EXECUTABLE}, "
    "PKG_CONFIG_PATH=${_pc_path}")
  _run_consumer(pkg_config ARGS
    -DAMARK_CONSUME_MODE=pkg_config
    "-DPKG_CONFIG_EXECUTABLE=${PKG_CONFIG_EXECUTABLE}")

else()
  # add_subdirectory and fetchcontent: the source tree, built in the kind under
  # test.
  _run_consumer(${MODE} ARGS
    "-DAMARK_CONSUME_MODE=${MODE}"
    "-DBUILD_SHARED_LIBS=${BUILD_SHARED_LIBS}")
endif()

message(STATUS "run_package_test: ${MODE} passed")
