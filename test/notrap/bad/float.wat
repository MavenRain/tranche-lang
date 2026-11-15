;; A float op: i64.trunc_f64_u traps on NaN.
(module
  (memory 1 1)
  (func (param i64) (result i64)
    f64.const 0
    i64.trunc_f64_u))
