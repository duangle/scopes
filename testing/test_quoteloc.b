IR

include "../macros.b"
include "../libc.b"

define macro__file__ (env expr)
    preprocessor-func
    defvalue path
        call anchor-path expr
    ret
        ?
            icmp == path
                null (typeof path)
            null Value
            call new-string path

define macro__line__ (env expr)
    preprocessor-func
    ret
        call new-integer
            zext
                call anchor-lineno expr
                i64

run
    call set-macro env
        &str "__file__"
        macro__file__
    call set-macro env
        &str "__line__"
        macro__line__

run
    defvalue lineno (add (__line__) 1)
    call printf
        &str "%s:%i: here\n"
        &str
            __file__;
        lineno
