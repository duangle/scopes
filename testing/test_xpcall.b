IR

include "../macros.b"

define blow-up ()
    function void
    raise
        quote BANG!
    unreachable;

run
    xpcall
        null (pointer opaque)
        define "" (ctx)
            xpcall-try-func
            blow-up;
            printf
                &str "went fine!\n"
            ret
                null (pointer opaque)
        define "" (ctx value)
            xpcall-except-func
            printf
                &str "blew up!\n"
            print value
            ret
                null (pointer opaque)
