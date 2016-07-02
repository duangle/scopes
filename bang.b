bang

; dump LLVM IR for this module after compilation
;(dump-module)

; import-c: parse and compile C++ source code / headers using clang
import-c bang ./bang.h ()
###import-c bang2 ./clang/include/llvm-c/Core.h (
    -x c
    -I./clang/include
    -I./clang/lib/clang/3.8.0/include
    -D__STDC_LIMIT_MACROS
    -D__STDC_CONSTANT_MACROS
    )

; proto-eval: runs code in the compiler context, allowing to compile & register
; new expression handlers before the rest of the module is translated.
proto-eval
    dump-module ;

    extern printf
        function-type int32 ((pointer-type int8) ...)

    call printf
        array-ref "running in the compiler! (meta=%p, preproc=%p)\n"
        meta-environment
        call get-preprocessor meta-environment

    var preprocessor
        function (env expr)
            Preprocessor
            call printf
                array-ref "s-expr received!\n"
            ? (call list? env expr)
                call printf (array-ref "is a list!\n")
                ? (call symbol? env expr)
                    call printf (array-ref "is a symbol!\n")
                    ? (call string? env expr)
                        call printf (array-ref "is a string\n")
                        call printf (array-ref "is god knows what!\n")
            expr

    call set-preprocessor meta-environment preprocessor

var int int32

extern printf
    function-type int ((pointer-type int8) ...)

var hello-world
    array-ref "Hello World!\n"

call printf hello-world


