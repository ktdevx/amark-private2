#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) The Amark Authors
"""Generate the named character reference table used by src/base/entity.c.

One file is generated: src/base/entity_tables.inc, from the WHATWG HTML
Standard's entities.json (https://html.spec.whatwg.org/entities.json).

The WHATWG publishes entities.json without a version, so the script pins the
SHA-256 of the file it was last generated from (ENTITIES_SHA256). A file with
another digest is refused; when the WHATWG changes the list, review the change,
update ENTITIES_SHA256 and regenerate.

Only the names that end in ";" are kept: CommonMark recognizes an entity
reference only as "&", a name of the list and ";" (the legacy names without
the semicolon, such as "&amp", are left out).

This is a maintainer tool. The build and the tests never run it; the
generated file is committed to the repository.

Usage:
    # Download entities.json into <dir> and (re)generate the table.
    python3 tools/gen_entity_table.py --fetch --data-dir <dir>

    # Regenerate the table from entities.json already in <dir>.
    python3 tools/gen_entity_table.py --data-dir <dir>

    # Compare the generated result with the committed file.
    # Exits with a non-zero status when it differs.
    python3 tools/gen_entity_table.py --data-dir <dir> --check

Requires Python 3.8 or later and the standard library only.
"""

import argparse
import hashlib
import json
import os
import re
import sys
import urllib.request

ENTITIES_URL = "https://html.spec.whatwg.org/entities.json"
ENTITIES_FILE = "entities.json"

# The SHA-256 of the pinned entities.json.
ENTITIES_SHA256 = (
    "d741d877ac77c4194c4ad526b5b4a19aef8dfe411ab840a466891cdbb9f362e6")

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUTPUT = os.path.join("src", "base", "entity_tables.inc")

# The most code points a reference of the list expands to.
MAX_CODEPOINTS = 2

NAME_PATTERN = re.compile(r"&([A-Za-z][A-Za-z0-9]*);")

LICENSE_HEADER = (
    "/* SPDX-License-Identifier: MIT\n"
    " * Copyright (c) The Amark Authors */\n"
)


class GenError(Exception):
    pass


def fetch_entities(data_dir):
    """Download entities.json into data_dir."""
    os.makedirs(data_dir, exist_ok=True)
    dest = os.path.join(data_dir, ENTITIES_FILE)
    sys.stderr.write("fetching {0}\n".format(ENTITIES_URL))
    try:
        with urllib.request.urlopen(ENTITIES_URL, timeout=60) as response:
            data = response.read()
    except OSError as exc:
        raise GenError("failed to fetch {0}: {1}".format(ENTITIES_URL, exc))
    tmp = dest + ".tmp"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, dest)


def read_entities(data_dir):
    """Return the parsed entities.json after checking its digest."""
    path = os.path.join(data_dir, ENTITIES_FILE)
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError as exc:
        raise GenError(
            "cannot read {0}: {1} (use --fetch to download it)".format(
                path, exc))
    digest = hashlib.sha256(data).hexdigest()
    if digest != ENTITIES_SHA256:
        raise GenError(
            "{0}: SHA-256 is {1}, but the script pins {2}; review the "
            "change of the list and update ENTITIES_SHA256".format(
                path, digest, ENTITIES_SHA256))
    try:
        return json.loads(data.decode("utf-8"))
    except ValueError as exc:
        raise GenError("{0}: invalid JSON: {1}".format(path, exc))


def parse_entities(data_dir):
    """Return a list of (name bytes, (code points...)) sorted by name bytes.

    The name excludes the leading "&" and the trailing ";". Only the entries
    whose key ends in ";" are kept.
    """
    entities = read_entities(data_dir)
    if not isinstance(entities, dict):
        raise GenError("entities.json: the top level is not an object")
    result = []
    for key, value in entities.items():
        if not key.endswith(";"):
            continue
        match = NAME_PATTERN.fullmatch(key)
        if match is None:
            raise GenError("entities.json: unexpected name {0!r}".format(key))
        codepoints = tuple(value.get("codepoints", ()))
        if not 1 <= len(codepoints) <= MAX_CODEPOINTS:
            raise GenError(
                "entities.json: {0} expands to {1} code points".format(
                    key, len(codepoints)))
        for c in codepoints:
            if (not isinstance(c, int) or c <= 0 or c > 0x10FFFF
                    or 0xD800 <= c <= 0xDFFF):
                raise GenError(
                    "entities.json: {0} has an invalid code point".format(key))
        characters = value.get("characters")
        if characters != "".join(chr(c) for c in codepoints):
            raise GenError(
                "entities.json: {0}: characters do not match code points"
                .format(key))
        result.append((match.group(1).encode("ascii"), codepoints))
    result.sort()
    return result


def generate_entity_tables(data_dir):
    entities = parse_entities(data_dir)
    two = sum(1 for _, cps in entities if len(cps) == 2)
    longest = max(len(name) for name, _ in entities)
    lines = [
        LICENSE_HEADER.rstrip("\n"),
        "",
        "/* Generated by tools/gen_entity_table.py from the WHATWG",
        " * entities.json ({0}).".format(ENTITIES_URL),
        " * Do not edit by hand.",
        " *",
        " * entities.json SHA-256: {0}".format(ENTITIES_SHA256),
        " *",
        " * To regenerate (Python 3.8 or later, maintainers only):",
        " *   python3 tools/gen_entity_table.py --fetch --data-dir <dir>",
        " * To verify that this file is up to date:",
        " *   python3 tools/gen_entity_table.py --data-dir <dir> --check",
        " *",
        " * The includer defines amark_entity_t as",
        " *   typedef struct { const char *name; uint32_t codepoints[2]; }"
        " amark_entity_t;",
        " * name is the name without \"&\" and \";\" (ASCII letters and"
        " digits). The",
        " * table holds every name of entities.json that ends in \";\","
        " sorted by the",
        " * bytes of the name. codepoints holds 1 or 2 code points, followed"
        " by a",
        " * zero (U+0000 is never part of a reference). */",
        "",
        "/* The length of the longest name. */",
        "#define AMARK_ENTITY_NAME_MAX {0}".format(longest),
        "",
        "/* {0} entries: {1} of 1, {2} of 2 code points */".format(
            len(entities), len(entities) - two, two),
        "static const amark_entity_t amark_entities[] = {",
    ]
    for name, codepoints in entities:
        padded = codepoints + (0,) * (MAX_CODEPOINTS - len(codepoints))
        lines.append("    {{\"{0}\", {{{1}}}}},".format(
            name.decode("ascii"),
            ", ".join("0x{0:06X}".format(c) for c in padded)))
    lines.append("};")
    return "\n".join(lines) + "\n"


def read_existing(path):
    try:
        with open(path, "r", encoding="utf-8", newline="") as f:
            return f.read()
    except OSError:
        return None


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Generate the named character reference table.")
    parser.add_argument(
        "--data-dir", required=True,
        help="directory holding entities.json")
    parser.add_argument(
        "--fetch", action="store_true",
        help="download entities.json from the WHATWG into --data-dir")
    parser.add_argument(
        "--check", action="store_true",
        help="compare the generated output with the existing file instead "
             "of writing it; exit non-zero on any difference")
    parser.add_argument(
        "--repo-root", default=REPO_ROOT,
        help="repository root (default: the parent of tools/)")
    args = parser.parse_args(argv)

    try:
        if args.fetch:
            fetch_entities(args.data_dir)
        content = generate_entity_tables(args.data_dir)
    except GenError as exc:
        sys.stderr.write("error: {0}\n".format(exc))
        return 2
    path = os.path.join(args.repo_root, OUTPUT)
    existing = read_existing(path)
    if args.check:
        if existing is None:
            sys.stderr.write("MISSING  {0}\n".format(OUTPUT))
        elif existing != content:
            sys.stderr.write("DIFFERS  {0}\n".format(OUTPUT))
        else:
            sys.stdout.write("OK       {0}\n".format(OUTPUT))
            sys.stdout.write("the table matches entities.json\n")
            return 0
        sys.stderr.write(
            "1 file(s) out of date; rerun without --check to regenerate\n")
        return 1
    if existing == content:
        sys.stdout.write("unchanged {0}\n".format(OUTPUT))
    else:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(content)
        sys.stdout.write("wrote     {0}\n".format(OUTPUT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
