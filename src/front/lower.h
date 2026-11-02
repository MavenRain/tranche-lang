/* The lowering from checked values to the IR, and the `langc ir` dump.
   SHARED by the tcc kits. */
#ifndef LANG_FRONT_LOWER_H
#define LANG_FRONT_LOWER_H

#include "front/core.h"
#include "ir.h"

/* Lowers each entry of the checked program M (a PROGRAM definition where
   entry_of holds) into PROG. Writes notes to ERR, or no notes when ERR is
   NULL (check --json, Q-S3-34). Returns 1, or 0 after a diagnostic. */
int lower_program(Machine *m, IrProgram *prog, FILE *err);
/* Writes PROG as text: a `func` line for each entry, then one statement on
   each line. */
void ir_print(const IrProgram *prog, FILE *out);
/* The bound of one entry in IR units (C9c, Q-S3-32). */
typedef struct {
  uint64_t ops;    /* the IR statements and expression nodes that run */
  uint64_t stack;  /* words: the locals, the status word, the deepest expression */
  uint64_t memory; /* words: the ALLOC blocks and the prelude result blocks */
} IrBound;
/* Puts the bound of FN in OUT: a loop multiplies its body by its count, IF
   and SWITCH take the larger arm, and each sum or product stops at
   UINT64_MAX. Returns 1, or 0 after an INTERNAL diagnostic (a loop count
   that is not a constant: lower_program refuses it first). */
int ir_bound(Machine *m, const IrFunc *fn, IrBound *out);

#endif
