

fn do_loop (n)
    #for x y in (zip (range n) (range 0 100 2))
        print x y
    for x in (range n)
        if ((x % 2) == 1)
            continue;
        print x
    print "done"

fn main ()
    do_loop 10

main;


