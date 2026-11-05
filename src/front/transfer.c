/* The transfer proof in the host (S5 row C8). See front/transfer.h. */
#include <string.h>

#include "front/transfer.h"

#include "front/blsag.h"
#include "front/threshold.h"

/* "transfer" fits in limb 0. */
const U256 TR_TAG = {{0x7472616e73666572u, 0u, 0u, 0u}};
const U256 TR_Q_TAG = {{0x747271u, 0u, 0u, 0u}};
const U256 TR_REST_TAG = {{0x747272u, 0u, 0u, 0u}};
const U256 TR_NONCE_TAG = {{0x747277u, 0u, 0u, 0u}};

/* The contexts of the parts: S for (1) and REST_KEY, Q for Q_BITS, R for
   REST_BITS. */
typedef struct {
  U256 s;
  U256 q;
  U256 r;
} TrCtx;

/* blsag_reduce(thr_hash(BASE, I)). */
static void nonce(U256 *out, const U256 *base, uint64_t i) {
  U256 w;
  u256_from_u64(&w, i);
  thr_hash(out, base, &w);
  blsag_reduce(out, out);
}

/* OUT = A B + C mod n. OUT can be any input. */
static void mul_add(U256 *out, const U256 *a, const U256 *b, const U256 *c) {
  U256 ar;
  U256 br;
  U256 cr;
  Fe fa;
  Fe fb;
  Fe fc;
  Fe ab;
  Fe s;
  blsag_reduce(&ar, a);
  blsag_reduce(&br, b);
  blsag_reduce(&cr, c);
  fe_from_u256(&fa, &ar, &GROUP_N);
  fe_from_u256(&fb, &br, &GROUP_N);
  fe_from_u256(&fc, &cr, &GROUP_N);
  fe_mul(&ab, &fa, &fb, &GROUP_N);
  fe_add(&s, &ab, &fc, &GROUP_N);
  fe_to_u256(out, &s);
}

/* OUT = A - B mod n. OUT can be any input. */
static void sub_mod(U256 *out, const U256 *a, const U256 *b) {
  U256 ar;
  U256 br;
  Fe fa;
  Fe fb;
  Fe d;
  blsag_reduce(&ar, a);
  blsag_reduce(&br, b);
  fe_from_u256(&fa, &ar, &GROUP_N);
  fe_from_u256(&fb, &br, &GROUP_N);
  fe_sub(&d, &fa, &fb, &GROUP_N);
  fe_to_u256(out, &d);
}

/* A - B. */
static void point_sub(GroupPoint *out, const GroupPoint *a, const GroupPoint *b) {
  GroupPoint t;
  group_neg(&t, b);
  group_add(out, a, &t);
}

static void cipher_sub(ElgCipher *out, const ElgCipher *a, const ElgCipher *b) {
  point_sub(&out->c1, &a->c1, &b->c1);
  point_sub(&out->c2, &a->c2, &b->c2);
}

static int cipher_on_curve(const ElgCipher *ct) {
  return group_on_curve(&ct->c1) && group_on_curve(&ct->c2);
}

static TrResult from_sig(SigResult r) {
  switch (r) {
  case SIG_OK:
    return TR_OK;
  case SIG_REFUSED:
    return TR_REFUSED;
  case SIG_BAD_SLOT:
  case SIG_BAD_SUM:
    return TR_BAD_PROOF;
  }
  return TR_BAD_PROOF;
}

static void contexts(TrCtx *cx, const U256 *ctx) {
  thr_hash(&cx->s, &TR_TAG, ctx);
  thr_hash(&cx->q, &TR_Q_TAG, ctx);
  thr_hash(&cx->r, &TR_REST_TAG, ctx);
}

/* Each point is on the curve, and the Q ciphers share c1. */
static TrResult check_statement(const TrStatement *st) {
  if (!group_on_curve(&st->key_h) || !group_on_curve(&st->key_k) || !group_on_curve(&st->key_o) || !cipher_on_curve(&st->q_h) || !cipher_on_curve(&st->q_k)
      || !cipher_on_curve(&st->q_o) || !cipher_on_curve(&st->own_h))
    return TR_REFUSED;
  if (!group_eq(&st->q_h.c1, &st->q_k.c1) || !group_eq(&st->q_h.c1, &st->q_o.c1))
    return TR_REFUSED;
  return TR_OK;
}

/* (1) for I = 0 (K) and I = 1 (O): the key X_H - X_I and the cipher (c1, c2_H
   - c2_I) of 0 with the nonce R. */
static void same_part(GroupPoint *key, ElgCipher *ct, const TrStatement *st, uint32_t i) {
  const GroupPoint *key_i = i == 0u ? &st->key_k : &st->key_o;
  const ElgCipher *q_i = i == 0u ? &st->q_k : &st->q_o;
  point_sub(key, &st->key_h, key_i);
  ct->c1 = st->q_h.c1;
  point_sub(&ct->c2, &st->q_h.c2, &q_i->c2);
}

/* REST_KEY: D = OWN_H - Q_H - REST as the cipher (X_H, D.c2) of 0 under the
   key D.c1, with the witness x_H. It holds iff D.c2 = x_H D.c1, that is iff D
   decrypts to 0. */
static void rest_part(GroupPoint *key, ElgCipher *ct, const TrStatement *st, const ElgCipher *rest) {
  ElgCipher d;
  cipher_sub(&d, &st->own_h, &st->q_h);
  cipher_sub(&d, &d, rest);
  *key = d.c1;
  ct->c1 = st->key_h;
  ct->c2 = d.c2;
}

/* CT - sum 2^i B_i, by Horner over the bits: Enc(0; R - S) for an honest
   range, S = sum 2^i s_i. */
static void range_rest(ElgCipher *out, const TrRange *proof, const ElgCipher *ct) {
  ElgCipher acc;
  ElgCipher t;
  uint32_t i;
  acc = proof->bit[TR_BITS - 1u];
  for (i = TR_BITS - 1u; i > 0; i--) {
    elg_add(&t, &acc, &acc);
    elg_add(&acc, &t, &proof->bit[i - 1u]);
  }
  cipher_sub(out, ct, &acc);
}

/* The range proof of the low TR_BITS bits of V, with no range check. */
static TrResult range_build(TrRange *out, const GroupPoint *pk, const ElgCipher *ct, uint64_t v, const U256 *r, const U256 *base, const U256 *ctx) {
  ElgCipher d;
  U256 s;
  U256 t;
  U256 word;
  U256 zero;
  uint32_t bit;
  uint32_t i;
  SigResult sr = SIG_OK;
  memset(out, 0, sizeof *out);
  u256_from_u64(&zero, 0u);
  t = zero;
  for (i = 0; sr == SIG_OK && i < TR_BITS; i++) {
    bit = (uint32_t)((v >> i) & 1u);
    nonce(&s, base, i);
    u256_from_u64(&word, bit);
    elg_encrypt(&out->bit[i], pk, &word, &s);
    sr = sig_or_prove(&out->proof[i], pk, &out->bit[i], bit, &s, ctx, i);
    u256_from_u64(&word, (uint64_t)1 << i);
    mul_add(&t, &word, &s, &t);
  }
  if (sr != SIG_OK)
    return from_sig(sr);
  range_rest(&d, out, ct);
  sub_mod(&t, r, &t);
  return from_sig(sig_rand_prove(&out->sum, pk, &d, &zero, &t, ctx));
}

TrResult transfer_range_prove(TrRange *out, const GroupPoint *pk, const ElgCipher *ct, uint64_t v, const U256 *r, const U256 *base, const U256 *ctx) {
  if (v >= ((uint64_t)1 << TR_BITS))
    return TR_RANGE;
  return range_build(out, pk, ct, v, r, base, ctx);
}

TrResult transfer_range_verify(const TrRange *proof, const GroupPoint *pk, const ElgCipher *ct, const U256 *ctx) {
  ElgCipher d;
  U256 zero;
  TrResult res = TR_OK;
  uint32_t i;
  if (!group_on_curve(pk) || !cipher_on_curve(ct))
    return TR_REFUSED;
  for (i = 0; res == TR_OK && i < TR_BITS; i++)
    res = from_sig(sig_or_verify(&proof->proof[i], pk, &proof->bit[i], ctx, i));
  if (res != TR_OK)
    return res;
  /* Without the sum, the bits can encode any value. */
  range_rest(&d, proof, ct);
  u256_from_u64(&zero, 0u);
  return from_sig(sig_rand_verify(&proof->sum, pk, &d, &zero, ctx));
}

TrResult transfer_build(TransferProof *out, const TrStatement *st, uint64_t q, const U256 *r, uint64_t mu, const U256 *x_h, const U256 *seed) {
  TrCtx cx;
  ElgCipher ct;
  GroupPoint key;
  U256 base;
  U256 sub;
  U256 word;
  U256 rest;
  U256 t;
  U256 zero;
  uint32_t i;
  TrResult res = check_statement(st);
  if (res != TR_OK)
    return res;
  memset(out, 0, sizeof *out);
  contexts(&cx, &st->ctx);
  u256_from_u64(&zero, 0u);
  /* The nonce base binds the context, the values and the secrets of H. */
  thr_hash(&base, &TR_NONCE_TAG, seed);
  thr_hash(&base, &base, &st->ctx);
  u256_from_u64(&word, q);
  thr_hash(&base, &base, &word);
  thr_hash(&base, &base, r);
  u256_from_u64(&rest, mu);
  thr_hash(&base, &base, &rest);
  thr_hash(&base, &base, x_h);
  for (i = 0; res == TR_OK && i < 2u; i++) {
    same_part(&key, &ct, st, i);
    res = from_sig(sig_rand_prove(&out->same[i], &key, &ct, &zero, r, &cx.s));
  }
  thr_hash(&sub, &base, &TR_Q_TAG);
  if (res == TR_OK)
    res = range_build(&out->q_bits, &st->key_h, &st->q_h, q, r, &sub, &cx.q);
  if (res != TR_OK)
    return res;
  /* MU - Q mod n: when Q > MU it is at or above n - 2^32, and no bits match
     it. */
  sub_mod(&rest, &rest, &word);
  nonce(&t, &base, 0u);
  elg_encrypt(&out->rest, &st->key_h, &rest, &t);
  rest_part(&key, &ct, st, &out->rest);
  res = from_sig(sig_rand_prove(&out->rest_key, &key, &ct, &zero, x_h, &cx.s));
  thr_hash(&sub, &base, &TR_REST_TAG);
  if (res == TR_OK)
    res = range_build(&out->rest_bits, &st->key_h, &out->rest, mu - q, &t, &sub, &cx.r);
  return res;
}

TrResult transfer_prove(TransferProof *out, const TrStatement *st, uint64_t q, const U256 *r, uint64_t mu, const U256 *x_h, const U256 *seed) {
  /* Q <= MU < 2^32, so Q < 2^32 too. */
  if (mu >= ((uint64_t)1 << TR_BITS) || q > mu)
    return TR_RANGE;
  return transfer_build(out, st, q, r, mu, x_h, seed);
}

TrResult transfer_verify(const TransferProof *proof, const TrStatement *st) {
  TrCtx cx;
  ElgCipher ct;
  GroupPoint key;
  U256 zero;
  uint32_t i;
  TrResult res = check_statement(st);
  if (res != TR_OK)
    return res;
  contexts(&cx, &st->ctx);
  u256_from_u64(&zero, 0u);
  for (i = 0; res == TR_OK && i < 2u; i++) {
    same_part(&key, &ct, st, i);
    res = from_sig(sig_rand_verify(&proof->same[i], &key, &ct, &zero, &cx.s));
  }
  if (res == TR_OK)
    res = transfer_range_verify(&proof->q_bits, &st->key_h, &st->q_h, &cx.q);
  if (res == TR_OK && !cipher_on_curve(&proof->rest))
    res = TR_REFUSED;
  if (res != TR_OK)
    return res;
  rest_part(&key, &ct, st, &proof->rest);
  res = from_sig(sig_rand_verify(&proof->rest_key, &key, &ct, &zero, &cx.s));
  if (res == TR_OK)
    res = transfer_range_verify(&proof->rest_bits, &st->key_h, &proof->rest, &cx.r);
  return res;
}
