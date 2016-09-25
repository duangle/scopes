# boot script
# the bangra executable looks for a boot file at
# path/to/executable.b and, if found, executes it.
bangra

/// let fac
    ///function (n)
        select (<= n 1)
            1
            * n (this-function (- n 1))
    function (n)
        let fac-times
            function (n acc)
                select (== n 0)
                    acc
                    this-function (- n 1) (* acc n)
        fac-times n 1

let repeat
    function (m init f)
        select (<= m 0)
            null
            apply
                function (n x)
                    let nx
                        f n x
                    let nn (+ n 1)
                    select (== nn m)
                        x
                        this-function nn nx
                \ 0 init

repeat 10 1
    function (n x)
        print n x
        * x 2

let sin
    external "sin"
        cdecl double (tupleof double) false

print
    sin 0.5

print
    0.1
    sin 0
    sin 0.1
    sin 0.5
    sin 1.2
print "done"

let x 10

let plus2
    function (x)
        + x 2

print
    plus2 50
print
    proto-eval
        plus2 x

let api
    import-c "bangra.h" (tupleof)

let new-symbol
    @ api "bangra_symbol"
let string-size
    @ api "bangra_string_size"
let string-value
    @ api "bangra_string_value"
# signed long long int bangra_string_size(ValueRef expr);

let puts
    external "puts"
        cdecl int (tupleof rawstring) false

puts
    string-value
        new-symbol "12345"

let testtext
    "yo yo yo"
print
    puts testtext
    sin 5

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
