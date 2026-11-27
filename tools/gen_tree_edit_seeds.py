#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) The Amark Authors
"""Generate the seed corpus of the edit fuzzer (fuzz/tree_edit_fuzz.c).

The seeds are written to fuzz/corpus/tree_edit/, one file each, in the input
format that fuzz/tree_edit_fuzz.c describes: an operation byte (the operation,
the document, the failure injection) followed by its arguments. The tables
below (OPS, TEXTS, SOURCES, the numbers, the token kinds) mirror the tables of
the fuzz target and must be kept in the same order as them. Nodes are picked
by their index in the ring of their document (0xC0 + index): the document node
is 0, the other nodes follow in the order of their creation (a destroyed node
shifts the later ones).

The seeds are hand-assembled sequences (INPUT.md 3.1 built record by record,
then queried, edited, copied and imported; definitions and their lookup; a
fenced code block; the line operations; a list; tabs; sources; free content
and content tokens; refused structural edits; a sweep of injected failures)
and the regressions found by replaying random inputs (regression_*).

This is a maintainer tool. The build and the tests never run it; the
generated corpus is committed to the repository.

Usage:
    # (Re)generate the corpus (the directory then holds exactly the seeds).
    python3 tools/gen_tree_edit_seeds.py

    # Compare the generated seeds with the committed corpus.
    # Exits with a non-zero status when they differ.
    python3 tools/gen_tree_edit_seeds.py --check

Requires Python 3.8 or later and the standard library only.
"""

import argparse
import os
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CORPUS = os.path.join("fuzz", "corpus", "tree_edit")

# Inputs found by the random replay, kept as they were found (bytes.fromhex).
DERIVED_VALUE_REFUSED_BY_SETTERS = bytes.fromhex(
    "0800000002000300040007000b0010000b000c000b000b01c0c101c1c201c2c3"
    "01c3c401c4c501c4c601c6c701c6c801c6c901c4ca0bc000020bc100020bc200"
    "020bc300020bc400020cc1000000020cc1010000020cc3001002020cc3010002"
    "020fc400a8800fc401048011c5008003000111c6008400888011c70085020001"
    "11c8008800818011c9008903000111c9008d00000111c6008e00888011ca008f"
    "80000114c2010114c20401269700c701e354574f9711c701e354574f17c701e3"
    "54574f9600c6010216c601029800c60118c6019a00c9019a02c9011ac9019b00"
    "c49b13c41bc41bc21cc38116c202099600c2010216c2010216c2030018c20326"
    "2700ad00c01f")

TEXT_TOKEN_ON_INVALID_UTF8 = bytes.fromhex(
    "0800000002000300040007000b0010000b000c000b000b01c0c101c1c201c2c3"
    "01c3c401c4c501c4c601c6c701c6c801c60010000b000c000b000b01c0c101c1"
    "c201c2c301c3c401c4c501c4c601c6c701c6c801c6c901c4ca0bc000020bc100"
    "020bc200020bc300020bc400020cc1000000020cc1010000020cc3001002020c"
    "c3010002020fc40004800fc901c4ca0bc000020bc100020bc200020bc300020c"
    "17000000020cc1010000020cc3001002020cc3010002020fc40004800fc40104"
    "8011c5008003000111c6008400888011c7008502000111c8008800818011c900"
    "8903000111c9008d00000111c6008e00888011ca008f80000114c2010114c204"
    "01269700c701e354574f9711c701e354574f17c701e354574f9600c6010216c6"
    "01029800c60118c6019a00c9019a02c9011ac9019b00c49b13c41bc41bc21cc3"
    "8116c202099600c2010216c2010216c2030018c28d00000111c6008e00888011"
    "03262700ad0000c0670029c1cb0129c11f")


OPS = [
    "create", "append_child", "prepend_child", "insert_before", "insert_after", "replace",
    "unlink", "destroy", "add_source", "append_source", "finish_source", "set_lines",
    "add_margin", "add_blank_line", "add_token", "add_content", "set_free_content",
    "add_content_token", "move_content_tokens", "split_content_token", "record_int",
    "record_string", "set_int", "set_string", "unset", "set_inline_text",
    "discard_facet_spelling", "discard_spelling", "discard_margin", "move_lines",
    "remove_lines", "trim_content_start", "replace_content", "copy", "import",
    "import_children", "find_definition", "verbatim", "verbatim_all", "check_lossless",
    "get", "equal", "position", "iterate", "columns", "dump",
]
OPCODE = {name: i for i, name in enumerate(OPS)}

TEXTS = [
    "", "foo", "Foo", "a b", "x\ny", "a\r\nb\rc", "\n", "a\n\n", "  \u00df ", "\u1e9e",
    "[x]", None, "a\tb", "http://a", "&amp;", "\\*", "#", "```", "- ", "> ", "    code",
    "a\n \nb", "one *two*", "bar",
]
SOURCES = [
    "> - one *two\n>   three* four\n", "# a #\n", "```\nx\n```\n", "p1\np2\n", ">\t\tfoo\n",
    "a\r\nb\rc", "", "\ufeffbom\n", "[foo]: /url \"t\"\n", "x", "  \n\n", "[t][Foo] [foo]\n",
    "- a\n\n- b\n", "> a\nb\n",
]

# Node types.
DOCUMENT, BLOCK_QUOTE, LIST, ITEM, CODE_BLOCK, HTML_BLOCK, PARAGRAPH, HEADING = range(1, 9)
THEMATIC_BREAK, LINK_DEFINITION, TEXT, SOFTBREAK, LINEBREAK, CODE, HTML_INLINE = range(9, 16)
EMPH, STRONG, LINK, IMAGE = range(16, 20)

# Token kinds (the byte that picks the kind; < 0x80 is TEXT for content tokens,
# MARKER for layout tokens).
def kind(k):
    for b in range(0x80, 0x100):
        if b % 11 == k:
            return b
    raise ValueError(k)

TEXT_K, MARKER_K, SPACE_K, DELIM_K, ESCAPE_K, CHARREF_K, REPLACED_K, LB_K, TABREM_K = range(1, 10)


def N(i):
    """The node at index i of the ring of the document."""
    assert 0 <= i < 48
    return 0xC0 + i


def OTHER(i):
    assert 0 <= i < 15
    return 0xF0 + i


NULL = 0xFF


def T(text):
    """A text argument."""
    if text in TEXTS:
        return [TEXTS.index(text)]
    raw = text.encode() if isinstance(text, str) else text
    assert len(raw) <= 31
    return [0xE0 | len(raw)] + list(raw)


def NUM(n):
    table = [0, 1, 2, 3, 4, 5, 6, 7, -1, 9, 10, 999999999, 1000000000]
    return [table.index(n)]


class Seed:
    def __init__(self):
        self.out = []

    def op(self, name, *args, doc=0, inject=None, once=False):
        code = OPCODE[name] | (0x40 if doc else 0)
        if inject is not None:
            code |= 0x80
        self.out.append(code)
        if inject is not None:
            self.out.append((inject & 0x0F) | (0x10 if once else 0))
        for a in args:
            if isinstance(a, list):
                self.out.extend(a)
            else:
                self.out.append(a)
        return self

    def bytes(self):
        return bytes(self.out)


def build_3_1_fixed(s, doc=0):
    """INPUT.md 3.1, with the exact tokens."""
    s.op("add_source", SOURCES.index("> - one *two\n>   three* four\n"), 0, doc=doc)
    for t in (BLOCK_QUOTE, LIST, ITEM, PARAGRAPH, TEXT, EMPH, TEXT, SOFTBREAK, TEXT, TEXT):
        s.op("create", t, doc=doc)
    for parent, child in ((0, 1), (1, 2), (2, 3), (3, 4), (4, 5), (4, 6), (6, 7), (6, 8),
                          (6, 9), (4, 10)):
        s.op("append_child", N(parent), N(child), doc=doc)
    for block in (0, 1, 2, 3, 4):
        s.op("set_lines", N(block), 0, 2, doc=doc)
    s.op("add_margin", N(1), 0, 0x00, 0, 2, doc=doc)
    s.op("add_margin", N(1), 1, 0x00, 0, 2, doc=doc)
    s.op("add_margin", N(3), 0, 0x10, 2, 2, doc=doc)
    s.op("add_margin", N(3), 1, 0x00, 2, 2, doc=doc)
    s.op("add_content", N(4), 0, 4, 0x80, doc=doc)
    s.op("add_content", N(4), 1, 4, 0x80, doc=doc)
    # add_content_token: node, leaf, start, end, kind, facet.
    # start 0x80|n: n; end < 0x80: start + 1 + end % 4.
    s.op("add_content_token", N(5), 0, 0x80, 3, 0, 1, doc=doc)                 # "one " [0,4)
    s.op("add_content_token", N(6), 0, 0x84, 0, kind(DELIM_K), 0x80, doc=doc)  # "*" [4,5)
    s.op("add_content_token", N(7), 0, 0x85, 2, 0, 1, doc=doc)                 # "two" [5,8)
    s.op("add_content_token", N(8), 0, 0x88, 0, kind(LB_K), 0x80, doc=doc)     # "\n" [8,9)
    s.op("add_content_token", N(9), 0, 0x89, 3, 0, 1, doc=doc)                 # "thre" [9,13)
    s.op("add_content_token", N(9), 0, 0x8D, 0, 0, 1, doc=doc)                 # "e" [13,14)
    s.op("add_content_token", N(6), 0, 0x8E, 0, kind(DELIM_K), 0x80, doc=doc)  # "*" [14,15)
    s.op("add_content_token", N(10), 0, 0x8F, 0x80, 0, 1, doc=doc)             # " four" [15,20)
    # The list: bullet, "-".
    s.op("record_int", N(2), 1, NUM(1), doc=doc)
    s.op("record_int", N(2), 4, NUM(1), doc=doc)
    return s


def seeds():
    out = {}

    s = Seed()
    build_3_1_fixed(s)
    s.op("check_lossless", 0)
    s.op("verbatim_all")
    s.op("verbatim", N(0), 0)
    s.op("verbatim", N(4), 1)
    s.op("get", N(7), 1)
    s.op("get", N(2), 3)
    s.op("position", N(9))
    s.op("position", N(1))
    s.op("iterate", N(0))
    s.op("dump", N(0), 0x1F)
    s.op("equal", N(5), N(10), 3)
    out["input_3_1_build"] = s.bytes()

    s = Seed()
    build_3_1_fixed(s)
    s.op("verbatim_all")
    s.op("set_string", N(7), 1, T("TWO"), inject=0)
    s.op("set_string", N(7), 1, T("TWO"), inject=1, once=True)
    s.op("set_string", N(7), 1, T("TWO"))
    s.op("set_int", N(6), 1, NUM(2), inject=0)
    s.op("set_int", N(6), 1, NUM(2))
    s.op("unset", N(6), 1, inject=0)
    s.op("unset", N(6), 1)
    s.op("discard_facet_spelling", N(9), 1, inject=0)
    s.op("discard_facet_spelling", N(9), 1, inject=2)
    s.op("discard_facet_spelling", N(9), 1)
    s.op("discard_spelling", N(4), inject=0)
    s.op("discard_spelling", N(4), inject=3, once=True)
    s.op("discard_spelling", N(4))
    s.op("discard_spelling", N(2))
    s.op("discard_margin", N(3), 0x81)
    s.op("set_int", N(2), 2, NUM(9))
    s.op("set_int", N(2), 1, NUM(2), inject=0)
    s.op("set_int", N(2), 1, NUM(2))
    s.op("set_int", N(2), 3, NUM(0))
    s.op("unset", N(2), 3)
    s.op("verbatim_all")
    s.op("check_lossless", 0)
    s.op("dump", N(0), 0x1F, inject=0)
    out["input_3_1_edits"] = s.bytes()

    s = Seed()
    build_3_1_fixed(s)
    s.op("verbatim_all")
    s.op("copy", N(1), inject=0)
    s.op("copy", N(1), inject=5, once=True)
    s.op("copy", N(1))                                  # node 11-20
    s.op("append_child", N(0), N(11))
    s.op("import", N(1), inject=0)
    s.op("import", N(1), inject=7, once=True)
    s.op("import", N(1))                                # B: nodes 1-10
    s.op("append_child", N(0), N(1), doc=1)
    s.op("import_children", N(0), 1, 1, N(0), doc=1, inject=0)
    s.op("import_children", N(0), 0, 1, N(0), doc=1)
    s.op("replace_content", N(4), 1, N(4), doc=1, inject=1)
    s.op("replace_content", N(4), 1, N(4), doc=1)
    s.op("replace_content", N(14), 0, N(14))
    s.op("set_free_content", N(14), T("one *two*"))
    s.op("verbatim_all")
    s.op("verbatim_all", doc=1)
    s.op("check_lossless", 0, doc=1)
    s.op("equal", N(1), N(11), 1)
    s.op("equal", N(1), OTHER(1), 3)
    s.op("destroy", N(11))
    s.op("verbatim_all")
    out["copy_import"] = s.bytes()

    s = Seed()
    s.op("add_source", SOURCES.index("[foo]: /url \"t\"\n"), 0)
    s.op("add_source", SOURCES.index("[t][Foo] [foo]\n"), 0)
    s.op("create", LINK_DEFINITION)     # 1
    s.op("create", PARAGRAPH)           # 2
    s.op("create", LINK)                # 3
    s.op("create", TEXT)                # 4
    s.op("create", LINK)                # 5
    s.op("create", LINK_DEFINITION)     # 6
    s.op("append_child", N(0), N(1))
    s.op("append_child", N(0), N(2))
    s.op("append_child", N(2), N(3))
    s.op("append_child", N(3), N(4))
    s.op("append_child", N(2), N(5))
    s.op("record_string", N(1), 1, T("foo"))
    s.op("record_string", N(1), 2, T("http://a"))
    s.op("record_string", N(1), 3, T("bar"))
    s.op("record_int", N(3), 4, NUM(2))      # full
    s.op("record_string", N(3), 3, T("Foo"))
    s.op("record_string", N(4), 1, T("t"))
    s.op("record_int", N(5), 4, NUM(4))      # shortcut
    s.op("record_string", N(5), 3, T("foo"))
    s.op("find_definition", T("FOO"))
    s.op("find_definition", T("foo"), inject=0)
    s.op("get", N(3), 1)
    s.op("get", N(3), 2, inject=0)
    s.op("get", N(5), 1)
    s.op("record_string", N(6), 1, T("Foo"))
    s.op("insert_before", N(1), N(6))
    s.op("find_definition", T("foo"))
    s.op("find_definition", T("foo"), inject=1)
    s.op("set_string", N(6), 1, T("other"), inject=0)
    s.op("set_string", N(6), 1, T("other"))
    s.op("get", N(5), 1, inject=0)
    s.op("get", N(5), 1)
    s.op("set_string", N(5), 1, T("x"))       # becomes inline
    s.op("insert_after", N(4), N(5))          # link in link: refused
    s.op("append_child", N(3), N(5))          # link in link: refused
    s.op("unlink", N(1))
    s.op("find_definition", T("foo"))
    s.op("equal", N(3), N(5), 1)
    s.op("dump", N(0), 0x07)
    out["definitions"] = s.bytes()

    s = Seed()
    s.op("add_source", SOURCES.index("```\nx\n```\n"), 0)
    s.op("create", CODE_BLOCK)          # 1
    s.op("append_child", N(0), N(1))
    s.op("set_lines", N(1), 0, 3)
    # Layout tokens: the opening fence (marker 0-3), the closing fence.
    s.op("add_token", N(1), 0, 0, 2, 0, 0)
    s.op("add_content", N(1), 1, 0, 0x80)
    s.op("add_token", N(1), 2, 0, 2, 0, 0)
    s.op("record_int", N(1), 3, NUM(2))    # fenced
    s.op("record_int", N(1), 4, NUM(1))    # backtick
    s.op("record_int", N(1), 5, NUM(3))
    s.op("add_content_token", N(1), 0, 0x80, 0x80, 0, 2)   # "x" literal
    s.op("check_lossless", 0)
    s.op("verbatim_all")
    s.op("get", N(1), 2)
    s.op("get", N(1), 2, inject=0)
    s.op("copy", N(1))                   # 2
    s.op("append_child", N(0), N(2))
    s.op("discard_facet_spelling", N(2), 2, inject=0)
    s.op("discard_facet_spelling", N(2), 2)
    s.op("set_string", N(1), 2, T("a\n\n"), inject=1)
    s.op("set_string", N(1), 2, T("a\n\n"))
    s.op("set_string", N(1), 1, T("foo"))
    s.op("set_string", N(1), 1, T("x\ny"))    # invalid: line break
    s.op("set_int", N(1), 3, NUM(1))
    s.op("set_free_content", N(2), T("a\r\nb\rc"))
    s.op("get", N(2), 2)
    s.op("set_string", N(2), 2, T(""))
    s.op("set_string", N(2), 2, T("\n"))
    s.op("verbatim_all")
    s.op("dump", N(0), 0x1F)
    out["code_block"] = s.bytes()

    s = Seed()
    s.op("add_source", SOURCES.index("p1\np2\n"), 0)
    s.op("add_source", SOURCES.index("- a\n\n- b\n"), 0)
    s.op("create", PARAGRAPH)   # 1
    s.op("create", PARAGRAPH)   # 2
    s.op("create", PARAGRAPH)   # 3
    s.op("append_child", N(0), N(1))
    s.op("append_child", N(0), N(2))
    s.op("set_lines", N(1), 0, 2)
    s.op("add_content", N(1), 0, 0, 0x80)
    s.op("add_content", N(1), 1, 0, 0x80)
    s.op("copy", N(1))          # 4: shares the content
    s.op("move_lines", N(1), N(2), 0xC0, inject=0)
    s.op("move_lines", N(1), N(2), 0xC0, inject=1, once=True)
    s.op("move_lines", N(1), N(2), 0xC0)
    s.op("remove_lines", N(4), 0x80, inject=0)
    s.op("remove_lines", N(4), 0x80)
    s.op("trim_content_start", N(2), 1, 0, 0, inject=0)
    s.op("trim_content_start", N(2), 1, 0, 0)
    s.op("remove_lines", N(1), 0x80)
    s.op("set_lines", N(3), 2, 3)
    s.op("append_child", N(0), N(3))
    s.op("add_content", N(3), 2, 2, 0x80)
    s.op("add_content", N(3), 3, 0, 0x80)
    s.op("add_content", N(3), 4, 2, 0x80)
    s.op("remove_lines", N(3), 0xC1, inject=2)
    s.op("remove_lines", N(3), 0xC1)
    s.op("check_lossless", 1)
    s.op("verbatim_all")
    out["line_operations"] = s.bytes()

    s = Seed()
    s.op("add_source", SOURCES.index("- a\n\n- b\n"), 0)
    s.op("create", LIST)        # 1
    s.op("create", ITEM)        # 2
    s.op("create", PARAGRAPH)   # 3
    s.op("create", ITEM)        # 4
    s.op("create", PARAGRAPH)   # 5
    s.op("create", TEXT)        # 6
    s.op("create", TEXT)        # 7
    for parent, child in ((0, 1), (1, 2), (2, 3), (1, 4), (4, 5), (3, 6), (5, 7)):
        s.op("append_child", N(parent), N(child))
    s.op("set_lines", N(0), 0, 3)
    s.op("set_lines", N(1), 0, 3)
    s.op("set_lines", N(2), 0, 1)
    s.op("set_lines", N(3), 0, 1)
    s.op("set_lines", N(4), 2, 1)
    s.op("set_lines", N(5), 2, 1)
    s.op("add_margin", N(2), 0, 0x10, 0, 2)
    s.op("add_margin", N(4), 2, 0x10, 0, 2)
    s.op("add_blank_line", N(1), 1, 0, 0)
    s.op("add_content", N(3), 0, 2, 0x80)
    s.op("add_content", N(5), 2, 2, 0x80)
    s.op("add_content_token", N(6), 0, 0x80, 0x80, 0, 1)
    s.op("add_content_token", N(7), 0, 0x80, 0x80, 0, 1)
    s.op("record_int", N(1), 1, NUM(1))
    s.op("record_int", N(1), 4, NUM(1))
    s.op("check_lossless", 0)
    s.op("verbatim_all")
    s.op("get", N(1), 3)
    s.op("set_int", N(1), 3, NUM(1), inject=0)
    s.op("set_int", N(1), 3, NUM(1))
    s.op("unset", N(1), 3)
    s.op("set_int", N(1), 1, NUM(2), inject=0)
    s.op("set_int", N(1), 1, NUM(2), inject=1, once=True)
    s.op("set_int", N(1), 1, NUM(2))
    s.op("set_int", N(1), 2, NUM(10))
    s.op("replace", N(2), N(4))
    s.op("insert_before", N(4), N(2))
    s.op("prepend_child", N(1), N(4))
    s.op("discard_spelling", N(1), inject=0)
    s.op("discard_spelling", N(1))
    s.op("verbatim_all")
    s.op("destroy", N(1))
    s.op("verbatim_all")
    out["list_items"] = s.bytes()

    s = Seed()
    s.op("add_source", SOURCES.index(">\t\tfoo\n"), 0)
    s.op("create", BLOCK_QUOTE)   # 1
    s.op("create", CODE_BLOCK)    # 2
    s.op("append_child", N(0), N(1))
    s.op("append_child", N(1), N(2))
    s.op("set_lines", N(1), 0, 1)
    s.op("set_lines", N(2), 0, 1)
    # ">" and the first column of the tab: margin [0,2).
    s.op("add_margin", N(1), 0, 0, 0, 2)
    # The indentation [2,6): space.
    s.op("add_token", N(2), 0, 2, 3, kind(SPACE_K), 0)
    s.op("add_content", N(2), 0, 6, 0x80)
    # The content "  foo": the rest of the tab, then the text.
    s.op("add_content_token", N(2), 0, 0x80, 1, kind(TABREM_K), 2)
    s.op("add_content_token", N(2), 0, 0x82, 0x80, 0, 2)
    s.op("record_int", N(2), 3, NUM(1))    # indented
    s.op("columns", 0, 3)
    s.op("columns", 0, 3, inject=0)
    s.op("position", N(2), inject=0)
    s.op("check_lossless", 0)
    s.op("verbatim_all")
    s.op("get", N(2), 2)
    s.op("trim_content_start", N(2), 2, 0, 0)
    s.op("dump", N(0), 0x1F)
    out["tabs"] = s.bytes()

    s = Seed()
    s.op("add_source", SOURCES.index("a\r\nb\rc"), 0x80)        # not finished
    s.op("append_source", 0, T("\r"))
    s.op("append_source", 0, T("\nd"), inject=0)
    s.op("append_source", 0, T("\nd"))
    s.op("finish_source", 0, inject=0)
    s.op("finish_source", 0)
    s.op("finish_source", 0)
    s.op("append_source", 0, T("x"))
    s.op("add_source", SOURCES.index("\ufeffbom\n"), 0)
    s.op("add_source", SOURCES.index(""), 0)
    s.op("add_source", SOURCES.index("x"), 0x7F)                # invalid flags
    s.op("add_source", 0xE3, 0x41, 0x09, 0x42, 0)               # raw "A\tB"
    s.op("add_source", SOURCES.index("  \n\n"), 0, inject=0)
    s.op("columns", 0x80, 1)
    s.op("columns", 4, 2)
    s.op("check_lossless", 1)
    s.op("check_lossless", 9)
    s.op("create", PARAGRAPH)   # 1
    s.op("set_lines", N(1), 1, 1)
    s.op("add_content", N(1), 1, 0, 0x80)
    s.op("dump", N(0), 0x10)
    out["sources"] = s.bytes()

    s = Seed()
    s.op("create", PARAGRAPH)            # 1
    s.op("create", EMPH)                 # 2
    s.op("create", TEXT)                 # 3
    s.op("create", HEADING)              # 4
    s.op("append_child", N(0), N(1))
    s.op("append_child", N(1), N(2))
    s.op("append_child", N(2), N(3))
    s.op("set_free_content", N(1), T("one *two*"))
    s.op("add_content_token", N(3), 0, 0x80, 3, 0, 1)             # "one "
    s.op("add_content_token", N(2), 0, 0x84, 0, kind(DELIM_K), 0x80)
    s.op("add_content_token", N(2), 0, 0x88, 0, kind(DELIM_K), 0x80)
    s.op("split_content_token", N(3), 0, 2, inject=0)
    s.op("split_content_token", N(3), 0, 2)
    s.op("move_content_tokens", N(3), N(2), 1, 1)
    s.op("move_content_tokens", N(2), N(3), 1, 1, inject=0)
    s.op("move_content_tokens", N(2), N(3), 1, 1)
    s.op("set_inline_text", N(4), T("x\ny"), inject=0)
    s.op("set_inline_text", N(4), T("x\ny"), inject=2)
    s.op("set_inline_text", N(4), T("x\ny"))
    s.op("append_child", N(0), N(4))
    s.op("replace_content", N(4), 0, N(1))
    s.op("replace_content", N(4), 0, N(1), inject=3)
    s.op("set_int", N(4), 1, NUM(7))
    s.op("set_int", N(4), 1, NUM(6))
    s.op("set_int", N(4), 2, NUM(2), inject=0)
    s.op("set_int", N(4), 2, NUM(2))
    s.op("set_inline_text", N(1), T("a\r\nb\rc"))
    s.op("verbatim", N(1), 1)
    s.op("dump", N(0), 0x1F)
    out["free_content_tokens"] = s.bytes()

    s = Seed()
    s.op("create", BLOCK_QUOTE)      # 1
    s.op("create", PARAGRAPH)        # 2
    s.op("create", TEXT)             # 3
    s.op("create", LINK)             # 4
    s.op("create", IMAGE)            # 5
    s.op("create", LIST)             # 6
    s.op("create", ITEM)             # 7
    s.op("create", 0)                # invalid type
    s.op("create", 20)               # invalid type
    s.op("append_child", N(0), N(3))   # text under the document: refused
    s.op("append_child", N(0), N(1))
    s.op("append_child", N(1), N(2))
    s.op("append_child", N(2), N(4))
    s.op("append_child", N(4), N(5))
    s.op("append_child", N(5), N(3))
    s.op("append_child", N(2), N(1))   # cycle: refused
    s.op("append_child", N(0), N(7))   # item under the document: refused
    s.op("append_child", N(6), N(7))
    s.op("insert_after", N(1), N(6))
    s.op("insert_before", N(0), N(6))  # the document as anchor: refused
    s.op("prepend_child", N(0), N(6))
    s.op("replace", N(1), N(2))
    s.op("replace", N(2), N(1))
    s.op("replace", N(0), N(1))
    s.op("unlink", N(0))
    s.op("unlink", N(4))
    s.op("insert_after", N(6), N(4))   # link under the document: refused
    s.op("append_child", N(0), OTHER(0))
    s.op("append_child", NULL, N(1))
    s.op("destroy", N(0))
    s.op("destroy", N(6))
    s.op("destroy", NULL)
    s.op("set_inline_text", N(2), T("a\n \nb"))
    s.op("iterate", N(0))
    s.op("verbatim_all")
    s.op("equal", N(1), N(1), 3)
    s.op("dump", NULL, 0x1F)
    out["structure"] = s.bytes()

    s = Seed()
    build_3_1_fixed(s)
    build_3_1_fixed(s, doc=1)
    for budget in range(0, 8):
        s.op("import", N(1), inject=budget)
        s.op("import_children", N(4), 0, 1, N(4), inject=budget)
        s.op("replace_content", N(4), 1, N(4), inject=budget)
        s.op("discard_spelling", N(4), inject=budget, once=True)
        s.op("verbatim_all", inject=budget)
        s.op("check_lossless", 0, inject=budget)
        s.op("copy", N(3), inject=budget, once=True)
    out["oom_sweep"] = s.bytes()

    # Regressions found by the random replay (see the regression tests in
    # tests/tree): a token of a dropped facet; a derived value that the setters
    # refuse, kept by a drop; a TEXT token over invalid UTF-8.
    s = Seed()
    build_3_1_fixed(s)
    s.op("set_string", N(7), 1, T("TWO"))
    s.op("add_content_token", N(7), 0, 0x85, 2, 0, 1)
    s.op("move_content_tokens", N(9), N(7), 0, 1)
    s.op("set_string", N(4), 0, T("x"))
    s.op("discard_facet_spelling", N(1), 1)
    out["regression_dropped_facet_token"] = s.bytes()
    out["regression_derived_value_refused_by_setters"] = DERIVED_VALUE_REFUSED_BY_SETTERS
    out["regression_text_token_on_invalid_utf8"] = TEXT_TOKEN_ON_INVALID_UTF8

    # Layout tokens of a facet whose kind does not match the bytes of their
    # columns (an ESCAPE or a CHAR_REF over NUL or an invalid byte, an ESCAPE
    # for a RAW label), all refused, then matching ones.
    s = Seed()
    s.op("add_source", T(b"\\\0\n\\\xff\na\xff\na\0\n\\*\n"), 0)
    s.op("create", CODE_BLOCK)          # 1
    s.op("create", LINK_DEFINITION)     # 2
    s.op("create", CODE_BLOCK)          # 3
    s.op("create", CODE_BLOCK)          # 4
    s.op("create", CODE_BLOCK)          # 5
    s.op("append_child", N(0), N(1))
    s.op("append_child", N(0), N(2))
    # set_lines: first % (lines + 1), count % (lines + 2); 5 lines.
    s.op("set_lines", N(1), 0, 1)
    s.op("set_lines", N(3), 1, 1)
    s.op("set_lines", N(4), 2, 1)
    s.op("set_lines", N(2), 3, 1)
    s.op("set_lines", N(5), 4, 1)
    # add_token: block, line, start, end (start + 1 + end % 4), kind, facet
    # (0x80 | n: facet n % (count + 2); below 0x80: none).
    s.op("add_token", N(1), 0, 0, 1, kind(ESCAPE_K), 0x86)    # info: refused
    s.op("add_token", N(3), 1, 0, 1, kind(ESCAPE_K), 0x86)    # info: refused
    s.op("add_token", N(4), 2, 0, 1, kind(CHARREF_K), 0x86)   # info: refused
    s.op("add_token", N(4), 2, 0, 1, kind(TEXT_K), 0x86)      # info: refused
    s.op("add_token", N(2), 3, 0, 1, kind(ESCAPE_K), 0x86)    # label: refused
    s.op("add_token", N(1), 0, 0, 1, kind(ESCAPE_K), 0)       # structural
    s.op("add_token", N(5), 4, 0, 1, kind(ESCAPE_K), 0x86)    # info "*"
    s.op("get", N(5), 1)
    s.op("discard_facet_spelling", N(5), 1)
    s.op("check_lossless", 0)
    out["regression_layout_token_bytes"] = s.bytes()

    out["empty"] = b""
    out["one_byte"] = b"\x00"
    return out


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Generate the seed corpus of the edit fuzzer.")
    parser.add_argument(
        "--check", action="store_true",
        help="compare the generated seeds with the existing corpus instead of "
             "writing them; exit non-zero on any difference")
    parser.add_argument(
        "--repo-root", default=REPO_ROOT,
        help="repository root (default: the parent of tools/)")
    args = parser.parse_args(argv)

    directory = os.path.join(args.repo_root, CORPUS)
    expected = seeds()
    existing = sorted(os.listdir(directory)) if os.path.isdir(directory) else []
    if args.check:
        problems = 0
        for name in sorted(expected):
            path = os.path.join(directory, name)
            if not os.path.isfile(path):
                sys.stderr.write("MISSING  {0}\n".format(os.path.join(CORPUS, name)))
                problems += 1
                continue
            with open(path, "rb") as f:
                if f.read() != expected[name]:
                    sys.stderr.write("DIFFERS  {0}\n".format(os.path.join(CORPUS, name)))
                    problems += 1
        for name in existing:
            if name not in expected:
                sys.stderr.write("EXTRA    {0}\n".format(os.path.join(CORPUS, name)))
                problems += 1
        if problems:
            sys.stderr.write("{0} file(s) out of date; rerun without --check to "
                             "regenerate\n".format(problems))
            return 1
        sys.stdout.write("OK       {0} ({1} seeds)\n".format(CORPUS, len(expected)))
        return 0
    os.makedirs(directory, exist_ok=True)
    for name in existing:
        if name not in expected:
            os.remove(os.path.join(directory, name))
            sys.stdout.write("removed   {0}\n".format(os.path.join(CORPUS, name)))
    for name in sorted(expected):
        path = os.path.join(directory, name)
        old = None
        if os.path.isfile(path):
            with open(path, "rb") as f:
                old = f.read()
        if old == expected[name]:
            continue
        with open(path, "wb") as f:
            f.write(expected[name])
        sys.stdout.write("wrote     {0}\n".format(os.path.join(CORPUS, name)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
