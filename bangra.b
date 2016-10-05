# boot script
# the bangra executable looks for a boot file at
# path/to/executable.b and, if found, executes it.
bangra

syntax-scope (scope)
    structof
        tupleof "#parent" scope
        tupleof "syntax"
            syntax-macro
                function (env expr)
                    let name
                        @ expr 0 1 0
                    let func
                        @ expr 0 1 1
                    let scope-param
                        parameter "scope"
                    expand env
                        cons
                            slist syntax-scope (slist scope-param)
                                slist structof
                                    slist tupleof "#parent" scope-param
                                    slist tupleof (string name)
                                        slist syntax-macro
                                            cons (quote function) func
                            @ expr 1

let x 5
syntax get-x (env expr)
    cons x
        @ expr 1
do
    let x 6
    print "get-x:" x (get-x)

do
    let k
        quote a b c
    print
        @ k 0
        @ k 1 0
        @ k 1 1 0

let f
    function (return)
        return 2
        3

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

print
    + 1 2 3 4 5
# prints 14 6
print
    ::* + 2
    ::* + 3
    ::* + 4
    5

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

