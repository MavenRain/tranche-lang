/* The word codec of the S5 transfer kit words in the host (S5 row C9, B3-2,
   B3-15, B3-16). M1, M2, M3 and the transfer proof (1) to (3) go to the guest
   as one `List U256` each (the S4 zkProve form). A point is 2 words (x, y),
   a scalar is 1 word. A message has only the parts that the table uses: M
   gates, C F ciphers, and C branches in each 1-of-C proof. Decode wants the
   exact length and refuses a point off the curve.

   Words at C = 1, M = 1: M1 548, M2 22, M3 7, the transfer proof 1053. */
#ifndef LANG_FRONT_TRWIRE_H
#define LANG_FRONT_TRWIRE_H

#include <stdint.h>

#include "front/group.h"
#include "front/elgamal.h"
#include "front/jointr.h"
#include "front/transfer.h"
#include "front/u256.h"

/* The tag of the transfer context: the ASCII bytes of "trctx" as a word. */
extern const U256 TRW_CTX_TAG;

/* M3 (D, D_PROOF) and the transfer proof (SAME[2], Q_BITS, REST, REST_KEY,
   REST_BITS) have a fixed length. */
#define TRW_M3_LEN 7u
#define TRW_PROOF_LEN 1053u

/* The words of M1 with C classes and M distinct caps, and of M2 with C
   classes. */
uint32_t trw_m1_len(uint32_t c, uint32_t m);
uint32_t trw_m2_len(uint32_t c);

/* Each put writes LEN words to OUT, and each get reads LEN words from IN.
   Each returns 1, or 0 when LEN is not the length of the message, C is not 1
   to JR_C_MAX, M is above JR_M_MAX, or (get) a point is off the curve. */
int trw_m1_put(U256 *out, uint32_t len, const JrM1 *m1, uint32_t c, uint32_t m);
int trw_m1_get(JrM1 *m1, const U256 *in, uint32_t len, uint32_t c, uint32_t m);
int trw_m2_put(U256 *out, uint32_t len, const JrM2 *m2, uint32_t c);
int trw_m2_get(JrM2 *m2, const U256 *in, uint32_t len, uint32_t c);
int trw_m3_put(U256 *out, uint32_t len, const JrM3 *m3);
int trw_m3_get(JrM3 *m3, const U256 *in, uint32_t len);
int trw_proof_put(U256 *out, uint32_t len, const TransferProof *pf);
int trw_proof_get(TransferProof *pf, const U256 *in, uint32_t len);

/* The context of a transfer (O28, B3-15): the chain of thr_hash from
   TRW_CTX_TAG over KEY_H and KEY_K (x, y each), then Q_H, Q_K and Q_O (c1.x,
   c1.y, c2.x, c2.y each). JrStatement and TrStatement take the same CTX. */
void trw_ctx(U256 *ctx, const GroupPoint *key_h, const GroupPoint *key_k, const ElgCipher *q_h,
             const ElgCipher *q_k, const ElgCipher *q_o);

/* The claimCom numbers of the class (J, S) with J the Juris constructor index
   and S the Status constructor index (B3-16, the claims.lang:34 form): JURIS
   = J + 1, STATUS = S. */
void trw_class(uint64_t *juris, uint64_t *status, uint32_t j, uint32_t s);

#endif
