IR

include "../api.b"
include "../libc.b"

execute
    define "" ()
        function void
        label $entry
            call printf
                bitcast (global "" "executable-path = %s\nargc = %i\n") rawstring
                load executable-path
                load argc
            br
                label $loop
        label $loop
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
                label $done
        label $done
            ret;
