# boot script
# the bangra executable looks for a boot file at
# path/to/executable.b and, if found, executes it.
IR

include "libc.b"
include "macros.b"

run
    call printf
        &str "startup script loaded.\n"
