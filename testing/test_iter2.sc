
fn range (a b c)
    let num-type = (typeof a)
    let step = 
        if (c == none)
            num-type 1
        else c
    let from =
        if (b == none)
            num-type 0
        else a
    let to =
        if (b == none) a
        else b
    fn ()
        return
            label (f fdone x)
                if (x < to)
                    f (x + step) x
                else
                    fdone;
            unconst from

fn do_loop (n)
    let iter start = ((range 0 n 1))
    do
        label done ()
        iter
            label loop (next x...)
                print x...
                iter loop done next
            done
            start
    print "done"

fn main ()
    do_loop 10

main;


