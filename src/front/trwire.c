#include <string.h>

#include "front/trwire.h"

#include "front/threshold.h"

const U256 TRW_CTX_TAG = {{0x7472637478u, 0u, 0u, 0u}};

/* The words of each part. */
#define W_POINT 2u
#define W_CIPHER 4u
#define W_DLEQ 5u
#define W_OR 12u
#define W_GATE (JR_GATE_BITS * (W_CIPHER + W_OR) + W_DLEQ)

/* A cursor over LEN words. OK falls to 0 on a word past LEN or a point off
   the curve. */
typedef struct {
  U256 *out;
  const U256 *in;
  uint32_t at;
  uint32_t len;
  int ok;
} Cur;

static Cur put_cur(U256 *out, uint32_t len) {
  Cur c;
  c.out = out;
  c.in = NULL;
  c.at = 0;
  c.len = len;
  c.ok = 1;
  return c;
}

static Cur get_cur(const U256 *in, uint32_t len) {
  Cur c;
  c.out = NULL;
  c.in = in;
  c.at = 0;
  c.len = len;
  c.ok = 1;
  return c;
}

/* 1 when each word was used and no part failed. */
static int done(const Cur *c) {
  return c->ok && c->at == c->len;
}

static int classes_ok(uint32_t c) {
  return c >= 1u && c <= JR_C_MAX;
}

static void put_word(Cur *c, const U256 *v) {
  if (c->at < c->len) {
    c->out[c->at] = *v;
    c->at++;
  } else
    c->ok = 0;
}

static void put_point(Cur *c, const GroupPoint *p) {
  U256 x;
  U256 y;
  group_to_words(&x, &y, p);
  put_word(c, &x);
  put_word(c, &y);
}

static void put_cipher(Cur *c, const ElgCipher *e) {
  put_point(c, &e->c1);
  put_point(c, &e->c2);
}

static void put_dleq(Cur *c, const SigDleq *d) {
  put_point(c, &d->a);
  put_point(c, &d->b);
  put_word(c, &d->z);
}

static void put_or(Cur *c, const SigOr *o) {
  put_point(c, &o->a0);
  put_point(c, &o->b0);
  put_point(c, &o->a1);
  put_point(c, &o->b1);
  put_word(c, &o->e0);
  put_word(c, &o->e1);
  put_word(c, &o->z0);
  put_word(c, &o->z1);
}

/* N challenges, then N branches of K atoms. */
static void put_jr_or(Cur *c, const JrOr *o, uint32_t n, uint32_t k) {
  uint32_t i;
  uint32_t j;
  for (i = 0; i < n; i++)
    put_word(c, &o->e[i]);
  for (i = 0; i < n; i++)
    for (j = 0; j < k; j++)
      put_dleq(c, &o->atom[i][j]);
}

static void put_gate(Cur *c, const JrGate *g) {
  uint32_t i;
  for (i = 0; i < JR_GATE_BITS; i++)
    put_cipher(c, &g->bit[i]);
  for (i = 0; i < JR_GATE_BITS; i++)
    put_or(c, &g->proof[i]);
  put_dleq(c, &g->sum);
}

static void put_range(Cur *c, const TrRange *r) {
  uint32_t i;
  for (i = 0; i < TR_BITS; i++)
    put_cipher(c, &r->bit[i]);
  for (i = 0; i < TR_BITS; i++)
    put_or(c, &r->proof[i]);
  put_dleq(c, &r->sum);
}

static void get_word(Cur *c, U256 *v) {
  if (c->at < c->len) {
    *v = c->in[c->at];
    c->at++;
  } else {
    memset(v, 0, sizeof *v);
    c->ok = 0;
  }
}

static void get_point(Cur *c, GroupPoint *p) {
  U256 x;
  U256 y;
  get_word(c, &x);
  get_word(c, &y);
  if (!group_from_words(p, &x, &y)) {
    memset(p, 0, sizeof *p);
    c->ok = 0;
  }
}

static void get_cipher(Cur *c, ElgCipher *e) {
  get_point(c, &e->c1);
  get_point(c, &e->c2);
}

static void get_dleq(Cur *c, SigDleq *d) {
  get_point(c, &d->a);
  get_point(c, &d->b);
  get_word(c, &d->z);
}

static void get_or(Cur *c, SigOr *o) {
  get_point(c, &o->a0);
  get_point(c, &o->b0);
  get_point(c, &o->a1);
  get_point(c, &o->b1);
  get_word(c, &o->e0);
  get_word(c, &o->e1);
  get_word(c, &o->z0);
  get_word(c, &o->z1);
}

static void get_jr_or(Cur *c, JrOr *o, uint32_t n, uint32_t k) {
  uint32_t i;
  uint32_t j;
  for (i = 0; i < n; i++)
    get_word(c, &o->e[i]);
  for (i = 0; i < n; i++)
    for (j = 0; j < k; j++)
      get_dleq(c, &o->atom[i][j]);
}

static void get_gate(Cur *c, JrGate *g) {
  uint32_t i;
  for (i = 0; i < JR_GATE_BITS; i++)
    get_cipher(c, &g->bit[i]);
  for (i = 0; i < JR_GATE_BITS; i++)
    get_or(c, &g->proof[i]);
  get_dleq(c, &g->sum);
}

static void get_range(Cur *c, TrRange *r) {
  uint32_t i;
  for (i = 0; i < TR_BITS; i++)
    get_cipher(c, &r->bit[i]);
  for (i = 0; i < TR_BITS; i++)
    get_or(c, &r->proof[i]);
  get_dleq(c, &r->sum);
}

/* M gates, C F ciphers, and the F proof of C branches of C + 1 atoms. */
uint32_t trw_m1_len(uint32_t c, uint32_t m) {
  return m * W_GATE + c * W_CIPHER + c + c * (c + 1u) * W_DLEQ;
}

/* Y, the Y proof of C branches of 2 atoms, D and D_PROOF. */
uint32_t trw_m2_len(uint32_t c) {
  return W_CIPHER + c + c * 2u * W_DLEQ + W_POINT + W_DLEQ;
}

int trw_m1_put(U256 *out, uint32_t len, const JrM1 *m1, uint32_t c, uint32_t m) {
  Cur cur = put_cur(out, len);
  uint32_t i;
  if (!classes_ok(c) || m > JR_M_MAX)
    return 0;
  for (i = 0; i < m; i++)
    put_gate(&cur, &m1->g[i]);
  for (i = 0; i < c; i++)
    put_cipher(&cur, &m1->f[i]);
  put_jr_or(&cur, &m1->f_proof, c, c + 1u);
  return done(&cur);
}

int trw_m1_get(JrM1 *m1, const U256 *in, uint32_t len, uint32_t c, uint32_t m) {
  Cur cur = get_cur(in, len);
  uint32_t i;
  memset(m1, 0, sizeof *m1);
  if (!classes_ok(c) || m > JR_M_MAX || len != trw_m1_len(c, m))
    return 0;
  for (i = 0; i < m; i++)
    get_gate(&cur, &m1->g[i]);
  for (i = 0; i < c; i++)
    get_cipher(&cur, &m1->f[i]);
  get_jr_or(&cur, &m1->f_proof, c, c + 1u);
  return done(&cur);
}

int trw_m2_put(U256 *out, uint32_t len, const JrM2 *m2, uint32_t c) {
  Cur cur = put_cur(out, len);
  if (!classes_ok(c))
    return 0;
  put_cipher(&cur, &m2->y);
  put_jr_or(&cur, &m2->y_proof, c, 2u);
  put_point(&cur, &m2->d);
  put_dleq(&cur, &m2->d_proof);
  return done(&cur);
}

int trw_m2_get(JrM2 *m2, const U256 *in, uint32_t len, uint32_t c) {
  Cur cur = get_cur(in, len);
  memset(m2, 0, sizeof *m2);
  if (!classes_ok(c) || len != trw_m2_len(c))
    return 0;
  get_cipher(&cur, &m2->y);
  get_jr_or(&cur, &m2->y_proof, c, 2u);
  get_point(&cur, &m2->d);
  get_dleq(&cur, &m2->d_proof);
  return done(&cur);
}

int trw_m3_put(U256 *out, uint32_t len, const JrM3 *m3) {
  Cur cur = put_cur(out, len);
  put_point(&cur, &m3->d);
  put_dleq(&cur, &m3->d_proof);
  return done(&cur);
}

int trw_m3_get(JrM3 *m3, const U256 *in, uint32_t len) {
  Cur cur = get_cur(in, len);
  memset(m3, 0, sizeof *m3);
  if (len != TRW_M3_LEN)
    return 0;
  get_point(&cur, &m3->d);
  get_dleq(&cur, &m3->d_proof);
  return done(&cur);
}

int trw_proof_put(U256 *out, uint32_t len, const TransferProof *pf) {
  Cur cur = put_cur(out, len);
  put_dleq(&cur, &pf->same[0]);
  put_dleq(&cur, &pf->same[1]);
  put_range(&cur, &pf->q_bits);
  put_cipher(&cur, &pf->rest);
  put_dleq(&cur, &pf->rest_key);
  put_range(&cur, &pf->rest_bits);
  return done(&cur);
}

int trw_proof_get(TransferProof *pf, const U256 *in, uint32_t len) {
  Cur cur = get_cur(in, len);
  memset(pf, 0, sizeof *pf);
  if (len != TRW_PROOF_LEN)
    return 0;
  get_dleq(&cur, &pf->same[0]);
  get_dleq(&cur, &pf->same[1]);
  get_range(&cur, &pf->q_bits);
  get_cipher(&cur, &pf->rest);
  get_dleq(&cur, &pf->rest_key);
  get_range(&cur, &pf->rest_bits);
  return done(&cur);
}

static void absorb(U256 *acc, const GroupPoint *p) {
  U256 x;
  U256 y;
  group_to_words(&x, &y, p);
  thr_hash(acc, acc, &x);
  thr_hash(acc, acc, &y);
}

void trw_ctx(U256 *ctx, const GroupPoint *key_h, const GroupPoint *key_k, const ElgCipher *q_h,
             const ElgCipher *q_k, const ElgCipher *q_o) {
  *ctx = TRW_CTX_TAG;
  absorb(ctx, key_h);
  absorb(ctx, key_k);
  absorb(ctx, &q_h->c1);
  absorb(ctx, &q_h->c2);
  absorb(ctx, &q_k->c1);
  absorb(ctx, &q_k->c2);
  absorb(ctx, &q_o->c1);
  absorb(ctx, &q_o->c2);
}

void trw_class(uint64_t *juris, uint64_t *status, uint32_t j, uint32_t s) {
  *juris = (uint64_t)j + 1u;
  *status = s;
}
