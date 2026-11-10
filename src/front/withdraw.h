/* The withdraw proof in the host (tranche-lang S7 row C5, Q-S7-4,
   Q-S7-10).

   The holder H with the key X_H = x_H G holds OWN_H = Enc(bal; r) under X_H
   (the holdBal Ct). To take W out at the scale 2^P, H shows that
   bal - W 2^P is in [0, 2^Bbal) (O8, L18), with no secret in the proof:

   REST      Enc(bal - W 2^P mod n; t) under X_H, t from the nonce base;
   REST_KEY  a proof of randomness that D = OWN_H - Enc(W 2^P; 1) - REST
             decrypts to 0 under x_H (the rest_part form of
             front/transfer.c);
   BIT, PROOF, SUM
             the range proof of REST with n = WD_BITS (tr_range_build).

   REST < 2^Bbal, bal < 2^Bbal (the balance bound), W 2^P < 2^Bbal and
   2^(Bbal + 1) < n, so bal - W 2^P = REST holds as integers, with no
   reduction modulo n.

   The context binds the statement (Q-S7-10 (b)): the chain of thr_hash from
   WD_TAG over KEY_H (x, y), OWN_H (c1.x, c1.y, c2.x, c2.y) and W. The
   Schnorr signature on wdDigest binds the position and the id of the
   withdraw (Q-S7-3); the proof does not. */
#ifndef LANG_FRONT_WITHDRAW_H
#define LANG_FRONT_WITHDRAW_H

#include <stdint.h>

#include "front/transfer.h"

#define WD_BITS 160u   /* Bbal */
#define WD_P 32u       /* P */
#define WD_W_BITS 128u /* Bbal - P: the public bound of W */

extern const U256 WD_TAG;
extern const U256 WD_REST_TAG;
extern const U256 WD_NONCE_TAG;

typedef struct {
  GroupPoint key_h;
  ElgCipher own_h;
  U256 w;
} WdStatement;

typedef struct {
  ElgCipher rest;
  SigDleq rest_key;
  ElgCipher bit[WD_BITS];
  SigOr proof[WD_BITS];
  SigDleq sum;
} WithdrawProof;

/* The context of ST (Q-S7-10 (b)). The points of ST are on the curve. */
void withdraw_ctx(U256 *ctx, const WdStatement *st);

/* The proof for the balance MU, the value of OWN_H, with the secret X_H and
   the nonce base from SEED. No range check: the tool forges with it.
   TR_REFUSED when a point of ST is off the curve. */
TrResult withdraw_build(WithdrawProof *out, const WdStatement *st, const U256 *mu, const U256 *x_h, const U256 *seed);

/* TR_RANGE when W >= 2^WD_W_BITS, MU >= 2^WD_BITS or W 2^P > MU; else
   withdraw_build. */
TrResult withdraw_prove(WithdrawProof *out, const WdStatement *st, const U256 *mu, const U256 *x_h, const U256 *seed);

/* TR_RANGE when W >= 2^WD_W_BITS (the public bound comes first, PLAN 4.4
   withdraw), TR_REFUSED when a point is off the curve, TR_BAD_PROOF when
   REST_KEY or the range proof of REST fails, else TR_OK. */
TrResult withdraw_verify(const WithdrawProof *proof, const WdStatement *st);

#endif
