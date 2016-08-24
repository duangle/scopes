# boot script
# the bangra executable looks for a boot file at
# path/to/executable.b and, if found, executes it.
bangra

let printf
    external "printf"
        cdecl int (rawstring ...)

call printf "%s %s!\n" "hello" "world"


///
    include "libc.b"
    include "macros.b"
    include "lang.b"

    run
        call printf
            &str "startup script loaded.\n"

