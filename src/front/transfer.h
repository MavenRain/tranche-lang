/* The transfer proof in the host (S5 row C8, docs/PLAN.md 4.2 TransferProof
   (1) to (3)). The holder H sends Q to the receiver K. Q is encrypted under
   X_H, X_K and the open key X_O with one nonce R, and OWN_H encrypts the
   balance MU of H under X_H.

   (1) SAME[I] shows that Q_H and Q_I (I = K, O) hold one Q: with c1 equal,
   (c1, c2_H - c2_I) = (R G, R (X_H - X_I)) is sig_rand of 0 under the key
   X_H - X_I. (2) Q_BITS: 32 bit ciphers under X_H with one sig_or proof each,
   and one sig_rand proof that Q_H minus sum 2^i B_i encrypts 0, so Q < 2^32.
   (3) REST = Enc_{X_H}(MU - Q; T) is fresh. REST_KEY shows with the secret
   x_H that OWN_H - Q_H - REST decrypts to 0, and REST_BITS gives MU - Q <
   2^32, so Q <= MU. The joint key of C7 is derived from Q_H and Q_K (B2-1),
   so (1) also covers it. */
#ifndef LANG_FRONT_TRANSFER_H
#define LANG_FRONT_TRANSFER_H

#include <stdint.h>

#include "front/group.h"
#include "front/elgamal.h"
#include "front/sigma.h"
#include "front/u256.h"

/* Q, MU and MU - Q are below 2^TR_BITS (Units, N7). */
#define TR_BITS 32u

extern const U256 TR_TAG;
extern const U256 TR_Q_TAG;
extern const U256 TR_REST_TAG;
extern const U256 TR_NONCE_TAG;

typedef enum {
  TR_OK,
  TR_REFUSED,
  TR_RANGE,
  TR_BAD_PROOF
} TrResult;

/* The public part of a transfer. CTX hashes the transfer (from, to and the
   Q ciphers), so a proof does not verify on another transfer (O28). */
typedef struct {
  GroupPoint key_h;
  GroupPoint key_k;
  GroupPoint key_o;
  ElgCipher q_h;
  ElgCipher q_k;
  ElgCipher q_o;
  ElgCipher own_h;
  U256 ctx;
} TrStatement;

/* A value below 2^TR_BITS: one bit cipher and one sig_or proof per bit, and
   the sig_rand proof that the cipher minus sum 2^i B_i encrypts 0. */
typedef struct {
  ElgCipher bit[TR_BITS];
  SigOr proof[TR_BITS];
  SigDleq sum;
} TrRange;

typedef struct {
  SigDleq same[2];
  TrRange q_bits;
  ElgCipher rest;
  SigDleq rest_key;
  TrRange rest_bits;
} TransferProof;

/* The range proof that CT (nonce R) under PK holds V. TR_RANGE when V >=
   2^TR_BITS. The bit nonces come from BASE. */
TrResult transfer_range_prove(TrRange *out, const GroupPoint *pk, const ElgCipher *ct, uint64_t v, const U256 *r, const U256 *base, const U256 *ctx);

/* TR_REFUSED when a point is off the curve, TR_BAD_PROOF when a check fails. */
TrResult transfer_range_verify(const TrRange *proof, const GroupPoint *pk, const ElgCipher *ct, const U256 *ctx);

/* The proof of H with the nonce R of the Q ciphers, its balance MU and its
   secret X_H. TR_RANGE when MU is at or above 2^TR_BITS or Q > MU (so Q <=
   MU < 2^TR_BITS); TR_REFUSED when a point is off the curve or the Q ciphers
   do not share c1. */
TrResult transfer_prove(TransferProof *out, const TrStatement *st, uint64_t q, const U256 *r, uint64_t mu, const U256 *x_h, const U256 *seed);

/* transfer_prove without the range checks: the bits take the low TR_BITS
   bits of Q and of MU - Q. Only the tool calls it, to forge (2) and (3). */
TrResult transfer_build(TransferProof *out, const TrStatement *st, uint64_t q, const U256 *r, uint64_t mu, const U256 *x_h, const U256 *seed);

/* TR_REFUSED when a point is off the curve or the Q ciphers do not share c1,
   TR_BAD_PROOF when a check fails. */
TrResult transfer_verify(const TransferProof *proof, const TrStatement *st);

#endif
