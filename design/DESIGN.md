# tranche-lang domain design

> Paste the domain design below the line, verbatim. Do not edit it.

This file is the only source of the core types and the core operations of
tranche-lang. `SPEC.md` sections 5 and 6 cite its sections. Do not add a core
type or a core operation that this file does not give.

When the design changes, paste the new version. Then record the change as an
open item in `SPEC.md` section 9.

---

## The request (USER, 2026-11-01, verbatim)

One change only: the paste had one trailing space after "criteria:". That
space is not here, because `git diff --check` refuses trailing white space.

```text
I want to write a new programming language with the following characteristics:

First, the language should not allow any runtime errors, period.

Next, for type constructors/higher-kinded types, admit only cell monads and their duals as representated computationally in a Cubical Type Theory.

For base types and operations, admit the minimum in order to express https://github.com/MavenRain/interest-lang as an off-chain ZK+FHE+MPC version governed by a ZK+FHE+MPC off-chain threshold-authorized, self-amending system with a base like https://github.com/MavenRain/orbit-lang.

Finally, I want agents to reach for this language when it fits their use case.  To that end, please add the following criteria:

  The plan for making agents reach for the language includes:
  - one canonical form per operation and a langc fmt canonical printer;
  - langc check --json, giving fix hints and progress counts (parsed, typed, laws
    discharged);
  - a SKILL.md with 3 to 5 graded examples;
  - an MCP server written in TinyCC (langc mcp);
  - AGENTS.md and a README table saying when to use the language;
  - a token-count comparison against Python and Rust.
```

## The S0 answers (USER, 2026-11-01)

- Targets: Wasm MVP, ES module + JSON, WGSL compute.
- Primary target: Wasm.
- Name: tranche-lang.
- Host: TinyCC. The request asks for an MCP server written in TinyCC.

## The interest-lang domain design (MavenRain/interest-lang fe3f47b, design/DESIGN.md below its line, verbatim)

A self-referential ownership-interest DAO denotes as the existing aggregation acting on a claim algebra. The token is a residual claim on an LLC or SPV, not a deed. The design is the one already fixed in [MavenRain/self-referential-dao](https://github.com/MavenRain/self-referential-dao), extended the way `escrow-lang` extends it: the treasury is an action of the aggregation, not a second aggregation.




## Stance

Elliott's rule is the whole method: the instance's meaning is the meaning's instance. Model first, representation second, meaning function total. Every admitted operation is a homomorphism for that meaning. A failed homomorphism is an abstraction leak and is rejected, not patched. Partiality is `Option`, never an exception.

The governance half is not redesigned.

\[
\llbracket\mathrm{DAO}\rrbracket = \mathrm{Aggregation}\,\mathrm{act}\,F = \mathrm{LeftKanExtension}\,(\mathrm{orbitProjection}\,\mathrm{act})\,F
\]

`act` is the symmetry that encodes voter anonymity by the orbit quotient. `F : Obj ⥤ D` is the constitution. Anonymity is the quotient. Legitimacy is the left-Kan universal property.

## Semantic domain

Two meanings, then their dependent pairing.

The interest is a finitely supported measure on verified identities, not on raw addresses:

\[
\mu : \mathrm{Identity} \to \mathbb{Q}_{\ge 0}, \qquad \mathrm{supply} = \sum_h \mu(h)
\]

An identity carries jurisdiction, investor status, and (only at the representation layer) a wallet set. The claim is the pro-rata functional

\[
\mathrm{claim}(h, a) = \begin{cases} (\mu(h)/\mathrm{supply})\cdot a & \mathrm{supply} > 0 \\ 0 & \mathrm{supply} = 0 \end{cases}
\]

on a cancellative commutative monoid `Asset` of rent and sale proceeds. Cancellation is what makes a double-claim observationally impossible.

A waterfall \(W : \{\mathrm{Rent}, \mathrm{Sale}\} \to \mathrm{Asset} \to \mathrm{Asset}\) selects which inflow is distributable. A restriction

\[
R : \mathrm{Identity} \times \mathrm{Identity} \times \mathbb{Q}_{\ge 0} \to \mathrm{Prop}
\]

depends only on identity claims. \(R\) is the image of securities compliance inside the model. A transfer the bytecode allows but \(R\) rejects is not in the semantic image.

\[
\llbracket\mathrm{OwnershipDAO}\rrbracket = \Sigma\,(L : \mathrm{Aggregation}\,\mathrm{act}\,F).\;\mathrm{InterestState}
\]

with \(\mathrm{InterestState} = (\mu, W, R, \mathrm{treasury})\). The objects of \(F\) include the admissible \((R, W)\).

The deed, the land registry, and the operating agreement are an external interpretation of this claim. Putting them inside \(\llbracket\cdot\rrbracket\) is the leak this design refuses: a transfer the contract accepts is not a conveyance of legal title. RealT and Lofty are interpretations of the same model, not extra semantic objects. Liquidity and the existence of a compliant secondary market are outside the denotation.

## Operations are homomorphisms

**Transfer.** \(\tau(h, k, q)\) is defined iff \(R(h, k, q)\) and \(q \le \mu(h)\), and then it is the unique measure update that subtracts \(q\) from \(h\) and adds \(q\) to \(k\). Otherwise `none`. Laws inherited from the free cancellative commutative monoid of finitely supported measures, quotiented by \(R\): identity at quantity zero, conservation of supply, associativity on admissible chains, commutativity of disjoint transfers. A success return while \(R\) fails is a leak. ERC-1644 forced recovery is not a holder transfer. It denotes only when \((\mathrm{Gov}\,L).\mathrm{obj}\,X\) authorizes it, and it does not quotient through the voter orbit.

**Distribution.** Pro-rata is forced, not chosen. It is the unique monoid homomorphism from \((\mathrm{Asset}, +)\) to holder claims that preserves the unit and the sum-to-inflow law:

\[
\sum_h \mathrm{claim}(h, W(\mathrm{kind})(a)) = W(\mathrm{kind})(a)
\]

The claim travels with the token, so transfer-then-distribute equals distribute on the image measure. In the Arrow-Impossibility regime the verdict is `none` and distribution is `none`: funds stay in the treasury, neither burned nor silently paid.

**Amendment.** Forced by the same definitional equality as the repo:

\[
\llbracket\mathrm{amend}\rrbracket = \mathrm{Gov}, \qquad \mathrm{Gov}\,L = \mathrm{orbitProjection}\,\mathrm{act} \ggg L.\mathrm{functor}
\]

Amendment rewrites the constitution the next distribution and the next restricted transfer will read. It does not move \(\mu\) or the treasury. A restriction module, jurisdiction gate, or sale-proceeds toggle that cannot be recovered as \((\mathrm{Gov}\,L).\mathrm{obj}\,X\) is not in the image of \(\llbracket\cdot\rrbracket\).

Compliance labels and voter anonymity are different actions. Identifying them collapses the orbit quotient. A labelled constitution is the impossibility case; an anonymous constitution keeps the quotient.

## Self-reference

Self-reference is an object-wise fixed point of \(\mathrm{Gov}\), not a domain-theoretic least or greatest fixed point and not a coinductive coalgebra.

\[
\mathrm{IsSelfConstituting}\,F \;\equiv\; \exists L.\;\forall X.\;(\mathrm{Gov}\,L).\mathrm{obj}\,X = F.\mathrm{obj}\,X
\]

A self-referential ownership DAO is one whose restriction predicate and waterfall are exactly the rules the holders aggregated. The unit is the Lambek unit of the Kan extension, \(L.\mathrm{unit} : F \Rightarrow \mathrm{Gov}\,L\). Existence for the shipped constant constitution is constructive via `zeroPhaseAggregation`. Knaster-Tarski is not used.

The honesty fix carries over. For the constant constitution, \(\mathrm{IsSelfConstituting}\) is single-valued at every coordination pressure \(\beta\), and it is always the zero phase. Nonzero phases are not fixed points. The fork lives in the cardinality of `Aggregation` objects, which bifurcates from one to two at \(\beta_c = 1\), not in the fixed-point predicate. A genuinely bifurcating self-constitution would need a non-constant, order-parameter-pinning constitution, which this design does not supply.

## Three fates, inherited

The representation lands in exactly one regime of `self_governance_trichotomy`.

- **Arrow-Impossibility.** No legitimate anonymous aggregation, hence no self-constituting charter. Verdict-dependent operations denote to `none`. Claims freeze as measures; they are not deleted.
- **Arrow-Debreu.** Object-unique left Kan extension. One legitimate verdict on \((R, W)\).
- **Schelling-Ising.** Object-distinct left Kan extensions: two equally legitimate restriction regimes or waterfalls. That is a fork of aggregation objects, not by itself a fork of \(\mathrm{IsSelfConstituting}\).

## One meaning, three representations

| Shape | Representation | What it denotes |
| --- | --- | --- |
| ERC-721 whole-property NFT | \(\mathrm{supply} = 1\), Dirac \(\mu\) | the whole inflow is that identity's claim |
| ERC-1400 fractional security token | coproduct of measures, one \(R\) per partition | tranches are summands, not a second algebra |
| ERC-3643 (T-REX) | identity is ONCHAINID; \(R\) reads claim topics from trusted issuers | `canTransfer` is a decision procedure for \(R\) |

A document hash is metadata. If a document changes the waterfall, that change has to come through \(\mathrm{amend} = \mathrm{Gov}\) or it is a leak. The trusted-issuer set is part of the constitution and is amendable only through \(\mathrm{Gov}\).

A later contract is correct exactly when its observable operations agree with this denotation: an accepted transfer is an \(R\)-admissible measure update, a distribution is the pro-rata linear map and is `none` under impossibility, an amendment equals \(\mathrm{Gov}\) and does not move balances, and a self-constituting charter satisfies the object-wise fixed point. Resemblance to an ERC storage layout is not correctness.

## The orbit-lang domain design (MavenRain/orbit-lang 2162fb2, design/DESIGN.md below its line, verbatim)

One change only: 8 lines of the orbit-lang file end in two spaces (Markdown
line breaks). Those spaces are not here, because `git diff --check` refuses
trailing white space.

## The request (verbatim, 2026-10-16)

I want to write a new programming language with the following features:

Type constructors/higher types includes are products, coproducts, monads (map/bind), algebras/coalgebras (fold/unfold), quotients/paths/loops/HITs/QITs/HIITs/QIITs as in Cubical TT

The only base types and operations allowed are as described in the following (however, the laws should solve as a minimal, but not complete, list):

## The pasted design (verbatim)

**Minimal span in Cubical TT (CCHM-style) for a zero-knowledge self-referential DAO**, following the denotational design in [MavenRain/self-referential-dao](https://github.com/MavenRain/self-referential-dao).

The reference treats a DAO not as storage or a contract, but as the universal lift of anonymous local choices along the voter-orbit quotient:

```text
⟦DAO⟧ = Aggregation act F ≔ LeftKanExtension (orbitProjection act) F
```

Self-reference is the fixed point of the governance endo-operation

```text
Gov L ≔ orbitProjection act ⋙ L.functor
IsSelfConstituting F ≔ ∃ L. ∀ X. (Gov L).obj X ≡ F.obj X
```

Anonymity and legitimacy are inherited from the orbit quotient and the Lan universal property (unit, descent, uniqueness). The three UAT regimes (Arrow-Impossibility / Arrow-Debreu / Schelling-Ising) classify the cardinality of such aggregations. The original design has no cryptographic ZK; the Assay/EVM port is public. We therefore add a minimal ZK layer for private ballots while preserving the categorical core.

Cubical TT supplies Path types (equalites as functions out of the interval \(I\)), path abstraction \(\langle i\rangle t\), path application, composition/filling (Kan operations), and Glue for equivalences. We work in a universe \(U\) and write Path \(A\) \(a\) \(b\) (or \(a\equiv b\)) for paths.

### Types

```text
Member, Choice, Outcome, Group : U

Config      : U          -- configurations of local choices
Orbit       : U          -- orbit space of Config under the group action
Constitution : U         -- choice rules (roughly Config → Outcome, or functors)
Aggregation  : U         -- the semantic domain (realizations of the Lan)
ZKWitness    : U         -- zero-knowledge witness of a well-formed private ballot
Tally        : U         -- public aggregate (outcome of Aggregation)
Ballot_priv  : U         -- private ballot (commitment + choice + nullifier / proof data)
```

### Operations

```text
orbitProj   : Config → Orbit
act         : Group → Config → Config          -- group action
aggregate   : Constitution → Aggregation
gov         : Aggregation → Constitution
amend       : Aggregation → Aggregation        -- self-amendment (⟦amend⟧ = gov)
castPrivate : Member → Choice → Ballot_priv
proveZK     : Ballot_priv → ZKWitness
verifyZK    : ZKWitness → Tally → U            -- verification predicate (or a path type)
project     : Ballot_priv → Choice             -- public choice projection
```

(The Kan structure of Aggregation is packaged inside `aggregate`; the unit and descent of the Lan are the mediating paths below.)

### Laws

1. **Anonymity (orbit invariance)**
   For all \(g : \mathsf{Group}\), \(c : \mathsf{Config}\),
   ```text
   ⟨i⟩ aggregate F (act g c)  ≡  aggregate F c     : Path Tally …
   ```
   (or the stronger statement that the constitution is constant on orbits: \(F.(\mathsf{act}\, g\, x) \equiv F.x\)).

2. **Legitimacy (Lan universal property)**
   There is a unit path
   ```text
   unit : Path (Constitution → …) (F ⋙ orbitProj) (aggregate F)
   ```
   and a unique mediating path (descent) for any other candidate extension. Uniqueness is expressed by a path between any two such mediators (the cubical form of \(\mathsf{desc\_unique}\)).

3. **Self-constitution (fixed point)**
   ```text
   IsSelfConstituting F ≔ Σ (L : Aggregation), Path Constitution (gov L) F
   ```
   Equivalently, a path \(\langle i\rangle\,\mathsf{gov}\,(\mathsf{aggregate}\, F)\, \equiv\, F\).
   In the constant/zero-phase constitutions of the reference this path exists uniquely; in the Schelling-Ising regime the Aggregation type itself may fork (two distinct paths), while the self-constituting witness remains single-valued.

4. **Amendment homomorphism**
   ```text
   ⟨i⟩ amend (aggregate F)  ≡  aggregate (gov (aggregate F))
   ```
   (i.e. \(\llbracket\mathsf{amend}\rrbracket = \mathsf{Gov}\)).

5. **Zero-knowledge (completeness & soundness)**
   - Completeness: for any well-formed private ballot \(b\),
     ```text
     ⟨i⟩ verifyZK (proveZK b) (aggregate F (project b))  ≡  1
     ```
   - Soundness / privacy: verification depends only on the public projection and the tally. Using Glue (or a partial element / face restriction) one obtains an equivalence
     ```text
     Ballot_priv  ≃  Choice × ZKWitness
     ```
     such that the path is constant on the private data; extracting the private data from a verified witness is impossible (the fibre over a public choice is a contractible or empty higher type after verification).
   - Non-malleability / one-use: a nullifier path that is unique per Member (or a higher path enforcing that two proofs for the same nullifier are path-equal).

6. **Regime trichotomy (optional but faithful to the reference)**
   The type Aggregation is classified by a sum (or a dependent family indexed by a coordination parameter \(\beta\)):
   ```text
   Aggregation ≡ Empty          +   (unique L)   +   (L₀ × L₁)
   --            Arrow-Impossibility   Arrow-Debreu   Schelling-Ising
   ```
   with a path witnessing the bifurcation at the critical value \(\beta_c = 1\), and the law
   ```text
   ¬ IsSelfConstituting F   whenever   Aggregation ≡ Empty.
   ```

These types, operations and path-laws are the minimal cubical signature that reifies the denotational meaning of the referenced DAO (Lan along the orbit projection, governance fixed point, anonymity/legitimacy) while adding a zero-knowledge ballot layer whose privacy is expressed by path-independence and Glue equivalences. All equalities are computational paths; the Kan operations of Cubical TT supply the fillers needed for composition of amendments and transport along privacy equivalences.

## The request, continued (verbatim)

---------

Finally, the host language shall be TinyCC, and the compilation target should be EVM-deployable bytecode to any EVM chain, such as Telcoin network or mainnet Ethereum.
