IR

include "../api.b"
include "../libc.b"
include "../macros.b"

run
    call dump-value
        qquote
            stuff
                directly splicing lists:
                    unquote
                        call at
                            quote (x y z)
                    \ , and that is it
    ?
        int i1 1
        call printf
            @str "choice X!\n"
    ?
        int i1 1
        ?
            int i1 1
            call printf
                @str "choice 11!\n"
            call printf
                @str "choice 10!\n"
        ?
            int i1 1
            call printf
                @str "choice 01!\n"
            call printf
                @str "choice 00!\n"
    call printf
        @str "selection %i\n"
        if
            int i1 0;
                call printf
                    @str "choice A!\n"
                1
            int i1 0;
                call printf
                    @str "choice B!\n"
                2
            int i1 0;
                call printf
                    @str "choice C!\n"
                3
            else
                call printf
                    @str "choice D!\n"
                4

    loop i 0 (icmp u< i 10) (add i 1)
        call printf
            @str "-----\n"
        call printf
            @str "boing! %i\n"
            i

    loop expr
        call at (quote (a b c d))
        icmp != expr (null Value)
        call next expr

        call dump-value expr

