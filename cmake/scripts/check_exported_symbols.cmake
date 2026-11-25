# SPDX-License-Identifier: MIT
# Copyright (c) The Amark Authors
#
# Checks the exports of the shared library (2.4, 4.10, 4.12, 9.3): every exported
# symbol matches ^amark_[a-z0-9][a-z0-9_]*$ and is the name of a function declared
# with AMARK_API in the public headers. Internal functions also begin with amark_,
# so the prefix alone does not tell them apart. Registered by tests/CMakeLists.txt
# as abi.exported_symbols (label "abi") when BUILD_SHARED_LIBS is on.
#
#   cmake -DLIBRARY=<shared library> -DFORMAT=ELF|MACHO|PE
#         -DHEADER_DIR=<source>/include/amark
#         [-DNM=<nm>] [-DDUMPBIN=<dumpbin>] [-DLLVM_READOBJ=<llvm-readobj>]
#         [-DOBJDUMP=<objdump>]
#         -P check_exported_symbols.cmake
#
# The listing tool:
#   ELF    NM -D --defined-only
#   MACHO  NM -gU (the leading "_" of each Mach-O symbol is removed)
#   PE     the first one found of DUMPBIN /exports, LLVM_READOBJ --coff-exports and
#          OBJDUMP -p
# An ELF symbol version ("name@VER", "name@@VER") is removed from the name.
# Without a listing tool, prints "AMARK_SKIP: ..." and succeeds, which CTest
# reports as skipped (4.12). The check fails when the tool fails, when it lists no
# symbol, or when no AMARK_API function is found in the headers.
#
# The names of the public functions are taken from every *.h of HEADER_DIR, with
# the comments and the preprocessor directives removed: for each AMARK_API, the
# last identifier before the first "(" that follows it. This covers return types
# that begin with amark_ and declarations over several lines.

cmake_minimum_required(VERSION 3.21)

foreach(_var LIBRARY FORMAT HEADER_DIR)
  if(NOT DEFINED ${_var} OR "${${_var}}" STREQUAL "")
    message(FATAL_ERROR "check_exported_symbols: ${_var} is not set")
  endif()
endforeach()
if(NOT EXISTS "${LIBRARY}")
  message(FATAL_ERROR "check_exported_symbols: the library does not exist: "
    "\"${LIBRARY}\"")
endif()
if(NOT IS_DIRECTORY "${HEADER_DIR}")
  message(FATAL_ERROR "check_exported_symbols: HEADER_DIR is not a directory: "
    "\"${HEADER_DIR}\"")
endif()

# True when the tool path in <var> is set and names an existing file.
function(_amark_tool_found var out)
  set(_found FALSE)
  if(DEFINED ${var} AND NOT "${${var}}" STREQUAL "" AND NOT "${${var}}" MATCHES "-NOTFOUND$"
     AND EXISTS "${${var}}" AND NOT IS_DIRECTORY "${${var}}")
    set(_found TRUE)
  endif()
  set(${out} ${_found} PARENT_SCOPE)
endfunction()

# --- The public functions declared with AMARK_API ------------------------------

file(GLOB _headers LIST_DIRECTORIES false "${HEADER_DIR}/*.h")
list(SORT _headers)
set(_api_names "")
foreach(_header IN LISTS _headers)
  file(READ "${_header}" _text)
  # Characters that CMake lists treat specially; none of them is part of a name.
  string(REPLACE ";" " " _text "${_text}")
  string(REPLACE "[" " " _text "${_text}")
  string(REPLACE "]" " " _text "${_text}")
  string(REPLACE "\r" "" _text "${_text}")
  # Block comments.
  set(_stripped "")
  while(TRUE)
    string(FIND "${_text}" "/*" _begin)
    if(_begin EQUAL -1)
      string(APPEND _stripped "${_text}")
      break()
    endif()
    string(SUBSTRING "${_text}" 0 ${_begin} _before)
    string(APPEND _stripped "${_before} ")
    math(EXPR _after "${_begin} + 2")
    string(SUBSTRING "${_text}" ${_after} -1 _text)
    string(FIND "${_text}" "*/" _end)
    if(_end EQUAL -1)
      message(FATAL_ERROR "check_exported_symbols: unterminated comment in "
        "\"${_header}\"")
    endif()
    math(EXPR _after "${_end} + 2")
    string(SUBSTRING "${_text}" ${_after} -1 _text)
  endwhile()
  set(_text "${_stripped}")
  # Line comments, continued lines and preprocessor directives.
  string(REGEX REPLACE "//[^\n]*" "" _text "${_text}")
  string(REGEX REPLACE "\\\\\n" " " _text "${_text}")
  string(REGEX REPLACE "(^|\n)[ \t]*#[^\n]*" "\\1" _text "${_text}")
  # Each AMARK_API up to the first "(" after it.
  string(REGEX MATCHALL "(^|[^A-Za-z0-9_])AMARK_API[^A-Za-z0-9_(][^(]*\\(" _decls "${_text}")
  foreach(_decl IN LISTS _decls)
    string(REGEX REPLACE "\\($" "" _decl "${_decl}")
    if(_decl MATCHES "([A-Za-z_][A-Za-z0-9_]*)[ \t\n]*$")
      list(APPEND _api_names "${CMAKE_MATCH_1}")
    endif()
  endforeach()
endforeach()
list(REMOVE_DUPLICATES _api_names)
list(LENGTH _api_names _api_count)
if(_api_count EQUAL 0)
  message(FATAL_ERROR "check_exported_symbols: no function declared with AMARK_API "
    "was found in \"${HEADER_DIR}\"/*.h")
endif()

# --- The exported symbols ------------------------------------------------------

set(_command "")
if(FORMAT STREQUAL "ELF" OR FORMAT STREQUAL "MACHO")
  _amark_tool_found(NM _have)
  if(NOT _have)
    message("AMARK_SKIP: nm not found (NM=\"${NM}\")")
    return()
  endif()
  if(FORMAT STREQUAL "ELF")
    set(_command "${NM}" -D --defined-only "${LIBRARY}")
  else()
    set(_command "${NM}" -gU "${LIBRARY}")
  endif()
elseif(FORMAT STREQUAL "PE")
  _amark_tool_found(DUMPBIN _have_dumpbin)
  _amark_tool_found(LLVM_READOBJ _have_readobj)
  _amark_tool_found(OBJDUMP _have_objdump)
  if(_have_dumpbin)
    set(_pe_tool dumpbin)
    set(_command "${DUMPBIN}" /nologo /exports "${LIBRARY}")
  elseif(_have_readobj)
    set(_pe_tool readobj)
    set(_command "${LLVM_READOBJ}" --coff-exports "${LIBRARY}")
  elseif(_have_objdump)
    set(_pe_tool objdump)
    set(_command "${OBJDUMP}" -p "${LIBRARY}")
  else()
    message("AMARK_SKIP: none of dumpbin, llvm-readobj and objdump was found")
    return()
  endif()
else()
  message(FATAL_ERROR "check_exported_symbols: unknown FORMAT \"${FORMAT}\" "
    "(expected ELF, MACHO or PE)")
endif()

list(JOIN _command " " _command_text)
execute_process(COMMAND ${_command}
  RESULT_VARIABLE _result
  OUTPUT_VARIABLE _output
  ERROR_VARIABLE _error)
if(NOT _result EQUAL 0)
  message(FATAL_ERROR "check_exported_symbols: the listing tool failed "
    "(${_result}): ${_command_text}\n${_output}${_error}")
endif()

string(REPLACE ";" " " _output "${_output}")
string(REPLACE "[" "<" _output "${_output}")
string(REPLACE "]" ">" _output "${_output}")
string(REPLACE "\r" "" _output "${_output}")
string(REPLACE "\n" ";" _lines "${_output}")

set(_symbols "")
set(_in_table FALSE)
foreach(_line IN LISTS _lines)
  set(_name "")
  if(FORMAT STREQUAL "ELF" OR FORMAT STREQUAL "MACHO")
    # "<address> <type> <name>"; the name is the last field.
    if(_line MATCHES "^[ \t]*$")
      continue()
    endif()
    if(_line MATCHES "([^ \t]+)[ \t]*$")
      set(_name "${CMAKE_MATCH_1}")
    endif()
    if(FORMAT STREQUAL "MACHO")
      string(REGEX REPLACE "^_" "" _name "${_name}")
    else()
      string(REGEX REPLACE "@.*$" "" _name "${_name}")
    endif()
  elseif(_pe_tool STREQUAL "dumpbin")
    # "  <ordinal> <hint> <RVA> <name>" (or without the RVA for a forwarder).
    if(_line MATCHES "^[ \t]+[0-9]+[ \t]+[0-9A-Fa-f]+[ \t]+[0-9A-Fa-f]+[ \t]+([^ \t]+)")
      set(_name "${CMAKE_MATCH_1}")
    endif()
  elseif(_pe_tool STREQUAL "readobj")
    # "  Name: <name>" inside each "Export {" block.
    if(_line MATCHES "^[ \t]*Name:[ \t]*([^ \t]+)")
      set(_name "${CMAKE_MATCH_1}")
    endif()
  else()
    # The rows of the "<Ordinal/Name Pointer> Table" (or "[Ordinal/Name Pointer]
    # Table" of older binutils). The row format depends on the binutils version:
    #   "\t[   0] <name>"                        (older)
    #   "\t<0> <name>"
    #   "\t< 0> +base< 1> 0000 <name>"           (binutils 2.4x)
    # so a row is a line starting with an index in brackets, and the name is its
    # last field. Blank lines and the "Ordinal Hint Name" heading are skipped; the
    # first other line after the rows ends the table.
    if(_line MATCHES "Ordinal/Name Pointer[]>] Table")
      set(_in_table TRUE)
      set(_table_rows 0)
      continue()
    endif()
    if(_in_table)
      if(_line MATCHES "^[ \t]*$" OR _line MATCHES "^[ \t]*Ordinal[ \t]+Hint[ \t]+Name[ \t]*$")
        # Not a row; the table goes on.
      elseif(_line MATCHES "^[ \t]*[[<][ \t]*[0-9]+[]>].*[ \t]([^ \t]+)[ \t]*$")
        set(_name "${CMAKE_MATCH_1}")
        math(EXPR _table_rows "${_table_rows} + 1")
      elseif(_table_rows GREATER 0)
        set(_in_table FALSE)
      endif()
    endif()
  endif()
  if(NOT _name STREQUAL "")
    list(APPEND _symbols "${_name}")
  endif()
endforeach()
list(REMOVE_DUPLICATES _symbols)
list(SORT _symbols)
list(LENGTH _symbols _symbol_count)
if(_symbol_count EQUAL 0)
  message(FATAL_ERROR "check_exported_symbols: no exported symbol was listed by: "
    "${_command_text}\n${_output}")
endif()

# --- The comparison ------------------------------------------------------------

set(_bad "")
foreach(_symbol IN LISTS _symbols)
  if(NOT _symbol MATCHES "^amark_[a-z0-9][a-z0-9_]*$")
    list(APPEND _bad "${_symbol} (does not match ^amark_[a-z0-9][a-z0-9_]*$)")
  elseif(NOT _symbol IN_LIST _api_names)
    list(APPEND _bad "${_symbol} (not declared with AMARK_API in ${HEADER_DIR})")
  endif()
endforeach()

if(_bad)
  list(LENGTH _bad _bad_count)
  list(JOIN _bad "\n  " _bad_text)
  message(FATAL_ERROR "check_exported_symbols: ${LIBRARY} exports ${_bad_count} "
    "symbol(s) that are not public API:\n  ${_bad_text}")
endif()

message(STATUS "check_exported_symbols: ${_symbol_count} exported symbols of "
  "${LIBRARY} are all public API (${_api_count} AMARK_API functions declared)")
