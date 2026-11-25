# SPDX-License-Identifier: MIT
# Copyright (c) The Amark Authors
#
# Compiler settings of the C targets Amark builds, and the sanitizers of the build.
#
# Include this module after project() and after the options below are declared,
# and before the targets that the sanitizers should cover are created.
#
# Variables read (normally the options of the top-level CMakeLists.txt):
#   AMARK_WARNINGS_AS_ERRORS  Treat warnings as errors. When it is not defined,
#                             the default is PROJECT_IS_TOP_LEVEL: on when Amark is
#                             the top-level project, off when it is embedded.
#   AMARK_SANITIZE            A list of sanitizers, from "address" and "undefined".
#                             Used only when Amark is the top-level project; it is
#                             ignored when Amark is embedded in another project.
#
# amark_target_defaults(<target>)
#   Applies to <target>, as PRIVATE settings that are not passed to its consumers:
#     C99 with C_STANDARD_REQUIRED and without compiler extensions; /std:c11 and
#     /utf-8 on MSVC, which has no C99 mode.
#     Warnings: -Wall -Wextra -Wpedantic on GCC and Clang, /W4 on MSVC, plus -Werror
#     or /WX when AMARK_WARNINGS_AS_ERRORS is on.
#     C_VISIBILITY_PRESET hidden and VISIBILITY_INLINES_HIDDEN.
#
# Sanitizers (when this module is included):
#   When Amark is the top-level project and AMARK_SANITIZE is not empty, adds
#   -fsanitize=<list> -fno-omit-frame-pointer -fno-sanitize-recover=all to the
#   compile and link options of the including directory, so that every target
#   created after the include, including the dependencies fetched for the tests,
#   is instrumented. MSVC accepts only "address" (/fsanitize=address). An unknown
#   value, a value the compiler does not support, or a compiler that cannot
#   build and link with the sanitizers stops the configuration with FATAL_ERROR.

include_guard(DIRECTORY)

if(NOT DEFINED PROJECT_IS_TOP_LEVEL)
  message(FATAL_ERROR "AmarkCompilerOptions.cmake must be included after project().")
endif()

function(amark_target_defaults target)
  if(NOT TARGET "${target}")
    message(FATAL_ERROR "amark_target_defaults: \"${target}\" is not a target.")
  endif()

  if(DEFINED AMARK_WARNINGS_AS_ERRORS)
    set(warnings_as_errors "${AMARK_WARNINGS_AS_ERRORS}")
  else()
    set(warnings_as_errors "${PROJECT_IS_TOP_LEVEL}")
  endif()

  set_target_properties("${target}" PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
  )

  if(MSVC)
    target_compile_options("${target}" PRIVATE /std:c11 /utf-8 /W4)
    if(warnings_as_errors)
      target_compile_options("${target}" PRIVATE /WX)
    endif()
  elseif(CMAKE_C_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
    target_compile_options("${target}" PRIVATE -Wall -Wextra -Wpedantic)
    if(warnings_as_errors)
      target_compile_options("${target}" PRIVATE -Werror)
    endif()
  endif()
endfunction()

function(_amark_apply_sanitizers)
  set(sanitizers ${AMARK_SANITIZE})
  list(REMOVE_DUPLICATES sanitizers)
  if(NOT sanitizers)
    return()
  endif()

  foreach(sanitizer IN LISTS sanitizers)
    if(NOT sanitizer MATCHES "^(address|undefined)$")
      message(FATAL_ERROR
        "AMARK_SANITIZE: unknown sanitizer \"${sanitizer}\" "
        "(the values are \"address\" and \"undefined\").")
    endif()
  endforeach()

  set(strip_runtime_checks OFF)
  if(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
    foreach(sanitizer IN LISTS sanitizers)
      if(NOT sanitizer STREQUAL "address")
        message(FATAL_ERROR
          "AMARK_SANITIZE: the sanitizer \"${sanitizer}\" is not supported by MSVC "
          "(MSVC supports only \"address\").")
      endif()
    endforeach()
    set(compile_options /fsanitize=address)
    set(link_options "")
    set(strip_runtime_checks ON)
  elseif(CMAKE_C_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$"
         AND NOT CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    list(JOIN sanitizers "," joined)
    set(compile_options
      "-fsanitize=${joined}" -fno-omit-frame-pointer -fno-sanitize-recover=all)
    set(link_options ${compile_options})
  else()
    message(FATAL_ERROR
      "AMARK_SANITIZE: the compiler \"${CMAKE_C_COMPILER_ID}\" is not supported "
      "for sanitizers (use GCC, Clang or MSVC).")
  endif()

  if(strip_runtime_checks)
    # The runtime checks (/RTC*) of the default MSVC flags cannot be combined
    # with /fsanitize=address. They are removed before the check below, which
    # compiles with these flags, and in the including directory.
    set(flags_vars CMAKE_C_FLAGS)
    set(configs Debug Release RelWithDebInfo MinSizeRel
      ${CMAKE_CONFIGURATION_TYPES} ${CMAKE_BUILD_TYPE})
    foreach(config IN LISTS configs)
      string(TOUPPER "${config}" config)
      list(APPEND flags_vars "CMAKE_C_FLAGS_${config}")
    endforeach()
    list(REMOVE_DUPLICATES flags_vars)
    foreach(flags_var IN LISTS flags_vars)
      if(DEFINED ${flags_var})
        string(REGEX REPLACE "(^| )[/-]RTC[1csu]*" "" flags "${${flags_var}}")
        set(${flags_var} "${flags}")
        set(${flags_var} "${flags}" PARENT_SCOPE)
      endif()
    endforeach()
  endif()

  # The compiler and its runtime must be able to build and link with the options;
  # for example, MinGW-w64 GCC accepts -fsanitize=address but has no runtime for it.
  include(CheckCSourceCompiles)
  string(MAKE_C_IDENTIFIER "${sanitizers}" check_suffix)
  set(CMAKE_REQUIRED_QUIET ON)
  list(JOIN compile_options " " CMAKE_REQUIRED_FLAGS)
  set(CMAKE_REQUIRED_LINK_OPTIONS ${link_options})
  check_c_source_compiles("int main(void) { return 0; }"
    "AMARK_SANITIZE_WORKS_${check_suffix}")
  if(NOT AMARK_SANITIZE_WORKS_${check_suffix})
    message(FATAL_ERROR
      "AMARK_SANITIZE: the compiler \"${CMAKE_C_COMPILER_ID}\" cannot build and "
      "link with the sanitizers \"${sanitizers}\" (${compile_options}).")
  endif()

  add_compile_options(${compile_options})
  if(link_options)
    add_link_options(${link_options})
  endif()
  message(STATUS "Amark: sanitizers enabled: ${sanitizers}")
endfunction()

if(PROJECT_IS_TOP_LEVEL)
  _amark_apply_sanitizers()
elseif(AMARK_SANITIZE)
  message(STATUS
    "Amark: AMARK_SANITIZE is ignored because Amark is not the top-level project.")
endif()
