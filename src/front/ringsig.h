/* A ring signature with no key image over secp256k1 in the host: the
   attestation `att` of an Identity (docs/PLAN.md 4.1, S4 row C1, ruling
   Q-S4-1 (a)). It is the chain of front/blsag.c with the key image terms
   removed (the SAG form): one key of the ring signed, and the signature
   does not tell which key. The kit words (S4 BUILD2) must use the same hash
   chain order.

   Words are U256 (front/u256.h), points are affine GroupPoint (front/group.h)
   and hash(a, b) is the kit `hash`: SHA-256 of the 64 packed big endian
   bytes. The member index i runs from 0 in ring order. */
#ifndef LANG_FRONT_RINGSIG_H
#define LANG_FRONT_RINGSIG_H

#include <stdint.h>

#include "front/blsag.h"

/* The largest ring: maxIssuers (Q-S4-1 (a)), the bLSAG cap (O28). */
#define RINGSIG_RING_MAX BLSAG_RING_MAX

/* The domain tag: the ASCII bytes of "tranche-attest" as a word (O12: Part I
   adds its own tags). */
extern const U256 RINGSIG_TAG;

typedef enum {
  RINGSIG_REJECT,
  RINGSIG_ACCEPT,
  RINGSIG_NONE /* the ring size is bad, a ring key is off the curve or the point at infinity, or KEY or COM is off the curve */
} RingsigVerdict;

/* The message: m = hash(hash(hash(KEY.x, KEY.y), COM.x), COM.y). */
void ringsig_message(U256 *out, const GroupPoint *key, const GroupPoint *com);
/* Signs (KEY, COM) for the ring with the secret X, whose point x G must be
   in the ring. SIG gets the N + 1 words c_0, s_0 to s_(n minus 1). The
   chain: c_(i+1) = the hash fold of (tag, D, m, L_i.x, L_i.y) with
   L_i = s_i G + c_i P_i and D the ring digest (blsag_digest). Deterministic
   scalars bind tag, X, D, m, index and role (private alpha or public
   response). 1, or 0 when N is 0 or above RINGSIG_RING_MAX, a ring key is
   off the curve or the point at infinity, KEY or COM is off the curve, X is
   0 or GROUP_N or more, or x G is not in the ring. */
int ringsig_sign(U256 *sig, const GroupPoint *ring, uint32_t n, const U256 *x, const GroupPoint *key, const GroupPoint *com);
/* Checks the N + 1 words SIG for the ring and (KEY, COM): accept when the
   chain closes (c_n = c_0) and each s_i is below GROUP_N. No range test on
   c_0. */
RingsigVerdict ringsig_verify(const GroupPoint *ring, uint32_t n, const GroupPoint *key, const GroupPoint *com, const U256 *sig);

#endif
