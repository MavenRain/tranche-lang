/* Host FROST regressions (slice S7 b2, Q-S7-2 to Q-S7-5 (a)): the threshold
   Schnorr signatures of front/frost.c under the append key. The keyAppend
   committee comes from the DKG of front/dkg.c as in test/dkgtool.c (2 of 3,
   then the reshare to 3 of 4); a second committee from other seeds gives a
   key with the other y. Rows: the known answers of rho and c, 2 of 3 signs
   and verifies, a bad share gives AUTH_BAD_SHARE at its member index,
   another commitment list gives another rho and R, two sessions give two R,
   a key with the larger y and a key with the smaller y sign, and the 3 of 4
   key signs. The message is the head of the empty record (hash tagGenesis
   0) and the entry 42. The seeds are fixed for reproducible vectors only.
   With the argument `words` it also prints the closed Auth words of
   examples/append.lang (slice S7 b5), one `name 0x<word>` per line. The
   pay rows (slice S7 C7) run on every run: the C ixDigest gives the kernel
   vector, and the pay committee signs (ixHead log, ixDigest p). With the
   argument `pay` it also prints the pay words of examples/pay.lang in the
   same form. The wd rows (slice S7 C8) sign the G1 withdrawal of
   examples/pay.lang with one plain key and, with the argument `wd`,
   print its words in the same form. */
#include <stdio.h>
#include <string.h>

#include "front/blsag.h"
#include "front/dkg.h"
#include "front/elgamal.h"
#include "front/frost.h"

static int fail(const char *what) {
  fprintf(stderr, "FAIL frost %s\n", what);
  return 0;
}

static int holds(const char *name, int cond) {
  return cond ? 1 : fail(name);
}

static int expect(const char *name, AuthResult got, AuthResult want) {
  if (got == want)
    return 1;
  fprintf(stderr, "FAIL frost %s: result %d, want %d\n", name, (int)got, (int)want);
  return 0;
}

static void word(const char *name, const U256 *w) {
  printf("%s 0x%016llx%016llx%016llx%016llx\n", name, (unsigned long long)w->limb[3], (unsigned long long)w->limb[2],
         (unsigned long long)w->limb[1], (unsigned long long)w->limb[0]);
}

static int same(const U256 *a, const U256 *b) {
  return memcmp(a->limb, b->limb, sizeof a->limb) == 0;
}

/* 1 when P, a point on the curve, has the larger of its two y. */
static int larger_y(const GroupPoint *p) {
  GroupPoint low;
  U256 x;
  U256 y;
  group_to_words(&x, &y, p);
  return group_lift_x(&low, &x) && !group_eq(&low, p);
}

/* The binding factor of signer I as the header of front/frost.h states it:
   the chain of thr_hash from AUTH_RHO_TAG over I, PX, HEAD, E and then j,
   D.x, D.y, E.x, E.y for each of the K commitments of LIST, mod GROUP_N. */
static void rho_chain(U256 *out, uint32_t i, const U256 *px, const U256 *head, const U256 *e, const AuthCommit *list, uint32_t k) {
  U256 x;
  U256 y;
  uint32_t m;
  u256_from_u64(&x, i);
  thr_hash(out, &AUTH_RHO_TAG, &x);
  thr_hash(out, out, px);
  thr_hash(out, out, head);
  thr_hash(out, out, e);
  for (m = 0; m < k; m++) {
    u256_from_u64(&x, list[m].i);
    thr_hash(out, out, &x);
    group_to_words(&x, &y, &list[m].d);
    thr_hash(out, out, &x);
    thr_hash(out, out, &y);
    group_to_words(&x, &y, &list[m].e);
    thr_hash(out, out, &x);
    thr_hash(out, out, &y);
  }
  blsag_reduce(out, out);
}

/* The DKG of N members at the threshold T, member i with the seed SEEDS[i -
   1] and a_0 = thr_nonce(seed, 0): SHARES (x = 1 to N), the key PK and
   KEYS[x - 1] = Y_x. 1, or 0 when a dealing or a combine refuses or two
   members get different keys. */
static int committee(ThrShare *shares, GroupPoint *pk, GroupPoint *keys, const U256 *seeds, uint32_t t, uint32_t n) {
  DkgDeal deals[THR_N_MAX];
  U256 dealt[THR_N_MAX][THR_N_MAX];
  U256 col[THR_N_MAX];
  U256 a0;
  GroupPoint got;
  uint32_t i;
  uint32_t j;
  for (i = 0; i < n; i++) {
    thr_nonce(&a0, &seeds[i], 0u);
    if (!dkg_deal(&deals[i], dealt[i], &a0, &seeds[i], i + 1u, t, n))
      return 0;
  }
  for (j = 0; j < n; j++) {
    for (i = 0; i < n; i++)
      col[i] = dealt[i][j];
    if (!dkg_combine(&shares[j], &got, deals, col, n, t, n, j + 1u))
      return 0;
    if (j == 0u)
      *pk = got;
    if (!group_eq(pk, &got))
      return 0;
    keys[j] = shares[j].key;
  }
  return 1;
}

/* One signing session of the K members of SIGNERS (rising member indices)
   of the committee SHARES (T of N, KEYS) under PK for the message (HEAD,
   E): round 1 with the seeds blsag_key(SEED + m), round 2, then
   frost_aggregate into SIG. LIST and ZS get the commitments and the
   shares. */
static AuthResult session(AuthSig *sig, AuthCommit *list, AuthShare *zs, const ThrShare *shares, const uint32_t *signers, uint32_t k, const GroupPoint *keys, uint32_t t, uint32_t n, const GroupPoint *pk, const U256 *head, const U256 *e, uint64_t seed) {
  AuthNonce nonces[THR_N_MAX];
  AuthResult r;
  U256 s;
  uint32_t at;
  uint32_t m;
  for (m = 0; m < k; m++) {
    blsag_key(&s, seed + m);
    frost_commit(&nonces[m], &list[m], &shares[signers[m] - 1u], &s);
  }
  for (m = 0; m < k; m++) {
    r = frost_sign(&zs[m], &shares[signers[m] - 1u], &nonces[m], pk, head, e, list, k);
    if (r != AUTH_OK)
      return r;
  }
  return frost_aggregate(sig, &at, zs, list, k, keys, t, n, pk, head, e);
}

/* H3 of domain/domain.lang: hash (hash T D) E. */
static void h3(U256 *out, const U256 *t, const U256 *d, const U256 *e) {
  thr_hash(out, t, d);
  thr_hash(out, out, e);
}

/* ixDigest of domain/domain.lang (slice S7 C7) for the Instruction
   (PAYEE, ASSET, AMOUNT, POS) under TAG: hash (hash (hash (hash TAG
   PAYEE) ASSET) AMOUNT) (toU256 POS). */
static void ix_digest(U256 *d, const U256 *tag, uint64_t payee, uint64_t asset, uint64_t amount, uint64_t pos) {
  U256 x;
  u256_from_u64(&x, payee);
  thr_hash(d, tag, &x);
  u256_from_u64(&x, asset);
  thr_hash(d, d, &x);
  u256_from_u64(&x, amount);
  thr_hash(d, d, &x);
  u256_from_u64(&x, pos);
  thr_hash(d, d, &x);
}

/* word, only when SHOW is set. */
static void shown(int show, const char *name, const U256 *w) {
  if (show)
    word(name, w);
}

/* One pay signature: the 3 of 4 session with the signers 1, 2 and 3 on
   (HEAD, D), then schnorr_verify under PX. It puts the signature in SIG
   and, with SHOW, prints R_NAME and Z_NAME. 0 when the session or the
   check fails. */
static int pay_signed(int show, const char *r_name, const char *z_name, AuthSig *sig, const ThrShare *shares, const GroupPoint *keys, const GroupPoint *pk, const U256 *px, const U256 *head, const U256 *d, uint64_t seed) {
  static const uint32_t trio[3] = {1u, 2u, 3u};
  AuthCommit list[THR_N_MAX];
  AuthShare zs[THR_N_MAX];
  if (session(sig, list, zs, shares, trio, 3u, keys, 3u, 4u, pk, head, d, seed) != AUTH_OK || schnorr_verify(px, sig, head, d) != AUTH_OK)
    return fail(r_name);
  shown(show, r_name, &sig->rx);
  shown(show, z_name, &sig->z);
  return 1;
}

/* The pay rows (slice S7 C7): a 3 of 4 keyPay committee from the seeds
   blsag_key(600 + i) and its key payKey. p is the Instruction (payee 2,
   asset 3, amount 60, pos 20) and q is p with the amount 0. G0 is ixHead
   of the empty log (the head of the empty record), and h1 = H3 tagAppend
   g0 (ixDigest p) is ixHead of the log [p]. payDigest is ixDigest p,
   payDigest2 is ixDigest q and payHead1 is h1. payR, payZ: p at g0.
   payZBad is payZ plus 1. payReplayR, payReplayZ: p at h1. pay2R, pay2Z:
   q at h1. The C ixDigest of (1, 2, 3, 4) must give the kernel vector,
   payZBad must not verify, and the signature of p at g0 must not verify
   at h1. With SHOW it prints the words. 0 on a failure. */
static int pay_words(const U256 *g0, int show) {
  ThrShare shares[THR_N_MAX];
  GroupPoint pk;
  GroupPoint keys[THR_N_MAX];
  U256 seeds[THR_N_MAX];
  U256 tag;
  U256 append;
  U256 want;
  U256 px;
  U256 py;
  U256 one;
  U256 dv;
  U256 dp;
  U256 dq;
  U256 h1;
  AuthSig sig;
  AuthSig bad;
  AuthSig x;
  uint32_t i;
  for (i = 0; i < 4u; i++)
    blsag_key(&seeds[i], 600u + i);
  if (!committee(shares, &pk, keys, seeds, 3u, 4u))
    return fail("the keyPay committee");
  group_to_words(&px, &py, &pk);
  u256_from_u64(&tag, 0x6e6368652d706179ull);
  tag.limb[1] = 0x747261ull;
  u256_from_u64(&append, 0x6e2d617070656e64ull);
  append.limb[1] = 0x63616972ull;
  u256_from_u64(&want, 0xd8efb97c45225e47ull);
  want.limb[1] = 0x5fffa0c6d2ecb44dull;
  want.limb[2] = 0x6cd1b31d7746c5a1ull;
  want.limb[3] = 0xda35b30ff4652bebull;
  u256_from_u64(&one, 1u);
  ix_digest(&dv, &tag, 1u, 2u, 3u, 4u);
  ix_digest(&dp, &tag, 2u, 3u, 60u, 20u);
  ix_digest(&dq, &tag, 2u, 3u, 0u, 20u);
  h3(&h1, &append, g0, &dp);
  if (!holds("ixDigest gives the kernel vector", same(&dv, &want)))
    return 0;
  shown(show, "payKey", &px);
  shown(show, "payDigest", &dp);
  shown(show, "payDigest2", &dq);
  shown(show, "payHead1", &h1);
  if (!pay_signed(show, "payR", "payZ", &sig, shares, keys, &pk, &px, g0, &dp, 610u))
    return 0;
  bad = sig;
  u256_add(&bad.z, &sig.z, &one);
  shown(show, "payZBad", &bad.z);
  return holds("payZBad does not verify", schnorr_verify(&px, &bad, g0, &dp) != AUTH_OK)
         && holds("payR does not verify at h1", schnorr_verify(&px, &sig, &h1, &dp) != AUTH_OK)
         && pay_signed(show, "payReplayR", "payReplayZ", &x, shares, keys, &pk, &px, &h1, &dp, 620u)
         && pay_signed(show, "pay2R", "pay2Z", &x, shares, keys, &pk, &px, &h1, &dq, 630u);
}

/* The withdrawal words of examples/pay.lang (slice S7 C8, Q-S7-14): one
   plain Schnorr signature by the G1 key x_h = 11 on (m1, m2), with m1 =
   wdDigest (idKey f) bal 2 30 3 and m2 = wdProofDigest proof, the guest
   words of the G1 rows, and the nonce k = blsag_key(640). The x-only lift
   takes the smaller y, so x is negated when P = x G has the larger y and
   k when R = k G has the larger y; z = k + c x mod GROUP_N with c =
   frost_challenge(P.x, R.x, m1, m2). P.x must be the guest word g1Px, the
   signature must verify, and wdZBad (wdZ plus 1) must not. With SHOW it
   prints wdR, wdZ and wdZBad. 0 on a failure. */
static int wd_words(int show) {
  GroupPoint g;
  GroupPoint p;
  GroupPoint r;
  U256 xh;
  U256 k;
  U256 px;
  U256 py;
  U256 ry;
  U256 c;
  U256 m1;
  U256 m2;
  U256 want;
  U256 one;
  Fe fx;
  Fe fk;
  Fe fc;
  Fe neg;
  Fe cx;
  Fe fz;
  AuthSig sig;
  AuthSig bad;
  u256_from_u64(&m1, 0x8b33e465f8a9a485ull);
  m1.limb[1] = 0x3f467168ed4cc85eull;
  m1.limb[2] = 0xb9bb32488eacd884ull;
  m1.limb[3] = 0x2133fc014a91cd3full;
  u256_from_u64(&m2, 0xd07842695aa644abull);
  m2.limb[1] = 0xe04def187cf29c3bull;
  m2.limb[2] = 0x41382351ba699a48ull;
  m2.limb[3] = 0x38d6a0852dffe219ull;
  u256_from_u64(&want, 0xbbec17895da008cbull);
  want.limb[1] = 0x5649980be5c17891ull;
  want.limb[2] = 0x5ef4246b70c65aacull;
  want.limb[3] = 0x774ae7f858a9411eull;
  u256_from_u64(&xh, 11u);
  u256_from_u64(&one, 1u);
  blsag_key(&k, 640u);
  elg_generator(&g);
  group_mul(&p, &g, &xh);
  group_mul(&r, &g, &k);
  group_to_words(&px, &py, &p);
  group_to_words(&sig.rx, &ry, &r);
  if (!fe_from_u256(&fx, &xh, &GROUP_N) || !fe_from_u256(&fk, &k, &GROUP_N))
    return fail("the wd scalars");
  if (larger_y(&p)) {
    fe_neg(&neg, &fx, &GROUP_N);
    fx = neg;
  }
  if (larger_y(&r)) {
    fe_neg(&neg, &fk, &GROUP_N);
    fk = neg;
  }
  frost_challenge(&c, &px, &sig.rx, &m1, &m2);
  if (!fe_from_u256(&fc, &c, &GROUP_N))
    return fail("the wd challenge");
  fe_mul(&cx, &fc, &fx, &GROUP_N);
  fe_add(&fz, &fk, &cx, &GROUP_N);
  fe_to_u256(&sig.z, &fz);
  bad = sig;
  u256_add(&bad.z, &sig.z, &one);
  if (!holds("wdKey is g1Px", same(&px, &want)) || !holds("wdZ verifies", schnorr_verify(&px, &sig, &m1, &m2) == AUTH_OK)
      || !holds("wdZBad does not verify", schnorr_verify(&px, &bad, &m1, &m2) != AUTH_OK))
    return 0;
  shown(show, "wdR", &sig.rx);
  shown(show, "wdZ", &sig.z);
  shown(show, "wdZBad", &bad.z);
  return 1;
}

int main(int argc, char **argv) {
  static const uint32_t pair[2] = {1u, 2u};
  static const uint32_t append_q[2] = {1u, 3u};
  static const uint32_t three[3] = {2u, 3u, 4u};
  ThrShare old[THR_N_MAX];
  ThrShare fresh[THR_N_MAX];
  ThrShare other[THR_N_MAX];
  GroupPoint pk;
  GroupPoint other_pk;
  GroupPoint old_keys[THR_N_MAX];
  GroupPoint new_keys[THR_N_MAX];
  GroupPoint other_keys[THR_N_MAX];
  DkgDeal deals[THR_N_MAX];
  U256 dealt[THR_N_MAX][THR_N_MAX];
  U256 col[THR_N_MAX];
  U256 seeds[THR_N_MAX];
  U256 genesis;
  U256 zero;
  U256 one;
  U256 head;
  U256 e;
  U256 px;
  U256 py;
  U256 ox;
  U256 oy;
  U256 rho_a;
  U256 rho_b;
  U256 want;
  U256 c;
  U256 z_bad;
  AuthCommit list_a[THR_N_MAX];
  AuthCommit list_b[THR_N_MAX];
  AuthCommit list_o[THR_N_MAX];
  AuthCommit list_t[THR_N_MAX];
  AuthShare zs_a[THR_N_MAX];
  AuthShare zs_b[THR_N_MAX];
  AuthShare zs_o[THR_N_MAX];
  AuthShare zs_t[THR_N_MAX];
  AuthShare bad[2];
  AuthSig sig_a;
  AuthSig sig_b;
  AuthSig sig_o;
  AuthSig sig_t;
  AuthSig sig_x;
  uint32_t at = 0u;
  uint32_t i;
  uint32_t j;
  int high;
  int ok = 1;
  /* The keyAppend committee of test/dkgtool.c: 2 of 3, seeds blsag_key(20 + i). */
  for (i = 0; i < 3u; i++)
    blsag_key(&seeds[i], 20u + i);
  if (!committee(old, &pk, old_keys, seeds, 2u, 3u)) {
    fail("the 2 of 3 DKG refused");
    return 1;
  }
  group_to_words(&px, &py, &pk);
  high = larger_y(&pk);
  printf("keyAppend key: the %s y\n", high ? "larger" : "smaller");
  /* The message: the head of the empty record and the entry 42. */
  u256_from_u64(&zero, 0u);
  u256_from_u64(&genesis, 0x2d67656e65736973ull);
  genesis.limb[1] = 0x636169726eull;
  thr_hash(&head, &genesis, &zero);
  u256_from_u64(&e, 42u);
  u256_from_u64(&one, 1u);
  ok &= expect(high ? "2 of 3 signs (a key with the larger y)" : "2 of 3 signs (a key with the smaller y)",
               session(&sig_a, list_a, zs_a, old, pair, 2u, old_keys, 2u, 3u, &pk, &head, &e, 300u), AUTH_OK)
        & expect("2 of 3 verifies", schnorr_verify(&px, &sig_a, &head, &e), AUTH_OK);
  frost_rho(&rho_a, 1u, &px, &head, &e, list_a, 2u);
  rho_chain(&want, 1u, &px, &head, &e, list_a, 2u);
  ok &= holds("KAT rho (the chain of front/frost.h)", same(&rho_a, &want));
  frost_challenge(&c, &px, &sig_a.rx, &head, &e);
  thr_hash(&want, &AUTH_TAG, &px);
  thr_hash(&want, &want, &sig_a.rx);
  thr_hash(&want, &want, &head);
  thr_hash(&want, &want, &e);
  blsag_reduce(&want, &want);
  ok &= holds("KAT c (the chain of front/frost.h)", same(&c, &want));
  /* z + 1 on the share of member 2, at list position 1. */
  bad[0] = zs_a[0];
  bad[1] = zs_a[1];
  u256_add(&bad[1].z, &zs_a[1].z, &one);
  ok &= expect("a bad share", frost_aggregate(&sig_x, &at, bad, list_a, 2u, old_keys, 2u, 3u, &pk, &head, &e), AUTH_BAD_SHARE)
        & holds("a bad share is at its member index", at == list_a[1].i && at == 2u);
  ok &= expect("a second session signs", session(&sig_b, list_b, zs_b, old, pair, 2u, old_keys, 2u, 3u, &pk, &head, &e, 310u), AUTH_OK);
  frost_rho(&rho_b, 1u, &px, &head, &e, list_b, 2u);
  ok &= holds("another commitment list gives another rho and another R", !same(&rho_a, &rho_b) && !same(&sig_a.rx, &sig_b.rx))
        & holds("two sessions give two R", !same(&sig_a.rx, &sig_b.rx));
  /* The other y: the B1 smoke seeds base * 10 + i (base 1 gives the larger
     y, base 3 the smaller y). */
  for (i = 0; i < 3u; i++)
    u256_from_u64(&seeds[i], (high ? 3u : 1u) * 10u + i);
  if (!committee(other, &other_pk, other_keys, seeds, 2u, 3u)) {
    fail("the second 2 of 3 DKG refused");
    return 1;
  }
  group_to_words(&ox, &oy, &other_pk);
  ok &= holds("the second key has the other y", larger_y(&other_pk) != high);
  ok &= expect(high ? "a key with the smaller y signs" : "a key with the larger y signs",
               session(&sig_o, list_o, zs_o, other, pair, 2u, other_keys, 2u, 3u, &other_pk, &head, &e, 320u), AUTH_OK)
        & expect("the other key verifies", schnorr_verify(&ox, &sig_o, &head, &e), AUTH_OK);
  /* The reshare of test/dkgtool.c keyAppend: members 1 and 3 deal to 3 of 4. */
  for (i = 0; i < 2u; i++) {
    blsag_key(&seeds[i], 120u + i);
    if (!dkg_reshare(&deals[i], dealt[i], &old[append_q[i] - 1u], append_q, 2u, &seeds[i], 3u, 4u)) {
      fail("a reshare dealing refused");
      return 1;
    }
  }
  for (j = 0; j < 4u; j++) {
    for (i = 0; i < 2u; i++)
      col[i] = dealt[i][j];
    if (!dkg_reshare_combine(&fresh[j], deals, col, 2u, old_keys, 2u, 3u, &pk, j + 1u)) {
      fail("the reshare aborted");
      return 1;
    }
    new_keys[j] = fresh[j].key;
  }
  ok &= expect("the 3 of 4 key signs", session(&sig_t, list_t, zs_t, fresh, three, 3u, new_keys, 3u, 4u, &pk, &head, &e, 330u), AUTH_OK)
        & expect("the 3 of 4 key verifies", schnorr_verify(&px, &sig_t, &head, &e), AUTH_OK);
  if (!ok)
    return 1;
  if (!pay_words(&head, argc == 2 && strcmp(argv[1], "pay") == 0))
    return 1;
  if (!wd_words(argc == 2 && strcmp(argv[1], "wd") == 0))
    return 1;
  if (argc == 2 && strcmp(argv[1], "words") == 0) {
    u256_add(&z_bad, &sig_a.z, &one);
    word("key", &px);
    word("otherKey", &ox);
    word("head", &head);
    word("entry", &e);
    word("authR", &sig_a.rx);
    word("authZ", &sig_a.z);
    word("authZBad", &z_bad);
    word("authR34", &sig_t.rx);
    word("authZ34", &sig_t.z);
  }
  puts("frost regressions: passed");
  return 0;
}
