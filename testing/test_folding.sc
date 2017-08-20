
fn print_stuff (x)
    print "line"
    fn ()
        print x

fn main ()
    let link = (print_stuff (unconst "hello"))
    link;

#dump-label
    typify main

main;

# this case is illegal
#do
    define ascope (Scope)
    syntax-extend
        fn print_stuff2 (x)
            print "line"
            set-scope-symbol! ascope 'somefunc
                fn ()
                    print x

        fn main2 ()
            print_stuff2 (unconst "hello")
        main2;
        syntax-scope

    ascope.somefunc;

true