# boot script
# the bangra executable looks for a boot file at
# path/to/executable.b and, if found, executes it.
bangra

let x 5

syntax-run
    print "running in new context!" x
    tupleof
        quote do
        quote let x 6
        tupleof
            quote print
            "running in main context!"
            \ "overriden value:" (quote x) "old value:" x

do
    let x
        + 2
            + 4 5
    apply
        eval (locals)
            quote
                print x

let f
    function (return)
        return 2
        3

::* apply
::* eval (locals)
::* quote do
print # displays 3
    f
        function (x) x

print # displays 2
    call/cc f

let proc
    function (x)
        print "======= STAGE 2 ======="
        apply
            eval x globals

::* apply
::* eval globals
::* quote do
let testf
    print "declaring testf..."
    function ()
        let
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
#dump testf
testf;

print "quoted:"
    quote test
    quote 1 2 3

do
    :: let x
    function (x)
        x
    print x

# prints 14 6
print
    :: + 2
    :: + 3
    :: + 4
    5
    6

# prints ((+ 2 3) (+ 4 6) (+ 10 12))
print
    ::* quote

    + 2 3

    + 4 6

    + 10 12

do
    let rec
        function (x f)
            f x
            branch (> x 0)
                function ()
                    rec (- x 1) f
                function ()
                    print "done."
    rec 4
        function (k)
            print k

let
    sin
        external "sin"
            cdecl double (tupleof double) false
    x 10
    plus2
        print "declaring plus2..."
        function (x)
            + x 2
    api
        import-c "bangra.h" (tupleof)
    puts
        external "puts"
            cdecl int (tupleof rawstring) false
    testtext "yo yo yo"
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
            structof
                tupleof "#parent" globals
            quote
                + 0.5 0.5

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

