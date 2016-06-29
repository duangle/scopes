bang

(dump-module)

import-c bang0 ./bang0.cpp (
    -DBANG_HEADER_ONLY
    -I./clang/lib/clang/3.8.0/include
    )

extern LLVMVoidType
    function-type (pointer-type int8) ()

extern dlopen
    function-type (pointer-type int8) ((pointer-type int8) int32)

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
            dump
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
