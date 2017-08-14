
fn print-vector (v)
    print
        extractelement v 0
        extractelement v 1
        extractelement v 2
        extractelement v 3

fn do-ops (v w)
    v + w * w

#compile
    typify
        fn vectorof-f32 (...)
            vectorof f32 ...
        \ f32 f32 f32 f32
    'dump-disassembly

fn test-vector-ops (v w)
    let v = (vectorof f32 1.0 2.0 3.0 4.0)
    let w = (vectorof f32 0.1 0.2 0.3 0.4)
    print-vector (do-ops v w)
    print-vector (fcmp>o v w)
    print-vector
        shufflevector v w (vectorof i32 7 5 3 1)

let VT = (vector f32 4:usize)

# working with constants
test-vector-ops (nullof VT) (nullof VT)
# working with variables
test-vector-ops (unconst (nullof VT)) (unconst (nullof VT))

#compile
    typify do-ops VT VT
    'dump-disassembly
    'dump-function