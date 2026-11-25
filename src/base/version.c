/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

#include <amark/version.h>

long amark_version_number(void)
{
    return AMARK_VERSION_NUMBER;
}

const char *amark_version_string(void)
{
    return AMARK_VERSION_STRING;
}
