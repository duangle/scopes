
# types hooking functions to switch their implementations

fn test ()
    fn join (a b)
        string-join a b

    dump-frame (Closure-frame join)
    let a = join
    let b = join
    # ensure function has a fixed scope label
    assert (a == b)

test;

#set-type-symbol! T 'indirect-call
    fn (f args...)
