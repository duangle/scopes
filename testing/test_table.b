IR

include "../macros.b"

run
    defvalue test-table
        table
            quote a;
                1
            quote b;
                quote 2
            "c"
                quote 3
    print
        call get-key test-table (quote a)
        call get-key test-table (quote b)
        call get-key test-table (quote c)

dump-module;