# boot script
# the bangra executable looks for a boot file at
# path/to/executable.b and, if found, executes it.
bangra

dump
    function ()
        print
            apply-rec
                function (func-a func-b)
                    print "func-x!" func-a func-b
                    func-a 1
                function (x)
                    print "func-a"
                    + (func-b x) 1
                function (x)
                    print "func-b"
                    * x 2

apply
    function (sin x plus2 api puts testtext)
        print
            sin 0.5

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

        print
            scope;
        print
            plus2 50

        print
            puts testtext
            sin 5

        apply
            @
                tupleof
                    function ()
                        print "no"
                    function ()
                        print "yes"
                < x 12

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

    external "sin"
        cdecl double (tupleof double) false
    10
    function (x)
        + x 2
    import-c "bangra.h" (tupleof)
    external "puts"
        cdecl int (tupleof rawstring) false
    "yo yo yo"

