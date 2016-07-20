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

run
    call set-macro env
        &str "__file__"
        macro__file__

run
    call printf
        &str "file: %s\n"
        &str
            __file__;
