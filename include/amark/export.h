/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The export macro and the declaration wrapper of the public headers.
 *
 * AMARK_API marks every public function of Amark. The library is built with the
 * symbol visibility hidden by default, so only the functions marked with
 * AMARK_API are exported from the shared library.
 *
 * On Windows, AMARK_SHARED selects the DLL import and export. The build defines
 * AMARK_SHARED and AMARK_BUILDING while it builds the DLL, and passes AMARK_SHARED
 * to the consumers of the shared library (the CMake target Amark::amark and the
 * pkg-config file do it), so applications do not define these macros by hand.
 *
 * The visibility attribute is outside of C99. These conditional definitions are
 * the only place where Amark uses a compiler-specific feature.
 *
 * AMARK_BEGIN_DECLS and AMARK_END_DECLS enclose the declarations of every public
 * header, so that the functions are linked with C names from C++.
 */
#ifndef AMARK_EXPORT_H
#define AMARK_EXPORT_H

#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(AMARK_SHARED) && defined(AMARK_BUILDING)
#    define AMARK_API __declspec(dllexport)
#  elif defined(AMARK_SHARED)
#    define AMARK_API __declspec(dllimport)
#  else
#    define AMARK_API
#  endif
#elif defined(__GNUC__) && (__GNUC__ >= 4)
#  define AMARK_API __attribute__((visibility("default")))
#else
#  define AMARK_API
#endif

#ifdef __cplusplus
#  define AMARK_BEGIN_DECLS extern "C" {
#  define AMARK_END_DECLS }
#else
#  define AMARK_BEGIN_DECLS
#  define AMARK_END_DECLS
#endif

#endif /* AMARK_EXPORT_H */
