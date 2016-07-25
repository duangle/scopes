IR

include "../macros.b"

run
    printf
        &str "%s\n"
        string-value
            format-value
                quote
                    print
                        string-concat
                            quote "hel\x00lo "
                            quote wo\x00rld
                -1

    print
        string-concat
            quote "hel\x00lo "
            quote wo\x00rld
    defvalue k
        quote "world!"
    print
        # wo
        string-slice k 0 2
        # world!
        string-slice k 0 0x7fffffff
        # rl
        string-slice k 2 -2
        # d!
        string-slice k -2 0x7fffffff

