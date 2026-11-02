/* Exponential ElGamal over secp256k1 in the host (slice S5 b5). See
   front/elgamal.h.

   The baby step table is static (ruling Q-S3-27): gen/elgtable.c writes the
   key of j G for each j below ELG_BABY_STEPS = 2^16 to build/elg_table.c at
   build time, with open addressing over 2 x 2^16 slots and a key from the
   low limbs of x and y (elg_point_key). A key hit is checked by a full m G,
   so a clash of keys costs time but never gives a wrong m. The giant steps
   go by group_walk: Jacobian adds and one inverse for each chunk. */
#include "front/elgamal.h"

#include "front/blsag.h"
#include "sha256.h"

const U256 ELG_TAG = {{0x656c67616d616cu, 0u, 0u, 0u}};

void elg_generator(GroupPoint *out) {
  group_gen(out);
}

static void gen_mul(GroupPoint *out, const U256 *k) {
  GroupPoint g;
  group_gen(&g);
  group_mul(out, &g, k);
}

void elg_nonce(U256 *out, uint64_t i) {
  U256 w;
  u256_from_u64(&w, i);
  sha256_words(&w, &ELG_TAG, &w);
  blsag_reduce(out, &w);
}

void elg_public(GroupPoint *out, const U256 *sk) {
  gen_mul(out, sk);
}

void elg_encrypt(ElgCipher *out, const GroupPoint *pk, const U256 *m, const U256 *r) {
  ElgCipher t;
  GroupPoint shared;
  gen_mul(&t.c1, r);
  gen_mul(&t.c2, m);
  group_mul(&shared, pk, r);
  group_add(&t.c2, &t.c2, &shared);
  *out = t;
}

void elg_add(ElgCipher *out, const ElgCipher *a, const ElgCipher *b) {
  group_add(&out->c1, &a->c1, &b->c1);
  group_add(&out->c2, &a->c2, &b->c2);
}

void elg_scale(ElgCipher *out, const ElgCipher *ct, const U256 *k) {
  group_mul(&out->c1, &ct->c1, k);
  group_mul(&out->c2, &ct->c2, k);
}

/* The m = BASE + j with m G = TARGET, for each slot j with the key of Q.
   Returns 1 when found. */
static int probe(uint64_t *m, const GroupPoint *q, const GroupPoint *target, uint64_t base) {
  const uint32_t mask = 2u * ELG_BABY_STEPS - 1u;
  uint32_t key = elg_point_key(q);
  uint32_t s = key & mask;
  U256 w;
  GroupPoint check;
  while (ELG_TABLE[s].step != 0) {
    if (ELG_TABLE[s].key == key) {
      u256_from_u64(&w, base + ELG_TABLE[s].step - 1u);
      gen_mul(&check, &w);
      if (group_eq(&check, target)) {
        *m = base + ELG_TABLE[s].step - 1u;
        return 1;
      }
    }
    s = (s + 1u) & mask;
  }
  return 0;
}

/* Giant step i is Q = POINT minus i 2^16 G, so a slot j with j G = Q gives
   m = i 2^16 + j. The giant steps go in chunks of up to GROUP_WALK_MAX - 1;
   the walk of a chunk has one more point, the first Q of the next chunk. */
ElgResult elg_log(uint64_t *m, const GroupPoint *point, uint32_t bits) {
  uint64_t giants;
  uint64_t i;
  uint64_t found;
  uint32_t c;
  uint32_t k;
  U256 w;
  GroupPoint stride;
  GroupPoint q;
  GroupPoint pts[GROUP_WALK_MAX];
  if (bits > ELG_BITS_MAX || !group_on_curve(point))
    return ELG_REFUSED;
  giants = bits <= ELG_BABY_BITS ? 1u : (uint64_t)1u << (bits - ELG_BABY_BITS);
  u256_from_u64(&w, ELG_BABY_STEPS);
  gen_mul(&stride, &w);
  group_neg(&stride, &stride);
  q = *point;
  i = 0;
  while (i < giants) {
    c = giants - i < GROUP_WALK_MAX - 1u ? (uint32_t)(giants - i) : GROUP_WALK_MAX - 1u;
    group_walk(pts, &q, &stride, c + 1u);
    for (k = 0; k < c; k++) {
      if (probe(&found, &pts[k], point, (i + k) * ELG_BABY_STEPS)) {
        /* The log below n is unique, so no m below 2^BITS. */
        if (found >> bits != 0u)
          return ELG_NOT_FOUND;
        *m = found;
        return ELG_FOUND;
      }
    }
    q = pts[c];
    i += c;
  }
  return ELG_NOT_FOUND;
}

ElgResult elg_decrypt(uint64_t *m, const U256 *sk, const ElgCipher *ct, uint32_t bits) {
  GroupPoint point;
  if (bits > ELG_BITS_MAX || !group_on_curve(&ct->c1) || !group_on_curve(&ct->c2))
    return ELG_REFUSED;
  group_mul(&point, &ct->c1, sk);
  group_neg(&point, &point);
  group_add(&point, &ct->c2, &point);
  return elg_log(m, &point, bits);
}
