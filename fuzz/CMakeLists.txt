# SPDX-License-Identifier: MIT
# Copyright (c) The Amark Authors
#
# The fuzz targets of Amark, added with amark_add_fuzzer() (cmake/AmarkFuzz.cmake,
# which also states the rules for the support libraries they link). This
# directory is added after tests/ when AMARK_FUZZ or AMARK_BUILD_TESTS is on:
#   - with AMARK_FUZZ on, each target is a libFuzzer executable, or an executable
#     with the standalone main (standalone_main.c) when libFuzzer is not available;
#   - with AMARK_BUILD_TESTS on, the corpus of each target <name>_fuzz (its seeds
#     in corpus/<name>/) is replayed by the CTest test fuzz.<name>.corpus, whether
#     AMARK_FUZZ is on or off.
# Later specs add their fuzz targets here, or in subdirectories added here.
