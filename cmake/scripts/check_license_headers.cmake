# SPDX-License-Identifier: MIT
# Copyright (c) The Amark Authors
#
# Checks that every C source and header of the source tree begins with the license
# header (8.3, 8.4). Registered by tests/CMakeLists.txt as lint.license_headers
# (label "lint").
#
#   cmake -DAMARK_SOURCE_DIR=<source> -P check_license_headers.cmake
#
# Scanned: the *.c, *.h, *.cpp and *.inc files under include/, src/, tests/,
# fuzz/, bench/ and tools/ of AMARK_SOURCE_DIR (missing directories are skipped).
# Not scanned: test data and fuzz corpora (tests/**/data/**, fuzz/corpus/**), the
# same directories .gitattributes treats as binary.
#
# The first two lines of each file must be exactly
#
#   /* SPDX-License-Identifier: MIT
#    * Copyright (c) The Amark Authors */
#
# followed by a line break or the end of the file. A CR before a line break is
# accepted (.gitattributes checks the sources out with LF, but a copy made
# otherwise should not fail only for that). Every file without the header is listed,
# then the check fails; it also fails when it finds no file at all.

cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED AMARK_SOURCE_DIR OR NOT IS_DIRECTORY "${AMARK_SOURCE_DIR}")
  message(FATAL_ERROR "check_license_headers: AMARK_SOURCE_DIR is not a directory: "
    "\"${AMARK_SOURCE_DIR}\"")
endif()

set(_line1 "/* SPDX-License-Identifier: MIT")
set(_line2 " * Copyright (c) The Amark Authors */")

set(_roots include src tests fuzz bench tools)
set(_extensions c h cpp inc)

set(_files "")
foreach(_root IN LISTS _roots)
  if(NOT IS_DIRECTORY "${AMARK_SOURCE_DIR}/${_root}")
    continue()
  endif()
  set(_globs "")
  foreach(_ext IN LISTS _extensions)
    list(APPEND _globs "${AMARK_SOURCE_DIR}/${_root}/*.${_ext}")
  endforeach()
  file(GLOB_RECURSE _found LIST_DIRECTORIES false
    RELATIVE "${AMARK_SOURCE_DIR}" ${_globs})
  list(APPEND _files ${_found})
endforeach()

# Test data and fuzz corpora.
list(FILTER _files EXCLUDE REGEX "^tests/(.*/)?data/")
list(FILTER _files EXCLUDE REGEX "^fuzz/corpus/")
list(REMOVE_DUPLICATES _files)
list(SORT _files)

list(LENGTH _files _count)
if(_count EQUAL 0)
  message(FATAL_ERROR "check_license_headers: no C source or header found under "
    "\"${AMARK_SOURCE_DIR}\"")
endif()

# Removes the line break (LF or CRLF) at the start of <var>; sets <ok> to FALSE
# when there is none.
macro(_amark_take_line_break var ok)
  string(SUBSTRING "${${var}}" 0 2 _head)
  if(_head STREQUAL "\r\n")
    string(SUBSTRING "${${var}}" 2 -1 ${var})
    set(${ok} TRUE)
  else()
    string(SUBSTRING "${${var}}" 0 1 _head)
    if(_head STREQUAL "\n")
      string(SUBSTRING "${${var}}" 1 -1 ${var})
      set(${ok} TRUE)
    else()
      set(${ok} FALSE)
    endif()
  endif()
endmacro()

# Returns in <out> whether <text> begins with <prefix>, and removes it.
macro(_amark_take_prefix var prefix out)
  string(LENGTH "${prefix}" _plen)
  string(SUBSTRING "${${var}}" 0 ${_plen} _head)
  if(_head STREQUAL "${prefix}")
    string(SUBSTRING "${${var}}" ${_plen} -1 ${var})
    set(${out} TRUE)
  else()
    set(${out} FALSE)
  endif()
endmacro()

set(_missing "")
foreach(_file IN LISTS _files)
  # The header is 70 bytes; 128 leave room for the line breaks.
  file(READ "${AMARK_SOURCE_DIR}/${_file}" _text LIMIT 128)
  set(_ok FALSE)
  _amark_take_prefix(_text "${_line1}" _ok)
  if(_ok)
    _amark_take_line_break(_text _ok)
  endif()
  if(_ok)
    _amark_take_prefix(_text "${_line2}" _ok)
  endif()
  if(_ok AND NOT _text STREQUAL "")
    _amark_take_line_break(_text _ok)
  endif()
  if(NOT _ok)
    list(APPEND _missing "${_file}")
  endif()
endforeach()

list(LENGTH _missing _missing_count)
if(_missing_count GREATER 0)
  set(_report "")
  foreach(_file IN LISTS _missing)
    string(APPEND _report "  ${_file}\n")
  endforeach()
  message(FATAL_ERROR "check_license_headers: ${_missing_count} of ${_count} "
    "files do not begin with the license header\n"
    "  ${_line1}\n  ${_line2}\n"
    "Files without it:\n${_report}")
endif()

message(STATUS "check_license_headers: all ${_count} files begin with the license header")
