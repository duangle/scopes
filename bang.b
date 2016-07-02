bang

; dump LLVM IR for this module after compilation
; (dump-module)

; import-c: parse and compile C++ source code / headers using clang
import-c bang ./bang.h ()

; meta-eval: runs code in the compiler context, allowing to compile & register
; new expression handlers before the rest of the module is translated.
proto-eval
    ; dump-module ;

    extern printf
        function-type int32 ((pointer-type int8) ...)

    call printf
        array-ref "running in the compiler!\n"

extern printf
    function-type int32 ((pointer-type int8) ...)
extern sin
    function-type double (double)

function computesin ()
    function-type double ()
    call sin
        const-real double 0.5

function dostuff-hyphenated (a b)
    function-type int32 (int32 int32)
    do
        ?
            const-int bool 0
            ()
            call printf
                array-ref "value = %f! (false) %i %i\n"
                call computesin
                \ a b
    const-int int32 0

call dostuff-hyphenated
    const-int int32 1
    const-int int32 2

call printf
    array-ref "Hello World! %s\n"
    call return_test_string

call printf
    array-ref "%p\n"
    function stuffz ()
        function-type int32 ()
        const-int int32 0


