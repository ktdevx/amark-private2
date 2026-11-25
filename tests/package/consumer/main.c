/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The consumer of the package tests: uses Amark as an application does, through
 * <amark/amark.h> and the library it was given. Returns 0 on success; each failed
 * check prints a line to stderr and makes the result 1.
 *
 * AMARK_CONSUMER_EXPECTED_VERSION, defined by consumer/CMakeLists.txt, is the
 * version read from the source tree under test, so that headers or a library from
 * another place are detected.
 */
#include <amark/amark.h>

#include <stdio.h>
#include <string.h>

#ifndef AMARK_CONSUMER_EXPECTED_VERSION
#error "AMARK_CONSUMER_EXPECTED_VERSION must be defined"
#endif

static int failures = 0;

static void check(int condition, const char *what)
{
    if (!condition)
    {
        fprintf(stderr, "amark_consumer: check failed: %s\n", what);
        failures = 1;
    }
}

int main(void)
{
    static const char text[] = "Hello, Amark";
    amark_buffer_t *buffer = NULL;

    check(strcmp(AMARK_VERSION_STRING, AMARK_CONSUMER_EXPECTED_VERSION) == 0,
          "AMARK_VERSION_STRING is the version of the source tree");
    check(strcmp(amark_version_string(), AMARK_VERSION_STRING) == 0,
          "amark_version_string() equals AMARK_VERSION_STRING");
    check(amark_version_number() == AMARK_VERSION_NUMBER,
          "amark_version_number() equals AMARK_VERSION_NUMBER");

    if ((amark_buffer_create(&buffer) != AMARK_ERROR_NONE) || (buffer == NULL))
    {
        fprintf(stderr, "amark_consumer: amark_buffer_create failed\n");
        return 1;
    }
    check(amark_buffer_append_cstr(buffer, "Hello, ") == AMARK_ERROR_NONE,
          "amark_buffer_append_cstr succeeds");
    check(amark_buffer_append(buffer, "Amark", 5) == AMARK_ERROR_NONE,
          "amark_buffer_append succeeds");
    check(amark_buffer_length(buffer) == sizeof text - 1,
          "the buffer length is that of the appended text");
    check(memcmp(amark_buffer_data(buffer), text, sizeof text) == 0,
          "the buffer holds the appended text and its NUL terminator");
    amark_buffer_destroy(buffer);

    if (failures == 0)
    {
        printf("amark_consumer: Amark %s works\n", amark_version_string());
    }
    return failures;
}
