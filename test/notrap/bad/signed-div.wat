;; A signed div: INT64_MIN / -1 traps.
(module
  (memory 1 1)
  (func (param i64) (result i64)
    local.get 0
    local.get 0
    i64.div_s))
