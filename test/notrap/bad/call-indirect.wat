;; call_indirect: a null entry or a type mismatch traps.
(module
  (type $t (func (param i64) (result i64)))
  (table 1 funcref)
  (memory 1 1)
  (func (type $t) (param i64) (result i64)
    local.get 0
    i32.const 0
    call_indirect (type $t)))
