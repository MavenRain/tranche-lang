# tranche-lang plan

tranche-lang is a Cubical TT language for one job: off-chain ownership
interests. A measure of units on attested identities gives each holder a
pro-rata claim on the rent and sale inflows of an asset. The holdings are
encrypted (FHE), each transfer carries a proof (ZK), and each opening needs
a threshold of the committee (MPC). A threshold polity governs the rules
(the restriction, the waterfall, the issuers, the polity itself) off chain,
and it can amend its own rules. No checked program has a run-time error.

The design is `design/DESIGN.md`: the request, verbatim, the S0 answers,
the interest-lang domain design, verbatim, and the orbit-lang domain
design, verbatim. This file gives the readings of that request, the route,
the slices and the open questions. Section 12 lists the questions for the
USER. A reading that is not ruled stays a recommendation.

The name: interest-lang DESIGN.md:110 (fe3f47b) reads an ERC-1400 token as
a coproduct of measures, "tranches are summands, not a second algebra".

Status: S3 DONE: N1 and N5 to N8 in the kernel, the eval and the
lowering (rounds R1 to R3): `Option` results and `NonZero`, no trap in
the eval, `Fail` and `Except Fail` in the prelude, `elg_log` with B = 32,
the entry ABI with no trap (Q-S3-22, Q-S3-23), `upTo K` loops, and the
`bounds` and `fails` of `check --json` (Q-S3-32 to Q-S3-37). `make check`
EXIT 0. Next: S4 (Part I).

## 1. Rulings

| Date | Ruling | Source |
|---|---|---|
| 2026-11-01 | Targets: Wasm MVP (tcc-wasm kit), ES module + JSON (tcc-js kit), WGSL compute (tcc-webgpu kit). Wasm is the primary target. The gate runs it first, and the differential tests compare it with `langc eval`. | DESIGN.md, S0 answers |
| 2026-11-01 | Name: tranche-lang. | DESIGN.md, S0 answers |
| 2026-11-01 | Host: TinyCC. The request asks for "an MCP server written in TinyCC", so all of `langc` is C99 that TinyCC builds, `langc mcp` too. | DESIGN.md, request |
| 2026-11-01 | S1, the section 12 questions. The USER answers, verbatim: "(a) Keep cairn (Recommended)", "(a) No time (Recommended)", "(a) own + pub (Recommended)", "(a) B = 32 (Recommended)", "(c) Private claims", "(b) Encrypted balances", "(a) Members (Recommended)", "(b) AmendBallot only", "(a) Static capacity (Recommended)", "(a) cairn rulings (Recommended)". So: Q-S0-1 (a), Q-S0-2 (a), Q-S0-3 (a), Q-S0-4 (a), Q-S0-5 (c), Q-S0-6 (b), Q-S0-7 (a), Q-S0-8 (b), Q-S0-9 (a), Q-S0-10 (a). Sections 2 to 11 read Q-S0-5 (c), Q-S0-6 (b) and Q-S0-8 (b). | USER, S1 (AskUserQuestion) |
| 2026-11-01 | S2-Q1, the domain tags. The USER answer, verbatim: "(a) Keep cairn tags in S2 (Recommended)". The 7 hash tags of the port ("cairn", "cairn-genesis", "cairn-append", "cairn-out", "cairn-auth", "cairn-authw", "cairn-rho") and their comments stay, so the gate vectors equal cairn-lang S13 byte for byte (O12). | USER, S2 (AskUserQuestion) |
| 2026-11-01 | Q-S3-1, verbatim: "(a) Carry over (Recommended)". The accrue-lang S3 rulings Q-S3-1 to Q-S3-9, Q-S3-20 and Q-S3-21 (accrue-lang dbf4e11) hold here. `natAdd`, `natSub`, `u256Add`, `u256Sub` and a new `u256Mul` give an `Option` (`none` on an overflow or a borrow). `natDiv` and `natMod` take a `NonZero` divisor. `mulDiv : U256 -> U256 -> NonZero U256 -> Option U256`. The 16 trap sites and `VAL_TRAP` go, and the crypto words give an `Option`. A literal other than 0 checks as a `NonZero`. `REFUSE_PARTIAL` has 3 sites. | USER, S3 (AskUserQuestion) |
| 2026-11-01 | Q-S3-2, verbatim: "(c) Trap-free ABI now". S3 does the entry ABI of S8 now: an entry with an `Option` result gives the host a status word (`none` or a value), with no `unreachable` in Wasm, no `throw` in JS and no trap status in WGSL for `none`. The `test/entrytest.c` grid (USER 9aafbf8) stays in the gate and reads `none`. | USER, S3 (AskUserQuestion) |
| 2026-11-01 | Q-S3-3, verbatim: "(a) Prelude, kit stays Option (Recommended)". The kit words keep `Option` (O9). `domain/domain.lang` gets the closed data `Fail` (the 12 N6 constructors) and the family `Except` (a cell monad: `eta`, `fail`) with `orFail : Fail -> Option A -> Except Fail A`. `langc eval` prints the `Fail` value, and `check --json` lists the `Fail` values that each entry can give. | USER, S3 (AskUserQuestion) |
| 2026-11-01 | Q-S3-4, verbatim: "(a) Measure + refuse, literal cap (Recommended)". `check --json` gives each entry its operation count, stack depth and memory. `REFUSE_UNBOUNDED` refuses a loop with a run-time trip count; its fix hint and `.fix` row name a literal-capacity form (an `upTo K` word). N7: a kit word over `elg_log` with `B = 32` (Q-S0-4 (a)), `none` at `2^32`. | USER, S3 (AskUserQuestion) |
| 2026-11-01 | Q-S3-22, verbatim: "(a) Status out-param (Recommended)". The form of the Q-S3-2 (c) status word: an entry with an `Option` result takes one more argument, the address of a status word. The entry writes 0 (`none`) or 1 (`some`) there and returns the payload (0 for `none`). The same form in Wasm (an `i32` address in linear memory), JS (an `Int32Array(1)`) and WGSL (one status word for each invocation). A call keeps no state between calls. The number 22 comes after the accrue-lang rulings that S3 carries (Q-S3-1 to Q-S3-21). | USER, S3 R2 (AskUserQuestion) |
| 2026-11-01 | Q-S3-23, verbatim: "(a) Second word (Recommended)". The WGSL form of the Q-S3-22 status: the out row of each invocation keeps its status word (0 ok, 1 TRAP, 2 none written). An entry with an `Option` result writes its Q-S3-22 status (0 `none`, 1 `some`) in a second word after that word. | USER, S3 R2 (AskUserQuestion) |
| 2026-11-01 | Q-S3-24, verbatim: "(a) Want row follows (Recommended)". examples/zk.lang `noSigner` keeps its R1 form (`bind ring2 (fun rg => bind (zkSign ...) ...)`). The stuck bind puts `zkSign` at the head, so the lowering gives REFUSE_TARGET_SECRET before the `zkVerify` refusal (REFUSE_LOWER). The want rows (build, ir) for `noSigner` become REFUSE_TARGET_SECRET. | USER, S3 R2 (AskUserQuestion) |
| 2026-11-01 | Q-S3-25, verbatim: "(a) Except A over closed Fail (Recommended)". `domain/domain.lang` declares `family Except (A : Type 0) := exEta (exEtaOf : A) | exFail (exFailOf : Fail)`: one type parameter, the one-cell form of section 3. `Except Fail A` in this plan is `Except A`. `orFail : (A : Type 0) -> Fail -> Option A -> Except A`. | USER, S3 (AskUserQuestion) |
| 2026-11-01 | Q-S3-26, verbatim: "(a) elgLog (Recommended)". The N7 kit word is `elgLog : Prod U256 U256 -> Option Nat` over `elg_log` with `B = 32`: `some m` for the point m G with m below 2^32, `none` at 2^32 and for a point off the curve. It has no target form until S8. | USER, S3 (AskUserQuestion) |
| 2026-11-02 | Q-S3-27, verbatim: "(a) Static table (Recommended)". `elg_log` searches with a static baby step table: a generator tool writes the 2^16 baby step keys of `j G` as a .c table at build time (Jacobian adds, one batched normalize per chunk). The giant steps are Jacobian adds with one batched inversion per chunk. `bits` up to 32 use the static table; a found m of 2^bits or more gives `none`. Measured before (tcc): `group_add` 6.52 ms (one `fe_inv`, 5.47 ms), `fe_mul` 14.9 us, so one `elg_log` at bits 32 took about 7 min (m = 7) to 14 min (2^32 - 1). | USER, S3 (AskUserQuestion) |
| 2026-11-02 | Q-S3-28, verbatim: "(a) Refuse all 4 sites + upTo K (Recommended)". Measure (C9a, survey R3 s5): 2 of the 56 lowered modules have a run-time trip count, both the entry `square` (examples/algebra.lang, examples/entries.lang; a Nat fold, lower.c `fold_nat`). `REFUSE_UNBOUNDED` refuses a loop whose trip count is not a literal at the 4 loop sites (`fold_nat`, `unfold`, `chain_end`, `walk_with`). The new kit word `upTo K` (K a Nat literal) is a fold with the literal trip count K that gives `none` when n > K. `square` in the 2 examples moves to `upTo`. The fix hint and the `.fix` row name `upTo K` (not a list capacity). | USER, S3 (AskUserQuestion) |
| 2026-11-02 | Q-S3-29, verbatim: "(b) upTo K covers lists too (Recommended)". Correction to Q-S3-28: the gate also lowers 4 test files with run-time trip-count loops (test/ir/spine.lang 45, test/ir/comb.lang 21, test/ir/lazy.lang 1, test/wasm/traps.lang 3; Nat folds, list-length REPEATs, spine WHILE on fuel 2^63). Q-S3-28 stays at all 4 sites, and `upTo K` also gets a list form: a list walk or fold with a literal capacity K that gives `none` when the list is longer. The 4 test files move to `upTo` and keep their wasm and js differential rows. | USER, S3 (AskUserQuestion) |
| 2026-11-02 | Q-S3-30, verbatim: "(a) One word, fold args (Recommended)". The shape of the Q-S3-28/29 word: `upTo K f z x : Option A`, K a Nat literal, x a `Nat` or a `List B` (the word picks the form from x, as `fold` does), f of the `fold` step type (`A -> A`, or `B -> A -> A`). The result is `some (fold f z x)` when x <= K (or the length of x <= K), else `none`. In C9b2, `square` moves to `bind (upTo K ...) (fun r => r)`. | USER, S3 R3 (AskUserQuestion) |
| 2026-11-02 | Q-S3-31, verbatim: "Fuse into upTo folds (Recommended)". A gap of C9b2 (survey R3 s8): with REFUSE_UNBOUNDED at `walk_with`, a walk over a run-time list or FreeMon (`fold`, `map`, `filter`, `bind`, `fmChain`, `fmRun`, `len`) cannot lower, and `upTo` has only the Nat and List fold forms. Each def of test/ir/spine.lang and test/ir/comb.lang moves to one `upTo` Nat or List fold with the same outputs (the map, filter or bind work goes in the fold step; comb calculates its digit fold directly). The rows and the wasm and js differential rows stay. The walks of `map`, `filter`, `bind`, `fmChain`, `fmRun` and `len` are always REFUSE_UNBOUNDED; their lowerings stay behind the refusal. | USER, S3 R3 (AskUserQuestion) |
| 2026-11-02 | Q-S3-32, verbatim: "IR units (Recommended)". The C9c N8 bounds are in IR units (survey R3 s13). `ops`: the IR statements and expression nodes that run; a loop multiplies its body by its literal count, IF and SWITCH take the larger arm, a prelude CALL counts 1. `stack`: words, `local_count` plus the status word plus the deepest expression; the IR has no call between funcs, thus each entry is one frame. `memory`: heap words, the fields of each ALLOC plus the result block of each prelude CALL, times the loop counts, the larger arm. The target costs (Wasm, ES, WGSL) stay for S8. | USER, S3 R3 (AskUserQuestion) |
| 2026-11-02 | Q-S3-33, verbatim: "bounds array after progress (Recommended)". The `check --json` line gets `,"bounds":[{"entry":"NAME","ops":N,"stack":S,"memory":M}]` after `progress`: one object for each entry, in source order, and `[]` when there is no entry or the check fails. Each of the 21 `test/json` rows changes. | USER, S3 R3 (AskUserQuestion) |
| 2026-11-02 | Q-S3-34, verbatim: "S3, refusal = diagnostic (Recommended)". C9c is in S3. `check --json` runs `lower_program` after a clean check, with the notes to a sink. A lowering refusal (`REFUSE_UNBOUNDED`, `REFUSE_LOWER`, `REFUSE_TARGET_SECRET` and the others) is the one diagnostic, exit 1 (N8: a program with no bound is refused). `langc check` without `--json` does not change. C9c adds the `REFUSE_UNBOUNDED` json row, and `docs/CHECK-JSON.md` drops "as `langc check` does". The S8 row keeps the target measure. | USER, S3 R3 (AskUserQuestion) |
| 2026-11-02 | Q-S3-35, verbatim: "Except defs, normal form (Recommended)". No entry can give a `Fail`: `entry_of` admits a Nat, Flag or U256 result or an `Option` of one, and `Except` is a family (O24). `check --json` lists each program def with an `Except` result and the `Fail` constructors in its normal form. This is an upper set: the inputs are Nat, Flag or U256, thus each `Fail` value comes from a constructor in the term. The key is `,"fails":[{"def":"NAME","values":[...]}]` after `bounds`, `[]` when there is no such def or the check fails. The bound entries have no `fails` key. | USER, S3 R3 (AskUserQuestion) |
| 2026-11-02 | Q-S3-36, verbatim: "Extend + 4 rows (Recommended)". The gate rule `json refusal codes` reads the `REFUSE_` codes of `src/front/check.c` and `src/front/lower.c`. Each lowering code gets a `test/json` row with a `.json` and a `.fix`: `REFUSE_UNBOUNDED`, `REFUSE_LOWER`, `REFUSE_RUNTIME_CLOSURE`, `REFUSE_RUNTIME_TREE` and `REFUSE_TARGET_SECRET` (14 to 19 codes). | USER, S3 R3 (AskUserQuestion) |
| 2026-11-02 | Q-S3-37, verbatim: "mcp check = check --json (Recommended)". The text of the mcp `check` tool stays the `check --json` line byte for byte, thus `test/mcp/check.out` (ids 2 to 4) and `test/mcp/kit-check.out` (id 2) get the `bounds` and `fails` keys (Q-S3-33). No code change. | USER, S3 R3 (AskUserQuestion) |
| 2026-11-02 | Q-S4-1, verbatim: "Issuer ring (Recommended)". O11 closed. `att` is a ring signature with no key image on `(key, com)` over `ring`, a copy of `issuers` (capacity `maxIssuers` = 28, blsag.h:19). The ledger cannot see which issuer signed. An amendment that removes an issuer makes each attestation whose `ring` has it give `badAttest`; its holder gets a new attestation. | USER, S4 (AskUserQuestion) |
| 2026-11-02 | Q-S4-2, verbatim: "Joint proof". O10 closed as (b). The two parties of a transfer make the R row proof together, and no party sees the claims of the other. `R` stays `Claims -> Claims -> Option Units`. PLAN 4.1:174-178 changes: only the attesting issuer sees the claims of a holder; the receiver does not give the opening of `com k` to the sender. S5 designs the two-party proof (much more code than the sender form); the S5 survey asks its form. | USER, S4 (AskUserQuestion) |
| 2026-11-02 | Q-S4-3, verbatim: "Kit word rTable (Recommended)". The program writes `def table := rTable R`. `rTable` checks that `Juris` and `Status` are closed sums with no fields, evaluates `R` on each pair at check time and gives the admitted rows with their caps. `check --json` reports the row count. No reserved names. | USER, S4 (AskUserQuestion) |
| 2026-11-02 | Q-S4-4, verbatim: "Except Identity (Recommended)". `attested : Constitution -> Identity -> Except Identity`: `exEta` of the identity when the ring signature verifies and each key of `ring` is in `issuers`, else `exFail badAttest`. `check --json` lists `badAttest` in `fails`. | USER, S4 (AskUserQuestion) |
| 2026-11-02 | Q-S4-5, verbatim: "(a) CPoint (Recommended)". The domain adds `def CPoint : Type 0 := Prod U256 U256`, a curve point (the kit form of `claimCom`, `attestSign` and `attestVerify`); `Point` stays the x-only member key. `issuers : FreeMon CPoint`; `Identity (key : CPoint, com : CPoint, ring : List CPoint, att : List U256)`. `ringIn` folds `ring` (capacity `maxIssuers` = 28) with a member test in `issuers` (point equality by two U256 equalities). | USER, S4 BUILD2b (AskUserQuestion) |
| 2026-11-03 | Q-S4-6, verbatim: "Covenant (Recommended)". The record of 4.3 is `Covenant`, with the maker `makeCovenant`. The kernel gives `Constitution` (the social-choice Sigma of orbit-lang, as in IsSelfConstituting) and the domain gives the family `Charter`, so a domain def with either name is `REFUSE_NAME` (rule R4). The plan prose keeps the word constitution. | USER, S4 BUILD2d (AskUserQuestion) |
| 2026-11-03 | Q-S4-7, verbatim: "List of rows (Recommended)", "28 rows + 1024 pairs (Recommended)", "New key tables (Recommended)". `rTable R` has the type `List (Prod (Prod C C) Nat)` with `C = Prod Juris Status` (Units is S5, so S4 writes Nat): the admitted rows `pair (pair h k) cap` in constructor order, a static list. It is not an entry, so it has no bounds row and no lowering. REFUSE_UNBOUNDED when the admitted rows are more than 28 (BLSAG_RING_MAX: the S5 row proof is a 1-of-n OR over the rows) or the pairs \|J\|^2 \|S\|^2 are more than 1024 (check-time work). `check --json` has a new key `"tables":[{"def":"NAME","rows":N,"pairs":P}]` after `fails`, always present, so the 28 test/json/*.json lines get `,"tables":[]`. | USER, S4 BUILD3 (AskUserQuestion) |
| 2026-11-03 | Q-S4-8, verbatim: "Generic + investor (Recommended)". R1 permits a family only in domain/domain.lang, so after the C5 block (Covenant, Identity): `family Juris := jurisA \| jurisB \| jurisC`, `family Status := retail \| accredited` (no fields), Claims in the Identity record form. examples/claims.lang declares no family. | USER, S4 BUILD3d (AskUserQuestion) |
| 2026-11-03 | Q-S4-9, verbatim: "(a) Fix in S4 (Recommended)". Each def that lowers (closed or entry) and reaches a secret op (the `secret_op` set of `src/front/lower.c`, transitive over def refs) refuses REFUSE_TARGET_SECRET, so a closed def such as `k := zkKey 0` refuses too, though its value is a constant before the lowering. The gate ir arm accepts that code only for examples/zk.lang, examples/claims.lang and examples/zkcast.lang. The C8 mutant that drops the walk call is KILLED by test/json/REFUSE_TARGET_SECRET.closed. O4. | USER, S4 BUILD3i (AskUserQuestion) |
| 2026-11-03 | Q-S5-1, verbatim: "Oblivious select (Recommended)". The joint R row proof of Q-S4-2 (b) is an oblivious selection and a joint opening of one bit under the joint key `J` (Q-S5-2), in three messages. M1 (sender to receiver): `Q_J`, the ciphertext of `q` under `J` (a DLEQ with the `q` ciphertexts); the threshold bits `G` (Q-S5-3); and `F_b` = Enc_J(f(a, b)) for each receiver class `b`, with an OR over the sender classes that `com h` opens to the class and that each `F_b` re-randomizes its `G_j` (or encrypts 0 for a refused pair). M2 (receiver to sender): `Y` = `F_b` + Enc_J(0; s) with an OR over the receiver classes, and the share `d_k` with a DLEQ. M3 (sender, only after it checks M2): the share `d_h` with a DLEQ. The verifier checks that `Y.c2 - d_h - d_k` is the generator (the selected bit is 1). Each party learns only the outcome bit. The ledger learns that the pair is admitted with `q <= cap`. The kit proof check `transferVerify` is HOST-LIMIT until S8 (the `attestVerify` form). The provers `jrSend`, `jrSelect` and `jrOpen` are secret ops. A refused pair and `q > cap` stop the protocol before a Transfer exists, so `refusedPair` and `overCap` are values of the party side, and `transfer` sees only `badProof`. | USER, S5 (AskUserQuestion) |
| 2026-11-03 | Q-S5-2, verbatim: "MuSig weights (Recommended)". The joint key is `J = mu_h key h + mu_k key k` with `mu_i = hash("tranche-joint", key h, key k, key i)`. There is no setup. A rogue key (`key k = K - key h` with a known `K`) does not give a known `J`. The tag is new (the O12 list). | USER, S5 (AskUserQuestion) |
| 2026-11-03 | Q-S5-3, verbatim: "Threshold bits (Recommended)". The `q <= cap` part has one threshold bit `G_j` = Enc_J([q <= v_j]) for each distinct cap value `v_j` of the table (m <= 28). Each has an OR of (`G_j` is 1 and `v_j - q` is in `[0, 2^B)`) and (`G_j` is 0 and `q - v_j - 1` is in `[0, 2^B)`): 64 m bit proofs. `F` links to `G` by re-randomization (DLEQ only). | USER, S5 (AskUserQuestion) |
| 2026-11-03 | Q-S5-4, verbatim: "Domain families (Recommended)". O4: domain/domain.lang declares `family HolderKey := holderKey (holderKeyOf : U256)` and `family Nonce := nonceOf (nonceVal : U256)`. The Q-S4-9 walk also refuses `REFUSE_TARGET_SECRET` for each lowered def that names either family, its constructor or its field. `decryptIn : HolderKey -> Ct -> Except Units` and `ctEnc : CPoint -> Units -> Nonce -> Option Ct`. | USER, S5 (AskUserQuestion) |
| 2026-11-03 | Q-S5-5, verbatim: "Families". Not the recommendation, (b): `family Units := unitsOf (unitsVal : Nat)` and `family Ct := ctOf (ctC1 : CPoint) (ctC2 : CPoint)`, with checked constructors: `toUnits : Nat -> Except Units` gives `outOfRange` at `2^32` and above. The `rTable` cap type changes from `Nat` to `Units`: the RULE_RTABLE type of `src/front/check.c`, the 4 rTable rows of test/json, examples/claims.lang and 4.1 (R). | USER, S5 (AskUserQuestion) |
| 2026-11-03 | Q-S5-6, verbatim: "keyOpenC on Covenant (Recommended)". `Point` is the x coordinate only (F5), so `Covenant` gets the field `keyOpenC : CPoint`, the full point of `keyOpen` (the S4 extension form). The Polity projections stay. `makeCovenant` gets one more argument, so the S4 examples and rows that call it change. | USER, S5 (AskUserQuestion) |
| 2026-11-03 | Q-S5-7, verbatim: "1024". Not the recommendation, (b): `maxHolders` = 1024, the N8 pair cap number (about 3.3 MB static at about 101 words for each Holding). The S8 N8 measure must take it. | USER, S5 (AskUserQuestion) |
| 2026-11-03 | Q-S5-8, verbatim: "Issue in S5, Gov in S6 (Recommended)". O6: S5 builds `issue : Measure -> Issue -> Except Measure` and its proof: `pub` under `keyOpenC` and `own` under `key id` open to the public amount (`sig_rand` and a DLEQ). `supply + m` gives `overflow` at `2^32`. The authority is an argument, and S6 binds it to `Gov`. | USER, S5 (AskUserQuestion) |
| 2026-11-03 | Q-S5-9, verbatim: "FreeMon sub-bag (Recommended)". The C6 lookup by key: fmChain into B needs `isSet B`, and the kit has no set proof for `Option Holding` (Identity holds two Lists, Ct is a family). `holderAt : Measure -> CPoint -> FreeMon Holding` gives the holdings whose `idKey` is `cpointEq` to the key (fmChain into FreeMon Holding, a set by fmIsSet); the empty bag means no holder. An update uses fmMap (no set proof). | USER, S5 BUILD1 (AskUserQuestion) |
| 2026-11-03 | Q-S5-10, (a). `Covenant` gets a rows field: `def Rows : Type 0 := List (Prod (Prod Claims Claims) Units)` and `Covenant := Prod Polity (Prod (FreeMon CPoint) (Prod CPoint Rows))`. `makeCovenant` takes the rows as its 4th argument, and `covRows` gives them. The check refuses the name of a later declaration, so the Claims block and `family Units` move up in domain/domain.lang, before the CPoint section. | USER, S5 BUILD3 (AskUserQuestion) |

The seed is `bin/new-lang.sh tranche-lang tcc-wasm` from lang-template
5265a2a. Native code and EVM are not targets. EVM goes because the request
asks for an off-chain version of interest-lang.

## 2. The reading of R0: no run-time error, period

R0: "the language should not allow any runtime errors, period."

This is wider than the accrue-lang R0 (no trap, N1 to N5,
`REFUSE_PARTIAL`, `test/notraptool.c`). The reading: a checked program
gets to a value on each input, on each target. Three things make this
true. Each operation is total. The resources of each run have a static
bound. Each failure that the domain knows is a typed value. A program that
cannot meet R0 is refused at check time, with a fix hint.

| Rule | Statement | Where |
|---|---|---|
| N1 | Each partial operation has an `Option` or `Except Fail` result, or a refinement argument. `natSub`, `u256Sub`, `u256Add`, `u256Mul` give `none` on an underflow or an overflow (the accrue-lang S3 rulings). Division takes a `NonZero` divisor. `mulDiv a b d` = `floor(a * b / d)` with a 512-bit product. | kernel types, eval, each writer |
| N2 | No recursion at run time, no run-time closure, no run-time tree fold (the kit HOST-LIMITs). So the stack depth has a static bound. | kit, `REFUSE_REC` |
| N3 | Each run-time list has a capacity that the program fixes (Q-S0-9). An operation that would pass the capacity gives `full`. Memory is static: no `memory.grow`, so each party runs in the same memory and gets the same result. | lowering, Wasm writer |
| N4 | The writers emit no trap form. Wasm: no `unreachable`, each `div` and `rem` has a divisor that the type proves is not zero, each load and store is inside the static memory. ES: no `throw`, no BigInt division by zero. WGSL: no division by zero. | writers, `test/notrap/` |
| N5 | `langc eval` never aborts on a checked program. A `Nat` above 2^64 - 1 has an `Option` form or the program is refused at check time (cairn-lang S8 shows this kit trap). | eval |
| N6 | Each failure that the domain knows is a value of the closed sum `Fail`: `badProof`, `badSig`, `badAttest`, `refusedPair`, `overCap`, `overBalance`, `noQuorum`, `dropped`, `outOfRange`, `full`, `overflow`, `replay`. A failed verification, a missing quorum and a dropped MPC party are values, not stops. The claims and the balances are private (Q-S0-5 (c), Q-S0-6 (b)), so the ledger gives `badProof` for a pair, a cap or a balance that a proof does not meet. The holder program gives `refusedPair`, `overCap` or `overBalance` before it makes the proof. | prelude, Part H, Part G, Part P |
| N7 | Exponential ElGamal decryption is total only in a static range. `decryptIn : HolderKey -> Ct -> Except Units` (Q-S5-4 (a); the domain `Except` fixes `Fail`) searches `[0, 2^B)` with a fixed number of steps (baby step, giant step, a static table). A plaintext outside the range gives `outOfRange`. No decryption loops until it finds a value. Q-S0-4 picks `B`. | Part H, eval, each writer |
| N8 | Static bounds. The checker gives each entry point an upper bound on its operations, its stack depth and its memory, from the IR: no loop, and each fold is over a list with a capacity. `langc check --json` reports the three bounds. A program with no bound is refused. | checker, `check --json` |

`Except Fail A` is a cell monad (section 3): one generator `eta`, one cell
`fail : Fail -> T A` with closed base data. `Option A` is `Except Unit A`.
So N6 adds no new kind of type constructor. The kernel words that return
`Option` now stay as they are; the new domain words return `Except Fail`
(O9).

Refusal codes: `REFUSE_PARTIAL` (from accrue-lang: a partial kit word
without its total form; the fix hint names the total form) and
`REFUSE_UNBOUNDED` (N8: no static bound; the fix hint names the list that
needs a capacity).

Gate for R0: a C tool `test/notraptool.c` reads each built module and
fails on each trap opcode and each unguarded `div` or `rem` (Wasm), and on
each `throw` (ES). Each target runs the edge rows (zero divisor, 2^256 - 1,
a full list, an empty list, a plaintext at `2^B - 1` and at `2^B`, a
quorum of `t - 1`, one dropped party) and must give the eval value. The
Wasm runs also report the stack depth and the memory used, and the gate
fails when a value is above the N8 bound.

## 3. The reading of R1

R1: "for type constructors/higher-kinded types, admit only cell monads and
their duals as representated computationally in a Cubical Type Theory."

cairn-lang and accrue-lang have the same R1 and the same reading, from
intent-lang. tranche-lang takes it without change:

- The ambient structure is not limited: Pi, Sigma, `Path`, `PathP`, the
  interval `I`, `transp`, `hcomp`, the universe `U`, `Glue` and `ua`.
- A declared type constructor must be one of two classes:
  - CM, cell monad: the free algebra monad of a cell signature. The
    parameter occurs only in the generator `eta : A -> T A`. Each other
    cell takes closed base data and arguments of type `T A`. Unit is `eta`,
    bind is the recursor, and the laws are paths by induction.
  - CC, cocell comonad: the cofree coalgebra comonad of a cocell
    signature. One observation `eps : D A -> A`, each other field `D A ->
    D A` or `D A -> E -> D A`. Counit is `eps`, extend is the corecursor.
- `REFUSE_CELL` refuses each other parameterized declaration. The cons
  list is refused. The free monoid `FreeMon` is the admitted list.

tranche-lang uses the duality in two places:

- The ledger is a CM (`FreeMon Entry`, a SHA-256 hash chain, cairn-lang
  Part R). The measure `mu`, the claim balances and the treasury are its
  fold.
- The state at a position is its dual: the cofree comonad of `Entry -> -`
  (a CC), with `eps` = the state now and `step` = the state after the next
  entry. The pairing of the two runs the ledger on a state. Law L7 (a
  claim travels with the units) is a path about this pairing.

`Enc` (the sealed identity CM), `Except Fail` (N6) and the `squash` cell
of `ZKProof` (4.5) are CMs too.

## 4. The minimal base signature (R2)

R2: admit the minimum to express interest-lang "as an off-chain ZK+FHE+MPC
version governed by a ZK+FHE+MPC off-chain threshold-authorized,
self-amending system with a base like" orbit-lang.

Each name has one of three kinds, as in cairn-lang and accrue-lang:

| Kind | Meaning |
|---|---|
| slot | The program gives the type from formers and kit base data. |
| defined | The prelude defines it from slots, formers and kit base data. |
| primitive | Opaque. The kit implements it. Only the listed operations use it. |

Kit base data (all of it, O1): `Scalar` (the field of the group order),
`Point` (the group), `Digest` (32 bytes), `U256` (asset amounts). `Nat`
stays internal to the kernel. There is no `Addr`, `msg.sender`, ERC-20
`token`, block or clock: those are chain data.

interest-lang gives the meaning (DESIGN.md:56, fe3f47b): `Sigma (L :
Aggregation act F). InterestState`, with `InterestState = (mu, W, R,
treasury)`. Parts I, H and P give `InterestState`. Part G gives `L`.

### 4.1 Part I: identities and the restriction

| Name | Kind | Reading |
|---|---|---|
| Claims | defined | `(juris : Juris, status : Status)`, two closed sums that the program declares. interest-lang DESIGN.md:39: "jurisdiction, investor status, and (only at the representation layer) a wallet set". The wallet set is not here. The claims are private (Q-S0-5 (c)): the ledger holds only their commitment `com`. |
| ClaimCom | defined | `com = juris * G1 + status * G2 + r * H`, a Pedersen commitment on secp256k1. `G1`, `G2` and `H` are fixed generators from hash to curve, so no one knows a relation between them. The holder keeps the opening `(claims, r)` secret (O4). |
| Identity | defined | `(key : CPoint, com : CPoint, ring : List CPoint, att : List U256)` (Q-S4-5 (a)). `ring` is a copy of `issuers` (4.3) when the issuer attests, with the capacity `maxIssuers` (N3). `att` is a ring signature on `(key, com)` over `ring` with no key image (the cairn-lang bLSAG code without the link; Q-S4-1 (a): the issuer ring, so the ledger cannot see which issuer signed). So `att` is a zero-knowledge proof that one key in `ring` signed, and it does not tell which key. The issuer sees the claims and `r` when it attests (V6). Q-S0-5 (c). |
| attested | defined | `Covenant -> Identity -> Except Identity` (Q-S4-4). `exEta` of the identity when the ring signature verifies and each key of `ring` is in `issuers`. A bad ring signature, or a key in `ring` that is not in `issuers`, gives `exFail badAttest` (O11). |
| R | slot | `Claims -> Claims -> Option Units`, a total program definition. `none` refuses the pair. `some cap` admits the pair with a bound. The compiler makes the table of `R` over the closed sums `Juris` and `Status`, so the table has a static number of rows (N8). The transfer proof shows, with the claims hidden, that `com h` and `com k` open to a row that `R` admits and that `q <= cap` of that row (4.2). The kit word `rTable R` gives the table (Q-S4-3, Q-S4-7): the type `List (Prod (Prod C C) Units)` with `C = Prod Juris Status` (Q-S5-5 (b); S4 wrote Nat), the admitted rows `pair (pair h k) cap` in constructor order. More than 28 rows or more than 1024 pairs is `REFUSE_UNBOUNDED`. interest-lang DESIGN.md:50-53: R "depends only on identity claims" and is "the image of securities compliance inside the model". Q-S0-5 (c). |

interest-lang DESIGN.md:111 reads ERC-3643: "R reads claim topics from
trusted issuers", and `canTransfer` decides R. Here `R` with `attested` is
that decision. DESIGN.md:113: "The trusted-issuer set is part of the
constitution and is amendable only through Gov".

Who sees the claims (Q-S0-5 (c), Q-S4-2 (b)): only the issuer that
attests. The two parties of a transfer make the R row proof together (a
joint proof, O10), and no party sees the claims of the other: the
receiver does not give the opening of `com k` to the sender. The
ledger, the committees and the other holders see only `com`
and `ring`. They do not see the claims or the issuer that signed (O11).

### 4.2 Part H: the encrypted holdings and the transfer

| Name | Kind | Reading |
|---|---|---|
| Units | defined | `family Units := unitsOf (unitsVal : Nat)` (Q-S5-5 (b)), a `Nat` below `2^B` (N7, Q-S0-4, B = 32). The checked constructor `toUnits : Nat -> Except Units` gives `outOfRange` at `2^32` and above. The measure is in units, not in `Q>=0`: interest-lang DESIGN.md:36 has `mu : Identity -> Q>=0`, and an integer unit is its representation. |
| Ct | defined | `family Ct := ctOf (ctC1 : CPoint) (ctC2 : CPoint)` (Q-S5-5 (b)), an exponential ElGamal ciphertext on secp256k1 (`c1 = r G`, `c2 = m G + r K`, as cairn-lang `src/front/elgamal.c`). `ctAdd`, `ctSub` and `ctScale` by a public scalar are the homomorphic operations, guest defs over the kit words `ecAdd` and `ecMul` (F4 of the S5 survey), so Ct needs no new kit base type (O1). `ctEnc : CPoint -> Units -> Nonce -> Option Ct` (Q-S5-4 (a)). |
| Holding | defined | `(id : Identity, own : Ct, pub : Ct, bal : Ct)`. `own` is `mu(h)` under `key id`; `pub` is `mu(h)` under `keyOpenC` (Q-S5-6 (a)); `bal` is the claim balance under `key id`, initialized to an encryption of zero (4.4, Q-S0-6 (b)). Q-S0-3. With Q-S0-6 (b) the committee opens no single `pub`: `pub` serves the supply audit (L2) and O7. |
| Measure | defined | `FreeMon Holding` with the capacity `maxHolders` = 1024 (N3, Q-S5-7 (b)), and public `supply : Units` and `distributed : U256`. `supply` changes only by an issue entry that the polity authorizes: `issue : Measure -> Issue -> Except Measure` (O6, Q-S5-8 (a)), with the authority as an argument that S6 binds to `Gov`. `distributed` starts at zero and records the lifetime distribution total for the balance bound (4.4, O8). |
| decryptIn | defined | `HolderKey -> Ct -> Except Units` (N7, Q-S5-4 (a); the domain `Except` fixes `Fail`): `c2 - sk c1`, then `elgLog`, and a none gives `outOfRange` (L17). The holder runs it on `own` with its secret key. `HolderKey` is a secret mark (O4), so a lowered def that names it refuses `REFUSE_TARGET_SECRET`. The committee runs it only through `open` (4.3): on a tally, and on the sum of the `pub` ciphertexts. |
| TransferProof | defined | One `ZKProof` (4.5) that is a Sigma composition of the cairn-lang proofs (`src/front/sigma.c`): (1) the ciphertexts of `q` under `key h`, `key k` and `keyOpenC` share one plaintext (DLEQ); (2) `q` is in `[0, 2^B)` (one disjunctive Chaum-Pedersen bit proof for each bit); (3) `mu(h) - q` is in `[0, 2^B)` (the same bit proofs on a fresh ciphertext, and a proof with the secret key of `h` that it equals `own h - q`); (4) the joint R row proof (4.1, Q-S0-5 (c), Q-S4-2 (b), Q-S5-1 (a)): the transcript `Q_J`, `G`, `F`, `Y`, `d_k`, `d_h` of the three messages M1 (sender), M2 (receiver) and M3 (sender) under the joint key `J` (Q-S5-2 (a)), with their five sub-proofs. It shows, with the claims hidden from the ledger and from the other party, that `com h` and `com k` open to a row that `R` admits (a static number of rows, N8); (5) `q <= cap` of that row: the threshold bits `G_j` (Q-S5-3 (a)), and the selected bit `Y` opens to 1. The kit proof check `transferVerify` (all of TransferProof) is HOST-LIMIT until S8 (the `attestVerify` form). A refused pair or `q > cap` stops the protocol of the two parties before a Transfer exists, so `refusedPair` and `overCap` are values of the party side (the prover words `jrSend`, `jrSelect` and `jrOpen`), and `transfer` sees only `badProof`. |
| Transfer | defined | `(from, to : Identity, q : three Ct, proof : TransferProof, sig)`. `sig` is a Schnorr signature of `key from` on the digest. |
| transfer | defined | `Covenant -> Measure -> Transfer -> Except Measure` (the domain `Except` fixes `Fail`). interest-lang DESIGN.md:65: tau(h, k, q) "is defined iff R(h, k, q) and q <= mu(h)", and then it subtracts `q` from `h` and adds `q` to `k`. Here: `attested` for both, the proof (with the R row of the hidden claims, Q-S0-5 (c)), the signature, then `own h - q`, `pub h - q`, `own k + q`, `pub k + q` by the homomorphic operations. `bal` does not change. Each check that fails gives its `Fail` value. |

Conservation holds by the group law: the same ciphertext of `q` leaves
`pub h` and enters `pub k`, so the sum of the `pub` ciphertexts does not
change (L2). No proof is needed for it.

A transfer enters the ledger only with a FROST threshold Schnorr
signature of the append committee under `keyAppend` on `(head r, e)` (the
cairn-lang `Auth`, `src/front/frost.c`). The ledger refuses a second entry
with the same digest (`replay`).

ERC-1644 forced recovery (DESIGN.md:65) is not a holder transfer. It is an
entry that only `Gov` can authorize, and it does not quotient through the
voter orbit. It waits for a slice (O7).

### 4.3 Part G: the polity (orbit-lang and cairn-lang)

| Name | Kind | Reading |
|---|---|---|
| Member | defined | `Point`: one public key. Members vote. Holders do not vote by their units (Q-S0-7). |
| Covenant | defined record | Not the kernel `Constitution` of IsSelfConstituting (Q-S4-6). The cairn-lang `Polity` (cairn-lang docs/PLAN.md:170, 60768e8) plus `issuers : FreeMon CPoint` (Q-S4-5 (a)), `keyOpenC : CPoint` (Q-S5-6 (a), the full point of `keyOpen`), `rows : Rows` (Q-S5-10 (a), the table of R: `makeCovenant` takes it as its 4th argument, and `covRows` gives it), `keyPay`, `tPay`, and `policy : Option D`, the policy in force. With Q-S0-8 (b), `(R, W)` changes only with a whole new constitution. The keys come from the DKG; there is no trusted dealer. |
| D | defined | `Nat` below `decisions`: the admissible policies that the program declares. `policyOf : D -> Prod R W` is a total program definition. The objects of `F` "include the admissible (R, W)" (interest-lang DESIGN.md:59). |
| Tally | defined | `Units`: the opened homomorphic sum of the yes votes on one proposal (the cairn-lang tally). This is the orbit quotient: the count, never who voted what (orbit-lang DESIGN.md:29). |
| Gov, GovPi | defined | `Gov` = `amendAll` (cairn-lang): the fold of the decided amendments in ledger order. `GovPi` = `policyOf` of the `policy` of the constitution that `Gov` gives (`none` when the `policy` is `none`). orbit-lang DESIGN.md:35: `Gov L := orbitProjection act >>> L.functor`. |
| AmendBallot | defined | The cairn-lang ballot: `Enc Bool` with its proof and its bLSAG (cairn-lang `src/front/blsag.c`), on a whole new `Covenant`. The key image of the bLSAG is the nullifier: one ballot for each member (orbit-lang law 5, DESIGN.md:112). `decide` = `yes >= tAmend`. With Q-S0-8 (b) it is the only ballot: a new policy is a new constitution. |
| Fate | defined | `impossible | debreu D`: orbit-lang law 6 (DESIGN.md:117), `Empty + (unique L) + (L0 x L1)`. `fateOf` reads the `policy` in force: `none` gives `impossible`, `some d` gives `debreu d`. Under Q-S0-8 (b) the `ising` summand has no inhabitant (section 5). |
| IsSelfConstituting | defined | orbit-lang DESIGN.md:91: `Sigma (L : Aggregation), Path Constitution (gov L) F`. A program gives the witness as a path; the kernel checks it (L13). |
| Share, open | primitive | Partial decryption with a DLEQ proof; `tOpen` valid shares combined by Lagrange (MPC, cairn-lang `src/front/threshold.c`). `open` gives `noQuorum` below `tOpen` valid shares, and it names each dropped party in its result (N6). |
| Deal, Reshare | primitive | cairn-lang `src/front/dkg.c`: Pedersen DKG with Feldman commitments, and reshare when `members` or a threshold changes. |

Self-reference: an amendment is a whole `Covenant`, so the polity can
amend `tAmend`, `issuers` and `policy`. Each ballot is decided by the
constitution in force when it opens. A chain of amendments is a fold over
`FreeMon Covenant` (the cairn-lang `amendAll`). interest-lang
DESIGN.md:81: amendment "does not move mu or the treasury" (L8).
DESIGN.md:83: compliance labels and voter anonymity are different actions,
so a ballot never carries the claims of an identity.

### 4.4 Part P: distribution and payment

| Name | Kind | Reading |
|---|---|---|
| Kind | defined | `rent | sale`. |
| W | slot | `Kind -> U256 -> U256` from the policy in force, with `W k a <= a` (a law on the slot). interest-lang DESIGN.md:47: W "selects which inflow is distributable". |
| Inflow | defined | `(kind : Kind, amount : U256)`, a ledger entry that the polity authorizes. |
| claim | defined | With Q-S0-6 (b) each claim is an encrypted balance. `u = mulDiv A (2^P) supply` when `supply > 0`, else `0`: the public credit for each unit, in units of `2^-P` of the asset (O8). The claim of `h` is `mu(h) * u`, and the ledger adds it to `bal h` as `scale u (own h)` by the homomorphic operations: no proof and no open. interest-lang DESIGN.md:42 has the claim `mu(h) / supply * A`; `mu(h) * u / 2^P` is below it by less than `mu(h) / 2^P` base units. |
| distribute | defined | `Fate -> Measure -> Inflow -> Except Fail (Prod Measure U256)`. Under `impossible` it credits nothing, and the inflow stays in the treasury (DESIGN.md:73: "funds stay in the treasury, neither burned nor silently paid"). Under `debreu d` it takes `A = W k a`. If `supply = 0`, it credits nothing and retains all of the inflow, with `u = 0` and `res A = A * 2^P`. Otherwise it checks the public lifetime total `distributed + A < 2^(Bbal - P)` before computing `u` or changing any balance, then adds `claim` to each `bal h`, increments `distributed` by `A`, and keeps the residual. Equality with the limit or an overflow gives `full` and leaves the state unchanged (O8). It opens nothing. |
| Withdraw | defined | `(id : Identity, w : U256, proof : WithdrawProof, sig)`. `WithdrawProof`: with the secret key of `id`, the bit proofs (4.2) that `bal h - w * 2^P` is in `[0, 2^Bbal)`. `sig` is a Schnorr signature of `key id` on the digest. |
| withdraw | defined | `Covenant -> Measure -> Withdraw -> Except Fail (Prod Measure Instruction)`: first check the public integer bound `w < 2^(Bbal - P)` (`outOfRange` otherwise), then the proof against the current `bal h` and the signature, then subtract `w * 2^P` and give an `Instruction` to `key id` for `w`. The bound is checked before conversion to `Scalar`; no reduction modulo the group order can establish it. Together with the balance bound and `2^(Bbal + 1) < n` (O8), the residual range proof establishes integer non-underflow. The holder program gives `overBalance` before it makes the proof when `w * 2^P` is above its balance. |
| Instruction | defined | `(payee : Point, asset : Digest, amount : U256, pos : OpPos)`: the output of `withdraw`. |
| PayAuth, paySnoc | defined | The accrue-lang S7 form, ported: `family PayAuth := payAuthOf (payR : U256) (payZ : U256)`, `payOk` checks `schnorrOk` under `keyPay` on `(ixHead log, ixDigest p)`, and `paySnoc` adds an entry only with a valid `PayAuth` and a digest that is not in the log (accrue-lang domain/domain.lang:606-613, d1017f1). |
| Treasury | defined | The fold of the ledger, in units of `2^-P` (O8): each inflow adds `a * 2^P`, each distribution takes `u * supply`. It keeps `a - W k a` and the residual `res A`. A withdrawal takes from `bal h`, not from the treasury. |

The sum law with fixed point. interest-lang DESIGN.md:70 states
`Sum_h claim(h, W(kind)(a)) = W(kind)(a)` over `Q>=0`. Here each unit gets
`u = floor(A * 2^P / supply)`, and `Sum_h mu(h) = supply`, so the law is:

`Sum_h mu(h) * u + res A = A * 2^P`.

When `supply > 0`, `0 <= res A < supply`. When `supply = 0`, `u = 0`
and `res A = A * 2^P`: the whole inflow stays in the treasury. There is
no residual bound by `supply` in that case.

`u` and `supply` are public, so the ledger computes `res A` in public. The
plaintext of the sum of the credits is `u * supply` by the group law, so
no proof is needed. With `supply > 0` and `P >= B` (O8), `res A` is below
one base unit of the asset. `res A` goes to the treasury (L6). A withdrawal pays from the
balance. accrue-lang has the same shape for a settlement: it pays a
residual (`withdrawableAt` = recognized minus withdrawn,
domain/domain.lang:476-478, d1017f1) and reads what was paid from the
record (`settledIn`, domain/domain.lang:614-623; `escSettleR`, :638), so a
replay pays zero.

The protocol holds no money. A rail outside the language pays each
`Instruction` (one for each withdrawal) that has a valid `PayAuth`.

Privacy (Q-S0-6 (b)): `distribute` opens nothing. `mu(h)`, each `q` and
each balance stay encrypted under the key of the holder, and no committee
sees them. The holder knows its balance as a plain fold (each `u` is
public, and it decrypts `own h` with `decryptIn`), so it never searches
for the plaintext of `bal h`. The rail sees each withdrawal: the payee and
`w`. A holder can split and delay its withdrawals, but the rail learns the
sum that each payee takes out (O8).

### 4.5 Enc, ZK and MPC

The readings of cairn-lang PLAN 3.3 hold: `Enc` is the identity cell monad
behind a sealed eliminator, `bind` is homomorphic evaluation (affine maps
only, for exponential ElGamal), `decrypt` is a law-only monad morphism
`Enc -> Id`, `open q c = decrypt c` is the MPC law, and `ZKProof W := || W
||` (the `squash` cell) is zero knowledge. IND-CPA and proof soundness are
VALIDATION assumptions. tranche-lang adds N7: the `decrypt` of the law is
`decryptIn` in the range, and the law holds for each plaintext below
`2^B` (L17).

orbit-lang law 5 (DESIGN.md:107-110) asks for a Glue equivalence
`Ballot_priv ~ Choice x ZKWitness` with a path that is constant on the
private data. The reading: the `squash` cell gives it. Each map out of
`|| W ||` into a set is constant, so a verified witness gives no private
data.

## 5. Time and the three fates

Time: interest-lang has no clock. A distribution, a transfer and an
amendment are ledger entries with positions. The reading (Q-S0-2 (a)):
keep the cairn-lang S3 result, `REFUSE_TIME` and `OpPos` (cairn-lang
docs/PLAN.md:247, 60768e8). Each program reads order only as an `OpPos`.

The three fates (interest-lang DESIGN.md:99-103, orbit-lang law 6):

- `impossible`: the constitution in force has `policy = none`.
  `distribute` credits nothing; the inflow stays in the treasury; the
  claims stay as a measure. The verdict words give `none`.
  `IsSelfConstituting` has no witness (orbit-lang DESIGN.md:122).
- `debreu d`: `policy = some d`; `GovPi` gives the policy `policyOf d`.
- `ising d0 d1`: two choices that are equally legitimate. interest-lang
  DESIGN.md:103 says this is "a fork of aggregation objects, not by itself
  a fork of IsSelfConstituting". The reading (Q-S0-8 (b), as cairn-lang):
  no `ising`. Each AmendBallot tally has one verdict (`yes >= tAmend`),
  and `amendAll` folds the passed amendments in ledger order (L14), so
  one constitution is in force at each position. When two amendments
  pass, the later one in ledger order is in force. There is no `choice`
  slot, so there is no `REFUSE_BIFURCATION`. So a checked program gets
  only `impossible` or `debreu`.

interest-lang DESIGN.md:93-95: for the constant constitution the witness
is constructive through `zeroPhaseAggregation`, and it is single-valued.
tranche-lang gives that witness as a guest path (L13).

## 6. Laws

Each law is a path that the kernel checks. "VALIDATION" marks a
computational assumption that no path can prove (docs/VALIDATION.md).

| Law | Statement |
|---|---|
| L1 zero | `transfer` with `q = 0` leaves the plaintext measure unchanged (DESIGN.md:65) |
| L2 conservation | the sum of the `pub` ciphertexts is the same after a transfer; `supply` is the same |
| L3 chains | two admissible transfers in sequence give the same measure as their composite, when it is admissible |
| L4 disjoint | two transfers on disjoint identities commute |
| L5 R gate | `transfer` is `some` only when both are `attested` and the joint R row proof holds (Q-S4-2); by its soundness (V3) `R` admits the hidden pair and `q <= cap`. A success while R fails is a leak (DESIGN.md:65). Q-S0-5 (c) |
| L6 sum with residual | `Sum_h mu(h) * u + res A = A * 2^P`; if `supply > 0`, `res A < supply`; if `supply = 0`, `u = 0` and `res A = A * 2^P` (4.4) |
| L7 claims travel | a transfer, then `distribute`, gives the same credits as `distribute` on the image measure (DESIGN.md:73); a transfer moves no balance |
| L8 amend | an amendment leaves `mu`, the balances and the treasury (DESIGN.md:81) |
| L9 tally | `open q (tallyEnc bs) = count (map decrypt bs)`: the yes count of the AmendBallots on one proposal |
| L10 MPC | `open q c = decrypt c` |
| L11 orbit | the tally of a relabeled ballot profile is the same tally (orbit-lang law 1, DESIGN.md:75-80) |
| L12 legitimacy | `aggregate` factors through the orbit quotient, with the unit path (orbit-lang law 2, DESIGN.md:82-87) |
| L13 self-constituting | `GovPi` is `policyOf` of the `policy` in force; at a constant constitution, the path `gov (aggregate F) = F` (orbit-lang law 3, DESIGN.md:89-94) |
| L14 amend fold | `amend (ps ++ qs) = amend qs . amend ps`, and `amend = Gov` (orbit-lang law 4, DESIGN.md:96-100) |
| L15 one ballot | the ledger refuses a second ballot with the same key image (orbit-lang law 5, DESIGN.md:112) |
| L16 fates | `impossible` credits nothing and keeps the treasury; no `IsSelfConstituting` witness at `impossible` (orbit-lang law 6, DESIGN.md:122) |
| L17 range | `decryptIn (enc m) = m` for `m < 2^B`, and `outOfRange` for `m >= 2^B` (N7) |
| L18 withdraw | `withdraw` checks `w < 2^(Bbal - P)` before scalar conversion, takes `w * 2^P` from `bal h` and gives one `Instruction` for `w`; the sum of the balances plus `2^P` times the sum of the withdrawals is the sum of the credits as integers, without reduction modulo `n` (Q-S0-6 (b)) |
| V1 to V6 | VALIDATION: collision resistance of SHA-256; IND-CPA of ElGamal (DDH); soundness and zero knowledge of the Sigma proofs, DLEQ, bLSAG and the issuer ring signature in the ROM, and the binding of the claim commitments (DL); unforgeability of FROST and Schnorr; the honesty of `tOpen` members (they open only a tally or the sum of the `pub` ciphertexts); the issuers attest true claims |

## 7. Route

### 7.1 The cairn-lang port

cairn-lang is the governance half. It has the same kit (tcc-wasm) and the
same lang-template (5265a2a) as this seed: its seed is e222eba, and its
newest USER commit is 60768e8 (S13). So the port is a diff, not a copy:
S2 applies `git -C cairn-lang diff e222eba 60768e8` to this seed with the
name changed (13 commits, cairn-lang S1 to S13; 472 files, 30752
insertions, 1139 deletions, counted 2026-11-01), then runs `make -B check`, because a
patch sets new mtimes only on the files it touches. Survey first: SAME,
CHANGED, CAIRN-ONLY.

The port brings: the Cubical TT checker and evaluator, `REFUSE_CELL`,
`FreeMon`, `Enc`, `group.h` on secp256k1, SHA-256, the Sigma, ElGamal,
DLEQ, threshold, bLSAG, FROST and DKG code, Part D and Part R, the Wasm,
ES and WGSL writers (cairn-lang S8 to S10), `langc fmt` (S11), `langc
check --json` (S12), `langc mcp` and `langc explain` (S13). Keep
`REFUSE_TIME` and `OpPos` (Q-S0-2 (a)). Do not take `.kanon-exec/`,
`.kanon-wait/`, `.kanon-replies/`, `skills/` or `examples/skill/` (they
are not tracked in cairn-lang).

The cairn-lang domain (`domain/domain.lang`) stays as the governance
part. S4 to S7 add Parts I, H, P and the new Part G words to it as guest
code.

### 7.2 The three targets

One front end (the ported kernel) and three writers that read its output:

| Target | Source | Crypto at run time | Use |
|---|---|---|---|
| Wasm MVP (primary) | cairn-lang `src/wasm.c` and `src/front/lower.c` (S8) | the cairn-lang Wasm prelude (i64 limbs, point add and multiply, SHA-256), plus the N7 table | each party: holders, issuers, members, the open, append and pay committees |
| ES module + JSON | cairn-lang `src/js.c` (S9, 6b73467: "Write ES modules and the JSON ABI from the IR") | BigInt in a JS prelude | public checks of the ledger and of the proofs. JS is not constant time, so a program that touches a secret key, a key share or a nonce is refused here (`REFUSE_TARGET_SECRET`, cairn-lang S9). |
| WGSL compute | cairn-lang `src/wgsl.c` (S10) | u32 limbs | batch work: check many transfer proofs and ballot proofs, the credits of many holders |

Differential tests: each target runs the same vectors as `langc eval`.
The kit harnesses stay. No new node, Python or shell harness: new tests
are guest `.lang` files and C tools.

## 8. Agent adoption (A1 to A6)

The request asks for six items so that agents reach for tranche-lang when
it fits. cairn-lang built the same six items. The port (S2) brings A1, A2
and A4. Later slices extend them to the new words and codes.

| Item | Deliverable | Gate | cairn-lang slice |
|---|---|---|---|
| A1 | One canonical form for each operation: no aliases, no second syntax. `langc fmt FILE` prints the canonical form. `langc fmt --check` exits 1 when a file is not canonical. | `fmt` is idempotent; `parse (fmt x)` has the same core term as `x`; each example and test file is canonical. | S11, 3717b81 (ported at S2) |
| A2 | `langc check --json FILE`: one JSON object with `diagnostics` (code, span, message, `fix`: a concrete edit) and `progress` (`parsed`, `typed`, `laws_discharged`, `laws_total`), plus the N8 bounds. Schema in `docs/CHECK-JSON.md`. | Rows in `test/json/` with the expected JSON, byte equal. Each refusal code has a `fix` row. | S12, 98d18ef (ported at S2) |
| A3 | `SKILL.md` with five graded examples (G1 to G5). Each example is a file in `examples/skill/`, and SKILL.md quotes it. | The gate checks each example and checks that the SKILL.md block and the file are byte equal. | S14, not committed (in progress at 60768e8) |
| A4 | `langc mcp`: an MCP server over stdio (JSON-RPC 2.0, one message on each line), in C, built by TinyCC into the same `langc`. Tools: `check`, `fmt`, `eval`, `build` (with a target), `explain` (a refusal code). The MCP spec version is pinned in `docs/MCP.md`. | A C tool `test/mcptool.c` sends `test/mcp/*.in` and compares the replies with `*.out`. | S13, 60768e8 (ported at S2) |
| A5 | `AGENTS.md` (how an agent works in this repo, with the gate command) and a README table: when to use tranche-lang, when not to use it, and what to use then. | Review only. | S14, not committed |
| A6 | Token counts: G1 to G5 in tranche-lang, Python and Rust. Report `docs/TOKEN-BASELINE.md` with the tokenizer pin, each count and each file hash (Q-S0-10). | `make tokens`, not in `make check`. | S15, not started |

Graded examples:

| Grade | Example | Uses |
|---|---|---|
| G1 | One property, one holder (`supply = 1`, a Dirac measure): one rent inflow, the whole claim credited to that identity, then one withdrawal | Part I, Part P, one pay key, threshold 1 (interest-lang DESIGN.md:109) |
| G2 | A confidential transfer between two attested identities with private claims, with `R` that admits the hidden pair with a cap | `transfer`, `TransferProof` with the R row proof, L2, L5 |
| G3 | A rent distribution over three holders, with a residual to the treasury, then a withdrawal | `distribute`, `claim`, `withdraw`, L6, L18, PayAuth |
| G4 | An encrypted amendment vote on a new constitution whose policy turns on the distribution of sale proceeds | AmendBallot, tally, `decide`, `GovPi`, L8, L11 |
| G5 | No policy in force (`policy = none`): the inflow stays in the treasury; then the polity amends `tAmend`, `issuers` and `policy` | `impossible`, L16, `amend` as a fold, L14 |

## 9. Tests

The kit layout stays: `test/parse`, `test/check`, `test/eval`, `test/ir`,
`test/fronttool.c`, `test/gate.sh`. The port adds the cairn-lang tools
(SHA-256, secp256k1, ElGamal, threshold, Sigma, bLSAG, FROST, DKG, fmt,
json, mcp) and their rows. New: `test/notrap/` and `test/notraptool.c`
(R0, N4), the N8 bound rows, the range rows (N7), the transfer proof rows
(valid, and one row for each `Fail` value of `transfer`), the R row proof
rows (Q-S0-5 (c)), the distribution rows (L6, L7, L16), the withdrawal
rows (L18; `overBalance`, `outOfRange`, `replay`). Regression rows for S7:
with `P = 32`, `Bbal = 160` and group order `n`, an empty balance and
`w = floor(n / 2^32)` must give `outOfRange` before checking a proof (the
unbounded modular subtraction instead has residual `n mod 2^32`, which
is in range); `distributed = 2^128 - 1` followed by `A = 1` must give
`full` with no state change; and `supply = 0`, `A = 1` must credit nothing,
retain the entire inflow, and satisfy L6 with `res A = 2^32`. Include
ordinary valid withdrawals and positive-supply rounding rows.
Mutation: each slice that adds a refusal or a law
kills at least one mutant of it.

## 10. Slices

| Slice | Work | Status |
|---|---|---|
| S0 | Seed from tcc-wasm, DESIGN.md verbatim, this plan; gate; seed commit; private repo | DONE |
| S1 | The USER rules section 12. Record the rulings here. | DONE: section 1 (Q-S0-5 (c), Q-S0-6 (b), Q-S0-8 (b), each other question (a)); sections 2 to 11 read them |
| S2 | Port the cairn-lang diff e222eba..60768e8 (7.1). Survey first: SAME, CHANGED, CAIRN-ONLY. | DONE: survey 52 SAME, 2 CHANGED (README.md and this file, kept), 418 CAIRN-ONLY; `git apply` of 470 files with no reject. The name change: 27 self-name sites (the MCP server name and tool texts, 9 `test/mcp` replies, 7 comments, the `REFUSE_NAME` fix `trancheHead`); 16 citations of cairn-lang slices and the 11 tag comments stay (S2-Q1 (a)). `make -B check` EXIT 0; the 56 count lines equal cairn-lang S13. |
| S3 | R0: N1, N5 to N8 in the kernel and eval; `Except Fail`; `REFUSE_PARTIAL`, `REFUSE_UNBOUNDED`; checked `U256`, `mulDiv`. | DONE: R1 the kernel and the eval (N1, N5, N6, `REFUSE_PARTIAL`, HistBind); R2 the entry ABI with no trap (a status out-param, Q-S3-22, Q-S3-23); R3 `elg_log` with a static table (N7, Q-S3-27), `upTo K` loops, N8 `bounds` and `fails` in `check --json` with `REFUSE_UNBOUNDED` (Q-S3-32 to Q-S3-36), mcp `check` = `check --json` (Q-S3-37). `make check` EXIT 0: examples 59, evals 275, json rows 28, json refusal codes 19 of 19, json fixes 24. |
| S4 | Part I: Claims, ClaimCom, Identity with the issuer ring signature, `attested`, `issuers`, the slot `R` and its table; L5 (O10, O11). | DONE: C1 the ring signature with no key image (`src/front/ringsig.c`, Q-S4-1 (a)); C2 the claim commitment (`src/front/claimcom.c`); C3 and C4 the kit words `claimCom` and `attestSign` (secret, `REFUSE_TARGET_SECRET`) and `attestVerify` (`REFUSE_LOWER` until S8); C5 `Covenant`, `Identity`, `ringIn`, `attested`, the families `Juris` and `Status` (Q-S4-4 to Q-S4-6, Q-S4-8); C6 `rTable` and the `tables` key of `check --json` (Q-S4-3, Q-S4-7); C7 `examples/claims.lang`; the def rule of `REFUSE_TARGET_SECRET` (Q-S4-9 (a)); C8 5 of 5 mutants KILLED. O10 and O11 closed. `make check` EXIT 0: examples 60, evals 295, ir 55 lowered, json rows 33, json refusal codes 19 of 19, json fixes 28. |
| S5 | Part H: Units, `decryptIn`, Holding, Measure, TransferProof with the joint R row proof (Q-S4-2), `transfer`; L1 to L4, L17. | DONE: C1 and C2 the families `Units` and `Ct` (Q-S5-5 (b)); C3 and C4 the secret marks `HolderKey` and `Nonce` (Q-S5-4 (a)), `ctEnc` and `decryptIn`; C5 and C6 `Holding` and `Measure` (`maxHolders` = 1024, Q-S5-7 (b); `holderAt`, Q-S5-9 (a)); C7 the joint R row protocol (`src/front/jointr.c`, Q-S5-1 (a) to Q-S5-3 (a)); C8 the transfer proof (`src/front/transfer.c`, `src/front/trwire.c`); C9 the kit words `jrSend`, `jrSelect`, `jrOpen` and `transferProve` (secret, `REFUSE_TARGET_SECRET`) and `transferVerify` (HOST-LIMIT until S8); C10 `Transfer`, `transfer` and the Covenant rows (Q-S5-10 (a)); C11 the issue (Q-S5-8 (a)); C12 the law rows L1 to L4 and L17 (`examples/transfer.lang`, `examples/trkit.lang`); C13 8 of 8 mutants KILLED (the range mutant by a row at `mu = 2^32 - 1`). `make check` EXIT 0: examples 63, evals 345, ir 56 lowered, json rows 34, json refusal codes 19 of 19, json fixes 29, transfer 12 rows, jointr 16 rows. |
| S6 | Part G: `D`, `policyOf`, the `policy` of the constitution, the AmendBallot tally, `Gov` as `amendAll`, `GovPi`, Fate, IsSelfConstituting; L9 to L16. | NEXT |
| S7 | Part P: W, Inflow, `claim`, `distribute`, the claim balances, Withdraw, `withdraw`, Instruction, PayAuth (from accrue-lang), Treasury; L6 to L8, L18 (O8). | |
| S8 | Targets: lowering of the new words on Wasm, ES and WGSL; N4, `test/notraptool.c`, the N8 measure; differential vectors. | |
| S9 | A1, A2, A4 for the new words and codes: canonical forms, `fix` rows, `explain` rows. | |
| S10 | A3 and A5: SKILL.md, G1 to G5, AGENTS.md, the README table (after the cairn-lang S14 USER commit). | |
| S11 | A6: the token comparison. | |
| S12 | Close: SPEC.md, probe/CAPABILITY.md, VALIDATION.md, final gate. | |

## 11. Open items

- O1 The kit base data is `Scalar`, `Point`, `Digest`, `U256`. If a slice
  needs more, it records why here first.
- O2 cairn-lang is in its S14 (SKILL.md, not committed). S2 ports
  60768e8 only. S10 takes the cairn-lang S14 form only after its USER
  commit.
- O3 The coproduct of tranches (interest-lang DESIGN.md:110: "coproduct
  of measures, one R per partition") waits for a slice after S7. S5
  builds one partition.
- O4 The secret marks for `REFUSE_TARGET_SECRET`: the secret key of a
  holder, the key shares, the nonces and, with Q-S0-5 (c), the claim
  opening `(claims, r)`. S4 marks the claim opening. S5 marks the holder
  key and the nonce in the types (Q-S5-4 (a)): the domain families
  `HolderKey` and `Nonce`, and the Q-S4-9 walk refuses a lowered def
  that names either. The def rule (Q-S4-9 (a)): each def that
  lowers (closed or entry) and reaches a secret op, transitive over def
  refs, refuses `REFUSE_TARGET_SECRET`. The secret ops are the
  `secret_op` set of `src/front/lower.c`; the refusal message cites them
  as O6 (the cairn-lang PLAN item).
- O5 The test vectors come from the published standards (NIST CAVP for
  SHA-256, SEC 2 for secp256k1, the FROST RFC 9591 vectors), not from an
  implementation that the gate would trust.
- O6 The issue of new units (`supply` changes) is a ledger entry that
  only `Gov` authorizes. Its proof (the new `pub` ciphertext opens to the
  issued amount) is S5 work (Q-S5-8 (a)): `issue : Measure -> Issue ->
  Except Measure` takes the authority as an argument, and S6 binds it to
  `Gov`.
- O7 ERC-1644 forced recovery (DESIGN.md:65) is out of S5. It needs a
  `Gov` authorization path that does not pass through the voter orbit.
- O8 The static ranges of the claim balances (4.4, Q-S0-6 (b)). The
  recommendation: `P = 32` (`P >= B`, so the residual of a distribution
  with positive supply is below one base unit) and `Bbal = 160` (each withdrawal
  proof has 160 bit proofs; the public `distributed` counter sums `A`
  over successful distributions with positive supply and stays strictly
  below `2^128` base units, else `distribute` gives `full`). Withdrawals
  never reduce this counter. The checker requires `B <= P < Bbal` and
  `2^(Bbal + 1) < n`, where `n` is the group order. Each debit is also
  strictly below `2^Bbal`, checked as an integer before scalar conversion
  (4.4), so a negative balance cannot wrap to an accepted residual. The
  USER rules `P` and `Bbal` before S7. The rail sees each
  withdrawal amount: that is the limit of a plain rail.
- O9 The kernel words that return `Option` stay. S3 decides which new
  words return `Except Fail`, and `check --json` shows the `Fail` value of
  each edge row.
- O10 Who makes the R row proof (4.1, Q-S0-5 (c)). The recommendation:
  the sender, after the receiver gives it the opening of `com k` off the
  ledger, so the two parties of a transfer see the claims of each other.
  The other forms: a joint proof of the two parties (no party sees the
  claims of the other; much more code), or an `R` in two parts, one for
  the sender and one for the receiver (each party proves its part; the
  cap is the smaller of the two caps). The USER rules it before S5.
  Closed: Q-S4-2 (b).
- O11 Revocation with the issuer ring (4.1, Q-S0-5 (c)). The ring hides
  the issuer that signed, so the polity cannot revoke the attestations of
  one issuer alone. When an amendment removes an issuer, each attestation
  whose `ring` has that issuer gives `badAttest`, and its holder gets a
  new attestation. The other form: a Schnorr signature by a named issuer
  on `(key, com)`. It keeps the claims private and revokes by issuer, but
  it shows the issuer. The USER rules it before S4.
  Closed: Q-S4-1 (a).
- O12 The domain tags of the port are the cairn-lang tags (S2-Q1 (a)):
  the ASCII words "cairn", "cairn-genesis", "cairn-append", "cairn-out",
  "cairn-auth", "cairn-authw" and "cairn-rho" as hex constants. A later
  slice changes them to "tranche" words with new vectors, at the
  earliest S4 (Part I adds its own tags). The MCP server version stays
  "S13" (the cairn-lang slice) until S9 sets it.
  S5 adds its own tags: "tranche-joint" (the MuSig weights of
  Q-S5-2 (a)) and the short tags "jrf", "jry", "jrg" and "jrw"
  (`src/front/jointr.c`), "transfer", "trq", "trr" and "trw"
  (`src/front/transfer.c`) and "trctx" (`src/front/trwire.c`).

## 12. Questions for the USER (S0)

Each question has a recommendation (rec). The USER ruled each question
at S1 (section 1).

- Q-S0-1 Crypto. (a) rec: keep the cairn-lang rulings: secp256k1,
  SHA-256, exponential ElGamal, FROST, Pedersen DKG with Feldman
  commitments. They come with the S2 port. (b) Pick again for
  tranche-lang.
- Q-S0-2 Time. (a) rec: no time, as cairn-lang: keep `REFUSE_TIME` and
  `OpPos`; order is the ledger position (section 5). interest-lang has no
  clock. (b) A stamp chain signed by a threshold, as accrue-lang: only
  needed for dated rules (a lockup until a date).
- Q-S0-3 Where `mu(h)` is encrypted. (a) rec: two ciphertexts for each
  holding (4.2): `own` under the holder key, so the holder can decrypt and
  prove `q <= mu(h)`, and `pub` under `keyOpen`, so the committee can open
  claims at a distribution; one DLEQ proof ties them. (b) `pub` only; the
  committee checks `q <= mu(h)` by an MPC comparison on shares: no holder
  key, but more MPC rounds and much more code. (c) Plain `mu(h)`; only the
  ballots are encrypted (as accrue-lang Q-S0-3 (a)).
- Q-S0-4 The static range `B` (N7). (a) rec: `B = 32`: `supply` below
  2^32 units, a table of 2^16 steps, 32 bit proofs for each range proof.
  (b) `B = 40`: more units, a table of 2^20 steps, about 16 times the
  table memory. (c) `B = 24`: a small table, but at most 2^24 units.
- Q-S0-5 Identities and R. (a) rec: the claims are public and signed by
  an issuer in `issuers`; `R` is a total program on two `Claims` that
  gives `none` or a cap, and the transfer proof shows `q <= cap` (4.1).
  (b) `R` reads only the claims and has no cap: less proof, no holding
  limit. (c) Private claims with a ZK proof of an issuer signature: more
  privacy, much more proof code.
- Q-S0-6 Payment. (a) rec: the protocol holds no money; each claim is an
  `Instruction` with a threshold `PayAuth` on the ledger (the accrue-lang
  S7 form), and a rail outside the language pays it. (b) Encrypted
  internal claim balances: the committee does not see `mu(h)`, but each
  withdrawal needs a range proof and the rail still sees the amount.
- Q-S0-7 Who votes. (a) rec: the members of the constitution, one ballot
  each, under a bLSAG ring (cairn-lang). The tally is a sum, so orbit-lang
  law 1 holds for each permutation of members. (b) Holders, with weight
  `mu(h)`: the weight breaks the symmetry of law 1 (interest-lang
  DESIGN.md:83 keeps labels and anonymity apart), and exponential ElGamal
  cannot multiply a vote by an encrypted weight.
- Q-S0-8 Ballots and ties. (a) rec: as accrue-lang: a one-hot
  PolicyBallot over `D` for `(R, W)`, an AmendBallot on a whole new
  constitution, and `REFUSE_BIFURCATION` for a choice rule that has a tie
  row with no tie-break (section 5). (b) AmendBallot only, as cairn-lang: `(R, W)`
  changes only with a whole new constitution. (c) As (a), but a tie keeps
  the policy in force and is not refused.
- Q-S0-9 Run-time capacity (N3). (a) rec: each run-time list has a
  capacity in the program (for example `maxHolders := 64`), and static
  memory. Each party gets the same result. (b) `memory.grow` with a
  `full` path: no capacity to pick, but two parties with different memory
  limits can get different results.
- Q-S0-10 Token comparison (A6). (a) rec: take the cairn-lang rulings:
  the Anthropic `count_tokens` endpoint for the newest Claude model (model
  id and date in the report), and the Python and Rust programs run with
  outputs equal to the tranche-lang output. (b) A pinned offline
  tokenizer, text only.
