IR

include "../macros.b"

# from http://llvm.org/docs/LangRef.html#getelementptr-instruction

struct RT i8 (array (array i32 20) 10) i8
struct ST i32 double RT

define foo (s)
    function (& i32) (& ST)
    ret
        getelementptr s 1 2 1 5 13

#dump-module;
run
    call foo
        null (& ST)
