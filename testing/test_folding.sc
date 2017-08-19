
fn dostuff2 (x)
    let y = (mul x x)
    let z = y
    let w = z
    w

fn dostuff (x)
    let y = x
    let z = (add y y)
    let w = (dostuff2 z)
    call
        fn domorestuff (n)
            mul w w
        5

fn dostuff3 (x)
    let x = (add x x)
    x

#dump-label (Closure-label dostuff3)
print;
dump-label
    typify dostuff i32
