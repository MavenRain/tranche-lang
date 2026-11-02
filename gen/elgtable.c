/* Writes the static baby step table of front/elgamal.c (ruling Q-S3-27):
   the key of j G for each j below ELG_BABY_STEPS, by open addressing over
   2 ELG_BABY_STEPS slots (key & mask, then a linear probe).
   Usage: tcc -Isrc src/front/u256.c src/front/field.c src/front/secp256k1.c
   -run gen/elgtable.c build/elg_table.c */
#include <stdio.h>
#include <string.h>

#include "front/elgamal.h"

#define SLOTS (2u * ELG_BABY_STEPS)

static ElgSlot table[SLOTS];

static void insert(uint32_t key, uint32_t j) {
  uint32_t s = key & (SLOTS - 1u);
  while (table[s].step != 0u)
    s = (s + 1u) & (SLOTS - 1u);
  table[s].key = key;
  table[s].step = j + 1u;
}

/* j G in chunks of GROUP_WALK_MAX: the next chunk starts at the last point
   plus G. */
static void fill(void) {
  GroupPoint pts[GROUP_WALK_MAX];
  GroupPoint start;
  GroupPoint gen;
  uint32_t j;
  uint32_t i;
  memset(&start, 0, sizeof start);
  group_gen(&gen);
  for (j = 0; j < ELG_BABY_STEPS; j += GROUP_WALK_MAX) {
    group_walk(pts, &start, &gen, GROUP_WALK_MAX);
    for (i = 0; i < GROUP_WALK_MAX; i++)
      insert(elg_point_key(&pts[i]), j + i);
    group_add(&start, &pts[GROUP_WALK_MAX - 1u], &gen);
  }
}

static int write_table(FILE *out) {
  uint32_t s;
  fputs("/* Made by gen/elgtable.c (ruling Q-S3-27). Do not edit. */\n", out);
  fputs("#include \"front/elgamal.h\"\n\nconst ElgSlot ELG_TABLE[2u * ELG_BABY_STEPS] = {", out);
  for (s = 0; s < SLOTS; s++)
    fprintf(out, "%s{0x%08xu, %uu},", s % 8u == 0u ? "\n  " : " ", (unsigned)table[s].key, (unsigned)table[s].step);
  fputs("\n};\n", out);
  return ferror(out) == 0;
}

int main(int argc, char **argv) {
  if (argc != 2) {
    fputs("usage: elgtable OUT\n", stderr);
    return 1;
  }
  fill();
  FILE *out = fopen(argv[1], "wb");
  int ok = out != NULL && write_table(out);
  if (out != NULL) ok = fclose(out) == 0 && ok;
  if (!ok) fprintf(stderr, "elgtable: cannot write %s\n", argv[1]);
  return ok ? 0 : 1;
}
