IR

include "../macros.b"
include "../libc.b"

defstruct X i32 i32 float
dumptype
    struct "x" i32 i32

defvalue sumv
    add 2 3

select false
    error 0 "should never be triggered!"
    true

dump
    structof "" 1 2 3.5
dump
    structof X 1 2 3.5
dump
    arrayof i32 1 2 3
        add 4 5
dump
    vectorof 1.2 2 3 4.5

dump
    add
        vectorof 1 2 3 4
        vectorof 4 3 2 1

defvalue c
    extractvalue
        "ABCDE*GHIJ"
        sumv

dump
    select
        icmp == (add 1 2) 3
        "test"
        42.5

define faddsub (a b)
    function (vector float 2) float float
    ret
        vectorof
            fadd a b
            fsub a b
dump faddsub

define test ()
    function void
    defvalue v
        call faddsub 0.2 0.8
    call printf
        &str "%c %f %f\n"
        c
        fpext (extractelement v 0) double
        fpext (extractelement v 1) double
    ret;
dump test

run
    call test