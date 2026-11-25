# SPDX-License-Identifier: MIT
# Copyright (c) The Amark Authors
#
# Checks a release tag against the version of Amark (6.3). Run by the verify job of
# .github/workflows/release.yml before anything is built.
#
#   cmake -DTAG=<tag> [-DAMARK_SOURCE_DIR=<source>] -P check_version_tag.cmake
#
# TAG               The tag of the release, v<major>.<minor>.<patch> (for example
#                   v0.1.0). A full ref name (refs/tags/v0.1.0) is also accepted.
#                   Each number must be a decimal without leading zeros,
#                   (0|[1-9][0-9]*), as in include/amark/version.h.
# AMARK_SOURCE_DIR  The Amark source tree (default: two directories above this
#                   script). Its include/amark/version.h, the single definition of
#                   the version, is read with amark_read_version().
#
# Exit code 0 when the version of the tag equals the version of version.h. A
# malformed tag, or a tag of another version, fails with FATAL_ERROR; a mismatch
# shows both versions.

cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED AMARK_SOURCE_DIR OR AMARK_SOURCE_DIR STREQUAL "")
  get_filename_component(AMARK_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
if(NOT IS_DIRECTORY "${AMARK_SOURCE_DIR}")
  message(FATAL_ERROR "check_version_tag: AMARK_SOURCE_DIR is not a directory: "
    "\"${AMARK_SOURCE_DIR}\"")
endif()

if(NOT DEFINED TAG OR TAG STREQUAL "")
  message(FATAL_ERROR "check_version_tag: TAG is required "
    "(cmake -DTAG=v<major>.<minor>.<patch> -P check_version_tag.cmake).")
endif()

set(_tag "${TAG}")
if(_tag MATCHES "^refs/tags/(.*)$")
  set(_tag "${CMAKE_MATCH_1}")
endif()

set(_number "(0|[1-9][0-9]*)")
if(NOT _tag MATCHES "^v${_number}\\.${_number}\\.${_number}$")
  message(FATAL_ERROR "check_version_tag: the tag \"${TAG}\" is not of the form "
    "v<major>.<minor>.<patch>, each a decimal number without leading zeros "
    "(for example v1.2.3).")
endif()
set(_tag_version "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")

include("${AMARK_SOURCE_DIR}/cmake/AmarkVersion.cmake")
set(_header "${AMARK_SOURCE_DIR}/include/amark/version.h")
amark_read_version("${_header}")

if(NOT _tag_version STREQUAL AMARK_VERSION)
  message(FATAL_ERROR "check_version_tag: the version of the tag \"${_tag}\" "
    "(${_tag_version}) does not match the version of Amark defined in "
    "include/amark/version.h (${AMARK_VERSION}). Nothing is released.")
endif()

message(STATUS "check_version_tag: the tag \"${_tag}\" matches the version "
  "${AMARK_VERSION} of include/amark/version.h")
