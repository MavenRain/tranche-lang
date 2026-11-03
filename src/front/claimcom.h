/* The claim commitment of an Identity in the host (docs/PLAN.md 4.1
   ClaimCom, S4 row C2): com = juris G1 + status G2 + r H, a Pedersen
   commitment on secp256k1. G1, G2 and H come from hash to curve by try and
   increment (the blsag_hash_point form), so no one knows a relation between
   them or to G. The holder keeps the opening (juris, status, r) secret
   (O4). */
#ifndef LANG_FRONT_CLAIMCOM_H
#define LANG_FRONT_CLAIMCOM_H

#include <stdint.h>

#include "front/blsag.h"

/* The ASCII bytes of "tranche-g1", "tranche-g2" and "tranche-h" as words
   (O12: Part I adds its own tags). */
extern const U256 CLAIMCOM_TAG_G1;
extern const U256 CLAIMCOM_TAG_G2;
extern const U256 CLAIMCOM_TAG_H;

/* The generator of TAG: x_k = hash(TAG, k) from k = 0; the first x_k below
   p with x_k^3 + 7 a square gives the point, with y the smaller root.
   *COUNTER gets k. 1, or 0 when BLSAG_HP_TRIES counters fail. */
int claimcom_generator(GroupPoint *out, uint32_t *counter, const U256 *tag);
/* OUT = JURIS G1 + STATUS G2 + R H for any 256-bit R. 1, or 0 when R is 0
   mod n (no hiding) or a generator fails. A zero JURIS or STATUS gives the
   point at infinity for its term (group.h: (0, 0)), so it needs no special
   case. */
int claimcom(GroupPoint *out, uint64_t juris, uint64_t status, const U256 *r);

#endif
