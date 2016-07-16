IR

include "../api.b"
include "../libc.b"

execute
    define "" ()
        function void
        defvalue $entry this-block
        call printf
            bitcast (global "" "executable-path = %s\nargc = %i\n") rawstring
            load executable-path
            load argc
        br
            block $loop
        set-block $loop
        defvalue i
            phi i32
                0 $entry
        call printf
            bitcast (global "" "argv[%i] = %s\n") rawstring
            i
            load
                getelementptr
                    load
                        argv
                    i
        defvalue nextvar
            add i 1
        incoming i
            nextvar $loop
        cond-br
            icmp u< nextvar
                load argc
            $loop
            block $done
        set-block $done
        ret;
