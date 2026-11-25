# SPDX-License-Identifier: MIT
# Copyright (c) The Amark Authors
#
# The helpers of the test generators (4.7, 4.8), for script mode only: a generator
# script run by amark_add_generated_tests() includes this module with
#   include("${AMARK_TESTGEN_MODULE}")
#
# A generator script starts with cmake_minimum_required(VERSION 3.21), as the
# module checks.
#
# Data is always handled as hexadecimal strings, two lowercase digits per byte, so
# that NUL, CR, ";", "[", "]" and invalid UTF-8 survive CMake's strings and lists.
#
# amark_testgen_read_lines_hex(<out_list> <file>)
#   Splits <file> at LF and sets <out_list> to the list of its lines, each as a
#   hexadecimal string. The LF is not included; a CR is kept. A final LF ends the
#   last line and adds no empty line; a last line without LF is returned too. An
#   empty line is an empty element. As in any CMake list, an empty file and a
#   file of one empty line (a single LF) both give an empty list: a generator that
#   must tell them apart compares the file itself (file(READ ... HEX)).
#
# amark_testgen_hex(<out_var> <text>)
#   Sets <out_var> to <text> (such as an ASCII separator line) as a hexadecimal
#   string, to compare it with the lines.
#
# amark_testgen_join_lines_hex(<out_var> <list>)
#   Joins the hexadecimal lines of <list> with LF (0a), without a final LF.
#
# amark_testgen_byte_array(<out_var> NAME <c_name> HEX <hex>)
#   Sets <out_var> to the C code
#     static const unsigned char <c_name>[] = {...};
#     static const size_t <c_name>_size = <n>;
#   holding the bytes of <hex>. With no byte, the array holds one 0 and
#   <c_name>_size is 0. The generator must use both, or the warnings of the tests
#   (unused variables) fail the build; the code needs <stddef.h>.
#
# amark_testgen_c_identifier(<out_var> <text>)
#   Replaces every character other than [A-Za-z0-9_] with "_", and puts "_" in
#   front when the result starts with a digit or is empty.
#
# amark_testgen_c_string(<out_var> <ascii_text>)
#   Sets <out_var> to a C string literal (with the quotes) of a short text, such
#   as a test name or a note: printable ASCII is kept, except that "\", "\"" and
#   "?" (trigraphs) are escaped; tab, LF and CR become \t, \n and \r, and every
#   other byte a three-digit octal escape.
#
# amark_testgen_write(<file> <content>)
#   Writes <content> to <file> unless <file> already holds exactly <content>, so
#   that an unchanged output is not compiled again.

if(NOT CMAKE_SCRIPT_MODE_FILE)
  message(FATAL_ERROR "AmarkTestGen.cmake is for generator scripts run with cmake -P")
endif()

# A script run with -P starts with the policies of CMake 2.4, under which list()
# drops the empty elements, that is the empty lines. The policies of the caller
# reach this point: require those of the project.
cmake_policy(GET CMP0007 list_policy)
if(NOT list_policy STREQUAL "NEW")
  message(FATAL_ERROR
    "AmarkTestGen.cmake: start the generator script with cmake_minimum_required(VERSION 3.21)")
endif()
unset(list_policy)
cmake_policy(VERSION 3.21)

include_guard(GLOBAL)

function(amark_testgen_read_lines_hex out_list file)
  if(NOT EXISTS "${file}" OR IS_DIRECTORY "${file}")
    message(FATAL_ERROR "amark_testgen_read_lines_hex: cannot read '${file}'")
  endif()
  file(READ "${file}" hex HEX)
  if(hex STREQUAL "")
    set(${out_list} "" PARENT_SCOPE)
    return()
  endif()
  # A comma after every byte keeps the bytes aligned: "0a," can only match a whole
  # byte, as a comma follows only the second digit of a byte.
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "\\1," hex "${hex}")
  string(REPLACE "0a," ";" hex "${hex}")
  string(REPLACE "," "" hex "${hex}")
  # A final LF ends the last line instead of starting an empty one.
  string(REGEX REPLACE ";$" "" hex "${hex}")
  set(${out_list} "${hex}" PARENT_SCOPE)
endfunction()

function(amark_testgen_hex out_var text)
  string(HEX "${text}" hex)
  set(${out_var} "${hex}" PARENT_SCOPE)
endfunction()

function(amark_testgen_join_lines_hex out_var list)
  # The elements are hexadecimal digits only, so every ";" separates two lines.
  string(REPLACE ";" "0a" joined "${list}")
  set(${out_var} "${joined}" PARENT_SCOPE)
endfunction()

function(amark_testgen_byte_array out_var)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "NAME;HEX" "")
  if(arg_UNPARSED_ARGUMENTS OR NOT DEFINED arg_NAME OR arg_NAME STREQUAL "")
    message(FATAL_ERROR "amark_testgen_byte_array: usage: <out_var> NAME <c_name> HEX <hex>")
  endif()
  string(LENGTH "${arg_HEX}" digits)
  math(EXPR odd "${digits} % 2")
  if(odd OR NOT "${arg_HEX}" MATCHES "^[0-9a-fA-F]*$")
    message(FATAL_ERROR "amark_testgen_byte_array(${arg_NAME}): not a hexadecimal byte string")
  endif()
  math(EXPR size "${digits} / 2")
  if(size EQUAL 0)
    set(body "0")
  else()
    string(TOLOWER "${arg_HEX}" hex)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1, " body "${hex}")
    # Twelve bytes per line (CMake's regular expressions have no {n}).
    string(REPEAT "0x[0-9a-f][0-9a-f], " 12 twelve)
    string(REGEX REPLACE "(${twelve})" "\\1\n    " body "${body}")
    string(REPLACE ", \n" ",\n" body "${body}")
    string(REGEX REPLACE "[ \n]+$" "" body "${body}")
    string(REGEX REPLACE ",$" "" body "${body}")
  endif()
  set(${out_var}
    "static const unsigned char ${arg_NAME}[] = {\n    ${body}\n};\nstatic const size_t ${arg_NAME}_size = ${size};\n"
    PARENT_SCOPE)
endfunction()

function(amark_testgen_c_identifier out_var text)
  string(REGEX REPLACE "[^A-Za-z0-9_]" "_" identifier "${text}")
  if(identifier STREQUAL "" OR identifier MATCHES "^[0-9]")
    set(identifier "_${identifier}")
  endif()
  set(${out_var} "${identifier}" PARENT_SCOPE)
endfunction()

function(amark_testgen_c_string out_var text)
  string(HEX "${text}" hex)
  string(LENGTH "${hex}" digits)
  set(literal "\"")
  set(after_octal FALSE)
  set(i 0)
  while(i LESS digits)
    string(SUBSTRING "${hex}" ${i} 2 byte_hex)
    math(EXPR byte "0x${byte_hex}")
    # A digit right after an octal escape is legal C, but MSVC warns about it
    # (C4125, an error with warnings as errors): split the literal there.
    if(after_octal AND byte GREATER_EQUAL 48 AND byte LESS_EQUAL 57)
      string(APPEND literal "\" \"")
    endif()
    set(after_octal FALSE)
    if(byte EQUAL 9)
      string(APPEND literal "\\t")
    elseif(byte EQUAL 10)
      string(APPEND literal "\\n")
    elseif(byte EQUAL 13)
      string(APPEND literal "\\r")
    elseif(byte EQUAL 34 OR byte EQUAL 63 OR byte EQUAL 92)
      # " ? \
      string(ASCII ${byte} char)
      string(APPEND literal "\\${char}")
    elseif(byte GREATER_EQUAL 32 AND byte LESS 127)
      string(ASCII ${byte} char)
      string(APPEND literal "${char}")
    else()
      # Three octal digits: a digit that follows cannot extend the escape.
      math(EXPR d1 "${byte} / 64")
      math(EXPR d2 "(${byte} / 8) % 8")
      math(EXPR d3 "${byte} % 8")
      string(APPEND literal "\\${d1}${d2}${d3}")
      set(after_octal TRUE)
    endif()
    math(EXPR i "${i} + 2")
  endwhile()
  string(APPEND literal "\"")
  set(${out_var} "${literal}" PARENT_SCOPE)
endfunction()

function(amark_testgen_write file content)
  if(EXISTS "${file}")
    file(READ "${file}" old)
    if("${old}" STREQUAL "${content}")
      return()
    endif()
  endif()
  file(WRITE "${file}" "${content}")
endfunction()
