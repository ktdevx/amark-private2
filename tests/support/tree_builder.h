/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * Building trees from S-expressions in the format of the dump for the tests, and
 * the dump check (design, TestSupport).
 *
 * tree_builder.c is the INTERFACE source of amark_test_tree: it is compiled with
 * the settings of each test that links it, and it uses only the public API, so it
 * goes with either amark or amark_internal. It depends on Naos, so fuzz targets
 * must not link it.
 */
#ifndef AMARK_TEST_TREE_BUILDER_H
#define AMARK_TEST_TREE_BUILDER_H

#include <amark/node.h>
#include <amark/document.h>
#include <amark/value.h>
#include <amark/spelling.h>
#include <amark/dump.h>

#include <naos/naos.h>

#endif /* AMARK_TEST_TREE_BUILDER_H */
