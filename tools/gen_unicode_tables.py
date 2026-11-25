#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) The Amark Authors
"""Generate the Unicode character class tables used by src/base/unicode.c.

This is a maintainer tool. The build and the tests never run it; the
generated files are committed to the repository.

Usage:
    # Download the pinned UCD files into <dir> and (re)generate the tables.
    python3 tools/gen_unicode_tables.py --fetch --ucd-dir <dir>

    # Regenerate the tables from UCD files already in <dir>.
    python3 tools/gen_unicode_tables.py --ucd-dir <dir>

    # Compare the generated result with the committed files.
    # Exits with a non-zero status when any of them differs.
    python3 tools/gen_unicode_tables.py --ucd-dir <dir> --check

Extending:
    * To add a table derived from the General_Category property, append a
      (name, categories) entry to GENERAL_CATEGORY_TABLES.
    * To add a table derived from another UCD file (for example
      CaseFolding.txt), add its file name to UCD_FILES, write a generator
      function that takes the UCD directory and returns the full text of the
      output file, and append (output path, generator) to OUTPUTS. Every entry
      of OUTPUTS is written by the default mode and compared by --check, and
      every run checks UNICODE_VERSION against the public header.

Requires Python 3.8 or later and the standard library only.
"""

import argparse
import os
import re
import sys
import urllib.request

# The single place that pins the Unicode version in this script. It must match
# AMARK_UNICODE_VERSION_{MAJOR,MINOR,UPDATE} in include/amark/unicode.h.
UNICODE_VERSION = (17, 0, 0)

UCD_BASE_URL = "https://www.unicode.org/Public/{0}.{1}.{2}/ucd/"
UCD_FILES = ("UnicodeData.txt",)

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
UNICODE_HEADER = os.path.join("include", "amark", "unicode.h")

# Tables generated from the General_Category field of UnicodeData.txt:
# (C array name, set of General_Category values). Each table is the sorted,
# non-overlapping list of code point ranges whose category is in the set,
# with adjacent ranges merged.
GENERAL_CATEGORY_TABLES = (
    # CommonMark "Unicode whitespace character" (besides U+0009, U+000A,
    # U+000C and U+000D, which the classifier handles without the table).
    ("amark_unicode_zs", frozenset(["Zs"])),
    # CommonMark "Unicode punctuation character": P* or S*.
    ("amark_unicode_punct", frozenset([
        "Pc", "Pd", "Ps", "Pe", "Pi", "Pf", "Po",
        "Sm", "Sc", "Sk", "So",
    ])),
)

LICENSE_HEADER = (
    "/* SPDX-License-Identifier: MIT\n"
    " * Copyright (c) The Amark Authors */\n"
)


def version_string():
    return "{0}.{1}.{2}".format(*UNICODE_VERSION)


class GenError(Exception):
    pass


def fetch_ucd(ucd_dir):
    """Download the pinned version of every file in UCD_FILES into ucd_dir."""
    os.makedirs(ucd_dir, exist_ok=True)
    base = UCD_BASE_URL.format(*UNICODE_VERSION)
    for name in UCD_FILES:
        url = base + name
        dest = os.path.join(ucd_dir, name)
        sys.stderr.write("fetching {0}\n".format(url))
        try:
            with urllib.request.urlopen(url, timeout=60) as response:
                data = response.read()
        except OSError as exc:
            raise GenError("failed to fetch {0}: {1}".format(url, exc))
        tmp = dest + ".tmp"
        with open(tmp, "wb") as f:
            f.write(data)
        os.replace(tmp, dest)


def read_ucd_lines(ucd_dir, name):
    path = os.path.join(ucd_dir, name)
    try:
        with open(path, "r", encoding="utf-8") as f:
            return f.read().splitlines()
    except OSError as exc:
        raise GenError(
            "cannot read {0}: {1} (use --fetch to download Unicode {2})"
            .format(path, exc, version_string()))


def parse_unicode_data(ucd_dir):
    """Return a list of (first, last, general_category) from UnicodeData.txt.

    Range entries (<..., First> followed by <..., Last>) are expanded into a
    single (first, last) item.
    """
    entries = []
    pending_first = None
    for lineno, line in enumerate(
            read_ucd_lines(ucd_dir, "UnicodeData.txt"), start=1):
        if not line.strip():
            continue
        fields = line.split(";")
        if len(fields) != 15:
            raise GenError(
                "UnicodeData.txt:{0}: expected 15 fields".format(lineno))
        cp = int(fields[0], 16)
        name = fields[1]
        category = fields[2]
        if name.endswith(", First>"):
            if pending_first is not None:
                raise GenError(
                    "UnicodeData.txt:{0}: nested range start".format(lineno))
            pending_first = (cp, name[:-len(", First>")], category)
            continue
        if name.endswith(", Last>"):
            if (pending_first is None
                    or pending_first[1] != name[:-len(", Last>")]
                    or pending_first[2] != category):
                raise GenError(
                    "UnicodeData.txt:{0}: unmatched range end".format(lineno))
            entries.append((pending_first[0], cp, category))
            pending_first = None
            continue
        if pending_first is not None:
            raise GenError(
                "UnicodeData.txt:{0}: range start without end".format(lineno))
        entries.append((cp, cp, category))
    if pending_first is not None:
        raise GenError("UnicodeData.txt: range start without end at EOF")
    entries.sort()
    for prev, cur in zip(entries, entries[1:]):
        if cur[0] <= prev[1]:
            raise GenError(
                "UnicodeData.txt: overlapping entries at U+{0:04X}"
                .format(cur[0]))
    return entries


def merge_ranges(entries, categories):
    """Return sorted, merged (first, last) ranges whose category matches."""
    ranges = []
    for first, last, category in entries:
        if category not in categories:
            continue
        if ranges and ranges[-1][1] + 1 == first:
            ranges[-1] = (ranges[-1][0], last)
        else:
            ranges.append((first, last))
    return ranges


def format_range_table(name, categories, ranges):
    count = sum(last - first + 1 for first, last in ranges)
    lines = [
        "/* General_Category: {0} ({1} ranges, {2} code points) */".format(
            " ".join(sorted(categories)), len(ranges), count),
        "static const amark_unicode_range_t {0}[] = {{".format(name),
    ]
    for first, last in ranges:
        lines.append("    {{0x{0:06X}, 0x{1:06X}}},".format(first, last))
    lines.append("};")
    return "\n".join(lines) + "\n"


def generated_banner(source_files):
    return (
        "/* Generated by tools/gen_unicode_tables.py from Unicode {0}\n"
        " * ({1}). Do not edit by hand.\n"
        " *\n"
        " * Unicode version: {0}\n"
        " *\n"
        " * To regenerate (Python 3.8 or later, maintainers only):\n"
        " *   python3 tools/gen_unicode_tables.py --fetch --ucd-dir <dir>\n"
        " * To verify that this file is up to date:\n"
        " *   python3 tools/gen_unicode_tables.py --ucd-dir <dir> --check\n"
        " *\n"
        " * The includer defines amark_unicode_range_t as\n"
        " *   typedef struct {{ uint32_t first; uint32_t last; }}"
        " amark_unicode_range_t;\n"
        " * Each table is sorted by code point, its ranges do not overlap, and\n"
        " * adjacent ranges are merged. */\n"
    ).format(version_string(), ", ".join(source_files))


def generate_unicode_tables(ucd_dir):
    entries = parse_unicode_data(ucd_dir)
    parts = [LICENSE_HEADER, "\n", generated_banner(["UnicodeData.txt"])]
    for name, categories in GENERAL_CATEGORY_TABLES:
        parts.append("\n")
        parts.append(format_range_table(
            name, categories, merge_ranges(entries, categories)))
    return "".join(parts)


# (output path relative to the repository root, generator function).
OUTPUTS = (
    (os.path.join("src", "base", "unicode_tables.inc"),
     generate_unicode_tables),
)


def check_header_version(repo_root):
    """Fail unless include/amark/unicode.h states UNICODE_VERSION.

    Returns False (after a warning) when the header does not exist yet.
    """
    path = os.path.join(repo_root, UNICODE_HEADER)
    if not os.path.exists(path):
        sys.stderr.write(
            "warning: {0} not found; skipping the Unicode version check\n"
            .format(UNICODE_HEADER))
        return False
    with open(path, "r", encoding="utf-8") as f:
        text = f.read()
    found = []
    for part in ("MAJOR", "MINOR", "UPDATE"):
        match = re.search(
            r"^\s*#\s*define\s+AMARK_UNICODE_VERSION_{0}\s+(\d+)\b".format(
                part), text, re.MULTILINE)
        if match is None:
            raise GenError("{0}: AMARK_UNICODE_VERSION_{1} not found".format(
                UNICODE_HEADER, part))
        found.append(int(match.group(1)))
    if tuple(found) != UNICODE_VERSION:
        raise GenError(
            "{0} declares Unicode {1}.{2}.{3} but the script pins {4}"
            .format(UNICODE_HEADER, found[0], found[1], found[2],
                    version_string()))
    return True


def read_existing(path):
    try:
        with open(path, "r", encoding="utf-8", newline="") as f:
            return f.read()
    except OSError:
        return None


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Generate the Unicode {0} character class tables."
        .format(version_string()))
    parser.add_argument(
        "--ucd-dir", required=True,
        help="directory holding the UCD files (UnicodeData.txt, ...)")
    parser.add_argument(
        "--fetch", action="store_true",
        help="download the pinned UCD files from unicode.org into --ucd-dir")
    parser.add_argument(
        "--check", action="store_true",
        help="compare the generated output with the existing files instead "
             "of writing them; exit non-zero on any difference")
    parser.add_argument(
        "--repo-root", default=REPO_ROOT,
        help="repository root (default: the parent of tools/)")
    args = parser.parse_args(argv)

    try:
        check_header_version(args.repo_root)
        if args.fetch:
            fetch_ucd(args.ucd_dir)
        mismatches = 0
        for rel_path, generator in OUTPUTS:
            content = generator(args.ucd_dir)
            path = os.path.join(args.repo_root, rel_path)
            existing = read_existing(path)
            if args.check:
                if existing is None:
                    sys.stderr.write("MISSING  {0}\n".format(rel_path))
                    mismatches += 1
                elif existing != content:
                    sys.stderr.write("DIFFERS  {0}\n".format(rel_path))
                    mismatches += 1
                else:
                    sys.stdout.write("OK       {0}\n".format(rel_path))
            elif existing == content:
                sys.stdout.write("unchanged {0}\n".format(rel_path))
            else:
                os.makedirs(os.path.dirname(path), exist_ok=True)
                with open(path, "w", encoding="utf-8", newline="\n") as f:
                    f.write(content)
                sys.stdout.write("wrote     {0}\n".format(rel_path))
    except GenError as exc:
        sys.stderr.write("error: {0}\n".format(exc))
        return 2
    if args.check:
        if mismatches:
            sys.stderr.write(
                "{0} file(s) out of date; rerun without --check to "
                "regenerate\n".format(mismatches))
            return 1
        sys.stdout.write(
            "all tables match Unicode {0}\n".format(version_string()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
