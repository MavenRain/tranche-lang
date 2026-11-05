/* Rows for the joint threshold rule (S5 row C7): an admitted pair passes at
   Q = cap, a refused pair and Q above the cap close, one changed word in each
   part of M1, M2 and M3 fails, and a rogue key does not cancel the key of H.
   Without an argument the tool runs the short set for make check (the first
   pair, the M1 and M2 flips, ctx + 1, the cap rows and the send refusals);
   with any argument it runs the full set. One honest run is about 1900
   group_mul, about 150 s under tcc. */
#include <stdio.h>

#include "front/claimcom.h"
#include "front/jointr.h"

static uint32_t rows;
static uint32_t failed;

/* The messages are about 150 KB each at the host bounds. */
static JrM1 m1;
static JrM2 m2;
static JrM3 m3;
static JrM1 bad1;
static JrM2 bad2;
static JrM3 bad3;
static JrTable table;
static U256 x_h;
static U256 x_k;
static U256 r_h;
static U256 r_k;
static U256 r;
static U256 seed;

typedef struct {
  uint32_t a;
  uint32_t b;
  uint64_t cap;
} Pair;

#define JT_PAIRS 3u

static const Pair PAIRS[JT_PAIRS] = {{0u, 1u, 100u}, {1u, 2u, 5000u}, {2u, 0u, 100u}};

static void row(int ok, const char *name) {
  rows++;
  if (!ok) {
    failed++;
    fprintf(stderr, "FAIL jointr %s\n", name);
  }
}

/* 3 classes: juris 1 to 3 with status 1, and the pairs (caps 100 and 5000). */
static void make_table(void) {
  uint32_t a;
  uint32_t i;
  table.c = 3u;
  for (a = 0; a < table.c; a++) {
    table.juris[a] = a + 1u;
    table.status[a] = 1u;
  }
  for (i = 0; i < JT_PAIRS; i++) {
    table.admit[PAIRS[i].a][PAIRS[i].b] = 1u;
    table.cap[PAIRS[i].a][PAIRS[i].b] = PAIRS[i].cap;
  }
}

/* H of class A, K of class B with the key KEY_K, and Q under both keys. */
static int statement(JrStatement *st, uint32_t a, uint32_t b, uint64_t q, const GroupPoint *key_k) {
  U256 qw;
  elg_public(&st->key_h, &x_h);
  st->key_k = *key_k;
  u256_from_u64(&qw, q);
  elg_encrypt(&st->q_h, &st->key_h, &qw, &r);
  elg_encrypt(&st->q_k, &st->key_k, &qw, &r);
  blsag_key(&st->ctx, 23u);
  return claimcom(&st->com_h, table.juris[a], table.status[a], &r_h) && claimcom(&st->com_k, table.juris[b], table.status[b], &r_k);
}

/* M1, M2 and M3; 1 when the verdict of H (jointr_open) is WANT and, with
   THIRD, the verdict of a third party (jointr_verify) too. M3 comes only
   from jointr_open, so the rows check jointr_verify on the first pair. */
static int run(const JrStatement *st, uint32_t a, uint32_t b, uint64_t q, JrResult want, int third) {
  JrResult res = jointr_send(&m1, &table, st, q, &r, a, &r_h, &seed);
  if (res == JR_OK)
    res = jointr_select(&m2, &m1, &table, st, b, &r_k, &x_k, &seed);
  if (res != JR_OK || jointr_open(&m3, &m1, &m2, &table, st, &x_h) != want)
    return 0;
  return !third || jointr_verify(&m1, &m2, &m3, &table, st) == want;
}

static int bad(JrResult got) {
  return got == JR_BAD_PROOF || got == JR_REFUSED;
}

static void fresh(void) {
  bad1 = m1;
  bad2 = m2;
  bad3 = m3;
}

static int verify_bad(const JrStatement *st) {
  return bad(jointr_verify(&bad1, &bad2, &bad3, &table, st));
}

int main(int argc, char **argv) {
  JrStatement st;
  JrStatement alt;
  JrTable wide;
  GroupPoint key_k;
  GroupPoint big_k;
  GroupPoint rogue;
  GroupPoint j;
  GroupPoint t;
  U256 mu_h;
  U256 mu_k;
  U256 k;
  U256 qw;
  uint64_t v[JR_M_MAX];
  uint32_t m = 0;
  uint32_t i;
  char name[64];
  make_table();
  (void)argv;
  /* Fixed entropy and context for reproducible vectors only, never a live
     transfer. */
  blsag_key(&x_h, 5u);
  blsag_key(&x_k, 7u);
  blsag_key(&r_h, 11u);
  blsag_key(&r_k, 13u);
  blsag_key(&r, 17u);
  blsag_key(&seed, 19u);
  elg_public(&key_k, &x_k);
  row(statement(&st, 0u, 1u, 100u, &key_k) && run(&st, 0u, 1u, 100u, JR_OK, 1), "accept 0 1 q = cap");
  /* One changed word in each part of M1 and M2 of the pair (0, 1). */
  fresh();
  bad1.g[0].bit[5].c2.x.w[0] ^= 1u;
  row(verify_bad(&st), "G bit");
  fresh();
  bad1.g[1].proof[7].z0.limb[0] ^= 1u;
  row(verify_bad(&st), "G proof");
  fresh();
  bad1.g[1].sum.z.limb[0] ^= 1u;
  row(verify_bad(&st), "G sum");
  fresh();
  bad1.f[2].c1.x.w[0] ^= 1u;
  row(verify_bad(&st), "F");
  fresh();
  bad1.f_proof.e[1].limb[0] ^= 1u;
  row(verify_bad(&st), "F proof e");
  fresh();
  bad1.f_proof.atom[2][2].z.limb[0] ^= 1u;
  row(verify_bad(&st), "F proof z");
  fresh();
  bad2.y.c2.x.w[0] ^= 1u;
  row(verify_bad(&st), "Y");
  fresh();
  bad2.y_proof.atom[1][1].z.limb[0] ^= 1u;
  row(verify_bad(&st), "Y proof");
  fresh();
  alt = st;
  alt.ctx.limb[0] += 1u;
  row(verify_bad(&alt), "ctx + 1");
  /* The caps and the refusals of the host bounds. The refusals write BAD1,
     so M1 of the pair (0, 1) stays for the rows of the full set. */
  row(jointr_caps(v, &m, &table) && m == 2u && v[0] == 100u && v[1] == 5000u, "caps sorted distinct");
  wide = table;
  wide.cap[0][1] = (uint64_t)1 << JR_BITS;
  row(!jointr_caps(v, &m, &wide), "cap 2^32 refused");
  wide = table;
  wide.c = JR_C_MAX;
  for (i = 0; i < JR_M_MAX + 1u; i++) {
    wide.admit[i % JR_C_MAX][i / JR_C_MAX + 6u] = 1u;
    wide.cap[i % JR_C_MAX][i / JR_C_MAX + 6u] = 7u + i;
  }
  row(!jointr_caps(v, &m, &wide), "more than JR_M_MAX caps refused");
  row(jointr_send(&bad1, &table, &st, (uint64_t)1 << JR_BITS, &r, 0u, &r_h, &seed) == JR_REFUSED, "q = 2^32 refused");
  alt = st;
  u256_from_u64(&qw, 100u);
  elg_encrypt(&alt.q_k, &alt.key_k, &qw, &r_h);
  row(jointr_send(&bad1, &table, &alt, 100u, &r, 0u, &r_h, &seed) == JR_REFUSED, "two c1 refused");
  row(jointr_send(&bad1, &table, &st, 100u, &r, 3u, &r_h, &seed) == JR_REFUSED, "class out of the table refused");
  if (argc < 2) {
    printf("jointr: %u rows, %u failed\n", rows, failed);
    return failed == 0 ? 0 : 1;
  }
  /* The full set: the shares, the swap and the checks of the earlier
     messages, on the transcript of the pair (0, 1). */
  fresh();
  bad2.d.x.w[0] ^= 1u;
  row(verify_bad(&st), "d_k");
  fresh();
  bad2.d_proof.z.limb[0] ^= 1u;
  row(verify_bad(&st), "d_k proof");
  fresh();
  bad3.d.x.w[0] ^= 1u;
  row(verify_bad(&st), "d_h");
  fresh();
  bad3.d_proof.z.limb[0] ^= 1u;
  row(verify_bad(&st), "d_h proof");
  fresh();
  alt = st;
  alt.com_h = st.com_k;
  alt.com_k = st.com_h;
  row(verify_bad(&alt), "com_h and com_k swapped");
  fresh();
  bad1.f_proof.e[1].limb[0] ^= 1u;
  row(bad(jointr_select(&bad2, &bad1, &table, &st, 1u, &r_k, &x_k, &seed)), "select checks M1");
  fresh();
  bad2.y_proof.e[0].limb[0] ^= 1u;
  row(bad(jointr_open(&bad3, &m1, &bad2, &table, &st, &x_h)), "open checks M2");
  /* A second admitted pair at Q = cap, Q = 0, and the two closes. */
  snprintf(name, sizeof name, "accept %u %u q = cap", PAIRS[1].a, PAIRS[1].b);
  row(statement(&st, PAIRS[1].a, PAIRS[1].b, PAIRS[1].cap, &key_k) && run(&st, PAIRS[1].a, PAIRS[1].b, PAIRS[1].cap, JR_OK, 0), name);
  row(statement(&st, 0u, 1u, 0u, &key_k) && run(&st, 0u, 1u, 0u, JR_OK, 0), "accept 0 1 q = 0");
  row(statement(&st, 1u, 0u, 50u, &key_k) && run(&st, 1u, 0u, 50u, JR_CLOSED, 0), "refused pair 1 0 closes");
  row(statement(&st, 0u, 1u, 101u, &key_k) && run(&st, 0u, 1u, 101u, JR_CLOSED, 0), "q = cap + 1 closes");
  /* Rogue key: K picks X_K = K' - X_H. J is not K', and a share with the
     secret of K' does not verify. */
  blsag_key(&k, 29u);
  elg_public(&big_k, &k);
  group_neg(&t, &st.key_h);
  group_add(&rogue, &big_k, &t);
  row(jointr_key(&j, &mu_h, &mu_k, &st.key_h, &rogue) && !group_eq(&j, &big_k), "rogue key: J is not K'");
  row(statement(&alt, 0u, 1u, 100u, &rogue) && jointr_send(&m1, &table, &alt, 100u, &r, 0u, &r_h, &seed) == JR_OK
          && jointr_select(&m2, &m1, &table, &alt, 1u, &r_k, &k, &seed) == JR_OK && bad(jointr_open(&m3, &m1, &m2, &table, &alt, &x_h)),
      "rogue key: the share of K' does not verify");
  printf("jointr: %u rows, %u failed\n", rows, failed);
  return failed == 0 ? 0 : 1;
}
