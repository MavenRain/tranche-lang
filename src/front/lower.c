/* The lowering from checked values to the first-order IR. SHARED by the tcc
   kits.

   Each entry is evaluated on variables for its parameters. The normal form
   is the residual program, and this file writes it as IR statements. Each
   value becomes an Operand: a word and a trap flag (see src/ir.h). An
   eliminator on a heap value loads the tag and the fields of the block,
   behind a guard on the trap flag. A fold over a list or a linear family
   walks the spine into a reversed chain, then folds the chain from its last
   node. The chain is a list: a block [1, word, flag, rest, 0]. */
#include "front/lower.h"

#include <stdio.h>
#include <string.h>

#include "front/check.h"
#include "front/claimcom.h"
#include "front/group.h"
#include "front/jointr.h"
#include "front/ringsig.h"
#include "front/sigma.h"

#define IR_PRINT_DEPTH_MAX 4000u

/* A value at run time: its word and its trap flag. Each part is a constant
   or a local. */
typedef struct {
  const IrExpr *w;
  const IrExpr *f;
} Operand;

/* The two locals of a result. */
typedef struct {
  uint32_t w;
  uint32_t f;
} Slot;

typedef struct Buf Buf;

/* The operand of a value lowered earlier: one lowering per Value pointer
   (flag 51). A block finds the entries of its own memo and of the blocks it
   is placed in (flag 52), so a local set in a sibling arm is never read. */
typedef struct {
  const Value *key;
  Operand op;
} Memo;

struct Buf {
  const IrStmt **items;
  size_t count;
  size_t cap;
  const Buf *parent; /* the block this one is placed in, or NULL */
  Memo *memo;        /* open addressing on the Value pointer */
  size_t memo_count;
  size_t memo_cap; /* 0 or a power of two */
};

typedef struct {
  Machine *m;
  uint32_t local_count;
  Operand *levels; /* the operand of each de Bruijn level */
  uint32_t level_count;
  uint32_t level_cap;
} Low;

static int oom(Low *l) {
  return diag_fail(l->m->diag, "OOM", l->m->def, "out of memory");
}

static int internal(Low *l, const char *what) {
  return diag_fail(l->m->diag, "INTERNAL", l->m->def, "the lowering found %s", what);
}

/* A trap site that no type proves total (Q-S8-3 N4). */
static int refuse_trap(Low *l, const char *what) {
  return diag_fail(l->m->diag, "REFUSE_LOWER", l->m->def, "%s does not lower (N4)", what);
}

static IrExpr *expr_alloc(Low *l, IrExprKind kind) {
  IrExpr *e = arena_alloc(l->m->arena, sizeof *e);
  if (e == NULL)
    oom(l);
  else {
    e->kind = kind;
    e->nonzero = 0;
  }
  return e;
}

static const IrExpr *ex_const(Low *l, uint64_t value) {
  IrExpr *e = expr_alloc(l, IR_EXPR_CONST);
  if (e != NULL) {
    e->value = value;
    e->nonzero = value != 0;
  }
  return e;
}

static const IrExpr *ex_local(Low *l, uint32_t local) {
  IrExpr *e = expr_alloc(l, IR_EXPR_LOCAL);
  if (e != NULL)
    e->local = local;
  return e;
}

/* A DIV or MOD divisor without the nonzero bit refuses (Q-S8-3 N4). */
static const IrExpr *ex_bin(Low *l, IrOp op, const IrExpr *a, const IrExpr *b) {
  IrExpr *e;
  if (b != NULL && (op == IR_OP_DIV || op == IR_OP_MOD) && !b->nonzero) {
    refuse_trap(l, "a DIV or MOD divisor that is not NonZero");
    return NULL;
  }
  e = a == NULL || b == NULL ? NULL : expr_alloc(l, IR_EXPR_BINARY);
  if (e == NULL)
    return NULL;
  e->op = op;
  e->left = a;
  e->right = b;
  return e;
}

/* Word WORD of the block at ADDR. */
static const IrExpr *ex_load(Low *l, const IrExpr *addr, uint32_t word) {
  IrExpr *e = addr == NULL ? NULL : expr_alloc(l, IR_EXPR_LOAD);
  if (e == NULL)
    return NULL;
  e->left = addr;
  e->field = word;
  return e;
}

static int is_const(const IrExpr *e, uint64_t value) {
  return e != NULL && e->kind == IR_EXPR_CONST && e->value == value;
}

/* The or of two flags. A constant flag folds away. */
static const IrExpr *ex_or(Low *l, const IrExpr *a, const IrExpr *b) {
  if (a == NULL || b == NULL)
    return NULL;
  if (is_const(a, 0) || is_const(b, 1))
    return b;
  if (is_const(b, 0) || is_const(a, 1))
    return a;
  return ex_bin(l, IR_OP_OR, a, b);
}

static Operand operand(const IrExpr *w, const IrExpr *f) {
  Operand o;
  o.w = w;
  o.f = f;
  return o;
}

static int give(Low *l, Operand *out, uint64_t w, uint64_t f) {
  *out = operand(ex_const(l, w), ex_const(l, f));
  return out->w != NULL && out->f != NULL;
}

static Operand trap_value(Low *l) {
  return operand(ex_const(l, 0), ex_const(l, 1));
}

static uint32_t local_new(Low *l) {
  return l->local_count++;
}

static Slot slot_new(Low *l) {
  Slot s;
  s.w = local_new(l);
  s.f = local_new(l);
  return s;
}

static Operand slot_get(Low *l, Slot s) {
  return operand(ex_local(l, s.w), ex_local(l, s.f));
}

/* A new block, placed in PARENT (NULL for the body of an entry). */
static Buf buf_under(const Buf *parent) {
  Buf b;
  memset(&b, 0, sizeof b);
  b.parent = parent;
  return b;
}

/* The slot of KEY in MEMO (CAP entries): its entry, or the free slot
   where it goes. */
static size_t memo_slot(const Memo *memo, size_t cap, const Value *key) {
  size_t i = (((size_t)(uintptr_t)key) >> 4) * 2654435761u & (cap - 1u);
  while (memo[i].key != NULL && memo[i].key != key)
    i = (i + 1u) & (cap - 1u);
  return i;
}

/* 1 and OUT when V was lowered into B or into a block B is placed in. */
static int memo_find(const Buf *b, const Value *v, Operand *out) {
  size_t i;
  for (; b != NULL; b = b->parent) {
    if (b->memo_cap == 0)
      continue;
    i = memo_slot(b->memo, b->memo_cap, v);
    if (b->memo[i].key == v) {
      *out = b->memo[i].op;
      return 1;
    }
  }
  return 0;
}

static int memo_grow(Low *l, Buf *b) {
  size_t cap = b->memo_cap == 0 ? 16u : b->memo_cap * 2u;
  Memo *grown = arena_alloc(l->m->arena, cap * sizeof *grown);
  size_t i;
  if (grown == NULL)
    return oom(l);
  memset(grown, 0, cap * sizeof *grown);
  for (i = 0; i < b->memo_cap; i++)
    if (b->memo[i].key != NULL)
      grown[memo_slot(grown, cap, b->memo[i].key)] = b->memo[i];
  b->memo = grown;
  b->memo_cap = cap;
  return 1;
}

/* Records V as O in the memo of B. The table stays at most half full. */
static int memo_put(Low *l, Buf *b, const Value *v, Operand o) {
  size_t i;
  if (2u * (b->memo_count + 1u) > b->memo_cap && !memo_grow(l, b))
    return 0;
  i = memo_slot(b->memo, b->memo_cap, v);
  if (b->memo[i].key == NULL)
    b->memo_count++;
  b->memo[i].key = v;
  b->memo[i].op = o;
  return 1;
}

/* A value with a lowering worth one memo entry: an operation or a stuck
   operation (flag 54). A number, a type, a variable and a closure are
   constants, a level or a diagnostic. */
static int memo_kind(const Value *v) {
  return v->kind == VAL_OP || v->kind == VAL_STUCK;
}

static int buf_grow(Low *l, Buf *b) {
  size_t cap = b->cap == 0 ? 8u : b->cap * 2u;
  const IrStmt **grown = arena_alloc(l->m->arena, cap * sizeof *grown);
  size_t i;
  if (grown == NULL)
    return oom(l);
  for (i = 0; i < b->count; i++)
    grown[i] = b->items[i];
  b->items = grown;
  b->cap = cap;
  return 1;
}

static int emit(Low *l, Buf *b, const IrStmt *s) {
  if (s == NULL || (b->count == b->cap && !buf_grow(l, b)))
    return 0;
  b->items[b->count++] = s;
  return 1;
}

static int splice(Low *l, Buf *b, const Buf *in) {
  size_t i;
  for (i = 0; i < in->count; i++)
    if (!emit(l, b, in->items[i]))
      return 0;
  return 1;
}

static IrBlock block_of(const Buf *b) {
  IrBlock out;
  out.items = b->items;
  out.count = b->count;
  return out;
}

static IrStmt *stmt_new(Low *l, IrStmtKind kind) {
  IrStmt *s = arena_alloc(l->m->arena, sizeof *s);
  if (s == NULL)
    oom(l);
  else
    s->kind = kind;
  return s;
}

/* A statement that reads the expression E. */
static IrStmt *stmt_expr(Low *l, IrStmtKind kind, const IrExpr *e) {
  IrStmt *s = e == NULL ? NULL : stmt_new(l, kind);
  if (s != NULL)
    s->expr = e;
  return s;
}

static int set(Low *l, Buf *b, uint32_t local, const IrExpr *e) {
  IrStmt *s = stmt_expr(l, IR_STMT_SET, e);
  if (s != NULL)
    s->local = local;
  return emit(l, b, s);
}

/* DST := SRC. A flag that reads the word local of DST goes through a
   temporary, so a loop-carried update stays correct. */
static int store(Low *l, Buf *b, Slot dst, Operand src) {
  uint32_t t;
  if (src.w == NULL || src.f == NULL)
    return 0;
  if (src.f->kind == IR_EXPR_LOCAL && src.f->local == dst.w) {
    t = local_new(l);
    return set(l, b, t, src.f) && set(l, b, dst.w, src.w) && set(l, b, dst.f, ex_local(l, t));
  }
  return set(l, b, dst.w, src.w) && set(l, b, dst.f, src.f);
}

/* E as a constant or a local. The local keeps the nonzero bit of E. */
static const IrExpr *atom(Low *l, Buf *b, const IrExpr *e) {
  uint32_t t;
  IrExpr *x;
  if (e == NULL || e->kind == IR_EXPR_CONST || e->kind == IR_EXPR_LOCAL)
    return e;
  t = local_new(l);
  x = set(l, b, t, e) ? expr_alloc(l, IR_EXPR_LOCAL) : NULL;
  if (x == NULL)
    return NULL;
  x->local = t;
  x->nonzero = e->nonzero;
  return x;
}

static int atoms(Low *l, Buf *b, const IrExpr *w, const IrExpr *f, Operand *out) {
  out->w = atom(l, b, w);
  out->f = atom(l, b, f);
  return out->w != NULL && out->f != NULL;
}

static int st_if(Low *l, Buf *b, const IrExpr *c, const Buf *yes, const Buf *no) {
  IrStmt *s = stmt_expr(l, IR_STMT_IF, c);
  if (s == NULL)
    return 0;
  s->body = block_of(yes);
  s->otherwise = block_of(no);
  return emit(l, b, s);
}

/* IF F {YES} ELSE {NO}. A constant F keeps one block. */
static int if_flag(Low *l, Buf *b, const IrExpr *f, const Buf *yes, const Buf *no) {
  if (f == NULL)
    return 0;
  if (is_const(f, 0) || is_const(f, 1))
    return splice(l, b, is_const(f, 1) ? yes : no);
  return st_if(l, b, f, yes, no);
}

/* Runs IN when the trap flag F is 0. When F is 1, RES gets the trap. */
static int guard(Low *l, Buf *b, const IrExpr *f, Slot res, const Buf *in) {
  Buf trap = buf_under(b);
  return store(l, &trap, res, trap_value(l)) && if_flag(l, b, f, &trap, in);
}

static int st_switch(Low *l, Buf *b, const IrExpr *e, const IrBlock *arms, size_t n) {
  IrStmt *s = stmt_expr(l, IR_STMT_SWITCH, e);
  if (s == NULL)
    return 0;
  s->arms = arms;
  s->arm_count = n;
  return emit(l, b, s);
}

/* REPEAT COUNT {BODY}, with a new counter local. */
static int st_repeat(Low *l, Buf *b, const IrExpr *count, const Buf *body) {
  IrStmt *s = stmt_expr(l, IR_STMT_REPEAT, count);
  if (s == NULL)
    return 0;
  s->counter = local_new(l);
  s->body = block_of(body);
  return emit(l, b, s);
}

/* WHILE COUNT, LOCAL {BODY}, with a new counter local. */
static int st_while(Low *l, Buf *b, const IrExpr *count, uint32_t local, const Buf *body) {
  IrStmt *s = stmt_expr(l, IR_STMT_WHILE, count);
  if (s == NULL)
    return 0;
  s->counter = local_new(l);
  s->local = local;
  s->body = block_of(body);
  return emit(l, b, s);
}

/* A new block [TAG, the word and the flag of each of the N operands]. */
static int block_tag(Low *l, Buf *b, const IrExpr *tag, const Operand *ops, uint32_t n, Operand *out) {
  const IrExpr **fields = arena_alloc(l->m->arena, (2u * (size_t)n + 1u) * sizeof *fields);
  IrStmt *s = stmt_new(l, IR_STMT_ALLOC);
  uint32_t i;
  if (fields == NULL || s == NULL)
    return fields == NULL ? oom(l) : 0;
  fields[0] = tag;
  for (i = 0; i < n; i++) {
    fields[1u + 2u * i] = ops[i].w;
    fields[2u + 2u * i] = ops[i].f;
  }
  s->local = local_new(l);
  s->fields = fields;
  s->field_count = 2u * (size_t)n + 1u;
  *out = operand(ex_local(l, s->local), ex_const(l, 0));
  return fields[0] != NULL && emit(l, b, s);
}

/* block_tag with the constant TAG. */
static int block_ops(Low *l, Buf *b, uint64_t tag, const Operand *ops, uint32_t n, Operand *out) {
  return block_tag(l, b, ex_const(l, tag), ops, n, out);
}

/* The operand of field K of the block at ADDR. */
static Operand field_op(Low *l, const IrExpr *addr, uint32_t k) {
  return operand(ex_load(l, addr, 1u + 2u * k), ex_load(l, addr, 2u + 2u * k));
}

/* Field K of the block at ADDR, copied into two new locals. */
static int field_at(Low *l, Buf *b, const IrExpr *addr, uint32_t k, Operand *out) {
  Slot s = slot_new(l);
  *out = slot_get(l, s);
  return store(l, b, s, field_op(l, addr, k));
}

static int levels_grow(Low *l) {
  uint32_t cap = l->level_cap == 0 ? 64u : l->level_cap * 2u;
  Operand *grown = arena_alloc(l->m->arena, (size_t)cap * sizeof *grown);
  uint32_t i;
  if (grown == NULL)
    return oom(l);
  for (i = 0; i < l->level_count; i++)
    grown[i] = l->levels[i];
  l->levels = grown;
  l->level_cap = cap;
  return 1;
}

/* A new variable that stands for the operand O. Levels never repeat. */
static const Value *fresh(Low *l, Operand o) {
  if (o.w == NULL || o.f == NULL || (l->level_count == l->level_cap && !levels_grow(l)))
    return NULL;
  l->levels[l->level_count] = o;
  return val_var(l->m, l->level_count++);
}

static const char *op_word(IrOp op) {
  switch (op) {
  case IR_OP_ADD:
    return "add";
  case IR_OP_SUB:
    return "sub";
  case IR_OP_MUL:
    return "mul";
  case IR_OP_EQ:
    return "eq";
  case IR_OP_LE:
    return "le";
  case IR_OP_ADD_CARRY:
    return "add_carry";
  case IR_OP_MUL_CARRY:
    return "mul_carry";
  case IR_OP_OR:
    return "or";
  case IR_OP_LT:
    return "lt";
  case IR_OP_DIV:
    return "div";
  case IR_OP_MOD:
    return "mod";
  }
  return "?";
}

static const char *call_word(IrCall call) {
  switch (call) {
  case IR_CALL_U256_ADD:
    return "u256_add";
  case IR_CALL_U256_SUB:
    return "u256_sub";
  case IR_CALL_U256_MUL:
    return "u256_mul";
  case IR_CALL_MUL_DIV:
    return "mul_div";
  case IR_CALL_U256_LE:
    return "u256_le";
  case IR_CALL_U256_EQ:
    return "u256_eq";
  case IR_CALL_TO_U256:
    return "to_u256";
  case IR_CALL_SHA256:
    return "sha256";
  case IR_CALL_FE_ADD:
    return "fe_add";
  case IR_CALL_FE_SUB:
    return "fe_sub";
  case IR_CALL_FE_MUL:
    return "fe_mul";
  case IR_CALL_FE_INV:
    return "fe_inv";
  case IR_CALL_EC_ADD:
    return "ec_add";
  case IR_CALL_EC_MUL:
    return "ec_mul";
  case IR_CALL_SCHNORR_OK:
    return "schnorr_ok";
  }
  return "?";
}

static void expr_print(const IrExpr *e, FILE *out, unsigned depth) {
  size_t i;
  if (depth > IR_PRINT_DEPTH_MAX) {
    fputs("...", out);
    return;
  }
  switch (e->kind) {
  case IR_EXPR_CONST:
    fprintf(out, "%llu", (unsigned long long)e->value);
    return;
  case IR_EXPR_LOCAL:
    fprintf(out, "l%u", (unsigned)e->local);
    return;
  case IR_EXPR_BINARY:
    fprintf(out, "%s(", op_word(e->op));
    expr_print(e->left, out, depth + 1u);
    fputs(", ", out);
    expr_print(e->right, out, depth + 1u);
    fputs(")", out);
    return;
  case IR_EXPR_LOAD:
    fputs("load(", out);
    expr_print(e->left, out, depth + 1u);
    fprintf(out, ", %u)", (unsigned)e->field);
    return;
  case IR_EXPR_CALL:
    fprintf(out, "call %s(", call_word(e->call));
    for (i = 0; i < e->arg_count; i++) {
      fputs(i == 0 ? "" : ", ", out);
      expr_print(e->args[i], out, depth + 1u);
    }
    fputs(")", out);
    return;
  }
}

static void block_print(const IrBlock *b, FILE *out, unsigned depth);

static void indent(FILE *out, unsigned depth) {
  fprintf(out, "%*s", (int)(2u * depth), "");
}

static void close_print(FILE *out, unsigned depth) {
  indent(out, depth);
  fputs("}\n", out);
}

static void stmt_print(const IrStmt *s, FILE *out, unsigned depth) {
  size_t i;
  indent(out, depth);
  switch (s->kind) {
  case IR_STMT_SET:
    fprintf(out, "l%u := ", (unsigned)s->local);
    expr_print(s->expr, out, 0);
    fputs("\n", out);
    return;
  case IR_STMT_STATUS:
    fputs("status := ", out);
    expr_print(s->expr, out, 0);
    fputs("\n", out);
    return;
  case IR_STMT_ALLOC:
    fprintf(out, "l%u := alloc [", (unsigned)s->local);
    for (i = 0; i < s->field_count; i++) {
      fputs(i == 0 ? "" : ", ", out);
      expr_print(s->fields[i], out, 0);
    }
    fputs("]\n", out);
    return;
  case IR_STMT_IF:
    fputs("if ", out);
    expr_print(s->expr, out, 0);
    fputs(" {\n", out);
    block_print(&s->body, out, depth + 1u);
    indent(out, depth);
    fputs("} else {\n", out);
    block_print(&s->otherwise, out, depth + 1u);
    close_print(out, depth);
    return;
  case IR_STMT_SWITCH:
    fputs("switch ", out);
    expr_print(s->expr, out, 0);
    fputs(" {\n", out);
    for (i = 0; i < s->arm_count; i++) {
      indent(out, depth);
      fprintf(out, "arm %zu:\n", i);
      block_print(&s->arms[i], out, depth + 1u);
    }
    close_print(out, depth);
    return;
  case IR_STMT_REPEAT:
    fprintf(out, "repeat l%u := ", (unsigned)s->counter);
    expr_print(s->expr, out, 0);
    fputs(" {\n", out);
    block_print(&s->body, out, depth + 1u);
    close_print(out, depth);
    return;
  case IR_STMT_WHILE:
    fprintf(out, "while l%u := ", (unsigned)s->counter);
    expr_print(s->expr, out, 0);
    fprintf(out, ", l%u {\n", (unsigned)s->local);
    block_print(&s->body, out, depth + 1u);
    close_print(out, depth);
    return;
  case IR_STMT_LIMIT:
    fputs("trap\n", out);
    return;
  case IR_STMT_RETURN:
    fputs("return ", out);
    expr_print(s->expr, out, 0);
    fputs("\n", out);
    return;
  }
}

static void block_print(const IrBlock *b, FILE *out, unsigned depth) {
  size_t i;
  if (depth > IR_PRINT_DEPTH_MAX) {
    fputs("...\n", out);
    return;
  }
  for (i = 0; i < b->count; i++)
    stmt_print(b->items[i], out, depth);
}

static const char *scalar_word(IrScalar s) {
  switch (s) {
  case IR_SCALAR_NAT:
    return "nat";
  case IR_SCALAR_FLAG:
    return "flag";
  case IR_SCALAR_U256:
    return "u256";
  }
  return "?";
}

void ir_print(const IrProgram *prog, FILE *out) {
  size_t i;
  size_t k;
  for (i = 0; i < prog->func_count; i++) {
    const IrFunc *fn = &prog->funcs[i];
    fprintf(out, "func %s (", fn->name);
    for (k = 0; k < fn->param_count; k++)
      fprintf(out, "%s%s", k == 0 ? "" : ", ", scalar_word(fn->params[k]));
    fprintf(out, ") -> %s%s, %u locals\n", fn->status ? "option " : "", scalar_word(fn->result), (unsigned)fn->local_count);
    block_print(&fn->body, out, 1u);
  }
}

/* The bound of an entry (C9c, Q-S3-32). Each sum and product stops at
   UINT64_MAX. */
static uint64_t sat_add(uint64_t a, uint64_t b) {
  return a > UINT64_MAX - b ? UINT64_MAX : a + b;
}

static uint64_t sat_mul(uint64_t a, uint64_t b) {
  return a != 0u && b > UINT64_MAX / a ? UINT64_MAX : a * b;
}

static uint64_t max_of(uint64_t a, uint64_t b) {
  return a > b ? a : b;
}

/* The Wasm heap words of one prelude call (Q-S8-11): its result block and the
   blocks of its temps on the worst arm, the sum over the alloc sites of the
   prelude function in src/wasm.c and of the helpers it calls. */
static uint64_t call_words(IrCall c) {
  switch (c) {
  case IR_CALL_U256_ADD:
  case IR_CALL_U256_SUB:
  case IR_CALL_TO_U256:
  case IR_CALL_SHA256:
  case IR_CALL_FE_ADD:
  case IR_CALL_FE_SUB:
  case IR_CALL_U256_MUL:
    return 4u;
  case IR_CALL_FE_MUL:
    return 12u;
  case IR_CALL_MUL_DIV:
    return 8u;
  case IR_CALL_FE_INV:
    return 6148u;
  case IR_CALL_EC_ADD:
    return 6321u;
  case IR_CALL_EC_MUL:
    return 88173u;
  case IR_CALL_SCHNORR_OK:
    return 195118u;
  case IR_CALL_U256_LE:
  case IR_CALL_U256_EQ:
    return 0u;
  }
  return 0u;
}

/* OUT gets B after it: the ops and the memory add, the stack is the larger. */
static void add_bound(IrBound *out, const IrBound *b) {
  out->ops = sat_add(out->ops, b->ops);
  out->stack = max_of(out->stack, b->stack);
  out->memory = sat_add(out->memory, b->memory);
}

/* OUT gets the larger of OUT and B, each count on its own (the larger arm). */
static void max_bound(IrBound *out, const IrBound *b) {
  out->ops = max_of(out->ops, b->ops);
  out->stack = max_of(out->stack, b->stack);
  out->memory = max_of(out->memory, b->memory);
}

/* The bound of the expression E: its nodes, the most words that it holds at
   one time (the left operand before the right, the args in order) and the
   result blocks of its prelude calls. */
static void expr_bound(const IrExpr *e, IrBound *out) {
  IrBound a;
  IrBound b;
  size_t i;
  switch (e->kind) {
  case IR_EXPR_CONST:
  case IR_EXPR_LOCAL:
    out->ops = 1u;
    out->stack = 1u;
    out->memory = 0u;
    return;
  case IR_EXPR_BINARY:
    expr_bound(e->left, &a);
    expr_bound(e->right, &b);
    out->ops = sat_add(1u, sat_add(a.ops, b.ops));
    out->stack = max_of(a.stack, sat_add(1u, b.stack));
    out->memory = sat_add(a.memory, b.memory);
    return;
  case IR_EXPR_LOAD:
    expr_bound(e->left, &a);
    out->ops = sat_add(1u, a.ops);
    out->stack = a.stack;
    out->memory = a.memory;
    return;
  case IR_EXPR_CALL:
    out->ops = 1u;
    out->stack = 1u;
    out->memory = call_words(e->call);
    for (i = 0; i < e->arg_count; i++) {
      expr_bound(e->args[i], &a);
      out->ops = sat_add(out->ops, a.ops);
      out->stack = max_of(out->stack, sat_add(i, a.stack));
      out->memory = sat_add(out->memory, a.memory);
    }
    return;
  }
}

static int block_bound(Machine *m, const char *name, const IrBlock *b, IrBound *out, unsigned depth);

/* The bound of the statement S: 1 for S, its expressions, the larger arm of
   an IF or a SWITCH, and K times the body of a loop with the count K. */
static int stmt_bound(Machine *m, const char *name, const IrStmt *s, IrBound *out, unsigned depth) {
  IrBound x;
  IrBound arm;
  size_t i;
  out->ops = 1u;
  out->stack = 0u;
  out->memory = 0u;
  if (depth > IR_PRINT_DEPTH_MAX)
    return diag_fail(m->diag, "INTERNAL", name, "the IR is deeper than %u statements, thus it has no bound", IR_PRINT_DEPTH_MAX);
  switch (s->kind) {
  case IR_STMT_SET:
  case IR_STMT_RETURN:
  case IR_STMT_STATUS:
    expr_bound(s->expr, &x);
    add_bound(out, &x);
    return 1;
  case IR_STMT_ALLOC:
    out->memory = s->field_count;
    for (i = 0; i < s->field_count; i++) {
      expr_bound(s->fields[i], &x);
      add_bound(out, &x);
    }
    return 1;
  case IR_STMT_IF:
    expr_bound(s->expr, &x);
    add_bound(out, &x);
    if (!block_bound(m, name, &s->body, &x, depth + 1u) || !block_bound(m, name, &s->otherwise, &arm, depth + 1u))
      return 0;
    max_bound(&x, &arm);
    add_bound(out, &x);
    return 1;
  case IR_STMT_SWITCH:
    expr_bound(s->expr, &x);
    add_bound(out, &x);
    x.ops = 0u;
    x.stack = 0u;
    x.memory = 0u;
    for (i = 0; i < s->arm_count; i++) {
      if (!block_bound(m, name, &s->arms[i], &arm, depth + 1u))
        return 0;
      max_bound(&x, &arm);
    }
    add_bound(out, &x);
    return 1;
  case IR_STMT_REPEAT:
  case IR_STMT_WHILE:
    if (s->expr->kind != IR_EXPR_CONST)
      return diag_fail(m->diag, "INTERNAL", name, "a loop count that is not a constant has no bound (lower_program refuses it first)");
    expr_bound(s->expr, &x);
    add_bound(out, &x);
    if (!block_bound(m, name, &s->body, &x, depth + 1u))
      return 0;
    x.ops = sat_mul(s->expr->value, x.ops);
    x.memory = sat_mul(s->expr->value, x.memory);
    add_bound(out, &x);
    return 1;
  case IR_STMT_LIMIT:
    return 1;
  }
  return 1;
}

static int block_bound(Machine *m, const char *name, const IrBlock *b, IrBound *out, unsigned depth) {
  IrBound x;
  size_t i;
  out->ops = 0u;
  out->stack = 0u;
  out->memory = 0u;
  for (i = 0; i < b->count; i++) {
    if (!stmt_bound(m, name, b->items[i], &x, depth))
      return 0;
    add_bound(out, &x);
  }
  return 1;
}

/* The Wasm heap words of the entry prologue (Q-S8-11): part_entry in
   src/wasm.c copies each U256 parameter to a new block of 4 words. */
static uint64_t param_words(const IrFunc *fn) {
  uint64_t w = 0u;
  size_t k;
  for (k = 0; k < fn->param_count; k++)
    w = sat_add(w, fn->params[k] == IR_SCALAR_U256 ? 4u : 0u);
  return w;
}

int ir_bound(Machine *m, const IrFunc *fn, IrBound *out) {
  IrBound body;
  if (!block_bound(m, fn->name, &fn->body, &body, 1u))
    return 0;
  out->ops = body.ops;
  out->stack = sat_add(sat_add(fn->local_count, fn->status ? 1u : 0u), body.stack);
  out->memory = sat_add(param_words(fn), body.memory);
  return 1;
}

static int lower(Low *l, Buf *b, const Value *v, Operand *out);

static int closure(Low *l) {
  return diag_fail(l->m->diag, "REFUSE_RUNTIME_CLOSURE", l->m->def, "a function value must exist at run time (HOST-LIMIT)");
}

/* 1 when the evaluator gave a value, else 0 after a diagnostic. */
static int got(Low *l, const Value *v) {
  return v != NULL ? 1 : (l->m->diag->set ? 0 : internal(l, "a missing value"));
}

/* RES := the lowering of V. */
static int lower_into(Low *l, Buf *b, const Value *v, Slot res) {
  Operand o;
  return lower(l, b, v, &o) && store(l, b, res, o);
}

/* RES := FN applied to N new variables, one for each operand of ARGS. */
static int apply_into(Low *l, Buf *b, const Value *fn, const Operand *args, uint32_t n, Slot res) {
  const Value *v = fn;
  const Value *x;
  uint32_t i;
  for (i = 0; i < n && v != NULL; i++) {
    x = fresh(l, args[i]);
    v = x == NULL ? NULL : apply_value(l->m, v, x);
  }
  return got(l, v) && lower_into(l, b, v, res);
}

/* A new block [TAG, the lowering of each of the N values of ARGS]. */
static int ctor_block(Low *l, Buf *b, uint64_t tag, const Value *const *args, uint32_t n, Operand *out) {
  Operand *ops = arena_alloc(l->m->arena, ((size_t)n + 1u) * sizeof *ops);
  uint32_t i;
  if (ops == NULL)
    return oom(l);
  for (i = 0; i < n; i++)
    if (!lower(l, b, args[i], &ops[i]))
      return 0;
  return block_ops(l, b, tag, ops, n, out);
}

/* A constructor with fewer arguments than its arity is a function. */
static int ctor_full(Low *l, Buf *b, const Value *v, uint32_t skip, uint32_t n, uint64_t tag, Operand *out) {
  return v->argc < skip + n ? closure(l) : ctor_block(l, b, tag, v->args + skip, n, out);
}

/* The number of recursive fields of C. */
static uint32_t rec_count(const CtorInfo *c) {
  uint32_t n = 0;
  uint32_t k;
  for (k = 0; k < c->field_count; k++)
    n += c->fields[k].recursive ? 1u : 0u;
  return n;
}

/* 1 when X is a point constructor value without a recursive field (a
   leaf, as fmEta and fmUnit). */
static int static_leaf(const Low *l, const Value *x) {
  return x != NULL && (x->kind == VAL_OP || x->kind == VAL_STUCK) && x->op == OP_CTOR && l->m->ctors[x->inst].interval_count == 0u &&
         rec_count(&l->m->ctors[x->inst]) == 0u;
}

/* A binary node (cairn-lang S8, Q-S8-6, Q-S8-10): a point constructor with
   the 2 recursive fields L < R lowers when one side is a static leaf. A
   static leaf at L gives the constructor tag (the walk goes to R); a static
   leaf at R gives the pseudo tag ctor_count (the walk goes to L). Else the
   value is a tree at run time. */
static int lower_ctor(Low *l, Buf *b, const Value *v, Operand *out) {
  const CtorInfo *c = &l->m->ctors[v->inst];
  const FamilyInfo *fam = &l->m->families[c->family];
  const Value *const *f = v->args + fam->param_count;
  uint32_t side[2] = {0u, 0u};
  uint32_t n = 0;
  uint32_t k;
  if (c->interval_count != 0u || rec_count(c) != 2u || v->argc < fam->param_count + c->field_count)
    return ctor_full(l, b, v, fam->param_count, c->field_count, v->inst - fam->first_ctor, out);
  for (k = 0; k < c->field_count; k++)
    if (c->fields[k].recursive)
      side[n++] = k;
  return static_leaf(l, f[side[0]])   ? ctor_block(l, b, v->inst - fam->first_ctor, f, c->field_count, out)
         : static_leaf(l, f[side[1]]) ? ctor_block(l, b, fam->ctor_count, f, c->field_count, out)
                                      : diag_fail(l->m->diag, "REFUSE_RUNTIME_TREE", l->m->def,
                                                  "a run-time %s without a static leaf side is a tree of %s (HOST-LIMIT)", c->name, fam->name);
}

/* An operation with no form on the target (cairn-lang S8, Q-S8-1, Q-S8-5). */
static int refuse(Low *l, const char *what) {
  return diag_fail(l->m->diag, "REFUSE_LOWER", l->m->def, "%s does not lower (HOST-LIMIT)", what);
}

/* A loop whose trip count exists only at run time (tranche-lang S3,
   Q-S3-28): LOOP names the loop word. The fix is the upTo K form with a Nat
   literal K (Q-S3-30). */
static int unbounded(Low *l, const char *loop) {
  return diag_fail(l->m->diag, "REFUSE_UNBOUNDED", l->m->def, "%s has a run-time trip count; write it as upTo K f z x with a Nat literal K (Q-S3-28)", loop);
}

/* An operation on a secret key or a witness (the zk key, sign, witness, cast
   and prove operations) has no form on any target: a module is public
   (cairn-lang S9, Q-S9-3, PLAN O6). claimCom (the opening of a claim)
   and attestSign (the issuer key) are secret too (S4 C4, O4). */
static int secret_op(Low *l) {
  return diag_fail(l->m->diag, "REFUSE_TARGET_SECRET", l->m->def, "the operation touches a secret key or a witness (O6)");
}

/* Q-S4-9 (a), O6: a def that lowers (closed or entry) and reaches a secret
   op refuses as secret_op does. The evaluator reduces a closed secret op
   (zkKey 0) to a constant, so lower_canon does not see it: this walk reads
   the syntax of the def and of each def that it names, transitively. SEEN
   marks each def once, so a cycle ends. The ops are the secret_op arm of
   lower_canon. */
static int secret_kind(Op op) {
  return op == OP_ZK_KEY || op == OP_ZK_SIGN || op == OP_ZK_WITNESS || op == OP_ZK_CAST || op == OP_ZK_PROVE || op == OP_CLAIM_COM || op == OP_ATTEST_SIGN ||
         op == OP_JR_SEND || op == OP_JR_SELECT || op == OP_JR_OPEN || op == OP_TRANSFER_PROVE ||
         op == OP_ISSUE_PROVE || op == OP_WITHDRAW_PROVE;
}

/* S5 C3 (O4, Q-S5-4 (a)): the domain families HolderKey and Nonce are the
   mark. A CORE_OP that is one of them (OP_FAMILY), a constructor of one
   (OP_CTOR) or a field of one (OP_PROJ) is secret, so a def that names a
   holder key or a nonce, also only in a binder type, reaches the mark. */
static int secret_family(const Machine *m, uint32_t f) {
  return f < m->family_count &&
         (strcmp(m->families[f].name, "HolderKey") == 0 || strcmp(m->families[f].name, "Nonce") == 0);
}

static int secret_mark(const Machine *m, const Core *t) {
  if (t->op == OP_FAMILY)
    return secret_family(m, t->inst);
  if ((t->op == OP_CTOR || t->op == OP_PROJ) && t->inst < m->ctor_count)
    return secret_family(m, m->ctors[t->inst].family);
  return 0;
}

static int secret_reach(const Machine *m, unsigned char *seen, const Core *t) {
  uint32_t i;
  if (t == NULL)
    return 0;
  switch (t->kind) {
  case CORE_VAR:
  case CORE_NAT:
  case CORE_WORD:
  case CORE_UNIV:
  case CORE_IZERO:
  case CORE_IONE:
    return 0;
  case CORE_GLOBAL:
    if (seen[t->index])
      return 0;
    seen[t->index] = 1;
    return secret_reach(m, seen, m->defs[t->index].body);
  case CORE_LAM:
  case CORE_PI:
  case CORE_SIGMA:
  case CORE_APP:
  case CORE_PLAM:
  case CORE_INEG:
  case CORE_IMEET:
  case CORE_IJOIN:
    return secret_reach(m, seen, t->a) || secret_reach(m, seen, t->b);
  case CORE_PATHP:
  case CORE_PAPP:
    return secret_reach(m, seen, t->a) || secret_reach(m, seen, t->b) || secret_reach(m, seen, t->args[0]) || secret_reach(m, seen, t->args[1]);
  case CORE_OP:
    break;
  }
  if (secret_kind(t->op) || secret_mark(m, t))
    return 1;
  for (i = 0; i < t->argc; i++) {
    if (secret_reach(m, seen, t->args[i]))
      return 1;
  }
  return 0;
}

/* The prelude function CALL applied to the N words of ARGS. */
static const IrExpr *ex_call(Low *l, IrCall call, const IrExpr *const *args, size_t n) {
  IrExpr *e = expr_alloc(l, IR_EXPR_CALL);
  if (e == NULL)
    return NULL;
  e->call = call;
  e->args = args;
  e->arg_count = n;
  return e;
}

/* The constant U256 (or Addr) X: a new block of its 4 limbs, the low limb
   first (src/ir.h). */
static int word_block(Low *l, Buf *b, const U256 *x, Operand *out) {
  const IrExpr **fields = arena_alloc(l->m->arena, 4u * sizeof *fields);
  IrStmt *s = stmt_new(l, IR_STMT_ALLOC);
  uint32_t k;
  if (fields == NULL)
    return oom(l);
  for (k = 0; k < 4u; k++)
    fields[k] = ex_const(l, x->limb[k]);
  if (s == NULL || fields[0] == NULL || fields[1] == NULL || fields[2] == NULL || fields[3] == NULL)
    return 0;
  s->local = local_new(l);
  s->fields = fields;
  s->field_count = 4u;
  *out = operand(ex_local(l, s->local), ex_const(l, 0));
  return out->w != NULL && out->f != NULL && emit(l, b, s);
}

static int lower_stuck(Low *l, Buf *b, const Value *v, Operand *out);

/* A cons list (Q-S8-26): the spine walk. A tail that is a full cons value
   without a memo entry is a cell of the spine. The walk lowers the heads in
   order and then the last tail, and it builds the cells from the last to the
   first. The depth follows the element nesting and not the list length. The
   statements and the memo entries come in the order of the recursive form;
   lower() records the first cell V. */
static int lower_cons(Low *l, Buf *b, const Value *v, Operand *out) {
  const Value *c;
  const Value **cells;
  Operand *heads;
  Operand tail;
  Operand ops[2];
  uint32_t cap = 0;
  uint32_t n = 0;
  int hit = 0;
  if (v->argc < 2u)
    return closure(l);
  for (c = v; c != NULL && c->kind == VAL_OP && c->op == OP_CONS && c->argc >= 2u; c = c->args[1])
    cap++;
  cells = arena_alloc(l->m->arena, (size_t)cap * sizeof *cells);
  heads = arena_alloc(l->m->arena, (size_t)cap * sizeof *heads);
  if (cells == NULL || heads == NULL)
    return oom(l);
  c = v;
  while (!hit && n < cap) {
    cells[n] = c;
    if (!lower(l, b, c->args[0], &heads[n]))
      return 0;
    c = c->args[1];
    n++;
    hit = n < cap && memo_find(b, c, &tail);
  }
  if (!hit && !lower(l, b, c, &tail))
    return 0;
  for (; n > 0u; n--) {
    ops[0] = heads[n - 1u];
    ops[1] = tail;
    if (!block_ops(l, b, 1, ops, 2, &tail) || (n > 1u && !memo_put(l, b, cells[n - 1u], tail)))
      return 0;
  }
  *out = tail;
  return 1;
}

/* A canonical value. An eliminator here has too few arguments, so it is a
   function. */
static int lower_canon(Low *l, Buf *b, const Value *v, Operand *out) {
  switch (v->op) {
  case OP_NAT:
  case OP_FLAG:
  case OP_UNIT:
  case OP_PROD:
  case OP_SUM:
  case OP_OPTION:
  case OP_LIST:
  case OP_FAMILY:
  case OP_UNIT_VAL:
  case OP_FLAG_NO:
  case OP_U256:
  case OP_ADDR:
  case OP_INTERVAL:
  case OP_SYSTEM:
  case OP_FIN:
  case OP_NONZERO:
  case OP_NAT_IS_SET:
  case OP_U256_IS_SET:
  case OP_FLAG_IS_SET:
  case OP_OPT_NAT_IS_SET:
    return give(l, out, 0, 0);
  case OP_FLAG_YES:
    return give(l, out, 1, 0);
  case OP_PAIR:
  case OP_PACK:
    return ctor_full(l, b, v, 0, 2, 0, out);
  case OP_INL:
    return ctor_full(l, b, v, 0, 1, 0, out);
  case OP_INR:
  case OP_SOME:
    return ctor_full(l, b, v, 0, 1, 1, out);
  case OP_NONE:
  case OP_NIL:
    return ctor_full(l, b, v, 0, 0, 0, out);
  case OP_CONS:
    return lower_cons(l, b, v, out);
  case OP_CTOR:
    return lower_ctor(l, b, v, out);
  case OP_FIRST:
  case OP_SECOND:
  case OP_EITHER:
  case OP_OPTION_ELIM:
  case OP_PURE:
  case OP_MAP:
  case OP_BIND:
  case OP_FOLD_NAT:
  case OP_FOLD_LIST:
  case OP_FOLD_FAMILY:
  case OP_UPTO_NAT:
  case OP_UPTO_LIST:
  case OP_UNFOLD:
  case OP_FILTER:
  case OP_WITNESS:
  case OP_PAYLOAD:
  case OP_ELIM:
  case OP_NAT_ADD:
  case OP_NAT_SUB:
  case OP_NAT_EQ:
  case OP_NAT_LE:
  case OP_FLAG_IF:
  case OP_U256_ADD:
  case OP_U256_SUB:
  case OP_U256_LE:
  case OP_U256_EQ:
  case OP_TO_U256:
  case OP_ADDR_EQ:
  case OP_NAT_DIV:
  case OP_NAT_MOD:
  case OP_HASH:
  case OP_ADDR_TO_U256:
  case OP_PROJ:
  case OP_FIN_ADD:
  case OP_FIN_NEG:
  case OP_FIN_CASE:
  case OP_EC_ADD:
  case OP_EC_MUL:
  case OP_SCHNORR_OK:
  case OP_U256_MUL:
  case OP_MUL_DIV:
  case OP_NONZERO_OF:
  case OP_NONZERO_VALUE:
    return closure(l);
  /* No form on the target: the refusals of lower_stuck. */
  case OP_TRANSP:
  case OP_HCOMP:
  case OP_GLUE:
  case OP_GLUE_ELEM:
  case OP_UNGLUE:
  case OP_KMAP:
  case OP_KMAP_OF:
  case OP_KMAP_GET:
  case OP_KMAP_SET:
  case OP_ELG_LOG:
  case OP_ZK_HP:
  case OP_ZK_KEY:
  case OP_ZK_DIGEST:
  case OP_ZK_SIGN:
  case OP_ZK_VERIFY:
  case OP_CLAIM_COM:
  case OP_ATTEST_SIGN:
  case OP_ATTEST_VERIFY:
  case OP_JR_SEND:
  case OP_JR_SELECT:
  case OP_JR_OPEN:
  case OP_TRANSFER_PROVE:
  case OP_TRANSFER_VERIFY:
  case OP_WITHDRAW_PROVE:
  case OP_WITHDRAW_VERIFY:
  case OP_ISSUE_PROVE:
  case OP_ISSUE_VERIFY:
  case OP_RTABLE:
  case OP_ZK_BALLOT:
  case OP_ZK_WITNESS:
  case OP_ZK_CAST:
  case OP_ZK_PROVE:
  case OP_ZK_PROJECT:
  case OP_ZK_CHECK:
    return lower_stuck(l, b, v, out);
  }
  return internal(l, "an unknown operation");
}

/* A total Nat operation (natEq, natLe). */
static int nat_bin(Low *l, Buf *b, const Value *v, IrOp op, Operand *out) {
  Operand x;
  Operand y;
  if (!lower(l, b, v->args[0], &x) || !lower(l, b, v->args[1], &y))
    return 0;
  return atoms(l, b, ex_bin(l, op, x.w, y.w), ex_or(l, x.f, y.f), out);
}

/* natAdd and natSub (Q-S3-23): an Option Nat block, none on the carry of
   natAdd and when y > x for natSub. Field 0 keeps the lazy flag of the
   arguments. */
static int nat_opt(Low *l, Buf *b, const Value *v, int add, Operand *out) {
  Operand x;
  Operand y;
  Operand o;
  const IrExpr *tag;
  if (!lower(l, b, v->args[0], &x) || !lower(l, b, v->args[1], &y))
    return 0;
  tag = add ? ex_bin(l, IR_OP_EQ, ex_bin(l, IR_OP_ADD_CARRY, x.w, y.w), ex_const(l, 0)) : ex_bin(l, IR_OP_LE, y.w, x.w);
  return atoms(l, b, ex_bin(l, add ? IR_OP_ADD : IR_OP_SUB, x.w, y.w), ex_or(l, x.f, y.f), &o) &&
         block_tag(l, b, atom(l, b, tag), &o, 1, out);
}

/* Field K of the block X, behind the guard on the flag of X. */
static int field_of(Low *l, Buf *b, const Value *xv, uint32_t k, Operand *out) {
  Operand x;
  Slot res = slot_new(l);
  Buf in = buf_under(b);
  *out = slot_get(l, res);
  return lower(l, b, xv, &x) && store(l, &in, res, field_op(l, x.w, k)) && guard(l, b, x.f, res, &in);
}

static int flag_if(Low *l, Buf *b, const Value *v, Operand *out) {
  Operand c;
  Slot res = slot_new(l);
  Buf yes = buf_under(b);
  Buf no = buf_under(b);
  Buf in = buf_under(b);
  *out = slot_get(l, res);
  return lower(l, b, v->args[0], &c) && lower_into(l, &yes, v->args[1], res) && lower_into(l, &no, v->args[2], res) &&
         if_flag(l, &in, c.w, &yes, &no) && guard(l, b, c.f, res, &in);
}

/* SWITCH on the tag of X, N arms. Arm I applies CASES[I] to the FIELDS[I]
   fields of the block (with 0 fields, the arm lowers CASES[I]). */
static int cases_of(Low *l, Buf *b, const Value *xv, const Value *const *cases, const uint32_t *fields, uint32_t n, Operand *out) {
  IrBlock *arms = arena_alloc(l->m->arena, ((size_t)n + 1u) * sizeof *arms);
  Operand *args;
  Operand x;
  Slot res = slot_new(l);
  Buf in = buf_under(b);
  Buf arm;
  uint32_t i;
  uint32_t k;
  *out = slot_get(l, res);
  if (arms == NULL)
    return oom(l);
  if (!lower(l, b, xv, &x))
    return 0;
  for (i = 0; i < n; i++) {
    arm = buf_under(b);
    args = arena_alloc(l->m->arena, ((size_t)fields[i] + 1u) * sizeof *args);
    if (args == NULL)
      return oom(l);
    for (k = 0; k < fields[i]; k++)
      if (!field_at(l, &arm, x.w, k, &args[k]))
        return 0;
    if (!apply_into(l, &arm, cases[i], args, fields[i], res))
      return 0;
    arms[i] = block_of(&arm);
  }
  return st_switch(l, &in, ex_load(l, x.w, 0), arms, n) && guard(l, b, x.f, res, &in);
}

static int two_cases(Low *l, Buf *b, const Value *v, uint32_t none_fields, Operand *out) {
  uint32_t fields[2];
  fields[0] = none_fields;
  fields[1] = 1;
  return cases_of(l, b, v->args[2], v->args, fields, 2, out);
}

/* The fuel of a walk over a heap spine. */
#define SPINE_FUEL ((uint64_t)1 << 63)

/* A constructor without a recursive field. */
#define NO_REC UINT32_MAX

/* The recursive field of each list tag: nil has none, cons has the tail. */
static const uint32_t LIST_REC[2] = {NO_REC, 1u};

/* The locals of a walk: the reversed chain, its length, the trap flag of
   the end, the node where the walk stopped, and the WHILE condition. CAP is
   the literal bound of the length (SPINE_FUEL: no bound, Q-S3-28). */
typedef struct {
  uint32_t rev;
  uint32_t count;
  uint32_t end;
  uint32_t cur;
  uint32_t cont;
  uint64_t cap;
} Walk;

/* A fold into B -> B applied to B0 (cairn-lang S8, Q-S8-11): the fold V,
   the recursive field REC of each of the N tags, the binary node BIN, the
   tag NOW whose leaf goes on ACC during the walk and the tag LATER whose
   leaf goes on ACC in the pass. */
typedef struct {
  const Value *v;
  const uint32_t *rec;
  uint32_t n;
  uint32_t bin;
  uint32_t now;
  uint32_t later;
  Slot acc;
} Chain;

static int leaf_at(Low *l, Buf *b, const IrExpr *addr, const Chain *ch, uint32_t i);

static Walk walk_new(Low *l) {
  Walk w;
  w.rev = local_new(l);
  w.count = local_new(l);
  w.end = local_new(l);
  w.cur = local_new(l);
  w.cont = local_new(l);
  w.cap = SPINE_FUEL;
  return w;
}

/* REV := a new chain cell [1, the word and the flag of OP, REV, 0], and
   COUNT increases by 1. */
static int chain_push(Low *l, Buf *b, const Walk *w, Operand op) {
  Operand ops[2];
  Operand c;
  ops[0] = op;
  ops[1] = operand(ex_local(l, w->rev), ex_const(l, 0));
  return block_ops(l, b, 1, ops, 2u, &c) && set(l, b, w->rev, c.w) &&
         set(l, b, w->count, ex_bin(l, IR_OP_ADD, ex_local(l, w->count), ex_const(l, 1)));
}

/* A walk step at a node whose recursive field is K: push the node, then go
   to field K. A trapped field ends the walk. */
static int walk_step(Low *l, Buf *b, const Walk *w, uint32_t k) {
  const IrExpr *cur = ex_local(l, w->cur);
  return chain_push(l, b, w, operand(cur, ex_const(l, 0))) && set(l, b, w->end, ex_load(l, cur, 2u + 2u * k)) &&
         set(l, b, w->cur, ex_load(l, cur, 1u + 2u * k)) &&
         set(l, b, w->cont, ex_bin(l, IR_OP_EQ, ex_local(l, w->end), ex_const(l, 0)));
}

/* The spine walk of X. A node with tag I has the recursive field REC[I] (or
   NO_REC). A node with a recursive field goes on the chain. The walk stops
   at a node without one (CUR), or at a trapped field (END = 1). The cell of
   the last node comes first. With CH (a fold into B -> B), the leaf of a
   node with the tag NOW goes on ACC before the step. */
static int walk_fuel(Low *l, Buf *b, Operand x, const uint32_t *rec, uint32_t n, const Chain *ch, uint64_t cap, Walk *w) {
  IrBlock *arms = arena_alloc(l->m->arena, ((size_t)n + 1u) * sizeof *arms);
  Buf body = buf_under(b);
  Buf arm;
  uint32_t i;
  *w = walk_new(l);
  w->cap = cap;
  if (arms == NULL)
    return oom(l);
  for (i = 0; i < n; i++) {
    arm = buf_under(&body);
    if (!(rec[i] == NO_REC ? set(l, &arm, w->cont, ex_const(l, 0))
                           : (ch == NULL || i != ch->now || leaf_at(l, &arm, ex_local(l, w->cur), ch, i)) &&
                                 walk_step(l, &arm, w, rec[i])))
      return 0;
    arms[i] = block_of(&arm);
  }
  return set(l, b, w->rev, ex_const(l, 0)) && set(l, b, w->count, ex_const(l, 0)) && set(l, b, w->end, x.f) &&
         set(l, b, w->cur, x.w) && set(l, b, w->cont, ex_bin(l, IR_OP_EQ, x.f, ex_const(l, 0))) &&
         st_switch(l, &body, ex_load(l, ex_local(l, w->cur), 0), arms, n) &&
         (cap == SPINE_FUEL || set(l, &body, w->cont,
                                   ex_bin(l, IR_OP_MUL, ex_local(l, w->cont),
                                          ex_bin(l, IR_OP_LT, ex_local(l, w->count), ex_const(l, cap))))) &&
         st_while(l, b, ex_const(l, cap), w->cont, &body);
}

/* walk_fuel with no capacity: a run-time trip count, so the walk of the
   loop word LOOP refuses REFUSE_UNBOUNDED (Q-S3-31); the lowering stays
   behind the refusal. A capacity CAP < SPINE_FUEL (upTo, Q-S3-29) also stops
   the walk after CAP cells and is the literal WHILE fuel. */
static int walk_with(Low *l, Buf *b, Operand x, const uint32_t *rec, uint32_t n, const Chain *ch, const char *loop, Walk *w) {
  return unbounded(l, loop) && walk_fuel(l, b, x, rec, n, ch, SPINE_FUEL, w);
}

static int walk(Low *l, Buf *b, Operand x, const uint32_t *rec, uint32_t n, const char *loop, Walk *w) {
  return walk_with(l, b, x, rec, n, NULL, loop, w);
}

/* The start of a pass over the chain of W: NODE := word 1 of the cell. */
static int chain_node(Low *l, Buf *body, const Walk *w, const IrExpr **node) {
  uint32_t t = local_new(l);
  *node = ex_local(l, t);
  return set(l, body, t, ex_load(l, ex_local(l, w->rev), 1));
}

/* The start of a pass over a chain of list nodes: H := the head. */
static int chain_head(Low *l, Buf *body, const Walk *w, Operand *h) {
  const IrExpr *node;
  return chain_node(l, body, w, &node) && field_at(l, body, node, 0, h);
}

/* The end of a pass: REV := the next cell, and I := 0, then REPEAT CAP {IF
   I < COUNT {BODY}; I := I + 1}, CAP a literal (Q-S3-28). A walk without a
   cap (SPINE_FUEL) refuses REFUSE_UNBOUNDED. */
static int chain_end(Low *l, Buf *b, const Walk *w, Buf *body) {
  uint32_t i = local_new(l);
  Buf loop = buf_under(b);
  Buf skip = buf_under(b);
  return (w->cap != SPINE_FUEL || unbounded(l, "a pass over a spine")) &&
         set(l, body, w->rev, ex_load(l, ex_local(l, w->rev), 3)) && set(l, b, i, ex_const(l, 0)) &&
         if_flag(l, &loop, ex_bin(l, IR_OP_LT, ex_local(l, i), ex_local(l, w->count)), body, &skip) &&
         set(l, &loop, i, ex_bin(l, IR_OP_ADD, ex_local(l, i), ex_const(l, 1))) && st_repeat(l, b, ex_const(l, w->cap), &loop);
}

/* OUT := a trap tail when the flag local END is 1, else nil. */
static int tail_init(Low *l, Buf *b, uint32_t end, Slot out) {
  Operand nil;
  Buf in = buf_under(b);
  return block_ops(l, &in, 0, NULL, 0, &nil) && store(l, &in, out, nil) && guard(l, b, ex_local(l, end), out, &in);
}

/* OUT := a new cons cell [1, H, OUT]. */
static int prepend(Low *l, Buf *b, Operand h, Slot out) {
  Operand ops[2];
  Operand c;
  ops[0] = h;
  ops[1] = slot_get(l, out);
  return block_ops(l, b, 1, ops, 2u, &c) && store(l, b, out, c);
}

/* A right fold over a list. F takes the head, then the acc. A trap tail
   gives a trap base acc. */
static int fold_list(Low *l, Buf *b, const Value *v, Operand *out) {
  Operand x;
  Operand args[2];
  Walk w;
  Slot acc = slot_new(l);
  Buf in = buf_under(b);
  Buf body = buf_under(b);
  *out = slot_get(l, acc);
  args[1] = slot_get(l, acc);
  return lower(l, b, v->args[2], &x) && walk(l, b, x, LIST_REC, 2u, "a fold over a list", &w) && lower_into(l, &in, v->args[1], acc) &&
         guard(l, b, ex_local(l, w.end), acc, &in) && chain_head(l, &body, &w, &args[0]) &&
         apply_into(l, &body, v->args[0], args, 2u, acc) && chain_end(l, b, &w, &body);
}

/* MAP on a list. A trap tail stays a trap tail. */
static int list_map(Low *l, Buf *b, const Value *v, Operand *out) {
  Operand x;
  Operand h;
  Walk w;
  Slot res = slot_new(l);
  Slot ys = slot_new(l);
  Buf body = buf_under(b);
  *out = slot_get(l, res);
  return lower(l, b, v->args[1], &x) && walk(l, b, x, LIST_REC, 2u, "a map", &w) && tail_init(l, b, w.end, res) &&
         chain_head(l, &body, &w, &h) && apply_into(l, &body, v->args[0], &h, 1u, ys) &&
         prepend(l, &body, slot_get(l, ys), res) && chain_end(l, b, &w, &body);
}

/* BIND on a list. The pass starts at the last node, so a result of F with
   a trap tail drops the items after it. */
static int list_bind(Low *l, Buf *b, const Value *v, Operand *out) {
  Operand x;
  Operand h;
  Operand y;
  Walk w;
  Walk u;
  Slot res = slot_new(l);
  Slot rs = slot_new(l);
  Buf body = buf_under(b);
  Buf inner = buf_under(&body);
  Buf none = buf_under(&body);
  *out = slot_get(l, res);
  return lower(l, b, v->args[0], &x) && walk(l, b, x, LIST_REC, 2u, "a bind", &w) && tail_init(l, b, w.end, res) &&
         chain_head(l, &body, &w, &h) && apply_into(l, &body, v->args[1], &h, 1u, rs) &&
         walk(l, &body, slot_get(l, rs), LIST_REC, 2u, "a bind", &u) && guard(l, &body, ex_local(l, u.end), res, &none) &&
         chain_head(l, &inner, &u, &y) && prepend(l, &inner, y, res) && chain_end(l, &body, &u, &inner) &&
         chain_end(l, b, &w, &body);
}

/* FILTER on a list. The pass starts at the last node, so a trapped test
   drops the items after it: the result is the items kept before the first
   trapped test, then a trap tail. */
static int list_filter(Low *l, Buf *b, const Value *v, Operand *out) {
  Operand x;
  Operand h;
  Walk w;
  Slot res = slot_new(l);
  Slot ts = slot_new(l);
  Buf body = buf_under(b);
  Buf keep = buf_under(&body);
  Buf drop = buf_under(&body);
  Buf test = buf_under(&body);
  *out = slot_get(l, res);
  return lower(l, b, v->args[1], &x) && walk(l, b, x, LIST_REC, 2u, "a filter", &w) && tail_init(l, b, w.end, res) &&
         chain_head(l, &body, &w, &h) && apply_into(l, &body, v->args[0], &h, 1u, ts) && prepend(l, &keep, h, res) &&
         if_flag(l, &test, ex_local(l, ts.w), &keep, &drop) && guard(l, &body, ex_local(l, ts.f), res, &test) &&
         chain_end(l, b, &w, &body);
}

/* An unfold that ends at a trap: END := 1, and the WHILE stops. */
static int unfold_stop(Low *l, Buf *b, const Walk *w) {
  return set(l, b, w->end, ex_const(l, 1)) && set(l, b, w->cont, ex_const(l, 0));
}

/* Push the item of the pair at P, then SEED := the next seed. */
static int unfold_push(Low *l, Buf *b, const Walk *w, const IrExpr *p, Slot seed) {
  Operand a;
  Operand s;
  return field_at(l, b, p, 0, &a) && field_at(l, b, p, 1, &s) && chain_push(l, b, w, a) && store(l, b, seed, s);
}

/* The WHILE body of an unfold: R := G SEED. None stops; a trap or some(trap)
   stops with END = 1; some(pair) pushes the item. */
static int unfold_step(Low *l, Buf *b, const Walk *w, const Value *g, Slot seed) {
  Operand s = slot_get(l, seed);
  Operand p;
  Slot rs = slot_new(l);
  Operand r = slot_get(l, rs);
  Buf stop = buf_under(b);
  Buf stop_some = buf_under(b);
  Buf push = buf_under(b);
  Buf some = buf_under(b);
  Buf none = buf_under(b);
  Buf tag = buf_under(b);
  return apply_into(l, b, g, &s, 1u, rs) && field_at(l, &some, r.w, 0, &p) && unfold_push(l, &push, w, p.w, seed) &&
         unfold_stop(l, &stop_some, w) && if_flag(l, &some, p.f, &stop_some, &push) &&
         set(l, &none, w->cont, ex_const(l, 0)) && if_flag(l, &tag, ex_load(l, r.w, 0), &some, &none) &&
         unfold_stop(l, &stop, w) && if_flag(l, b, r.f, &stop, &tag);
}

/* UNFOLD G LIMIT SEED. The WHILE pushes the items on a chain, then a pass
   from the last item builds the list. A run-time LIMIT refuses
   REFUSE_UNBOUNDED (Q-S3-28); the literal LIMIT is the cap of the pass. */
static int unfold(Low *l, Buf *b, const Value *v, Operand *out) {
  Operand lim;
  Operand h;
  Walk w = walk_new(l);
  Slot res = slot_new(l);
  Slot seed = slot_new(l);
  Buf in = buf_under(b);
  Buf step = buf_under(&in);
  Buf body = buf_under(&in);
  *out = slot_get(l, res);
  if (v->args[1]->kind != VAL_NAT)
    return unbounded(l, "an unfold");
  w.cap = v->args[1]->nat;
  return lower(l, b, v->args[1], &lim) && set(l, &in, w.rev, ex_const(l, 0)) && set(l, &in, w.count, ex_const(l, 0)) &&
         set(l, &in, w.end, ex_const(l, 0)) && set(l, &in, w.cont, ex_const(l, 1)) &&
         lower_into(l, &in, v->args[2], seed) && unfold_step(l, &step, &w, v->args[0], seed) &&
         st_while(l, &in, lim.w, w.cont, &step) && tail_init(l, &in, w.end, res) &&
         field_at(l, &body, ex_local(l, w.rev), 0, &h) && prepend(l, &body, h, res) && chain_end(l, &in, &w, &body) &&
         guard(l, b, lim.f, res, &in);
}

/* A fold step at the node ADDR of the constructor C: ACC := FN applied to
   the fields, with ACC for the recursive field R. ELIM (tranche elim, eval.c
   elim_family): the recursive field and then ACC, and a path field and then
   its erased fold. */
static int fam_case(Low *l, Buf *b, const IrExpr *addr, const Value *fn, const CtorInfo *c, uint32_t r, int elim, Slot acc) {
  Operand *args = arena_alloc(l->m->arena, (2u * (size_t)c->field_count + 1u) * sizeof *args);
  uint32_t n = 0;
  uint32_t k;
  if (args == NULL)
    return oom(l);
  for (k = 0; k < c->field_count; k++) {
    if ((k != r || elim) && !field_at(l, b, addr, k, &args[n]))
      return 0;
    n += k != r || elim ? 1u : 0u;
    if (k == r)
      args[n++] = slot_get(l, acc);
    if (elim && c->fields[k].path)
      args[n++] = operand(ex_const(l, 0), ex_const(l, 0));
  }
  return apply_into(l, b, fn, args, n, acc);
}

/* The index of the first binary node of FAM (a point constructor with 2
   recursive fields), or NO_REC. */
static uint32_t bin_of(const Low *l, const FamilyInfo *fam) {
  const CtorInfo *c;
  uint32_t i;
  for (i = 0; i < fam->ctor_count; i++) {
    c = &l->m->ctors[fam->first_ctor + i];
    if (c->interval_count == 0u && rec_count(c) == 2u)
      return i;
  }
  return NO_REC;
}

static int fam_switch(Low *l, Buf *b, const IrExpr *addr, const Value *v, const uint32_t *rec, uint32_t n, int rec_arms, Slot acc);

/* A fold step at the binary node ADDR (constructor I of the fold V,
   cairn-lang S8, Q-S8-6): ACC := the case of I applied to the fields, with
   ACC for the recursive field R and, for the other recursive field (a
   static leaf), its fold by the leaf arms of V. */
static int bin_case(Low *l, Buf *b, const IrExpr *addr, const Value *v, const uint32_t *rec, uint32_t n, uint32_t i, uint32_t r, Slot acc) {
  const FamilyInfo *fam = &l->m->families[v->inst];
  const CtorInfo *c = &l->m->ctors[fam->first_ctor + i];
  Operand *args = arena_alloc(l->m->arena, ((size_t)c->field_count + 1u) * sizeof *args);
  Operand lf = operand(ex_const(l, 0), ex_const(l, 0));
  uint32_t t = local_new(l);
  Slot leaf = slot_new(l);
  Buf in = buf_under(b);
  uint32_t k;
  if (args == NULL)
    return oom(l);
  for (k = 0; k < c->field_count; k++) {
    if (k != r && !field_at(l, b, addr, k, &args[k]))
      return 0;
    lf = k != r && c->fields[k].recursive ? args[k] : lf;
    args[k] = k == r ? slot_get(l, acc) : (c->fields[k].recursive ? slot_get(l, leaf) : args[k]);
  }
  return set(l, b, t, lf.w) && fam_switch(l, &in, ex_local(l, t), v, rec, n, 0, leaf) && guard(l, b, lf.f, leaf, &in) &&
         apply_into(l, b, v->args[i], args, c->field_count, acc);
}

/* SWITCH on the tag of the node at ADDR into ACC, with N arms (arm
   ctor_count is the pseudo tag of a binary node). When REC_ARMS is 1, the
   arms of the constructors with a recursive field apply their case (else
   the arms of the ones without). The other arms are not reached; they give
   a trap. A path constructor has no run-time value: its arm is a trap. */
static int fam_switch(Low *l, Buf *b, const IrExpr *addr, const Value *v, const uint32_t *rec, uint32_t n, int rec_arms, Slot acc) {
  const FamilyInfo *fam = &l->m->families[v->inst];
  IrBlock *arms = arena_alloc(l->m->arena, ((size_t)n + 1u) * sizeof *arms);
  const CtorInfo *c;
  Buf arm;
  uint32_t ci;
  uint32_t i;
  if (arms == NULL)
    return oom(l);
  for (i = 0; i < n; i++) {
    arm = buf_under(b);
    ci = i < fam->ctor_count ? i : bin_of(l, fam);
    c = &l->m->ctors[fam->first_ctor + ci];
    if (!(c->interval_count == 0u && (rec[i] != NO_REC) == rec_arms
              ? (rec_count(c) == 2u ? bin_case(l, &arm, addr, v, rec, n, ci, rec[i], acc)
                                    : fam_case(l, &arm, addr, v->args[ci], c, rec[i], v->op == OP_ELIM, acc))
              : store(l, &arm, acc, trap_value(l))))
      return 0;
    arms[i] = block_of(&arm);
  }
  return st_switch(l, b, ex_load(l, addr, 0), arms, n);
}

/* A fold over a linear family: the walk, the base node into ACC, then a
   pass from the last recursive node with ACC for its recursive field. N is
   the arm count (the pseudo tag of a binary node adds 1). */
static int family_spine(Low *l, Buf *b, const Value *v, const uint32_t *rec, uint32_t n, Operand *out) {
  const IrExpr *node;
  Operand x;
  Walk w;
  Slot acc = slot_new(l);
  Buf in = buf_under(b);
  Buf body = buf_under(b);
  *out = slot_get(l, acc);
  return lower(l, b, v->args[v->argc - 1u], &x) && walk(l, b, x, rec, n, "a fold over a family", &w) &&
         fam_switch(l, &in, ex_local(l, w.cur), v, rec, n, 0, acc) && guard(l, b, ex_local(l, w.end), acc, &in) &&
         chain_node(l, &body, &w, &node) && fam_switch(l, &body, node, v, rec, n, 1, acc) && chain_end(l, b, &w, &body);
}

/* A fold over a family without a recursive field is a case split. With at
   most one recursive field in each point constructor, it is a spine walk.
   One binary node (cairn-lang S8, Q-S8-6) walks too: a static leaf side
   (lower_ctor) gives the constructor tag (REC = R) or the pseudo tag
   ctor_count (REC = L). A path constructor stops the walk. ELIM over a
   binary node refuses. */
static int family_fold(Low *l, Buf *b, const Value *v, Operand *out) {
  const FamilyInfo *fam = &l->m->families[v->inst];
  uint32_t *fields = arena_alloc(l->m->arena, ((size_t)fam->ctor_count + 1u) * sizeof *fields);
  uint32_t *recs = arena_alloc(l->m->arena, ((size_t)fam->ctor_count + 1u) * sizeof *recs);
  const CtorInfo *c;
  uint32_t bin = bin_of(l, fam);
  uint32_t rec = 0;
  uint32_t here;
  uint32_t i;
  uint32_t k;
  if (fields == NULL || recs == NULL)
    return oom(l);
  recs[fam->ctor_count] = NO_REC;
  for (i = 0; i < fam->ctor_count; i++) {
    c = &l->m->ctors[fam->first_ctor + i];
    here = rec_count(c);
    recs[i] = NO_REC;
    for (k = 0; k < c->field_count; k++) {
      recs[fam->ctor_count] = i == bin && c->fields[k].recursive && recs[i] == NO_REC ? k : recs[fam->ctor_count];
      recs[i] = c->fields[k].recursive ? k : recs[i];
    }
    recs[i] = c->interval_count != 0u ? NO_REC : recs[i];
    if (c->interval_count == 0u && here > (i == bin && v->op != OP_ELIM ? 2u : 1u))
      return diag_fail(l->m->diag, "REFUSE_RUNTIME_TREE", l->m->def, "a fold over a tree of %s must exist at run time (HOST-LIMIT)", fam->name);
    rec += here;
    fields[i] = c->field_count;
  }
  return rec != 0 ? family_spine(l, b, v, recs, bin != NO_REC ? fam->ctor_count + 1u : fam->ctor_count, out)
                  : cases_of(l, b, v->args[v->argc - 1u], v->args, fields, fam->ctor_count, out);
}

/* ACC := the leaf case of the node at ADDR (a point constructor without a
   recursive field) applied to its fields, then to ACC. The other arms are
   not reached; they give a trap. */
static int leaf_step(Low *l, Buf *b, const IrExpr *addr, const Chain *ch) {
  const FamilyInfo *fam = &l->m->families[ch->v->inst];
  IrBlock *arms = arena_alloc(l->m->arena, ((size_t)ch->n + 1u) * sizeof *arms);
  const CtorInfo *c;
  Operand *args;
  Buf arm;
  int live;
  uint32_t i;
  uint32_t k;
  if (arms == NULL)
    return oom(l);
  for (i = 0; i < ch->n; i++) {
    arm = buf_under(b);
    c = &l->m->ctors[fam->first_ctor + (i < fam->ctor_count ? i : ch->bin)];
    live = i < fam->ctor_count && c->interval_count == 0u && rec_count(c) == 0u;
    args = arena_alloc(l->m->arena, ((size_t)c->field_count + 1u) * sizeof *args);
    if (args == NULL)
      return oom(l);
    for (k = 0; live && k < c->field_count; k++)
      if (!field_at(l, &arm, addr, k, &args[k]))
        return 0;
    args[c->field_count] = slot_get(l, ch->acc);
    if (!(live ? apply_into(l, &arm, ch->v->args[i], args, c->field_count + 1u, ch->acc)
               : store(l, &arm, ch->acc, trap_value(l))))
      return 0;
    arms[i] = block_of(&arm);
  }
  return st_switch(l, b, ex_load(l, addr, 0), arms, ch->n);
}

/* ACC := the leaf side of the binary node at ADDR (tag I) applied to ACC:
   the field L for the constructor tag, the field R for the pseudo tag. */
static int leaf_at(Low *l, Buf *b, const IrExpr *addr, const Chain *ch, uint32_t i) {
  const FamilyInfo *fam = &l->m->families[ch->v->inst];
  uint32_t k = ch->rec[i] == ch->rec[fam->ctor_count] ? ch->rec[ch->bin] : ch->rec[fam->ctor_count];
  uint32_t t = local_new(l);
  Operand lf;
  Buf in = buf_under(b);
  return field_at(l, b, addr, k, &lf) && set(l, b, t, lf.w) && leaf_step(l, &in, ex_local(l, t), ch) &&
         guard(l, b, lf.f, ch->acc, &in);
}

/* A pass step at NODE: the leaf of a node with the tag LATER goes on ACC;
   the other arms are empty. */
static int chain_pass(Low *l, Buf *b, const IrExpr *node, const Chain *ch) {
  IrBlock *arms = arena_alloc(l->m->arena, ((size_t)ch->n + 1u) * sizeof *arms);
  Buf arm;
  uint32_t i;
  if (arms == NULL)
    return oom(l);
  for (i = 0; i < ch->n; i++) {
    arm = buf_under(b);
    if (i == ch->later && !leaf_at(l, &arm, node, ch, i))
      return 0;
    arms[i] = block_of(&arm);
  }
  return st_switch(l, b, ex_load(l, node, 0), arms, ch->n);
}

/* 1 when X is the variable Y. */
static int var_is(const Value *x, const Value *y) {
  return x != NULL && x->kind == VAL_VAR && x->nat == y->nat;
}

/* 1 when X is the application F (G D) of three variables. */
static int app2(const Value *x, const Value *f, const Value *g, const Value *d) {
  return x != NULL && x->kind == VAL_APP && var_is(x->dom, f) && x->arg != NULL && x->arg->kind == VAL_APP &&
         var_is(x->arg->dom, g) && var_is(x->arg->arg, d);
}

/* A fold over a family with one binary node into B -> B, applied to B0
   (cairn-lang S8, Q-S8-11): a loop on B. The binary case on new variables
   G, H, then D must be H (G D) (fmChain: the left side first) or G (H D)
   (fmRun: the right side first); else REFUSE_RUNTIME_CLOSURE, as for any
   other application. ACC := B0, then each leaf case on ACC: the leaves
   that go first during the walk (outer node first), the base node, then
   the others in the pass from the innermost node. */
static int chain_fold(Low *l, Buf *b, const Value *app, Operand *out) {
  const Value *v = app->dom;
  const FamilyInfo *fam;
  const CtorInfo *c;
  const Value *g;
  const Value *h;
  const Value *d;
  const Value *x;
  const IrExpr *node;
  uint32_t *recs;
  Chain ch;
  Operand t;
  Walk w;
  Buf in = buf_under(b);
  Buf body = buf_under(b);
  uint32_t i;
  uint32_t k;
  if (v == NULL || v->kind != VAL_STUCK || v->op != OP_FOLD_FAMILY)
    return closure(l);
  fam = &l->m->families[v->inst];
  ch.bin = bin_of(l, fam);
  if (ch.bin == NO_REC)
    return closure(l);
  recs = arena_alloc(l->m->arena, ((size_t)fam->ctor_count + 1u) * sizeof *recs);
  if (recs == NULL)
    return oom(l);
  for (i = 0; i < fam->ctor_count; i++) {
    c = &l->m->ctors[fam->first_ctor + i];
    recs[i] = NO_REC;
    if (c->interval_count == 0u && i != ch.bin && rec_count(c) != 0u)
      return closure(l);
  }
  c = &l->m->ctors[fam->first_ctor + ch.bin];
  recs[fam->ctor_count] = NO_REC;
  for (k = 0; k < c->field_count; k++) {
    recs[fam->ctor_count] = c->fields[k].recursive && recs[fam->ctor_count] == NO_REC ? k : recs[fam->ctor_count];
    recs[ch.bin] = c->fields[k].recursive ? k : recs[ch.bin];
  }
  g = fresh(l, operand(ex_const(l, 0), ex_const(l, 0)));
  h = fresh(l, operand(ex_const(l, 0), ex_const(l, 0)));
  d = fresh(l, operand(ex_const(l, 0), ex_const(l, 0)));
  x = g == NULL || h == NULL || d == NULL ? NULL : apply_value(l->m, v->args[ch.bin], g);
  x = x == NULL ? NULL : apply_value(l->m, x, h);
  x = x == NULL ? NULL : apply_value(l->m, x, d);
  if (!got(l, x))
    return 0;
  if (!app2(x, h, g, d) && !app2(x, g, h, d))
    return closure(l);
  ch.v = v;
  ch.rec = recs;
  ch.n = fam->ctor_count + 1u;
  ch.now = app2(x, h, g, d) ? ch.bin : fam->ctor_count;
  ch.later = app2(x, h, g, d) ? fam->ctor_count : ch.bin;
  ch.acc = slot_new(l);
  *out = slot_get(l, ch.acc);
  return lower_into(l, b, app->arg, ch.acc) && lower(l, b, v->args[v->argc - 1u], &t) &&
         walk_with(l, b, t, recs, ch.n, &ch, "a fold over a family", &w) && leaf_step(l, &in, ex_local(l, w.cur), &ch) &&
         guard(l, b, ex_local(l, w.end), ch.acc, &in) && chain_node(l, &body, &w, &node) &&
         chain_pass(l, &body, node, &ch) && chain_end(l, b, &w, &body);
}

/* The two arms of an option or a sum, behind the guard on the flag of X. */
static int two_arms(Low *l, Buf *b, Operand x, const Buf *a0, const Buf *a1, Slot res) {
  IrBlock *arms = arena_alloc(l->m->arena, 2u * sizeof *arms);
  Buf in = buf_under(b);
  if (arms == NULL)
    return oom(l);
  arms[0] = block_of(a0);
  arms[1] = block_of(a1);
  return st_switch(l, &in, ex_load(l, x.w, 0), arms, 2u) && guard(l, b, x.f, res, &in);
}

/* Lowers the option or sum XV into X. Arm 0 gets RES := X; arm 1 gets P :=
   the payload. */
static int carrier_begin(Low *l, Buf *b, const Value *xv, Slot res, Operand *x, Buf *a0, Buf *a1, Operand *p) {
  return lower(l, b, xv, x) && store(l, a0, res, *x) && field_at(l, a1, x->w, 0, p);
}

static int carrier_map(Low *l, Buf *b, const Value *v, Operand *out) {
  Operand x;
  Operand p;
  Operand y;
  Operand t;
  Slot res = slot_new(l);
  Slot ts = slot_new(l);
  Buf a0 = buf_under(b);
  Buf a1 = buf_under(b);
  *out = slot_get(l, res);
  t = slot_get(l, ts);
  return carrier_begin(l, b, v->args[1], res, &x, &a0, &a1, &p) && apply_into(l, &a1, v->args[0], &p, 1, ts) &&
         block_ops(l, &a1, 1, &t, 1, &y) && store(l, &a1, res, y) && two_arms(l, b, x, &a0, &a1, res);
}

static int carrier_bind(Low *l, Buf *b, const Value *v, Operand *out) {
  Operand x;
  Operand p;
  Slot res = slot_new(l);
  Buf a0 = buf_under(b);
  Buf a1 = buf_under(b);
  *out = slot_get(l, res);
  return carrier_begin(l, b, v->args[0], res, &x, &a0, &a1, &p) && apply_into(l, &a1, v->args[1], &p, 1, res) &&
         two_arms(l, b, x, &a0, &a1, res);
}

static int carrier_filter(Low *l, Buf *b, const Value *v, Operand *out) {
  Operand x;
  Operand p;
  Operand y;
  Slot res = slot_new(l);
  Slot ts = slot_new(l);
  Buf a0 = buf_under(b);
  Buf a1 = buf_under(b);
  Buf keep = buf_under(&a1);
  Buf drop = buf_under(&a1);
  Buf test = buf_under(&a1);
  *out = slot_get(l, res);
  return carrier_begin(l, b, v->args[1], res, &x, &a0, &a1, &p) && apply_into(l, &a1, v->args[0], &p, 1, ts) &&
         store(l, &keep, res, x) && block_ops(l, &drop, 0, NULL, 0, &y) && store(l, &drop, res, y) &&
         if_flag(l, &test, ex_local(l, ts.w), &keep, &drop) && guard(l, &a1, ex_local(l, ts.f), res, &test) &&
         two_arms(l, b, x, &a0, &a1, res);
}

/* FILTER on a list, or on a sum. */
static int not_option(Low *l, Buf *b, const Value *v, Operand *out) {
  return v->inst == CARRIER_LIST ? list_filter(l, b, v, out) : internal(l, "a filter over a sum");
}

/* ACC := Z, then ACC := STEP ACC, N times. A run-time N refuses
   REFUSE_UNBOUNDED (Q-S3-28); eval unrolls a literal N. */
static int fold_nat(Low *l, Buf *b, const Value *v, Operand *out) {
  Operand n;
  Operand a;
  Slot acc = slot_new(l);
  Buf in = buf_under(b);
  Buf body = buf_under(&in);
  *out = slot_get(l, acc);
  a = slot_get(l, acc);
  if (v->args[2]->kind != VAL_NAT)
    return unbounded(l, "a fold over a Nat");
  return lower(l, b, v->args[2], &n) && lower_into(l, &in, v->args[1], acc) && apply_into(l, &body, v->args[0], &a, 1, acc) &&
         st_repeat(l, &in, n.w, &body) && guard(l, b, n.f, acc, &in);
}

/* Proof contents erase, but forcing a proof must preserve its trap flag. */
static int proof_op(Low *l, Buf *b, const Value *v, uint32_t first, uint32_t count, Operand *out) {
  Operand p;
  const IrExpr *flag = ex_const(l, 0);
  uint32_t i;
  for (i = first; i < first + count; i++) {
    if (!lower(l, b, v->args[i], &p))
      return 0;
    flag = ex_or(l, flag, p.f);
  }
  return atoms(l, b, ex_const(l, 0), flag, out);
}

/* The divisor Y + (Y == 0) of each DIV and MOD: 1 where Y is 0, else Y, so
   never 0. It has the nonzero bit (Q-S8-3 N4). *ZERO is the atom Y == 0, for
   the flag. */
static const IrExpr *ex_divisor(Low *l, Buf *b, const IrExpr *y, const IrExpr **zero) {
  IrExpr *e;
  *zero = atom(l, b, ex_bin(l, IR_OP_EQ, y, ex_const(l, 0)));
  e = *zero == NULL ? NULL : expr_alloc(l, IR_EXPR_BINARY);
  if (e == NULL)
    return NULL;
  e->op = IR_OP_ADD;
  e->left = y;
  e->right = *zero;
  e->nonzero = 1;
  return e;
}

/* natDiv and natMod: a zero divisor sets the flag, and the word divides by
   1 (ex_divisor). */
static int nat_divmod(Low *l, Buf *b, const Value *v, IrOp op, Operand *out) {
  Operand x;
  Operand y;
  const IrExpr *zero;
  const IrExpr *d;
  if (!lower(l, b, v->args[0], &x) || !lower(l, b, v->args[1], &y))
    return 0;
  d = ex_divisor(l, b, y.w, &zero);
  return atoms(l, b, ex_bin(l, op, x.w, d), ex_or(l, zero, ex_or(l, x.f, y.f)), out);
}

/* CALL on the N arguments of V, behind the guard on their flags. */
static int call_op(Low *l, Buf *b, const Value *v, IrCall call, uint32_t n, Operand *out) {
  const IrExpr **args = arena_alloc(l->m->arena, ((size_t)n + 1u) * sizeof *args);
  const IrExpr *f = ex_const(l, 0);
  const IrExpr *w;
  Operand x;
  Slot res = slot_new(l);
  Buf in = buf_under(b);
  uint32_t i;
  *out = slot_get(l, res);
  if (args == NULL)
    return oom(l);
  for (i = 0; i < n; i++) {
    if (!lower(l, b, v->args[i], &x))
      return 0;
    args[i] = x.w;
    f = ex_or(l, f, x.f);
  }
  w = atom(l, &in, ex_call(l, call, args, n));
  return w != NULL && store(l, &in, res, operand(w, ex_const(l, 0))) &&
         guard(l, b, f, res, &in);
}

/* An Option from CALL on the N arguments of V (Q-S3-23), behind the guard
   on their flags. LE: the tag is U256_LE (second, first) (u256Sub); else
   the result 0 is none (a U256 overflow, a curve error or a quotient that
   does not fit; never a heap block). */
static int call_opt(Low *l, Buf *b, const Value *v, IrCall call, uint32_t n, int le, Operand *out) {
  const IrExpr **args = arena_alloc(l->m->arena, ((size_t)n + 3u) * sizeof *args);
  const IrExpr *f = ex_const(l, 0);
  const IrExpr *w;
  const IrExpr *tag;
  Operand x;
  Operand o;
  Operand opt;
  Slot res = slot_new(l);
  Buf in = buf_under(b);
  uint32_t i;
  *out = slot_get(l, res);
  if (args == NULL)
    return oom(l);
  for (i = 0; i < n; i++) {
    if (!lower(l, b, v->args[i], &x))
      return 0;
    args[i] = x.w;
    f = ex_or(l, f, x.f);
  }
  w = atom(l, &in, ex_call(l, call, args, n));
  if (w == NULL)
    return 0;
  if (le) {
    args[n] = args[1];
    args[n + 1u] = args[0];
  }
  tag = le ? ex_call(l, IR_CALL_U256_LE, args + n, 2) : ex_bin(l, IR_OP_EQ, ex_bin(l, IR_OP_EQ, w, ex_const(l, 0)), ex_const(l, 0));
  o = operand(w, ex_const(l, 0));
  return block_tag(l, &in, atom(l, &in, tag), &o, 1, &opt) && store(l, &in, res, opt) && guard(l, b, f, res, &in);
}

/* nonZero x (Q-S3-23): an Option NonZero block, none when x is 0. */
static int nonzero_opt(Low *l, Buf *b, const Value *v, Operand *out) {
  Operand x;
  const IrExpr *bits;
  uint32_t i;
  if (!lower(l, b, v->args[0], &x))
    return 0;
  bits = v->inst ? ex_load(l, x.w, 0) : x.w;
  for (i = 1; v->inst && i < 4u; i++)
    bits = ex_bin(l, IR_OP_OR, bits, ex_load(l, x.w, i));
  return block_tag(l, b, atom(l, b, ex_bin(l, IR_OP_EQ, ex_bin(l, IR_OP_EQ, bits, ex_const(l, 0)), ex_const(l, 0))), &x, 1, out);
}

/* upTo K f z n (Q-S3-30): ACC := Z, I := 0, then REPEAT K {IF I < N {ACC :=
   STEP ACC}; I := I + 1}. OUT := some ACC when N <= K, else none. A trap in
   N, or in ACC when N <= K, traps OUT. */
static int upto_nat(Low *l, Buf *b, const Value *v, Operand *out) {
  Operand k;
  Operand n;
  Operand a;
  Operand r;
  const IrExpr *le;
  Slot acc = slot_new(l);
  uint32_t i = local_new(l);
  Buf in = buf_under(b);
  Buf body = buf_under(&in);
  Buf step = buf_under(&body);
  Buf skip = buf_under(&body);
  a = slot_get(l, acc);
  r = slot_get(l, acc);
  if (!(lower(l, b, v->args[0], &k) && lower(l, b, v->args[3], &n) && lower_into(l, &in, v->args[2], acc) &&
        set(l, &in, i, ex_const(l, 0)) && apply_into(l, &step, v->args[1], &a, 1, acc) &&
        if_flag(l, &body, ex_bin(l, IR_OP_LT, ex_local(l, i), n.w), &step, &skip) &&
        set(l, &body, i, ex_bin(l, IR_OP_ADD, ex_local(l, i), ex_const(l, 1))) && st_repeat(l, &in, k.w, &body) &&
        guard(l, b, n.f, acc, &in)))
    return 0;
  le = atom(l, b, ex_bin(l, IR_OP_LE, n.w, k.w));
  if (!block_tag(l, b, le, &r, 1, out))
    return 0;
  out->f = atom(l, b, ex_bin(l, IR_OP_OR, n.f, ex_bin(l, IR_OP_MUL, le, r.f)));
  return 1;
}

/* upTo K f z xs (Q-S3-29/30): a walk of at most K + 1 cells (a literal
   fuel), then, when COUNT <= K, a fold pass of COUNT steps under a literal
   REPEAT K; some ACC when COUNT <= K, else none. */
static int upto_list(Low *l, Buf *b, const Value *v, Operand *out) {
  Operand x;
  Operand args[2];
  Operand r;
  Walk w;
  const IrExpr *le;
  const IrExpr *lim;
  Slot acc = slot_new(l);
  uint32_t i = local_new(l);
  uint64_t k = v->args[0]->nat;
  Buf in = buf_under(b);
  Buf body = buf_under(&in);
  Buf step = buf_under(&body);
  Buf skip = buf_under(&body);
  args[1] = slot_get(l, acc);
  r = slot_get(l, acc);
  if (v->args[0]->kind != VAL_NAT)
    return internal(l, "an upTo whose capacity is not a Nat literal");
  if (!(lower(l, b, v->args[3], &x) && walk_fuel(l, b, x, LIST_REC, 2u, NULL, k < SPINE_FUEL ? k + 1u : SPINE_FUEL, &w)))
    return 0;
  le = atom(l, b, ex_bin(l, IR_OP_LE, ex_local(l, w.count), ex_const(l, k)));
  lim = atom(l, b, ex_bin(l, IR_OP_MUL, ex_local(l, w.count), le));
  if (!(lower_into(l, &in, v->args[2], acc) && set(l, &in, i, ex_const(l, 0)) && chain_head(l, &step, &w, &args[0]) &&
        apply_into(l, &step, v->args[1], args, 2u, acc) && set(l, &step, w.rev, ex_load(l, ex_local(l, w.rev), 3)) &&
        if_flag(l, &body, ex_bin(l, IR_OP_LT, ex_local(l, i), lim), &step, &skip) &&
        set(l, &body, i, ex_bin(l, IR_OP_ADD, ex_local(l, i), ex_const(l, 1))) && st_repeat(l, &in, ex_const(l, k), &body) &&
        guard(l, b, ex_local(l, w.end), acc, &in) && block_tag(l, b, le, &r, 1, out)))
    return 0;
  out->f = atom(l, b, ex_bin(l, IR_OP_OR, ex_local(l, w.end), ex_bin(l, IR_OP_MUL, le, r.f)));
  return 1;
}

/* finAdd n a b and finNeg n a as Nat operations on the index (Q-S8-1; eval.c
   fin_add, fin_neg). Fin 0 has no value, so n = 0 sets the flag and the
   modulus is 1. */
static int fin_op(Low *l, Buf *b, const Value *v, int add, Operand *out) {
  Operand n;
  Operand x;
  Operand y = {NULL, NULL};
  const IrExpr *k;
  const IrExpr *xm;
  const IrExpr *ym;
  const IrExpr *d;
  const IrExpr *f;
  const IrExpr *z;
  if (!lower(l, b, v->args[0], &n) || !lower(l, b, v->args[1], &x) || (add && !lower(l, b, v->args[2], &y)))
    return 0;
  k = atom(l, b, ex_divisor(l, b, n.w, &z));
  f = ex_or(l, ex_or(l, n.f, x.f), z);
  xm = atom(l, b, ex_bin(l, IR_OP_MOD, x.w, k));
  if (!add)
    return atoms(l, b, ex_bin(l, IR_OP_MOD, ex_bin(l, IR_OP_SUB, n.w, xm), k), f, out);
  ym = atom(l, b, ex_bin(l, IR_OP_MOD, y.w, k));
  d = atom(l, b, ex_bin(l, IR_OP_SUB, n.w, ym));
  return atoms(l, b,
               ex_bin(l, IR_OP_ADD, ex_bin(l, IR_OP_SUB, xm, d), ex_bin(l, IR_OP_MUL, ex_bin(l, IR_OP_LT, xm, d), ex_bin(l, IR_OP_ADD, xm, ym))),
               ex_or(l, f, y.f), out);
}

/* finAll at a run-time index k: a SWITCH on k, arm J the body J. */
static int fin_switch(Low *l, Buf *b, const Value *v, Operand *out) {
  IrBlock *arms = arena_alloc(l->m->arena, (size_t)v->argc * sizeof *arms);
  Operand k;
  Slot res = slot_new(l);
  Buf in = buf_under(b);
  Buf arm;
  uint32_t j;
  *out = slot_get(l, res);
  if (arms == NULL)
    return oom(l);
  if (!lower(l, b, v->args[0], &k))
    return 0;
  for (j = 0; j + 1u < v->argc; j++) {
    arm = buf_under(b);
    if (!lower_into(l, &arm, v->args[j + 1u], res))
      return 0;
    arms[j] = block_of(&arm);
  }
  return st_switch(l, &in, k.w, arms, v->argc - 1u) && guard(l, b, k.f, res, &in);
}

/* elim P c1 .. cn x on a variable (eval.c reduce_elim): at Sum the two
   cases, at Unit the one case, with no case a trap, and at a domain family
   (FIELD = the family + 1) the family fold with the cases after P. */
static int elim_of(Low *l, Buf *b, const Value *v, Operand *out) {
  uint32_t fields[2] = {1u, 1u};
  Value *w;
  Operand x;
  Slot res;
  Buf in;
  if (v->field != 0u) {
    w = arena_alloc(l->m->arena, sizeof *w);
    if (w == NULL)
      return oom(l);
    *w = *v;
    w->inst = v->field - 1u;
    w->args = v->args + 1;
    w->argc = v->argc - 1u;
    return family_fold(l, b, w, out);
  }
  if (v->inst == 2u)
    return cases_of(l, b, v->args[3], v->args + 1, fields, 2, out);
  if (v->inst != 1u)
    return lower(l, b, v->args[v->argc - 1u], &x) && atoms(l, b, ex_const(l, 0), ex_const(l, 1), out);
  res = slot_new(l);
  in = buf_under(b);
  *out = slot_get(l, res);
  return lower(l, b, v->args[2], &x) && lower_into(l, &in, v->args[1], res) && guard(l, b, x.f, res, &in);
}

/* The field prime p of secp256k1 (secp256k1.c): the modulus of pt_neg. */
static const U256 LOW_SECP_P = {{0xfffffffefffffc2full, 0xffffffffffffffffull, 0xffffffffffffffffull, 0xffffffffffffffffull}};

/* A copy of TK_ISSUE_TAG (the macro is private to eval.c): the ASCII bytes
   of "trissue". */
#define LOW_ISSUE_TAG 0x74726973737565ull

/* The prelude function CALL on the first N of the words A, B and C. */
static const IrExpr *call_of(Low *l, IrCall call, uint32_t n, const IrExpr *a, const IrExpr *b, const IrExpr *c) {
  const IrExpr **args = arena_alloc(l->m->arena, 3u * sizeof *args);
  if (args == NULL) {
    (void)oom(l);
    return NULL;
  }
  args[0] = a;
  args[1] = b;
  args[2] = c;
  if (a == NULL || (n > 1u && b == NULL) || (n > 2u && c == NULL))
    return NULL;
  return ex_call(l, call, args, n);
}

/* One EC op (IR_CALL_EC_ADD or IR_CALL_EC_MUL) on P and Q, in a new local. */
static const IrExpr *pt_op(Low *l, Buf *b, IrCall call, const IrExpr *p, const IrExpr *q) {
  const IrExpr *e = call_of(l, call, 2u, p, q, NULL);
  return e == NULL ? NULL : atom(l, b, e);
}

/* *OUT = the point block (x, -y) of the valid point block P (group_neg).
   FE_SUB(0, y, p) is fe_neg, so (0, 0) stays (0, 0). */
static int pt_neg(Low *l, Buf *b, const IrExpr *p, Operand zero, Operand *out) {
  Operand m;
  Operand ops[2];
  const IrExpr *y;
  if (!word_block(l, b, &LOW_SECP_P, &m))
    return 0;
  y = call_of(l, IR_CALL_FE_SUB, 3u, zero.w, ex_load(l, p, 3u), m.w);
  y = y == NULL ? NULL : atom(l, b, y);
  ops[0] = operand(ex_load(l, p, 1u), ex_const(l, 0));
  ops[1] = operand(y, ex_const(l, 0));
  return y != NULL && ops[0].w != NULL && block_ops(l, b, 0, ops, 2u, out);
}

/* 1 when the point blocks P and Q have the same coordinates (group_eq). */
static const IrExpr *pt_eq(Low *l, const IrExpr *p, const IrExpr *q) {
  const IrExpr *x = call_of(l, IR_CALL_U256_EQ, 2u, ex_load(l, p, 1u), ex_load(l, q, 1u), NULL);
  const IrExpr *y = call_of(l, IR_CALL_U256_EQ, 2u, ex_load(l, p, 3u), ex_load(l, q, 3u), NULL);
  return x == NULL || y == NULL ? NULL : ex_bin(l, IR_OP_MUL, x, y);
}

/* A new cons list of the N words of ITEMS. The tail of the last cell is 0:
   a REPEAT of N turns reads the list and does not load through that tail. */
static const IrExpr *word_list(Low *l, Buf *b, const IrExpr *const *items, uint32_t n) {
  Operand ops[2];
  Operand cell = operand(ex_const(l, 0), ex_const(l, 0));
  uint32_t i;
  for (i = n; i > 0u; i--) {
    ops[0] = operand(items[i - 1u], ex_const(l, 0));
    ops[1] = cell;
    if (ops[0].w == NULL || cell.w == NULL || !block_ops(l, b, 1, ops, 2u, &cell))
      return NULL;
  }
  return cell.w;
}

/* ACC := SHA256(ACC, w) for each of the N words of ITEMS in order (the
   thr_hash chain of the host). One REPEAT over a new list: one SHA256 site. */
static int hash_fold(Low *l, Buf *b, uint32_t acc, const IrExpr *const *items, uint32_t n) {
  uint32_t cur = local_new(l);
  Buf body = buf_under(b);
  const IrExpr *c = ex_local(l, cur);
  return set(l, b, cur, word_list(l, b, items, n)) &&
         set(l, &body, acc, call_of(l, IR_CALL_SHA256, 2u, ex_local(l, acc), ex_load(l, c, 1u), NULL)) &&
         set(l, &body, cur, ex_load(l, c, 3u)) && st_repeat(l, b, ex_const(l, n), &body);
}

/* Runs IN when the locals TF (a trap) and BAD (a decode failure) are 0. */
static int when_live(Low *l, Buf *b, uint32_t tf, uint32_t bad, const Buf *in) {
  Buf skip = buf_under(b);
  return if_flag(l, b, ex_or(l, ex_local(l, tf), ex_local(l, bad)), &skip, in);
}

/* LOCAL := LOCAL or F. */
static int or_into(Low *l, Buf *b, uint32_t local, const IrExpr *f) {
  return f != NULL && set(l, b, local, ex_or(l, ex_local(l, local), f));
}

/* One step of the list walk at CUR, when TF and BAD are 0. A cons cell gives
   its head in *OUT and moves CUR to its tail; nil sets BAD. TF gets the flags
   of the head and of the tail, so no later step loads through a trap. */
static int list_step(Low *l, Buf *b, uint32_t tf, uint32_t bad, uint32_t cur, Operand *out) {
  Buf st = buf_under(b);
  Buf yes = buf_under(&st);
  Buf no = buf_under(&st);
  const IrExpr *c = ex_local(l, cur);
  return field_at(l, &yes, c, 0, out) && set(l, &yes, tf, ex_or(l, out->f, ex_load(l, c, 4u))) &&
         set(l, &yes, cur, ex_load(l, c, 3u)) && set(l, &no, bad, ex_const(l, 1)) &&
         if_flag(l, &st, ex_bin(l, IR_OP_EQ, ex_load(l, c, 0), ex_const(l, 1)), &yes, &no) &&
         when_live(l, b, tf, bad, &st);
}

/* The operands of one issueVerify call (issue_verify). */
typedef struct {
  uint32_t tf;    /* the local: 1 when an operand or a field is a trap */
  uint32_t bad;   /* the local: 1 when the host gives none */
  Operand key[2]; /* keyOpenC and key (point blocks) */
  Operand c1[2];  /* c1 and c2 of the 2 Ct */
  Operand c2[2];
  Operand a[2]; /* a, b and z of the 2 proofs */
  Operand b[2];
  Operand z[2];
  Operand n;    /* the Nat of m */
  Operand zero; /* the U256 0 */
} Issue;

/* Phase 1 of issue_verify: the operands of V into *S. BAD = 1 when the proof
   is not 10 words or one of the 10 points fails group_from_words. Each load
   is under TF = 0 and BAD = 0. */
static int issue_decode(Low *l, Buf *b, const Value *v, Issue *s) {
  static const U256 zero_word = {{0u, 0u, 0u, 0u}};
  Operand x[5];
  Operand ct[2];
  Operand w[10];
  Operand ops[2];
  Operand inf;
  const IrExpr *pts[10];
  const IrExpr *f = ex_const(l, 0);
  const IrExpr *r;
  const IrExpr *c;
  uint32_t cur = local_new(l);
  uint32_t i;
  Buf s1 = buf_under(b);
  Buf s2 = buf_under(b);
  Buf s3 = buf_under(b);
  Buf s4 = buf_under(b);
  Buf body = buf_under(&s4);
  s->tf = local_new(l);
  s->bad = local_new(l);
  c = ex_local(l, cur);
  for (i = 0; i < 5u; i++) {
    if (!lower(l, b, v->args[i], &x[i]))
      return 0;
    f = ex_or(l, f, x[i].f);
  }
  s->key[0] = x[0];
  s->key[1] = x[1];
  if (!set(l, b, s->tf, f) || !set(l, b, s->bad, ex_const(l, 0)) || !word_block(l, b, &zero_word, &s->zero))
    return 0;
  /* The 2 Ct blocks of the pair and the Nat of m. */
  if (!field_at(l, &s1, x[2].w, 0, &ct[0]) || !field_at(l, &s1, x[2].w, 1, &ct[1]) ||
      !field_at(l, &s1, x[3].w, 0, &s->n) || !set(l, &s1, s->tf, ex_or(l, ex_or(l, ct[0].f, ct[1].f), s->n.f)) ||
      !when_live(l, b, s->tf, s->bad, &s1))
    return 0;
  /* The 4 points of the 2 Ct blocks, and the first cell of the proof. */
  for (i = 0; i < 2u; i++) {
    if (!field_at(l, &s2, ct[i].w, 0, &s->c1[i]) || !field_at(l, &s2, ct[i].w, 1, &s->c2[i]))
      return 0;
  }
  if (!set(l, &s2, s->tf, ex_or(l, ex_or(l, s->c1[0].f, s->c2[0].f), ex_or(l, s->c1[1].f, s->c2[1].f))) ||
      !set(l, &s2, cur, x[4].w) || !when_live(l, b, s->tf, s->bad, &s2))
    return 0;
  /* The 10 proof words (tk_get): 10 cons cells, then nil. */
  for (i = 0; i < 10u; i++) {
    if (!list_step(l, b, s->tf, s->bad, cur, &w[i]))
      return 0;
  }
  if (!set(l, &s3, s->bad, ex_bin(l, IR_OP_EQ, ex_bin(l, IR_OP_EQ, ex_load(l, c, 0), ex_const(l, 0)), ex_const(l, 0))) ||
      !when_live(l, b, s->tf, s->bad, &s3))
    return 0;
  /* The proof points (tk_dleq_get: a.x, a.y, b.x, b.y, z). Then
     group_from_words on the 10 points: p + (0, 0) is 0 when p has a trapped
     coordinate, a coordinate of p or more, or is off the curve and not (0, 0). */
  for (i = 0; i < 2u; i++) {
    if (!block_ops(l, &s4, 0, &w[5u * i], 2u, &s->a[i]) || !block_ops(l, &s4, 0, &w[5u * i + 2u], 2u, &s->b[i]))
      return 0;
    s->z[i] = w[5u * i + 4u];
    pts[i] = s->key[i].w;
    pts[2u + 2u * i] = s->c1[i].w;
    pts[3u + 2u * i] = s->c2[i].w;
    pts[6u + 2u * i] = s->a[i].w;
    pts[7u + 2u * i] = s->b[i].w;
  }
  ops[0] = s->zero;
  ops[1] = s->zero;
  if (!block_ops(l, &s4, 0, ops, 2u, &inf) || !set(l, &s4, cur, word_list(l, &s4, pts, 10u)))
    return 0;
  r = pt_op(l, &body, IR_CALL_EC_ADD, ex_load(l, c, 1u), inf.w);
  return r != NULL && or_into(l, &body, s->bad, ex_bin(l, IR_OP_EQ, r, ex_const(l, 0))) &&
         set(l, &body, cur, ex_load(l, c, 3u)) && st_repeat(l, &s4, ex_const(l, 10u), &body) &&
         when_live(l, b, s->tf, s->bad, &s4);
}

/* The locals P (pk, c1, c2, a, b, z) := the operands of proof K. */
static int issue_proof(Low *l, Buf *b, const Issue *s, const uint32_t *p, uint32_t k) {
  return set(l, b, p[0], s->key[k].w) && set(l, b, p[1], s->c1[k].w) && set(l, b, p[2], s->c2[k].w) &&
         set(l, b, p[3], s->a[k].w) && set(l, b, p[4], s->b[k].w) && set(l, b, p[5], s->z[k].w);
}

/* Phase 2 of issue_verify (sig_rand_verify of sigma.c, one turn for each
   proof): *OUT is 1 when z G = a + e c1 and z pk = b + e v for the 2 proofs,
   with v = c2 + neg (m G). It runs only on valid points, so no EC result is 0.
   The EC ops and the hash chains are in loops: the WGSL pipeline has a size
   limit on an entry. */
static int issue_check(Low *l, Buf *b, const Issue *s, const IrExpr **out) {
  static const U256 tag_word = {{LOW_ISSUE_TAG, 0u, 0u, 0u}};
  GroupPoint gen;
  U256 gx;
  U256 gy;
  Operand ops[2];
  Operand g;
  Operand neg;
  Operand tag;
  Operand rtag;
  const IrExpr *items[13];
  const IrExpr *lhs[2];
  const IrExpr *rhs[2];
  const IrExpr *eq[2];
  const IrExpr *mw;
  const IrExpr *mg;
  const IrExpr *vv;
  uint32_t ctx = local_new(l);
  uint32_t e = local_new(l);
  uint32_t ok = local_new(l);
  uint32_t p[6];
  uint32_t i;
  uint32_t k;
  Buf body = buf_under(b);
  group_gen(&gen);
  group_to_words(&gx, &gy, &gen);
  if (!word_block(l, b, &gx, &ops[0]) || !word_block(l, b, &gy, &ops[1]) || !block_ops(l, b, 0, ops, 2u, &g) ||
      !word_block(l, b, &tag_word, &tag) || !word_block(l, b, &RAND_TAG, &rtag))
    return 0;
  mw = call_of(l, IR_CALL_TO_U256, 1u, s->n.w, NULL, NULL);
  mw = mw == NULL ? NULL : atom(l, b, mw);
  mg = pt_op(l, b, IR_CALL_EC_MUL, g.w, mw);
  if (mw == NULL || mg == NULL || !pt_neg(l, b, mg, s->zero, &neg))
    return 0;
  /* ctx (tk_issue): the chain from the tag over the 2 keys, the 4 points of
     the 2 Ct (x, then y), then m. */
  for (i = 0; i < 2u; i++) {
    items[2u * i] = ex_load(l, s->key[i].w, 1u);
    items[2u * i + 1u] = ex_load(l, s->key[i].w, 3u);
    items[4u + 4u * i] = ex_load(l, s->c1[i].w, 1u);
    items[5u + 4u * i] = ex_load(l, s->c1[i].w, 3u);
    items[6u + 4u * i] = ex_load(l, s->c2[i].w, 1u);
    items[7u + 4u * i] = ex_load(l, s->c2[i].w, 3u);
  }
  items[12] = mw;
  if (!set(l, b, ctx, tag.w) || !hash_fold(l, b, ctx, items, 13u))
    return 0;
  for (i = 0; i < 6u; i++)
    p[i] = local_new(l);
  if (!issue_proof(l, b, s, p, 0) || !set(l, b, ok, ex_const(l, 1)))
    return 0;
  /* e (sig_rand_challenge): the chain from RAND_TAG over ctx, pk, c1, c2, m,
     a and b. */
  vv = pt_op(l, &body, IR_CALL_EC_ADD, ex_local(l, p[2]), neg.w);
  items[0] = ex_local(l, ctx);
  for (i = 0; i < 5u; i++) {
    k = 1u + 2u * i + (i > 2u ? 1u : 0u);
    items[k] = ex_load(l, ex_local(l, p[i]), 1u);
    items[k + 1u] = ex_load(l, ex_local(l, p[i]), 3u);
  }
  items[7] = mw;
  if (vv == NULL || !set(l, &body, e, rtag.w) || !hash_fold(l, &body, e, items, 12u))
    return 0;
  /* z G = a + e c1, then z pk = b + e v (eq_holds). */
  lhs[0] = pt_op(l, &body, IR_CALL_EC_MUL, g.w, ex_local(l, p[5]));
  rhs[0] = pt_op(l, &body, IR_CALL_EC_MUL, ex_local(l, p[1]), ex_local(l, e));
  rhs[0] = pt_op(l, &body, IR_CALL_EC_ADD, ex_local(l, p[3]), rhs[0]);
  lhs[1] = pt_op(l, &body, IR_CALL_EC_MUL, ex_local(l, p[0]), ex_local(l, p[5]));
  rhs[1] = pt_op(l, &body, IR_CALL_EC_MUL, vv, ex_local(l, e));
  rhs[1] = pt_op(l, &body, IR_CALL_EC_ADD, ex_local(l, p[4]), rhs[1]);
  for (i = 0; i < 2u; i++) {
    eq[i] = lhs[i] == NULL || rhs[i] == NULL ? NULL : pt_eq(l, lhs[i], rhs[i]);
    if (eq[i] == NULL)
      return 0;
  }
  *out = ex_local(l, ok);
  return set(l, &body, ok, ex_bin(l, IR_OP_MUL, ex_local(l, ok), ex_bin(l, IR_OP_MUL, eq[0], eq[1]))) &&
         issue_proof(l, &body, s, p, 1) && st_repeat(l, b, ex_const(l, 2u), &body);
}

/* issueVerify (S8 C3): tk_issue_verify of eval.c. A trap when TF is 1, none
   when BAD is 1, else some (the flag of the 2 proofs). */
static int issue_verify(Low *l, Buf *b, const Value *v, Operand *out) {
  Issue s;
  Operand opt;
  Operand flag;
  const IrExpr *ok;
  Slot res = slot_new(l);
  Buf rest = buf_under(b);
  Buf none = buf_under(&rest);
  Buf run = buf_under(&rest);
  *out = slot_get(l, res);
  if (!issue_decode(l, b, v, &s) || !block_ops(l, &none, 0, NULL, 0, &opt) || !store(l, &none, res, opt) ||
      !issue_check(l, &run, &s, &ok))
    return 0;
  flag = operand(ok, ex_const(l, 0));
  return block_ops(l, &run, 1, &flag, 1, &opt) && store(l, &run, res, opt) &&
         if_flag(l, &rest, ex_local(l, s.bad), &none, &run) && guard(l, b, ex_local(l, s.tf), res, &rest);
}

/* The proof of withdrawVerify (trwire.c wd_proof_get): LOW_WD_LEN words.
   Words 0 to 3 rest, 4 to 8 rest_key, then LOW_WD_BITS bit ciphers (4 words
   each), LOW_WD_BITS bit proofs (12 words each) and sum (5 words). */
#define LOW_WD_LEN 2574u
#define LOW_WD_BITS 160u
#define LOW_WD_POINTS 966u
#define LOW_WD_SKIP4 328u

/* WD_TAG and WD_REST_TAG of withdraw.c. */
static const U256 LOW_WD_TAG = {{0x7769746864726177ull, 0u, 0u, 0u}};
static const U256 LOW_WD_REST_TAG = {{0x776472u, 0u, 0u, 0u}};

/* The operands of one withdrawVerify call (withdraw_verify). */
typedef struct {
  uint32_t tf;  /* the local: 1 when an operand or a field is a trap */
  uint32_t bad; /* the local: 1 when the host gives none */
  Operand key;  /* key_h (a point block) */
  Operand c1;   /* c1 and c2 of own_h */
  Operand c2;
  Operand w;    /* the word w */
  Operand pf;   /* the proof list */
  Operand zero; /* the U256 0 */
  Operand inf;  /* the point block (0, 0) */
} WdArgs;

/* The new locals OUT[0] to OUT[N - 1] := the next N heads of the list at the
   local CUR, and CUR moves past them. withdraw_decode tested each cell. */
static int wd_take(Low *l, Buf *b, uint32_t cur, uint32_t *out, uint32_t n) {
  const IrExpr *c = ex_local(l, cur);
  uint32_t i;
  for (i = 0; i < n; i++) {
    out[i] = local_new(l);
    if (!set(l, b, out[i], ex_load(l, c, 1u)) || !set(l, b, cur, ex_load(l, c, 3u)))
      return 0;
  }
  return 1;
}

/* *OUT = the point block of the word locals W[0] (x) and W[1] (y). */
static int wd_point(Low *l, Buf *b, const uint32_t *w, Operand *out) {
  Operand ops[2];
  ops[0] = operand(ex_local(l, w[0]), ex_const(l, 0));
  ops[1] = operand(ex_local(l, w[1]), ex_const(l, 0));
  return block_ops(l, b, 0, ops, 2u, out);
}

/* BAD := 1 when the point block P fails group_from_words (the test of
   issue_decode: P + (0, 0) is 0). */
static int wd_pt_test(Low *l, Buf *b, uint32_t bad, const IrExpr *p, const IrExpr *inf) {
  const IrExpr *r = pt_op(l, b, IR_CALL_EC_ADD, p, inf);
  return r != NULL && or_into(l, b, bad, ex_bin(l, IR_OP_EQ, r, ex_const(l, 0)));
}

/* A new local := U256_SUB(X, Y). */
static const IrExpr *wd_sub(Low *l, Buf *b, const IrExpr *x, const IrExpr *y) {
  const IrExpr *e = call_of(l, IR_CALL_U256_SUB, 2u, x, y, NULL);
  return e == NULL ? NULL : atom(l, b, e);
}

/* The locals of a proof list walk (pf_walk): the cell, the cell count, the
   point index and the 2 counters of the skip. The caller makes them first, so
   the local numbers of withdraw do not change. */
typedef struct {
  uint32_t cur;
  uint32_t len;
  uint32_t idx;
  uint32_t ks;
  uint32_t sk;
} PfWalk;

/* The count of scalar words before point J of a proof list (at most 4). The
   locals of a divisor go in PT, before their use. */
typedef const IrExpr *(*PfSkip)(Low *l, Buf *pt, const IrExpr *j);

/* One jointr list of transferVerify for pf_walk (trw_m1_get, trw_m2_get and trw_m3_get of trwire.c): KIND 1, 2 or 3,
   C classes, M gates (the M of tk_table). */
typedef struct {
  uint32_t kind;
  uint32_t c;
  uint32_t m;
} JrList;

/* The skip of the jointr list Q (the words before point J that are not point words, at most JR_C_MAX). M1: each gate
   is 33 ciphers (c1, c2: 66 points), then 33 ORs (A0, B0, A1, B1, then the scalars e0, e1, z0, z1: 132 points), then
   its sum dleq (A, B, z): 200 points (trwire.c get_gate); then the C f ciphers (z of the last gate before f[0].c1);
   then f_proof (C words e before its first atom, then z before each other atom). M2: y (2 points), y_proof (C words
   e, then 2 C atoms), d, the dleq of d (A, B, z). M3: d and its dleq (no skip). With T = J mod 200 in the gates: 4
   words when T >= 70 and T mod 4 == 2 (the scalars of an OR, the last before A), 1 word when T == 0 and J > 0 (z of
   the sum dleq before); the parity tests are of J + NF (the parity of J - NF). */
static const IrExpr *jr_skip(Low *l, Buf *pt, const JrList *q, const IrExpr *j) {
  const IrExpr *z;
  const IrExpr *k;
  const IrExpr *g;
  const IrExpr *e;
  uint32_t ng = 200u * q->m;
  uint32_t nf = ng + 2u * q->c;
  if (q->kind == 3u)
    return ex_const(l, 0);
  if (q->kind == 2u) {
    e = ex_bin(l, IR_OP_EQ, ex_bin(l, IR_OP_MOD, j, ex_divisor(l, pt, ex_const(l, 2u), &z)), ex_const(l, 0));
    e = ex_bin(l, IR_OP_MUL, e, ex_bin(l, IR_OP_LT, ex_const(l, 2u), j));
    e = ex_bin(l, IR_OP_MUL, e,
               ex_bin(l, IR_OP_EQ, ex_bin(l, IR_OP_EQ, j, ex_const(l, 4u * q->c + 4u)), ex_const(l, 0)));
    return ex_bin(l, IR_OP_ADD, e, ex_bin(l, IR_OP_MUL, ex_const(l, q->c), ex_bin(l, IR_OP_EQ, j, ex_const(l, 2u))));
  }
  k = ex_bin(l, IR_OP_MOD, j, ex_divisor(l, pt, ex_const(l, 200u), &z));
  g = ex_bin(l, IR_OP_EQ, ex_bin(l, IR_OP_MOD, k, ex_divisor(l, pt, ex_const(l, 4u), &z)), ex_const(l, 2u));
  g = ex_bin(l, IR_OP_MUL, ex_const(l, 4u), ex_bin(l, IR_OP_MUL, ex_bin(l, IR_OP_LT, ex_const(l, 69u), k), g));
  g = ex_bin(l, IR_OP_ADD, g,
             ex_bin(l, IR_OP_MUL, ex_bin(l, IR_OP_EQ, k, ex_const(l, 0)), ex_bin(l, IR_OP_LT, ex_const(l, 0), j)));
  g = ex_bin(l, IR_OP_MUL, g, ex_bin(l, IR_OP_LT, j, ex_const(l, ng)));
  e = ex_bin(l, IR_OP_MOD, ex_bin(l, IR_OP_ADD, j, ex_const(l, nf)), ex_divisor(l, pt, ex_const(l, 2u), &z));
  e = ex_bin(l, IR_OP_MUL, ex_bin(l, IR_OP_EQ, e, ex_const(l, 0)), ex_bin(l, IR_OP_LT, ex_const(l, nf), j));
  e = ex_bin(l, IR_OP_ADD, e, ex_bin(l, IR_OP_MUL, ex_const(l, q->c), ex_bin(l, IR_OP_EQ, j, ex_const(l, nf))));
  g = ex_bin(l, IR_OP_ADD, g, e);
  return ng == 0u ? g : ex_bin(l, IR_OP_ADD, g, ex_bin(l, IR_OP_EQ, j, ex_const(l, ng)));
}

/* The proof length and the point test of the proof list LIST (tk_get,
   get_point). BAD := 1 when LIST is not LEN cons cells: a count of the cells,
   LEN + 1 at most, and each cell gives the flags of its head and tail. Then
   turn J of POINTS skips SKIP_OF(J) scalar words (an inner REPEAT of 4 turns
   with a guard), and BAD := 1 when the next 2 words fail the point test. The
   list walks are REPEAT loops with a live guard (Q-S8-21). */
static int pf_walk(Low *l, Buf *b, const PfWalk *w, uint32_t tf, uint32_t bad, const IrExpr *list, const IrExpr *inf,
                   uint32_t len, uint32_t points, PfSkip skip_of, const JrList *q) {
  Operand p;
  const IrExpr *c = ex_local(l, w->cur);
  const IrExpr *j = ex_local(l, w->idx);
  const IrExpr *off = ex_bin(l, IR_OP_EQ, ex_bin(l, IR_OP_EQ, ex_local(l, w->len), ex_const(l, len)), ex_const(l, 0));
  const IrExpr *skip;
  uint32_t xy[2];
  Buf s3 = buf_under(b);
  Buf st = buf_under(&s3);
  Buf sl = buf_under(&st);
  Buf sy = buf_under(&sl);
  Buf sn = buf_under(&sl);
  Buf s4 = buf_under(b);
  Buf s5 = buf_under(b);
  Buf pt = buf_under(&s5);
  Buf skb = buf_under(&pt);
  Buf sky = buf_under(&skb);
  Buf skn = buf_under(&skb);
  if (!set(l, &sy, tf, ex_or(l, ex_load(l, c, 2u), ex_load(l, c, 4u))) ||
      !set(l, &sy, w->len, ex_bin(l, IR_OP_ADD, ex_local(l, w->len), ex_const(l, 1))) ||
      !set(l, &sy, w->cur, ex_load(l, c, 3u)) ||
      !if_flag(l, &sl, ex_bin(l, IR_OP_EQ, ex_load(l, c, 0), ex_const(l, 1)), &sy, &sn) ||
      !when_live(l, &st, tf, bad, &sl) || !set(l, &s3, w->cur, list) || !set(l, &s3, w->len, ex_const(l, 0)) ||
      !st_repeat(l, &s3, ex_const(l, len + 1u), &st) || !when_live(l, b, tf, bad, &s3) || !set(l, &s4, bad, off) ||
      !when_live(l, b, tf, bad, &s4))
    return 0;
  skip = q == NULL ? skip_of(l, &pt, j) : jr_skip(l, &pt, q, j);
  if (skip == NULL || !set(l, &sky, w->cur, ex_load(l, c, 3u)) ||
      !if_flag(l, &skb, ex_bin(l, IR_OP_LT, ex_local(l, w->ks), ex_local(l, w->sk)), &sky, &skn) ||
      !set(l, &skb, w->ks, ex_bin(l, IR_OP_ADD, ex_local(l, w->ks), ex_const(l, 1))) || !set(l, &pt, w->sk, skip) ||
      !set(l, &pt, w->ks, ex_const(l, 0)) || !st_repeat(l, &pt, ex_const(l, q == NULL ? 4u : JR_C_MAX), &skb) ||
      !wd_take(l, &pt, w->cur, xy, 2u) || !wd_point(l, &pt, xy, &p) || !wd_pt_test(l, &pt, bad, p.w, inf) ||
      !set(l, &pt, w->idx, ex_bin(l, IR_OP_ADD, j, ex_const(l, 1))) || !set(l, &s5, w->cur, list) ||
      !set(l, &s5, w->idx, ex_const(l, 0)) || !st_repeat(l, &s5, ex_const(l, points), &pt))
    return 0;
  return when_live(l, b, tf, bad, &s5);
}

/* The skip of the withdraw proof (PfSkip): z of rest_key (1 word) before
   point 4, and e0, e1, z0, z1 (4 words) before point LOW_WD_SKIP4 + 4 k. The
   divisor 4 of J mod 4 is the ex_divisor form, in PT before its use: the Wasm
   no-trap scan takes no literal divisor. */
static const IrExpr *wd_skip(Low *l, Buf *pt, const IrExpr *j) {
  const IrExpr *z;
  const IrExpr *skip =
      ex_bin(l, IR_OP_MUL, ex_bin(l, IR_OP_LE, ex_const(l, LOW_WD_SKIP4), j),
             ex_bin(l, IR_OP_EQ, ex_bin(l, IR_OP_MOD, j, ex_divisor(l, pt, ex_const(l, 4u), &z)), ex_const(l, 0)));
  return ex_bin(l, IR_OP_ADD, ex_bin(l, IR_OP_EQ, j, ex_const(l, 4u)), ex_bin(l, IR_OP_MUL, ex_const(l, 4u), skip));
}

/* Phase 1 of withdraw_verify: the operands of V into *S. BAD = 1 when key_h
   or a point of own_h fails the point test, the proof is not LOW_WD_LEN words,
   or one of its LOW_WD_POINTS points fails the point test: eval.c gives none
   before withdraw_verify runs. The list walks are REPEAT loops with a live
   guard (Q-S8-21). */
static int withdraw_decode(Low *l, Buf *b, const Value *v, WdArgs *s) {
  static const U256 zero_word = {{0u, 0u, 0u, 0u}};
  Operand x[4];
  Operand ops[2];
  const IrExpr *pts[3];
  const IrExpr *f = ex_const(l, 0);
  const IrExpr *c;
  PfWalk w;
  uint32_t i;
  Buf s1 = buf_under(b);
  Buf s2 = buf_under(b);
  Buf kb = buf_under(&s2);
  w.cur = local_new(l);
  w.len = local_new(l);
  w.idx = local_new(l);
  w.ks = local_new(l);
  w.sk = local_new(l);
  s->tf = local_new(l);
  s->bad = local_new(l);
  c = ex_local(l, w.cur);
  for (i = 0; i < 4u; i++) {
    if (!lower(l, b, v->args[i], &x[i]))
      return 0;
    f = ex_or(l, f, x[i].f);
  }
  s->key = x[0];
  s->w = x[2];
  s->pf = x[3];
  if (!set(l, b, s->tf, f) || !set(l, b, s->bad, ex_const(l, 0)) || !word_block(l, b, &zero_word, &s->zero))
    return 0;
  ops[0] = s->zero;
  ops[1] = s->zero;
  if (!block_ops(l, b, 0, ops, 2u, &s->inf))
    return 0;
  /* The 2 points of own_h. */
  if (!field_at(l, &s1, x[1].w, 0, &s->c1) || !field_at(l, &s1, x[1].w, 1, &s->c2) ||
      !set(l, &s1, s->tf, ex_or(l, s->c1.f, s->c2.f)) || !when_live(l, b, s->tf, s->bad, &s1))
    return 0;
  /* tk_wd_statement: the point test of key_h, own_h.c1 and own_h.c2. */
  pts[0] = s->key.w;
  pts[1] = s->c1.w;
  pts[2] = s->c2.w;
  if (!wd_pt_test(l, &kb, s->bad, ex_load(l, c, 1u), s->inf.w) || !set(l, &kb, w.cur, ex_load(l, c, 3u)) ||
      !set(l, &s2, w.cur, word_list(l, &s2, pts, 3u)) || !st_repeat(l, &s2, ex_const(l, 3u), &kb) ||
      !when_live(l, b, s->tf, s->bad, &s2))
    return 0;
  /* The proof (wd_proof_get): LOW_WD_LEN words, LOW_WD_POINTS points. */
  return pf_walk(l, b, &w, s->tf, s->bad, x[3].w, s->inf.w, LOW_WD_LEN, LOW_WD_POINTS, wd_skip, NULL);
}

/* The constants and the locals of withdraw_check. */
typedef struct {
  Operand g;    /* the generator G */
  Operand ng;   /* -G */
  uint32_t ok;  /* the local: the flag of the checks */
  uint32_t ctx; /* the local: ctx (withdraw_ctx) */
  uint32_t cr;  /* the local: cr = H(WD_REST_TAG, ctx) */
  uint32_t bh;  /* the local: the cell of bit[0] */
  uint32_t pc;  /* the local: the cell of proof[0], then of sum */
  uint32_t a1;  /* the locals of the Horner sum (c1 and c2 point blocks) */
  uint32_t a2;
} WdRun;

/* The locals P (pk, c1, c2, a, b, z, the context) := the 7 words of OPS. */
static int wd_proof(Low *l, Buf *b, const uint32_t *p, const IrExpr *const *ops) {
  uint32_t i;
  for (i = 0; i < 7u; i++) {
    if (!set(l, b, p[i], ops[i]))
      return 0;
  }
  return 1;
}

/* tr_range_check, the bit proofs (sig_or_verify of sigma.c), one turn for each
   bit I of N: OK := OK and e0 + e1 = ch (mod 2^256) and z0 G = a0 + e0 c1,
   z0 KEY = b0 + e0 c2, z1 G = a1 + e1 c1, z1 KEY = b1 + e1 d with d = c2 - G.
   ch is the chain from ONEHOT_TAG over the context local CX (withdraw: cr), BASE + I,
   KEY, c1, c2, a0, b0, a1, b1. */
static int wd_bits(Low *l, Buf *b, const WdRun *r, const IrExpr *key, uint32_t cx, uint32_t n, uint32_t base) {
  static const U256 one_word = {{1u, 0u, 0u, 0u}};
  static const U256 max_word = {{~0ull, ~0ull, ~0ull, ~0ull}};
  Operand otag;
  Operand one;
  Operand max;
  Operand pt[6];
  const IrExpr *items[16];
  const IrExpr *lhs[4];
  const IrExpr *rhs[4];
  const IrExpr *e0;
  const IrExpr *e1;
  const IrExpr *ch;
  const IrExpr *ix;
  const IrExpr *lo;
  const IrExpr *ea;
  const IrExpr *eb;
  const IrExpr *fl;
  const IrExpr *dd;
  const IrExpr *eq;
  uint32_t bc = local_new(l);
  uint32_t idx = local_new(l);
  uint32_t h = local_new(l);
  uint32_t bw[4];
  uint32_t qw[12];
  uint32_t i;
  Buf body = buf_under(b);
  if (!word_block(l, b, &ONEHOT_TAG, &otag) || !word_block(l, b, &one_word, &one) ||
      !word_block(l, b, &max_word, &max) || !set(l, b, bc, ex_local(l, r->bh)) || !set(l, b, idx, ex_const(l, base)) ||
      !wd_take(l, &body, bc, bw, 4u) || !wd_take(l, &body, r->pc, qw, 12u))
    return 0;
  for (i = 0; i < 2u; i++) {
    if (!wd_point(l, &body, &bw[2u * i], &pt[i]))
      return 0;
  }
  for (i = 0; i < 4u; i++) {
    if (!wd_point(l, &body, &qw[2u * i], &pt[2u + i]))
      return 0;
  }
  ix = call_of(l, IR_CALL_TO_U256, 1u, ex_local(l, idx), NULL, NULL);
  ix = ix == NULL ? NULL : atom(l, &body, ix);
  items[0] = ex_local(l, cx);
  items[1] = ix;
  items[2] = ex_load(l, key, 1u);
  items[3] = ex_load(l, key, 3u);
  for (i = 0; i < 4u; i++)
    items[4u + i] = ex_local(l, bw[i]);
  for (i = 0; i < 8u; i++)
    items[8u + i] = ex_local(l, qw[i]);
  if (ix == NULL || !set(l, &body, h, otag.w) || !hash_fold(l, &body, h, items, 16u))
    return 0;
  /* e0 + e1 = ch modulo 2^256 (wrap_add): e1 = ch - e0 when e0 <= ch, else
     e1 = (2^256 - 1) - (e0 - ch - 1). U256_SUB gives 0 and no trap when b > a. */
  ch = ex_local(l, h);
  e0 = ex_local(l, qw[8]);
  e1 = ex_local(l, qw[9]);
  lo = call_of(l, IR_CALL_U256_LE, 2u, e0, ch, NULL);
  ea = call_of(l, IR_CALL_U256_EQ, 2u, e1, wd_sub(l, &body, ch, e0), NULL);
  eb = wd_sub(l, &body, max.w, wd_sub(l, &body, wd_sub(l, &body, e0, ch), one.w));
  eb = call_of(l, IR_CALL_U256_EQ, 2u, e1, eb, NULL);
  if (lo == NULL || ea == NULL || eb == NULL)
    return 0;
  fl = ex_or(l, ex_bin(l, IR_OP_MUL, lo, ea), ex_bin(l, IR_OP_MUL, ex_bin(l, IR_OP_EQ, lo, ex_const(l, 0)), eb));
  /* The 4 equations (eq_holds). */
  dd = pt_op(l, &body, IR_CALL_EC_ADD, pt[1].w, r->ng.w);
  lhs[0] = pt_op(l, &body, IR_CALL_EC_MUL, r->g.w, ex_local(l, qw[10]));
  rhs[0] = pt_op(l, &body, IR_CALL_EC_MUL, pt[0].w, e0);
  rhs[0] = pt_op(l, &body, IR_CALL_EC_ADD, pt[2].w, rhs[0]);
  lhs[1] = pt_op(l, &body, IR_CALL_EC_MUL, key, ex_local(l, qw[10]));
  rhs[1] = pt_op(l, &body, IR_CALL_EC_MUL, pt[1].w, e0);
  rhs[1] = pt_op(l, &body, IR_CALL_EC_ADD, pt[3].w, rhs[1]);
  lhs[2] = pt_op(l, &body, IR_CALL_EC_MUL, r->g.w, ex_local(l, qw[11]));
  rhs[2] = pt_op(l, &body, IR_CALL_EC_MUL, pt[0].w, e1);
  rhs[2] = pt_op(l, &body, IR_CALL_EC_ADD, pt[4].w, rhs[2]);
  lhs[3] = pt_op(l, &body, IR_CALL_EC_MUL, key, ex_local(l, qw[11]));
  rhs[3] = dd == NULL ? NULL : pt_op(l, &body, IR_CALL_EC_MUL, dd, e1);
  rhs[3] = pt_op(l, &body, IR_CALL_EC_ADD, pt[5].w, rhs[3]);
  for (i = 0; i < 4u; i++) {
    eq = lhs[i] == NULL || rhs[i] == NULL ? NULL : pt_eq(l, lhs[i], rhs[i]);
    if (eq == NULL)
      return 0;
    fl = ex_bin(l, IR_OP_MUL, fl, eq);
  }
  return set(l, &body, r->ok, ex_bin(l, IR_OP_MUL, ex_local(l, r->ok), fl)) &&
         set(l, &body, idx, ex_bin(l, IR_OP_ADD, ex_local(l, idx), ex_const(l, 1))) &&
         st_repeat(l, b, ex_const(l, n), &body);
}

/* range_rest of transfer.c (Q-S8-24) for N bits: acc = bit[N - 1], then
   acc := 2 acc + bit[i - 1] for i = N - 1 down to 1, on c1 (A1) and on c2
   (A2). Withdraw: N = LOW_WD_BITS. Turn J reaches bit[N - 1 - J] from bit[0]
   with an inner REPEAT and a guard: no EC op and no heap for the walk. */
static int wd_horner(Low *l, Buf *b, const WdRun *r, uint32_t n) {
  Operand pt[2];
  const IrExpr *d1;
  const IrExpr *d2;
  const IrExpr *to;
  uint32_t hc = local_new(l);
  uint32_t idx = local_new(l);
  uint32_t k = local_new(l);
  uint32_t hw[4];
  const IrExpr *c = ex_local(l, hc);
  Buf body = buf_under(b);
  Buf walk = buf_under(&body);
  Buf wy = buf_under(&walk);
  Buf wn = buf_under(&walk);
  Buf first = buf_under(&body);
  Buf later = buf_under(&body);
  to = ex_bin(l, IR_OP_SUB, ex_const(l, n - 1u), ex_local(l, idx));
  if (!set(l, &wy, hc, ex_load(l, ex_load(l, ex_load(l, ex_load(l, c, 3u), 3u), 3u), 3u)) ||
      !if_flag(l, &walk, ex_bin(l, IR_OP_LT, ex_local(l, k), to), &wy, &wn) ||
      !set(l, &walk, k, ex_bin(l, IR_OP_ADD, ex_local(l, k), ex_const(l, 1))) ||
      !set(l, &body, hc, ex_local(l, r->bh)) || !set(l, &body, k, ex_const(l, 0)) ||
      !st_repeat(l, &body, ex_const(l, n - 1u), &walk) || !wd_take(l, &body, hc, hw, 4u) ||
      !wd_point(l, &body, &hw[0], &pt[0]) || !wd_point(l, &body, &hw[2], &pt[1]) ||
      !set(l, &first, r->a1, pt[0].w) || !set(l, &first, r->a2, pt[1].w))
    return 0;
  d1 = pt_op(l, &later, IR_CALL_EC_ADD, ex_local(l, r->a1), ex_local(l, r->a1));
  d1 = pt_op(l, &later, IR_CALL_EC_ADD, d1, pt[0].w);
  d2 = pt_op(l, &later, IR_CALL_EC_ADD, ex_local(l, r->a2), ex_local(l, r->a2));
  d2 = pt_op(l, &later, IR_CALL_EC_ADD, d2, pt[1].w);
  return d1 != NULL && d2 != NULL && set(l, &later, r->a1, d1) && set(l, &later, r->a2, d2) &&
         if_flag(l, &body, ex_bin(l, IR_OP_EQ, ex_local(l, idx), ex_const(l, 0)), &first, &later) &&
         set(l, &body, idx, ex_bin(l, IR_OP_ADD, ex_local(l, idx), ex_const(l, 1))) &&
         set(l, b, idx, ex_const(l, 0)) && st_repeat(l, b, ex_const(l, n), &body);
}

/* The proofs of one wd_sums loop, at most: the 5 dleqs of transferVerify. */
#define LOW_SUMS_MAX 5u

/* sig_rand_verify of sigma.c with m = 0 (so v = c2), one turn for each of the
   K proofs (2 <= K <= LOW_SUMS_MAX). Proof I is the 7 words SETS[7 I] to
   SETS[7 I + 6]: pk, c1, c2, a, b, z and the context. Withdraw: P0 (rest_key:
   pk D.c1, ct (key_h, D.c2), ctx), then P1 (sum: pk key_h, ct d, cr). OK :=
   OK and z G = a + e c1 and z pk = b + e c2, with e the chain from RAND_TAG
   over the context, pk, c1, c2, ZERO (m = 0), a and b. The 6 EC sites are in
   one loop body: the locals P hold proof I, and the K - 2 sets of locals Q
   move proof I + 1 into P at the end of turn I (the body reads the last proof
   from SETS). */
static int wd_sums(Low *l, Buf *b, const WdRun *r, const IrExpr *zero, const IrExpr *const *sets, uint32_t k) {
  Operand rtag;
  const IrExpr *items[12];
  const IrExpr *lhs[2];
  const IrExpr *rhs[2];
  const IrExpr *eq[2];
  const IrExpr *nx[7];
  const uint32_t *dst;
  uint32_t e = local_new(l);
  uint32_t p[7];
  uint32_t q[7u * (LOW_SUMS_MAX - 2u)];
  uint32_t i;
  uint32_t t;
  Buf body = buf_under(b);
  if (k < 1u || k > LOW_SUMS_MAX)
    return refuse(l, "a proof sum count");
  for (i = 0; i < 7u; i++)
    p[i] = local_new(l);
  /* K = 1 (the sum of one gate, jr_gates): no Q locals. */
  for (i = 0; k > 1u && i < 7u * (k - 2u); i++)
    q[i] = local_new(l);
  if (!word_block(l, b, &RAND_TAG, &rtag) || !wd_proof(l, b, p, sets))
    return 0;
  for (t = 1; t + 1u < k; t++) {
    if (!wd_proof(l, b, &q[7u * (t - 1u)], sets + 7u * t))
      return 0;
  }
  items[0] = ex_local(l, p[6]);
  for (i = 0; i < 3u; i++) {
    items[1u + 2u * i] = ex_load(l, ex_local(l, p[i]), 1u);
    items[2u + 2u * i] = ex_load(l, ex_local(l, p[i]), 3u);
  }
  items[7] = zero;
  for (i = 0; i < 2u; i++) {
    items[8u + 2u * i] = ex_load(l, ex_local(l, p[3u + i]), 1u);
    items[9u + 2u * i] = ex_load(l, ex_local(l, p[3u + i]), 3u);
  }
  if (!set(l, &body, e, rtag.w) || !hash_fold(l, &body, e, items, 12u))
    return 0;
  lhs[0] = pt_op(l, &body, IR_CALL_EC_MUL, r->g.w, ex_local(l, p[5]));
  rhs[0] = pt_op(l, &body, IR_CALL_EC_MUL, ex_local(l, p[1]), ex_local(l, e));
  rhs[0] = pt_op(l, &body, IR_CALL_EC_ADD, ex_local(l, p[3]), rhs[0]);
  lhs[1] = pt_op(l, &body, IR_CALL_EC_MUL, ex_local(l, p[0]), ex_local(l, p[5]));
  rhs[1] = pt_op(l, &body, IR_CALL_EC_MUL, ex_local(l, p[2]), ex_local(l, e));
  rhs[1] = pt_op(l, &body, IR_CALL_EC_ADD, ex_local(l, p[4]), rhs[1]);
  for (i = 0; i < 2u; i++) {
    eq[i] = lhs[i] == NULL || rhs[i] == NULL ? NULL : pt_eq(l, lhs[i], rhs[i]);
    if (eq[i] == NULL)
      return 0;
  }
  if (!set(l, &body, r->ok, ex_bin(l, IR_OP_MUL, ex_local(l, r->ok), ex_bin(l, IR_OP_MUL, eq[0], eq[1]))))
    return 0;
  /* The next proof: P := Q1, Q1 := Q2, and so on; the last := proof K - 1. */
  dst = p;
  for (t = 1; t + 1u < k; t++) {
    for (i = 0; i < 7u; i++)
      nx[i] = ex_local(l, q[7u * (t - 1u) + i]);
    if (!wd_proof(l, &body, dst, nx))
      return 0;
    dst = &q[7u * (t - 1u)];
  }
  return wd_proof(l, &body, dst, sets + 7u * (k - 1u)) && st_repeat(l, b, ex_const(l, k), &body);
}

/* Phase 2 of withdraw_verify (withdraw.c): *OUT is 0 when w >= 2^128
   (TR_RANGE), else 1 when the rest_key proof, the bit proofs and the sum proof
   hold. rest_part: D = own_h - Enc(m; 1) - rest with m = w 2^32 (no mod n:
   m < 2^160), so D.c1 = own.c1 - G - rest.c1 and D.c2 = own.c2 - (m G + key_h)
   - rest.c2. The direct form of Q-S8-25: the EC ops are in loops (the WGSL
   pipeline has a size limit on an entry). */
static int withdraw_check(Low *l, Buf *b, const WdArgs *s, const IrExpr **out) {
  static const U256 big_word = {{0u, 0u, 1u, 0u}};
  static const U256 shift_word = {{0x100000000ull, 0u, 0u, 0u}};
  GroupPoint gen;
  U256 gx;
  U256 gy;
  Operand ops[2];
  Operand big;
  Operand shift;
  Operand tag;
  Operand rtag;
  Operand pr[6];
  Operand neg[5];
  const IrExpr *items[7];
  const IrExpr *ps[14];
  const IrExpr *m;
  const IrExpr *mg;
  const IrExpr *ek;
  const IrExpr *d[4];
  const IrExpr *hi_w;
  uint32_t rw[4];
  uint32_t kw[5];
  uint32_t sw[5];
  uint32_t i;
  WdRun r;
  Buf go = buf_under(b);
  Buf hi = buf_under(b);
  Buf step = buf_under(&go);
  r.ok = local_new(l);
  r.ctx = local_new(l);
  r.cr = local_new(l);
  r.bh = local_new(l);
  r.pc = local_new(l);
  r.a1 = local_new(l);
  r.a2 = local_new(l);
  group_gen(&gen);
  group_to_words(&gx, &gy, &gen);
  if (!word_block(l, &go, &gx, &ops[0]) || !word_block(l, &go, &gy, &ops[1]) || !block_ops(l, &go, 0, ops, 2u, &r.g) ||
      !pt_neg(l, &go, r.g.w, s->zero, &r.ng) || !word_block(l, &go, &shift_word, &shift) ||
      !word_block(l, &go, &LOW_WD_TAG, &tag) || !word_block(l, &go, &LOW_WD_REST_TAG, &rtag))
    return 0;
  /* ctx (withdraw_ctx): the chain from WD_TAG over key, own.c1, own.c2 (x,
     then y) and w; then cr = H(WD_REST_TAG, ctx). */
  items[0] = ex_load(l, s->key.w, 1u);
  items[1] = ex_load(l, s->key.w, 3u);
  items[2] = ex_load(l, s->c1.w, 1u);
  items[3] = ex_load(l, s->c1.w, 3u);
  items[4] = ex_load(l, s->c2.w, 1u);
  items[5] = ex_load(l, s->c2.w, 3u);
  items[6] = s->w.w;
  if (!set(l, &go, r.ctx, tag.w) || !hash_fold(l, &go, r.ctx, items, 7u) || !set(l, &go, r.cr, rtag.w))
    return 0;
  items[0] = ex_local(l, r.ctx);
  if (!hash_fold(l, &go, r.cr, items, 1u))
    return 0;
  /* The words of rest and rest_key; bh := bit[0]; pc := proof[0]. */
  if (!set(l, &go, r.pc, s->pf.w) || !wd_take(l, &go, r.pc, rw, 4u) || !wd_take(l, &go, r.pc, kw, 5u) ||
      !set(l, &go, r.bh, ex_local(l, r.pc)) || !set(l, &step, r.pc, ex_load(l, ex_local(l, r.pc), 3u)) ||
      !st_repeat(l, &go, ex_const(l, 4u * LOW_WD_BITS), &step))
    return 0;
  for (i = 0; i < 2u; i++) {
    if (!wd_point(l, &go, &rw[2u * i], &pr[i]) || !wd_point(l, &go, &kw[2u * i], &pr[2u + i]))
      return 0;
  }
  /* rest_part. */
  m = call_of(l, IR_CALL_U256_MUL, 2u, s->w.w, shift.w, NULL);
  m = m == NULL ? NULL : atom(l, &go, m);
  mg = pt_op(l, &go, IR_CALL_EC_MUL, r.g.w, m);
  ek = pt_op(l, &go, IR_CALL_EC_ADD, mg, s->key.w);
  if (ek == NULL || !pt_neg(l, &go, pr[0].w, s->zero, &neg[0]) || !pt_neg(l, &go, pr[1].w, s->zero, &neg[1]) ||
      !pt_neg(l, &go, ek, s->zero, &neg[2]))
    return 0;
  d[0] = pt_op(l, &go, IR_CALL_EC_ADD, s->c1.w, r.ng.w);
  d[0] = pt_op(l, &go, IR_CALL_EC_ADD, d[0], neg[0].w);
  d[1] = pt_op(l, &go, IR_CALL_EC_ADD, s->c2.w, neg[2].w);
  d[1] = pt_op(l, &go, IR_CALL_EC_ADD, d[1], neg[1].w);
  if (d[0] == NULL || d[1] == NULL || !wd_bits(l, &go, &r, s->key.w, r.cr, LOW_WD_BITS, 0u) ||
      !wd_horner(l, &go, &r, LOW_WD_BITS) || !pt_neg(l, &go, ex_local(l, r.a1), s->zero, &neg[3]) ||
      !pt_neg(l, &go, ex_local(l, r.a2), s->zero, &neg[4]))
    return 0;
  /* d = rest - acc, then the words of sum. */
  d[2] = pt_op(l, &go, IR_CALL_EC_ADD, pr[0].w, neg[3].w);
  d[3] = pt_op(l, &go, IR_CALL_EC_ADD, pr[1].w, neg[4].w);
  if (d[2] == NULL || d[3] == NULL || !wd_take(l, &go, r.pc, sw, 5u) || !wd_point(l, &go, &sw[0], &pr[4]) ||
      !wd_point(l, &go, &sw[2], &pr[5]))
    return 0;
  ps[0] = d[0];
  ps[1] = s->key.w;
  ps[2] = d[1];
  ps[3] = pr[2].w;
  ps[4] = pr[3].w;
  ps[5] = ex_local(l, kw[4]);
  ps[6] = ex_local(l, r.ctx);
  ps[7] = s->key.w;
  ps[8] = d[2];
  ps[9] = d[3];
  ps[10] = pr[4].w;
  ps[11] = pr[5].w;
  ps[12] = ex_local(l, sw[4]);
  ps[13] = ex_local(l, r.cr);
  if (!wd_sums(l, &go, &r, s->zero.w, ps, 2u) || !set(l, &hi, r.ok, ex_const(l, 0)) || !set(l, b, r.ok, ex_const(l, 1)) ||
      !word_block(l, b, &big_word, &big))
    return 0;
  /* w >= 2^128: TR_RANGE, flag 0 (after the decode: eval.c tests the points
     first). */
  hi_w = call_of(l, IR_CALL_U256_LE, 2u, big.w, s->w.w, NULL);
  *out = ex_local(l, r.ok);
  return hi_w != NULL && if_flag(l, b, hi_w, &hi, &go);
}

/* withdrawVerify (S8 C3): tk_withdraw_verify of eval.c. A trap when TF is 1,
   none when BAD is 1, else some (the flag of withdraw_check). */
static int withdraw_verify(Low *l, Buf *b, const Value *v, Operand *out) {
  WdArgs s;
  Operand opt;
  Operand flag;
  const IrExpr *ok;
  Slot res = slot_new(l);
  Buf rest = buf_under(b);
  Buf none = buf_under(&rest);
  Buf run = buf_under(&rest);
  *out = slot_get(l, res);
  if (!withdraw_decode(l, b, v, &s) || !block_ops(l, &none, 0, NULL, 0, &opt) || !store(l, &none, res, opt) ||
      !withdraw_check(l, &run, &s, &ok))
    return 0;
  flag = operand(ok, ex_const(l, 0));
  return block_ops(l, &run, 1, &flag, 1, &opt) && store(l, &run, res, opt) &&
         if_flag(l, &rest, ex_local(l, s.bad), &none, &run) && guard(l, b, ex_local(l, s.tf), res, &rest);
}


/* transferVerify (S8 C3, P5), the transfer half (T1 of r3s25-arm-plan.md). The proof is LOW_TR_LEN words with
   LOW_TR_POINTS points (trw_proof_get); each range is LOW_TR_BITS bits (TR_BITS). The 4-word skips are before points
   LOW_TR_Q4 + 4 k (q_bits proof[k + 1], then q_bits sum) and LOW_TR_R4 + 4 k (rest_bits), k = 0..31. */
#define LOW_TR_LEN 1053u
#define LOW_TR_BITS 32u
#define LOW_TR_POINTS 396u
#define LOW_TR_Q4 72u
#define LOW_TR_Q4_END 196u
#define LOW_TR_R4 270u

/* The tags of trwire.c :7 and transfer.c :10-12: "trctx", "transfer", "trq", "trr". */
static const U256 LOW_TRW_CTX_TAG = {{0x7472637478ull, 0u, 0u, 0u}};
static const U256 LOW_TR_TAG = {{0x7472616e73666572ull, 0u, 0u, 0u}};
static const U256 LOW_TR_Q_TAG = {{0x747271u, 0u, 0u, 0u}};
static const U256 LOW_TR_REST_TAG = {{0x747272u, 0u, 0u, 0u}};

/* The operands of transferVerify (tk_transfer_verify of eval.c: rows, st, the 4 proof lists). */
typedef struct {
  uint32_t tf;   /* the local: 1 when an operand or a field is a trap */
  uint32_t bad;  /* the local: 1 when the host gives none */
  Operand pt[5]; /* the points of st: key_h, key_k, key_o, com_h, com_k */
  Operand ct[4]; /* the ciphers of st: q_h, q_k, q_o, own_h */
  Operand cp[8]; /* c1 and c2 of each cipher of CT, in that order */
  Operand pf;    /* the proof list (field 0 of the proofs) */
  Operand ml[3]; /* the m1, m2 and m3 lists (fields 1 to 3 of the proofs; jr_decode walks them) */
  Operand zero;  /* the U256 0 */
  Operand inf;   /* the point block (0, 0) */
} TrArgs;

/* The skip of the transfer proof (PfSkip): z of same[0], same[1], q_bits sum and rest_key (1 word) before points 2,
   4, 198 and 202; e0, e1, z0, z1 (4 words) before LOW_TR_Q4 + 4 k (J mod 4 = 0, J <= LOW_TR_Q4_END) and before
   LOW_TR_R4 + 4 k (J mod 4 = 2). The divisor 4 is the ex_divisor form, in PT before its use (wd_skip). */
static const IrExpr *tr_skip(Low *l, Buf *pt, const IrExpr *j) {
  const IrExpr *z;
  const IrExpr *m = ex_bin(l, IR_OP_MOD, j, ex_divisor(l, pt, ex_const(l, 4u), &z));
  const IrExpr *q = ex_bin(l, IR_OP_MUL, ex_bin(l, IR_OP_LE, ex_const(l, LOW_TR_Q4), j),
                           ex_bin(l, IR_OP_LE, j, ex_const(l, LOW_TR_Q4_END)));
  const IrExpr *q4 = ex_bin(l, IR_OP_MUL, q, ex_bin(l, IR_OP_EQ, m, ex_const(l, 0)));
  const IrExpr *r4 =
      ex_bin(l, IR_OP_MUL, ex_bin(l, IR_OP_LE, ex_const(l, LOW_TR_R4), j), ex_bin(l, IR_OP_EQ, m, ex_const(l, 2u)));
  const IrExpr *one = ex_bin(l, IR_OP_EQ, j, ex_const(l, 2u));
  one = ex_bin(l, IR_OP_ADD, one, ex_bin(l, IR_OP_EQ, j, ex_const(l, 4u)));
  one = ex_bin(l, IR_OP_ADD, one, ex_bin(l, IR_OP_EQ, j, ex_const(l, 198u)));
  one = ex_bin(l, IR_OP_ADD, one, ex_bin(l, IR_OP_EQ, j, ex_const(l, 202u)));
  return ex_bin(l, IR_OP_ADD, one, ex_bin(l, IR_OP_MUL, ex_const(l, 4u), ex_bin(l, IR_OP_ADD, q4, r4)));
}

/* 1 when the U256 words A and B are the same (IR_CALL_U256_LE both ways). */
static const IrExpr *tr_word_eq(Low *l, const IrExpr *a, const IrExpr *b) {
  const IrExpr *x = call_of(l, IR_CALL_U256_LE, 2u, a, b, NULL);
  const IrExpr *y = call_of(l, IR_CALL_U256_LE, 2u, b, a, NULL);
  return x == NULL || y == NULL ? NULL : ex_bin(l, IR_OP_MUL, x, y);
}

/* 1 when the point blocks P and Q have the same x and y (group_eq of decoded points). */
static const IrExpr *tr_pt_eq(Low *l, const IrExpr *p, const IrExpr *q) {
  const IrExpr *x = tr_word_eq(l, ex_load(l, p, 1u), ex_load(l, q, 1u));
  const IrExpr *y = tr_word_eq(l, ex_load(l, p, 3u), ex_load(l, q, 3u));
  return x == NULL || y == NULL ? NULL : ex_bin(l, IR_OP_MUL, x, y);
}

/* Phase 1 of transferVerify: the operands of V into *S. BAD = 1 when a point of st fails the point test
   (tk_statement), q_h.c1, q_k.c1 and q_o.c1 are not the same (check_statement: TR_REFUSED is none), the proof is not
   LOW_TR_LEN words, or one of its LOW_TR_POINTS points fails the point test (trw_proof_get). The m1, m2 and m3 lists
   go to S->ml (jr_decode). The list walks are REPEAT loops with a live guard (Q-S8-21). */
static int tr_decode(Low *l, Buf *b, const Value *v, TrArgs *s) {
  static const U256 zero_word = {{0u, 0u, 0u, 0u}};
  Operand x[3];
  Operand ops[2];
  const IrExpr *pts[13];
  const IrExpr *f = ex_const(l, 0);
  const IrExpr *c;
  const IrExpr *e;
  const IrExpr *e2;
  PfWalk w;
  uint32_t i;
  Buf s1 = buf_under(b);
  Buf s2 = buf_under(b);
  Buf s3 = buf_under(b);
  Buf kb = buf_under(&s3);
  w.cur = local_new(l);
  w.len = local_new(l);
  w.idx = local_new(l);
  w.ks = local_new(l);
  w.sk = local_new(l);
  s->tf = local_new(l);
  s->bad = local_new(l);
  c = ex_local(l, w.cur);
  for (i = 0; i < 3u; i++) {
    if (!lower(l, b, v->args[i], &x[i]))
      return 0;
    f = ex_or(l, f, x[i].f);
  }
  if (!set(l, b, s->tf, f) || !set(l, b, s->bad, ex_const(l, 0)) || !word_block(l, b, &zero_word, &s->zero))
    return 0;
  ops[0] = s->zero;
  ops[1] = s->zero;
  if (!block_ops(l, b, 0, ops, 2u, &s->inf))
    return 0;
  /* The 9 parts of st (5 points, then 4 ciphers) and the proof list. A Prod value is a right-nested pair chain: st
     is field 0 eight times and the last tail is ct[3]; the proofs are pf = field 0, r1 = field 1, ml[0] = r1.0,
     r2 = r1.1, ml[1] = r2.0 and ml[2] = r2.1. The flag of each tail goes into the trap flag. */
  f = ex_const(l, 0);
  {
    Operand rest = x[1];
    Operand nx;
    Operand r1;
    Operand r2;
    for (i = 0; i < 9u; i++) {
      Operand *o = i < 5u ? &s->pt[i] : &s->ct[i - 5u];
      if (i == 8u) {
        *o = rest;
      } else {
        if (!field_at(l, &s1, rest.w, 0, o) || !field_at(l, &s1, rest.w, 1u, &nx))
          return 0;
        f = ex_or(l, f, nx.f);
        rest = nx;
      }
      f = ex_or(l, f, o->f);
    }
    if (!field_at(l, &s1, x[2].w, 1u, &r1) || !field_at(l, &s1, r1.w, 0, &s->ml[0]) ||
        !field_at(l, &s1, r1.w, 1u, &r2) || !field_at(l, &s1, r2.w, 0, &s->ml[1]) ||
        !field_at(l, &s1, r2.w, 1u, &s->ml[2]))
      return 0;
    f = ex_or(l, ex_or(l, f, r1.f), r2.f);
    for (i = 0; i < 3u; i++)
      f = ex_or(l, f, s->ml[i].f);
  }
  if (!field_at(l, &s1, x[2].w, 0, &s->pf) || !set(l, &s1, s->tf, ex_or(l, f, s->pf.f)) ||
      !when_live(l, b, s->tf, s->bad, &s1))
    return 0;
  /* The 2 points of each cipher (after the trap test of the ciphers). */
  f = ex_const(l, 0);
  for (i = 0; i < 8u; i++) {
    if (!field_at(l, &s2, s->ct[i / 2u].w, i % 2u, &s->cp[i]))
      return 0;
    f = ex_or(l, f, s->cp[i].f);
  }
  if (!set(l, &s2, s->tf, f) || !when_live(l, b, s->tf, s->bad, &s2))
    return 0;
  /* tk_statement: the point test of the 13 points; check_statement: the c1 of q_h, q_k and q_o. */
  for (i = 0; i < 5u; i++)
    pts[i] = s->pt[i].w;
  for (i = 0; i < 8u; i++)
    pts[5u + i] = s->cp[i].w;
  e = tr_pt_eq(l, s->cp[0].w, s->cp[2].w);
  e2 = tr_pt_eq(l, s->cp[0].w, s->cp[4].w);
  e = e == NULL || e2 == NULL ? NULL : ex_bin(l, IR_OP_MUL, e, e2);
  if (e == NULL || !wd_pt_test(l, &kb, s->bad, ex_load(l, c, 1u), s->inf.w) || !set(l, &kb, w.cur, ex_load(l, c, 3u)) ||
      !set(l, &s3, w.cur, word_list(l, &s3, pts, 13u)) || !st_repeat(l, &s3, ex_const(l, 13u), &kb) ||
      !set(l, &s3, s->bad, ex_or(l, ex_local(l, s->bad), ex_bin(l, IR_OP_EQ, e, ex_const(l, 0)))) ||
      !when_live(l, b, s->tf, s->bad, &s3))
    return 0;
  /* The proof (trw_proof_get): LOW_TR_LEN words, LOW_TR_POINTS points. */
  return pf_walk(l, b, &w, s->tf, s->bad, s->pf.w, s->inf.w, LOW_TR_LEN, LOW_TR_POINTS, tr_skip, NULL);
}

/* The 7 words of one dleq of wd_sums at P: PK, C1, C2, the points AB[0] (a) and AB[1] (b), Z and the context CX. */
static void tr_dleq(const IrExpr **p, const IrExpr *pk, const IrExpr *c1, const IrExpr *c2, const Operand *ab,
                    const IrExpr *z, const IrExpr *cx) {
  p[0] = pk;
  p[1] = c1;
  p[2] = c2;
  p[3] = ab[0].w;
  p[4] = ab[1].w;
  p[5] = z;
  p[6] = cx;
}

/* The transfer half of transferVerify (transfer_verify of transfer.c, after the decode): ctx (trw_ctx), then cx.s,
   cx.q and cx.r (contexts); the 2 range checks of LOW_TR_BITS bits (wd_bits, then wd_horner: q_bits on q_h with cx.q,
   rest_bits on rest with cx.r; the rest range has its own Horner locals, so the q acc stays); then one wd_sums of the 5
   dleqs: same[0] and same[1] (same_part) and rest_key (rest_part) with cx.s, the q sum with cx.q, the rest sum with
   cx.r. R gets new locals and R->OK is the flag: after the decode no step gives TR_REFUSED (check_statement is in
   tr_decode; derived points are on the curve or infinity). The proof walk: same[0], same[1], q_bits, rest, rest_key,
   rest_bits (trw_proof_get). */
static int tr_half(Low *l, Buf *go, const TrArgs *s, WdRun *r) {
  GroupPoint gen;
  U256 gx;
  U256 gy;
  Operand ops[2];
  Operand tag[4];
  Operand pr[12];
  Operand neg[12];
  const IrExpr *items[16];
  const IrExpr *src[12];
  const IrExpr *d[10];
  const IrExpr *ps[35];
  uint32_t cx[3];
  uint32_t s0w[5];
  uint32_t s1w[5];
  uint32_t qw[5];
  uint32_t rw[4];
  uint32_t kw[5];
  uint32_t xw[5];
  uint32_t i;
  WdRun rr;
  Buf st1 = buf_under(go);
  Buf st2 = buf_under(go);
  r->ok = local_new(l);
  r->ctx = local_new(l);
  r->cr = local_new(l);
  r->bh = local_new(l);
  r->pc = local_new(l);
  r->a1 = local_new(l);
  r->a2 = local_new(l);
  cx[0] = local_new(l);
  cx[1] = local_new(l);
  cx[2] = r->cr;
  group_gen(&gen);
  group_to_words(&gx, &gy, &gen);
  if (!word_block(l, go, &gx, &ops[0]) || !word_block(l, go, &gy, &ops[1]) || !block_ops(l, go, 0, ops, 2u, &r->g) ||
      !pt_neg(l, go, r->g.w, s->zero, &r->ng) || !word_block(l, go, &LOW_TRW_CTX_TAG, &tag[0]) ||
      !word_block(l, go, &LOW_TR_TAG, &tag[1]) || !word_block(l, go, &LOW_TR_Q_TAG, &tag[2]) ||
      !word_block(l, go, &LOW_TR_REST_TAG, &tag[3]) || !set(l, go, r->ok, ex_const(l, 1)))
    return 0;
  rr = *r;
  rr.a1 = local_new(l);
  rr.a2 = local_new(l);
  /* ctx (trw_ctx): the chain from TRW_CTX_TAG over key_h, key_k, then c1 and c2 of q_h, q_k and q_o (x, then y). */
  items[0] = ex_load(l, s->pt[0].w, 1u);
  items[1] = ex_load(l, s->pt[0].w, 3u);
  items[2] = ex_load(l, s->pt[1].w, 1u);
  items[3] = ex_load(l, s->pt[1].w, 3u);
  for (i = 0; i < 6u; i++) {
    items[4u + 2u * i] = ex_load(l, s->cp[i].w, 1u);
    items[5u + 2u * i] = ex_load(l, s->cp[i].w, 3u);
  }
  if (!set(l, go, r->ctx, tag[0].w) || !hash_fold(l, go, r->ctx, items, 16u))
    return 0;
  /* cx.s, cx.q, cx.r = H(tag, ctx) (contexts of transfer.c). */
  items[0] = ex_local(l, r->ctx);
  for (i = 0; i < 3u; i++) {
    if (!set(l, go, cx[i], tag[1u + i].w) || !hash_fold(l, go, cx[i], items, 1u))
      return 0;
  }
  /* The proof walk. bh := bit[0] and pc := proof[0] of each range (the step loops); wd_bits moves pc to its sum. */
  if (!set(l, go, r->pc, s->pf.w) || !wd_take(l, go, r->pc, s0w, 5u) || !wd_take(l, go, r->pc, s1w, 5u) ||
      !set(l, go, r->bh, ex_local(l, r->pc)) || !set(l, &st1, r->pc, ex_load(l, ex_local(l, r->pc), 3u)) ||
      !st_repeat(l, go, ex_const(l, 4u * LOW_TR_BITS), &st1) ||
      !wd_bits(l, go, r, s->pt[0].w, cx[1], LOW_TR_BITS, 0u) || !wd_horner(l, go, r, LOW_TR_BITS) ||
      !wd_take(l, go, r->pc, qw, 5u) || !wd_take(l, go, r->pc, rw, 4u) || !wd_take(l, go, r->pc, kw, 5u) ||
      !set(l, go, r->bh, ex_local(l, r->pc)) || !set(l, &st2, r->pc, ex_load(l, ex_local(l, r->pc), 3u)) ||
      !st_repeat(l, go, ex_const(l, 4u * LOW_TR_BITS), &st2) ||
      !wd_bits(l, go, &rr, s->pt[0].w, cx[2], LOW_TR_BITS, 0u) || !wd_horner(l, go, &rr, LOW_TR_BITS) ||
      !wd_take(l, go, r->pc, xw, 5u))
    return 0;
  /* pr: same[0] a b, same[1] a b, q sum a b, rest c1 c2, rest_key a b, rest sum a b. */
  for (i = 0; i < 2u; i++) {
    if (!wd_point(l, go, &s0w[2u * i], &pr[i]) || !wd_point(l, go, &s1w[2u * i], &pr[2u + i]) ||
        !wd_point(l, go, &qw[2u * i], &pr[4u + i]) || !wd_point(l, go, &rw[2u * i], &pr[6u + i]) ||
        !wd_point(l, go, &kw[2u * i], &pr[8u + i]) || !wd_point(l, go, &xw[2u * i], &pr[10u + i]))
      return 0;
  }
  /* -key_k, -key_o, -q_k.c2, -q_o.c2, -q_h.c1, -q_h.c2, -rest.c1, -rest.c2, then -acc of each range (c1, c2). */
  src[0] = s->pt[1].w;
  src[1] = s->pt[2].w;
  src[2] = s->cp[3].w;
  src[3] = s->cp[5].w;
  src[4] = s->cp[0].w;
  src[5] = s->cp[1].w;
  src[6] = pr[6].w;
  src[7] = pr[7].w;
  src[8] = ex_local(l, r->a1);
  src[9] = ex_local(l, r->a2);
  src[10] = ex_local(l, rr.a1);
  src[11] = ex_local(l, rr.a2);
  for (i = 0; i < 12u; i++) {
    if (!pt_neg(l, go, src[i], s->zero, &neg[i]))
      return 0;
  }
  /* same_part (key_h - key_i, q_h.c2 - q_i.c2), q_h - acc, rest_part (own_h - q_h - rest), rest - acc. */
  d[0] = pt_op(l, go, IR_CALL_EC_ADD, s->pt[0].w, neg[0].w);
  d[1] = pt_op(l, go, IR_CALL_EC_ADD, s->cp[1].w, neg[2].w);
  d[2] = pt_op(l, go, IR_CALL_EC_ADD, s->pt[0].w, neg[1].w);
  d[3] = pt_op(l, go, IR_CALL_EC_ADD, s->cp[1].w, neg[3].w);
  d[4] = pt_op(l, go, IR_CALL_EC_ADD, s->cp[0].w, neg[8].w);
  d[5] = pt_op(l, go, IR_CALL_EC_ADD, s->cp[1].w, neg[9].w);
  d[6] = pt_op(l, go, IR_CALL_EC_ADD, s->cp[6].w, neg[4].w);
  d[6] = pt_op(l, go, IR_CALL_EC_ADD, d[6], neg[6].w);
  d[7] = pt_op(l, go, IR_CALL_EC_ADD, s->cp[7].w, neg[5].w);
  d[7] = pt_op(l, go, IR_CALL_EC_ADD, d[7], neg[7].w);
  d[8] = pt_op(l, go, IR_CALL_EC_ADD, pr[6].w, neg[10].w);
  d[9] = pt_op(l, go, IR_CALL_EC_ADD, pr[7].w, neg[11].w);
  for (i = 0; i < 10u; i++) {
    if (d[i] == NULL)
      return 0;
  }
  tr_dleq(&ps[0], d[0], s->cp[0].w, d[1], &pr[0], ex_local(l, s0w[4]), ex_local(l, cx[0]));
  tr_dleq(&ps[7], d[2], s->cp[0].w, d[3], &pr[2], ex_local(l, s1w[4]), ex_local(l, cx[0]));
  tr_dleq(&ps[14], d[6], s->pt[0].w, d[7], &pr[8], ex_local(l, kw[4]), ex_local(l, cx[0]));
  tr_dleq(&ps[21], s->pt[0].w, d[4], d[5], &pr[4], ex_local(l, qw[4]), ex_local(l, cx[1]));
  tr_dleq(&ps[28], s->pt[0].w, d[8], d[9], &pr[10], ex_local(l, xw[4]), ex_local(l, cx[2]));
  return wd_sums(l, go, r, s->zero.w, ps, 5u);
}

/* The group order n of secp256k1 (GROUP_N of group.c is a Modulus): the bound
   of each s word of attestVerify. */
static const U256 LOW_SECP_N = {{0xbfd25e8cd0364141ull, 0xbaaedce6af48a03bull, 0xfffffffffffffffeull, 0xffffffffffffffffull}};

/* The jointr half of transferVerify (jointr_verify of jointr.c): the static table (lower time, Q-S8-30) and the
   setup values of jointr.c :224-250 (run time). */
typedef struct {
  JrTable t;                       /* the table of the rows (eval_tr_table: the host table) */
  uint32_t nm;                     /* the M of the table (tk_table) */
  uint64_t v[JR_C_MAX * JR_C_MAX]; /* the caps of jointr_caps (V) */
  uint32_t m;                      /* the M of jointr_caps */
  Operand n;                       /* the LOW_SECP_N block */
  Operand g1;                      /* G1, G2 and H (claimcom_generator; point blocks) */
  Operand g2;
  Operand h;
  Operand jg[JR_C_MAX];            /* juris[i] G1 of each class (host group_mul) */
  Operand sg[JR_C_MAX];            /* status[i] G2 of each class (host group_mul) */
  uint32_t mh;                     /* the local: mu_h = key_weight(key_h, key_k, key_h) mod n */
  uint32_t mk;                     /* the local: mu_k = key_weight(key_h, key_k, key_k) mod n */
  uint32_t mu;                     /* the local: mu = mu_h + mu_k mod n */
  uint32_t cg;                     /* the locals: ctx_g, ctx_f and ctx_y = H(tag, ctx) */
  uint32_t cf;
  uint32_t cy;
  const IrExpr *jk;                /* J = mu_h key_h + mu_k key_k (jointr_key) */
  const IrExpr *qj2;               /* qj.c2 = mu_h q_h.c2 + mu_k q_k.c2 (qj.c1 is q_h.c1: cp[0]) */
} JrRun;

/* The static part of the jointr half (Q-S8-30): the table of the closed ROWS at lower time, then its caps. 2 when
   ROWS is not closed (the lowering refuses), 0 on a bad table (each input gives none, as eval), else 1. */
static int jr_table(Low *l, const Value *rows, JrRun *j) {
  int rc;
  memset(j, 0, sizeof *j);
  rc = eval_tr_table(l->m, &j->t, &j->nm, rows);
  if (rc == 2)
    return refuse(l, "a transferVerify rows term that is not closed");
  return rc == 1 ? jointr_caps(j->v, &j->m, &j->t) != 0 : rc;
}

/* *OUT = the point block of the host point P (lower time; the group_gen form of tr_half). */
static int jr_point(Low *l, Buf *go, GroupPoint *p, Operand *out) {
  U256 x;
  U256 y;
  Operand ops[2];
  group_to_words(&x, &y, p);
  return word_block(l, go, &x, &ops[0]) && word_block(l, go, &y, &ops[1]) && block_ops(l, go, 0, ops, 2u, out);
}

/* LOCAL := key_weight(key_h, key_k, KI) mod n (key_weight, then blsag_reduce): the hash fold from TAG (JR_TAG, all 4
   words) over x, then y of key_h, key_k and KI; then one U256_SUB of n when n <= the fold (a hash is under 2 n). */
static int jr_weight(Low *l, Buf *go, const TrArgs *s, const JrRun *j, const Operand *tag, uint32_t local,
                     const IrExpr *ki) {
  const IrExpr *items[6];
  const IrExpr *le;
  const IrExpr *sub;
  Buf yes = buf_under(go);
  Buf no = buf_under(go);
  items[0] = ex_load(l, s->pt[0].w, 1u);
  items[1] = ex_load(l, s->pt[0].w, 3u);
  items[2] = ex_load(l, s->pt[1].w, 1u);
  items[3] = ex_load(l, s->pt[1].w, 3u);
  items[4] = ex_load(l, ki, 1u);
  items[5] = ex_load(l, ki, 3u);
  if (!set(l, go, local, tag->w) || !hash_fold(l, go, local, items, 6u))
    return 0;
  le = call_of(l, IR_CALL_U256_LE, 2u, j->n.w, ex_local(l, local), NULL);
  le = le == NULL ? NULL : atom(l, go, le);
  sub = call_of(l, IR_CALL_U256_SUB, 2u, ex_local(l, local), j->n.w, NULL);
  return le != NULL && sub != NULL && set(l, &yes, local, sub) && st_if(l, go, le, &yes, &no);
}

/* The setup of the jointr half (jointr.c :224-250, after tr_decode and tr_half): G1, G2, H and the class points
   (lower time); mu_h, mu_k, mu and J (jointr_key); qj.c2; ctx_g, ctx_f and ctx_y from R->ctx (trw_ctx). BAD gets 1
   when mu is 0 or J is (0, 0) (JR_REFUSED: eval gives none). jr_table sets the table of J first. */
static int jr_setup(Low *l, Buf *go, const TrArgs *s, const WdRun *r, JrRun *j) {
  GroupPoint g[3];
  GroupPoint p;
  Operand tag[4];
  const IrExpr *items[1];
  const IrExpr *mu;
  const IrExpr *t;
  const IrExpr *u;
  const IrExpr *e[2];
  uint32_t cxs[3];
  uint32_t c = 0;
  uint32_t i;
  if (!claimcom_generator(&g[0], &c, &CLAIMCOM_TAG_G1) || !claimcom_generator(&g[1], &c, &CLAIMCOM_TAG_G2) ||
      !claimcom_generator(&g[2], &c, &CLAIMCOM_TAG_H) || !jr_point(l, go, &g[0], &j->g1) ||
      !jr_point(l, go, &g[1], &j->g2) || !jr_point(l, go, &g[2], &j->h) || !word_block(l, go, &LOW_SECP_N, &j->n) ||
      !word_block(l, go, &JR_TAG, &tag[0]) || !word_block(l, go, &JR_G_TAG, &tag[1]) ||
      !word_block(l, go, &JR_F_TAG, &tag[2]) || !word_block(l, go, &JR_Y_TAG, &tag[3]))
    return 0;
  for (i = 0; i < j->t.c && i < JR_C_MAX; i++) {
    U256 a = {{j->t.juris[i], 0u, 0u, 0u}};
    U256 b = {{j->t.status[i], 0u, 0u, 0u}};
    group_mul(&p, &g[0], &a);
    if (!jr_point(l, go, &p, &j->jg[i]))
      return 0;
    group_mul(&p, &g[1], &b);
    if (!jr_point(l, go, &p, &j->sg[i]))
      return 0;
  }
  j->mh = local_new(l);
  j->mk = local_new(l);
  j->mu = local_new(l);
  j->cg = local_new(l);
  j->cf = local_new(l);
  j->cy = local_new(l);
  if (!jr_weight(l, go, s, j, &tag[0], j->mh, s->pt[0].w) || !jr_weight(l, go, s, j, &tag[0], j->mk, s->pt[1].w))
    return 0;
  /* mu (FE_ADD: both are under n), J, then qj.c2. */
  mu = call_of(l, IR_CALL_FE_ADD, 3u, ex_local(l, j->mh), ex_local(l, j->mk), j->n.w);
  t = pt_op(l, go, IR_CALL_EC_MUL, s->pt[0].w, ex_local(l, j->mh));
  u = pt_op(l, go, IR_CALL_EC_MUL, s->pt[1].w, ex_local(l, j->mk));
  j->jk = pt_op(l, go, IR_CALL_EC_ADD, t, u);
  t = pt_op(l, go, IR_CALL_EC_MUL, s->cp[1].w, ex_local(l, j->mh));
  u = pt_op(l, go, IR_CALL_EC_MUL, s->cp[3].w, ex_local(l, j->mk));
  j->qj2 = pt_op(l, go, IR_CALL_EC_ADD, t, u);
  if (mu == NULL || j->jk == NULL || j->qj2 == NULL || !set(l, go, j->mu, mu))
    return 0;
  e[0] = tr_word_eq(l, ex_local(l, j->mu), s->zero.w);
  e[1] = tr_pt_eq(l, j->jk, s->inf.w);
  if (e[0] == NULL || e[1] == NULL || !set(l, go, s->bad, ex_or(l, ex_local(l, s->bad), ex_or(l, e[0], e[1]))))
    return 0;
  /* ctx_g, ctx_f, ctx_y = H(tag, ctx): the tag is the first input (jointr.c :12-14). */
  items[0] = ex_local(l, r->ctx);
  cxs[0] = j->cg;
  cxs[1] = j->cf;
  cxs[2] = j->cy;
  for (i = 0; i < 3u; i++) {
    if (!set(l, go, cxs[i], tag[1u + i].w) || !hash_fold(l, go, cxs[i], items, 1u))
      return 0;
  }
  return 1;
}

/* check_m1, the gates (jointr.c:468-475), for each gate G < M of the caps (J->m): the 33 bit proofs (wd_bits: pk J,
   ctx_g, the chain index 33 G + I), then the gate cipher (gate_cipher, jointr.c:405-429: wd_horner over the 33 bit
   ciphers, then acc := mu acc + qj, then c2 := c2 - k G with k G = mu ((v + 2^32) G), v the cap of G, (v + 2^32) G
   at lower time), then the sum dleq (sig_rand_verify with m = 0: one wd_sums set of J, the cipher and ctx_g). R->pc
   is the cell of gate 0 (the M1 list) at the start and the cell of f[0] at the end. R->ok is the flag. A cap over
   2^64 - 2^32 is a lowering error (0). KW[4 G] to KW[4 G + 3] get the words of g[G].bit[32] (for f_statement). */
static int jr_gates(Low *l, Buf *go, const TrArgs *s, WdRun *r, const JrRun *j, uint32_t *kw) {
  GroupPoint gen;
  GroupPoint p;
  Operand vg;
  Operand ab[2];
  Operand neg;
  const IrExpr *ps[7];
  const IrExpr *c[4];
  uint32_t sw[5];
  uint32_t g;
  uint32_t t;
  group_gen(&gen);
  for (g = 0; g < j->m; g++) {
    Buf st = buf_under(go);
    Buf kt = buf_under(go);
    const U256 k = {{j->v[g] + 0x100000000ull, 0u, 0u, 0u}};
    if (j->v[g] > ~0ull - 0x100000000ull)
      return 0;
    group_mul(&p, &gen, &k);
    /* Bit 32 of gate G: 4 * 32 cells after the cell of bit 0 (R->pc here). */
    t = local_new(l);
    if (!set(l, go, t, ex_local(l, r->pc)) || !set(l, &kt, t, ex_load(l, ex_local(l, t), 3u)) ||
        !st_repeat(l, go, ex_const(l, 4u * (JR_GATE_BITS - 1u)), &kt) || !wd_take(l, go, t, &kw[4u * g], 4u))
      return 0;
    if (!set(l, go, r->bh, ex_local(l, r->pc)) || !set(l, &st, r->pc, ex_load(l, ex_local(l, r->pc), 3u)) ||
        !st_repeat(l, go, ex_const(l, 4u * JR_GATE_BITS), &st) ||
        !wd_bits(l, go, r, j->jk, j->cg, JR_GATE_BITS, g * JR_GATE_BITS) || !wd_horner(l, go, r, JR_GATE_BITS) ||
        !wd_take(l, go, r->pc, sw, 5u) || !wd_point(l, go, &sw[0], &ab[0]) || !wd_point(l, go, &sw[2], &ab[1]) ||
        !jr_point(l, go, &p, &vg))
      return 0;
    c[0] = pt_op(l, go, IR_CALL_EC_MUL, ex_local(l, r->a1), ex_local(l, j->mu));
    c[0] = pt_op(l, go, IR_CALL_EC_ADD, c[0], s->cp[0].w);
    c[1] = pt_op(l, go, IR_CALL_EC_MUL, ex_local(l, r->a2), ex_local(l, j->mu));
    c[1] = pt_op(l, go, IR_CALL_EC_ADD, c[1], j->qj2);
    c[2] = pt_op(l, go, IR_CALL_EC_MUL, vg.w, ex_local(l, j->mu));
    if (c[0] == NULL || c[1] == NULL || c[2] == NULL || !pt_neg(l, go, c[2], s->zero, &neg))
      return 0;
    c[3] = pt_op(l, go, IR_CALL_EC_ADD, c[1], neg.w);
    if (c[3] == NULL)
      return 0;
    tr_dleq(ps, j->jk, c[0], c[3], ab, ex_local(l, sw[4]), ex_local(l, j->cg));
    if (!wd_sums(l, go, r, s->zero.w, ps, 1u))
      return 0;
  }
  return 1;
}

/* T3b (r3 s31). The statement of one or_verify atom (JrAtom): b1, p1, b2, p2 as point exprs. B2 == NULL is a class
   atom (b2 = p2 = (0, 0)): its fold words are 0 and its second eq_holds is B == (0, 0) (z (0, 0) = B + e (0, 0)). */
typedef struct {
  const IrExpr *b1;
  const IrExpr *p1;
  const IrExpr *b2;
  const IrExpr *p2;
} JrAt;

/* n - 1: -P = (n - 1) P at lower time (the class points of f_statement). */
static const U256 JR_N1 = {{0xbfd25e8cd0364140ull, 0xbaaedce6af48a03bull, 0xfffffffffffffffeull,
                            0xffffffffffffffffull}};

static void jr_at(JrAt *at, const IrExpr *b1, const IrExpr *p1, const IrExpr *b2, const IrExpr *p2) {
  at->b1 = b1;
  at->p1 = p1;
  at->b2 = b2;
  at->p2 = p2;
}

/* R->ok := R->ok F (a JR_BAD_PROOF test). */
static int jr_and(Low *l, Buf *go, const WdRun *r, const IrExpr *f) {
  return f != NULL && set(l, go, r->ok, ex_bin(l, IR_OP_MUL, ex_local(l, r->ok), f));
}

/* BAD := 1 when the point P fails group_from_words (P + (0, 0) is 0). get_point (trwire.c) refuses such a wire
   point in the decode, before any check, and eval gives none on a bad decode (whatever R->ok is). group_on_curve
   takes each decoded point, so no check of jointr.c refuses one after the decode. jr_decode tests each point first:
   this test is a second guard. */
static int jr_refuse(Low *l, Buf *go, const TrArgs *s, const IrExpr *p) {
  const IrExpr *e = pt_op(l, go, IR_CALL_EC_ADD, p, s->inf.w);
  return e != NULL &&
         or_into(l, go, s->bad, ex_bin(l, IR_OP_EQ, e, ex_const(l, 0)));
}

/* eq_holds of jointr.c: z B == A + e P (NULL on a lowering error). */
static const IrExpr *jr_eq(Low *l, Buf *go, const IrExpr *b, const IrExpr *z, const IrExpr *a, const IrExpr *e,
                           const IrExpr *p) {
  const IrExpr *lhs = pt_op(l, go, IR_CALL_EC_MUL, b, z);
  const IrExpr *rhs = pt_op(l, go, IR_CALL_EC_MUL, p, e);
  rhs = rhs == NULL ? NULL : pt_op(l, go, IR_CALL_EC_ADD, a, rhs);
  return lhs == NULL || rhs == NULL ? NULL : pt_eq(l, lhs, rhs);
}

/* or_verify (jointr.c:310-341) of N branches of K atoms; T4 uses it for Y too. AT[I K + J] is the statement of atom
   (I, J), E[I] the local of e[I], W[5 (I K + J)] the locals of A.x, A.y, B.x, B.y and z of atom (I, J), CTX the local
   of the context. Step 1 (A, B on the curve): jr_refuse. Step 2 (or_challenge, jointr.c:253-267): H := CTX (no tag
   hash), the fold of b1, p1, b2, p2, A, B of each atom (x, then y), then blsag_reduce (one U256_SUB of n). Steps 3
   and 4: SUM := e[0] + ... + e[N - 1] mod n (FE_ADD), e[I] < n for each I when N > 1, SUM == H (for N = 1 this is
   U256_EQ(e[0], H), facts F row 49). Step 5: the 2 eq_holds of each atom. R->ok is the flag. */
static int jr_or(Low *l, Buf *go, const TrArgs *s, const WdRun *r, const JrRun *j, const JrAt *at, const uint32_t *e,
                 const uint32_t *w, uint32_t n, uint32_t k, uint32_t ctx) {
  Operand ab[2u * JR_C_MAX * (JR_C_MAX + 1u)];
  const IrExpr *items[12];
  const IrExpr *z;
  const IrExpr *t;
  const IrExpr *q[2];
  Buf yes = buf_under(go);
  Buf no = buf_under(go);
  uint32_t h = local_new(l);
  uint32_t sum = local_new(l);
  uint32_t i;
  uint32_t a;
  for (a = 0; a < n * k; a++) {
    if (!wd_point(l, go, &w[5u * a], &ab[2u * a]) || !wd_point(l, go, &w[5u * a + 2u], &ab[2u * a + 1u]) ||
        !jr_refuse(l, go, s, ab[2u * a].w) || !jr_refuse(l, go, s, ab[2u * a + 1u].w))
      return 0;
  }
  if (!set(l, go, h, ex_local(l, ctx)))
    return 0;
  for (a = 0; a < n * k; a++) {
    items[0] = ex_load(l, at[a].b1, 1u);
    items[1] = ex_load(l, at[a].b1, 3u);
    items[2] = ex_load(l, at[a].p1, 1u);
    items[3] = ex_load(l, at[a].p1, 3u);
    items[4] = at[a].b2 == NULL ? s->zero.w : ex_load(l, at[a].b2, 1u);
    items[5] = at[a].b2 == NULL ? s->zero.w : ex_load(l, at[a].b2, 3u);
    items[6] = at[a].p2 == NULL ? s->zero.w : ex_load(l, at[a].p2, 1u);
    items[7] = at[a].p2 == NULL ? s->zero.w : ex_load(l, at[a].p2, 3u);
    for (i = 0; i < 4u; i++)
      items[8u + i] = ex_local(l, w[5u * a + i]);
    if (!hash_fold(l, go, h, items, 12u))
      return 0;
  }
  t = call_of(l, IR_CALL_U256_LE, 2u, j->n.w, ex_local(l, h), NULL);
  t = t == NULL ? NULL : atom(l, go, t);
  z = call_of(l, IR_CALL_U256_SUB, 2u, ex_local(l, h), j->n.w, NULL);
  if (t == NULL || z == NULL || !set(l, &yes, h, z) || !st_if(l, go, t, &yes, &no) ||
      !set(l, go, sum, ex_local(l, e[0])))
    return 0;
  for (i = 1; i < n; i++) {
    t = call_of(l, IR_CALL_FE_ADD, 3u, ex_local(l, sum), ex_local(l, e[i]), j->n.w);
    if (t == NULL || !set(l, go, sum, t))
      return 0;
  }
  for (i = 0; n > 1u && i < n; i++) {
    t = call_of(l, IR_CALL_U256_LE, 2u, j->n.w, ex_local(l, e[i]), NULL);
    if (t == NULL || !jr_and(l, go, r, ex_bin(l, IR_OP_EQ, t, ex_const(l, 0))))
      return 0;
  }
  if (!jr_and(l, go, r, call_of(l, IR_CALL_U256_EQ, 2u, ex_local(l, sum), ex_local(l, h), NULL)))
    return 0;
  for (a = 0; a < n * k; a++) {
    z = ex_local(l, w[5u * a + 4u]);
    t = ex_local(l, e[a / k]);
    q[0] = jr_eq(l, go, at[a].b1, z, ab[2u * a].w, t, at[a].p1);
    q[1] = at[a].b2 == NULL ? tr_pt_eq(l, ab[2u * a + 1u].w, s->inf.w)
                            : jr_eq(l, go, at[a].b2, z, ab[2u * a + 1u].w, t, at[a].p2);
    if (q[0] == NULL || q[1] == NULL || !jr_and(l, go, r, ex_bin(l, IR_OP_MUL, q[0], q[1])))
      return 0;
  }
  return 1;
}

/* D of f_statement (jointr.c:383-385): F (the c1 and c2 blocks of f[B]), minus bit 32 of the gate
   cap_index(cap[A][B]) (KW of jr_gates) when admit[A][B] != 0 (cipher_sub). The gate is the G < M of the caps with
   v[G] == cap[A][B]; none is a lowering error. */
static int jr_d(Low *l, Buf *go, const TrArgs *s, const JrRun *j, const uint32_t *kw, uint32_t a, uint32_t b,
                const Operand *f, const IrExpr **d) {
  Operand kp[2];
  Operand neg[2];
  uint32_t g = 0;
  d[0] = f[0].w;
  d[1] = f[1].w;
  if (j->t.admit[a][b] == 0u)
    return 1;
  while (g < j->m && j->v[g] != j->t.cap[a][b])
    g++;
  if (g == j->m)
    return refuse(l, "a jointr cap");
  if (!wd_point(l, go, &kw[4u * g], &kp[0]) || !wd_point(l, go, &kw[4u * g + 2u], &kp[1]) ||
      !pt_neg(l, go, kp[0].w, s->zero, &neg[0]) || !pt_neg(l, go, kp[1].w, s->zero, &neg[1]))
    return 0;
  d[0] = pt_op(l, go, IR_CALL_EC_ADD, f[0].w, neg[0].w);
  d[1] = pt_op(l, go, IR_CALL_EC_ADD, f[1].w, neg[1].w);
  return d[0] != NULL && d[1] != NULL;
}

/* T4a (r3 s33). The point p1 of class_atom (jointr.c:356) at class A: COM - juris[A] G1 - status[A] G2. GS holds G1
   and G2 (claimcom_generator); the 2 negated points are of lower time (JR_N1). NULL on a lowering error. */
static const IrExpr *jr_class(Low *l, Buf *go, const JrRun *j, const GroupPoint *gs, const IrExpr *com, uint32_t a) {
  GroupPoint p;
  GroupPoint q[2];
  Operand ng[2];
  U256 ka = {{j->t.juris[a], 0u, 0u, 0u}};
  U256 kb = {{j->t.status[a], 0u, 0u, 0u}};
  const IrExpr *cp;
  group_mul(&p, &gs[0], &ka);
  group_mul(&q[0], &p, &JR_N1);
  group_mul(&p, &gs[1], &kb);
  group_mul(&q[1], &p, &JR_N1);
  if (!jr_point(l, go, &q[0], &ng[0]) || !jr_point(l, go, &q[1], &ng[1]))
    return NULL;
  cp = pt_op(l, go, IR_CALL_EC_ADD, com, ng[0].w);
  return cp == NULL ? NULL : pt_op(l, go, IR_CALL_EC_ADD, cp, ng[1].w);
}

/* check_m1 after the gates (jointr.c:476-482). R->pc is the cell of f[0] (the end of jr_gates). The c f ciphers (4
   words: c1.x, c1.y, c2.x, c2.y) and their on-curve test (cipher_on_curve, JR_REFUSED: jr_refuse); then f_statement
   (n = c, k = c + 1: atom (A, 0) = class_atom(com_h, A), atom (A, 1 + B) = zero_atom(d), d of jr_d); then jr_or with
   ctx_f. The class point (com_h - juris G1) - status G2 adds the 2 negated points of lower time (JR_N1). The word
   order of f_proof is NOT checked by a run: e[0] to e[n - 1], then the atoms in the order (I, J), 5 words each.
   R->pc ends after f_proof. FP[2 B] and FP[2 B + 1] get the blocks of f[B].c1 and f[B].c2 (for jr_y). */
static int jr_f(Low *l, Buf *go, const TrArgs *s, WdRun *r, const JrRun *j, const uint32_t *kw, Operand *fp) {
  GroupPoint gs[2];
  JrAt at[JR_C_MAX * (JR_C_MAX + 1u)];
  const IrExpr *cp;
  const IrExpr *d[2];
  uint32_t fw[4u * JR_C_MAX];
  uint32_t ew[JR_C_MAX];
  uint32_t aw[5u * JR_C_MAX * (JR_C_MAX + 1u)];
  uint32_t c = j->t.c;
  uint32_t cnt = 0;
  uint32_t a;
  uint32_t b;
  if (c == 0u || c > JR_C_MAX)
    return refuse(l, "a jointr class count");
  if (!claimcom_generator(&gs[0], &cnt, &CLAIMCOM_TAG_G1) || !claimcom_generator(&gs[1], &cnt, &CLAIMCOM_TAG_G2) ||
      !wd_take(l, go, r->pc, fw, 4u * c))
    return 0;
  for (b = 0; b < c; b++) {
    if (!wd_point(l, go, &fw[4u * b], &fp[2u * b]) || !wd_point(l, go, &fw[4u * b + 2u], &fp[2u * b + 1u]) ||
        !jr_refuse(l, go, s, fp[2u * b].w) || !jr_refuse(l, go, s, fp[2u * b + 1u].w))
      return 0;
  }
  for (a = 0; a < c; a++) {
    cp = jr_class(l, go, j, gs, s->pt[3].w, a);
    if (cp == NULL)
      return 0;
    jr_at(&at[a * (c + 1u)], j->h.w, cp, NULL, NULL);
    for (b = 0; b < c; b++) {
      if (!jr_d(l, go, s, j, kw, a, b, &fp[2u * b], d))
        return 0;
      jr_at(&at[a * (c + 1u) + 1u + b], r->g.w, d[0], j->jk, d[1]);
    }
  }
  return wd_take(l, go, r->pc, ew, c) && wd_take(l, go, r->pc, aw, 5u * c * (c + 1u)) &&
         jr_or(l, go, s, r, j, at, ew, aw, c, c + 1u, j->cf);
}

/* T4a (r3 s33). check_m2 (jointr.c:485-495) before its share. R->pc is the cell of y (M2: c1.x, c1.y, c2.x, c2.y).
   cipher_on_curve(y) (JR_REFUSED: jr_refuse of y.c1 and y.c2); y_statement (jointr.c:393-404: n = c, k = 2; atom
   (B, 0) = class_atom(com_k, B), atom (B, 1) = zero_atom(y - f[B]), FP of jr_f); then jr_or with ctx_y. Y[0] and
   Y[1] get the blocks of y.c1 and y.c2. R->pc ends after y_proof (the M2 d point is next: jr_share). */
static int jr_y(Low *l, Buf *go, const TrArgs *s, WdRun *r, const JrRun *j, const Operand *fp, Operand *y) {
  GroupPoint gs[2];
  Operand ng[2];
  JrAt at[2u * JR_C_MAX];
  const IrExpr *cp;
  const IrExpr *d[2];
  uint32_t yw[4];
  uint32_t ew[JR_C_MAX];
  uint32_t aw[10u * JR_C_MAX];
  uint32_t c = j->t.c;
  uint32_t cnt = 0;
  uint32_t b;
  if (c == 0u || c > JR_C_MAX)
    return refuse(l, "a jointr class count");
  if (!claimcom_generator(&gs[0], &cnt, &CLAIMCOM_TAG_G1) || !claimcom_generator(&gs[1], &cnt, &CLAIMCOM_TAG_G2) ||
      !wd_take(l, go, r->pc, yw, 4u) || !wd_point(l, go, &yw[0], &y[0]) || !wd_point(l, go, &yw[2], &y[1]) ||
      !jr_refuse(l, go, s, y[0].w) || !jr_refuse(l, go, s, y[1].w))
    return 0;
  for (b = 0; b < c; b++) {
    cp = jr_class(l, go, j, gs, s->pt[4].w, b);
    if (cp == NULL || !pt_neg(l, go, fp[2u * b].w, s->zero, &ng[0]) ||
        !pt_neg(l, go, fp[2u * b + 1u].w, s->zero, &ng[1]))
      return 0;
    d[0] = pt_op(l, go, IR_CALL_EC_ADD, y[0].w, ng[0].w);
    d[1] = pt_op(l, go, IR_CALL_EC_ADD, y[1].w, ng[1].w);
    if (d[0] == NULL || d[1] == NULL)
      return 0;
    jr_at(&at[2u * b], j->h.w, cp, NULL, NULL);
    jr_at(&at[2u * b + 1u], r->g.w, d[0], j->jk, d[1]);
  }
  return wd_take(l, go, r->pc, ew, c) && wd_take(l, go, r->pc, aw, 10u * c) &&
         jr_or(l, go, s, r, j, at, ew, aw, c, 2u, j->cy);
}

/* T4a (r3 s33). check_share (jointr.c:449-457) of one share. R->pc is the cell of d (2 words), then its dleq (A, B,
   z: 5 words, get_dleq). group_on_curve(d) (JR_REFUSED: jr_refuse); then sig_rand_verify(proof, pk = y.c1,
   ct = (mu KEY, d), m = 0, ctx_y) as one dleq of wd_sums (K = 1, the jr_gates form). YC1 is y.c1, KEY the block of
   key_h or key_k, MU the local of mu_h or mu_k. D gets the block of d. R->pc ends after the dleq. */
static int jr_share(Low *l, Buf *go, const TrArgs *s, WdRun *r, const JrRun *j, const IrExpr *yc1, const IrExpr *key,
                    uint32_t mu, Operand *d) {
  Operand ab[2];
  const IrExpr *ps[7];
  const IrExpr *c1;
  uint32_t dw[2];
  uint32_t sw[5];
  if (!wd_take(l, go, r->pc, dw, 2u) || !wd_point(l, go, dw, d) || !jr_refuse(l, go, s, d->w) ||
      !wd_take(l, go, r->pc, sw, 5u) || !wd_point(l, go, &sw[0], &ab[0]) || !wd_point(l, go, &sw[2], &ab[1]))
    return 0;
  c1 = pt_op(l, go, IR_CALL_EC_MUL, key, ex_local(l, mu));
  if (c1 == NULL)
    return 0;
  tr_dleq(ps, yc1, c1, d->w, ab, ex_local(l, sw[4]), ex_local(l, j->cy));
  return wd_sums(l, go, r, s->zero.w, ps, 1u);
}

/* T4a (r3 s33). outcome (jointr.c:498-505): p = y.c2 - d_h - d_k; the flag p == G (JR_OK, else JR_CLOSED). YC2 is
   y.c2, DH and DK the blocks of the 2 shares. NULL on a lowering error. */
static const IrExpr *jr_out(Low *l, Buf *go, const TrArgs *s, const WdRun *r, const IrExpr *yc2, const IrExpr *dh,
                            const IrExpr *dk) {
  Operand ng[2];
  const IrExpr *p;
  if (!pt_neg(l, go, dh, s->zero, &ng[0]) || !pt_neg(l, go, dk, s->zero, &ng[1]))
    return NULL;
  p = pt_op(l, go, IR_CALL_EC_ADD, yc2, ng[0].w);
  p = p == NULL ? NULL : pt_op(l, go, IR_CALL_EC_ADD, p, ng[1].w);
  return p == NULL ? NULL : pt_eq(l, p, r->g.w);
}

/* The m1, m2 and m3 decodes of tk_transfer_verify (trw_m1_get, trw_m2_get, trw_m3_get): BAD = 1 when a list is not
   its length (trw_m1_len, trw_m2_len, TRW_M3_LEN) or one of its points fails the point test (get_point: eval gives
   none on a bad decode, before any check). J holds the table (jr_table). */
static int jr_decode(Low *l, Buf *b, const TrArgs *s, const JrRun *j) {
  uint32_t c = j->t.c;
  const JrList q[3] = {{1u, c, j->nm}, {2u, c, j->nm}, {3u, c, j->nm}};
  const uint32_t len[3] = {533u * j->nm + 5u * c + 5u * c * (c + 1u), 11u * c + 11u, 7u};
  const uint32_t pts[3] = {200u * j->nm + 2u * c + 2u * c * (c + 1u), 4u * c + 5u, 3u};
  PfWalk w;
  uint32_t i;
  w.cur = local_new(l);
  w.len = local_new(l);
  w.idx = local_new(l);
  w.ks = local_new(l);
  w.sk = local_new(l);
  for (i = 0; i < 3u; i++) {
    if (!pf_walk(l, b, &w, s->tf, s->bad, s->ml[i].w, s->inf.w, len[i], pts[i], NULL, &q[i]))
      return 0;
  }
  return 1;
}

/* transferVerify (S8 C3, P5): tk_transfer_verify of eval.c. A trap when TF is 1; none when BAD is 1 after the
   decodes (tr_decode, jr_decode) or after the checks (jr_setup: mu is 0 or J is (0, 0)); else some (R->ok, the flag
   of the transfer half, check_m1, check_m2 and the 2 shares, times the outcome flag: a bad proof and JR_CLOSED give
   flagNo). The host order (jointr_verify, jointr.c:610-622): setup, check_m1, check_m2 with the M2 share, the M3
   share, outcome. R->pc is set to the start of M1, M2 and M3. */
static int transfer_verify(Low *l, Buf *b, const Value *v, Operand *out) {
  TrArgs s;
  WdRun r;
  JrRun j;
  uint32_t kw[4u * JR_C_MAX * JR_C_MAX];
  Operand fp[2u * JR_C_MAX];
  Operand y[2];
  Operand dh;
  Operand dk;
  Operand opt;
  Operand flag;
  const IrExpr *ok;
  Slot res = slot_new(l);
  Buf rest = buf_under(b);
  Buf none = buf_under(&rest);
  Buf run = buf_under(&rest);
  Buf late = buf_under(&run);
  Buf some = buf_under(&run);
  int table = jr_table(l, v->args[0], &j);
  if (!table)
    return l->m->diag->set ? 0 : block_ops(l, b, 0, NULL, 0, out);
  *out = slot_get(l, res);
  if (!tr_decode(l, b, v, &s) || !jr_decode(l, b, &s, &j) ||
      !block_ops(l, &none, 0, NULL, 0, &opt) || !store(l, &none, res, opt) || !block_ops(l, &late, 0, NULL, 0, &opt) ||
      !store(l, &late, res, opt) || !tr_half(l, &run, &s, &r) || !jr_setup(l, &run, &s, &r, &j) ||
      !set(l, &run, r.pc, s.ml[0].w) || !jr_gates(l, &run, &s, &r, &j, kw) || !jr_f(l, &run, &s, &r, &j, kw, fp) ||
      !set(l, &run, r.pc, s.ml[1].w) || !jr_y(l, &run, &s, &r, &j, fp, y) ||
      !jr_share(l, &run, &s, &r, &j, y[0].w, s.pt[1].w, j.mk, &dk) || !set(l, &run, r.pc, s.ml[2].w) ||
      !jr_share(l, &run, &s, &r, &j, y[0].w, s.pt[0].w, j.mh, &dh))
    return 0;
  ok = jr_out(l, &run, &s, &r, y[1].w, dh.w, dk.w);
  if (ok == NULL)
    return 0;
  flag = operand(ex_bin(l, IR_OP_MUL, ex_local(l, r.ok), ok), ex_const(l, 0));
  return block_ops(l, &some, 1, &flag, 1, &opt) && store(l, &some, res, opt) &&
         if_flag(l, &run, ex_local(l, s.bad), &late, &some) && if_flag(l, &rest, ex_local(l, s.bad), &none, &run) &&
         guard(l, b, ex_local(l, s.tf), res, &rest);
}

/* The operands of one attestVerify call (attest_verify). */
typedef struct {
  uint32_t tf;    /* the local: 1 when an operand or a field is a trap */
  uint32_t bad;   /* the local: 1 when the host gives none */
  uint32_t go;    /* the local: 1 when the proof has COUNT + 1 words */
  uint32_t count; /* the local: the number of ring keys */
  uint32_t d;     /* the local: the ring digest D (blsag_digest) */
  Operand ring;   /* the ring list */
  Operand key;    /* key (a point block) */
  Operand com;    /* com (a point block) */
  Operand sig;    /* the proof list */
} Attest;

/* Phase 1 of attest_verify (attest_verify and zk_ring of eval.c, in the host
   order): the operands of V into *S. BAD = 1 when the ring has 0 keys or more
   than RINGSIG_RING_MAX, when a ring key, key or com fails group_from_words,
   or when a ring key is (0, 0) and the proof has COUNT + 1 words. GO = 1 when
   the proof has COUNT + 1 words. Each walk is a REPEAT with a constant count
   (Q-S8-21) and each load is under TF = 0 and BAD = 0. */
static int attest_decode(Low *l, Buf *b, const Value *v, Attest *s) {
  static const U256 zero_word = {{0u, 0u, 0u, 0u}};
  Operand x[4];
  Operand ops[2];
  Operand zero;
  Operand inf;
  Operand p;
  const IrExpr *pts[2];
  const IrExpr *f = ex_const(l, 0);
  const IrExpr *c;
  const IrExpr *sc;
  const IrExpr *n;
  const IrExpr *r;
  const IrExpr *px;
  const IrExpr *py;
  const IrExpr *zx;
  const IrExpr *zy;
  uint32_t cur = local_new(l);
  uint32_t scur = local_new(l);
  uint32_t zkey = local_new(l);
  uint32_t len = local_new(l);
  uint32_t i;
  Buf turn = buf_under(b);
  Buf cell = buf_under(&turn);
  Buf cons = buf_under(&cell);
  Buf nil = buf_under(&cell);
  Buf over = buf_under(&cons);
  Buf take = buf_under(&cons);
  Buf chk = buf_under(&take);
  Buf use = buf_under(&chk);
  Buf first = buf_under(&use);
  Buf later = buf_under(&use);
  Buf s1 = buf_under(b);
  Buf s2 = buf_under(b);
  Buf kb = buf_under(&s2);
  Buf s3 = buf_under(b);
  Buf st = buf_under(&s3);
  Buf sl = buf_under(&st);
  Buf sy = buf_under(&sl);
  Buf sn = buf_under(&sl);
  Buf s4 = buf_under(b);
  s->tf = local_new(l);
  s->bad = local_new(l);
  s->go = local_new(l);
  s->count = local_new(l);
  s->d = local_new(l);
  c = ex_local(l, cur);
  sc = ex_local(l, scur);
  n = ex_local(l, s->count);
  for (i = 0; i < 4u; i++) {
    if (!lower(l, b, v->args[i], &x[i]))
      return 0;
    f = ex_or(l, f, x[i].f);
  }
  s->ring = x[0];
  s->key = x[1];
  s->com = x[2];
  s->sig = x[3];
  if (!set(l, b, s->tf, f) || !set(l, b, s->bad, ex_const(l, 0)) || !set(l, b, s->go, ex_const(l, 0)) ||
      !word_block(l, b, &zero_word, &zero))
    return 0;
  ops[0] = zero;
  ops[1] = zero;
  if (!block_ops(l, b, 0, ops, 2u, &inf) || !set(l, b, cur, x[0].w) || !set(l, b, s->count, ex_const(l, 0)) ||
      !set(l, b, zkey, ex_const(l, 0)) || !set(l, b, s->d, zero.w))
    return 0;
  /* The ring walk (zk_ring): a nil cell ends the walk, and a cons cell after
     RINGSIG_RING_MAX keys sets BAD. Each key gets the point test of
     issue_decode, the (0, 0) test of ring_ok (ZKEY, read after the length
     test) and one step of D: D := x on the first key, D := H(D, x) on a later
     key, then D := H(D, y). */
  if (!field_at(l, &take, c, 0, &p) || !set(l, &take, s->tf, ex_or(l, p.f, ex_load(l, c, 4u))))
    return 0;
  r = pt_op(l, &chk, IR_CALL_EC_ADD, p.w, inf.w);
  if (r == NULL || !or_into(l, &chk, s->bad, ex_bin(l, IR_OP_EQ, r, ex_const(l, 0))))
    return 0;
  px = ex_load(l, p.w, 1u);
  py = ex_load(l, p.w, 3u);
  zx = call_of(l, IR_CALL_U256_EQ, 2u, px, zero.w, NULL);
  zy = call_of(l, IR_CALL_U256_EQ, 2u, py, zero.w, NULL);
  if (zx == NULL || zy == NULL || !or_into(l, &use, zkey, ex_bin(l, IR_OP_MUL, zx, zy)) ||
      !set(l, &first, s->d, px) ||
      !set(l, &later, s->d, call_of(l, IR_CALL_SHA256, 2u, ex_local(l, s->d), px, NULL)) ||
      !if_flag(l, &use, ex_bin(l, IR_OP_EQ, n, ex_const(l, 0)), &first, &later) ||
      !set(l, &use, s->d, call_of(l, IR_CALL_SHA256, 2u, ex_local(l, s->d), py, NULL)) ||
      !set(l, &use, s->count, ex_bin(l, IR_OP_ADD, n, ex_const(l, 1))) || !set(l, &use, cur, ex_load(l, c, 3u)))
    return 0;
  if (!when_live(l, &chk, s->tf, s->bad, &use) || !when_live(l, &take, s->tf, s->bad, &chk) ||
      !set(l, &over, s->bad, ex_const(l, 1)) ||
      !if_flag(l, &cons, ex_bin(l, IR_OP_EQ, n, ex_const(l, RINGSIG_RING_MAX)), &over, &take) ||
      !if_flag(l, &cell, ex_bin(l, IR_OP_EQ, ex_load(l, c, 0), ex_const(l, 1)), &cons, &nil) ||
      !when_live(l, &turn, s->tf, s->bad, &cell) || !st_repeat(l, b, ex_const(l, RINGSIG_RING_MAX + 1u), &turn))
    return 0;
  if (!set(l, &s1, s->bad, ex_bin(l, IR_OP_EQ, n, ex_const(l, 0))) || !when_live(l, b, s->tf, s->bad, &s1))
    return 0;
  /* key and com: the point test. (0, 0) is a valid point here. */
  pts[0] = x[1].w;
  pts[1] = x[2].w;
  r = pt_op(l, &kb, IR_CALL_EC_ADD, ex_load(l, c, 1u), inf.w);
  if (r == NULL || !or_into(l, &kb, s->bad, ex_bin(l, IR_OP_EQ, r, ex_const(l, 0))) ||
      !set(l, &kb, cur, ex_load(l, c, 3u)) || !set(l, &s2, cur, word_list(l, &s2, pts, 2u)) ||
      !st_repeat(l, &s2, ex_const(l, 2u), &kb) || !when_live(l, b, s->tf, s->bad, &s2))
    return 0;
  /* The proof length: a count of the cons cells, RINGSIG_RING_MAX + 2 at
     most. A longer list gives a length that is not COUNT + 1. */
  if (!set(l, &sy, s->tf, ex_or(l, ex_load(l, sc, 2u), ex_load(l, sc, 4u))) ||
      !set(l, &sy, len, ex_bin(l, IR_OP_ADD, ex_local(l, len), ex_const(l, 1))) ||
      !set(l, &sy, scur, ex_load(l, sc, 3u)) ||
      !if_flag(l, &sl, ex_bin(l, IR_OP_EQ, ex_load(l, sc, 0), ex_const(l, 1)), &sy, &sn) ||
      !when_live(l, &st, s->tf, s->bad, &sl) || !set(l, &s3, scur, x[3].w) || !set(l, &s3, len, ex_const(l, 0)) ||
      !st_repeat(l, &s3, ex_const(l, RINGSIG_RING_MAX + 2u), &st) || !when_live(l, b, s->tf, s->bad, &s3))
    return 0;
  /* The length test is before the (0, 0) test of the ring keys (eval.c): a
     ring key (0, 0) gives none only when the length is COUNT + 1. */
  return set(l, &s4, s->go, ex_bin(l, IR_OP_EQ, ex_local(l, len), ex_bin(l, IR_OP_ADD, n, ex_const(l, 1)))) &&
         set(l, &s4, s->bad, ex_bin(l, IR_OP_MUL, ex_local(l, zkey), ex_local(l, s->go))) &&
         when_live(l, b, s->tf, s->bad, &s4);
}

/* Phase 2 of attest_verify (ringsig_verify of ringsig.c): *OUT is 1 when the
   proof has COUNT + 1 words, each s word is below the group order and the
   chain closes. One turn for each ring key: L = s G + c P, then
   c := H(H(T, L.x), L.y) with T = H(H(RINGSIG_TAG, D), m) (Q-S8-22). c and s
   are not reduced. A turn after an s word of the group order or more does no
   link: the result is 0 then. The keys are valid and not (0, 0), so no EC
   result is 0; L can be (0, 0). */
static int attest_check(Low *l, Buf *b, const Attest *s, const IrExpr **out) {
  GroupPoint gen;
  U256 gx;
  U256 gy;
  Operand ops[2];
  Operand g;
  Operand tag;
  Operand order;
  const IrExpr *items[3];
  const IrExpr *sw;
  const IrExpr *pk;
  const IrExpr *sg;
  const IrExpr *cp;
  const IrExpr *lp;
  const IrExpr *h;
  const IrExpr *eq;
  uint32_t msg = local_new(l);
  uint32_t t = local_new(l);
  uint32_t c = local_new(l);
  uint32_t rej = local_new(l);
  uint32_t i = local_new(l);
  uint32_t cur = local_new(l);
  uint32_t scur = local_new(l);
  uint32_t ok = local_new(l);
  Buf chain = buf_under(b);
  Buf skip = buf_under(b);
  Buf body = buf_under(&chain);
  Buf turn = buf_under(&body);
  Buf rest = buf_under(&body);
  Buf lk = buf_under(&turn);
  Buf stop = buf_under(&turn);
  group_gen(&gen);
  group_to_words(&gx, &gy, &gen);
  if (!word_block(l, &chain, &gx, &ops[0]) || !word_block(l, &chain, &gy, &ops[1]) ||
      !block_ops(l, &chain, 0, ops, 2u, &g) || !word_block(l, &chain, &RINGSIG_TAG, &tag) ||
      !word_block(l, &chain, &LOW_SECP_N, &order))
    return 0;
  /* m (ringsig_message): the chain from key.x over key.y, com.x and com.y.
     Then T, the part of the challenge that is the same on each turn. */
  items[0] = ex_load(l, s->key.w, 3u);
  items[1] = ex_load(l, s->com.w, 1u);
  items[2] = ex_load(l, s->com.w, 3u);
  if (!set(l, &chain, msg, ex_load(l, s->key.w, 1u)) || !hash_fold(l, &chain, msg, items, 3u))
    return 0;
  items[0] = ex_local(l, s->d);
  items[1] = ex_local(l, msg);
  if (!set(l, &chain, t, tag.w) || !hash_fold(l, &chain, t, items, 2u))
    return 0;
  /* One turn (link of ringsig.c), with the s test of ringsig_verify. */
  sw = ex_load(l, ex_local(l, scur), 1u);
  pk = ex_load(l, ex_local(l, cur), 1u);
  sw = sw == NULL ? NULL : atom(l, &turn, sw);
  pk = pk == NULL ? NULL : atom(l, &turn, pk);
  if (sw == NULL || pk == NULL || !or_into(l, &turn, rej, call_of(l, IR_CALL_U256_LE, 2u, order.w, sw, NULL)))
    return 0;
  sg = pt_op(l, &lk, IR_CALL_EC_MUL, g.w, sw);
  cp = pt_op(l, &lk, IR_CALL_EC_MUL, pk, ex_local(l, c));
  lp = sg == NULL || cp == NULL ? NULL : pt_op(l, &lk, IR_CALL_EC_ADD, sg, cp);
  h = lp == NULL ? NULL : call_of(l, IR_CALL_SHA256, 2u, ex_local(l, t), ex_load(l, lp, 1u), NULL);
  h = h == NULL ? NULL : atom(l, &lk, h);
  if (h == NULL || !set(l, &lk, c, call_of(l, IR_CALL_SHA256, 2u, h, ex_load(l, lp, 3u), NULL)) ||
      !if_flag(l, &turn, ex_local(l, rej), &stop, &lk) ||
      !set(l, &turn, scur, ex_load(l, ex_local(l, scur), 3u)) ||
      !set(l, &turn, cur, ex_load(l, ex_local(l, cur), 3u)) ||
      !set(l, &turn, i, ex_bin(l, IR_OP_ADD, ex_local(l, i), ex_const(l, 1))) ||
      !if_flag(l, &body, ex_bin(l, IR_OP_LT, ex_local(l, i), ex_local(l, s->count)), &turn, &rest))
    return 0;
  /* c starts at sig[0], and the s words start at the second cell. */
  eq = call_of(l, IR_CALL_U256_EQ, 2u, ex_local(l, c), ex_load(l, s->sig.w, 1u), NULL);
  *out = ex_local(l, ok);
  return eq != NULL && set(l, b, ok, ex_const(l, 0)) && set(l, &chain, c, ex_load(l, s->sig.w, 1u)) &&
         set(l, &chain, scur, ex_load(l, s->sig.w, 3u)) && set(l, &chain, cur, s->ring.w) &&
         set(l, &chain, i, ex_const(l, 0)) && set(l, &chain, rej, ex_const(l, 0)) &&
         st_repeat(l, &chain, ex_const(l, RINGSIG_RING_MAX), &body) &&
         set(l, &chain, ok, ex_bin(l, IR_OP_MUL, ex_bin(l, IR_OP_EQ, ex_local(l, rej), ex_const(l, 0)), eq)) &&
         if_flag(l, b, ex_local(l, s->go), &chain, &skip);
}

/* attestVerify (S8 C3): attest_verify of eval.c. A trap when TF is 1, none
   when BAD is 1, else some (the flag of the chain). */
static int attest_verify(Low *l, Buf *b, const Value *v, Operand *out) {
  Attest s;
  Operand opt;
  Operand flag;
  const IrExpr *ok;
  Slot res = slot_new(l);
  Buf rest = buf_under(b);
  Buf none = buf_under(&rest);
  Buf run = buf_under(&rest);
  *out = slot_get(l, res);
  if (!attest_decode(l, b, v, &s) || !block_ops(l, &none, 0, NULL, 0, &opt) || !store(l, &none, res, opt) ||
      !attest_check(l, &run, &s, &ok))
    return 0;
  flag = operand(ok, ex_const(l, 0));
  return block_ops(l, &run, 1, &flag, 1, &opt) && store(l, &run, res, opt) &&
         if_flag(l, &rest, ex_local(l, s.bad), &none, &run) && guard(l, b, ex_local(l, s.tf), res, &rest);
}

/* An eliminator on a variable. */
static int lower_stuck(Low *l, Buf *b, const Value *v, Operand *out) {
  switch (v->op) {
  case OP_NAT:
  case OP_FLAG:
  case OP_UNIT:
  case OP_PROD:
  case OP_SUM:
  case OP_OPTION:
  case OP_LIST:
  case OP_FAMILY:
  case OP_UNIT_VAL:
  case OP_FLAG_YES:
  case OP_FLAG_NO:
  case OP_PAIR:
  case OP_INL:
  case OP_INR:
  case OP_NONE:
  case OP_SOME:
  case OP_NIL:
  case OP_CONS:
  case OP_PACK:
  case OP_CTOR:
  case OP_PURE:
  case OP_U256:
  case OP_ADDR:
  case OP_INTERVAL:
  case OP_SYSTEM:
  case OP_FIN:
  case OP_NONZERO:
    return internal(l, "a stuck canonical value");
  case OP_FIRST:
  case OP_WITNESS:
    return field_of(l, b, v->args[0], 0, out);
  case OP_SECOND:
  case OP_PAYLOAD:
    return field_of(l, b, v->args[0], 1, out);
  case OP_PROJ:
    return field_of(l, b, v->args[0], v->field, out);
  case OP_EITHER:
    return two_cases(l, b, v, 1, out);
  case OP_OPTION_ELIM:
    return two_cases(l, b, v, 0, out);
  case OP_MAP:
    return v->inst == CARRIER_LIST ? list_map(l, b, v, out) : carrier_map(l, b, v, out);
  case OP_BIND:
    return v->inst == CARRIER_LIST ? list_bind(l, b, v, out) : carrier_bind(l, b, v, out);
  case OP_FILTER:
    return v->inst == CARRIER_OPTION ? carrier_filter(l, b, v, out) : not_option(l, b, v, out);
  case OP_FOLD_NAT:
    return fold_nat(l, b, v, out);
  case OP_UPTO_NAT:
    return upto_nat(l, b, v, out);
  case OP_UPTO_LIST:
    return upto_list(l, b, v, out);
  case OP_FOLD_LIST:
    return fold_list(l, b, v, out);
  case OP_UNFOLD:
    return unfold(l, b, v, out);
  case OP_FOLD_FAMILY:
    return family_fold(l, b, v, out);
  case OP_ELIM:
    return elim_of(l, b, v, out);
  case OP_NAT_ADD:
    return nat_opt(l, b, v, 1, out);
  case OP_NAT_SUB:
    return nat_opt(l, b, v, 0, out);
  case OP_NAT_EQ:
    return nat_bin(l, b, v, IR_OP_EQ, out);
  case OP_NAT_LE:
    return nat_bin(l, b, v, IR_OP_LE, out);
  case OP_FLAG_IF:
    return flag_if(l, b, v, out);
  case OP_NAT_DIV:
    return nat_divmod(l, b, v, IR_OP_DIV, out);
  case OP_NAT_MOD:
    return nat_divmod(l, b, v, IR_OP_MOD, out);
  case OP_U256_ADD:
    return call_opt(l, b, v, IR_CALL_U256_ADD, 2, 0, out);
  case OP_U256_SUB:
    return call_opt(l, b, v, IR_CALL_U256_SUB, 2, 1, out);
  case OP_U256_MUL:
    return call_opt(l, b, v, IR_CALL_U256_MUL, 2, 0, out);
  case OP_MUL_DIV:
    return call_opt(l, b, v, IR_CALL_MUL_DIV, 3, 0, out);
  case OP_ELG_LOG:
    return refuse(l, "an elgLog (N7, until S8)");
  case OP_U256_LE:
    return call_op(l, b, v, IR_CALL_U256_LE, 2, out);
  case OP_U256_EQ:
  case OP_ADDR_EQ:
    return call_op(l, b, v, IR_CALL_U256_EQ, 2, out);
  case OP_TO_U256:
    return call_op(l, b, v, IR_CALL_TO_U256, 1, out);
  case OP_HASH:
    return call_op(l, b, v, IR_CALL_SHA256, 2, out);
  case OP_ADDR_TO_U256:
    return lower(l, b, v->args[0], out);
  case OP_NONZERO_OF:
    return nonzero_opt(l, b, v, out);
  case OP_NONZERO_VALUE:
    return lower(l, b, v->args[v->argc - 1u], out);
  case OP_FIN_ADD:
    return fin_op(l, b, v, 1, out);
  case OP_FIN_NEG:
    return fin_op(l, b, v, 0, out);
  case OP_FIN_CASE:
    return fin_switch(l, b, v, out);
  case OP_NAT_IS_SET:
  case OP_U256_IS_SET:
  case OP_FLAG_IS_SET:
  case OP_OPT_NAT_IS_SET:
    return proof_op(l, b, v, 0, v->argc, out);
  case OP_EC_ADD:
    return call_opt(l, b, v, IR_CALL_EC_ADD, 2, 0, out);
  case OP_EC_MUL:
    return call_opt(l, b, v, IR_CALL_EC_MUL, 2, 0, out);
  case OP_SCHNORR_OK:
    return call_op(l, b, v, IR_CALL_SCHNORR_OK, 5, out);
  case OP_TRANSP:
    return refuse(l, "a transp that does not reduce at compile time");
  case OP_HCOMP:
    return refuse(l, "an hcomp that does not reduce at compile time");
  case OP_GLUE:
    return refuse(l, "a Glue type that does not reduce at compile time");
  case OP_GLUE_ELEM:
    return refuse(l, "a glue that does not reduce at compile time");
  case OP_UNGLUE:
    return refuse(l, "an unglue that does not reduce at compile time");
  case OP_KMAP:
  case OP_KMAP_OF:
  case OP_KMAP_GET:
  case OP_KMAP_SET:
    return refuse(l, "a run-time map (KMap)");
  case OP_ZK_KEY:
  case OP_ZK_SIGN:
  case OP_ZK_WITNESS:
  case OP_ZK_CAST:
  case OP_ZK_PROVE:
  case OP_CLAIM_COM:
  case OP_ATTEST_SIGN:
  case OP_JR_SEND:
  case OP_JR_SELECT:
  case OP_JR_OPEN:
  case OP_TRANSFER_PROVE:
  case OP_ISSUE_PROVE:
  case OP_WITHDRAW_PROVE:
    return secret_op(l);
  case OP_ZK_HP:
  case OP_ZK_DIGEST:
  case OP_ZK_VERIFY:
  case OP_ZK_BALLOT:
  case OP_ZK_PROJECT:
  case OP_ZK_CHECK:
    return refuse(l, "a host proof check (zk)");
  case OP_TRANSFER_VERIFY:
    /* P5 (B8): the WGSL build refuses it (HOST-LIMIT); the Wasm and JS builds lower it. */
    if (l->m->lower_wgsl)
      return diag_fail(l->m->diag, "REFUSE_LOWER", l->m->def, "transferVerify does not lower to WGSL (HOST-LIMIT)");
    return transfer_verify(l, b, v, out);
  case OP_WITHDRAW_VERIFY:
    /* Q-S8-27: the WGSL build refuses it (HOST-LIMIT); the Wasm and JS builds lower it. */
    if (l->m->lower_wgsl)
      return diag_fail(l->m->diag, "REFUSE_LOWER", l->m->def, "withdrawVerify does not lower to WGSL (HOST-LIMIT)");
    return withdraw_verify(l, b, v, out);
  case OP_ATTEST_VERIFY:
    return attest_verify(l, b, v, out);
  case OP_ISSUE_VERIFY:
    return issue_verify(l, b, v, out);
  case OP_RTABLE: /* S4 C6: the checker gives the static list, so no core has it */
    return internal(l, "an rTable after the check");
  }
  return internal(l, "an unknown operation");
}

static int lower_value(Low *l, Buf *b, const Value *v, Operand *out) {
  Operand p;
  Operand e;
  const Value *end;
  switch (v->kind) {
  case VAL_NAT:
    return give(l, out, v->nat, 0);
  case VAL_WORD:
    return word_block(l, b, v->word, out);
  /* A path, a path type and an interval term erase (Q-S8-1). A path
     application keeps the flag of the path. At a kept end (Q-S8-13) the
     word is the end. */
  case VAL_PLAM:
  case VAL_PATHP:
  case VAL_IVAL:
    return give(l, out, 0, 0);
  case VAL_PAPP:
    end = v->argc != 2u ? NULL : ival_is_end(v->arg, 0) ? v->args[0] : ival_is_end(v->arg, 1) ? v->args[1] : NULL;
    if (end != NULL)
      return lower(l, b, v->dom, &p) && lower(l, b, end, &e) && atoms(l, b, e.w, ex_or(l, p.f, e.f), out);
    return lower(l, b, v->dom, &p) && atoms(l, b, ex_const(l, 0), p.f, out);
  case VAL_UNIV:
  case VAL_PI:
  case VAL_SIGMA:
    return give(l, out, 0, 0);
  case VAL_LAM:
    return closure(l);
  /* A fold into B -> B applied to B0 may be a loop (Q-S8-11). */
  case VAL_APP:
    return chain_fold(l, b, v, out);
  case VAL_VAR:
    if (v->nat >= l->level_count)
      return internal(l, "a variable without an operand");
    *out = l->levels[v->nat];
    return 1;
  case VAL_OP:
    return lower_canon(l, b, v, out);
  case VAL_STUCK:
    return lower_stuck(l, b, v, out);
  }
  return internal(l, "an unknown value");
}

static int lower(Low *l, Buf *b, const Value *v, Operand *out) {
  int ok;
  if (v == NULL)
    return got(l, v);
  if (memo_kind(v) && memo_find(b, v, out))
    return 1;
  if (l->m->depth >= EVAL_DEPTH_LIMIT)
    return diag_fail(l->m->diag, "EVAL_DEPTH", l->m->def, "the lowering nests deeper than %u levels", EVAL_DEPTH_LIMIT);
  l->m->depth++;
  ok = lower_value(l, b, v, out);
  l->m->depth--;
  return ok && (!memo_kind(v) || memo_put(l, b, v, *out));
}

/* An entry with an Option result (Q-S3-22, Q-S3-23): the status word :=
   the tag of the block R, and R becomes the payload when the tag is 1, else
   0. The payload flag still runs TRAP (a backstop: R0 has no source). */
static int entry_option(Low *l, Buf *b, Operand *r) {
  const IrExpr *addr = r->w;
  const IrExpr *tag = atom(l, b, ex_load(l, addr, 0));
  Slot res = slot_new(l);
  Buf some = buf_under(b);
  Buf none = buf_under(b);
  Buf trap = buf_under(b);
  Buf pass = buf_under(b);
  *r = slot_get(l, res);
  return tag != NULL && emit(l, b, stmt_expr(l, IR_STMT_STATUS, tag)) && store(l, &some, res, field_op(l, addr, 0)) &&
         store(l, &none, res, operand(ex_const(l, 0), ex_const(l, 0))) && if_flag(l, b, tag, &some, &none) &&
         emit(l, &trap, stmt_new(l, IR_STMT_LIMIT)) && if_flag(l, b, r->f, &trap, &pass);
}

/* The entry INDEX, applied to a variable for each parameter. Only the
   result is strict: its flag runs TRAP, and an Option result sets the
   status word (entry_option). */
static int lower_entry(Machine *m, uint32_t index, const Entry *e, IrFunc *fn) {
  Low l;
  Buf body = buf_under(NULL);
  Buf trap = buf_under(NULL);
  Buf none = buf_under(NULL);
  IrScalar *params = arena_alloc(m->arena, ((size_t)e->param_count + 1u) * sizeof *params);
  unsigned char *seen = arena_alloc(m->arena, (size_t)m->def_count + 1u);
  const Value *v;
  const Value *x;
  Operand r;
  uint32_t k;
  memset(&l, 0, sizeof l);
  l.m = m;
  l.local_count = e->param_count;
  m->def = m->defs[index].name;
  m->fuel = EVAL_FUEL_STEPS;
  if (params == NULL || seen == NULL)
    return oom(&l);
  memset(seen, 0, (size_t)m->def_count + 1u);
  seen[index] = 1;
  if (secret_reach(m, seen, m->defs[index].body))
    return secret_op(&l);
  v = def_value(m, index);
  for (k = 0; k < e->param_count && v != NULL; k++) {
    params[k] = e->param_flag[k] ? IR_SCALAR_FLAG : e->param_u256[k] ? IR_SCALAR_U256 : IR_SCALAR_NAT;
    x = fresh(&l, operand(ex_local(&l, k), ex_const(&l, 0)));
    v = x == NULL ? NULL : apply_value(m, v, x);
  }
  if (!got(&l, v) || !lower(&l, &body, v, &r) || !emit(&l, &trap, stmt_new(&l, IR_STMT_LIMIT)) ||
      !if_flag(&l, &body, r.f, &trap, &none) || (e->result_option && !entry_option(&l, &body, &r)) ||
      !emit(&l, &body, stmt_expr(&l, IR_STMT_RETURN, r.w)))
    return 0;
  fn->name = m->defs[index].name;
  fn->params = params;
  fn->param_count = e->param_count;
  fn->result = e->result_flag ? IR_SCALAR_FLAG : e->result_u256 ? IR_SCALAR_U256 : IR_SCALAR_NAT;
  fn->status = e->result_option ? 1 : 0;
  fn->local_count = l.local_count;
  fn->body = block_of(&body);
  return 1;
}

/* Q-S8-11: FN gets the heap words of its bound, and src/wasm.c sizes memory 0
   from them. The heap starts at byte 40 (src/wasm.c WASM_HEAP_START) and a
   Wasm32 memory has at most 65536 pages of 64 KiB, so a larger bound does not
   lower (N4). */
static int entry_heap(Machine *m, IrFunc *fn) {
  IrBound b;
  if (!ir_bound(m, fn, &b))
    return 0;
  fn->heap_words = b.memory;
  return b.memory <= (UINT64_C(4294967296) - 40u) / 8u ||
         diag_fail(m->diag, "REFUSE_LOWER", m->def, "a heap of %llu words does not lower (N4)",
                   (unsigned long long)b.memory);
}

int lower_program(Machine *m, IrProgram *prog, FILE *err, int wgsl) {
  IrFunc *funcs = arena_alloc(m->arena, ((size_t)m->def_count + 1u) * sizeof *funcs);
  Entry e;
  size_t n = 0;
  uint32_t i;
  int entry;
  prog->funcs = funcs;
  prog->func_count = 0;
  if (funcs == NULL)
    return diag_fail(m->diag, "OOM", m->def, "out of memory");
  m->keep_ends = 1;
  m->lower_wgsl = wgsl;
  for (i = 0; i < m->def_count; i++) {
    m->fuel = EVAL_FUEL_STEPS;
    entry = m->defs[i].origin == ORIGIN_PROGRAM && entry_of(m, m->defs[i].type, &e);
    if (err != NULL && !entry && m->defs[i].origin == ORIGIN_PROGRAM && family_result_of(m, m->defs[i].type))
      fprintf(err, "langc: note: %s: a family result has no target form (O24)\n", m->defs[i].name);
    if (entry && !(lower_entry(m, i, &e, &funcs[n]) && entry_heap(m, &funcs[n])))
      return 0;
    n += entry ? 1u : 0u;
  }
  m->keep_ends = 0;
  m->lower_wgsl = 0;
  prog->func_count = n;
  return 1;
}
