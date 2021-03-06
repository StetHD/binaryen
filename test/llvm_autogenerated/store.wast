(module
  (memory 1
    (segment 4 "\10\04\00\00")
  )
  (export "memory" memory)
  (export "sti32" $sti32)
  (export "sti64" $sti64)
  (export "stf32" $stf32)
  (export "stf64" $stf64)
  (func $sti32 (param $0 i32) (param $1 i32)
    (i32.store
      (get_local $0)
      (get_local $1)
    )
    (return)
  )
  (func $sti64 (param $0 i32) (param $1 i64)
    (i64.store
      (get_local $0)
      (get_local $1)
    )
    (return)
  )
  (func $stf32 (param $0 i32) (param $1 f32)
    (f32.store
      (get_local $0)
      (get_local $1)
    )
    (return)
  )
  (func $stf64 (param $0 i32) (param $1 f64)
    (f64.store
      (get_local $0)
      (get_local $1)
    )
    (return)
  )
)
;; METADATA: { "asmConsts": {},"staticBump": 1040, "initializers": [] }
