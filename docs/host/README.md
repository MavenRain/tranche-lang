# TinyCC Wasm host kit

This kit compiles a small typed language to a WebAssembly MVP module.
One C99 program, `build/langc`, holds the parser, the checker, the
evaluator, the lowering to a first-order IR and the Wasm writer.
TinyCC builds it.

## Files

The repository has one kit:

- the source: the front end (`src/front/`), `src/ir.h`, `src/target.h`
  and `src/main.c`
- the writers: `src/wasm.c`, `src/js.c` and `src/wgsl.c`
- the domain and its embed tool: `domain/` and `gen/`
- the examples: `examples/`
- the tests: `test/`, with the runners `test/wasmtool.c` (on the
  wasmtime C API), `test/jstool.c` and `test/wgsltool.c`
- the build and gate files: `Makefile`, `test/gate.sh` and `PIN`

## Build

From the repository root:

```sh
make build
make check
make check-clang
```

`build` runs `tcc -run gen/embed.c` to copy `domain/domain.lang` into
`build/domain.c`. Then it builds `build/langc` with
`tcc -std=c99 -Wall -Werror`. The domain is part of the executable.
`check-clang` compiles the same sources with
`cc -std=c99 -Wall -Wextra -Wswitch-enum -Werror -fsyntax-only`.
`check` does both steps, then runs `test/gate.sh`.
The gate needs `node`, `wasm-validate` and `wasm-objdump` on the `PATH`.
`PIN` records the tool versions of the last GREEN gate.
The build and the gate need no network access.

## Source language

A program is a sequence of `def NAME : TYPE := TERM`. A line comment
starts with `--`. Lambdas are `fun (x : A) => t`. There is no text type.

Core names: types `Nat Flag Prod Sum Option List Eq`, values
`flagYes flagNo pair first second inl inr either none some option nil cons
pure map bind fold unfold filter pack witness payload refl symm trans
transport cong`, Nat operations `natAdd natSub natMul natEq natLe`, and the
Flag eliminator `flagIf b yes no`. `Type 0 : Type 1` is the universe
chain. `Sigma (x : A) B` and `(x : A) * B` are dependent pairs.
`FORMERS.md` gives the status of each former.

The compiler enforces these rules:

| Rule | Code | The compiler refuses |
|---|---|---|
| R0 | `REFUSE_PARTIAL` | a partial name (for example `div`, `mod` or `sub`), the `Option` result of a checked kit word where a plain value is wanted, or the literal 0 where a `NonZero` is wanted |
| R1 | `REFUSE_DATA` | a `family` declaration in a program |
| R2 | `REFUSE_AXIOM` | an `axiom` declaration |
| R3 | `REFUSE_REC` | `def rec`, or a definition that refers to itself or to a later definition |
| R4 | `REFUSE_NAME` | a core name, a domain name, a reserved name or a repeated name as a new definition |
| R4 | `REFUSE_TIME` | a time word (`now`, `block`, `timestamp`, `time`, `clock`, `epoch`) as a name or a use; the message says to count operations (`len r`, an `OpPos`) |
| R1, O7 | `REFUSE_CELL` | a type constructor that is not one cell signature or one cocell signature, a cons-list name in place of a cell monad, or a sealed family that is not a cell monad |
| - | `REFUSE_EVENT` | an event with more fields than the entry parameter limit, an event field that is not an ABI word type (`Nat`, `Flag`, `U256`, `Addr`), or a repeated event field |
| D-9 | `REFUSE_HISTORY_WRITE` | a history field `h` that is not `(h t)`, `ADD (h t) e`, or the variable of `bind (ADD (h t) e)`, with `ADD` as `u256Add` or `natAdd`, a history constructor without all of its arguments, or a definition with `State` in its type that refers to a name that the history rule does not check |
| - | `REFUSE_KIT` | a kit item outside the base span (PLAN section 4); only the kit mode `langc --kit` admits it |
| O7 | `REFUSE_LAW` | a law-only name (`decrypt`, `observe`, an elim, a fold or a projection of a sealed family) in a definition whose type is not a law (a `Path` or `Trunc1` codomain) |
| - | `REFUSE_MAP` | a `Map` key type that is not `Nat`, `U256` or `Addr`, or a `Map` value type that is not `Nat`, `Flag`, `U256` or `Addr` |
| - | `REFUSE_SIG` | a name of the design signature (PLAN section 3): a slot with the wrong type, a reserved name that no slice gives yet, or a defined or primitive name that the kernel gives |
| - | `REFUSE_STATE` | a second state, a state type with a name other than `State`, a state with parameters, an index telescope or not one constructor, a state field that is not first-order, or a history field that is not `Nat` or `U256` |

## CLI

```sh
build/langc check [--json] PROG
build/langc eval PROG NAME [ARGS...]
build/langc ir PROG
build/langc abi PROG
build/langc build PROG -o OUT [--runtime]
build/langc fmt PROG [--check]
build/langc explain CODE
build/langc [--kit] mcp
```

- `check` prints `ok`, or refuses the program. With `--json`, it
  prints one JSON line on stdout in place of `ok` or the diagnostic
  (`docs/CHECK-JSON.md`).
- `eval` prints the normal form of `NAME`. Each argument is a decimal
  number. For an Option result with a `Nat`, `Flag` or `U256` payload,
  `eval` prints the payload of `some`, or `none`. It prints another
  Option result as `some (...)` or `none` (S3, Q-S3-9).
- `ir` prints the first-order IR. It has one `func` for each entry.
- `abi` prints the name of each entry, one for each line.
- `build` writes the Wasm module to `OUT`. On this target, `--runtime`
  gives the same bytes.
- `fmt` prints the canonical text of the program (S11, A1). It parses
  `PROG` and does not check it, so it also accepts a program that `check`
  refuses (`examples/algebra.lang`, `domain/domain.lang`). With
  `--check`, `fmt` prints nothing and exits 0 when `PROG` is canonical.
  Else it prints `PROG:LINE: not canonical` on stderr, where `LINE` is
  the first line that is different, and exits 1. On a lex or parse error,
  `fmt` writes the diagnostic on stderr, writes nothing on stdout and
  exits 1. A usage error, an IO error or a full arena gives exit 2.
  Before emitting text, `fmt` checks that it fits the source size limit
  and reparses it. If the canonical spelling exceeds the size or parser
  depth limit, `fmt` exits 1 with a diagnostic and no stdout output.
- `explain` prints one line `CODE: MEANING (DOC, SECTION)` for each
  refusal code: the meaning of the code and the section of this file
  that has it (S13, Q-S13-7). For any other code, it writes an
  `EXPLAIN` diagnostic on stderr, writes nothing on stdout and exits 1.
- `mcp` is a Model Context Protocol server on stdin and stdout (S13,
  A4). It reads one JSON-RPC 2.0 message on each line and gives the
  verbs `check --json`, `fmt`, `eval`, `build` and `explain` as 5
  tools (`docs/MCP.md`). With `--kit`, each tool runs in the kit mode.
  At EOF it exits 0, also after error replies. It exits 2 when stdin
  cannot be read or stdout cannot be written.

The exit status is 0 for success, 1 for a refused program and 2 for a
usage or IO error. A diagnostic on stderr has the form
`langc: CODE: DEF: message`.

## Entries

An entry is a top-level program definition whose declared type is `Nat`,
`Flag`, or a first-order function from `Nat` and `Flag` arguments to a
`Nat` or `Flag` result, or to an `Option` of one. `build` exports each
entry by its name. Each parameter and the result is an `i64`. An entry
with an `Option` result has one more parameter, the status word (see "No
trap form"). Other definitions are not exported.

`Nat` is an unsigned 64-bit integer. `natAdd` gives `none` on an
overflow past 2^64 - 1, and `natSub` gives `none` when the second
argument is larger than the first. `Flag` is 0 or 1.

## Compilation

Compilation is partial evaluation. The compiler normalizes the body of
each entry with the parameters as free variables. Types and universes erase.
Proof contents erase. A lambda stays only as the direct function argument of
`fold`, `unfold`, `map`, `bind`, `filter`, `either` or `option`. The
lowering puts its body into a loop or a branch. The lowering refuses
programs with three HOST-LIMIT codes (see `docs/CAPABILITY.md`), one O6
code and one bound code (Q-S3-28):

- `REFUSE_RUNTIME_CLOSURE`: a function value must exist at run time.
- `REFUSE_RUNTIME_TREE`: a fold walks a tree that exists only at run time.
- `REFUSE_LOWER`: a construct does not lower.
- `REFUSE_TARGET_SECRET`: the operation touches a secret key or a witness (O6).
- `REFUSE_UNBOUNDED`: a loop has a trip count that exists only at run time; write it as `upTo K` with a Nat literal K.

## No trap form

A partial operation gives an `Option`: `none` where it has no result.
`langc eval` prints `none` (Q-S3-9). The writers have no trap form for
`none` (Q-S3-2 (c)): no `unreachable` in Wasm, no `throw` in JS and no
WGSL trap status. An entry with an `Option` result writes a status word,
0 for `none` and 1 for `some`. The result is valid only when the status
is 1.

- Wasm: the last parameter is an `i32`, the address of the status word
  (the tools give 32). The `abi --text` line of the entry starts
  `option[i32 status at 32] `, then the result shape. The JSON abi line
  has `"option":true` after the result.
- JS: the last parameter `st` is an `Int32Array`; the entry writes the
  status to `st[0]`.
- WGSL: the out row of an entry is [trap status, Option status, result].

The tools preset the status word to 2, so an entry that writes no status
gives a `bad` line.

The lowering lowers a value one time in a block. The blocks in that
block use the same result, but the two arms of a branch do not share
results. Normalization at compile time drops terms that are not read, for
example `first (pair a b)` gives `a`, so a `none` in an unused term does
not change the result.

## Wasm layout

- The module has one memory with a maximum of 16384 pages (1 GiB) and
  one mutable `i64` global, the heap pointer.
- Function 0 is the internal allocator `alloc (bytes : i32) -> i64`. It
  increases the heap pointer. When a block does not fit, it calls
  `memory.grow`. When the memory cannot grow past 1 GiB, it traps.
- Functions 1 to P are the prelude functions that the module calls.
  Function 1 + P + K is entry K. Each entry first sets the heap pointer to
  40 (`WASM_HEAP_START`), so each call starts with an empty heap.
- Address 0 holds a `U256` result (4 words). Address 32 holds the status
  word of an entry with an `Option` result (`WASM_STATUS_AT`).
- An entry traps when a `Flag` argument is larger than 1.
- A word is 8 bytes. A heap block holds the tag in word 0, the word of
  field K in word 1 + 2K and a flag word in word 2 + 2K. The flag words
  stay from the old lazy trap form. The curve prelude gives 0 when a flag
  word of a point is not 0 (`point_flags` in `src/wasm.c`).
- A tag switch is nested blocks and `br_table`. A tag out of range traps.
- `u256Mul` and `mulDiv` lower on all three targets. Multiplication gives
  `none` when the product does not fit; `mulDiv` divides the exact 512-bit
  product by its `NonZero U256` divisor and gives `none` when the quotient
  does not fit. `test/entrytest.c` checks their word boundaries, including
  zero divisors handled through `nonZero`.

## Domain slot

`domain/domain.lang` holds the families and the operations of the domain.
Only this file can declare a `family` (rule R1). `domain/README.md` tells
what the domain holds and how to change it.

## Gate

`make check` prints these lines when it is GREEN:

| Line | What the gate does |
|---|---|
| `examples: 59 checked, 0 failed` | `langc check` accepts each `examples/*.lang` file |
| `parse refusals: 19 checked` | each `test/parse` file fails with the code in `test/parse/expect.txt`; one file has 1100 nested parentheses |
| `check refusals: 204 checked` | each `test/check` mutant fails with the code in `test/check/expect.txt`; the `TYPE_REFL` message shows both normal forms |
| `evals: 275 checked` | `langc eval` gives each result in `test/eval/expect.txt` |
| `build refusals: 10 checked` | `langc build examples/dao.lang -o OUT` exits 0, writes nothing on stdout and writes a valid module; each refused build exits 1 with its code and writes nothing on stdout; the rows include the transp row (`test/ir/transp.lang`, `REFUSE_LOWER`), three `REFUSE_TARGET_SECRET` rows (`test/ir/secret-key.lang`, `test/ir/secret-sign.lang`, O6, and `test/ir/secret-key.lang` with `--wgsl`), the elim row (`test/ir/tree-elim.lang`, `REFUSE_RUNTIME_TREE`, O26), a guest-mode `REFUSE_KIT` row and the `REFUSE_UNBOUNDED` row (`test/ir/unbounded.lang`, Q-S3-28: the message names `upTo K`, and `build/fronttool fix` with `test/ir/unbounded.fix` gives a file that `langc --kit build` builds) |
| `ir: 56 lowered, 2 refused (HOST-LIMIT), 1 refused (REFUSE_TARGET_SECRET)` | `langc ir` lowers each example, with one `func` for each `abi --text` line derived independently from checked types; an example that does not lower refuses `REFUSE_LOWER` with a `HOST-LIMIT` detail (`examples/ballot.lang` and `examples/daokit.lang`: `sigOk` is a host proof check), and `examples/zk.lang` refuses `REFUSE_TARGET_SECRET` (`noSigner`, Q-S3-24) |
| `wasm: 56 modules valid` | `langc build` writes a module for each lowered example, and `wasm-validate` and `wasm-objdump -x` accept it |
| `wgsl: 56 modules written` | `langc build PROG --wgsl OUT.wgsl` writes a WGSL compute module for each lowered example; each build exits 0 and writes nothing on stdout |
| `wgsl refusals: 2 checked` | each `--wgsl` usage row of `src/main.c` fails with its message: `--wgsl` with `--js`, and `--wgsl` with no path |
| `wasm differential: 93 rows, 0 mismatches` | `build/wasmtool` (`test/wasmtool.c`, on the wasmtime C API) runs each `test/eval/expect.txt` row whose name is an entry of its file; each result is equal to the row result, and `none` is equal to `none` |
| `js differential: 93 rows, 0 mismatches` | `langc build PROG --js OUT.mjs` writes an ES module for each lowered example, and `build/jstool` (`test/jstool.c`, node through `popen`) runs the rows of the `wasm differential` line; each result is equal to the row result, and `none` is equal to `none` |
| `test/ir wasm: 5 modules valid, 32 rows, 0 mismatches` | each `test/ir` module builds in `--kit` mode and is valid, and each row of the gate list gives the same result on `build/wasmtool` and in `langc --kit eval` |
| `test/ir js: 32 rows, 0 mismatches` | each row of the `test/ir` gate list gives the same result on `build/jstool` and in `langc --kit eval` |
| `entry boundary grid: 616 rows, evaluator/wasm/js passed` | `test/entrytest.c` checks Nat and Flag entries against independent C arithmetic |
| `fixture boundary grid: 178 rows, wasm/js passed` | the fixture grid compares compiled results with the evaluator |
| `word boundary grid: 23 rows, evaluator/wasm/js passed` | fixed arithmetic identities check U256 zero detection, multiplication and full-width division, including overflow and zero-divisor handling |
| `json abi: 2 checked` | `langc --kit abi test/wasm/secp.lang` prints the JSON abi line (each kind is `nat`, `flag` or `u256`; an entry with an `Option` result has `"option":true`), and `abi` with a flag other than `--text` refuses `USAGE` |
| `fmt canonical: 308 passed` | `langc fmt` gives the same bytes for each accepted `.lang` file of `domain/`, `examples/` and `test/*/` outside `test/fmt/` (the `test/json` rows `LEX_*` and `PARSE_*` are not in this check); each file that `langc fmt` refuses is in `test/fmt/refused.txt` (18 `test/parse` files), and no accepted file is |
| `fmt round trip: 315 passed` | each accepted file (the 308 files and the 7 `test/fmt/*.lang` files) and its canonical text give the same `build/fronttool dump` (`test/fronttool.c`: one line for each node, with no line numbers and no comments) |
| `fmt rows: 7 passed` | `langc fmt test/fmt/X.lang` gives the bytes of `test/fmt/X.canon` for each of the 7 rows |
| `fmt check: 8 passed` | `langc fmt X.canon --check` exits 0 for each of the 7 rows, and `langc fmt test/fmt/check.lang --check` exits 1 with `test/fmt/check.lang:3: not canonical` |
| `json rows: 28 passed` | for each `test/json/X.lang`, `langc check --json` exits 0 (the rows `ok`, `kit` and `kit.TAG`) or 1 (each other row, whose first code is the file name up to the first dot), writes nothing on stderr, and gives the bytes of `test/json/X.json`; each refused row has `test/json/X.fix` (`docs/CHECK-JSON.md`) |
| `json refusal codes: 19 of 19` | each `REFUSE_` code of `src/front/check.c` and `src/front/lower.c` has a row `test/json/CODE.lang`, and the list of codes is not empty |
| `json fixes: 24 passed` | `build/fronttool fix` applies each `test/json/X.fix` to its row, and `langc check --json` of the result exits 0 or gives a first code other than the code of the row |
| `mcp rows: 9 passed` | for each `test/mcp/X.in`, `build/mcptool` runs `langc mcp` (`langc --kit mcp` when `X` starts with `kit-`) with the file on stdin: stdout gives the bytes of `test/mcp/X.out`, stderr is empty and the exit status is 0 |
| `mcp negatives: 3 of 3` | `build/mcptool` exits 1 on each of 3 bad inputs: a copy of the `explain` row with one more byte in its `.out`, a row with no `.out`, and no row |
| `mcp tools: 5 of 5` | each of the 5 tools (`check`, `fmt`, `eval`, `build`, `explain`) has a `tools/call` request in a `test/mcp/*.in` row |
| `explain codes: 19 of 19` | the `REFUSE_` codes in `src` (not in `src/front/codes.c`) are the codes of the `src/front/codes.c` table, and for each code `langc explain CODE` exits 0 with one line `CODE: ...` and no stderr |
| `gate: 0 failures` | the sum of all failures |

`make check` also runs `test/jstest.c` against an ES module with entries
named `default`, `a'` and `then`, loaded from a path containing a space,
`#`, `?` and `%`. It repeats the calls with an apostrophe in `TMPDIR`.

The gate also checks the usage and IO exit codes, five `eval` refusals,
`test/ir/closure.lang` (`REFUSE_RUNTIME_CLOSURE`), `test/ir/tree.lang`
(`REFUSE_RUNTIME_TREE`), the 11 entries of `test/ir/spine.lang`,
`pick 2 4 9` = `trap` on Wasm, and the absence of em-dashes and en-dashes
in the kit. The lowering computes a path of data that is stuck at compile
time before it takes the end point of the path, so a `none` in the path
reaches the result: the 4 entries of `test/ir/proof-trap.lang` give `none`
at 18446744073709551615 on Wasm.

`make check` also compares the 25 fixed rows in `test/wasm/traps.txt`
against the evaluator, Wasm and the ES module, and the rows of
`test/wasm/hash.txt` and `test/wasm/secp.txt` against Wasm and the ES
module. They cover neutral path endpoints,
`none` curve coordinates, and unused invalid curve results. It checks
that `abi --text` still reports checked signatures when lowering refuses
`test/ir/transp.lang`.

`make gpu-check` runs `sh test/gate.sh gpu`, the WebGPU rows (S10). It
needs a GPU adapter, so it runs outside the sandbox. `build/wgsltool`
(`test/wgsltool.c`) gives each WGSL module to deno (`--unstable-webgpu`;
`docs/VALIDATION.md` V11). `make check` skips these rows. `make gpu-check`
prints these lines when it is GREEN:

| Line | What the gate does |
|---|---|
| `wgsl valid: 52 modules` | the GPU compiles each entry of the WGSL module of each lowered example; the 4 modules of `test/wgsl/internal.txt` are not counted |
| `wgsl known internal: 4 modules, 20 entries` | each entry of `test/wgsl/internal.txt` gives `GPUInternalError` (the Metal backend limit, `docs/VALIDATION.md` V15), and no other entry does |
| `wgsl differential: 81 rows, 0 mismatches` | `build/wgsltool` runs the rows of the `wasm differential` line; each result is equal to the row result, and `none` is equal to `none` |
| `wgsl regressions: passed` | the rows of `test/wasm/hash.txt`, `test/wasm/secp.txt` and `test/wasm/traps.txt` on the WGSL module, so the gate prints this line three times |
| `test/ir wgsl: 32 rows, 0 mismatches` | each row of the `test/ir` gate list gives the same result on `build/wgsltool` and in `langc --kit eval` |
| `wgsl batch: 192 rows, 0 mismatches` | the batch proof check of `test/wgsl/batch.lang` on the rows of `test/wgsl/batch.txt`: 64 rows each for `sigOk`, `or0Ok` and `or1Ok` |
| `wgsl chain: 1024 links, 1 bad link found` | the chain rehash of `test/wgsl/batch.lang` checks 1024 links on the GPU and finds the one bad link |

## Known limits

- A `fold` over a bare `nil` cannot infer its type (`TYPE_INFER`). Use a
  definition with a declared `List` type.
- With no expected type, `either`, `map` and `cong` take the result type
  of the function at a fresh variable. A dependent result needs a
  declared type.
- A definition with a `List`, `Prod` or other non-scalar result is not an
  entry. `ir` and `abi` skip it.
- `langc eval` stops after 20000000 steps (`EVAL_FUEL`). The Wasm module
  has no step limit, so a large loop count can run on Wasm and stop in
  `eval`. The grid uses large values only for entries with no loop.
- The arena of `langc` and the Wasm memory each stop at 1 GiB.
- Source files are at most 1 MiB. `Nat` literals are at most
  18446744073709551615.
