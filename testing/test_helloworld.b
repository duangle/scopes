IR

include "../api.b"
include "../libc.b"

defvalue hello-world
    bitcast
        global ""
            "Hello World!\n"
        rawstring

/// bang
    qquote
        do
            #qquote (x y z)
            word1
            unquote-splice
                a b c
            word2

define main ()
    function void
    label ""
        call dump-value
            call ref
                call join
                    quote _Value (a b)
                    call at
                        quote _Value (a b c)
        # defining and calling a function inside another function
        call
            define "" ()
                function void
                label ""
                    ret;
        defvalue Q
            quote _Value word
        call printf
            bitcast
                global ""
                    "quote = %x %p %p\n"
                rawstring
            dump
                load
                    getelementptr
                        bitcast
                            Q
                            * i32
                        0
            Q
            Q

        call printf hello-world
        cond-br
            int i1 1
            label then
            label else
    label then
        defvalue c0
            bitcast
                global "" "Choice 1\n"
                rawstring
        br
            label done
    label else
        defvalue c1
            bitcast
                global "" "Choice 2\n"
                rawstring
        br
            label done
    label done
        call printf
            phi
                rawstring
                c0 then
                c1 else
        ret;

# dump-module
execute main
