#!/usr/bin/env bangra
IR

include "../macros.b"
include "../libc.b"

define macro-testfile (env expr)
    preprocessor-func
    defvalue filename
        call set-next
            call next expr
            null Value
    ret
        qquote
            module (unquote filename) IR
                include (unquote filename)

run
    call set-macro env
        &str "testfile"
        macro-testfile

testfile "test_table.b"
testfile "test_run.b"
testfile "test_string.b"
testfile "test_andor.b"
testfile "test_constexpr.b"
testfile "test_intrinsic.b"
testfile "test_argv.b"
testfile "test_ascii.b"
testfile "test_dynamic.b"
testfile "test_gep.b"
testfile "test_helloworld.b"
testfile "test_intro.b"
testfile "test_loop.b"
testfile "test_macro.b"
testfile "test_parsing.b"
testfile "test_quoteloc.b"
testfile "test_submodule.b"
testfile "test_bangra.b"

run
    call printf
        &str "\n\nAll tests finished.\n"
