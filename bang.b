bang ;

dump-module ;

defvalue printf
    extern "printf"
        function-type int32 ((pointer-type int8) ...)

defvalue hello-world
    bitcast
        const-global
            "Hello World!\n"
        pointer-type int8

defvalue $main
    function "" ()
        function-type void ()
        label entry
            call printf hello-world
            ret ;
