/* Rows for the withdraw proof (tranche-lang S7 row C5, Q-S7-10 (c)): an
   honest proof passes, the prover refuses W 2^P above MU, MU at 2^160 and W
   at 2^128, the verifier refuses W at 2^128 before any other check,
   another statement and one changed word fail, and honest proofs pass at
   MU = 2^160 - 1 with W = 2^128 - 1 and at MU = W 2^P. Without an argument
   the tool runs this short set for make check; with any argument it runs
   the full set (adds the flip in SUM, the forged proof of W 2^P above MU,
   and an honest proof at W = 0).
   One verify checks 160 bit proofs, five times the bits of one transfer
   range proof, so the flip rows reuse one honest proof. */
#include <stdio.h>

#include "front/blsag.h"
#include "front/withdraw.h"

#define WT_W 3u
/* WT_W 2^P = 12884901888 is below WT_MU, WT_W_OVER 2^P = 21474836480 is
   above it. */
#define WT_W_OVER 5u
#define WT_MU 20000000000ull

static uint32_t rows;
static uint32_t failed;

static WithdrawProof proof;
static WithdrawProof bad;
static U256 x_h;
static U256 r_own;
static U256 seed;

static void row(int ok, const char *name) {
  rows++;
  if (!ok) {
    failed++;
    fprintf(stderr, "FAIL withdraw %s\n", name);
  }
}

/* OWN_H = Enc(MU; r_own) under the key of H, and W. */
static void statement(WdStatement *st, const U256 *w, const U256 *mu) {
  elg_public(&st->key_h, &x_h);
  elg_encrypt(&st->own_h, &st->key_h, mu, &r_own);
  st->w = *w;
}

/* OUT = 2^BITS, BITS below 256. */
static void pow2(U256 *out, uint32_t bits) {
  u256_from_u64(out, 0u);
  out->limb[bits / 64u] = (uint64_t)1 << (bits % 64u);
}

static void fresh(void) {
  bad = proof;
}

/* BAD is the honest proof with one changed word: a changed scalar gives
   TR_BAD_PROOF, a changed point off the curve TR_REFUSED. */
static int fails(const WdStatement *st) {
  TrResult res = withdraw_verify(&bad, st);
  return res == TR_BAD_PROOF || res == TR_REFUSED;
}

/* An honest prove and verify of W and MU. */
static int honest(const U256 *w, const U256 *mu) {
  WdStatement st;
  statement(&st, w, mu);
  return withdraw_prove(&bad, &st, mu, &x_h, &seed) == TR_OK && withdraw_verify(&bad, &st) == TR_OK;
}

int main(int argc, char **argv) {
  WdStatement st;
  WdStatement alt;
  U256 w;
  U256 mu;
  U256 v;
  U256 top;
  U256 one;
  (void)argv;
  /* Fixed entropy for reproducible vectors only, never a live withdraw. */
  blsag_key(&x_h, 5u);
  blsag_key(&r_own, 13u);
  blsag_key(&seed, 19u);
  u256_from_u64(&one, 1u);
  u256_from_u64(&w, WT_W);
  u256_from_u64(&mu, WT_MU);
  statement(&st, &w, &mu);
  row(withdraw_prove(&proof, &st, &mu, &x_h, &seed) == TR_OK, "prove");
  row(withdraw_verify(&proof, &st) == TR_OK, "accept");
  /* The prover refusals: W 2^P above MU, MU = 2^160 and W = 2^128. */
  u256_from_u64(&v, WT_W_OVER);
  statement(&alt, &v, &mu);
  row(withdraw_prove(&bad, &alt, &mu, &x_h, &seed) == TR_RANGE, "w 2^p > mu");
  pow2(&top, WD_BITS);
  statement(&alt, &w, &top);
  row(withdraw_prove(&bad, &alt, &top, &x_h, &seed) == TR_RANGE, "mu = 2^160");
  pow2(&top, WD_W_BITS);
  statement(&alt, &top, &mu);
  row(withdraw_prove(&bad, &alt, &mu, &x_h, &seed) == TR_RANGE, "w = 2^128, prove");
  /* The verifier checks the public bound first. */
  row(withdraw_verify(&proof, &alt) == TR_RANGE, "w = 2^128, verify");
  /* The proof on another W and on another balance. */
  u256_add(&v, &w, &one);
  statement(&alt, &v, &mu);
  row(withdraw_verify(&proof, &alt) == TR_BAD_PROOF, "w + 1");
  u256_add(&v, &mu, &one);
  statement(&alt, &w, &v);
  row(withdraw_verify(&proof, &alt) == TR_BAD_PROOF, "bal + 1");
  alt = st;
  alt.key_h.x.w[0] ^= 1u;
  row(withdraw_verify(&proof, &alt) == TR_REFUSED, "key off the curve");
  /* One changed word in REST, REST_KEY, a bit and a bit proof. */
  fresh();
  bad.rest.c2.x.w[0] ^= 1u;
  row(withdraw_verify(&bad, &st) == TR_REFUSED, "rest off the curve");
  fresh();
  bad.rest_key.z.limb[0] ^= 1u;
  row(fails(&st), "rest key");
  fresh();
  bad.bit[0].c2.x.w[0] ^= 1u;
  row(fails(&st), "bit");
  fresh();
  bad.proof[5].z0.limb[0] ^= 1u;
  row(fails(&st), "bit proof");
  /* The top values: REST = 2^32 - 1. */
  pow2(&v, WD_BITS);
  u256_sub(&v, &v, &one);
  pow2(&top, WD_W_BITS);
  u256_sub(&top, &top, &one);
  row(honest(&top, &v), "mu = 2^160 - 1, w = 2^128 - 1");
  /* REST = 0. */
  u256_from_u64(&v, (uint64_t)WT_W << WD_P);
  row(honest(&w, &v), "mu = w 2^p");
  if (argc < 2) {
    printf("withdraw: %u rows, %u failed\n", rows, failed);
    return failed == 0 ? 0 : 1;
  }
  /* The full set. One changed word in SUM: each bit proof holds first. */
  fresh();
  bad.sum.z.limb[0] ^= 1u;
  row(fails(&st), "sum");
  /* W 2^P above MU: REST encrypts MU - W 2^P mod n, at or above n - 2^160,
     so REST_KEY holds and the sum of the low 160 bits fails. */
  u256_from_u64(&v, WT_W_OVER);
  statement(&alt, &v, &mu);
  row(withdraw_build(&bad, &alt, &mu, &x_h, &seed) == TR_OK && withdraw_verify(&bad, &alt) == TR_BAD_PROOF, "forged w 2^p > mu");
  /* W = 0. */
  u256_from_u64(&v, 0u);
  row(honest(&v, &mu), "w = 0");
  printf("withdraw: %u rows, %u failed\n", rows, failed);
  return failed == 0 ? 0 : 1;
}
