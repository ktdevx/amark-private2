/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The umbrella header of Amark: including <amark/amark.h> makes the whole public
 * API available.
 *
 * The headers are listed in the canonical include order (design, PublicHeaders):
 *   1. Common base (foundation):  export.h, version.h, error.h, allocator.h,
 *                                 buffer.h, utf8.h, unicode.h
 *   2. Syntax tree (syntax-tree): node.h, document.h, token.h, iterator.h,
 *                                 dump.h, extension.h
 *   3. Renderer and HTML (html-renderer): render.h, html.h
 *   4. Parser (block-parser, inline-parser): entity.h, scan.h, parser.h,
 *                                 block.h, inline.h, commonmark.h
 *   5. Markdown output (markdown-renderer): markdown.h
 * A header is added at its own position in this order, not appended at the end.
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

#endif /* AMARK_AMARK_H */
