bangra

let printf
    extern-C "printf"
        <- int (rawstring ...)

let +
    function (a b)
        IR-qquote
            typeof a
            add
                IR-unquote a
                IR-unquote b

let print
    function (x)
        printf "%s\n" x

let testf2
    function (arg1 arg2)
        printf "%s %s\n" arg1 arg2

let testf
    function (arg1 arg2)
        testf2 arg1 arg2

let sayhi
    function ()
        printf "hi!\n"

sayhi;

let sayhi
    function ()
        printf "yo!\n"
        sayhi;

sayhi;

print
    IR-qquote rawstring
        bitcast
            IR-unquote "hi"
            rawstring

printf "%i\n"
    +
        + 2 3
        + 4 5

printf "%i\n"
    testf "hello" "world"

testf "olleh" "dlrow"
