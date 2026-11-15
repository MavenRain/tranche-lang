/* The JS back end (S9 B2): `langc build PROG --js OUT.mjs` writes an ES
   module in place of the target. See src/js.c. */
#ifndef LANG_JS_H
#define LANG_JS_H

#include <stdio.h>

#include "ir.h"

/* Writes the ES module of PROG. Returns 1, or 0 after a message on ERR. */
int js_write(const IrProgram *prog, FILE *out, FILE *err);
/* js_write in the count mode (Q-S8-32, `langc build PROG --js-count OUT.mjs`,
   gate rows only): each statement adds its IR units to the counter OPS, and
   the module exports OPS as $ops. */
int js_write_count(const IrProgram *prog, FILE *out, FILE *err);

#endif
