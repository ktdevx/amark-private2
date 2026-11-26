/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The values of the facets: getting, setting, unsetting and recording them, the
 * facet numbers and the enumeration values of the built-in types, and the setting
 * of a text as a whole.
 */
#ifndef AMARK_VALUE_H
#define AMARK_VALUE_H

#include <stddef.h>
#include <stdint.h>

#include <amark/export.h>
#include <amark/error.h>
#include <amark/node.h>

AMARK_BEGIN_DECLS

/* The state of a value read from a facet. */
typedef enum amark_value_state
{
    /* The facet has a value: a set or recorded value, a value derived from the
     * spelling, or the default value of a value facet. */
    AMARK_VALUE_PRESENT = 1,
    /* A style facet that has no value. */
    AMARK_VALUE_UNSET = 2,
    /* The destination or the title of a reference link or image whose label has
     * no definition. */
    AMARK_VALUE_UNRESOLVED = 3
} amark_value_state_t;

/* A value of a facet. The caller sets struct_size to sizeof(amark_value_t). */
typedef struct amark_value
{
    size_t struct_size;
    amark_value_state_t state;
    /* An INT, an ENUM (from 1) or a BOOL (0 or 1). */
    int64_t number;
    /* A STRING, terminated by NUL, and its length in bytes. */
    const char *text;
    size_t length;
} amark_value_t;

AMARK_END_DECLS

#endif /* AMARK_VALUE_H */
