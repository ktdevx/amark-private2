# SPDX-License-Identifier: MIT
# Copyright (c) The Amark Authors
#
# Reads the version of Amark from include/amark/version.h, the single definition
# of the version, and derives the values the build and the package need.
#
# amark_read_version(<header>)
#   Reads the "#define AMARK_VERSION_MAJOR <n>", "..._MINOR <n>" and "..._PATCH <n>"
#   lines of <header> and sets, in the caller's scope:
#     AMARK_VERSION_MAJOR, AMARK_VERSION_MINOR, AMARK_VERSION_PATCH
#     AMARK_VERSION                The version as "X.Y.Z".
#     AMARK_SOVERSION              "0.<minor>" while the major version is 0,
#                                  otherwise "<major>".
#     AMARK_PACKAGE_COMPATIBILITY  The COMPATIBILITY of the CMake package version
#                                  file: SameMinorVersion while the major version
#                                  is 0, otherwise SameMajorVersion.
#   Each number must be a decimal without leading zeros, (0|[1-9][0-9]*). A missing
#   line or a malformed number stops the configuration with FATAL_ERROR.
#   AMARK_VERSION_STRING is not read: the header builds it from the three numbers.
#
# This file can be included both from a project and in script mode (cmake -P).

include_guard(GLOBAL)

function(amark_read_version header)
  if(NOT EXISTS "${header}")
    message(FATAL_ERROR "amark_read_version: version header not found: ${header}")
  endif()
  file(STRINGS "${header}" _amark_version_lines
       REGEX "^[ \t]*#[ \t]*define[ \t]+AMARK_VERSION_(MAJOR|MINOR|PATCH)([ \t]|$)")

  foreach(_part IN ITEMS MAJOR MINOR PATCH)
    set(_value "")
    set(_found FALSE)
    foreach(_line IN LISTS _amark_version_lines)
      if(_line MATCHES "^[ \t]*#[ \t]*define[ \t]+AMARK_VERSION_${_part}([ \t]|$)")
        if(_found)
          message(FATAL_ERROR
            "amark_read_version: AMARK_VERSION_${_part} is defined more than once in ${header}")
        endif()
        set(_found TRUE)
        if(_line MATCHES "^[ \t]*#[ \t]*define[ \t]+AMARK_VERSION_${_part}[ \t]+(0|[1-9][0-9]*)[ \t]*$")
          set(_value "${CMAKE_MATCH_1}")
        else()
          message(FATAL_ERROR
            "amark_read_version: malformed AMARK_VERSION_${_part} in ${header}: "
            "\"${_line}\". The value must be a decimal number without leading zeros, "
            "parentheses or suffixes (0|[1-9][0-9]*).")
        endif()
      endif()
    endforeach()
    if(NOT _found)
      message(FATAL_ERROR
        "amark_read_version: no \"#define AMARK_VERSION_${_part} <n>\" line in ${header}")
    endif()
    set(_amark_${_part} "${_value}")
  endforeach()

  foreach(_part IN ITEMS MINOR PATCH)
    string(LENGTH "${_amark_${_part}}" _len)
    if(_len GREATER 3)
      message(FATAL_ERROR
        "amark_read_version: AMARK_VERSION_${_part} in ${header} is ${_amark_${_part}}, "
        "but it must be in the range 0-999 (AMARK_VERSION_NUMBER would be ambiguous)")
    endif()
  endforeach()

  if(_amark_MAJOR STREQUAL "0")
    set(_soversion "0.${_amark_MINOR}")
    set(_compat SameMinorVersion)
  else()
    set(_soversion "${_amark_MAJOR}")
    set(_compat SameMajorVersion)
  endif()

  set(AMARK_VERSION_MAJOR "${_amark_MAJOR}" PARENT_SCOPE)
  set(AMARK_VERSION_MINOR "${_amark_MINOR}" PARENT_SCOPE)
  set(AMARK_VERSION_PATCH "${_amark_PATCH}" PARENT_SCOPE)
  set(AMARK_VERSION "${_amark_MAJOR}.${_amark_MINOR}.${_amark_PATCH}" PARENT_SCOPE)
  set(AMARK_SOVERSION "${_soversion}" PARENT_SCOPE)
  set(AMARK_PACKAGE_COMPATIBILITY "${_compat}" PARENT_SCOPE)
endfunction()
