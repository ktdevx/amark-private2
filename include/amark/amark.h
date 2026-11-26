/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The umbrella header of Amark: including <amark/amark.h> makes the whole public
 * API available.
 *
 * The headers are listed in the canonical include order (design, PublicHeaders):
 *   1. Common base (foundation):  export.h, version.h, error.h, allocator.h,
 *                                 buffer.h, utf8.h, unicode.h, entity.h
 *   2. Syntax tree (syntax-tree): node.h, document.h, value.h, spelling.h,
 *                                 iterator.h, dump.h, extension.h
 *   3. Renderer and HTML (html-renderer): render.h, html.h
 *   4. Parser (block-parser, inline-parser): scan.h, parser.h, block.h,
 *                                 inline.h, commonmark.h
 *   5. Markdown output (markdown-renderer): markdown.h
 * Groups 2-5 are added by those later specs, each header at its own position in
 * this order (not appended at the end); groups that do not exist yet are skipped.
 * The test lint.public_headers checks that every public header is included here,
 * in this order.
 * Every public header also compiles on its own; the order only keeps the content
 * of this file unique.
 */
#ifndef AMARK_AMARK_H
#define AMARK_AMARK_H

/* 1. Common base */
#include <amark/export.h>
#include <amark/version.h>
#include <amark/error.h>
#include <amark/allocator.h>
#include <amark/buffer.h>
#include <amark/utf8.h>
#include <amark/unicode.h>
#include <amark/entity.h>

/* 2. Syntax tree */
#include <amark/node.h>
#include <amark/document.h>
#include <amark/value.h>
#include <amark/spelling.h>
#include <amark/iterator.h>
#include <amark/dump.h>
#include <amark/extension.h>

#endif /* AMARK_AMARK_H */
