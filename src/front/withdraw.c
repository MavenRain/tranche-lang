/* The withdraw proof in the host (tranche-lang S7 row C5). See
   front/withdraw.h. */
#include <string.h>

#include "front/withdraw.h"

#include "front/threshold.h"

/* "withdraw" fits in limb 0. */
const U256 WD_TAG = {{0x7769746864726177u, 0u, 0u, 0u}};
/* "wdr" and "wdn". */
const U256 WD_REST_TAG = {{0x776472u, 0u, 0u, 0u}};
const U256 WD_NONCE_TAG = {{0x77646eu, 0u, 0u, 0u}};

void withdraw_ctx(U256 *ctx, const WdStatement *st) {
  U256 x;
  U256 y;
  group_to_words(&x, &y, &st->key_h);
  thr_hash(ctx, &WD_TAG, &x);
  thr_hash(ctx, ctx, &y);
  group_to_words(&x, &y, &st->own_h.c1);
  thr_hash(ctx, ctx, &x);
  thr_hash(ctx, ctx, &y);
  group_to_words(&x, &y, &st->own_h.c2);
  thr_hash(ctx, ctx, &x);
  thr_hash(ctx, ctx, &y);
  thr_hash(ctx, ctx, &st->w);
}

/* 1 when V < 2^BITS, BITS below 256. */
static int below(const U256 *v, uint32_t bits) {
  U256 top;
  u256_from_u64(&top, 0u);
  top.limb[bits / 64u] = (uint64_t)1 << (bits % 64u);
  return u256_cmp(v, &top) < 0;
}

/* OUT = W 2^P mod n. */
static void shifted(U256 *out, const U256 *w) {
  U256 two_p;
  U256 zero;
  u256_from_u64(&two_p, (uint64_t)1 << WD_P);
  u256_from_u64(&zero, 0u);
  tr_mul_add(out, w, &two_p, &zero);
}

static int on_curve(const WdStatement *st) {
  return group_on_curve(&st->key_h) && tr_cipher_on_curve(&st->own_h);
}

/* REST_KEY: D = OWN_H - Enc(M; 1) - REST as the cipher (X_H, D.c2) of 0
   under the key D.c1, with the witness x_H. The public M takes the nonce 1
   under X_H. It holds iff D.c2 = x_H D.c1, that is iff the value of OWN_H is
   M plus the value of REST modulo n. */
static void rest_part(GroupPoint *key, ElgCipher *ct, const WdStatement *st, const U256 *m, const ElgCipher *rest) {
  ElgCipher e;
  ElgCipher d;
  U256 one;
  u256_from_u64(&one, 1u);
  elg_encrypt(&e, &st->key_h, m, &one);
  tr_cipher_sub(&d, &st->own_h, &e);
  tr_cipher_sub(&d, &d, rest);
  *key = d.c1;
  ct->c1 = st->key_h;
  ct->c2 = d.c2;
}

TrResult withdraw_build(WithdrawProof *out, const WdStatement *st, const U256 *mu, const U256 *x_h, const U256 *seed) {
  GroupPoint key;
  ElgCipher ct;
  U256 ctx;
  U256 cr;
  U256 base;
  U256 sub;
  U256 m;
  U256 rest;
  U256 t;
  U256 zero;
  TrResult res;
  if (!on_curve(st))
    return TR_REFUSED;
  memset(out, 0, sizeof *out);
  withdraw_ctx(&ctx, st);
  thr_hash(&cr, &WD_REST_TAG, &ctx);
  u256_from_u64(&zero, 0u);
  /* The nonce base binds the context, the values and the secret of H. */
  thr_hash(&base, &WD_NONCE_TAG, seed);
  thr_hash(&base, &base, &ctx);
  thr_hash(&base, &base, &st->w);
  thr_hash(&base, &base, mu);
  thr_hash(&base, &base, x_h);
  /* MU - W 2^P mod n: when W 2^P > MU it is at or above n - 2^Bbal, and no
     bits match it. */
  shifted(&m, &st->w);
  tr_sub_mod(&rest, mu, &m);
  tr_nonce(&t, &base, 0u);
  elg_encrypt(&out->rest, &st->key_h, &rest, &t);
  rest_part(&key, &ct, st, &m, &out->rest);
  res = tr_from_sig(sig_rand_prove(&out->rest_key, &key, &ct, &zero, x_h, &ctx));
  thr_hash(&sub, &base, &WD_REST_TAG);
  if (res == TR_OK)
    res = tr_range_build(out->bit, out->proof, &out->sum, WD_BITS, &st->key_h, &out->rest, &rest, &t, &sub, &cr);
  return res;
}

TrResult withdraw_prove(WithdrawProof *out, const WdStatement *st, const U256 *mu, const U256 *x_h, const U256 *seed) {
  U256 m;
  if (!below(&st->w, WD_W_BITS) || !below(mu, WD_BITS))
    return TR_RANGE;
  /* W < 2^(Bbal - P), so W 2^P < 2^Bbal < n and M is W 2^P as an integer. */
  shifted(&m, &st->w);
  if (u256_cmp(&m, mu) > 0)
    return TR_RANGE;
  return withdraw_build(out, st, mu, x_h, seed);
}

TrResult withdraw_verify(const WithdrawProof *proof, const WdStatement *st) {
  GroupPoint key;
  ElgCipher ct;
  U256 ctx;
  U256 cr;
  U256 m;
  U256 zero;
  TrResult res;
  /* The public bound first: at or above 2^(Bbal - P), W 2^P can wrap
     modulo n. */
  if (!below(&st->w, WD_W_BITS))
    return TR_RANGE;
  if (!on_curve(st) || !tr_cipher_on_curve(&proof->rest))
    return TR_REFUSED;
  withdraw_ctx(&ctx, st);
  thr_hash(&cr, &WD_REST_TAG, &ctx);
  u256_from_u64(&zero, 0u);
  shifted(&m, &st->w);
  rest_part(&key, &ct, st, &m, &proof->rest);
  res = tr_from_sig(sig_rand_verify(&proof->rest_key, &key, &ct, &zero, &ctx));
  if (res == TR_OK)
    res = tr_range_check(proof->bit, proof->proof, &proof->sum, WD_BITS, &st->key_h, &proof->rest, &cr);
  return res;
}
