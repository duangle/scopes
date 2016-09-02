# boot script
# the bangra executable looks for a boot file at
# path/to/executable.b and, if found, executes it.
bangra

# types are first-class values
let printf-cdecl
    cdecl int (rawstring ...)

let puts
    cdecl int (rawstring)

let printf
    external "printf" printf-cdecl

let generate-a-function
    function (use-printf)
        let text
            "hello %s %i %f\n"

        function ()
            select use-printf
                do
                    apply printf "yes it's true!\n"
                do
                    let count
                        apply printf text "world" 0xff 2.5
                    apply printf "%i %i\n" count false

let call-a-function
    function (f use-printf)
        apply
            apply f use-printf

apply call-a-function generate-a-function false
apply call-a-function generate-a-function true

///
    include "libc.b"
    include "macros.b"
    include "lang.b"

    run
        call printf
            &str "startup script loaded.\n"
