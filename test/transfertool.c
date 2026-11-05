/* Rows for the transfer proof (S5 row C8): an honest proof passes, the prover
   refuses MU at 2^32 and Q above MU, Q ciphers with two nonces are refused,
   and one changed word in each part fails. Without an argument the tool runs
   the short set for make check (accept, the prover refusals, a prove at the
   literal MU 2^32 - 1, c1 not equal, and the flips in SAME and in Q_BITS,
   which fail before the rest is checked); with any argument it runs the full
   set (adds ctx + 1, the flips in REST, REST_KEY and REST_BITS, and the
   forged (2) and (3) parts). One verify is about 2 x 32 sig_or_verify, about
   530 group_mul (about 42 s under tcc), so the flip rows reuse one honest
   proof. */
#include <stdio.h>

#include "front/blsag.h"
#include "front/transfer.h"

#define TT_Q 1000u
#define TT_MU 5000u
/* The top MU as a literal: the 2^32 rows take top from TR_BITS. */
#define TT_MU_TOP 4294967295u

static uint32_t rows;
static uint32_t failed;

/* The proofs are about 35 KB each. */
static TransferProof proof;
static TransferProof bad;
static U256 x_h;
static U256 x_k;
static U256 x_o;
static U256 r;
static U256 r_own;
static U256 seed;

static void row(int ok, const char *name) {
  rows++;
  if (!ok) {
    failed++;
    fprintf(stderr, "FAIL transfer %s\n", name);
  }
}

/* Q under the keys of H, K and O with the one nonce R, MU under the key of
   H, and the context CTX. */
static void statement(TrStatement *st, uint64_t q, uint64_t mu, const U256 *ctx) {
  U256 w;
  elg_public(&st->key_h, &x_h);
  elg_public(&st->key_k, &x_k);
  elg_public(&st->key_o, &x_o);
  u256_from_u64(&w, q);
  elg_encrypt(&st->q_h, &st->key_h, &w, &r);
  elg_encrypt(&st->q_k, &st->key_k, &w, &r);
  elg_encrypt(&st->q_o, &st->key_o, &w, &r);
  u256_from_u64(&w, mu);
  elg_encrypt(&st->own_h, &st->key_h, &w, &r_own);
  st->ctx = *ctx;
}

static void fresh(void) {
  bad = proof;
}

/* BAD is the honest proof with one changed word: a changed scalar gives
   TR_BAD_PROOF, a changed point off the curve TR_REFUSED. */
static int fails(const TrStatement *st) {
  TrResult res = transfer_verify(&bad, st);
  return res == TR_BAD_PROOF || res == TR_REFUSED;
}

/* transfer_build of Q and MU on their statement; the verifier must refuse
   it. */
static int forged(uint64_t q, uint64_t mu, const U256 *ctx) {
  TrStatement st;
  statement(&st, q, mu, ctx);
  return transfer_build(&bad, &st, q, &r, mu, &x_h, &seed) == TR_OK && transfer_verify(&bad, &st) == TR_BAD_PROOF;
}

int main(int argc, char **argv) {
  TrStatement st;
  TrStatement alt;
  U256 ctx;
  U256 w;
  uint64_t top = (uint64_t)1 << TR_BITS;
  (void)argv;
  /* Fixed entropy and context for reproducible vectors only, never a live
     transfer. */
  blsag_key(&x_h, 5u);
  blsag_key(&x_k, 7u);
  blsag_key(&x_o, 11u);
  blsag_key(&r_own, 13u);
  blsag_key(&r, 17u);
  blsag_key(&seed, 19u);
  blsag_key(&ctx, 23u);
  statement(&st, TT_Q, TT_MU, &ctx);
  row(transfer_prove(&proof, &st, TT_Q, &r, TT_MU, &x_h, &seed) == TR_OK, "prove");
  row(transfer_verify(&proof, &st) == TR_OK, "accept");
  row(transfer_prove(&bad, &st, top, &r, top, &x_h, &seed) == TR_RANGE, "q = mu = 2^32");
  row(transfer_prove(&bad, &st, TT_MU + 1u, &r, TT_MU, &x_h, &seed) == TR_RANGE, "q = mu + 1");
  /* MU = 2^32 - 1 is in range. */
  statement(&alt, TT_Q, TT_MU_TOP, &ctx);
  row(transfer_prove(&bad, &alt, TT_Q, &r, TT_MU_TOP, &x_h, &seed) == TR_OK, "mu = 2^32 - 1");
  /* K encrypts Q with a second nonce. */
  alt = st;
  u256_from_u64(&w, TT_Q);
  elg_encrypt(&alt.q_k, &alt.key_k, &w, &r_own);
  row(transfer_prove(&bad, &alt, TT_Q, &r, TT_MU, &x_h, &seed) == TR_REFUSED, "c1 not equal, prove");
  row(transfer_verify(&proof, &alt) == TR_REFUSED, "c1 not equal, verify");
  /* One changed word in SAME and in Q_BITS: each fails before the rest. */
  fresh();
  bad.same[0].z.limb[0] ^= 1u;
  row(fails(&st), "same k z");
  fresh();
  bad.same[1].a.x.w[0] ^= 1u;
  row(fails(&st), "same open a");
  fresh();
  bad.q_bits.bit[0].c2.x.w[0] ^= 1u;
  row(fails(&st), "q bit");
  fresh();
  bad.q_bits.proof[3].z0.limb[0] ^= 1u;
  row(fails(&st), "q bit proof");
  fresh();
  bad.q_bits.sum.z.limb[0] ^= 1u;
  row(fails(&st), "q sum");
  if (argc < 2) {
    printf("transfer: %u rows, %u failed\n", rows, failed);
    return failed == 0 ? 0 : 1;
  }
  /* The full set. A copy of the proof on another transfer. */
  alt = st;
  alt.ctx.limb[0] += 1u;
  row(transfer_verify(&proof, &alt) == TR_BAD_PROOF, "ctx + 1");
  fresh();
  bad.rest.c2.x.w[0] ^= 1u;
  row(fails(&st), "rest");
  fresh();
  bad.rest_key.z.limb[0] ^= 1u;
  row(fails(&st), "rest key");
  fresh();
  bad.rest_bits.bit[31].c1.x.w[0] ^= 1u;
  row(fails(&st), "rest bit");
  fresh();
  bad.rest_bits.proof[0].e0.limb[0] ^= 1u;
  row(fails(&st), "rest bit proof");
  fresh();
  bad.rest_bits.sum.z.limb[0] ^= 1u;
  row(fails(&st), "rest sum");
  /* (2) on Q = 2^32: the bits take 0, so the sum of Q_BITS fails. (3) with Q
     > MU: REST encrypts MU - Q mod n and REST_KEY holds, so the sum of
     REST_BITS fails. */
  row(forged(top, TT_MU, &ctx), "forged (2) q = 2^32");
  row(forged(TT_MU + 1000u, TT_MU, &ctx), "forged (3) q > mu");
  printf("transfer: %u rows, %u failed\n", rows, failed);
  return failed == 0 ? 0 : 1;
}
