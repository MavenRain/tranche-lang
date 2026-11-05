/* The joint threshold rule in the host (S5 row C7). See front/jointr.h. */
#include <string.h>

#include "front/jointr.h"

#include "front/blsag.h"
#include "front/claimcom.h"
#include "front/threshold.h"

/* "tranche-joint": the last 8 bytes in limb 0 (the claimcom.c form). */
const U256 JR_TAG = {{0x68652d6a6f696e74u, 0x7472616e63u, 0u, 0u}};
const U256 JR_F_TAG = {{0x6a7266u, 0u, 0u, 0u}};
const U256 JR_Y_TAG = {{0x6a7279u, 0u, 0u, 0u}};
const U256 JR_G_TAG = {{0x6a7267u, 0u, 0u, 0u}};
const U256 JR_NONCE_TAG = {{0x6a7277u, 0u, 0u, 0u}};

/* The statement of one atom: P1 = x B1 and P2 = x B2. A one-base atom has
   B2 = P2 = (0, 0), so its check forces B of the proof to (0, 0). */
typedef struct {
  GroupPoint b1;
  GroupPoint p1;
  GroupPoint b2;
  GroupPoint p2;
} JrAtom;

/* N branches of K atoms each. */
typedef struct {
  JrAtom at[JR_C_MAX][JR_C_MAX + 1u];
  uint32_t n;
  uint32_t k;
} JrOrSt;

/* The values that M1, M2 and M3 share, derived from the table and the
   statement. */
typedef struct {
  GroupPoint j;
  U256 mu_h;
  U256 mu_k;
  U256 mu;
  ElgCipher qj;
  uint64_t v[JR_M_MAX];
  uint32_t m;
  GroupPoint g1;
  GroupPoint g2;
  GroupPoint h;
  U256 ctx_g;
  U256 ctx_f;
  U256 ctx_y;
} JrCtx;

/* ACC = thr_hash(thr_hash(ACC, P.x), P.y). */
static void absorb(U256 *acc, const GroupPoint *p) {
  U256 x;
  U256 y;
  group_to_words(&x, &y, p);
  thr_hash(acc, acc, &x);
  thr_hash(acc, acc, &y);
}

/* blsag_reduce(thr_hash(BASE, I)). */
static void nonce(U256 *out, const U256 *base, uint64_t i) {
  U256 w;
  u256_from_u64(&w, i);
  thr_hash(out, base, &w);
  blsag_reduce(out, out);
}

/* The chain from JR_NONCE_TAG over SEED and CTX, bound to the secrets X, Y
   and the class CLS. */
static void nonce_base(U256 *out, const U256 *seed, const U256 *ctx, const U256 *x, const U256 *y, uint32_t cls) {
  U256 w;
  thr_hash(out, &JR_NONCE_TAG, seed);
  thr_hash(out, out, ctx);
  thr_hash(out, out, x);
  thr_hash(out, out, y);
  u256_from_u64(&w, cls);
  thr_hash(out, out, &w);
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

static int word_zero(const U256 *x) {
  return x->limb[0] == 0u && x->limb[1] == 0u && x->limb[2] == 0u && x->limb[3] == 0u;
}

static int at_infinity(const GroupPoint *p) {
  return fe_is_zero(&p->x) && fe_is_zero(&p->y);
}

static void infinity(GroupPoint *out) {
  fe_from_u64(&out->x, 0u);
  fe_from_u64(&out->y, 0u);
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

/* 1 when z P = A + e Q. */
static int eq_holds(const U256 *z, const GroupPoint *p, const GroupPoint *a, const U256 *e, const GroupPoint *q) {
  GroupPoint l;
  GroupPoint r;
  group_mul(&l, p, z);
  group_mul(&r, q, e);
  group_add(&r, a, &r);
  return group_eq(&l, &r);
}

/* OUT = Z B - E P (the sig_simulate form). */
static void simulate(GroupPoint *out, const GroupPoint *b, const GroupPoint *p, const U256 *e, const U256 *z) {
  GroupPoint t;
  GroupPoint u;
  group_mul(&t, b, z);
  group_mul(&u, p, e);
  point_sub(out, &t, &u);
}

static JrResult from_sig(SigResult r) {
  switch (r) {
  case SIG_OK:
    return JR_OK;
  case SIG_REFUSED:
    return JR_REFUSED;
  case SIG_BAD_SLOT:
  case SIG_BAD_SUM:
    return JR_BAD_PROOF;
  }
  return JR_BAD_PROOF;
}

/* MU_I = blsag_reduce(chain(JR_TAG, KEY_H, KEY_K, KEY_I)). */
static void key_weight(U256 *out, const GroupPoint *key_h, const GroupPoint *key_k, const GroupPoint *key_i) {
  *out = JR_TAG;
  absorb(out, key_h);
  absorb(out, key_k);
  absorb(out, key_i);
  blsag_reduce(out, out);
}

int jointr_key(GroupPoint *j, U256 *mu_h, U256 *mu_k, const GroupPoint *key_h, const GroupPoint *key_k) {
  GroupPoint t;
  GroupPoint u;
  U256 one;
  U256 mu;
  key_weight(mu_h, key_h, key_k, key_h);
  key_weight(mu_k, key_h, key_k, key_k);
  u256_from_u64(&one, 1u);
  mul_add(&mu, mu_h, &one, mu_k);
  group_mul(&t, key_h, mu_h);
  group_mul(&u, key_k, mu_k);
  group_add(j, &t, &u);
  return !word_zero(&mu) && !at_infinity(j);
}

/* Puts CAP into the sorted distinct caps V. */
static int cap_insert(uint64_t *v, uint32_t *m, uint64_t cap) {
  uint32_t i;
  for (i = 0; i < *m; i++)
    if (v[i] == cap)
      return 1;
  if (cap >= ((uint64_t)1 << JR_BITS) || *m >= JR_M_MAX)
    return 0;
  for (i = *m; i > 0 && v[i - 1u] > cap; i--)
    v[i] = v[i - 1u];
  v[i] = cap;
  (*m)++;
  return 1;
}

int jointr_caps(uint64_t *v, uint32_t *m, const JrTable *table) {
  uint32_t a;
  uint32_t b;
  int ok;
  *m = 0;
  ok = table->c > 0u && table->c <= JR_C_MAX;
  for (a = 0; ok && a < table->c; a++)
    for (b = 0; ok && b < table->c; b++)
      if (table->admit[a][b] != 0u)
        ok = cap_insert(v, m, table->cap[a][b]);
  return ok;
}

/* The gate of CAP in the sorted caps. */
static uint32_t cap_index(const JrCtx *cx, uint64_t cap) {
  uint32_t i;
  uint32_t at = 0;
  for (i = 0; i < cx->m; i++)
    if (cx->v[i] == cap)
      at = i;
  return at;
}

static JrResult setup(JrCtx *cx, const JrTable *table, const JrStatement *st) {
  GroupPoint t;
  GroupPoint u;
  U256 one;
  uint32_t k;
  if (!jointr_caps(cx->v, &cx->m, table))
    return JR_REFUSED;
  if (!group_on_curve(&st->key_h) || !group_on_curve(&st->key_k) || !group_on_curve(&st->com_h) || !group_on_curve(&st->com_k) || !cipher_on_curve(&st->q_h)
      || !cipher_on_curve(&st->q_k))
    return JR_REFUSED;
  /* One nonce for both ciphertexts: with two, Q_J would not decrypt under J
     (B2-1). */
  if (!group_eq(&st->q_h.c1, &st->q_k.c1) || !jointr_key(&cx->j, &cx->mu_h, &cx->mu_k, &st->key_h, &st->key_k))
    return JR_REFUSED;
  if (!claimcom_generator(&cx->g1, &k, &CLAIMCOM_TAG_G1) || !claimcom_generator(&cx->g2, &k, &CLAIMCOM_TAG_G2) || !claimcom_generator(&cx->h, &k, &CLAIMCOM_TAG_H))
    return JR_REFUSED;
  u256_from_u64(&one, 1u);
  mul_add(&cx->mu, &cx->mu_h, &one, &cx->mu_k);
  cx->qj.c1 = st->q_h.c1;
  group_mul(&t, &st->q_h.c2, &cx->mu_h);
  group_mul(&u, &st->q_k.c2, &cx->mu_k);
  group_add(&cx->qj.c2, &t, &u);
  thr_hash(&cx->ctx_g, &JR_G_TAG, &st->ctx);
  thr_hash(&cx->ctx_f, &JR_F_TAG, &st->ctx);
  thr_hash(&cx->ctx_y, &JR_Y_TAG, &st->ctx);
  return JR_OK;
}

/* E = blsag_reduce(chain(CTX, each B1 P1 B2 P2 A B)). */
static void or_challenge(U256 *e, const JrOrSt *st, const JrOr *pf, const U256 *ctx) {
  uint32_t i;
  uint32_t j;
  *e = *ctx;
  for (i = 0; i < st->n; i++)
    for (j = 0; j < st->k; j++) {
      absorb(e, &st->at[i][j].b1);
      absorb(e, &st->at[i][j].p1);
      absorb(e, &st->at[i][j].b2);
      absorb(e, &st->at[i][j].p2);
      absorb(e, &pf->atom[i][j].a);
      absorb(e, &pf->atom[i][j].b);
    }
  blsag_reduce(e, e);
}

/* The proof with the real branch REAL and its K secrets X. Each other branch
   draws its challenge and its responses (the sig_simulate form); the real
   branch takes E minus their sum mod n (not the mod 2^256 split of sig_or). */
static void or_prove(JrOr *pf, const JrOrSt *st, uint32_t real, const U256 *x, const U256 *base, const U256 *ctx) {
  U256 w[JR_C_MAX + 1u];
  U256 e;
  Fe sum;
  Fe fv;
  Fe t;
  uint64_t d = 0;
  uint32_t i;
  uint32_t j;
  memset(pf, 0, sizeof *pf);
  for (i = 0; i < st->n; i++) {
    if (i != real)
      nonce(&pf->e[i], base, d++);
    for (j = 0; j < st->k; j++) {
      if (i == real) {
        nonce(&w[j], base, d++);
        group_mul(&pf->atom[i][j].a, &st->at[i][j].b1, &w[j]);
        group_mul(&pf->atom[i][j].b, &st->at[i][j].b2, &w[j]);
      } else {
        nonce(&pf->atom[i][j].z, base, d++);
        simulate(&pf->atom[i][j].a, &st->at[i][j].b1, &st->at[i][j].p1, &pf->e[i], &pf->atom[i][j].z);
        simulate(&pf->atom[i][j].b, &st->at[i][j].b2, &st->at[i][j].p2, &pf->e[i], &pf->atom[i][j].z);
      }
    }
  }
  or_challenge(&e, st, pf, ctx);
  fe_from_u256(&sum, &e, &GROUP_N);
  for (i = 0; i < st->n; i++)
    if (i != real) {
      fe_from_u256(&fv, &pf->e[i], &GROUP_N);
      fe_sub(&t, &sum, &fv, &GROUP_N);
      sum = t;
    }
  fe_to_u256(&pf->e[real], &sum);
  for (j = 0; j < st->k; j++)
    mul_add(&pf->atom[real][j].z, &pf->e[real], &x[j], &w[j]);
}

static JrResult or_verify(const JrOr *pf, const JrOrSt *st, const U256 *ctx) {
  U256 e;
  U256 got;
  Fe sum;
  Fe fv;
  Fe t;
  uint32_t i;
  uint32_t j;
  for (i = 0; i < st->n; i++)
    for (j = 0; j < st->k; j++)
      if (!group_on_curve(&pf->atom[i][j].a) || !group_on_curve(&pf->atom[i][j].b))
        return JR_REFUSED;
  or_challenge(&e, st, pf, ctx);
  fe_from_u64(&sum, 0u);
  for (i = 0; i < st->n; i++) {
    /* A challenge at or above n would give a second form of one proof. */
    if (!fe_from_u256(&fv, &pf->e[i], &GROUP_N))
      return JR_BAD_PROOF;
    fe_add(&t, &sum, &fv, &GROUP_N);
    sum = t;
  }
  fe_to_u256(&got, &sum);
  /* Without the split, simulate gives every branch for any challenge. */
  if (u256_cmp(&got, &e) != 0)
    return JR_BAD_PROOF;
  for (i = 0; i < st->n; i++)
    for (j = 0; j < st->k; j++)
      if (!eq_holds(&pf->atom[i][j].z, &st->at[i][j].b1, &pf->atom[i][j].a, &pf->e[i], &st->at[i][j].p1)
          || !eq_holds(&pf->atom[i][j].z, &st->at[i][j].b2, &pf->atom[i][j].b, &pf->e[i], &st->at[i][j].p2))
        return JR_BAD_PROOF;
  return JR_OK;
}

/* COM - JURIS G1 - STATUS G2: R H for the class with the opening R. */
static void class_point(GroupPoint *out, const GroupPoint *com, const JrCtx *cx, uint64_t juris, uint64_t status) {
  GroupPoint t;
  U256 k;
  u256_from_u64(&k, juris);
  group_mul(&t, &cx->g1, &k);
  point_sub(out, com, &t);
  u256_from_u64(&k, status);
  group_mul(&t, &cx->g2, &k);
  point_sub(out, out, &t);
}

/* Atom 0 of a branch opens COM at class A in base H (witness R). */
static void class_atom(JrAtom *at, const GroupPoint *com, const JrTable *table, const JrCtx *cx, uint32_t a) {
  at->b1 = cx->h;
  class_point(&at->p1, com, cx, table->juris[a], table->status[a]);
  infinity(&at->b2);
  infinity(&at->p2);
}

/* D encrypts 0 under J (witness the nonce of D). */
static void zero_atom(JrAtom *at, const ElgCipher *d, const JrCtx *cx) {
  elg_generator(&at->b1);
  at->p1 = d->c1;
  at->b2 = cx->j;
  at->p2 = d->c2;
}

/* Branch A of the F proof: atom 0 opens COM_H at class A, and atom 1 + B
   shows that F_B minus the gate of (A, B) (or F_B alone when (A, B) is not
   admitted) encrypts 0 under J. */
static void f_statement(JrOrSt *st, const JrM1 *m1, const JrTable *table, const JrStatement *s, const JrCtx *cx) {
  ElgCipher d;
  uint32_t a;
  uint32_t b;
  st->n = table->c;
  st->k = table->c + 1u;
  for (a = 0; a < table->c; a++) {
    class_atom(&st->at[a][0], &s->com_h, table, cx, a);
    for (b = 0; b < table->c; b++) {
      d = m1->f[b];
      if (table->admit[a][b] != 0u)
        cipher_sub(&d, &m1->f[b], &m1->g[cap_index(cx, table->cap[a][b])].bit[JR_BITS]);
      zero_atom(&st->at[a][1u + b], &d, cx);
    }
  }
}

/* Branch B of the Y proof: atom 0 opens COM_K at class B, and atom 1 shows
   that Y - F_B encrypts 0 under J. */
static void y_statement(JrOrSt *st, const JrM1 *m1, const JrM2 *m2, const JrTable *table, const JrStatement *s, const JrCtx *cx) {
  ElgCipher d;
  uint32_t b;
  st->n = table->c;
  st->k = 2u;
  for (b = 0; b < table->c; b++) {
    class_atom(&st->at[b][0], &s->com_k, table, cx, b);
    cipher_sub(&d, &m2->y, &m1->f[b]);
    zero_atom(&st->at[b][1], &d, cx);
  }
}

/* MU sum 2^i B_i + Q_J - (0, MU (V + 2^32) G), by Horner over the bits:
   Enc_J(0; MU S + R) for an honest gate. */
static void gate_cipher(ElgCipher *out, const JrGate *gate, const JrCtx *cx, uint64_t v) {
  ElgCipher acc;
  ElgCipher t;
  GroupPoint g;
  GroupPoint p;
  U256 k;
  U256 zero;
  uint32_t i;
  acc = gate->bit[JR_BITS];
  for (i = JR_BITS; i > 0; i--) {
    elg_add(&t, &acc, &acc);
    elg_add(&acc, &t, &gate->bit[i - 1u]);
  }
  elg_scale(&acc, &acc, &cx->mu);
  elg_add(&acc, &acc, &cx->qj);
  u256_from_u64(&k, v + ((uint64_t)1 << JR_BITS));
  u256_from_u64(&zero, 0u);
  mul_add(&k, &cx->mu, &k, &zero);
  elg_generator(&g);
  group_mul(&p, &g, &k);
  point_sub(&acc.c2, &acc.c2, &p);
  *out = acc;
}

/* The share D = MU X Y.c1 is sig_rand with the key Y.c1 and the cipher
   (MU KEY, D) of 0. */
static void share_cipher(ElgCipher *out, const GroupPoint *d, const GroupPoint *key, const U256 *mu) {
  group_mul(&out->c1, key, mu);
  out->c2 = *d;
}

static JrResult make_share(GroupPoint *d, SigDleq *proof, const ElgCipher *y, const GroupPoint *key, const U256 *mu, const U256 *x, const U256 *ctx) {
  ElgCipher ct;
  U256 zero;
  U256 t;
  u256_from_u64(&zero, 0u);
  mul_add(&t, mu, x, &zero);
  group_mul(d, &y->c1, &t);
  share_cipher(&ct, d, key, mu);
  return from_sig(sig_rand_prove(proof, &y->c1, &ct, &zero, &t, ctx));
}

static JrResult check_share(const GroupPoint *d, const SigDleq *proof, const ElgCipher *y, const GroupPoint *key, const U256 *mu, const U256 *ctx) {
  ElgCipher ct;
  U256 zero;
  if (!group_on_curve(d))
    return JR_REFUSED;
  share_cipher(&ct, d, key, mu);
  u256_from_u64(&zero, 0u);
  return from_sig(sig_rand_verify(proof, &y->c1, &ct, &zero, ctx));
}

static JrResult check_m1(const JrM1 *m1, const JrTable *table, const JrStatement *st, const JrCtx *cx) {
  JrOrSt fst;
  ElgCipher sum;
  U256 zero;
  JrResult res = JR_OK;
  uint32_t j;
  uint32_t i;
  uint32_t b;
  u256_from_u64(&zero, 0u);
  for (j = 0; res == JR_OK && j < cx->m; j++) {
    for (i = 0; res == JR_OK && i < JR_GATE_BITS; i++)
      res = from_sig(sig_or_verify(&m1->g[j].proof[i], &cx->j, &m1->g[j].bit[i], &cx->ctx_g, j * JR_GATE_BITS + i));
    if (res == JR_OK) {
      gate_cipher(&sum, &m1->g[j], cx, cx->v[j]);
      res = from_sig(sig_rand_verify(&m1->g[j].sum, &cx->j, &sum, &zero, &cx->ctx_g));
    }
  }
  for (b = 0; res == JR_OK && b < table->c; b++)
    if (!cipher_on_curve(&m1->f[b]))
      res = JR_REFUSED;
  if (res != JR_OK)
    return res;
  f_statement(&fst, m1, table, st, cx);
  return or_verify(&m1->f_proof, &fst, &cx->ctx_f);
}

static JrResult check_m2(const JrM1 *m1, const JrM2 *m2, const JrTable *table, const JrStatement *st, const JrCtx *cx) {
  JrOrSt yst;
  JrResult res;
  if (!cipher_on_curve(&m2->y))
    return JR_REFUSED;
  y_statement(&yst, m1, m2, table, st, cx);
  res = or_verify(&m2->y_proof, &yst, &cx->ctx_y);
  if (res != JR_OK)
    return res;
  return check_share(&m2->d, &m2->d_proof, &m2->y, &st->key_k, &cx->mu_k, &cx->ctx_y);
}

/* Y.c2 - D_H - D_K: G when the pair passes, (0, 0) when it is closed. */
static JrResult outcome(const JrM2 *m2, const GroupPoint *d_h) {
  GroupPoint p;
  GroupPoint g;
  point_sub(&p, &m2->y.c2, d_h);
  point_sub(&p, &p, &m2->d);
  elg_generator(&g);
  return group_eq(&p, &g) ? JR_OK : JR_CLOSED;
}

JrResult jointr_send(JrM1 *out, const JrTable *table, const JrStatement *st, uint64_t q, const U256 *r, uint32_t a, const U256 *r_h, const U256 *seed) {
  JrCtx cx;
  JrOrSt fst;
  ElgCipher sum;
  ElgCipher z;
  U256 x[JR_C_MAX + 1u];
  U256 base;
  U256 sub;
  U256 word;
  U256 s;
  U256 t;
  U256 zero;
  uint64_t w;
  uint32_t bit;
  uint32_t j;
  uint32_t i;
  uint32_t b;
  SigResult sr = SIG_OK;
  JrResult res = setup(&cx, table, st);
  if (res != JR_OK)
    return res;
  if (q >= ((uint64_t)1 << JR_BITS) || a >= table->c)
    return JR_REFUSED;
  memset(out, 0, sizeof *out);
  u256_from_u64(&word, q);
  nonce_base(&base, seed, &st->ctx, &word, r, a);
  u256_from_u64(&zero, 0u);
  for (j = 0; sr == SIG_OK && j < cx.m; j++) {
    /* Bit 32 of V + 2^32 - Q is 1 iff Q <= V, for Q and V below 2^32. */
    w = cx.v[j] + ((uint64_t)1 << JR_BITS) - q;
    t = zero;
    for (i = 0; sr == SIG_OK && i < JR_GATE_BITS; i++) {
      bit = (uint32_t)((w >> i) & 1u);
      nonce(&s, &base, j * JR_GATE_BITS + i);
      u256_from_u64(&word, bit);
      elg_encrypt(&out->g[j].bit[i], &cx.j, &word, &s);
      sr = sig_or_prove(&out->g[j].proof[i], &cx.j, &out->g[j].bit[i], bit, &s, &cx.ctx_g, j * JR_GATE_BITS + i);
      u256_from_u64(&word, (uint64_t)1 << i);
      mul_add(&t, &word, &s, &t);
    }
    mul_add(&t, &cx.mu, &t, r);
    gate_cipher(&sum, &out->g[j], &cx, cx.v[j]);
    if (sr == SIG_OK)
      sr = sig_rand_prove(&out->g[j].sum, &cx.j, &sum, &zero, &t, &cx.ctx_g);
  }
  if (sr != SIG_OK)
    return from_sig(sr);
  for (b = 0; b < table->c; b++) {
    nonce(&x[1u + b], &base, 512u + b);
    elg_encrypt(&z, &cx.j, &zero, &x[1u + b]);
    out->f[b] = z;
    if (table->admit[a][b] != 0u)
      elg_add(&out->f[b], &z, &out->g[cap_index(&cx, table->cap[a][b])].bit[JR_BITS]);
  }
  x[0] = *r_h;
  f_statement(&fst, out, table, st, &cx);
  thr_hash(&sub, &base, &JR_F_TAG);
  or_prove(&out->f_proof, &fst, a, x, &sub, &cx.ctx_f);
  return JR_OK;
}

JrResult jointr_select(JrM2 *out, const JrM1 *m1, const JrTable *table, const JrStatement *st, uint32_t b, const U256 *r_k, const U256 *x_k, const U256 *seed) {
  JrCtx cx;
  JrOrSt yst;
  ElgCipher z;
  U256 x[2];
  U256 base;
  U256 sub;
  U256 zero;
  JrResult res = setup(&cx, table, st);
  if (res == JR_OK && b >= table->c)
    res = JR_REFUSED;
  if (res == JR_OK)
    res = check_m1(m1, table, st, &cx);
  if (res != JR_OK)
    return res;
  memset(out, 0, sizeof *out);
  nonce_base(&base, seed, &st->ctx, r_k, x_k, b);
  u256_from_u64(&zero, 0u);
  nonce(&x[1], &base, 0u);
  elg_encrypt(&z, &cx.j, &zero, &x[1]);
  elg_add(&out->y, &m1->f[b], &z);
  x[0] = *r_k;
  y_statement(&yst, m1, out, table, st, &cx);
  thr_hash(&sub, &base, &JR_Y_TAG);
  or_prove(&out->y_proof, &yst, b, x, &sub, &cx.ctx_y);
  return make_share(&out->d, &out->d_proof, &out->y, &st->key_k, &cx.mu_k, x_k, &cx.ctx_y);
}

JrResult jointr_open(JrM3 *out, const JrM1 *m1, const JrM2 *m2, const JrTable *table, const JrStatement *st, const U256 *x_h) {
  JrCtx cx;
  JrResult res = setup(&cx, table, st);
  if (res == JR_OK)
    res = check_m1(m1, table, st, &cx);
  if (res == JR_OK)
    res = check_m2(m1, m2, table, st, &cx);
  if (res == JR_OK)
    res = make_share(&out->d, &out->d_proof, &m2->y, &st->key_h, &cx.mu_h, x_h, &cx.ctx_y);
  if (res != JR_OK)
    return res;
  return outcome(m2, &out->d);
}

JrResult jointr_verify(const JrM1 *m1, const JrM2 *m2, const JrM3 *m3, const JrTable *table, const JrStatement *st) {
  JrCtx cx;
  JrResult res = setup(&cx, table, st);
  if (res == JR_OK)
    res = check_m1(m1, table, st, &cx);
  if (res == JR_OK)
    res = check_m2(m1, m2, table, st, &cx);
  if (res == JR_OK)
    res = check_share(&m3->d, &m3->d_proof, &m2->y, &st->key_h, &cx.mu_h, &cx.ctx_y);
  if (res != JR_OK)
    return res;
  return outcome(m2, &m3->d);
}
