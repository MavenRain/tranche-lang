/* The `langc check --json` writer (S12 A2, rulings Q-S12-1 to Q-S12-8). */
#ifndef LANG_FRONT_JSON_H
#define LANG_FRONT_JSON_H

#include "front/check.h"
#include "front/lower.h"

/* Writes TEXT as a JSON string: `"`, `\\`, newline and tab get a short
   escape, other control bytes `\u00XX`, an invalid UTF-8 byte `\ufffd`,
   valid UTF-8 raw. The escapes of quorum-lang src/front/json.c. */
void json_string_n(const char *text, size_t len, FILE *out);
void json_string(const char *text, FILE *out);
/* Writes the one-line object of `langc check --json` and a newline:
   `{"diagnostics":[...],"progress":{...},"bounds":[...],"fails":[...]}` in
   this key order. DIAG gives the one diagnostic when DIAG->set, else the
   list is empty; its fix is null, or the edit of diag_fix. PATH is the
   span file, as given. When DIAG->set, bounds and fails are empty; else
   BOUNDS[i] is the bound of the entry PROG->funcs[i] (C9c, Q-S3-33), and
   FAILS holds FAIL_COUNT rows (Q-S3-35). */
/* One def of the `fails` key (C9c, Q-S3-35): the names of the Fail
   constructors in the normal form of DEF, in the constructor order. */
typedef struct {
  const char *def;
  const char *const *values;
  size_t value_count;
} FailRow;

void json_check(FILE *out, const char *path, const Diag *diag, const CheckProgress *progress,
                const IrProgram *prog, const IrBound *bounds, const FailRow *fails, size_t fail_count);

#endif
