# SPDX-License-Identifier: MIT
# Copyright (c) The Amark Authors
#
# The tests of the syntax tree (syntax-tree), one per component of the design.
# Added by tests/CMakeLists.txt with the other test directories, so it is also
# added in a fuzz-only configuration: register the tests with amark_add_test(),
# which does nothing when AMARK_BUILD_TESTS is off, and define nothing here that
# depends on Naos. Tests that include the internal header of src/tree/
# (tree/tree.h) use INTERNAL.

# TestSupport: building trees from S-expressions (tests/support/tree_builder.h,
# the INTERFACE library amark_test_tree, which tests/CMakeLists.txt defines only
# when AMARK_BUILD_TESTS is on; amark_add_test() does nothing otherwise).
amark_add_test(tree_builder_test SOURCES tree_builder_test.c INTERNAL
  LIBRARIES amark_test_tree)

# Registry: syntax sets, freezing, the identifiers, the lookups and the slots.
amark_add_test(registry_test SOURCES registry_test.c INTERNAL)

# BuiltinTypes: the 19 built-in node types and the built-in syntax set.
amark_add_test(builtin_types_test SOURCES builtin_types_test.c INTERNAL
  LIBRARIES amark_test_tree)

# Document: the life of documents and of the nodes they own.
amark_add_test(document_test SOURCES document_test.c INTERNAL)

# LineTable: the sources and their lines.
amark_add_test(line_table_test SOURCES line_table_test.c INTERNAL)

# NodeCore: the structural editing.
amark_add_test(structure_test SOURCES structure_test.c INTERNAL)

# Facets: the storage, the getters, the setters and the records of the facets.
amark_add_test(facet_test SOURCES facet_test.c INTERNAL)

# Facets: the values derived from the tokens.
amark_add_test(derive_test SOURCES derive_test.c INTERNAL)

# Layout: the line ranges, the margins, the blank lines and the leaf spellings.
amark_add_test(layout_test SOURCES layout_test.c INTERNAL)

# Content: the content objects and the content tokens.
amark_add_test(content_test SOURCES content_test.c INTERNAL)

# DefinitionIndex: the label normalization and the label index.
amark_add_test(label_index_test SOURCES label_index_test.c INTERNAL)

# CopyImport: the copies and the imports.
amark_add_test(copy_import_test SOURCES copy_import_test.c INTERNAL)

# Position: the source ranges.
amark_add_test(position_test SOURCES position_test.c INTERNAL)

# LosslessCheck: the lossless verification.
amark_add_test(lossless_test SOURCES lossless_test.c INTERNAL)

# Verbatim: the spelling state and whether the original spelling can be output.
amark_add_test(verbatim_test SOURCES verbatim_test.c INTERNAL)

# Iterator: the traversal.
amark_add_test(iterator_test SOURCES iterator_test.c INTERNAL)

# Equality: the semantic comparison.
amark_add_test(equal_test SOURCES equal_test.c INTERNAL)

# Dump: the S-expression dump and the dump writer.
amark_add_test(dump_test SOURCES dump_test.c INTERNAL)

# The extension node types through the public API.
amark_add_test(extension_type_test SOURCES extension_type_test.c INTERNAL)

# The flow of a parser building a tree and of the msgstr insertion (the tree
# builder sets up the containers of some examples).
amark_add_test(msgstr_flow_test SOURCES msgstr_flow_test.c INTERNAL
  LIBRARIES amark_test_tree)

# The out-of-memory sweep of every operation that changes a document.
amark_add_test(oom_test SOURCES oom_test.c INTERNAL)

# Deep trees (100000 levels) and the complexity counters.
amark_add_test(scale_test SOURCES scale_test.c INTERNAL)
