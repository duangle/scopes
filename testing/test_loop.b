IR

include "../libc.b"

define test-loop (n)
    function void i32
    label $entry
        br
            label $loop
    label $loop
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
            label $done

    label $done
        ret;

execute
    define "" ()
        function void
        label ""
            call test-loop 10
            ret;
