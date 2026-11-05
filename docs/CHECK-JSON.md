# `langc check --json`

`langc check --json` checks a program. After a clean check, it also lowers
the program to the IR, thus a lowering refusal is a diagnostic too (ruling
Q-S3-34). `langc check` without `--json` does not lower. It writes one
JSON line on stdout. A tool reads this line in place of `ok` or the text
diagnostic on stderr. S12 adds it (PLAN row A2, rulings Q-S12-1 to
Q-S12-12). S3 C9c adds the lowering and the keys `bounds` and `fails`
(rulings Q-S3-32 to Q-S3-36). S4 C6 adds the key `tables` (ruling
Q-S4-7).

## Usage

```sh
build/langc check --json PROG
build/langc --kit check --json PROG
```

| Exit | stdout | stderr |
|---|---|---|
| 0 | the JSON line, with an empty diagnostic list | nothing |
| 1 | the JSON line, with one diagnostic: a check refusal or a lowering refusal | nothing |
| 2 | nothing | the `OOM` diagnostic |

## The line

The line is one JSON object and a newline. The keys come in this order:

```
{"diagnostics":[DIAG],"progress":{"parsed":P,"typed":T,"laws_discharged":D,"laws_total":L},"bounds":[BOUND],"fails":[FAIL],"tables":[TABLE]}
```

When `langc` accepts the program, `diagnostics` is empty. Else it holds
one object, the first failure. An example (`test/json/REFUSE_SIG.json`):

```json
{"code":"REFUSE_SIG","span":{"file":"test/json/REFUSE_SIG.lang","line":1,"col":1,"end_line":1,"end_col":31},"message":"Outcome: the slot Outcome must have the type Type 0","fix":{"span":{"file":"test/json/REFUSE_SIG.lang","line":1,"col":15,"end_line":1,"end_col":21},"text":"Type 0"}}
```

- `code`: the refusal code. The `REFUSE_` codes are in
  `docs/host/README.md` (section Source language).
- `span`: the file and the source text that fails. The file is the path
  as given on the command line. Lines and columns start at 1, one byte
  is one column, and the end column is exclusive. A lowering diagnostic
  has `span` and `fix` null: the IR keeps no source span.
- `message`: the text of the diagnostic.
- `fix`: `null`, or one edit: put `fix.text` in place of the text of
  `fix.span`. The edit removes this failure. Another failure can come
  next. Some `LEX_` and `PARSE_` diagnostics and the `REFUSE_CELL`,
  `REFUSE_SIG` and `REFUSE_STATE` diagnostics have a fix.

Each string is JSON: `"`, `\`, newline and tab get a short escape, other
control bytes get `\u00XX`, an invalid UTF-8 byte gets `�`, and
valid UTF-8 stays as it is (`src/front/json.h`).

## Progress

| Key | What it counts |
|---|---|
| `parsed` | the program decls that parse (a block counts each sort) |
| `typed` | the program decls that check before the first failure |
| `laws_discharged` | the typed program defs that are laws only (`law_only`) |
| `laws_total` | `laws_discharged`, plus the program defs that did not check and that a syntax test of rule O7 marks as laws (`LawScan` in `src/front/check.c`) |

Only program decls count. Domain decls, kit decls and the spliced orbit,
dao and zk sources do not count. After a `LEX_` or a `PARSE_` failure,
the 4 counts are 0.

## Bounds

`bounds` has one object for each entry of the program, in source order
(rulings Q-S3-32 and Q-S3-33):

```
{"entry":"NAME","ops":N,"stack":S,"memory":M}
```

An entry is a def that the build gives as a target function (`entry_of`):
its result is a Nat, a Flag or a U256, or an `Option` of one. The counts
are in IR units, the statements of `langc --kit ir` (`ir_bound` in
`src/front/lower.c`):

| Key | What it counts |
|---|---|
| `ops` | 1 for each IR statement and 1 for each expression node. A prelude call counts 1 and its args. An `if` or a `switch` counts its larger arm. A loop counts 1, 1 for its count K, and K times its body. |
| `stack` | words: the locals, 1 for the status word of an `Option` entry (a Nat, Flag or U256 entry has no status word), and the most words that one expression holds at one time (the left operand before the right, the args in order) |
| `memory` | heap words: the fields of each `alloc` and the result block of each prelude call, times the loop counts, the larger arm |

The result block of a prelude call has 4 words for `u256_add`,
`u256_sub`, `u256_mul`, `to_u256`, `sha256`, the `fe_` calls and
`mul_div`, 5 words for `ec_add` and `ec_mul`, and 0 words for `u256_le`,
`u256_eq` and `schnorr_ok`. The EC temporaries do not count. A count
stops at `UINT64_MAX` and does not wrap. The IR has no call from one func
to another, thus each entry is one frame. The target costs (Wasm, ES,
WGSL) are for S8.

When `diagnostics` is not empty, or the program has no entry, `bounds` is
`[]`. An example (`test/json/kit.bounds.json`):

```
"bounds":[{"entry":"steps","ops":349,"stack":27,"memory":27},{"entry":"one","ops":2,"stack":1,"memory":0},{"entry":"wide","ops":39,"stack":12,"memory":7}]
```

## Fails

`fails` has one object for each program def with an `Except` result
(ruling Q-S3-35):

```
{"def":"NAME","values":["CTOR",...]}
```

`values` has the `Fail` constructors in the normal form of the def. This
is an upper set: the inputs are Nat, Flag or U256, thus each `Fail` value
comes from a constructor in the term. The walk does not go into a path
abstraction. `Except` is a family (O24), thus no entry can give a `Fail`,
and a def in `fails` has no object in `bounds`.

When `diagnostics` is not empty, or no def has an `Except` result, `fails`
is `[]`. An example (`test/json/kit.fails.json`):

```
"fails":[{"def":"pick","values":["full","overflow"]}]
```

## Tables

`tables` has one object for each program def whose core is `rTable R`
(ruling Q-S4-7):

```
{"def":"NAME","rows":N,"pairs":P}
```

The check applies these rules to the def, in this order:

1. R must have the type `C -> C -> Option Units`, with `C = Prod J S`
   (Units is the domain family of the cap, ruling Q-S5-5 (b)). J
   and S must be closed sums: each constructor has no field, and the type
   has no parameter, no index and no path constructor. C can be a def
   alias (`def C : Type 0 := Prod J S`). If not, the code is
   `TYPE_MISMATCH`.
2. `pairs` is the number of pairs (h, k) of points of C, thus
   (|J| |S|)^2. If `pairs` is more than 1024, the code is
   `REFUSE_UNBOUNDED`. The check does this before it evaluates R.
3. The check evaluates R at each pair. h is the outer loop and k is the
   inner loop. In the points of C, J is the outer loop and S is the inner
   loop. Each value must be a closed `none` or a closed `some (unitsOf n)`. If not,
   the code is `REFUSE_UNBOUNDED`.
4. `rows` is the number of `some` values. If `rows` is more than 28, the
   code is `REFUSE_UNBOUNDED`.

The core of the def is then a static quoted list: one `((h, k), u)` for
each `some u`, in the loop order. Thus the def has no object in `bounds`
and no object in `fails`.

When `diagnostics` is not empty, or no def has an `rTable R` core,
`tables` is `[]`. An example (`test/json/kit.tables.json`). C is
`Claims = Prod Juris Status`, with 6 points and 36 pairs:

```
def R : Claims -> Claims -> Option Units :=
  fun (h : Claims) (k : Claims) => fold (fold none none (some (unitsOf 1)) (first k)) (some (unitsOf 2)) (some (unitsOf 3)) (first h)

def table : List (Prod (Prod Claims Claims) Units) := rTable R
```

```
"tables":[{"def":"table","rows":28,"pairs":36}]
```

## FIXFILE

`build/fronttool fix PROG FIXFILE` applies one fix to PROG and writes the
result on stdout. FIXFILE has this form:

- line 1: `LINE COL END_LINE END_COL`, the span of the fix. Columns start
  at 1, one byte is one column, and the end is exclusive.
- the remaining text of the file: the new text, less one final newline.

The new text can be empty or end in a space. Thus `.gitattributes` turns
off the git whitespace check for `test/json/*.fix`.

An example, `test/json/REFUSE_SIG.fix`:

```
1 15 1 21
Type 0
```

The row `test/json/REFUSE_SIG.lang` is `def Outcome : Type 1 := Type 0`.
The fix puts `Type 0` in place of `Type 1` (line 1, columns 15 to 20).

## The test rows

The `json rows` block of `test/gate.sh` runs each `test/json/*.lang` file
as a row:

- `CODE.lang`: a row for the code CODE. `ok.lang` and `kit.lang` are
  accepted rows, and so are the rows `kit.TAG.lang` (`kit.bounds`,
  `kit.fails`, `kit.tables`). `langc` refuses each other row, and its
  first code is CODE.
- `CODE.TAG.lang`: one more row for CODE (`REFUSE_UNBOUNDED.rows`,
  `REFUSE_UNBOUNDED.pairs`, `REFUSE_TARGET_SECRET.closed`,
  `REFUSE_TARGET_SECRET.holder`, `TYPE_MISMATCH.rtable`). The code is the
  file name up to the first dot. The row `REFUSE_TARGET_SECRET.closed` is
  a closed def that reaches `zkKey` (ruling Q-S4-9). The row
  `REFUSE_TARGET_SECRET.holder` is a closed def that names the holder key
  mark `holderKey` (ruling Q-S5-4).
- `X.json`: the expected line of the row X. `langc check --json` must give
  these bytes and write nothing on stderr.
- `X.fix`: the FIXFILE of each refused row X. After `fronttool fix`,
  `langc check --json` must exit 0 or give a first code other than CODE.
- The rows `kit`, `REFUSE_HISTORY_WRITE`, `REFUSE_MAP`, `REFUSE_PARTIAL`,
  `REFUSE_STATE` and the 5 lowering rows (`REFUSE_LOWER`,
  `REFUSE_RUNTIME_CLOSURE`, `REFUSE_RUNTIME_TREE`, `REFUSE_TARGET_SECRET`
  and `REFUSE_UNBOUNDED`) run with `--kit`. The row
  `TYPE_MISMATCH.rtable` also runs with `--kit`: without `--kit`, the Nat
  literal of `unitsOf 1` gives `REFUSE_KIT`. The other `TYPE_MISMATCH` rows run without
  `--kit`.
- Each `REFUSE_` code of `src/front/check.c` and `src/front/lower.c` must
  have a row `CODE.lang` (ruling Q-S3-36).
- Each row must be `langc fmt` canonical (`build/langc fmt X.lang | cmp -
  X.lang`). The `LEX_` and `PARSE_` rows are not in this check, because
  `langc fmt` refuses them. The gate line `fmt canonical` counts the
  other rows.

To add a row X:

1. Write `test/json/X.lang`, and make sure that it is canonical.
2. From the repo root, run `build/langc [--kit] check --json
   test/json/X.lang > test/json/X.json`. The span file is the path as
   given, thus use this relative path.
3. Read the line. Count the progress values and the bounds by hand (the
   bounds from `build/langc --kit ir test/json/X.lang`).
4. For a refused row, write `test/json/X.fix`.

At S4 C7 the gate prints `json rows: 33 passed`, `json refusal codes: 19
of 19` and `json fixes: 28 passed`.
