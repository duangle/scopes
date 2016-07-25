bangra

let printf
    extern-C "printf"
        <- int (rawstring ...)

let testf2
    function (arg1 arg2)
        printf "%s %s\n" arg1 arg2

let testf
    function (arg1 arg2)
        testf2 arg1 arg2

printf "%i\n"
    testf "hello" "world"

:
    dump-module;
    void
