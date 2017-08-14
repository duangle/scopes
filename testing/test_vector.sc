
fn do-ops (v w)
    v + w * w

#compile
    typify
        fn vectorof-f32 (...)
            vectorof f32 ...
        \ f32 f32 f32 f32
    'dump-disassembly

fn test-vector-ops (v w)
    let v = (vectorof i32 10 20 (unpack (vectorof i32 30 40)))
    let w = (vectorof i32 1 2 3 4)
    assert 
        all? 
            (do-ops v w) == (vectorof i32 11 24 39 56)
    assert
        all? 
            (shufflevector v w (vectorof i32 7 5 3 1)) == (vectorof i32 4 2 40 20)
    print v w

let VT = (vector i32 4:usize)

# working with constants
test-vector-ops (nullof VT) (nullof VT)
# working with variables
test-vector-ops (unconst (nullof VT)) (unconst (nullof VT))

#compile
    typify do-ops VT VT
    'dump-disassembly
    'dump-function