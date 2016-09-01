# boot script
# the bangra executable looks for a boot file at
# path/to/executable.b and, if found, executes it.
bangra

# types are first-class values
let printf-cdecl
    cdecl int (rawstring ...)

let puts
    cdecl int (rawstring)

let use-printf false

let printf
    external "printf" printf-cdecl

let text
    "hello %s %i %f\n"

select use-printf
    do
        call printf "yes it's true!\n"
    do
        let count
            call printf text "world" 0xff 2.5
        call printf "%i %i\n" count false

///
    include "libc.b"
    include "macros.b"
    include "lang.b"

    run
        call printf
            &str "startup script loaded.\n"

