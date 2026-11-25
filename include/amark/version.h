/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The single definition of the Amark version.
 *
 * cmake/AmarkVersion.cmake reads the three "#define AMARK_VERSION_MAJOR/MINOR/PATCH"
 * lines below; every other version (the version string, the runtime version, the
 * CMake package version, the pkg-config version and the shared library SOVERSION)
 * is derived from them. The header also declares the runtime version functions.
 *
 * Write each number as a plain decimal without leading zeros, parentheses or
 * suffixes: it is stringified as-is into AMARK_VERSION_STRING. The minor and
 * patch versions must be in the range 0-999.
 */
#ifndef AMARK_VERSION_H
#define AMARK_VERSION_H

#include <amark/export.h>

#define AMARK_VERSION_MAJOR 0
#define AMARK_VERSION_MINOR 1
#define AMARK_VERSION_PATCH 0

/* Internal: two levels so that the argument macro is expanded before it is
 * stringified. Not for use by applications. */
#define AMARK_VERSION_STRINGIFY_(x) #x
#define AMARK_VERSION_STRINGIFY(x) AMARK_VERSION_STRINGIFY_(x)

/* The version as a string literal, e.g. "0.1.0". Built from the three numbers. */
#define AMARK_VERSION_STRING                         \
    AMARK_VERSION_STRINGIFY(AMARK_VERSION_MAJOR)     \
    "." AMARK_VERSION_STRINGIFY(AMARK_VERSION_MINOR) \
    "." AMARK_VERSION_STRINGIFY(AMARK_VERSION_PATCH)

/* Encodes a version as a comparable number: major * 1000000 + minor * 1000 + patch. */
#define AMARK_VERSION_ENCODE(major, minor, patch) \
    ((long)(major) * 1000000L + (long)(minor) * 1000L + (long)(patch))

/* The version of these headers as a comparable number, e.g. 1000 for 0.1.0. */
#define AMARK_VERSION_NUMBER \
    AMARK_VERSION_ENCODE(AMARK_VERSION_MAJOR, AMARK_VERSION_MINOR, AMARK_VERSION_PATCH)

AMARK_BEGIN_DECLS

/* Returns AMARK_VERSION_NUMBER of the library that is actually linked, which can
 * differ from the AMARK_VERSION_NUMBER of the headers the caller was built with. */
AMARK_API long amark_version_number(void);

/* Returns AMARK_VERSION_STRING of the library that is actually linked. The string
 * is static; do not free it. */
AMARK_API const char *amark_version_string(void);

AMARK_END_DECLS

#endif /* AMARK_VERSION_H */
