/* A ring signature with no key image (S4 row C1). See front/ringsig.h.

   Builder defaults, each named in the commit message: the tag word, the
   message chain (KEY.x, KEY.y, COM.x, COM.y), the challenge fold (tag, D, m,
   L.x, L.y), the nonce fold (tag, x, D, m, i, role) mod n, and the ring key
   test (on the curve and not the point at infinity: a key at infinity has
   the secret 0, so any party could sign for its ring). */
#include "front/ringsig.h"

#include "sha256.h"

const U256 RINGSIG_TAG = {{0x652d617474657374u, 0x7472616e6368u, 0u, 0u}};

/* K G. */
static void gen_mul(GroupPoint *out, const U256 *k) {
  GroupPoint g;
  group_gen(&g);
  group_mul(out, &g, k);
}

static int word_zero(const U256 *x) {
  return x->limb[0] == 0 && x->limb[1] == 0 && x->limb[2] == 0 && x->limb[3] == 0;
}

/* 1 when each ring key is on the curve and is not the point at infinity. */
static int ring_ok(const GroupPoint *ring, uint32_t n) {
  U256 x;
  U256 y;
  uint32_t i;
  for (i = 0; i < n; i++) {
    group_to_words(&x, &y, &ring[i]);
    if (!group_on_curve(&ring[i]) || (word_zero(&x) && word_zero(&y)))
      return 0;
  }
  return 1;
}

void ringsig_message(U256 *out, const GroupPoint *key, const GroupPoint *com) {
  U256 x;
  U256 y;
  group_to_words(&x, &y, key);
  sha256_words(out, &x, &y);
  group_to_words(&x, &y, com);
  sha256_words(out, out, &x);
  sha256_words(out, out, &y);
}

/* The challenge: fold hash left over tag, D, m, L.x, L.y. */
static void challenge(U256 *out, const U256 *d, const U256 *m, const GroupPoint *l) {
  U256 x;
  U256 y;
  group_to_words(&x, &y, l);
  sha256_words(out, &RINGSIG_TAG, d);
  sha256_words(out, out, m);
  sha256_words(out, out, &x);
  sha256_words(out, out, &y);
}

/* Deterministic scalars as in front/blsag.c. Role 0 is the private alpha,
   role 1 a public decoy response. The ring digest and the role prevent
   nonce reuse when a signer changes rings or positions. */
static void nonce(U256 *out, const U256 *x, const U256 *d, const U256 *m, uint32_t i, uint32_t role) {
  U256 t;
  U256 k;
  sha256_words(&t, &RINGSIG_TAG, x);
  sha256_words(&t, &t, d);
  sha256_words(&t, &t, m);
  u256_from_u64(&k, i);
  sha256_words(&t, &t, &k);
  u256_from_u64(&k, role);
  sha256_words(&t, &t, &k);
  blsag_reduce(out, &t);
}

/* One link of the chain: L = s G + c P, and NEXT the challenge of L. NEXT
   can be C. */
static void link(U256 *next, const U256 *d, const U256 *m, const GroupPoint *p, const U256 *s, const U256 *c) {
  GroupPoint l;
  GroupPoint t;
  gen_mul(&l, s);
  group_mul(&t, p, c);
  group_add(&l, &l, &t);
  challenge(next, d, m, &l);
}

int ringsig_sign(U256 *sig, const GroupPoint *ring, uint32_t n, const U256 *x, const GroupPoint *key, const GroupPoint *com) {
  GroupPoint pj;
  GroupPoint l;
  U256 c[RINGSIG_RING_MAX];
  U256 d;
  U256 m;
  U256 alpha;
  U256 cj;
  Fe fx;
  Fe fc;
  Fe fa;
  Fe prod;
  Fe sj;
  uint32_t i;
  uint32_t j;
  uint32_t k;
  if (n == 0 || n > RINGSIG_RING_MAX || word_zero(x) || !fe_from_u256(&fx, x, &GROUP_N))
    return 0;
  if (!ring_ok(ring, n) || !group_on_curve(key) || !group_on_curve(com))
    return 0;
  gen_mul(&pj, x);
  j = 0;
  while (j < n && !(fe_eq(&pj.x, &ring[j].x) && fe_eq(&pj.y, &ring[j].y)))
    j++;
  if (j == n)
    return 0;
  blsag_digest(&d, ring, n);
  ringsig_message(&m, key, com);
  nonce(&alpha, x, &d, &m, j, 0u);
  gen_mul(&l, &alpha);
  challenge(&c[(j + 1u) % n], &d, &m, &l);
  for (k = 1; k < n; k++) {
    i = (j + k) % n;
    nonce(&sig[1u + i], x, &d, &m, i, 1u);
    link(&c[(i + 1u) % n], &d, &m, &ring[i], &sig[1u + i], &c[i]);
  }
  /* s_j = alpha minus c_j x mod n, with c_j reduced first. */
  blsag_reduce(&cj, &c[j]);
  fe_from_u256(&fc, &cj, &GROUP_N);
  fe_from_u256(&fa, &alpha, &GROUP_N);
  fe_mul(&prod, &fc, &fx, &GROUP_N);
  fe_sub(&sj, &fa, &prod, &GROUP_N);
  fe_to_u256(&sig[1u + j], &sj);
  sig[0] = c[0];
  return 1;
}

RingsigVerdict ringsig_verify(const GroupPoint *ring, uint32_t n, const GroupPoint *key, const GroupPoint *com, const U256 *sig) {
  U256 d;
  U256 m;
  U256 c;
  U256 t;
  uint32_t i;
  if (n == 0 || n > RINGSIG_RING_MAX || !ring_ok(ring, n) || !group_on_curve(key) || !group_on_curve(com))
    return RINGSIG_NONE;
  /* s_i below n: the reduction leaves it as it is. */
  for (i = 0; i < n; i++) {
    blsag_reduce(&t, &sig[1u + i]);
    if (u256_cmp(&t, &sig[1u + i]) != 0)
      return RINGSIG_REJECT;
  }
  blsag_digest(&d, ring, n);
  ringsig_message(&m, key, com);
  c = sig[0];
  for (i = 0; i < n; i++)
    link(&c, &d, &m, &ring[i], &sig[1u + i], &c);
  return u256_cmp(&c, &sig[0]) == 0 ? RINGSIG_ACCEPT : RINGSIG_REJECT;
}
