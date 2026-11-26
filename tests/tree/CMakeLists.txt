# SPDX-License-Identifier: MIT
# Copyright (c) The Amark Authors
#
# The tests of the syntax tree (syntax-tree). Added by tests/CMakeLists.txt with
# the other test directories, so it is also added in a fuzz-only configuration:
# register the tests with amark_add_test(), which does nothing when
# AMARK_BUILD_TESTS is off, and define nothing here that depends on Naos.
# Tests that include the internal headers of src/tree/ (tree/tree.h,
# tree/syntax_set.h) use INTERNAL.

# The descriptors of the built-in node types and amark.core (internal tables).
amark_add_test(builtin_types_test SOURCES builtin_types_test.c INTERNAL)

# Syntax sets: the order of the extensions, the checks, freezing, the lookups, the
# slot index and the static core set (reads the internal struct and tables).
amark_add_test(syntax_set_test SOURCES syntax_set_test.c INTERNAL)

# Documents and the node core: creation and destruction (every node, the detached
# ones too), the queries, the type data, the subtree destruction (links nodes and
# reads the ring through the internal structs).
amark_add_test(document_test SOURCES document_test.c INTERNAL)

# Edit records and their propagation (links nodes and reads the counter through the
# internal structs).
amark_add_test(edit_tracking_test SOURCES edit_tracking_test.c INTERNAL)

# The check of the tree invariants, the containment rule on descriptors and the
# internal counters (breaks the internal structs directly).
amark_add_test(tree_check_test SOURCES tree_check_test.c INTERNAL)

# Structural editing: the positions, moves, replacement, the rejections, unlinking,
# destruction and their records (checks the invariants and reads the internal
# counters).
amark_add_test(structure_edit_test SOURCES structure_edit_test.c INTERNAL)

# Syntax tokens: the checks of each kind, the order, removal, transfer and access
# (checks the invariants and reads the internal counter and token arrays).
amark_add_test(token_test SOURCES token_test.c INTERNAL)

# Literals and the setting of inline text: the string helpers, the literal modes,
# the records, the tokens kept and the out-of-memory sweeps (calls the internal
# helpers and checks the invariants).
amark_add_test(literal_attr_test SOURCES literal_attr_test.c INTERNAL)

# The lossless check of the syntax tokens: the seams of the parsers built by hand,
# each kind of problem with its offset and owner, the detached nodes left out and
# the out-of-memory sweep (public API only).
amark_add_test(token_check_test SOURCES token_check_test.c)

# Copies and imports: the copy compared with the original by a reference function,
# the records, the import into another syntax set with the sources renumbered,
# the rejections, deep chains and the out-of-memory sweeps (reads the ring and
# checks the invariants).
amark_add_test(copy_import_test SOURCES copy_import_test.c INTERNAL)

# The traversal: the events of a sample tree, a leaf as the root, the subtree never
# left, the edits during the traversal, the resets and their rejections, a deep
# chain and the out-of-memory sweep of the creation (public API only).
amark_add_test(iterator_span_test SOURCES iterator_span_test.c)

# The semantic comparison and the dump: the tokens, records and associated pointers
# ignored, each attribute, literal, type and child difference, the type data of
# extension types, other syntax sets, the text normalization; the dump of the design
# example and of every attribute, the extension attributes through the writer and
# its rejections, the escapes, the tokens, records and spans, the restored buffer on
# failure, deep chains, no allocation of the comparison and the out-of-memory sweep
# of the dump (sets unknown edit record bits through the internal struct).
amark_add_test(equal_dump_test SOURCES equal_dump_test.c INTERNAL)

# The test support that builds trees from S-expressions (tests/support/tree_builder.c,
# linked through amark_test_tree): the round trip of the dump for the example of the
# design, every built-in attribute, the tokens, the escapes; the extension attributes
# given to the callback; the position of each kind of error; the document left as it
# was on failure and the edit tracking restored (public API only). Built twice, with
# amark and with amark_internal, as the tests of both kinds link amark_test_tree.
amark_add_test(tree_builder_test SOURCES tree_builder_test.c LIBRARIES amark_test_tree)
amark_add_test(tree_builder_internal_test SOURCES tree_builder_test.c LIBRARIES amark_test_tree
  INTERNAL)
