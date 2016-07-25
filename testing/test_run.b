IR

defvalue addsub
    define "" (a b)
        function (struct "" i32 i32) i32 i32
        ret
            structof ""
                add a b
                sub a b

define myfunc ()
    function i32
    ret
        extractvalue
            call addsub 2 3
            0

execute
    define "" (env)
        function Value Environment
        ret
            quote _Value
                dump-module;
    3
