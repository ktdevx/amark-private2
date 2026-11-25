/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The main of the fuzz targets when libFuzzer is not available (FuzzInfra).
 *
 * Reads each file given as an argument and calls LLVMFuzzerTestOneInput once with
 * its content, in the order of the arguments. Exits with 0 when every file was
 * run, and with a non-zero status when a file cannot be read or no file is given.
 * LLVMFuzzerInitialize is not supported: it would need a weak symbol, which C99
 * does not have.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

/* Reads the whole file at path into a new buffer (at least one byte, so that the
 * pointer is never NULL, as with libFuzzer). Returns NULL on failure. */
static uint8_t *read_file(const char *path, size_t *out_size)
{
    FILE *file = fopen(path, "rb");
    uint8_t *data = NULL;
    size_t size = 0;
    size_t capacity = 0;

    if (file == NULL)
    {
        return NULL;
    }

    for (;;)
    {
        size_t count;

        if (size == capacity)
        {
            size_t new_capacity = capacity == 0 ? 4096 : capacity * 2;
            uint8_t *grown;

            if (new_capacity < capacity)
            {
                free(data);
                fclose(file);
                return NULL;
            }

            grown = (uint8_t *)realloc(data, new_capacity);
            if (grown == NULL)
            {
                free(data);
                fclose(file);
                return NULL;
            }

            data = grown;
            capacity = new_capacity;
        }

        count = fread(data + size, 1, capacity - size, file);
        size += count;
        if (size < capacity)
        {
            /* A short read: the end of the file or an error. */
            if (ferror(file))
            {
                free(data);
                fclose(file);
                return NULL;
            }

            break;
        }
    }

    fclose(file);

    *out_size = size;
    return data;
}

int main(int argc, char **argv)
{
    int i;

    if (argc < 2)
    {
        fprintf(stderr, "usage: %s <input file>...\n", argc > 0 ? argv[0] : "fuzzer");
        return 2;
    }

    for (i = 1; i < argc; i++)
    {
        size_t size = 0;
        uint8_t *data = read_file(argv[i], &size);
        if (data == NULL)
        {
            fprintf(stderr, "error: cannot read '%s'\n", argv[i]);
            return 1;
        }

        fprintf(stderr, "Running: %s (%lu bytes)\n", argv[i], (unsigned long)size);
        (void)LLVMFuzzerTestOneInput(data, size);
        free(data);
    }

    fprintf(stderr, "Executed %d inputs\n", argc - 1);
    return 0;
}
