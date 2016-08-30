# boot script
# the bangra executable looks for a boot file at
# path/to/executable.b and, if found, executes it.
bangra

let puts
    external "puts"
        cdecl int (rawstring)

let printf
    external "printf"
        cdecl int (rawstring ...)

call printf "hello %s %i %f\n" "world" 0xff 2.5


///
    include "libc.b"
    include "macros.b"
    include "lang.b"

    run
        call printf
            &str "startup script loaded.\n"

