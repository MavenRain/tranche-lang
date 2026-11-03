/* The claim commitment (S4 row C2). See front/claimcom.h.

   Builder defaults, each named in the commit message: the three tag words
   and the generator fold hash(tag, k). */
#include "front/claimcom.h"

#include "sha256.h"

const U256 CLAIMCOM_TAG_G1 = {{0x616e6368652d6731u, 0x7472u, 0u, 0u}};
const U256 CLAIMCOM_TAG_G2 = {{0x616e6368652d6732u, 0x7472u, 0u, 0u}};
const U256 CLAIMCOM_TAG_H = {{0x72616e6368652d68u, 0x74u, 0u, 0u}};

static int word_zero(const U256 *x) {
  return x->limb[0] == 0 && x->limb[1] == 0 && x->limb[2] == 0 && x->limb[3] == 0;
}

int claimcom_generator(GroupPoint *out, uint32_t *counter, const U256 *tag) {
  U256 k;
  U256 xk;
  uint32_t i;
  for (i = 0; i < BLSAG_HP_TRIES; i++) {
    u256_from_u64(&k, i);
    sha256_words(&xk, tag, &k);
    if (group_lift_x(out, &xk)) {
      *counter = i;
      return 1;
    }
  }
  return 0;
}

/* OUT += K times the generator of TAG. */
static int add_term(GroupPoint *out, const U256 *tag, const U256 *k) {
  GroupPoint g;
  GroupPoint t;
  uint32_t counter = 0;
  if (!claimcom_generator(&g, &counter, tag))
    return 0;
  group_mul(&t, &g, k);
  group_add(out, out, &t);
  return 1;
}

int claimcom(GroupPoint *out, uint64_t juris, uint64_t status, const U256 *r) {
  const U256 zero = {{0u, 0u, 0u, 0u}};
  GroupPoint acc;
  U256 w;
  U256 j;
  U256 s;
  blsag_reduce(&w, r);
  if (word_zero(&w) || !group_from_words(&acc, &zero, &zero))
    return 0;
  u256_from_u64(&j, juris);
  u256_from_u64(&s, status);
  if (!add_term(&acc, &CLAIMCOM_TAG_G1, &j) || !add_term(&acc, &CLAIMCOM_TAG_G2, &s) || !add_term(&acc, &CLAIMCOM_TAG_H, &w))
    return 0;
  *out = acc;
  return 1;
}
