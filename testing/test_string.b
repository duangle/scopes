IR

include "../macros.b"

run
    call printf
        &str "%s\n"
        call string-value
            call format-value
                quote
                    print
                        call string-concat
                            quote "hel\x00lo "
                            quote wo\x00rld
                -1

    print
        call string-concat
            quote "hel\x00lo "
            quote wo\x00rld
    defvalue k
        quote "world!"
    print
        # wo
        call string-slice k 0 2
        # world!
        call string-slice k 0 0x7fffffff
        # rl
        call string-slice k 2 -2
        # d!
        call string-slice k -2 0x7fffffff

