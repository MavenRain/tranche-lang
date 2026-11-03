/* Rows for the claim commitment (S4 row C2): the pinned generators G1, G2
   and H (computed apart from this code, in Python, at S4 BUILD1), the
   homomorphic sum, and the zero openings. */
#include <stdio.h>

#include "front/claimcom.h"

static uint32_t rows;
static uint32_t failed;

static void row(int ok, const char *name) {
  rows++;
  if (!ok) {
    failed++;
    fprintf(stderr, "FAIL claimcom %s\n", name);
  }
}

static int same(const GroupPoint *a, const GroupPoint *b) {
  return fe_eq(&a->x, &b->x) && fe_eq(&a->y, &b->y);
}

/* 1 when P has the words X and Y. */
static int has_words(const GroupPoint *p, const U256 *x, const U256 *y) {
  U256 px;
  U256 py;
  group_to_words(&px, &py, p);
  return u256_cmp(&px, x) == 0 && u256_cmp(&py, y) == 0;
}

/* com(A, B, R) + com(C, D, S) = com(A + C, B + D, R + S); R + S must not
   overflow. */
static int homomorphic(uint64_t a, uint64_t b, const U256 *r, uint64_t c, uint64_t d, const U256 *s) {
  GroupPoint x;
  GroupPoint y;
  GroupPoint sum;
  GroupPoint want;
  U256 rs;
  if (!u256_add(&rs, r, s) || !claimcom(&x, a, b, r) || !claimcom(&y, c, d, s) || !claimcom(&want, a + c, b + d, &rs))
    return 0;
  group_add(&sum, &x, &y);
  return same(&sum, &want);
}

int main(void) {
  /* x and y of G1, G2 and H, limb 0 first, and the counter k of each. */
  const U256 g1x = {{0xf76932fa98c08981u, 0x758c3d39f7aaf323u, 0x825e9399f92701a9u, 0x2da1178e4d508688u}};
  const U256 g1y = {{0xf5f599a963936badu, 0x2a1052b78438ce03u, 0x21fa6c66a844e4f2u, 0x2cb47629bc05baa5u}};
  const U256 g2x = {{0xe7d0e407a5a271e3u, 0x7a6d9ed6b265f5a7u, 0xd4722e986add4732u, 0xec7450fbf4fc208fu}};
  const U256 g2y = {{0x033a88bb16528902u, 0x4fd621eb3da447f7u, 0x1512790064b2f46eu, 0x24c9b136cba9351bu}};
  const U256 hx = {{0xa856f79e785898feu, 0x2fa76a3657d90dedu, 0x85d1b3143b252f28u, 0x576e2c2d84f3ee66u}};
  const U256 hy = {{0xd309867055a273e5u, 0x029f775c911c5282u, 0xcfbd8e8a7fd32c2fu, 0x4d6bb774dcd5a015u}};
  const U256 zero = {{0u, 0u, 0u, 0u}};
  const U256 one = {{1u, 0u, 0u, 0u}};
  const U256 five = {{5u, 0u, 0u, 0u}};
  const U256 seven = {{7u, 0u, 0u, 0u}};
  const U256 nine = {{9u, 0u, 0u, 0u}};
  const U256 eleven = {{11u, 0u, 0u, 0u}};
  const U256 thirteen = {{13u, 0u, 0u, 0u}};
  /* The order n and n minus 1 of group.h, limb 0 first. */
  const U256 order = {{0xbfd25e8cd0364141u, 0xbaaedce6af48a03bu, 0xfffffffffffffffeu, 0xffffffffffffffffu}};
  const U256 order_less = {{0xbfd25e8cd0364140u, 0xbaaedce6af48a03bu, 0xfffffffffffffffeu, 0xffffffffffffffffu}};
  GroupPoint g1;
  GroupPoint g2;
  GroupPoint h;
  GroupPoint got;
  GroupPoint want;
  uint32_t k1 = 0;
  uint32_t k2 = 0;
  uint32_t kh = 0;
  int found;
  found = claimcom_generator(&g1, &k1, &CLAIMCOM_TAG_G1) && claimcom_generator(&g2, &k2, &CLAIMCOM_TAG_G2) && claimcom_generator(&h, &kh, &CLAIMCOM_TAG_H);
  row(found, "generators found");
  row(found && has_words(&g1, &g1x, &g1y) && k1 == 1u, "G1 vector");
  row(found && has_words(&g2, &g2x, &g2y) && k2 == 0u, "G2 vector");
  row(found && has_words(&h, &hx, &hy) && kh == 2u, "H vector");
  row(found && !same(&g1, &g2) && !same(&g1, &h) && !same(&g2, &h), "generators distinct");
  row(homomorphic(1u, 0u, &seven, 2u, 1u, &nine), "homomorphic small");
  row(homomorphic(2u, 1u, &order_less, 0u, 1u, &five), "homomorphic r + s above n");
  row(homomorphic(0x8000000000000000u, 3u, &eleven, 0x7fffffffffffffffu, 4u, &thirteen), "homomorphic juris 2^64 minus 1");
  /* r = 0 and r = n give none (no hiding). */
  row(!claimcom(&got, 1u, 1u, &zero), "r = 0");
  row(!claimcom(&got, 1u, 1u, &order), "r = n");
  /* juris = 0 and status = 0 (F5: 0 G1 is (0, 0)). */
  row(claimcom(&got, 0u, 0u, &one) && same(&got, &h), "com(0, 0, 1) = H");
  group_add(&want, &g2, &h);
  row(claimcom(&got, 0u, 1u, &one) && same(&got, &want), "com(0, 1, 1) = G2 + H");
  group_add(&want, &g1, &h);
  row(claimcom(&got, 1u, 0u, &one) && same(&got, &want), "com(1, 0, 1) = G1 + H");
  row(claimcom(&got, 2u, 1u, &seven) && group_on_curve(&got), "on the curve");
  printf("claimcom: %u rows, %u failed\n", rows, failed);
  return failed == 0 ? 0 : 1;
}
