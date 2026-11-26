/* SPDX-License-Identifier: MIT
 * Copyright (c) The Amark Authors */

/*
 * The S-expression dump of a subtree, the dump writer and the dump flags.
 */
#ifndef AMARK_DUMP_H
#define AMARK_DUMP_H

#include <amark/export.h>
#include <amark/error.h>
#include <amark/node.h>

AMARK_BEGIN_DECLS

/* The writer an extension type's dump function writes its attributes to
 * (opaque). */
typedef struct amark_dump_writer amark_dump_writer_t;

AMARK_END_DECLS

#endif /* AMARK_DUMP_H */
