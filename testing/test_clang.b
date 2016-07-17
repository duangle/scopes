IR

include "../macros.b"

run
    defvalue dest
        call ref
            null Value
    call import-c-module dest
        @str "C-Module"
        @str "../bangra.h"
        null (* rawstring)
        0
    call dump-value dest

