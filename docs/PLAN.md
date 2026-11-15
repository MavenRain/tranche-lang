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
| 2026-11-05 | Q-S6-1, verbatim: "New Covenant amend (Recommended)". A new amend over the whole `Covenant` beside the cairn-lang one: `CDecided := Prod Covenant Count`, `decideC` reads `tAmend` of the Covenant in force, `stepC`, `amendC` (fmChain), and `Gov := amendC`. The cairn-lang `Proposal` and `amendAll` over `Polity` stay for `examples/polity.lang` and its l9 rows. | USER, S6 (AskUserQuestion) |
| 2026-11-05 | Q-S6-2, verbatim: "W now, R stays field (Recommended)". S6 adds `family Kind`, `W` as the record that carries its law (the map `Kind -> U256 -> U256` and the path of `W k a <= a`), `D := Nat` as an index into a program table of `W` (an amendment to an index out of the table keeps the old Covenant), and `policyOf : D -> W`. R stays the `rows` of the Covenant (S5 unchanged), and `GovPi : Covenant -> Option (Prod Rows W)` pairs `covRows` with `policyOf`. | USER, S6 (AskUserQuestion) |
| 2026-11-05 | Q-S6-3, verbatim: "Guest bLSAG check (Recommended)". The AmendBallot carries a bLSAG over the members ring on the proposal digest, and the link tag is its key image. The cast verifies it with the kernel zk ops (`src/front/eval.c:1516`) and refuses a used key image (L15). | USER, S6 (AskUserQuestion) |
| 2026-11-05 | Q-S6-4, verbatim: "Threshold sig of Gov key (Recommended)". `issue` = `issueBy` with the authority `a` = a FROST threshold Schnorr (the `authOk` form) of `keyAppend` of the Covenant that `Gov` gives at the issue position, on the issue digest. S6 binds the authority of Q-S5-8 (O6) this way. | USER, S6 (AskUserQuestion) |
| 2026-11-05 | Q-S6-5, (a), no veto. `AggOf : Fate -> Type 0` (`impossible` gives `Empty`, `debreu d` gives `Unit`), and `IsSelfConstituting c0 F := Sigma (L : FreeMon CDecided) (Prod (AggOf (fateOf F)) (Path Covenant (Gov L c0) F))`. L13: at a constant constitution with `policy = some d`, the witness is the empty chain, `unit` and `refl`. L16: at `policy = none`, a map `IsSelfConstituting c0 F -> Empty`. | S6 survey (veto default) |
| 2026-11-05 | Q-S6-6, (a), no veto. L11 and L12 are guest paths over the Word action (`OrbitN`) in an example, plus eval rows. | S6 survey (veto default) |
| 2026-11-05 | Q-S6-7, (a), no veto. O7 (ERC-1644 forced recovery) stays out of S6 and stays open. | S6 survey (veto default) |
| 2026-11-05 | Q-S6-8, (a), no veto. One new `examples/govern.lang` for G5, and refusal rows in `test/check` (a used key image, an amendment below `tAmend` keeps the Covenant, a witness at `impossible`), counted in `gate.sh`. | S6 survey (veto default) |
| 2026-11-05 | Q-S6-9, verbatim: "All, rent; out = zero (Recommended)". The program table of `W`: `policyOf 0 = wAll`, `policyOf 1 = wRent` (a sale gives `0u`), and an index out of the table gives `wZero`. `policyKnown d := natLe d 1` tests that d is in the table. | USER, S6 BUILD1 |
| 2026-11-05 | Q-S6-10, verbatim: "Rows as FreeMon (Recommended)". `Rows := FreeMon (Prod (Prod Claims Claims) Units)`, so `fmIsSet` gives `isSet Rows` for `covIsSet`. `rowsOf` gives the Rows of the List of `rTable R`. | USER, S6 BUILD1 |
| 2026-11-05 | Q-S6-11, verbatim: "Clamp by construction (Recommended)". `W := wOf (wMap : Kind -> U256 -> U256)`, and the slot reads `W` only through the clamp `wApply` (a value above a gives a), so `W k a <= a` holds by construction. The kernel does not reduce `u256Le` on an open a, so no program gives the path of the law; the law rows are eval rows on literal points (C13). No kernel change. | USER, S6 BUILD1 |
| 2026-11-05 | Q-S6-12, verbatim: "Kit reads FreeMon rows (Recommended)". The rows argument of `jrSend`, `jrSelect`, `jrOpen` and `transferVerify` is a `FreeMon (Prod (Prod C C) Units)`. The kit reads a closed Rows in order (`fmMul` the left, then the right; `fmEta` one row; `fmUnit` none), and the path constructors keep that order. `rTable` stays a List. | USER, S6 BUILD1 |
| 2026-11-05 | Q-S6-13, verbatim: "Ballot carries ring (Recommended)". The AmendBallot carries its ring as `List CPoint` (`amendRing`), and `castAmend` checks that the x of each ring key is in `members` (the `ringIn` form: `isMemberKey`, `memberRing`). | USER, S6 BUILD2 |
| 2026-11-05 | Q-S6-14, verbatim: "Fixed word 1u (Recommended)". The zkVerify choice of the bLSAG is `amendChoice` (1u), and the link tag is `hash (hash I.x I.y) d` (the zkcast form, `amendTag`). | USER, S6 BUILD2 |
| 2026-11-05 | Q-S6-15, verbatim: "Ledger-bound digest". The issue signature binds a digest of the ledger L (the issue position): `ledgerDigest L` chains `covDigest` of each Covenant and the Count of each row in ledger order. No Covenant or CDecided digest existed before: C12 writes `covDigest` (each part, with one word for no policy and a different word for some d). | USER, S6 BUILD1 |
| 2026-11-05 | Q-S6-16, verbatim: "Ledger + supply (Recommended)". A correction of Q-S6-15: its question text said that (c) also stops replay. That was wrong: L holds only amendments, and an issue adds no row to L. So m1 = `issDigest L s i` binds the supply in force too (it changes on each issue, so a replay fails), with tagIssue, the to key, m and the 2 Ct; m2 = `wordsDigest 0u` of the proof. | USER, S6 BUILD1 |
| 2026-11-06 | Q-S6-17, verbatim: "Issue rows in trkit.lang (Recommended)". The C12 issue eval rows (isGood, isBadZ, isReplay, isNoKey) go in examples/trkit.lang: trkit has cov, holdH, idOf and the ibRun form, so the rows copy no def. | USER, S6 BUILD1 |
| 2026-11-06 | Q-S6-18, verbatim: "Gov level now, Measure in S7 (Recommended)". G5 in S6 (C13): examples/govern.lang gives `distOf n k a` (the part of the inflow `a` of the kind `k` that the policy in force distributes through `GovPi`: `0u` under `impossible`) and `keepOf n k a = u256Sub a (distOf n k a)`, an `Option U256` (the part that stays in the treasury), then the amend of `covN` to `covA` (`tAmend` 2, two issuers, policy `some 0`) as a fold (L14). The `distribute` and Treasury rows on a Measure (the G5 half of Part P) are S7. No domain change. |
| 2026-11-06 | Q-S7-1, verbatim: "One 6th Covenant part (Recommended)". The Covenant gets a 6th part `pay : PayCfg`, with `family PayCfg := payCfgOf (keyPay : Point) (asset : Digest)`. `makeCovenant` gets a 6th argument (the S6 C1 form). `payOk` reads `keyPay`, and `withdraw` reads `asset` for the Instruction. | USER, S7 s1 |
| 2026-11-06 | Q-S7-2, verbatim: "Fold of distribute (Recommended)". `treasury : Fate -> Measure -> List Inflow -> Except Fail (Prod Measure U256)` folds `distribute` over the inflows (a list with a capacity, N3 and N8) and adds the U256 of each step, `(a - A) * 2^P + res A`, in units of `2^-P` (O8). The Measure gets no new part. A withdrawal takes from `bal h`, not from the treasury (4.4). | USER, S7 s1 |
| 2026-11-06 | Q-S7-3, verbatim: "Withdraw carries pos (Recommended)". Withdraw gets a 5th part `pos : OpPos`: `(id, w, proof, sig, pos)`. The Schnorr signature of `key id` signs `wdDigest`: tagWithdraw, `idKey`, `w`, the words of the current `holdBal` Ct, the proof digest and `pos`. The Instruction copies `pos`. A replay fails on the balance and in `paySnoc`. | USER, S7 s1 |
| 2026-11-06 | Q-S7-4, verbatim: "New withdraw.c (Recommended)". `src/front/withdraw.c` reuses the bit-proof code of `src/front/transfer.c`: 160 bit proofs that `bal h - w * 2^P` is in `[0, 2^Bbal)` (O8). The kit words are `withdrawProve` (secret, `REFUSE_TARGET_SECRET`) and `withdrawVerify` (HOST-LIMIT until S8), the S5 C9 form. | USER, S7 s1 |
| 2026-11-06 | Q-S7-5, verbatim: "Minimal log (Recommended)". `PayLog = FreeMon Instruction`, with `tagPay`, `ixDigest` of an Instruction and `ixHead` of the log; `payOk` and `paySnoc` as accrue-lang domain/domain.lang:665-677. No `PayKind`: tranche-lang has no cancel and no dispute. | USER, S7 s1 |
| 2026-11-06 | Q-S7-6, verbatim: "New pay.lang + govern (Recommended)". The rows of G1, L6, L7, L8, L18, section 9 and PayAuth go in a new `examples/pay.lang`. The G5 rows of `distribute` and Treasury at `impossible` go in `examples/govern.lang` (Q-S6-18 (a)). | USER, S7 s1 |
| 2026-11-06 | Q-S7-7, verbatim: "Prod Point Digest (Recommended)". A family has no isSet word (the kernel has u256IsSet, natIsSet, flagIsSet and optNatIsSet), and covIsSet needs isSet of each Covenant part, so `def PayCfg : Type 0 := Prod Point Digest` replaces the family of Q-S7-1, with the defs `payCfgOf`, `keyPay`, `payAsset` and `payCfgIsSet := isSetProd U256 U256 u256IsSet u256IsSet` (the cpointIsSet form). The 6th Covenant part stays. | USER, S7 BUILD r0 |
| 2026-11-06 | Q-S7-8, verbatim: "pBits, balBits, decBits (Recommended)". src/js.c declares `const P` in its JS prelude, so the code names of the O8 constants are `pBits : Nat := 32`, `balBits : Nat := 160` and `decBits : Nat := 32` (the B of N7), with the U256 literals `twoP` (2^P) and `limA` (2^(Bbal - P)). The PLAN text keeps P, Bbal and B. | USER, S7 BUILD r0 |
| 2026-11-07 | Q-S7-10 (a), verbatim: "Export n-bit core (Recommended)". `src/front/transfer.c` exports the range proof of the low n bits of a U256 as `tr_range_build` and `tr_range_check` (1 <= n <= 256; bit i and the weight 2^i come from the limbs), and with a `tr_` prefix the helpers that `src/front/withdraw.c` uses. The S5 range proof is the n = `TR_BITS` form, with the same nonces, slots and sum, so the S5 proof words do not change. `src/front/withdraw.c` uses n = 160 (Bbal). | USER, S7 BUILD r2 |
| 2026-11-07 | Q-S7-10 (b), verbatim: "Ctx from statement (Recommended)". The host computes the context of a withdraw proof from the statement: the chain of `thr_hash` from `WD_TAG` over the words of the key of H, the holdBal Ct and `w`. The Schnorr signature on `wdDigest` binds the position and the id of the withdraw (Q-S7-3); the proof does not. The kit words are `withdrawProve key bal w [mu, x_h, seed]` (some proof words) and `withdrawVerify key bal w proof` (some flag). | USER, S7 BUILD r2 |
| 2026-11-07 | Q-S7-10 (c), verbatim: "New withdrawtool.c (Recommended)". `test/withdrawtool.c` (the `test/transfertool.c` form) runs the C rows of the withdraw proof in `make check`: the honest proof, the prover and verifier refusals (`w 2^P > mu`, `mu >= 2^160`, `w >= 2^128`), another statement and one changed word. With an argument, it also runs the changed sum, the forged proof of `w 2^P > mu` and the edge values. | USER, S7 BUILD r2 |
| 2026-11-07 | Q-S7-11, verbatim: "Domain: withdrawOut (Recommended)". The domain word of L18 is `withdrawOut`, not `withdraw`: `examples/contract.lang` (the kit contract example) has the entry `def withdraw`, and rule R4 refuses a program definition with the name of a domain definition (the r3 gate FAIL REFUSE_NAME). The kit example and `test/run` do not change. |
| 2026-11-09 | Q-S8-1, verbatim: "All 3 targets (Recommended)". The 4 public proof checks `attestVerify`, `transferVerify`, `issueVerify` and `withdrawVerify` lower on Wasm, ES and WGSL (S8 C3); the 3 differentials run their vectors. The prove words stay `REFUSE_TARGET_SECRET` (O6). | USER, S8 (AskUserQuestion) |
| 2026-11-09 | Q-S8-2, verbatim: "IR expansion in lower.c (Recommended)". `src/front/lower.c` writes each verify word as IR over the ops that lower now (OP_EC_ADD, OP_EC_MUL, OP_HASH, the U256 ops), so the 3 writers get no new prelude code and the differentials check the one site. | USER, S8 (AskUserQuestion) |
| 2026-11-09 | Q-S8-3, verbatim: "Total form, else refuse (Recommended)". N4: at each trap site of a writer, the writer emits the plain form where a type proves it (a `NonZero` divisor, a load or store inside the static memory); else the lowering refuses with `REFUSE_LOWER` and names N4. The `test/wasm/traps.lang` rows become refusal rows. | USER, S8 (AskUserQuestion) |
| 2026-11-09 | Q-S8-4, verbatim: "Eval IR-unit count (Recommended)". The N8 measure: `langc eval` counts the IR units that a run uses (the Q-S3-32 unit), and each differential row checks that the count is not above the `check --json` ops bound. A row has a bag of `maxHolders` = 1024 holdings (Q-S5-7). | USER, S8 (AskUserQuestion) |
| 2026-11-09 | Q-S8-5, verbatim: "Status LIMIT code (Recommended)". `IR_STMT_TRAP` (the fail flag f = 1: a Nat word above 2^64 - 1 or another host limit; lower.c:2357 option unwrap, lower.c:2391 entry) becomes a status code LIMIT. Each entry gets the status word (before S8 only the option entries had it). f = 1 stores LIMIT and returns a zero result. `langc eval` reports LIMIT; the `trap` expect rows and the entrytest rows change to LIMIT. | USER, S8 (AskUserQuestion) |
| 2026-11-09 | Q-S8-6, verbatim: "Static memory from N8 (Recommended)". The Wasm module declares a static memory: its initial memory is the largest `check --json` memory bound of its entries, in 64 KiB pages, and `alloc` has no `memory.grow`. A bound above the Wasm32 limit refuses with `REFUSE_LOWER` and names N4. Each load and store is then inside the static memory. | USER, S8 (AskUserQuestion) |
| 2026-11-09 | Q-S8-7, verbatim: "Status ARG code (Recommended)". The host argument checks (Wasm `flag_guard`, wasm.c:1216, a Flag above 1; the ES argument check, js.c:81, a non-BigInt or out-of-range argument) become a status code ARG: the entry checks its arguments first, stores ARG and returns a zero result. No `unreachable`, no `throw`. | USER, S8 (AskUserQuestion) |
| 2026-11-09 | Q-S8-8, verbatim: "Fixed module slot (Recommended)". The status word is in a fixed module slot: Wasm stores it at the constant address 32 (the word below `WASM_HEAP_START` = 40) and the entry has no status-address parameter; the ES module keeps an Int32Array(1) that it exports and the entry has no `st` parameter. No host value can make the status store fail. The host reads the slot after each call. | USER, S8 (AskUserQuestion) |
| 2026-11-09 | Q-S8-9, verbatim: "(a) Writer-local (Recommended)". The IR keeps one statement for the fail flag (IR_STMT_TRAP, renamed IR_STMT_LIMIT at the end of E2): status := LIMIT (2), then a zero result (for a U256 result, 4 zero words at address 0 first). Each writer emits it. An entry without an Option result gets status 1 from its writer (a prologue store). An Option entry keeps IR_STMT_STATUS, and each writer emits it always. The Wasm status word has the fixed address 32 (WASM_STATUS_AT), not a parameter. lower.c changes only the name. The writers change one at a time: Wasm, then ES, then WGSL. | USER, S8 (AskUserQuestion) |
| 2026-11-10 | Q-S8-10, verbatim: "entrytest.c IR row (Recommended)". No source program reaches the fail flag of IR_STMT_TRAP: the upTo walk end flag needs a list tail with the flag set, and an unfold at its limit ends the list with nil. So the LIMIT status (2) gets a hand-built IR function with the fail statement in test/entrytest.c: each writer emits it, and the test checks that the status word is 2. No test-only hook goes into langc. | USER, S8 (AskUserQuestion) |
| 2026-11-10 | Q-S8-11, verbatim (2 questions). Q1: "Count prelude heap (Recommended)". call_words gives each prelude call's full Wasm heap use (result + temps; the worst arm of fe_inv/ec_add/ec_mul loops). Q-S8-6 stays as ruled. Wasm exports the heap global as `$heap` and entrytest checks heap <= bound on every grid row. check --json memory numbers grow for FE/EC entries. Q2: "Drop the check (Recommended)". The Wasm bound proves the heap fits, and a JS array has no fixed size: alloc only pushes. HEAP_WORDS goes. Route: `wasmtool MODULE ABI BOUNDS.json` (BOUNDS.json is a `check --json` file) reads `$heap` after each call, and the memory bound also counts the 4-word block of each U256 entry parameter; the abi text and the default wasmtool output do not change. | USER, S8 (AskUserQuestion) |
| 2026-11-10 | Q-S8-12, verbatim: "Rely on N8 (Recommended)". The C2 tool `test/notraptool.c` has no rule for each Wasm load or store address (the lien-lang masked-address rule fails on 219 sites: a tranche address is an `alloc` block local plus a word offset). The tool checks that the memory has min = max, that no `memory.grow` occurs and that each data segment is inside the memory. The N8 bound (static memory, bump `alloc`, Q-S8-6) and the wasmtool heap row (BOUNDS.json, each run) are the bound check. No writer edit. Second question, verbatim: "By hand, main loop" (the C2 port is not given to a builder agent). |
| 2026-11-10 | Q-S8-13, verbatim: "PLAN note (Recommended)". No source program reaches the lower.c `ex_bin` refusal `a DIV or MOD divisor that is not NonZero` (N4). Each DIV or MOD divisor is an `ex_divisor` result (it sets the nonzero bit) or the `atom` of one (`atom` keeps the bit, lower.c:323). The callers are `nat_divmod` (natDiv and natMod, lower.c:1957-1958) and the MOD sites at lower.c:2113-2118 (the divisor `k` is the `atom` of an `ex_divisor`). The C2 tool `test/notraptool.c` rule DIV_FORM checks each divisor in each gate module. Thus this refusal has no gate row. The N4 heap refusal has a row: `test/ir/heap.lang` (`upTo 1000000000`, a heap of 3000000006 words) refuses `REFUSE_LOWER`. |
| 2026-11-11 | Q-S8-21, verbatim: "REPEAT + guard (Recommended)". The lowering of `attestVerify` (S8 C3) has 2 list walks: the ring list and the proof list. Each walk is an IR `REPEAT` with a constant count (29 ring cells, 28 links) and a live guard on each turn. It is not a `WHILE` with fuel and it is not a static unroll. A static unroll is 28 copies of the EC code, and the Metal compiler has a limit on the size of an inlined entry. |
| 2026-11-11 | Q-S8-22, verbatim: "Hoist T once (Recommended)". The lowering of `attestVerify` computes the challenge prefix T = H(H(TAG, D), m) one time. The challenge of each link is H(H(T, L.x), L.y). This is 4n + 4 SHA calls for a ring of n keys. A literal copy of the host `challenge()` for each link is 6n + 2 calls. The differentials at n = 1, 2 and 28 check that the result is equal to the host challenge. |
| 2026-11-11 | Q-S8-23, verbatim: "Per module from N8 (Recommended)". The WGSL heap slice of one invocation, `HEAP_WORDS` (src/wgsl.c), comes from the largest `check --json` memory bound of the entries of the module, the Q-S8-6 form for Wasm. It is not one fixed number: the old slice was 4096 words, and the lowering of `withdrawVerify` (S8 C3) needs about 58500 words. `test/wgsltool.c` reads `HEAP_WORDS` from the module text. | USER, S8 (AskUserQuestion) |
| 2026-11-11 | Q-S8-24, verbatim: "Reversed walk (Recommended)". The lowering of `withdrawVerify` adds the 160 bit proofs in the host order, from bit[159] down to bit[0] (Horner), but the proof list has bit[0] first. Turn j of the outer `REPEAT` gets bit[159 - j] with an inner `REPEAT` and a live guard (Q-S8-21): about 12700 more loads, no more EC ops and no heap. It is not a reversed list in the decode and not an EC_MUL by 2^i. | USER, S8 (AskUserQuestion) |
| 2026-11-11 | Q-S8-25, verbatim: "Direct first, share on fail (Recommended)". The `withdrawVerify` arm is first the direct form (the `issue_verify` form: about 13 EC_MUL and 17 EC_ADD call sites). Only if the WGSL pipeline check (`wgsltool --valid`) fails on the Metal limit of an inlined entry, the arm puts its EC ops on one shared EC_MUL site and one shared EC_ADD site, with an inner `REPEAT` over the ops. | USER, S8 (AskUserQuestion) |
| 2026-11-11 | Q-S8-26: the spine walk. The lowering of a cons list (`lower_cons`, src/front/lower.c) walks the spine with a C loop: it lowers each head in order, then it builds the cells from the last to the first. The depth of the lowering follows the element nesting and not the list length. Before, each tail was a field of its cell and was lowered by a recursive call, so a list cost 1 level per word: the 2574-word proof of the `withdrawVerify` W test (S8 C3) was over `EVAL_DEPTH_LIMIT` (2000, src/core.h). `EVAL_DEPTH_LIMIT` stays 2000 (the evaluator keeps it). | USER, S8 (AskUserQuestion) |
| 2026-11-11 | Q-S8-27, verbatim: "WGSL refusal (Recommended)". The WGSL build of `withdrawVerify` refuses with REFUSE_LOWER (HOST-LIMIT), as `transferVerify` does; the Wasm and JS builds lower it with the arm of Q-S8-24 and Q-S8-25. `lower_program` gets the target (its `wgsl` argument, 1 for `--wgsl`), and the OP_WITHDRAW_VERIFY case of src/front/lower.c refuses when it is 1. The reason: the WGSL module of the 14-row W test (11 entries, 18435628 bytes) failed the Metal pipeline check (`wgsltool --valid` rc 2 in 2735 s, GPUInternalError on each entry), and one invocation needs about 1.03 GB of heap (HEAP_WORDS 129012987). P4 closes on Wasm and JS with the 14 rows; `test/ir/withdraw-verify.lang` keeps a WGSL refusal row. The shared EC site form of Q-S8-25 is not used. |
| 2026-11-11 | Q-S8-28, verbatim: "5 rows, one per class (Recommended)". test/gate.sh carries 5 `withdrawVerify` vector rows of test/ir/withdraw-verify.lang on Wasm and JS, one per outcome class: `ok` accept (1), `ok` at w = 2^128 (0), `keyx` (a key off the curve, none), `f2573` (the sum word, 0) and `short` (2573 words, none). One row is about 133 s (the host eval, Wasm and JS). The WGSL list has no withdraw rows (Q-S8-27): the WGSL build is a `build_refused --wgsl` row with a check of its message. |
| 2026-11-11 | Q-S8-29, verbatim: "WGSL refusal (Recommended)". The WGSL build of `transferVerify` refuses with REFUSE_LOWER (HOST-LIMIT), the Q-S8-27 form of `withdrawVerify`; the Wasm and JS builds lower it (S8 C3, P5). The OP_TRANSFER_VERIFY case of src/front/lower.c refuses when `lower_wgsl` is 1. The reason: the host check runs 1311 group_mul and 1263 group_add with about 3 times as many distinct proof shapes as `withdrawVerify`, and the WGSL module of `withdrawVerify` failed the Metal pipeline check. The test/gate.sh row of test/ir/transfer-verify.lang becomes a `build_refused --wgsl` row with a check of its message. | USER, S8 (AskUserQuestion) |
| 2026-11-11 | Q-S8-30, verbatim: "Static rows (Recommended)". The lowering of `transferVerify` reads its rows argument, a `FreeMon (Prod (Prod C C) Units)`, as a closed term at lower time (the kit reads only a closed rows term, Q-S6-12). So the class count c, the cap count M, the caps and the lengths of the m1 and m2 proof lists are constants, and each walk of the arm has a static bound. A rows term that is not closed at lower time refuses with REFUSE_LOWER. | USER, S8 (AskUserQuestion) |
| 2026-11-11 | Q-S8-31, verbatim: "One row per class (Recommended)". test/gate.sh carries `transferVerify` vector rows on Wasm and JS, one per outcome class (the Q-S8-28 form): one good chain (c = 1, M = 1) gives the `ok` accept row; a changed proof word gives flagNo; a short proof list or a point off the curve gives none; a closed gate gives flagNo. The proof words are literals from one host run of `transferProve` (the r3s13 form). No c = 2 table row. | USER, S8 (AskUserQuestion) |
| 2026-11-12 | Q-S8-32, verbatim: "JS count mode (Recommended)". The N8 measure of S8 C4 is a count mode that only the gate uses. The JS writer (src/js.c) adds the Q-S3-32 units of each IR statement and expression that runs to one counter, and `test/jstool.c` prints the count after the result. Each JS differential row of test/gate.sh checks that the count is at most the `check --json` ops bound of its entry. The shipped JS text and the no-trap scan do not change. The reason: `eval` walks the core term and not the IR, the repo has no IR interpreter, and `Machine.fuel` (src/front/core.h) counts fold and list steps, not Q-S3-32 units. | USER, S8 (AskUserQuestion) |
| 2026-11-12 | Q-S8-33, verbatim: "Count replaces shipped (Recommended)". The wall form of the Q-S8-32 count rows. The run of the count rows on each JS differential row (r3 s41) gave bad 0 (test/ir 112 rows, examples 96 rows), but that form adds about 450 s to test/gate.sh, and the C4 wall is under 60 s. For test/ir/withdraw-verify.lang and test/ir/transfer-verify.lang, the count module replaces the shipped JS run: the shipped module still builds (the no-trap scan reads it), the results of the count module compare with the `langc --kit eval` rows, and each count is at most the `check --json` ops bound of its entry. The other modules run both, and the results of their count modules must be the same as the shipped results. Each JS differential row keeps its count check (the Q-S8-32 form). The gate adds about 200 s, and the shipped JS text of the 2 large modules does not run in the gate. The count modules go to a `cnt` subdirectory, so the no-trap scan does not change. | USER, S8 (AskUserQuestion) |
| 2026-11-12 | Q-S8-34, verbatim: "Pin test/ir bounds (Recommended)". The C4 bound row of each new entry. A new row of test/gate.sh runs `langc --kit check --json` on each test/ir module and compares its bounds byte-equal with a tracked expected file, so a bound that changes fails the gate. The count rows (Q-S8-32, Q-S8-33) check only that each count is at most its bound, and before this row only test/json/kit.bounds.json pinned bounds. | USER, S8 (AskUserQuestion) |
| 2026-11-12 | Q-S8-35, verbatim: "Run-time 1024 row (Recommended)". The C4 row with a bag of `maxHolders` = 1024 holdings (Q-S8-4, Q-S5-7). The row `units n1024` (examples/units.lang) does not measure it: the lowering folds the 1024 bag to a constant (ops bound 22), so the module holds no bag. A new test/ir entry takes a Nat `k` and builds the bag from it (`fmCons` of holding `k` on the 1024 bag, then `len` or `makeMeasure`), so the run walks the holdings. Its rows check the result, the count against the ops bound, and the memory bound (about 101 words for each holding, Q-S5-7). An entry cannot take a list parameter (Nat, Flag and U256 only). | USER, S8 (AskUserQuestion) |
| 2026-11-12 | Q-S8-36, verbatim: "Folded row + refusal row (Recommended)". The form of the Q-S8-35 row after the 1024 probe (r3 s44). Each entry of test/ir/bag1024.lang (`len` and `makeMeasure` on the 1024 bag, with a holding that is a function of the entry param `k`) folds at lower time: its `check --json` bound is ops 2, stack 2, memory 0, because `len` and `makeMeasure` read only the shape of the bag. The entry ships as planned, with its eval, Wasm, ES and WGSL rows, and its Q-S8-34 bound row (test/ir/bounds.txt) pins ops 2 and memory 0, so the fold is a tracked fact. The `holderAt` walk on the same bag does not fold: at 1024 holdings the lowering nests deeper than 2000 levels, and `check --json` and the 3 builds refuse EVAL_DEPTH; at 128 to 512 holdings the lowering runs out of memory (exit 2, 5 s to 32 s), and `langc check` with no --json says ok. One build row of test/gate.sh pins the EVAL_DEPTH refusal at 1024 (test/ir/holder-depth.lang: exit 1, no module, the message as the last line of stderr, after the 2 O24 notes). No change to src/front/lower.c. | USER, S8 (AskUserQuestion) |
| 2026-11-14 | Q-S8-37 (a), the USER ruling of r3 s47 (AskUserQuestion). The C3 test/ir rows are the differential vectors of the 4 proof checks (`attestVerify`, `transferVerify`, `issueVerify`, `withdrawVerify`). C5 adds no test/eval/expect.txt row for them. |
| 2026-11-14 | Q-S8-38 (a), the USER ruling of r3 s47 (AskUserQuestion). The S7 regression rows get first-order entries with a Flag result in `examples/pay.lang`, next to each theorem: `wdRangeOk` and `wdLimOk` (outOfRange), `s9FullOk` (full), `s9ZeroOk` (supply 0) and `l6RoundOk` (supply 3, the rounding row). Each entry compares the Except result with its expected value: the Fail tag, or the U256 parts (res A, the distributed word and the supply). The `pay` rows of test/eval/expect.txt run them in the eval, wasm, js and wgsl blocks. No new examples file: the examples count stays 66. |
| 2026-11-14 | Q-S8-39 (a), the USER ruling of r3 s47 (AskUserQuestion). No eval-only row for `wdProveVerify` (`examples/pay.lang`) or the `g1Run` row of `examples/paywd.lang`: they call the secret `withdrawProve`. The theorems (`langc check`) and the test/ir withdraw rows are the check. |
| 2026-11-14 | Q-S8-40, verbatim: "Bounds row kill (Recommended)", the USER ruling of r3 s51. It replaces "Row that sets a flag" (r3 s50). The mutants m4 ("fix 1: the st tail flags do not go into f") and m5 ("fix 1: the r2 tail flag does not go into f") give the value of the repo text on each of the 5 value rows (tranche-lang-work/s8/r3s37-mut.log: SURVIVED, SAME 5). The `test/ir bounds` row (`test/ir/bounds.txt`, Q-S8-34) kills each one (tranche-lang-work/s8/r3s51-mut.log: `bounds KILLED`). Thus the bounds row is the kill row of m4 and m5. |
| 2026-11-14 | Q-S8-41 and Q-S8-42, the USER rulings of r3 s53 (AskUserQuestion, verbatim labels). Q-S8-41, the mutant d9turn: "New gate row (Recommended)": `def v29t` (`test/ir/attest-verify.lang`), the ring of `v29` with the proof of `v28s`. Q-S8-42, the mutant d9eq: "Made-proof row": `def v29m`, the ring of `v29` with a proof that a prover form in tranche-lang-work made (digest over 29 keys, sign over 28, one fill word; the prover form is not in the repo). The ring of each def is closed: key 29 is a constant (29 G, the argument of the first `v29` row), and the row argument is only response 28. test/gate.sh has one row for each def (47 attest-verify rows), and `test/ir/bounds.txt` has the 2 bound rows. The host and each target give `none` on the 2 rows. With the 2 rows, the attest-verify rows kill d9eq and d9turn (tranche-lang-work/s8/r3s55-mut.log); the two SURVIVED before (r3s52-mut.log). |
| 2026-11-14 | S8 C6, the PART D mutants (the logs are tranche-lang-work/s8/r3s52-mut.log and r3s55-mut.log; the form is r3s55-mut.py). The control c0 (no edit) passes each row. A gate row kills each mutant: d1 "D1 Q-S8-29: the transferVerify WGSL refusal is off (if (0))"; d2 "D2 Q-S8-27: the withdrawVerify WGSL refusal is off (if (0))"; d3t "D3: the transferVerify message drops (HOST-LIMIT)"; d3w "D3: the withdrawVerify message drops (HOST-LIMIT)"; d4 "D4: the flag source m->lower_wgsl is 0 on each build"; d5 "D5 Q-S8-3 N4: the heap bound is (2^32 - 40) * 8 words, not / 8"; d6 "D6 Q-S8-36: the depth refusal is off (0 && depth >= EVAL_DEPTH_LIMIT)"; d8at "D8 Q-S8-9: WASM_STATUS_AT 32u -> 36u"; d8arg "D8 Q-S8-7: WASM_STATUS_ARG 3u -> 4u"; d9eq "D9 Q-S8-21: the ring cap test is n == RINGSIG_RING_MAX + 1"; d9turn "D9 Q-S8-21: the ring walk repeats RINGSIG_RING_MAX + 0 turns, not + 1"; d9len "D9 Q-S8-21: the proof length walk repeats RINGSIG_RING_MAX + 0 turns, not + 2"; d10 "D10 Q-S8-24: the Horner walk takes bit[idx] (bit[0] first), not bit[n - 1 - idx]"; d11 "D11 Q-S8-30: the lowered table has each cap 0 (not the host table)". d9eq and d9turn SURVIVED in r3s52-mut.log; the attest-verify rows kill them after the rows of Q-S8-41 and Q-S8-42 (r3s55-mut.log). D12, the lower.c `ex_bin` divisor refusal, has no gate row and no mutant: no source program reaches it (Q-S8-13). The mutants m1, m2 and m3 of r3s37-mut.log are KILLED by the value rows; m4 and m5 are in the Q-S8-40 row. |
| 2026-11-15 | Q-S8-17 (a), verbatim: "Keep, record only (Recommended)" (a USER ruling of the r3 plan; this row records it at C6). The Wasm Flag guard order stays: `entry_param` (src/wasm.c) copies a U256 param before a Flag check, and the copy is in bounds. No edit of the repo. |

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
| R | slot | `Claims -> Claims -> Option Units`, a total program definition. `none` refuses the pair. `some cap` admits the pair with a bound. The compiler makes the table of `R` over the closed sums `Juris` and `Status`, so the table has a static number of rows (N8). The transfer proof shows, with the claims hidden, that `com h` and `com k` open to a row that `R` admits and that `q <= cap` of that row (4.2). The kit word `rTable R` gives the table (Q-S4-3, Q-S4-7): the type `List (Prod (Prod C C) Units)` with `C = Prod Juris Status` (Q-S5-5 (b); S4 wrote Nat), the admitted rows `pair (pair h k) cap` in constructor order. More than 28 rows or more than 1024 pairs is `REFUSE_UNBOUNDED`. interest-lang DESIGN.md:50-53: R "depends only on identity claims" and is "the image of securities compliance inside the model". Q-S0-5 (c). The Covenant holds the table as `rows : Rows`, a `FreeMon` (Q-S6-10 (a)): `rowsOf` gives the Rows of the List of `rTable R`, and the kit words read a closed Rows in order (Q-S6-12 (a)). `rTable` stays a List. |

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
| Covenant | defined record | Not the kernel `Constitution` of IsSelfConstituting (Q-S4-6). The cairn-lang `Polity` (cairn-lang docs/PLAN.md:170, 60768e8) plus `issuers : FreeMon CPoint` (Q-S4-5 (a)), `keyOpenC : CPoint` (Q-S5-6 (a), the full point of `keyOpen`), `rows : Rows` (Q-S5-10 (a), the table of R: `makeCovenant` takes it as its 4th argument, and `covRows` gives it; `Rows := FreeMon (Prod (Prod Claims Claims) Units)`, Q-S6-10 (a), and the kit reads a closed Rows in order, Q-S6-12 (a)), `policy : Option D`, the policy in force (Q-S6-2 (a): the 5th part; `makeCovenant` takes it as its 5th argument, and `covPolicy` gives it), and the S7 fields `keyPay` and `tPay`. With Q-S0-8 (b), `(R, W)` changes only with a whole new constitution. The keys come from the DKG; there is no trusted dealer. |
| D | defined | `Nat`: an index into the program table of `W` (Q-S6-2 (a)). `policyOf : D -> W` is a total program definition over the table (Q-S6-9 (a)): 0 gives `wAll`, 1 gives `wRent` (a sale gives `0u`), and an index out of the table gives `wZero`. `policyKnown d` tests that d is in the table, and an amendment to an index out of the table keeps the old Covenant. R stays the Covenant `rows` (Q-S5-10 (a)), so the policy pair `(R, W)` is `covRows` with `policyOf`. The objects of `F` "include the admissible (R, W)" (interest-lang DESIGN.md:59). |
| Tally | defined | `Units`: the opened homomorphic sum of the yes votes on one proposal (the cairn-lang tally). This is the orbit quotient: the count, never who voted what (orbit-lang DESIGN.md:29). |
| Gov, GovPi | defined | `Gov` = `amendC` (Q-S6-1 (a)): the fold of the decided amendments (`CDecided := Prod Covenant Count`) in ledger order over the whole Covenant; `decideC` reads `tAmend` of the Covenant in force. The cairn-lang `amendAll` over `Polity` stays. `GovPi : Covenant -> Option (Prod Rows W)` pairs `covRows` with `policyOf` of the `policy` of the constitution that `Gov` gives (`none` when the `policy` is `none`, Q-S6-2 (a)). orbit-lang DESIGN.md:35: `Gov L := orbitProjection act >>> L.functor`. |
| AmendBallot | defined | The cairn-lang ballot: `Enc Bool` with its proof and its bLSAG (cairn-lang `src/front/blsag.c`), on a whole new `Covenant`. The key image of the bLSAG is the nullifier: one ballot for each member (orbit-lang law 5, DESIGN.md:112). `decide` = `yes >= tAmend`. The bLSAG is over the members ring on the proposal digest, and the cast verifies it in the guest with the kernel zk ops (Q-S6-3 (a)). The ballot carries its ring (Q-S6-13 (a)), and the zkVerify choice is `amendChoice` (Q-S6-14 (a)). With Q-S0-8 (b) it is the only ballot: a new policy is a new constitution. |
| Fate | defined | `impossible | debreu D`: orbit-lang law 6 (DESIGN.md:117), `Empty + (unique L) + (L0 x L1)`. `fateOf` reads the `policy` in force: `none` gives `impossible`, `some d` gives `debreu d`. Under Q-S0-8 (b) the `ising` summand has no inhabitant (section 5). |
| IsSelfConstituting | defined | orbit-lang DESIGN.md:91: `Sigma (L : Aggregation), Path Constitution (gov L) F`. A program gives the witness as a path; the kernel checks it (L13). The guest form (Q-S6-5 (a)): `Sigma (L : FreeMon CDecided) (Prod (AggOf (fateOf F)) (Path Covenant (Gov L c0) F))`, with `AggOf impossible = Empty` (L16) and `AggOf (debreu d) = Unit`. |
| Share, open | primitive | Partial decryption with a DLEQ proof; `tOpen` valid shares combined by Lagrange (MPC, cairn-lang `src/front/threshold.c`). `open` gives `noQuorum` below `tOpen` valid shares, and it names each dropped party in its result (N6). |
| Deal, Reshare | primitive | cairn-lang `src/front/dkg.c`: Pedersen DKG with Feldman commitments, and reshare when `members` or a threshold changes. |

Self-reference: an amendment is a whole `Covenant`, so the polity can
amend `tAmend`, `issuers` and `policy`. Each ballot is decided by the
constitution in force when it opens. A chain of amendments is a fold over
`FreeMon Covenant` (the cairn-lang `amendAll`). interest-lang
DESIGN.md:81: amendment "does not move mu or the treasury" (L8).
DESIGN.md:83: compliance labels and voter anonymity are different actions,
so a ballot never carries the claims of an identity.

S6 review correction: amendment ring keys use the representative with y <=
(p - 1) / 2 on secp256k1. Membership still compares x (Q-S6-13), but
accepting both signs allowed k and n - k to cast distinct key images for
one member. Signers normalize their scalar to this representative.

### 4.4 Part P: distribution and payment

S7 review correction: `covDigest` binds the payment key and asset of
`covPay`, so an issue signature cannot authorize a ledger that substitutes
either field. `examples/pay.lang` checks that either change alters the
digest; the `isPayKey` and `isPayAsset` eval rows in `examples/trkit.lang`
reject those substitutions with `noQuorum`. The valid issue signature
fixture was regenerated for the extended covenant digest.

| Name | Kind | Reading |
|---|---|---|
| Kind | defined | `rent | sale`. |
| W | slot | `W := wOf (wMap : Kind -> U256 -> U256)` (S6, Q-S6-2 (a)), from the policy in force (`policyOf` of the `policy` of the Covenant). The slot reads `W` only through the clamp `wApply` (a value above `a` gives `a`), so `W k a <= a` holds by construction (Q-S6-11 (a)); the law rows are eval rows on literal points. interest-lang DESIGN.md:47: W "selects which inflow is distributable". |
| Inflow | defined | `(kind : Kind, amount : U256)`, a ledger entry that the polity authorizes. |
| claim | defined | With Q-S0-6 (b) each claim is an encrypted balance. `u = mulDiv A (2^P) supply` when `supply > 0`, else `0`: the public credit for each unit, in units of `2^-P` of the asset (O8). The claim of `h` is `mu(h) * u`, and the ledger adds it to `bal h` as `scale u (own h)` by the homomorphic operations: no proof and no open. interest-lang DESIGN.md:42 has the claim `mu(h) / supply * A`; `mu(h) * u / 2^P` is below it by less than `mu(h) / 2^P` base units. |
| distribute | defined | `Fate -> Measure -> Inflow -> Except Fail (Prod Measure U256)`. Under `impossible` it credits nothing, and the inflow stays in the treasury (DESIGN.md:73: "funds stay in the treasury, neither burned nor silently paid"). Under `debreu d` it takes `A = W k a`. If `supply = 0`, it credits nothing and retains all of the inflow, with `u = 0` and `res A = A * 2^P`. Otherwise it checks the public lifetime total `distributed + A < 2^(Bbal - P)` before computing `u` or changing any balance, then adds `claim` to each `bal h`, increments `distributed` by `A`, and keeps the residual. Equality with the limit or an overflow gives `full` and leaves the state unchanged (O8). It opens nothing. |
| Withdraw | defined | `(id : Identity, w : U256, proof : WithdrawProof, sig, pos : OpPos)`, with `sig` as the two words `(rx, z)` (S7, Q-S7-3 (a); `makeWithdraw`, domain/domain.lang:788). `WithdrawProof`: with the secret key of `id`, the bit proofs (4.2) that `bal h - w * 2^P` is in `[0, 2^Bbal)` (`src/front/withdraw.c` on the n-bit range core of `src/front/transfer.c`, Q-S7-4 (a), Q-S7-10 (a) to (c); the C rows are `test/withdrawtool.c`). `sig` is a Schnorr signature of `key id` on `wdDigest` (domain/domain.lang:805). The Instruction copies `pos`, so a replay fails on the balance and in `paySnoc`. |
| withdrawOut | defined | `Covenant -> Measure -> Withdraw -> Except Fail (Prod Measure Instruction)`: first check the public integer bound `w < 2^(Bbal - P)` (`outOfRange` otherwise), then the proof against the current `bal h` and the signature, then subtract `w * 2^P` and give an `Instruction` to `key id` for `w`. The bound is checked before conversion to `Scalar`; no reduction modulo the group order can establish it. Together with the balance bound and `2^(Bbal + 1) < n` (O8), the residual range proof establishes integer non-underflow. The holder program gives `overBalance` before it makes the proof when `w * 2^P` is above its balance. |
| Instruction | defined | `(payee : Point, asset : Digest, amount : U256, pos : OpPos)`: the output of `withdrawOut`. |
| PayAuth, PayLog, paySnoc | defined | The accrue-lang S7 form, ported (S7, Q-S7-5 (a); domain/domain.lang:848 to :862): `family PayAuth := payAuthOf (payR : U256) (payZ : U256)`, `PayLog := FreeMon Instruction`, `ixDigest` of an Instruction under `tagPay` and `ixHead` of the log. `payOk` checks `schnorrOk` under `keyPay` of the Covenant part `pay` (Q-S7-1 (a)) on `(ixHead log, ixDigest p)`, and `paySnoc` adds an entry only with a valid `PayAuth` and a digest that is not in the log (accrue-lang domain/domain.lang:606-613, d1017f1). There is no `PayKind`: no cancel and no dispute. |
| Treasury | defined | `treasury : Fate -> Measure -> List Inflow -> Except Fail (Prod Measure U256)` (S7, Q-S7-2 (a); domain/domain.lang:693) folds `distribute` over the inflows (a list with a capacity, N3 and N8) and adds the U256 of each step, `(a - A) * 2^P + res A`. This is the fold of the ledger, in units of `2^-P` (O8): each inflow adds `a * 2^P`, each distribution takes `u * supply`. It keeps `a - W k a` and the residual `res A`. A withdrawal takes from `bal h`, not from the treasury. |

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
| L18 withdraw | `withdrawOut` checks `w < 2^(Bbal - P)` before scalar conversion, takes `w * 2^P` from `bal h` and gives one `Instruction` for `w`; the sum of the balances plus `2^P` times the sum of the withdrawals is the sum of the credits as integers, without reduction modulo `n` (Q-S0-6 (b)) |
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
| G3 | A rent distribution over three holders, with a residual to the treasury, then a withdrawal | `distribute`, `claim`, `withdrawOut`, L6, L18, PayAuth |
| G4 | An encrypted amendment vote on a new constitution whose policy turns on the distribution of sale proceeds | AmendBallot, tally, `decide`, `GovPi`, L8, L11 |
| G5 | No policy in force (`policy = none`): the inflow stays in the treasury; then the polity amends `tAmend`, `issuers` and `policy` | `impossible`, L16, `amend` as a fold, L14; S6 gives the Gov-level rows `distOf` and `keepOf` (Q-S6-18 (a)), and S7 adds the `distribute` and Treasury rows on a Measure |

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
| S6 | Part G: `D`, `policyOf`, the `policy` of the constitution, the AmendBallot tally, `Gov` as `amendC` (Q-S6-1 (a)), `GovPi`, Fate, IsSelfConstituting; L9 to L16. | DONE: C1 the 5th part `policy : Option D` of the Covenant (`covPolicy`, a 5th `makeCovenant` argument); C2 `D`, `Kind`, `W` with the clamp `wApply` (Q-S6-11 (a)) and the table `policyOf` (`wAll`, `wRent`, `wZero`, `policyKnown`; Q-S6-9 (a)); C3 `Rows` as a FreeMon and `covIsSet` (Q-S6-10 (a); the kit reads it in order, Q-S6-12 (a)), then `CDecided`, `decideC`, `stepC`, `amendC` and `Gov := amendC` (Q-S6-1 (a)); C4 the AmendBallot with the guest bLSAG and `castAmend` (Q-S6-3 (a), Q-S6-13 (a), Q-S6-14 (a); L15); C5 to C7 the tally laws L9 to L12 (L11 and L12 over `OrbitN`, Q-S6-6 (a)); C8 `Fate` and `fateOf`; C9 `GovPi`; C10 `AggOf` and `IsSelfConstituting` (L13, L16, Q-S6-5 (a)); C11 l9 on `amendC` (L14); C12 `issue` with the authority bound to `Gov` (`covDigest`, `ledgerDigest`, `issDigest`; Q-S6-4 (a), Q-S6-15 (c), Q-S6-16 (a), Q-S6-17 (a)); C13 `examples/govern.lang` with the G5 rows (Q-S6-18 (a)); the refusal rows `test/check/govern-*.lang` (Q-S6-8 (a)). `make check` EXIT 0: examples 64, check refusals 208, evals 384, ir 56 lowered, 2 HOST-LIMIT, 6 REFUSE_TARGET_SECRET. |
| S7 | Part P: W, Inflow, `claim`, `distribute`, the claim balances, Withdraw, `withdrawOut`, Instruction, PayAuth (from accrue-lang), Treasury; L6 to L8, L18 (O8); the G5 rows of `distribute` and Treasury at `impossible` (Q-S6-18 (a)). | DONE: C1 the 6th Covenant part `pay : PayCfg` with `PayCfg := Prod Point Digest` (`covPay`, a 6th `makeCovenant` argument; Q-S7-1 (a), Q-S7-7 (a)); C2 `Inflow`, `claim` (`u`) and the O8 constants `pBits`, `balBits` and `decBits` (Q-S7-8 (a)); C3 `distribute` and C4 `treasury` (Q-S7-2 (a)), with the G5 rows in `examples/govern.lang`; C5 the withdraw proof `src/front/withdraw.c` on the n-bit range core of `src/front/transfer.c` (Q-S7-4 (a), Q-S7-10 (a) to (c)), its kit words and `test/withdrawtool.c`; C6 `Withdraw` with `pos` (Q-S7-3 (a)), `withdrawOut` (Q-S7-11 (a)), `Instruction` and `wdDigest`; C7 `PayAuth`, `PayLog`, `payOk` and `paySnoc` (Q-S7-5 (a)), the `frosttool pay` and `frosttool wd` words; C8 the rows G1, L6, L7, L8, L18 and section 9 (Q-S7-6 (a)): `examples/pay.lang` lowers, and `examples/paywd.lang` holds the rows that reach a secret op (G1 and L7; the ir stage refuses it with `REFUSE_TARGET_SECRET`); C9 the mutants (the records are in tranche-lang-work/s7/mutants): phase W 4 of 4 KILLED by the row (`wm*`; `wm4-rest` is an equivalent mutant), phase L 8 of 8 KILLED by the row (`lm*`), and the gate set (the short set of `test/withdrawtool.c`) kills wm1, wm2, wm3 and wm5 (`short`). `make check` EXIT 0: examples 66, check refusals 208, evals 392, ir 57 lowered, 2 HOST-LIMIT, 7 REFUSE_TARGET_SECRET, json rows 34, json fixes 29, transfer 12 rows, withdraw 15 rows, jointr 16 rows. |
| S8 | Targets: lowering of the new words on Wasm, ES and WGSL; N4, `test/notraptool.c`, the N8 measure; differential vectors. | DONE: C1 the N4 writer audit (Q-S8-3): each trap site of the 3 writers (`src/wasm.c`, `src/js.c`, `src/wgsl.c`) has a total form or a lowering refusal (`src/front/lower.c`; the heap refusal row is `test/ir/heap.lang`); C2 `test/notraptool.c` with `test/notrap/`: the tool reads each module that the gate writes (Wasm bytes, ES text, WGSL text) and fails on a trap form (the rule DIV_FORM checks each divisor, Q-S8-13); C3 the lowering of the 4 public proof checks: `attestVerify` and `issueVerify` lower on Wasm, ES and WGSL, and `transferVerify` and `withdrawVerify` lower on Wasm and ES and refuse on WGSL with `REFUSE_LOWER` (Q-S8-27, Q-S8-29); the rows are in `test/ir/issue-verify.lang`, `test/ir/attest-verify.lang`, `test/ir/withdraw-verify.lang` and `test/ir/transfer-verify.lang`; C4 the N8 measure: the JS count mode (Q-S8-32, Q-S8-33), the pinned bounds `test/ir/bounds.txt` (Q-S8-34), the 1024 bag row `test/ir/bag1024.lang` and the depth refusal row `test/ir/holder-depth.lang` (Q-S8-35, Q-S8-36), and `HEAP_WORDS` for each WGSL module (Q-S8-23); C5 the differential vectors in `test/eval/expect.txt`: the S7 regression rows and the rows of each new word (Q-S8-37 to Q-S8-39), with the evals `wdRangeOk`, `wdLimOk`, `s9FullOk`, `s9ZeroOk` and `l6RoundOk` of `examples/pay.lang`; C6 the mutants: a gate row kills each one (the rows Q-S8-40, Q-S8-41 with Q-S8-42 and PART D above; the records are in tranche-lang-work/s8). `sh test/gate.sh gpu` EXIT 0 with `gate: 0 failures` (tranche-lang-work/s8/r3s56-gate-gpu.log): `examples: 66 checked, 0 failed`; `check refusals: 208 checked`; `evals: 397 checked`; `build refusals: 19 checked`; `ir: 57 lowered, 2 refused (HOST-LIMIT), 7 refused (REFUSE_TARGET_SECRET)`; `wasm: 57 modules valid`; `wgsl: 57 modules written`; `wasm differential: 101 rows, 0 mismatches`; `js differential: 101 rows, 0 mismatches`; `js count: 101 rows, 0 bad`; `test/ir wasm: 10 modules valid, 119 rows, 0 mismatches`; `test/ir js: 119 rows, 0 mismatches`; `test/ir js count: 119 rows, 0 bad`; `test/ir bounds: 69 rows, 0 changed`; `notrap regressions: 47 checked`; `notrap bad: 13 of 13 refused`; `notrap: 117 of 117`; `notrap js: 31 of 31`; `notrap wgsl: 57 of 57`; `wgsl valid: 57 modules`; `wgsl known internal: 0 modules, 0 entries`; `wgsl differential: 101 rows, 0 mismatches`; `test/ir wgsl: 109 rows, 0 mismatches`; `wgsl batch: 192 rows, 0 mismatches`; `wgsl chain: 1024 links, 1 bad link found`. |
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
