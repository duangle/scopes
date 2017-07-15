
fn range (a b c)
    let num-type = (typeof a)
    let step = 
        if (== c none)
            num-type 1
        else c
    let from =
        if (== b none)
            num-type 0
        else a
    let to =
        if (== b none) a
        else b
    return
        fn (x)
            if (< x to)
                return (+ x step) x
        unconst from

fn do_loop (n)
    let iter start = (range 0 n 1)
    let [loop] next x = (iter start)
    if (!= next none)
        print x
        loop (iter next)

fn main ()
    do_loop 10

compile
    typify table-test i32
    'dump-module


