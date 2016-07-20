IR

include "../macros.b"
include "../libc.b"

block outside-func
set-block outside-func
ret 0

define test ()
    function i32
    br outside-func
    outside-func

run
    call test