bang

struct TestStruct packed i32 i8

declare somef
    function TestStruct
        array i64 5
        vector i32 4

declare printf
    function i32 (* i8) ...

defvalue hello-world
    bitcast
        global ""
            "Hello World!\n"
        * i8

run
    define "" ()
        function void
        label ""
            call printf
                bitcast
                    global ""
                        "running in compiler!\n"
                    * i8
            ret;

struct Value
defvalue dump-value
    declare "bang_dump_value"
        function void (* Value)

define main ()
    function void
    label ""
        call dump-value
            quote Value
                run
                    print "'\"" '"\''
                    print "yo
                    yo" "hey hey" "ho"
                    print (
                    ) a b c (
                     ) d e f
                        g h i
                    # compare
                    do
                        if cond1:
                            do-this;
                        else if cond2:
                            do-that;
                        else:
                            do-something;

                    # to
                    do
                        if (cond1) {
                            do-this;
                        } else if (cond2) {
                            do-that;
                        } else {
                            do-that;
                        }
                    0x1A.25A 0x.e 0xaF0.3 0 1 1e+5 .134123123 123 012.3 12.512e+12 0 0 0 0 0 +1 -0.1 +0.1 2.5 +1 -1 +100 -100 +100
                    0666 ..5 ... :: .: a. a: a:a:a .a .a ...a 35.3 0x7fffffffffffffff 0x10 +inf -inf +nan -nan
                    -.0 1.0 1.594 1 .1e+12 .0 0. +5 +.5 .5 -1 -.5 - +a +0 -0 -2 2.5 inf nan n na i in
                    ... a. a: aa a,b,c 0.
                    a = 3, b = 4, c = 5;
                    {a b c} [(d)f g]
                    {a,(),;b, c;d e;}[]
                    [abc:a,b,c d,d;a,b,c,d;]
                    [][a,b,d,e f;]
                    [a = b,c = d,e = f]
                    [a b: c d,q,d e,e,]
                    [a b: c d;q;d e;e;]
                    ab.bc..cd...de.a.b.c.d
                    [ptr, * ptr, const * ptr]
                    int x, int y; x = 5, y = z
                    do                                # (do
                        a; b; c d
                    . .. ... ....
                    {
                        if a: b q, c d, d e;
                        if b: c;
                        if c {
                            b q;
                            c d;
                            d e;
                            };
                        }
                    a b;c d;
                        f g
                    do
                        print x; print
                            a + b
                    if q: a b, c d;
                    a b c,
                        d e f
                    e f, g h, i j k,m;
                    g h, i j k;
                    n o;
                    f g,q,w,q e
                    (if a == b && c == d: print a; print b;)
                    if a == b && c == d:
                        print "yes"; print "no"
                        print c
                    {
                        if (true)

                        {
                        }
                        else if (false)
                        {
                        }
                        else
                        {
                        };
                        print("hi",1,2,3,auto(),5,2 + 1);
                    }

                    if a == b && c == d: print a; print b;
                    if a; q e
                        a b c d;
                        e f;
                        g h; [i];
                        g h; j; k; l; m
                        j k; l m
                        teamo beamo
                    else
                        e f g
                    define "" ()
                        # comment
                        function void
                        label ""
                            call printf
                                bitcast
                                    global ""
                                        "running in compiler!\n"
                                    * i8
                            ret;

        defvalue Q
            quote Value word
        call printf
            bitcast
                global ""
                    "quote = %x %p %p\n"
                * i8
            dump
                load
                    getelementptr
                        bitcast
                            Q
                            * i32
                        int i32 0
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
                * i8
        br
            label done
    label else
        defvalue c1
            bitcast
                global "" "Choice 2\n"
                * i8
        br
            label done
    label done
        call printf
            phi
                * i8
                c0 then
                c1 else
        ret;

# dump-module
run main
