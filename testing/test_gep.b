IR

include "../macros.b"

# from http://llvm.org/docs/LangRef.html#getelementptr-instruction

defstruct RT i8 (array (array i32 20) 10) i8
defstruct ST i32 double RT

dumptype
    getelementtype (pointer ST) 1 2 1 5 13

define foo (s)
    function (pointer i32) (pointer ST)
    ret
        getelementptr s 1 2 1 5 13

dump
    icmp ==
        lengthof
            getelementtype
                typeof foo
                0
        2

dumptype
    getelementtype (typeof foo) 0 0

#dump-module;
run
    call foo
        null (pointer ST)
