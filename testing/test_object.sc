
let C =
    import-c "libc.c" "
        #include <stdio.h>
        "
        list;

fn main (argc argv)
    C.printf "hello world\n"
    return 0

#main none none
let table = (Scope)
set-scope-symbol! table 'main
    typify main i32 (pointer rawstring)

compile-object "test.o" table 'dump-module
