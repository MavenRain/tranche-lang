/* Rows for the ring signature with no key image (S4 row C1): accept on rings
   of 1, 2 and 28, a flipped bit of each word rejects, a changed KEY, COM or
   ring rejects, and the bad rings and keys give none. */
#include <stdio.h>

#include "front/ringsig.h"

static uint32_t rows;
static uint32_t failed;

static void row(int ok, const char *name, uint32_t i) {
  rows++;
  if (!ok) {
    failed++;
    fprintf(stderr, "FAIL ringsig %s %u\n", name, i);
  }
}

/* Signs with the secret of member J of the first N keys: accept, each
   flipped word rejects, and a second signing gives the same words. */
static void ring_rows(const GroupPoint *ring, const U256 *keys, uint32_t n, uint32_t j, const GroupPoint *key, const GroupPoint *com) {
  U256 sig[RINGSIG_RING_MAX + 1u];
  U256 again[RINGSIG_RING_MAX + 1u];
  U256 bad[RINGSIG_RING_MAX + 1u];
  uint32_t i;
  uint32_t w;
  int same = 1;
  row(ringsig_sign(sig, ring, n, &keys[j], key, com), "sign", n);
  row(ringsig_verify(ring, n, key, com, sig) == RINGSIG_ACCEPT, "accept", n);
  for (i = 0; i <= n; i++) {
    for (w = 0; w <= n; w++)
      bad[w] = sig[w];
    bad[i].limb[0] ^= 1u;
    row(ringsig_verify(ring, n, key, com, bad) == RINGSIG_REJECT, "flipped word", i);
  }
  row(ringsig_sign(again, ring, n, &keys[j], key, com), "sign again", n);
  for (i = 0; i <= n; i++)
    same = same && u256_cmp(&sig[i], &again[i]) == 0;
  row(same, "deterministic", n);
}

int main(void) {
  const U256 zero = {{0u, 0u, 0u, 0u}};
  const U256 five = {{5u, 0u, 0u, 0u}};
  const U256 seven = {{7u, 0u, 0u, 0u}};
  /* The order n of group.h, limb 0 first. */
  const U256 order = {{0xbfd25e8cd0364141u, 0xbaaedce6af48a03bu, 0xfffffffffffffffeu, 0xffffffffffffffffu}};
  GroupPoint g;
  GroupPoint ring[RINGSIG_RING_MAX + 1u];
  GroupPoint bad_ring[2];
  GroupPoint key;
  GroupPoint com;
  GroupPoint off;
  U256 keys[RINGSIG_RING_MAX + 1u];
  U256 sig[RINGSIG_RING_MAX + 2u];
  uint32_t i;
  group_gen(&g);
  for (i = 0; i <= RINGSIG_RING_MAX; i++) {
    blsag_key(&keys[i], i);
    group_mul(&ring[i], &g, &keys[i]);
  }
  group_mul(&key, &g, &five);
  group_mul(&com, &g, &seven);
  ring_rows(ring, keys, 1u, 0u, &key, &com);
  ring_rows(ring, keys, 2u, 1u, &key, &com);
  ring_rows(ring, keys, RINGSIG_RING_MAX, 13u, &key, &com);
  /* A changed KEY, COM or ring rejects. */
  row(ringsig_sign(sig, ring, 2u, &keys[0], &key, &com), "sign for changes", 2u);
  row(ringsig_verify(ring, 2u, &com, &com, sig) == RINGSIG_REJECT, "changed key", 2u);
  row(ringsig_verify(ring, 2u, &key, &key, sig) == RINGSIG_REJECT, "changed com", 2u);
  row(ringsig_verify(&ring[1], 2u, &key, &com, sig) == RINGSIG_REJECT, "changed ring", 2u);
  /* A secret not in the ring, the secret 0 and the secret n sign nothing. */
  row(!ringsig_sign(sig, ring, 2u, &keys[2], &key, &com), "secret not in ring", 2u);
  row(!ringsig_sign(sig, ring, 2u, &zero, &key, &com), "secret 0", 2u);
  row(!ringsig_sign(sig, ring, 2u, &order, &key, &com), "secret n", 2u);
  /* Ring 0 and ring 29 give none and sign nothing. */
  row(ringsig_verify(ring, 0u, &key, &com, sig) == RINGSIG_NONE, "verify ring", 0u);
  row(ringsig_verify(ring, RINGSIG_RING_MAX + 1u, &key, &com, sig) == RINGSIG_NONE, "verify ring", RINGSIG_RING_MAX + 1u);
  row(!ringsig_sign(sig, ring, 0u, &keys[0], &key, &com), "sign ring", 0u);
  row(!ringsig_sign(sig, ring, RINGSIG_RING_MAX + 1u, &keys[0], &key, &com), "sign ring", RINGSIG_RING_MAX + 1u);
  /* An off-curve ring key and a ring key at infinity give none. */
  bad_ring[0] = ring[0];
  bad_ring[1] = ring[1];
  bad_ring[1].y = ring[1].x;
  row(ringsig_verify(bad_ring, 2u, &key, &com, sig) == RINGSIG_NONE, "off-curve ring key", 1u);
  row(!ringsig_sign(sig, bad_ring, 2u, &keys[0], &key, &com), "sign off-curve ring key", 1u);
  row(group_from_words(&bad_ring[1], &zero, &zero), "infinity", 1u);
  row(ringsig_verify(bad_ring, 2u, &key, &com, sig) == RINGSIG_NONE, "ring key at infinity", 1u);
  row(!ringsig_sign(sig, bad_ring, 2u, &keys[0], &key, &com), "sign ring key at infinity", 1u);
  /* An off-curve KEY or COM gives none. */
  off = key;
  off.y = key.x;
  row(ringsig_verify(ring, 2u, &off, &com, sig) == RINGSIG_NONE, "off-curve key", 2u);
  row(ringsig_verify(ring, 2u, &key, &off, sig) == RINGSIG_NONE, "off-curve com", 2u);
  printf("ringsig: %u rows, %u failed\n", rows, failed);
  return failed == 0 ? 0 : 1;
}
