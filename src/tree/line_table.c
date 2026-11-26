/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * LineTable: the sources, their division into lines, the incremental
 * completion, the BOM, the line endings, the columns and the bytes.
 */
#include "tree/tree.h"

/* A translation unit must declare something (ISO C). */
typedef int amark_tree_line_table_unit_t;
