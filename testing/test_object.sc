
let C =
    import-c "libc.c" "
        #include <stdio.h>
        "
        list;

fn main (argc argv)
    C.printf "hello world\n"
    return 0

let main = (typify main i32 (pointer rawstring))
compile-object
    module-dir .. "/test.o"
    scopeof
        main = main
    'no-debug-info
    #'dump-module

