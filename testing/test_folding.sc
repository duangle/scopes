
fn dostuff (x)
    let y =
        (unconst true) or (unconst false)
    #let y =
        if (unconst true)
            unconst 10
        else
            unconst 20
    let z = (add y y)
    z

dump-label (Closure-label dostuff)
print;
dump-label
    typify dostuff i32
