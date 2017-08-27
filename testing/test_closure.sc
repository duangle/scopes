
do
    fn f (x)
        fn ff (y)
            add x y

    fn q ()
        (f 2) 3

    assert ((q) == 5)

do
    fn f (x)
        let x = (unconst x)
        fn ff (y)
            let y = (unconst y)
            add x y

    fn q ()
        (f 2) 3

    assert ((q) == 5)

fn test2 ()
    global quit = false
    var event = 1
    fn handle_events ()
        if (event != 0)
            if (event == 1)
                quit = true
        else
            handle_events;
    fn mainloop ()
        if (not quit)
            handle_events;
            mainloop;
    mainloop;

fn test3 ()
    fn handle_events ()
        if (unconst true)
            io-write! "\n"
        else
            handle_events;
    handle_events;

#dump-label
    Closure-label test3

dump-label
    typify test3
#compile
    typify test3

test2;
