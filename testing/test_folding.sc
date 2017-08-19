
fn dostuff2 (x)
    let y = (mul x x)
    let z = y
    let w = z
    w

fn dostuff (x)
    let y = x
    let z = (add y y)
    let w = z
    dostuff2 w

fn dostuff3 (x)
    let x = (add x x)
    x

dump-label (Closure-label dostuff3)
print;
dump-label
    typify dostuff3 i32
