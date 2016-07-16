IR

include "../libc.b"

define test-loop (n)
    function void i32
    defvalue $entry this-block
    br
        block $loop
    set-block $loop
    defvalue i
        phi i32
            1 $entry
    call printf
        bitcast (global "" "i = %i\n") rawstring
        i
    defvalue nextvar
        add i 1
    incoming i
        nextvar $loop
    cond-br
        icmp u< i n
        $loop
        block $done
    set-block $done
    ret;

execute
    define "" ()
        function void
        call test-loop 10
        ret;

