# boot script
# the bangra executable looks for a boot file at
# path/to/executable.b and, if found, executes it.
bangra

letrec
    with
        f
            function (return)
                return 2
                3

    print # displays 3
        f
            function (x) x

    print # displays 2
        call/cc f

print
    letrec
        with
            func-a
                function (x)
                    print "func-a" x
                    func-b (+ x 1)
            func-b
                function (x)
                    print "func-b" x
                    branch (< x 1000)
                        function ()
                            func-a (* x 2)
                        function ()
                            print "done."
                            x
        print "func-x!" func-a func-b
        func-a 1

letrec
    with
        sin
            external "sin"
                cdecl double (tupleof double) false
        x
            10
        plus2
            function (x)
                + x 2
        api
            import-c "bangra.h" (tupleof)
        puts
            external "puts"
                cdecl int (tupleof rawstring) false
        testtext
            "yo yo yo"
    print
        sin 0.5

    print "global:"
        @ globals "float"

    print
        0.1
        sin 0
        sin 0.1
        sin 0.5
        sin 1.2
    print "done"

    print
        "quoting:"
        quote
            function (x)
                + x 2

    print "sin:"
        (locals)
        sin
        @ (locals) "sin"
    print
        plus2 50

    print
        puts testtext
        sin 5

    print "eval:"
        apply
            eval
                quote
                    + 0.5 0.5
                structof
                    tupleof "#parent" globals

    print
        structof
            tupleof "k" 2
            tupleof "y" 3
    print
        typeof "test"

    print
        branch (< x 5)
            function ()
                print "yes"
            function ()
                print "no"

    print "hello world"
        rawstring

    print
        repr "hi"
        | 1 2
        ~ 0xff
        not true
        not false
        + "hi" "ho"
        != "a" "b"
        * 2 3
        % 4.3 3
        == 4 5
        == 3 2
        == 3 3
        == 3 3.0
        == 3.0 3
        == 3 3.5
        == 2.5 2.5
        float

