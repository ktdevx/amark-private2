# SPDX-License-Identifier: MIT
# Copyright (c) The Amark Authors
#
# Checks the public headers (9.1, 9.3, 9.7). Registered by tests/headers/CMakeLists.txt
# as lint.public_headers (label "lint").
#
#   cmake -DHEADER_DIR=<source>/include/amark -P check_public_headers.cmake
#
# 1. Names (9.3). In every header of HEADER_DIR, with the comments removed:
#    - every macro (#define) begins with AMARK_;
#    - every name declared at file scope (function, typedef, object) begins with
#      amark_ (an object may also be an AMARK_ constant). The declared name is the
#      identifier before the first "(" of the declaration, or, for a parenthesized
#      declarator such as "(*name)", the first identifier inside it, or else the
#      last identifier before "[", "=" or ";";
#    - every struct, union or enum tag at file scope begins with amark_;
#    - every enumerator written in an enum body begins with AMARK_ (enumerators
#      produced by a macro, such as AMARK_ERROR_LIST, are checked as macro names
#      only: the argument names are not expanded).
#    Parameter names and struct members are not checked: they are not names of the
#    API. Preprocessor conditionals are not evaluated, so every branch is checked.
# 2. The umbrella header amark.h (9.1, 9.7) includes every other header of
#    HEADER_DIR exactly once, as <amark/NAME.h>, and in the canonical include order
#    below (design, PublicHeaders). A header that is not in the canonical order
#    fails the check: adding one is a revision of the design's table.
#
# Every violation is listed, then the check fails. It also fails when it finds no
# header or no declared function.

cmake_minimum_required(VERSION 3.21)

# The canonical include order of amark.h (design, PublicHeaders). Groups that do
# not exist yet are skipped; a later spec inserts its headers at their position.
set(_canonical_order
  # 1. Common base (foundation)
  export.h version.h error.h allocator.h buffer.h utf8.h unicode.h entity.h
  # 2. Syntax tree (syntax-tree)
  node.h document.h value.h spelling.h iterator.h dump.h extension.h
  # 3. Renderer and HTML (html-renderer)
  render.h html.h
  # 4. Parser (block-parser, inline-parser)
  scan.h parser.h block.h inline.h commonmark.h
  # 5. Markdown output (markdown-renderer)
  markdown.h)

# Keywords and specifiers that are never the declared name.
set(_keywords
  auto bool char const double enum extern float inline int long register restrict
  short signed static struct typedef union unsigned void volatile _Bool _Complex)

if(NOT DEFINED HEADER_DIR OR NOT IS_DIRECTORY "${HEADER_DIR}")
  message(FATAL_ERROR "check_public_headers: HEADER_DIR is not a directory: "
    "\"${HEADER_DIR}\"")
endif()

file(GLOB _headers LIST_DIRECTORIES false RELATIVE "${HEADER_DIR}" "${HEADER_DIR}/*.h")
list(SORT _headers)
if(NOT _headers)
  message(FATAL_ERROR "check_public_headers: no header found in \"${HEADER_DIR}\"")
endif()

set(_violations "")
set(_function_count 0)

# Reads <header>, removes the comments, and sets <out_text> to the result with the
# continued lines joined. Characters CMake lists treat specially (";", "[", "]")
# become the tokens @SEMI, @LBRACK and @RBRACK.
function(_read_header header out_text)
  file(READ "${HEADER_DIR}/${header}" text)
  string(REPLACE "\r" "" text "${text}")
  string(REPLACE ";" " @SEMI " text "${text}")
  string(REPLACE "[" " @LBRACK " text "${text}")
  string(REPLACE "]" " @RBRACK " text "${text}")
  set(stripped "")
  while(TRUE)
    string(FIND "${text}" "/*" begin)
    if(begin EQUAL -1)
      string(APPEND stripped "${text}")
      break()
    endif()
    string(SUBSTRING "${text}" 0 ${begin} before)
    string(APPEND stripped "${before} ")
    math(EXPR after "${begin} + 2")
    string(SUBSTRING "${text}" ${after} -1 text)
    string(FIND "${text}" "*/" end)
    if(end EQUAL -1)
      message(FATAL_ERROR "check_public_headers: unterminated comment in \"${header}\"")
    endif()
    math(EXPR after "${end} + 2")
    string(SUBSTRING "${text}" ${after} -1 text)
  endwhile()
  string(REGEX REPLACE "//[^\n]*" "" stripped "${stripped}")
  string(REGEX REPLACE "\\\\\n" " " stripped "${stripped}")
  set(${out_text} "${stripped}" PARENT_SCOPE)
endfunction()

function(_is_identifier token out)
  if(token MATCHES "^[A-Za-z_][A-Za-z0-9_]*$" AND NOT token IN_LIST _keywords)
    set(${out} TRUE PARENT_SCOPE)
  else()
    set(${out} FALSE PARENT_SCOPE)
  endif()
endfunction()

# Finds the name declared by the file-scope declaration <decl> (a list of tokens)
# and appends a violation for <header> when it does not have the right prefix.
macro(_check_declaration header decl)
  set(_d_tokens ${decl})
  set(_d_name "")
  set(_d_function FALSE)
  set(_d_typedef FALSE)
  if("typedef" IN_LIST _d_tokens)
    set(_d_typedef TRUE)
  endif()
  # The first "(" at paren depth 0.
  list(FIND _d_tokens "@LPAREN" _d_paren)
  if(NOT _d_paren EQUAL -1)
    set(_d_previous "")
    if(_d_paren GREATER 0)
      math(EXPR _d_prev_index "${_d_paren} - 1")
      list(GET _d_tokens ${_d_prev_index} _d_previous)
    endif()
    _is_identifier("${_d_previous}" _d_is_id)
    if(_d_is_id)
      # name(...): a function, or a function-like macro used as a declaration.
      set(_d_name "${_d_previous}")
      set(_d_function TRUE)
    else()
      # A parenthesized declarator: the first identifier inside it.
      list(LENGTH _d_tokens _d_length)
      math(EXPR _d_index "${_d_paren} + 1")
      while(_d_index LESS _d_length)
        list(GET _d_tokens ${_d_index} _d_token)
        if(_d_token STREQUAL "@RPAREN" OR _d_token STREQUAL "@LPAREN")
          break()
        endif()
        _is_identifier("${_d_token}" _d_is_id)
        if(_d_is_id)
          set(_d_name "${_d_token}")
          break()
        endif()
        math(EXPR _d_index "${_d_index} + 1")
      endwhile()
    endif()
  else()
    # The last identifier before "[", "=" or the end.
    foreach(_d_token IN LISTS _d_tokens)
      if(_d_token STREQUAL "@LBRACK" OR _d_token STREQUAL "@EQ")
        break()
      endif()
      _is_identifier("${_d_token}" _d_is_id)
      if(_d_is_id)
        set(_d_name "${_d_token}")
      endif()
    endforeach()
  endif()

  list(JOIN _d_tokens " " _d_joined)
  if(_d_joined MATCHES "(^| )(struct|union|enum) [A-Za-z_][A-Za-z0-9_]*$")
    # A tag without a declarator ("struct amark_buffer;", "enum amark_x {...};"):
    # the tag is the name, and it is checked with the other tags.
  elseif(_d_name STREQUAL "")
    list(APPEND _violations
      "${header}: no declared name found in \"${_d_joined}\"")
  elseif(_d_function OR _d_typedef)
    if(NOT _d_name MATCHES "^amark_")
      if(_d_typedef)
        set(_d_what "type")
      else()
        set(_d_what "function")
      endif()
      list(APPEND _violations
        "${header}: the ${_d_what} \"${_d_name}\" does not begin with amark_")
    endif()
  elseif(NOT _d_name MATCHES "^(amark_|AMARK_)")
    list(APPEND _violations
      "${header}: the name \"${_d_name}\" does not begin with amark_ or AMARK_")
  endif()
  if(_d_function AND NOT _d_typedef)
    math(EXPR _function_count "${_function_count} + 1")
  endif()
endmacro()

# --- 1. Names ------------------------------------------------------------------

foreach(_header IN LISTS _headers)
  _read_header("${_header}" _text)

  # Macros.
  string(REGEX MATCHALL "(^|\n)[ \t]*#[ \t]*define[ \t]+[A-Za-z_][A-Za-z0-9_]*"
    _defines "${_text}")
  foreach(_define IN LISTS _defines)
    string(REGEX REPLACE "^.*define[ \t]+" "" _macro "${_define}")
    if(NOT _macro MATCHES "^AMARK_")
      list(APPEND _violations "${_header}: the macro \"${_macro}\" does not begin with AMARK_")
    endif()
  endforeach()

  # The declarations, without the preprocessor directives.
  string(REGEX REPLACE "(^|\n)[ \t]*#[^\n]*" "\\1" _code "${_text}")
  # Drop string and character literals (C++ linkage specifications and the like).
  string(REGEX REPLACE "\"([^\"\\\\\n]|\\\\.)*\"" " " _code "${_code}")
  string(REGEX REPLACE "'([^'\\\\\n]|\\\\.)*'" " " _code "${_code}")
  string(REPLACE "{" " @LBRACE " _code "${_code}")
  string(REPLACE "}" " @RBRACE " _code "${_code}")
  string(REPLACE "(" " @LPAREN " _code "${_code}")
  string(REPLACE ")" " @RPAREN " _code "${_code}")
  string(REPLACE "," " @COMMA " _code "${_code}")
  string(REPLACE "=" " @EQ " _code "${_code}")
  string(REPLACE "*" " @STAR " _code "${_code}")
  string(REGEX MATCHALL "@?[A-Za-z0-9_]+" _tokens "${_code}")

  set(_braces "")        # a stack of "enum" and "other"
  set(_paren_depth 0)
  set(_decl "")          # the file-scope tokens of the current declaration
  set(_tag_keyword "")   # the struct/union/enum keyword waiting for its body
  set(_previous "")
  set(_expect_tag FALSE)
  foreach(_token IN LISTS _tokens)
    list(LENGTH _braces _brace_depth)
    if(_brace_depth GREATER 0)
      list(GET _braces -1 _brace_kind)
    else()
      set(_brace_kind "")
    endif()

    if(_expect_tag)
      set(_expect_tag FALSE)
      _is_identifier("${_token}" _is_id)
      if(_is_id AND _brace_depth EQUAL 0 AND _paren_depth EQUAL 0
         AND NOT _token MATCHES "^amark_")
        list(APPEND _violations
          "${_header}: the ${_tag_keyword} tag \"${_token}\" does not begin with amark_")
      endif()
    endif()

    if(_token STREQUAL "@LBRACE")
      if(_tag_keyword STREQUAL "enum")
        list(APPEND _braces enum)
      else()
        list(APPEND _braces other)
      endif()
      set(_tag_keyword "")
    elseif(_token STREQUAL "@RBRACE")
      if(_brace_depth EQUAL 0)
        message(FATAL_ERROR "check_public_headers: unbalanced \"}\" in \"${_header}\"")
      endif()
      list(POP_BACK _braces)
    elseif(_token STREQUAL "@LPAREN")
      math(EXPR _paren_depth "${_paren_depth} + 1")
    elseif(_token STREQUAL "@RPAREN")
      math(EXPR _paren_depth "${_paren_depth} - 1")
    elseif(_token MATCHES "^(struct|union|enum)$")
      set(_tag_keyword "${_token}")
      set(_expect_tag TRUE)
    elseif(_brace_kind STREQUAL "enum" AND _paren_depth EQUAL 0
           AND _previous MATCHES "^@(LBRACE|COMMA)$")
      _is_identifier("${_token}" _is_id)
      if(_is_id AND NOT _token MATCHES "^AMARK_")
        list(APPEND _violations
          "${_header}: the enumerator \"${_token}\" does not begin with AMARK_")
      endif()
    endif()

    # File-scope declarations end with ";" outside braces.
    if(_brace_depth EQUAL 0 AND NOT _token STREQUAL "@LBRACE")
      if(_token STREQUAL "@SEMI")
        if(_paren_depth EQUAL 0)
          _check_declaration("${_header}" "${_decl}")
          set(_decl "")
          set(_tag_keyword "")
        endif()
      else()
        list(APPEND _decl "${_token}")
      endif()
    endif()
    set(_previous "${_token}")
  endforeach()
  if(NOT _braces STREQUAL "" OR NOT _paren_depth EQUAL 0)
    message(FATAL_ERROR "check_public_headers: unbalanced braces or parentheses in "
      "\"${_header}\"")
  endif()
  # Tokens after the last ";" (AMARK_END_DECLS) declare nothing.
endforeach()

if(_function_count EQUAL 0)
  list(APPEND _violations "no function declared in the headers of \"${HEADER_DIR}\"")
endif()

# --- 2. The umbrella header ----------------------------------------------------

if(NOT "amark.h" IN_LIST _headers)
  list(APPEND _violations "amark.h: the umbrella header does not exist")
else()
  _read_header("amark.h" _text)
  string(REGEX MATCHALL "(^|\n)[ \t]*#[ \t]*include[ \t]*<[^>\n]*>" _includes "${_text}")
  set(_included "")
  set(_last_position -1)
  set(_last_header "")
  foreach(_include IN LISTS _includes)
    string(REGEX REPLACE "^.*<([^>]*)>$" "\\1" _path "${_include}")
    if(NOT _path MATCHES "^amark/([^/]+)$")
      list(APPEND _violations
        "amark.h: includes <${_path}>, which is not a public header of Amark")
      continue()
    endif()
    set(_name "${CMAKE_MATCH_1}")
    if(_name IN_LIST _included)
      list(APPEND _violations "amark.h: includes <amark/${_name}> more than once")
      continue()
    endif()
    list(APPEND _included "${_name}")
    if(_name STREQUAL "amark.h" OR NOT _name IN_LIST _headers)
      list(APPEND _violations
        "amark.h: includes <amark/${_name}>, which is not another header of ${HEADER_DIR}")
    endif()
    list(FIND _canonical_order "${_name}" _position)
    if(_position EQUAL -1)
      list(APPEND _violations
        "amark.h: <amark/${_name}> is not in the canonical include order (design, PublicHeaders): add it to the design's table and to this script")
    elseif(_position LESS _last_position)
      list(APPEND _violations
        "amark.h: <amark/${_name}> comes after <amark/${_last_header}>, against the canonical include order")
    else()
      set(_last_position ${_position})
      set(_last_header "${_name}")
    endif()
  endforeach()
  foreach(_header IN LISTS _headers)
    if(NOT _header STREQUAL "amark.h" AND NOT _header IN_LIST _included)
      list(APPEND _violations "amark.h: does not include <amark/${_header}>")
    endif()
  endforeach()
endif()

# --- Result --------------------------------------------------------------------

if(_violations)
  list(LENGTH _violations _count)
  list(JOIN _violations "\n  " _report)
  message(FATAL_ERROR "check_public_headers: ${_count} problem(s) in the public "
    "headers:\n  ${_report}")
endif()
list(LENGTH _headers _header_count)
message(STATUS "check_public_headers: ${_header_count} headers, ${_function_count} "
  "functions: names and amark.h are correct")
