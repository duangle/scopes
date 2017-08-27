
fn assert-depth (n f)
    let typed-f =
        typify f
    let c = (Label-countof-reachable typed-f)
    if (c > n)
        dump-label typed-f
        syntax-error! (Label-anchor typed-f)
            .. "label too complex: " (repr c) " != " (repr n)

assert-depth 2:usize
    fn ()
        print "hello world"

