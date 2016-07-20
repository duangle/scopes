IR

include "../macros.b"
include "../libc.b"

declare llvm.cos.f64
    function double double

run
    defvalue angle
        real double 1.0471975511965979
    call printf
        &str "llvm.cos.f64(%f) = %f\n"
        angle
        call llvm.cos.f64 angle

