#!/bin/sh
# The kit gate. `make check` runs it from the kit directory after the build.
set -eu
fail=0
count=0
# `sh test/gate.sh gpu` adds the WebGPU rows (S10, make gpu-check).
gpu=${1:-}
# kit LIST FILE prints --kit when FILE is a line of LIST. A row whose text uses a
# kit name or a Nat or word literal is a kit row (orbit-lang S3); the others run
# in guest mode.
kit() {
  if [ -f "$1" ] && rg -q -x -F "$2" "$1"; then echo --kit; fi
}
# The count rows (S8 C4, Q-S8-32): `count_rows SRC DIR STEM [--kit]` builds SRC
# with --js-count to DIR/cnt/STEM.mjs (not a module of the no-trap scan) and
# runs the rows of DIR/STEM.in in build/jstool. Each count must be at most the
# `check --json` ops bound of its entry. When the shipped module ran (DIR/STEM.js
# is there), each result must be its row; when it did not run (Q-S8-33 (b)), the
# caller compares the results (DIR/STEM.cres) with the eval rows. It prints
# `ROWS BAD MAX` (MAX: the largest count), or `build` when a stage fails. The
# bad rows are in DIR/STEM.cbad.
count_rows() {
  cr_src=$1
  cr_dir=$2
  cr_stem=$3
  shift 3
  mkdir -p "$cr_dir/cnt"
  : >"$cr_dir/$cr_stem.cbad"
  : >"$cr_dir/$cr_stem.cres"
  if ! build/langc "$@" build "$cr_src" --js-count "$cr_dir/cnt/$cr_stem.mjs" 2>"$tmp/c.err" ||
    ! build/langc "$@" check --json "$cr_src" >"$cr_dir/$cr_stem.json" 2>>"$tmp/c.err" ||
    ! build/jstool "$cr_dir/cnt/$cr_stem.mjs" "$cr_dir/$cr_stem.abi" <"$cr_dir/$cr_stem.in" >"$cr_dir/$cr_stem.cnt" 2>>"$tmp/c.err"; then
    echo build
    return 0
  fi
  rg -o -r '$1 $2' '"entry":"([^"]*)","ops":([0-9]+)' "$cr_dir/$cr_stem.json" >"$cr_dir/$cr_stem.bound" || :
  set -- $(awk -v res="$cr_dir/$cr_stem.cres" -v bad="$cr_dir/$cr_stem.cbad" '
    FILENAME == ARGV[1] { b[$1] = $2; next }
    FILENAME == ARGV[2] { n += 1; name[n] = $1; next }
    /^ops [0-9]+$/ { ops[i] = $2 + 0; next }
    { i += 1; print > res }
    END {
      nb = 0
      mx = 0
      if (i != n) { print "rows " n ", results " i > bad; nb += 1 }
      for (k = 1; k <= n; k += 1) {
        if (!(k in ops)) { print "no count: row " k " " name[k] > bad; nb += 1 }
        else if (!(name[k] in b)) { print "no bound: row " k " " name[k] > bad; nb += 1 }
        else if (ops[k] > b[name[k]] + 0) { print "over: row " k " " name[k] " ops " ops[k] " bound " b[name[k]] > bad; nb += 1 }
        if ((k in ops) && ops[k] > mx) mx = ops[k]
      }
      printf "%d %d %d\n", n, nb, mx
    }' "$cr_dir/$cr_stem.bound" "$cr_dir/$cr_stem.in" "$cr_dir/$cr_stem.cnt")
  if [ -f "$cr_dir/$cr_stem.js" ]; then
    cmp -s "$cr_dir/$cr_stem.cres" "$cr_dir/$cr_stem.js" ||
      { echo "the results differ from the shipped module" >>"$cr_dir/$cr_stem.cbad"; set -- "$1" $(($2 + 1)) "$3"; }
  fi
  echo "$1 $2 $3"
}
for prog in examples/*.lang; do
  out=$(build/langc $(kit examples/kit.txt "${prog#examples/}") check "$prog" 2>&1) || true
  count=$((count + 1))
  if [ "$out" != "ok" ]; then
    echo "FAIL $prog: $out"
    fail=$((fail + 1))
  fi
done
echo "examples: $count checked, $fail failed"

tmp=${TMPDIR:-/tmp}/langc-gate.$$
mkdir -p "$tmp"
awk 'BEGIN { printf "def deep : Nat := " } { } END {
  for (i = 0; i < 1100; i++) printf "("; printf "1"; for (i = 0; i < 1100; i++) printf ")"; print "" }' </dev/null >"$tmp/deep.lang"

# refusals DIR LABEL: each line of DIR/expect.txt is `FILE CODE`. `langc check`
# must exit 1 and print `langc: CODE: ...` on stderr.
refusals() {
  checked=0
  while read -r file code; do
    path="$1/$file"
    [ "$file" = "deep.lang" ] && path="$tmp/deep.lang"
    status=0
    build/langc $(kit "$1/kit.txt" "$file") check "$path" 2>"$tmp/err" >/dev/null || status=$?
    first=$(head -n 1 "$tmp/err")
    checked=$((checked + 1))
    case "$status:$first" in
      "1:langc: $code: "*) ;;
      *) echo "FAIL $file: want exit 1 and $code, got exit $status: $first"; fail=$((fail + 1)) ;;
    esac
  done <"$1/expect.txt"
  echo "$2 refusals: $checked checked"
}
refusals test/parse parse
refusals test/check check

# A REFUSE_KIT row refuses a kit item in guest mode only: `langc --kit check`
# must print ok.
kit_only=0
while read -r file code; do
  [ "$code" = "REFUSE_KIT" ] || continue
  status=0
  out=$(build/langc --kit check "test/check/$file" 2>&1) || status=$?
  kit_only=$((kit_only + 1))
  if [ "$status:$out" != "0:ok" ]; then
    echo "FAIL $file: want ok under --kit, got exit $status: $out"
    fail=$((fail + 1))
  fi
done <test/check/expect.txt
echo "kit-only refusals: $kit_only checked"

# A refl refusal shows both normal forms.
build/langc $(kit test/check/kit.txt bad-refl.lang) check test/check/bad-refl.lang 2>"$tmp/err" >/dev/null || true
if ! rg -q -F 'the path at i1 is some 1201, but the type needs some 1200' "$tmp/err"; then
  echo "FAIL bad-refl.lang: the message does not show both normal forms"
  fail=$((fail + 1))
fi

# A REFUSE_TIME refusal names the op-count form (S3, O3).
time_hint=0
build/langc $(kit test/check/kit.txt time-block.lang) check test/check/time-block.lang 2>"$tmp/err" >/dev/null || true
time_hint=$((time_hint + 1))
if ! rg -q -F 'count operations: a position is `len r` (an OpPos) and a deadline is a count of later operations' "$tmp/err"; then
  echo "FAIL time-block.lang: the message does not name the op-count form"
  fail=$((fail + 1))
fi
echo "time hint: $time_hint checked"

# Each line of test/eval/expect.txt is `FILE NAME [ARGS] => OUTPUT`. `langc eval`
# must exit 0 and print OUTPUT.
evals=0
while IFS= read -r line; do
  want=${line#* => }
  set -- ${line%% => *}
  file=$1
  name=$2
  shift 2
  status=0
  got=$(build/langc $(kit examples/kit.txt "$file.lang") eval "examples/$file.lang" "$name" "$@" 2>"$tmp/err") || status=$?
  first=$(head -n 1 "$tmp/err")
  has_err=0
  [ ! -s "$tmp/err" ] || has_err=1
  evals=$((evals + 1))
  case "$status|$got|$has_err" in
    "0|$want|0") ;;
    *) echo "FAIL eval $file $name: want $want, got exit $status: $got $first"; fail=$((fail + 1)) ;;
  esac
done <test/eval/expect.txt
echo "evals: $evals checked"

# eval_refused STATUS CODE NAME [ARGS]: `langc --kit eval examples/entries.lang
# NAME ARGS` must exit STATUS and print `langc: CODE: NAME: ...` on stderr.
eval_refused() {
  want_status=$1
  code=$2
  shift 2
  status=0
  build/langc --kit eval examples/entries.lang "$@" 2>"$tmp/err" >/dev/null || status=$?
  case "$status:$(head -n 1 "$tmp/err")" in
    "$want_status:langc: $code: $1: "*) ;;
    *) echo "FAIL eval $*: want exit $want_status and $code, got exit $status"; fail=$((fail + 1)) ;;
  esac
}
eval_refused 2 EVAL_ARGS addPrice 1
eval_refused 2 EVAL_ARGS addPrice x 1
eval_refused 2 EVAL_ARGS addPrice 18446744073709551616 1
eval_refused 2 EVAL_ARGS pick 2 4 9
eval_refused 1 EVAL_ENTRY nope

# Each line of test/prove/expect.txt is `FILE NAME [POLL] => WORDS`. `langc prove`
# must exit 0 and print the words of WORDS, one per line (S8 B2c).
proves=0
while IFS= read -r line; do
  want=${line#* => }
  set -- ${line%% => *}
  file=$1
  name=$2
  shift 2
  status=0
  build/langc $(kit examples/kit.txt "$file.lang") prove "examples/$file.lang" "$name" "$@" >"$tmp/out" 2>"$tmp/err" || status=$?
  got=$(tr '\n' ' ' <"$tmp/out")
  got=${got% }
  proves=$((proves + 1))
  case "$status|$got" in
    "0|$want") ;;
    *) echo "FAIL prove $file $name: want $want, got exit $status: $got"; fail=$((fail + 1)) ;;
  esac
done <test/prove/expect.txt
echo "proves: $proves checked"

# prove_refused STATUS CODE FILE NAME [POLL]: `langc prove examples/FILE.lang
# NAME POLL` must exit STATUS and print `langc: CODE: NAME: ...` on stderr.
prove_refused() {
  want_status=$1
  code=$2
  file=$3
  shift 3
  status=0
  build/langc prove "examples/$file.lang" "$@" 2>"$tmp/err" >/dev/null || status=$?
  case "$status:$(head -n 1 "$tmp/err")" in
    "$want_status:langc: $code: $1: "*) ;;
    *) echo "FAIL prove $file $*: want exit $want_status and $code, got exit $status"; fail=$((fail + 1)) ;;
  esac
}
prove_refused 1 PROVE_BALLOT dao w0
prove_refused 2 EVAL_ARGS dao b0 x

# The target (S8, Q-S8-8). `langc build examples/dao.lang -o OUT` must exit 0,
# write nothing on stdout and write a valid module. build_refused FLAGS CODE
# FILE: `langc FLAGS build FILE -o OUT` must exit 1, print `langc: CODE: ...` on
# stderr and write nothing on stdout.
status=0
build/langc $(kit examples/kit.txt dao.lang) build examples/dao.lang -o "$tmp/dao.wasm" >"$tmp/out" 2>"$tmp/err" || status=$?
case "$status:$(wc -c <"$tmp/out" | tr -d ' ')" in
  0:0) wasm-validate "$tmp/dao.wasm" 2>"$tmp/err" || { echo "FAIL build dao: the module is not valid: $(head -n 1 "$tmp/err")"; fail=$((fail + 1)); } ;;
  *) echo "FAIL build dao: want exit 0 and no stdout, got exit $status: $(head -n 1 "$tmp/err")"; fail=$((fail + 1)) ;;
esac
builds=0
build_refused() {
  flags=$1
  code=$2
  file=$3
  oflag=${4:--o}
  status=0
  build/langc $flags build "$file" $oflag "$tmp/refused.out" >"$tmp/out" 2>"$tmp/err" || status=$?
  builds=$((builds + 1))
  case "$status:$(head -n 1 "$tmp/err"):$(wc -c <"$tmp/out" | tr -d ' ')" in
    "1:langc: $code: "*":0") ;;
    *) echo "FAIL build $file: want exit 1 and $code, got exit $status: $(head -n 1 "$tmp/err")"; fail=$((fail + 1)) ;;
  esac
}
build_refused --kit REFUSE_RUNTIME_CLOSURE test/ir/closure.lang
build_refused --kit REFUSE_RUNTIME_TREE test/ir/tree.lang
build_refused --kit REFUSE_TARGET_SECRET examples/zk.lang
build_refused --kit REFUSE_LOWER test/ir/transp.lang
build_refused --kit REFUSE_TARGET_SECRET test/ir/secret-key.lang
build_refused --kit REFUSE_TARGET_SECRET test/ir/secret-sign.lang
build_refused --kit REFUSE_TARGET_SECRET test/ir/secret-claim.lang
build_refused --kit REFUSE_TARGET_SECRET test/ir/secret-attest.lang
build_refused --kit REFUSE_TARGET_SECRET test/ir/secret-holder.lang
build_refused --kit REFUSE_TARGET_SECRET test/ir/secret-jrsend.lang
build_refused --kit REFUSE_TARGET_SECRET test/ir/secret-withdraw.lang
# Q-S8-27: withdrawVerify lowers to Wasm and ES, and the WGSL build refuses
# REFUSE_LOWER. build_refused checks the code only, so check the message too.
build_refused --kit REFUSE_LOWER test/ir/withdraw-verify.lang --wgsl
case "$(head -n 1 "$tmp/err")" in
  "langc: REFUSE_LOWER: ok: withdrawVerify does not lower to WGSL (HOST-LIMIT)") ;;
  *) echo "FAIL build test/ir/withdraw-verify.lang: want the WGSL message, got: $(head -n 1 "$tmp/err")"; fail=$((fail + 1)) ;;
esac
# Q-S8-36: the holderAt walk on the 1024 bag (test/ir/holder-depth.lang)
# nests deeper than 2000 levels in the lowering, so the build refuses
# EVAL_DEPTH (not a REFUSE_ code). build_refused reads the first line of
# stderr, but the 2 O24 notes of hk and bagk come first. This row wants
# exit 1, no stdout, no module and the message as the last line of stderr.
status=0
rm -f "$tmp/refused.out"
build/langc --kit build test/ir/holder-depth.lang -o "$tmp/refused.out" >"$tmp/out" 2>"$tmp/err" || status=$?
builds=$((builds + 1))
case "$status:$(wc -c <"$tmp/out" | tr -d ' '):$([ -e "$tmp/refused.out" ] && echo module || echo none):$(tail -n 1 "$tmp/err")" in
  "1:0:none:langc: EVAL_DEPTH: atk: the lowering nests deeper than 2000 levels") ;;
  *) echo "FAIL build test/ir/holder-depth.lang: want exit 1, no module and the EVAL_DEPTH message, got exit $status: $(tail -n 1 "$tmp/err")"; fail=$((fail + 1)) ;;
esac
# P5 (B8): transferVerify lowers to Wasm and ES, and the WGSL build refuses
# REFUSE_LOWER. build_refused checks the code only, so check the message too.
build_refused --kit REFUSE_LOWER test/ir/transfer-verify.lang --wgsl
case "$(head -n 1 "$tmp/err")" in
  "langc: REFUSE_LOWER: ok: transferVerify does not lower to WGSL (HOST-LIMIT)") ;;
  *) echo "FAIL build test/ir/transfer-verify.lang: want the WGSL message, got: $(head -n 1 "$tmp/err")"; fail=$((fail + 1)) ;;
esac
# Q-S8-3 N4: a heap over the bound of the static memory refuses
# REFUSE_LOWER. build_refused checks the code only, so check the message too.
build_refused --kit REFUSE_LOWER test/ir/heap.lang
case "$(head -n 1 "$tmp/err")" in
  "langc: REFUSE_LOWER: big: a heap of 3000000006 words does not lower (N4)") ;;
  *) echo "FAIL build test/ir/heap.lang: want the N4 heap message, got: $(head -n 1 "$tmp/err")"; fail=$((fail + 1)) ;;
esac
# S10: the WGSL module refuses the secret ops too.
build_refused --kit REFUSE_TARGET_SECRET test/ir/secret-key.lang --wgsl
build_refused "" REFUSE_KIT test/ir/comb.lang
build_refused --kit REFUSE_RUNTIME_TREE test/ir/tree-elim.lang
# O26 (Q-S9-11): the elim limit of family_fold refuses, not the closure
# check. build_refused checks the code only, so check the message too.
case "$(head -n 1 "$tmp/err")" in
  "langc: REFUSE_RUNTIME_TREE: t: a fold over a tree of Word must exist at run time (HOST-LIMIT)") ;;
  *) echo "FAIL build test/ir/tree-elim.lang: want the Word tree message, got: $(head -n 1 "$tmp/err")"; fail=$((fail + 1)) ;;
esac
# Q-S3-28: a loop with a run-time trip count refuses REFUSE_UNBOUNDED, and
# the message names upTo K. The check --json row is test/json/REFUSE_UNBOUNDED
# (Q-S3-34); here test/ir/unbounded.fix gives the upTo 7 form, and it builds.
build_refused --kit REFUSE_UNBOUNDED test/ir/unbounded.lang
case "$(head -n 1 "$tmp/err")" in
  "langc: REFUSE_UNBOUNDED: square: a fold over a Nat has a run-time trip count; write it as upTo K f z x with a Nat literal K (Q-S3-28)") ;;
  *) echo "FAIL build test/ir/unbounded.lang: want the upTo K message, got: $(head -n 1 "$tmp/err")"; fail=$((fail + 1)) ;;
esac
status=0
build/fronttool fix test/ir/unbounded.lang test/ir/unbounded.fix >"$tmp/unbounded-fixed.lang" 2>"$tmp/err" || status=$?
[ "$status" -ne 0 ] || build/langc --kit build "$tmp/unbounded-fixed.lang" -o "$tmp/unbounded.wasm" >"$tmp/out" 2>"$tmp/err" || status=$?
case "$status:$(wc -c <"$tmp/out" | tr -d ' ')" in
  0:0) ;;
  *) echo "FAIL build test/ir/unbounded.fix: want the fixed file to build, got exit $status: $(head -n 1 "$tmp/err")"; fail=$((fail + 1)) ;;
esac
# O24 (Q-S9-8): a def with scalar params and a family result has no target
# form. The build skips it and writes one note for it on stderr.
status=0
build/langc $(kit examples/kit.txt state-no-history.lang) build examples/state-no-history.lang -o "$tmp/note.wasm" >"$tmp/out" 2>"$tmp/err" || status=$?
case "$status:$(head -n 1 "$tmp/err")" in
  "0:langc: note: mk: a family result has no target form (O24)") ;;
  *) echo "FAIL build examples/state-no-history.lang: want exit 0 and the mk note, got exit $status: $(head -n 1 "$tmp/err")"; fail=$((fail + 1)) ;;
esac
# O24 (Q-S9-10): a Prod or a List result is a family result too. The
# pairOf def of examples/functions.lang gets the note.
status=0
build/langc $(kit examples/kit.txt functions.lang) build examples/functions.lang -o "$tmp/note.wasm" >"$tmp/out" 2>"$tmp/err" || status=$?
case "$status:$(head -n 1 "$tmp/err")" in
  "0:langc: note: pairOf: a family result has no target form (O24)") ;;
  *) echo "FAIL build examples/functions.lang: want exit 0 and the pairOf note, got exit $status: $(head -n 1 "$tmp/err")"; fail=$((fail + 1)) ;;
esac
echo "build refusals: $builds checked"

# The IR (Q-S8-7): each example lowers, with one `func` line for each `abi
# --text` line, and its module is valid (`wasm-validate`, `wasm-objdump -x`).
# An example that does not lower must refuse REFUSE_LOWER with a HOST-LIMIT
# detail (a host proof check, as examples/ballot.lang and examples/daokit.lang;
# the secp256k1 ops of part B lower), or REFUSE_TARGET_SECRET for a def that
# reaches a secret op (Q-S3-24, Q-S4-9: a closed def too, through the defs it
# names). Only examples/zk.lang, examples/claims.lang, examples/zkcast.lang,
# examples/trkit.lang, examples/transfer.lang, examples/govern.lang (the
# amend cast of S6 C4 signs with zkKey) and examples/paywd.lang (the G1
# identity of S7 C8 signs with zkKey) may refuse so. An example with a family-result def exits 0 with an O24 note.
irs=0
host_limits=0
secrets=0
wasms=0
wgsls=0
for f in examples/*.lang; do
  k=$(kit examples/kit.txt "${f#examples/}")
  status=0
  build/langc $k ir "$f" >"$tmp/ir.txt" 2>"$tmp/ir.err" || status=$?
  case "$status:$(head -n 1 "$tmp/ir.err")" in
    "1:langc: REFUSE_LOWER: "*"(HOST-LIMIT)"*) host_limits=$((host_limits + 1)); continue ;;
    "1:langc: REFUSE_TARGET_SECRET: "*)
      case $f in
        examples/zk.lang | examples/claims.lang | examples/zkcast.lang | examples/trkit.lang | examples/transfer.lang | examples/govern.lang | examples/paywd.lang) secrets=$((secrets + 1)); continue ;;
      esac
      echo "FAIL ir $f: exit $status: $(head -n 1 "$tmp/ir.err")"; fail=$((fail + 1)); continue ;;
    0: | "0:langc: note: "*"(O24)") ;;
    *) echo "FAIL ir $f: exit $status: $(head -n 1 "$tmp/ir.err")"; fail=$((fail + 1)); continue ;;
  esac
  funcs=$(rg -c '^func ' "$tmp/ir.txt" || echo 0)
  entries=$(build/langc $k abi "$f" --text | rg -c '.' || echo 0)
  [ "$funcs" -eq "$entries" ] || { echo "FAIL ir $f: $funcs funcs for $entries entries"; fail=$((fail + 1)); continue; }
  irs=$((irs + 1))
  w="$tmp/$(basename "$f" .lang).wasm"
  if ! build/langc $k build "$f" -o "$w" 2>"$tmp/w.err" || ! wasm-validate "$w" 2>>"$tmp/w.err" ||
    ! wasm-objdump -x "$w" >/dev/null 2>>"$tmp/w.err"; then
    echo "FAIL wasm $f: $(head -n 1 "$tmp/w.err")"
    fail=$((fail + 1))
    continue
  fi
  wasms=$((wasms + 1))
  # S10: the module builds with --wgsl too: exit 0, no stdout, a module file.
  g="$tmp/$(basename "$f" .lang).wgsl"
  status=0
  build/langc $k build "$f" --wgsl "$g" >"$tmp/out" 2>"$tmp/g.err" || status=$?
  if [ "$status:$(wc -c <"$tmp/out" | tr -d ' ')" != 0:0 ] || [ ! -s "$g" ]; then
    echo "FAIL wgsl $f: want exit 0, no stdout and a module, got exit $status: $(head -n 1 "$tmp/g.err")"
    fail=$((fail + 1))
    continue
  fi
  wgsls=$((wgsls + 1))
done
echo "ir: $irs lowered, $host_limits refused (HOST-LIMIT), $secrets refused (REFUSE_TARGET_SECRET)"
echo "wasm: $wasms modules valid"
echo "wgsl: $wgsls modules written"
# S10: the --wgsl usage rows of src/main.c: build takes one output flag, and
# --wgsl needs a path.
wgsl_refused=0
wgsl_usage() {
  msg=$1
  shift
  status=0
  build/langc --kit build test/ir/comb.lang "$@" >"$tmp/out" 2>"$tmp/err" || status=$?
  wgsl_refused=$((wgsl_refused + 1))
  case "$status:$(head -n 1 "$tmp/err"):$(wc -c <"$tmp/out" | tr -d ' ')" in
    "2:langc: USAGE: -: $msg:0") ;;
    *) echo "FAIL build $*: want exit 2 and USAGE, got exit $status: $(head -n 1 "$tmp/err")"; fail=$((fail + 1)) ;;
  esac
}
wgsl_usage "build takes one of -o OUT, --js OUT and --wgsl OUT" --wgsl "$tmp/a.wgsl" --js "$tmp/a.mjs"
wgsl_usage "unknown build argument '--wgsl'" --wgsl
echo "wgsl refusals: $wgsl_refused checked"

# The Wasm differential (Q-S8-7): each test/eval/expect.txt row whose NAME is an
# entry of FILE (a line of its `abi --text`) runs on the module in
# build/wasmtool and must print the row OUTPUT (a trap is a trap). Each FILE
# builds once and runs once, with its rows on stdin.
mkdir -p "$tmp/wd"
while IFS= read -r line; do
  want=${line#* => }
  set -- ${line%% => *}
  file=$1
  shift
  if [ ! -f "$tmp/wd/$file.abi" ]; then
    k=$(kit examples/kit.txt "$file.lang")
    : >"$tmp/wd/$file.abi"
    build/langc $k build "examples/$file.lang" -o "$tmp/wd/$file.wasm" 2>/dev/null &&
      build/langc $k abi "examples/$file.lang" --text >"$tmp/wd/$file.abi" 2>/dev/null || :
  fi
  rg -q "^$1 \(" "$tmp/wd/$file.abi" || continue
  echo "$*" >>"$tmp/wd/$file.in"
  echo "$want" >>"$tmp/wd/$file.want"
done <test/eval/expect.txt
rows=0
mismatches=0
for in_file in "$tmp"/wd/*.in; do
  file=$(basename "$in_file" .in)
  if ! build/wasmtool "$tmp/wd/$file.wasm" "$tmp/wd/$file.abi" <"$in_file" >"$tmp/wd/$file.got" 2>"$tmp/w.err"; then
    echo "FAIL wasmtool $file: $(head -n 1 "$tmp/w.err")"
    fail=$((fail + 1))
    continue
  fi
  rows=$((rows + $(rg -c '.' "$in_file")))
  paste -d '|' "$in_file" "$tmp/wd/$file.want" "$tmp/wd/$file.got" | awk -F '|' '$2 != $3' >"$tmp/bad.txt"
  bad=$(rg -c '.' "$tmp/bad.txt" || echo 0)
  mismatches=$((mismatches + bad))
  [ "$bad" -eq 0 ] || { echo "FAIL wasm differential $file: $bad mismatches, first $(head -n 1 "$tmp/bad.txt")"; fail=$((fail + 1)); }
done
echo "wasm differential: $rows rows, $mismatches mismatches"
# A Flag argument larger than 1 gives ARG (Q-S8-7; langc eval refuses it with EVAL_ARGS).
got=$(echo 'pick 2 4 9' | build/wasmtool "$tmp/wd/entries.wasm" "$tmp/wd/entries.abi" 2>&1) || true
[ "$got" = "ARG" ] || { echo "FAIL wasm pick 2 4 9: want ARG, got $got"; fail=$((fail + 1)); }

# The JS differential (S9 B4, Q-S9-6 (a)): the same rows run on the ES module
# of `langc build FILE --js` in build/jstool (under node) and must print the
# same OUTPUT.
js_rows=0
js_bad=0
js_count_rows=0
js_count_bad=0
for in_file in "$tmp"/wd/*.in; do
  file=$(basename "$in_file" .in)
  k=$(kit examples/kit.txt "$file.lang")
  if ! build/langc $k build "examples/$file.lang" --js "$tmp/wd/$file.mjs" 2>"$tmp/w.err" ||
    ! build/jstool "$tmp/wd/$file.mjs" "$tmp/wd/$file.abi" <"$in_file" >"$tmp/wd/$file.js" 2>>"$tmp/w.err"; then
    echo "FAIL jstool $file: $(head -n 1 "$tmp/w.err")"
    fail=$((fail + 1))
    continue
  fi
  js_rows=$((js_rows + $(rg -c '.' "$in_file")))
  paste -d '|' "$in_file" "$tmp/wd/$file.want" "$tmp/wd/$file.js" | awk -F '|' '$2 != $3' >"$tmp/bad.txt"
  bad=$(rg -c '.' "$tmp/bad.txt" || echo 0)
  js_bad=$((js_bad + bad))
  [ "$bad" -eq 0 ] || { echo "FAIL js differential $file: $bad mismatches, first $(head -n 1 "$tmp/bad.txt")"; fail=$((fail + 1)); }
  got=$(count_rows "examples/$file.lang" "$tmp/wd" "$file" $k)
  if [ "$got" = build ]; then
    echo "FAIL js count $file: $(head -n 1 "$tmp/c.err")"
    fail=$((fail + 1))
    continue
  fi
  cr_rows=${got%% *}
  got=${got#* }
  cr_bad=${got%% *}
  js_count_rows=$((js_count_rows + cr_rows))
  js_count_bad=$((js_count_bad + cr_bad))
  [ "$cr_bad" -eq 0 ] || { echo "FAIL js count $file: $cr_bad bad, first $(head -n 1 "$tmp/wd/$file.cbad")"; fail=$((fail + 1)); }
done
echo "js differential: $js_rows rows, $js_bad mismatches"
echo "js count: $js_count_rows rows, $js_count_bad bad"
got=$(echo 'pick 2 4 9' | build/jstool "$tmp/wd/entries.mjs" "$tmp/wd/entries.abi" 2>&1) || true
[ "$got" = "ARG" ] || { echo "FAIL js pick 2 4 9: want ARG, got $got"; fail=$((fail + 1)); }

# The test/ir modules (A7 piece (c)): each builds in --kit mode to a valid
# module. Each row `FILE NAME ARGS` below runs on the module of test/ir/FILE.lang
# in build/wasmtool and must print the `langc --kit eval` output (a trap is
# `trap`).
mkdir -p "$tmp/ir"
ir_mods=0
for m in comb lazy spine proof-trap memname issue-verify attest-verify withdraw-verify transfer-verify bag1024; do
  if ! build/langc --kit build "test/ir/$m.lang" -o "$tmp/ir/$m.wasm" 2>"$tmp/w.err" ||
    ! wasm-validate "$tmp/ir/$m.wasm" 2>>"$tmp/w.err" ||
    ! build/langc --kit abi "test/ir/$m.lang" --text >"$tmp/ir/$m.abi" 2>>"$tmp/w.err"; then
    echo "FAIL test/ir wasm $m: $(head -n 1 "$tmp/w.err")"
    fail=$((fail + 1))
    continue
  fi
  ir_mods=$((ir_mods + 1))
  : >"$tmp/ir/$m.in"
  : >"$tmp/ir/$m.want"
done
while IFS= read -r line; do
  set -- $line
  m=$1
  shift
  [ -f "$tmp/ir/$m.in" ] || continue
  want=$(build/langc --kit eval "test/ir/$m.lang" "$@" 2>/dev/null) || :
  [ -n "$want" ] || { echo "FAIL test/ir eval $line: no output"; fail=$((fail + 1)); continue; }
  echo "$*" >>"$tmp/ir/$m.in"
  echo "$want" >>"$tmp/ir/$m.want"
done <<'EOF'
comb snocChain 0
comb snocChain 1
comb snocChain 3
comb snocRun 0
comb snocRun 1
comb snocRun 3
comb consChain 0
comb consChain 1
comb consChain 3
comb consRun 0
comb consRun 1
comb consRun 3
comb mixChain 0
comb mixChain 1
comb mixChain 3
comb mixRun 0
comb mixRun 1
comb mixRun 3
comb snocLen 0
comb snocLen 1
comb snocLen 3
lazy lazy 3
spine trapHead 3
spine trapSum 3
spine filterTrap 3
spine bindTrap 3
proof-trap proofMove 0
proof-trap proofMove 18446744073709551615
proof-trap proofSymm 18446744073709551615
proof-trap proofTrans 18446744073709551615
proof-trap proofCong 18446744073709551615
memname memory
issue-verify vz 10759210715454867231877336886846517845312708813818011800782320276251449802981u
issue-verify vz 10759210715454867231877336886846517845312708813818011800782320276251449802982u
issue-verify vz 0u
issue-verify vz 115792089237316195423570985008687907852837564279074904382605163141518161494337u
issue-verify vz 115792089237316195423570985008687907853269984665640564039457584007913129639935u
issue-verify va 35062279437171282596050629070735700072639923391014583143766716849643692989431u
issue-verify va 35062279437171282596050629070735700072639923391014583143766716849643692989432u
issue-verify va 0u
issue-verify va 115792089237316195423570985008687907853269984665640564039457584007908834671663u
issue-verify va 115792089237316195423570985008687907853269984665640564039457584007913129639935u
issue-verify vm8 10759210715454867231877336886846517845312708813818011800782320276251449802981u
issue-verify vm0 105832500000366686126513841120619024399407881392553655522432285288073368893586u
issue-verify vm0 105832500000366686126513841120619024399407881392553655522432285288073368893587u
issue-verify vn9 35062279437171282596050629070735700072639923391014583143766716849643692989431u
issue-verify vn11 0u
issue-verify vn11 10759210715454867231877336886846517845312708813818011800782320276251449802981u
issue-verify vko 0u
issue-verify vko 100862081773581120499222301212791081193994281440454033593790618293887747050036u
issue-verify vkh 0u
issue-verify vkh 1u
issue-verify vkh 115792089237316195423570985008687907853269984665640564039457584007908834671663u
issue-verify vc 54253141229701892740678441538472478126466317116121420289154985997987923385716u
issue-verify vc 54253141229701892740678441538472478126466317116121420289154985997987923385717u
issue-verify vc 115792089237316195423570985008687907853269984665640564039457584007908834671663u
issue-verify vc 115792089237316195423570985008687907853269984665640564039457584007913129639935u
attest-verify v2s 71265592852439088978420232507440119336969720815783799636518122779530942521005u
attest-verify v2s 71265592852439088978420232507440119336969720815783799636518122779530942521006u
attest-verify v2s 0u
attest-verify v2s 115792089237316195423570985008687907852837564279074904382605163141518161494337u
attest-verify v2s 115792089237316195423570985008687907853269984665640564039457584007913129639935u
attest-verify v2c 14121108961765325475935132116446271014595882133811077218824500418693476021389u
attest-verify v2c 14121108961765325475935132116446271014595882133811077218824500418693476021390u
attest-verify v2c 115792089237316195423570985008687907853269984665640564039457584007913129639935u
attest-verify v2k 48009403158434809478298710137233764200988036438868259456275038304221065242292u
attest-verify v2k 48009403158434809478298710137233764200988036438868259456275038304221065242293u
attest-verify v2k 0u
attest-verify v2k 115792089237316195423570985008687907853269984665640564039457584007908834671663u
attest-verify v2k 115792089237316195423570985008687907853269984665640564039457584007913129639935u
attest-verify v2m 44696466249196887777481833878429299174322536392992392205634510941844339159869u
attest-verify v2m 44696466249196887777481833878429299174322536392992392205634510941844339159870u
attest-verify v2m 115792089237316195423570985008687907853269984665640564039457584007908834671663u
attest-verify v200 0u
attest-verify v200 1u
attest-verify v200 115792089237316195423570985008687907853269984665640564039457584007908834671663u
attest-verify v2r 55066263022277343669578718895168534326250603453777594175500187360389116729240u
attest-verify v2r 55066263022277343669578718895168534326250603453777594175500187360389116729241u
attest-verify v2r 115792089237316195423570985008687907853269984665640564039457584007908834671663u
attest-verify v2r 115792089237316195423570985008687907853269984665640564039457584007913129639935u
attest-verify v2z 0u
attest-verify v2z 1u
attest-verify v2zs 0u
attest-verify v2zs 1u
attest-verify v2n 49618969110780122258819871818221765269038836941366886600293911109636132149115u
attest-verify v2n 0u
attest-verify v2l 0u
attest-verify v2l 71265592852439088978420232507440119336969720815783799636518122779530942521005u
attest-verify v0 48009403158434809478298710137233764200988036438868259456275038304221065242292u
attest-verify v0 0u
attest-verify v1s 92209745376441754765850393498925718993366943089527067458675377871439987964140u
attest-verify v1s 92209745376441754765850393498925718993366943089527067458675377871439987964141u
attest-verify v1s 115792089237316195423570985008687907852837564279074904382605163141518161494337u
attest-verify v28s 56536691190730841183752368768954143162805440158008278995265568109046926654533u
attest-verify v28s 56536691190730841183752368768954143162805440158008278995265568109046926654534u
attest-verify v28s 115792089237316195423570985008687907852837564279074904382605163141518161494337u
attest-verify v28n 23965553744959645310344068650604351841176764670458888370228541308762941543155u
attest-verify v28l 0u
attest-verify v29 88789495143442116025801984763830218317601484092281216301053684219001740388315u
attest-verify v29 0u
attest-verify v29t 56536691190730841183752368768954143162805440158008278995265568109046926654533u
attest-verify v29m 95674064106947776571119569332896229328764294068924322990794687490889628823026u
attest-verify v28z 0u
attest-verify v28z 1u
withdraw-verify ok 3u
withdraw-verify ok 340282366920938463463374607431768211456u
withdraw-verify keyx 21505829891763648114329055987619236494102133314575206970830385799158076338149u
withdraw-verify f2573 40352516724488128786602981578208480139583434020001559256644750261983754227746u
withdraw-verify short 3u
transfer-verify ok 83171967071150358985866038151071862739706381731040805772715781802460730425109u
transfer-verify ok 83171967071150358985866038151071862739706381731040805772715781802460730425108u
transfer-verify zw 35624745970848103714088961184274129198232750449778654297452219920517638024189u
transfer-verify short 83171967071150358985866038151071862739706381731040805772715781802460730425109u
transfer-verify closed 83171967071150358985866038151071862739706381731040805772715781802460730425109u
bag1024 cons1025 3
bag1024 lenk 3
bag1024 fullk 3
bag1024 okk 3
bag1024 cons1025 0
EOF
ir_rows=0
ir_bad=0
for m in comb lazy spine proof-trap memname issue-verify attest-verify withdraw-verify transfer-verify bag1024; do
  [ -f "$tmp/ir/$m.in" ] || continue
  if ! build/wasmtool "$tmp/ir/$m.wasm" "$tmp/ir/$m.abi" <"$tmp/ir/$m.in" >"$tmp/ir/$m.got" 2>"$tmp/w.err"; then
    echo "FAIL wasmtool test/ir/$m.lang: $(head -n 1 "$tmp/w.err")"
    fail=$((fail + 1))
    continue
  fi
  ir_rows=$((ir_rows + $(rg -c '.' "$tmp/ir/$m.in" || echo 0)))
  paste -d '|' "$tmp/ir/$m.in" "$tmp/ir/$m.want" "$tmp/ir/$m.got" | awk -F '|' '$2 != $3' >"$tmp/bad.txt"
  bad=$(rg -c '.' "$tmp/bad.txt" || echo 0)
  ir_bad=$((ir_bad + bad))
  [ "$bad" -eq 0 ] || { echo "FAIL test/ir wasm $m: $bad mismatches, first $(head -n 1 "$tmp/bad.txt")"; fail=$((fail + 1)); }
done
echo "test/ir wasm: $ir_mods modules valid, $ir_rows rows, $ir_bad mismatches"

# The test/ir rows on the JS module (S9 B4): build/jstool must print the same
# `langc --kit eval` output.
ir_js_rows=0
ir_js_bad=0
ir_js_count_rows=0
ir_js_count_bad=0
for m in comb lazy spine proof-trap memname issue-verify attest-verify withdraw-verify transfer-verify bag1024; do
  [ -f "$tmp/ir/$m.in" ] || continue
  if ! build/langc --kit build "test/ir/$m.lang" --js "$tmp/ir/$m.mjs" 2>"$tmp/w.err"; then
    echo "FAIL jstool test/ir/$m.lang: $(head -n 1 "$tmp/w.err")"
    fail=$((fail + 1))
    continue
  fi
  # Q-S8-33 (b): the count module replaces the shipped run of the 2 large
  # modules. Their shipped module still builds (the no-trap scan reads it), and
  # the results of their count module compare with the eval rows below. The
  # other modules run both.
  case $m in
  withdraw-verify | transfer-verify) rm -f "$tmp/ir/$m.js" ;;
  *)
    if ! build/jstool "$tmp/ir/$m.mjs" "$tmp/ir/$m.abi" <"$tmp/ir/$m.in" >"$tmp/ir/$m.js" 2>"$tmp/w.err"; then
      echo "FAIL jstool test/ir/$m.lang: $(head -n 1 "$tmp/w.err")"
      fail=$((fail + 1))
      continue
    fi
    ;;
  esac
  got=$(count_rows "test/ir/$m.lang" "$tmp/ir" "$m" --kit)
  if [ "$got" = build ]; then
    echo "FAIL test/ir js count $m: $(head -n 1 "$tmp/c.err")"
    fail=$((fail + 1))
    continue
  fi
  cr_rows=${got%% *}
  got=${got#* }
  cr_bad=${got%% *}
  ir_js_count_rows=$((ir_js_count_rows + cr_rows))
  ir_js_count_bad=$((ir_js_count_bad + cr_bad))
  [ "$cr_bad" -eq 0 ] || { echo "FAIL test/ir js count $m: $cr_bad bad, first $(head -n 1 "$tmp/ir/$m.cbad")"; fail=$((fail + 1)); }
  [ -f "$tmp/ir/$m.js" ] || cp "$tmp/ir/$m.cres" "$tmp/ir/$m.js"
  ir_js_rows=$((ir_js_rows + $(rg -c '.' "$tmp/ir/$m.in" || echo 0)))
  paste -d '|' "$tmp/ir/$m.in" "$tmp/ir/$m.want" "$tmp/ir/$m.js" | awk -F '|' '$2 != $3' >"$tmp/bad.txt"
  bad=$(rg -c '.' "$tmp/bad.txt" || echo 0)
  ir_js_bad=$((ir_js_bad + bad))
  [ "$bad" -eq 0 ] || { echo "FAIL test/ir js $m: $bad mismatches, first $(head -n 1 "$tmp/bad.txt")"; fail=$((fail + 1)); }
done
echo "test/ir js: $ir_js_rows rows, $ir_js_bad mismatches"
echo "test/ir js count: $ir_js_count_rows rows, $ir_js_count_bad bad"

# The bound rows (S8 C4, Q-S8-34): the `check --json` bounds of each test/ir
# module must be byte-equal with test/ir/bounds.txt (one row
# `MODULE "entry":...,"memory":N` for each entry, in the module order). The
# json is the file that count_rows writes to $tmp/ir/MODULE.json.
: >"$tmp/bounds.got"
for m in comb lazy spine proof-trap memname issue-verify attest-verify withdraw-verify transfer-verify bag1024; do
  [ -f "$tmp/ir/$m.json" ] || continue
  rg -o '"entry":"[^"]*","ops":[0-9]+,"stack":[0-9]+,"memory":[0-9]+' "$tmp/ir/$m.json" |
    awk -v m="$m" '{ print m " " $0 }' >>"$tmp/bounds.got"
done
pin_rows=$(rg -c '.' "$tmp/bounds.got" || echo 0)
if cmp -s test/ir/bounds.txt "$tmp/bounds.got"; then
  echo "test/ir bounds: $pin_rows rows, 0 changed"
else
  echo "FAIL test/ir bounds: test/ir/bounds.txt differs, first: $(diff test/ir/bounds.txt "$tmp/bounds.got" | rg -m 1 '^[<>]' | cut -c1-160)"
  fail=$((fail + 1))
fi

# S8 C2 (PLAN N4, Q-S8-12): build/notraptool, the port of lien-lang
# test/notraptool.c. --self-test must print test/notrap/expect.txt. Each
# module of test/notrap/bad/expect.txt (NAME.wat through wat2wasm, NAME.mjs
# with --js, NAME.wgsl with --wgsl) must exit 1 with a FAIL line that has
# its message. Each module that this gate writes must have no trap form: the
# ir loop ($tmp/NAME.wasm and $tmp/NAME.wgsl), the examples of the wasmtool
# and jstool rows ($tmp/wd) and test/ir ($tmp/ir).
build/notraptool --self-test >"$tmp/notrap.out" 2>&1 || :
if cmp -s "$tmp/notrap.out" test/notrap/expect.txt; then
  echo "notrap regressions: $(rg -c '.' test/notrap/expect.txt) checked"
else
  echo "FAIL notrap regressions: the self-test output is not test/notrap/expect.txt"
  fail=$((fail + 1))
fi
mkdir -p "$tmp/nb"
notrap_bad=0
while read -r name msg; do
  status=0
  : >"$tmp/out"
  case "$name" in
    *.wat)
      if wat2wasm "test/notrap/bad/$name" -o "$tmp/nb/bad.wasm" 2>"$tmp/out"; then
        build/notraptool "$tmp/nb/bad.wasm" >"$tmp/out" 2>&1 || status=$?
      else
        status=9
      fi ;;
    *.mjs) build/notraptool --js "test/notrap/bad/$name" >"$tmp/out" 2>&1 || status=$? ;;
    *.wgsl) build/notraptool --wgsl "test/notrap/bad/$name" >"$tmp/out" 2>&1 || status=$? ;;
    *) status=9 ;;
  esac
  if [ "$status" -eq 1 ] && rg -q -F -- ": $msg" "$tmp/out"; then
    notrap_bad=$((notrap_bad + 1))
  else
    echo "FAIL notrap bad $name: want exit 1 and '$msg', got exit $status: $(head -n 1 "$tmp/out")"
    fail=$((fail + 1))
  fi
done <test/notrap/bad/expect.txt
echo "notrap bad: $notrap_bad of $(rg -c '.' test/notrap/bad/expect.txt) refused"
notrap_scan() { # $1 the mode name, then the tool arguments
  notrap_mode=$1
  shift
  status=0
  build/notraptool "$@" >"$tmp/notrap.scan" 2>&1 || status=$?
  rg '^FAIL' "$tmp/notrap.scan" | head -n 5
  [ "$status" -eq 0 ] || { echo "FAIL notrap $notrap_mode: exit $status"; fail=$((fail + 1)); }
  tail -n 1 "$tmp/notrap.scan"
}
notrap_scan wasm "$tmp"/*.wasm "$tmp"/wd/*.wasm "$tmp"/ir/*.wasm
notrap_scan js --js "$tmp"/wd/*.mjs "$tmp"/ir/*.mjs
notrap_scan wgsl --wgsl "$tmp"/*.wgsl

# S10: `sh test/gate.sh gpu` (make gpu-check, outside the sandbox: deno needs a
# GPU adapter) runs the WGSL modules in build/wgsltool. make check skips them.
if [ "$gpu" = gpu ]; then
  # Each module of the ir and wasm loop compiles, with one compute pipeline for
  # each entry. A module with no entry prints `valid 0 entries`.
  # Ruling Q-S10-r13: each line of test/wgsl/internal.txt is `MODULE ENTRY`, an
  # entry whose pipeline gives GPUInternalError (a Metal backend compile limit).
  # A listed module is known when its error lines are its listed entries and it
  # has no other line. A new error or a listed entry that compiles is a FAIL.
  wgsl_valid=0
  known_mods=0
  known_entries=0
  for f in examples/*.lang; do
    g="$tmp/$(basename "$f" .lang).wgsl"
    [ -f "$g" ] || continue
    awk -v f="$f" '$1 == f {print $2}' test/wgsl/internal.txt | sort >"$tmp/want.txt"
    if build/wgsltool --valid "$g" >"$tmp/v.out" 2>"$tmp/w.err" && rg -q -x 'valid [0-9]+ entries' "$tmp/v.out"; then
      if [ -s "$tmp/want.txt" ]; then
        echo "FAIL wgsl known $f: valid, but listed"
        fail=$((fail + 1))
      else
        wgsl_valid=$((wgsl_valid + 1))
      fi
    else
      { rg -o -r '$1' -x 'error (\w+) pipeline GPUInternalError' "$tmp/v.out" || true; } | sort >"$tmp/got.txt"
      other=$(rg -v -c -x 'error \w+ pipeline GPUInternalError|invalid [0-9]+ errors' "$tmp/v.out" || echo 0)
      if [ -s "$tmp/want.txt" ] && cmp -s "$tmp/want.txt" "$tmp/got.txt" && [ "$other" -eq 0 ]; then
        known_mods=$((known_mods + 1))
        known_entries=$((known_entries + $(rg -c '.' "$tmp/got.txt")))
      else
        echo "FAIL wgsl valid $f: $(head -n 1 "$tmp/v.out") $(head -n 1 "$tmp/w.err"), unlisted: $(comm -13 "$tmp/want.txt" "$tmp/got.txt" | tr '\n' ' ')"
        fail=$((fail + 1))
      fi
    fi
  done
  echo "wgsl valid: $wgsl_valid modules"
  echo "wgsl known internal: $known_mods modules, $known_entries entries"
  # The differential rows on the WGSL module must print the same OUTPUT.
  wgsl_rows=0
  wgsl_bad=0
  for in_file in "$tmp"/wd/*.in; do
    file=$(basename "$in_file" .in)
    k=$(kit examples/kit.txt "$file.lang")
    if ! build/langc $k build "examples/$file.lang" --wgsl "$tmp/wd/$file.wgsl" 2>"$tmp/w.err" ||
      ! build/wgsltool "$tmp/wd/$file.wgsl" "$tmp/wd/$file.abi" <"$in_file" >"$tmp/wd/$file.gpu" 2>>"$tmp/w.err"; then
      echo "FAIL wgsltool $file: $(head -n 1 "$tmp/w.err")"
      fail=$((fail + 1))
      continue
    fi
    wgsl_rows=$((wgsl_rows + $(rg -c '.' "$in_file")))
    paste -d '|' "$in_file" "$tmp/wd/$file.want" "$tmp/wd/$file.gpu" | awk -F '|' '$2 != $3' >"$tmp/bad.txt"
    bad=$(rg -c '.' "$tmp/bad.txt" || echo 0)
    wgsl_bad=$((wgsl_bad + bad))
    [ "$bad" -eq 0 ] || { echo "FAIL wgsl differential $file: $bad mismatches, first $(head -n 1 "$tmp/bad.txt")"; fail=$((fail + 1)); }
  done
  echo "wgsl differential: $wgsl_rows rows, $wgsl_bad mismatches"
  got=$(echo 'pick 2 4 9' | build/wgsltool "$tmp/wd/entries.wgsl" "$tmp/wd/entries.abi" 2>&1) || true
  [ "$got" = "ARG" ] || { echo "FAIL wgsl pick 2 4 9: want ARG, got $got"; fail=$((fail + 1)); }
  # The test/wasm tables of the Makefile regressions on the WGSL module.
  for t in hash secp traps; do
    if ! build/langc --kit build "test/wasm/$t.lang" --wgsl "$tmp/wgsl-$t.wgsl" 2>"$tmp/w.err" ||
      ! build/langc --kit abi "test/wasm/$t.lang" --text >"$tmp/wgsl-$t.abi" 2>>"$tmp/w.err"; then
      echo "FAIL wgsl regressions $t: $(head -n 1 "$tmp/w.err")"
      fail=$((fail + 1))
      continue
    fi
    awk -F ' => ' '/^[^#]/ {print $1}' "test/wasm/$t.txt" | build/wgsltool "$tmp/wgsl-$t.wgsl" "$tmp/wgsl-$t.abi" >"$tmp/wgsl-$t.out" 2>"$tmp/w.err" || :
    if awk -F ' => ' '/^[^#]/ {print $2}' "test/wasm/$t.txt" | cmp -s - "$tmp/wgsl-$t.out"; then
      echo "wgsl regressions: passed"
    else
      echo "FAIL wgsl regressions: the wgsltool output of test/wasm/$t.lang is not the RESULT text of test/wasm/$t.txt"
      fail=$((fail + 1))
    fi
  done
  # The test/ir rows on the WGSL module.
  ir_wgsl_rows=0
  ir_wgsl_bad=0
  for m in comb lazy spine proof-trap memname issue-verify attest-verify bag1024; do
    [ -f "$tmp/ir/$m.in" ] || continue
    if ! build/langc --kit build "test/ir/$m.lang" --wgsl "$tmp/ir/$m.wgsl" 2>"$tmp/w.err" ||
      ! build/wgsltool "$tmp/ir/$m.wgsl" "$tmp/ir/$m.abi" <"$tmp/ir/$m.in" >"$tmp/ir/$m.gpu" 2>>"$tmp/w.err"; then
      echo "FAIL wgsltool test/ir/$m.lang: $(head -n 1 "$tmp/w.err")"
      fail=$((fail + 1))
      continue
    fi
    ir_wgsl_rows=$((ir_wgsl_rows + $(rg -c '.' "$tmp/ir/$m.in" || echo 0)))
    paste -d '|' "$tmp/ir/$m.in" "$tmp/ir/$m.want" "$tmp/ir/$m.gpu" | awk -F '|' '$2 != $3' >"$tmp/bad.txt"
    bad=$(rg -c '.' "$tmp/bad.txt" || echo 0)
    ir_wgsl_bad=$((ir_wgsl_bad + bad))
    [ "$bad" -eq 0 ] || { echo "FAIL test/ir wgsl $m: $bad mismatches, first $(head -n 1 "$tmp/bad.txt")"; fail=$((fail + 1)); }
  done
  echo "test/ir wgsl: $ir_wgsl_rows rows, $ir_wgsl_bad mismatches"
  # The stored rows of test/wgsl/batch.txt (S10 B4b, rulings Q-S10-r9 and
  # Q-S10-r11). A ballot is 3 entries: sigOk, or0Ok and or1Ok. The wasm module
  # gives the want text and the WGSL module must give the same text. The or1Ok
  # row of ballot 37 and chain link 700 are bad: each gives 0. Each other row
  # gives 1.
  if build/langc --kit build test/wgsl/batch.lang -o "$tmp/batch.wasm" 2>"$tmp/w.err" &&
    build/langc --kit build test/wgsl/batch.lang --wgsl "$tmp/batch.wgsl" 2>>"$tmp/w.err" &&
    build/langc --kit abi test/wgsl/batch.lang --text >"$tmp/batch.abi" 2>>"$tmp/w.err"; then
    for n in sigOk or0Ok or1Ok linkOk; do
      rg "^$n " test/wgsl/batch.txt >"$tmp/$n.in" || :
      build/wasmtool "$tmp/batch.wasm" "$tmp/batch.abi" <"$tmp/$n.in" >"$tmp/$n.want" 2>"$tmp/w.err" || :
      build/wgsltool "$tmp/batch.wgsl" "$tmp/batch.abi" <"$tmp/$n.in" >"$tmp/$n.gpu" 2>>"$tmp/w.err" || :
      paste -d '|' "$tmp/$n.in" "$tmp/$n.want" "$tmp/$n.gpu" | awk -F '|' '$2 != $3' >"$tmp/bad.txt"
      echo "$(rg -c '.' "$tmp/$n.in" || echo 0) $(rg -c '.' "$tmp/bad.txt" || echo 0) $(rg -c -x 1 "$tmp/$n.gpu" || echo 0) $(rg -n -x 0 "$tmp/$n.gpu" | cut -d: -f1 | tr '\n' ' ')" >"$tmp/$n.sum"
    done
    read -r s_rows s_bad s_ones s_zero <"$tmp/sigOk.sum"
    read -r o0_rows o0_bad o0_ones o0_zero <"$tmp/or0Ok.sum"
    read -r o1_rows o1_bad o1_ones o1_zero <"$tmp/or1Ok.sum"
    read -r l_rows l_bad l_ones l_zero <"$tmp/linkOk.sum"
    b_rows=$((s_rows + o0_rows + o1_rows))
    b_bad=$((s_bad + o0_bad + o1_bad))
    b_ones=$((s_ones + o0_ones + o1_ones))
    if [ "$b_rows $b_bad $b_ones $s_zero|$o0_zero|$o1_zero" = "192 0 191 ||37" ]; then
      echo "wgsl batch: $b_rows rows, $b_bad mismatches"
    else
      echo "FAIL wgsl batch: $b_rows rows, $b_bad mismatches, $b_ones rows give 1, 0 at sigOk rows $s_zero, or0Ok rows $o0_zero, or1Ok rows $o1_zero (want 192 0 191, 0 at or1Ok row 37)"
      fail=$((fail + 1))
    fi
    if [ "$l_rows $l_bad $l_ones $l_zero" = "1024 0 1023 700" ]; then
      echo "wgsl chain: $l_rows links, 1 bad link found"
    else
      echo "FAIL wgsl chain: $l_rows links, $l_bad mismatches, $l_ones links give 1, 0 at links $l_zero (want 1024 0 1023 700)"
      fail=$((fail + 1))
    fi
  else
    echo "FAIL wgsl batch: $(head -n 1 "$tmp/w.err")"
    fail=$((fail + 1))
  fi
fi
json_want='{"entries":[{"name":"addX","params":["u256","u256","u256","u256"],"result":"u256","option":true},{"name":"addY","params":["u256","u256","u256","u256"],"result":"u256","option":true},{"name":"mulX","params":["u256","u256","u256"],"result":"u256","option":true},{"name":"mulY","params":["u256","u256","u256"],"result":"u256","option":true},{"name":"sigOk","params":["u256","u256","u256","u256","u256"],"result":"flag"}]}'
json_got=$(build/langc --kit abi test/wasm/secp.lang 2>&1) || json_got="exit $?"
[ "$json_got" = "$json_want" ] || { echo "FAIL json abi: build/langc --kit abi test/wasm/secp.lang is not the JSON abi line"; fail=$((fail + 1)); }
json_rc=0
build/langc --kit abi test/wasm/secp.lang --bad > /dev/null 2> "$tmp/json-abi.err" || json_rc=$?
json_msg=$(head -n 1 "$tmp/json-abi.err")
case "$json_rc $json_msg" in
  "2 "*USAGE*"abi takes only --text"*) ;;
  *) echo "FAIL json abi: --bad gave exit $json_rc, line 1: $json_msg"; fail=$((fail + 1)) ;;
esac
echo "json abi: 2 checked"

# The differential (S9 B2) in the guest. A row of test/contract/diff.txt is
# `PROG START GUEST PERMUTED BALLOTS NAME... => OUTCOME SELF`: `langc eval
# examples/PROG.lang` of GUEST (the canonical config) and of PERMUTED (a config
# outside the act orbit) must exit 0 and print the Outcome constructor of index
# OUTCOME. The rows are read on fd 3, so langc cannot read them.
# ctor_index TEXT prints the constructor index of TEXT: each leading `inr (` adds
# 1, then `inl unit` adds 0 and `inr unit` adds 1. Other text prints -.
ctor_index() {
  _text=$1
  _index=0
  while :; do
    case $_text in
      "inr ("*) _text=${_text#"inr ("}; _index=$((_index + 1)) ;;
      *) break ;;
    esac
  done
  case ${_text%%")"*} in
    "inl unit") echo "$_index" ;;
    "inr unit") echo $((_index + 1)) ;;
    *) echo - ;;
  esac
}
diffs=0
while read -r line <&3; do
  case $line in --*|'') continue ;; esac
  set -- ${line%% => *}
  want=${line#* => }
  for name in "$3" "$4"; do
    status=0
    got=$(build/langc $(kit examples/kit.txt "$1.lang") eval "examples/$1.lang" "$name" 2>"$tmp/err") || status=$?
    diffs=$((diffs + 1))
    if [ "$status" -ne 0 ] || [ "$(ctor_index "$got")" != "${want%% *}" ]; then
      echo "FAIL differential $1 $name: want outcome ${want%% *}, got exit $status: $got $(head -n 1 "$tmp/err")"
      fail=$((fail + 1))
    fi
  done
done 3<test/contract/diff.txt
echo "differential: $diffs guest evals checked"

# fmt (S11). Each repo program that `langc fmt` accepts is canonical (fmt F
# equals F byte for byte) and keeps its AST (the fronttool dump of F equals
# the dump of fmt F). fmt refuses exactly the files in test/fmt/refused.txt.
# Each test/fmt/X.lang is not canonical by design: the canonical line skips
# it, it formats to X.canon, and `langc fmt --check` accepts each X.canon and
# refuses test/fmt/check.lang at line 3.
same_dump() {
  build/fronttool dump "$1" >"$tmp/d1.txt" 2>&1 && build/fronttool dump "$2" >"$tmp/d2.txt" 2>&1 &&
    cmp -s "$tmp/d1.txt" "$tmp/d2.txt"
}
canon=0
trips=0
refused=0
for f in domain/domain.lang examples/*.lang test/*/*.lang; do
  case $f in
    test/json/LEX_* | test/json/PARSE_*) continue ;;
  esac
  status=0
  build/langc fmt "$f" >"$tmp/fmt.out" 2>"$tmp/fmt.err" || status=$?
  listed=0
  rg -qxF -- "$f" test/fmt/refused.txt && listed=1
  if [ "$status" -ne 0 ]; then
    refused=$((refused + 1))
    [ "$listed" -eq 1 ] || { echo "FAIL fmt $f: refused, exit $status: $(head -n 1 "$tmp/fmt.err")"; fail=$((fail + 1)); }
    continue
  fi
  [ "$listed" -eq 0 ] || { echo "FAIL fmt $f: accepted, but test/fmt/refused.txt lists it"; fail=$((fail + 1)); }
  case $f in
    test/fmt/*) ;;
    *)
      if cmp -s "$f" "$tmp/fmt.out"; then
        canon=$((canon + 1))
      else
        echo "FAIL fmt $f: not canonical"
        fail=$((fail + 1))
      fi
      ;;
  esac
  if same_dump "$f" "$tmp/fmt.out"; then
    trips=$((trips + 1))
  else
    echo "FAIL fmt round trip $f: the AST dump changes after fmt"
    fail=$((fail + 1))
  fi
done
want=$(($(wc -l <test/fmt/refused.txt)))
[ "$refused" -eq "$want" ] || { echo "FAIL fmt: $refused files refused, test/fmt/refused.txt lists $want"; fail=$((fail + 1)); }
echo "fmt canonical: $canon passed"
echo "fmt round trip: $trips passed"
rows=0
checks=0
for f in test/fmt/*.lang; do
  c=${f%.lang}.canon
  status=0
  build/langc fmt "$f" >"$tmp/fmt.out" 2>"$tmp/fmt.err" || status=$?
  if [ "$status" -eq 0 ] && cmp -s "$c" "$tmp/fmt.out"; then
    rows=$((rows + 1))
  else
    echo "FAIL fmt row $f: the output is not $c, exit $status"
    fail=$((fail + 1))
  fi
  status=0
  build/langc fmt "$c" --check >"$tmp/fmt.out" 2>"$tmp/fmt.err" || status=$?
  if [ "$status" -eq 0 ] && [ ! -s "$tmp/fmt.out" ] && [ ! -s "$tmp/fmt.err" ]; then
    checks=$((checks + 1))
  else
    echo "FAIL fmt --check $c: want exit 0 and no output, got exit $status"
    fail=$((fail + 1))
  fi
done
printf 'test/fmt/check.lang:3: not canonical\n' >"$tmp/want.err"
status=0
build/langc fmt test/fmt/check.lang --check >"$tmp/fmt.out" 2>"$tmp/fmt.err" || status=$?
if [ "$status" -eq 1 ] && [ ! -s "$tmp/fmt.out" ] && cmp -s "$tmp/want.err" "$tmp/fmt.err"; then
  checks=$((checks + 1))
else
  echo "FAIL fmt --check test/fmt/check.lang: want exit 1 and line 3, got exit $status: $(head -n 1 "$tmp/fmt.err")"
  fail=$((fail + 1))
fi
echo "fmt rows: $rows passed"
echo "fmt check: $checks passed"

# json rows: begin
# Each test/json/CODE.lang is a row: `langc check --json` writes
# test/json/CODE.json on stdout and nothing on stderr. It exits 0 for ok and
# kit, else it exits 1 and the first code is CODE. The rows kit,
# REFUSE_HISTORY_WRITE, REFUSE_MAP, REFUSE_PARTIAL, REFUSE_STATE and the 5
# lowering codes run with --kit, and so does the row TYPE_MISMATCH.rtable
# (its literal 1 is a kit Nat). Each REFUSE_ code of src/front/check.c and
# src/front/lower.c has a row (Q-S3-36). Each row other than ok and kit
# has test/json/CODE.fix: `fronttool fix` applies it, and `langc check --json`
# of the result exits 0 or gives a first code other than CODE.
# A row CODE.TAG.lang is one more row for CODE (the code is the file name up
# to the first dot); its .json and .fix go by the name CODE.TAG.
json_kit() {
  case $1 in
    kit | REFUSE_HISTORY_WRITE | REFUSE_LOWER | REFUSE_MAP | REFUSE_PARTIAL | REFUSE_RUNTIME_CLOSURE | REFUSE_RUNTIME_TREE | REFUSE_STATE | REFUSE_TARGET_SECRET | REFUSE_UNBOUNDED) echo --kit ;;
    TYPE_MISMATCH)
      case $2 in
        TYPE_MISMATCH.rtable) echo --kit ;;
      esac
      ;;
  esac
}
jrows=0
for f in test/json/*.lang; do
  stem=$(basename "$f" .lang)
  code=${stem%%.*}
  case $code in
    ok | kit) want=0 head='{"diagnostics":[],' ;;
    *)
      want=1 head="{\"diagnostics\":[{\"code\":\"$code\","
      [ -f "test/json/$stem.fix" ] || { echo "FAIL json $f: test/json/$stem.fix is missing"; fail=$((fail + 1)); }
      ;;
  esac
  status=0
  build/langc $(json_kit "$code" "$stem") check --json "$f" >"$tmp/json.out" 2>"$tmp/json.err" || status=$?
  first=0
  case $(cat "$tmp/json.out") in
    "$head"*) first=1 ;;
  esac
  if [ "$status" -eq "$want" ] && [ "$first" -eq 1 ] && [ ! -s "$tmp/json.err" ] && cmp -s "${f%.lang}.json" "$tmp/json.out"; then
    jrows=$((jrows + 1))
  else
    echo "FAIL json $f: want exit $want and ${f%.lang}.json, got exit $status: $(head -c 80 "$tmp/json.out")"
    fail=$((fail + 1))
  fi
done
jcodes=0
jwant=0
for code in $(rg -o --no-filename 'REFUSE_[A-Z_]+' src/front/check.c src/front/lower.c | sort -u); do
  jwant=$((jwant + 1))
  if [ -f "test/json/$code.lang" ]; then
    jcodes=$((jcodes + 1))
  else
    echo "FAIL json: no row test/json/$code.lang for the code $code"
    fail=$((fail + 1))
  fi
done
[ "$jwant" -gt 0 ] || { echo "FAIL json: no REFUSE_ code in src/front/check.c and src/front/lower.c"; fail=$((fail + 1)); }
jfixes=0
for x in test/json/*.fix; do
  stem=$(basename "$x" .fix)
  code=${stem%%.*}
  if ! build/fronttool fix "test/json/$stem.lang" "$x" >"$tmp/fixed.lang" 2>"$tmp/json.err"; then
    echo "FAIL json fix $x: fronttool fix fails: $(head -n 1 "$tmp/json.err")"
    fail=$((fail + 1))
    continue
  fi
  status=0
  build/langc $(json_kit "$code" "$stem") check --json "$tmp/fixed.lang" >"$tmp/json.out" 2>"$tmp/json.err" || status=$?
  same=0
  case $(cat "$tmp/json.out") in
    "{\"diagnostics\":[{\"code\":\"$code\","*) same=1 ;;
  esac
  if [ "$status" -eq 0 ] || { [ "$status" -eq 1 ] && [ "$same" -eq 0 ]; }; then
    jfixes=$((jfixes + 1))
  else
    echo "FAIL json fix $x: the fixed file gives exit $status: $(head -c 80 "$tmp/json.out")"
    fail=$((fail + 1))
  fi
done
echo "json rows: $jrows passed"
echo "json refusal codes: $jcodes of $jwant"
echo "json fixes: $jfixes passed"
# json rows: end
# mcp rows: begin
# Each test/mcp/NAME.in is a row (S13): build/mcptool runs `langc mcp`
# (`langc --kit mcp` when NAME starts with kit-) with NAME.in on stdin. The
# stdout equals test/mcp/NAME.out byte for byte, the stderr is empty and the
# exit is 0. The rows call each of the 5 tools. The REFUSE_ codes of src
# (src/front/codes.c aside) are the rows of src/front/codes.c, and
# `langc explain CODE` writes one line `CODE: ...` and nothing on stderr for
# each. build/mcptool exits 1 on each of 3 bad inputs: a changed .out, a
# missing .out and no rows (its FAIL lines go to $tmp, not to the log).
status=0
build/mcptool test/mcp/*.in || status=$?
[ "$status" -eq 0 ] || { echo "FAIL mcp: build/mcptool exit $status"; fail=$((fail + 1)); }
mkdir -p "$tmp/neg"
cp test/mcp/explain.in test/mcp/explain.out "$tmp/neg/"
printf x >>"$tmp/neg/explain.out"
neg=0
status=0
build/mcptool "$tmp/neg/explain.in" >"$tmp/neg.out" 2>&1 || status=$?
if [ "$status" -eq 1 ]; then
  neg=$((neg + 1))
else
  echo "FAIL mcp negative changed .out: exit $status"
  fail=$((fail + 1))
fi
cp test/mcp/explain.out "$tmp/neg/explain.out"
cp test/mcp/explain.in "$tmp/neg/kit-explain.in"
status=0
build/mcptool "$tmp/neg/explain.in" "$tmp/neg/kit-explain.in" >"$tmp/neg.out" 2>&1 || status=$?
if [ "$status" -eq 1 ]; then
  neg=$((neg + 1))
else
  echo "FAIL mcp negative missing .out: exit $status"
  fail=$((fail + 1))
fi
status=0
build/mcptool >"$tmp/neg.out" 2>&1 || status=$?
if [ "$status" -eq 1 ]; then
  neg=$((neg + 1))
else
  echo "FAIL mcp negative no rows: exit $status"
  fail=$((fail + 1))
fi
echo "mcp negatives: $neg of 3"
tools=0
for t in check fmt eval build explain; do
  if rg -qF -- "\"method\":\"tools/call\",\"params\":{\"name\":\"$t\"" test/mcp/*.in; then
    tools=$((tools + 1))
  else
    echo "FAIL mcp tools: no row of test/mcp calls $t"
    fail=$((fail + 1))
  fi
done
echo "mcp tools: $tools of 5"
rg -o --no-filename 'REFUSE_[A-Z0-9_]+' src -g '!codes.c' | sort -u >"$tmp/codes.src"
awk -F '"' '$1 == "  {" && $2 ~ /^REFUSE_/ {print $2}' src/front/codes.c | sort -u >"$tmp/codes.rows"
cmp -s "$tmp/codes.src" "$tmp/codes.rows" || { echo "FAIL explain codes: the REFUSE_ codes of src are not the rows of src/front/codes.c"; fail=$((fail + 1)); }
codes=0
for code in $(cat "$tmp/codes.src"); do
  status=0
  build/langc explain "$code" >"$tmp/explain.out" 2>"$tmp/explain.err" </dev/null || status=$?
  lines=$(($(wc -l <"$tmp/explain.out")))
  if [ "$status" -eq 0 ] && [ "$lines" -eq 1 ] && rg -q "^$code: " "$tmp/explain.out" && [ ! -s "$tmp/explain.err" ]; then
    codes=$((codes + 1))
  else
    echo "FAIL explain $code: exit $status, $lines lines"
    fail=$((fail + 1))
  fi
done
echo "explain codes: $codes of $(($(wc -l <"$tmp/codes.src")))"
# mcp rows: end

status=0
build/langc 2>/dev/null || status=$?
[ "$status" -eq 2 ] || { echo "FAIL usage: want exit 2, got $status"; fail=$((fail + 1)); }
status=0
build/langc --kit 2>/dev/null || status=$?
[ "$status" -eq 2 ] || { echo "FAIL usage --kit: want exit 2, got $status"; fail=$((fail + 1)); }
status=0
build/langc check "$tmp/missing.lang" 2>/dev/null || status=$?
[ "$status" -eq 2 ] || { echo "FAIL missing file: want exit 2, got $status"; fail=$((fail + 1)); }
rm -rf "$tmp"

if rg -n '\x{2013}|\x{2014}' . >/dev/null; then
  echo "FAIL: an em-dash or en-dash is in the kit"
  fail=$((fail + 1))
fi
echo "gate: $fail failures"
[ "$fail" -eq 0 ]
