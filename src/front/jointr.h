/* The joint threshold rule of a transfer in the host (S5 row C7, docs/PLAN.md
   4.1). The holder H of class A sends Q to the receiver K of class B. Both
   learn one bit: 1 when the pair (A, B) is admitted and Q is at most its cap,
   else 0. Neither learns the class of the other, Q or the cap.

   Q is encrypted under X_H and under X_K with one nonce R (the S5 transfer
   proof gives Q < 2^32). The joint key is J = MU_H X_H + MU_K X_K, where each
   weight MU_I hashes both keys (JR_TAG), so a key chosen after the other does
   not cancel it. Q_J = (c1, MU_H c2_H + MU_K c2_K) = Enc_J(MU Q; R) with MU =
   MU_H + MU_K is derived, with no proof of its own (B2-1).

   M1 (H): for each distinct cap V_j, 33 bit ciphers of V_j + 2^32 - Q under J
   with one sig_or proof each and one sig_rand proof of the sum; bit 32 is the
   gate G_j (1 iff Q <= V_j). Then one cipher F_B per class B: G_j(A, B)
   rerandomised when (A, B) is admitted, else Enc_J(0), with a 1-of-C
   OR-of-AND proof over the classes of H. M2 (K): Y = F_B + Enc_J(0) with a
   1-of-C proof over the classes of K, and the share D_K = MU_K x_K Y.c1. M3
   (H): the share D_H. Y.c2 - D_H - D_K is G (pass) or (0, 0) (closed). */
#ifndef LANG_FRONT_JOINTR_H
#define LANG_FRONT_JOINTR_H

#include <stdint.h>

#include "front/group.h"
#include "front/elgamal.h"
#include "front/sigma.h"
#include "front/u256.h"

/* Host bounds (B2-3, the SIG_K_MAX form): classes and distinct caps. */
#define JR_C_MAX 8u
#define JR_M_MAX 8u
/* Q and each cap are below 2^JR_BITS; a gate has JR_BITS + 1 bit ciphers. */
#define JR_BITS 32u
#define JR_GATE_BITS 33u

extern const U256 JR_TAG;
extern const U256 JR_F_TAG;
extern const U256 JR_Y_TAG;
extern const U256 JR_G_TAG;
extern const U256 JR_NONCE_TAG;

typedef enum { JR_OK, JR_REFUSED, JR_BAD_PROOF, JR_CLOSED } JrResult;

/* C classes (JURIS, STATUS); ADMIT[A][B] is 1 when the pair has the cap
   CAP[A][B]. */
typedef struct {
  uint32_t c;
  uint64_t juris[JR_C_MAX];
  uint64_t status[JR_C_MAX];
  uint32_t admit[JR_C_MAX][JR_C_MAX];
  uint64_t cap[JR_C_MAX][JR_C_MAX];
} JrTable;

/* The public part: the keys, the claim commitments of H and K, Q under each
   key, and the context of the transfer. */
typedef struct {
  GroupPoint key_h;
  GroupPoint key_k;
  GroupPoint com_h;
  GroupPoint com_k;
  ElgCipher q_h;
  ElgCipher q_k;
  U256 ctx;
} JrStatement;

typedef struct {
  ElgCipher bit[JR_GATE_BITS];
  SigOr proof[JR_GATE_BITS];
  SigDleq sum;
} JrGate;

/* A 1-of-N proof of N branches of K atoms (P1 = x B1, P2 = x B2): one
   challenge per branch, and the challenges sum to E mod n. */
typedef struct {
  U256 e[JR_C_MAX];
  SigDleq atom[JR_C_MAX][JR_C_MAX + 1u];
} JrOr;

typedef struct {
  JrGate g[JR_M_MAX];
  ElgCipher f[JR_C_MAX];
  JrOr f_proof;
} JrM1;

typedef struct {
  ElgCipher y;
  JrOr y_proof;
  GroupPoint d;
  SigDleq d_proof;
} JrM2;

typedef struct {
  GroupPoint d;
  SigDleq d_proof;
} JrM3;

/* J and the weights of KEY_H and KEY_K. Returns 0 when MU_H + MU_K = 0 mod n
   or J is (0, 0). */
int jointr_key(GroupPoint *j, U256 *mu_h, U256 *mu_k, const GroupPoint *key_h, const GroupPoint *key_k);

/* The M distinct caps of the admitted pairs, sorted up. Returns 0 when C is 0
   or above JR_C_MAX, M would pass JR_M_MAX, or a cap is at or above 2^32. */
int jointr_caps(uint64_t *v, uint32_t *m, const JrTable *table);

/* M1 for Q with the nonce R, the class A of H and its opening R_H. Refuses Q
   at or above 2^32, A out of the table, and q_h, q_k with two c1. */
JrResult jointr_send(JrM1 *out, const JrTable *table, const JrStatement *st, uint64_t q, const U256 *r, uint32_t a, const U256 *r_h, const U256 *seed);

/* M2 for the class B of K, its opening R_K and its secret X_K. Checks M1
   first. */
JrResult jointr_select(JrM2 *out, const JrM1 *m1, const JrTable *table, const JrStatement *st, uint32_t b, const U256 *r_k, const U256 *x_k, const U256 *seed);

/* M3 with the secret X_H of H. Checks M1 and M2 first; then JR_OK (pass) or
   JR_CLOSED. */
JrResult jointr_open(JrM3 *out, const JrM1 *m1, const JrM2 *m2, const JrTable *table, const JrStatement *st, const U256 *x_h);

/* The verdict of any party: JR_OK (pass), JR_CLOSED, or the first failure. */
JrResult jointr_verify(const JrM1 *m1, const JrM2 *m2, const JrM3 *m3, const JrTable *table, const JrStatement *st);

#endif
