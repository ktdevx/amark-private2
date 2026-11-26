/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The fuzz target of the editing of the syntax tree (TreeFuzz).
 *
 * In this form it creates no document and accepts every input: it keeps the
 * target and the replay of its corpus registered until the operations on the
 * tree exist.
 */
#include <stddef.h>
#include <stdint.h>

#include "tree/tree.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    (void)data;
    (void)size;
    return 0;
}
