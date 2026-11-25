/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

#include <amark/error.h>

const char *amark_error_string(amark_error_t error)
{
    /* No default label: a value outside of the list falls through to the end. */
    switch (error)
    {
#define AMARK_ERROR_CASE(name, text) \
    case name:                       \
        return text;
        AMARK_ERROR_LIST(AMARK_ERROR_CASE)
#undef AMARK_ERROR_CASE
    }

    return "unknown error";
}
