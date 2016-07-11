IR

# the bootstrap language is feature equivalent to LLVM IR, and translates
# directly to LLVM API commands. It sounds like a Turing tarpit, but the fact
# that it gives us simple means to get out of it and extend those means
# turns it more into Turing lube.

# from here, outside the confines of the horribly slow and tedious C++ compiler
# context, we're going to implement our own language.

################################################################################
# declare the bang API as we don't have the means to comfortably import clang
# declarations yet.

# opaque declarations for the bang compiler Environment and the Values of
# its S-Expression tree, which can be List, String, Symbol, Integer, Real.
struct _Environment
struct _Value
struct opaque

deftype rawstring (* i8)

declare printf (function i32 rawstring ...)
declare strcmp (function i32 rawstring rawstring)

declare bang_print (function void rawstring)

deftype Environment (* _Environment)
deftype Value (* _Value)

defvalue dump-value
    declare "bang_dump_value" (function void Value)

deftype preprocessor-func
    function Value Environment Value

defvalue set-preprocessor
    declare "bang_set_preprocessor" (function void (* preprocessor-func))

defvalue kind-of
    declare "bang_get_kind" (function i32 Value)

defvalue at
    declare "bang_at" (function Value Value)

defvalue next
    declare "bang_next" (function Value Value)

defvalue set-next
    declare "bang_set_next" (function Value Value Value)

defvalue ref
    declare "bang_ref" (function Value Value)

defvalue set-key
    declare "bang_set_key" (function void Value rawstring Value)

defvalue get-key
    declare "bang_get_key" (function Value Value rawstring)

defvalue handle-value
    declare "bang_handle_value" (function (* opaque) Value)

defvalue handle
    declare "bang_handle" (function Value (* opaque))

defvalue value==
    declare "bang_eq" (function i1 Value Value)

defvalue string-value
    declare "bang_string_value" (function rawstring Value)

defvalue error-message
    declare "bang_error_message" (function void Value rawstring ...)

defvalue set-anchor
    declare "bang_set_anchor" (function Value Value Value)

# redeclare pointer types to specialize our mapping handler
deftype bang-mapper-func
    function Value Value i32 Value
defvalue bang-map
    declare "bang_map" (function Value Value (* bang-mapper-func) Value)

defvalue value-type-none 0
defvalue value-type-list 1
defvalue value-type-string 2
defvalue value-type-symbol 3
defvalue value-type-integer 4
defvalue value-type-real 5

################################################################################
# fundamental helper functions

defvalue list?
    define "" (value)
        function i1 Value
        label ""
            ret
                icmp ==
                    call kind-of value
                    value-type-list

# non-list or empty list
defvalue atom?
    define "" (value)
        function i1 Value
        label ""
            cond-br
                call list? value
                label $is-list
                    ret
                        icmp ==
                            call at value
                            null Value
                label $is-not-list
                    ret (int i1 1)

defvalue symbol?
    define "" (value)
        function i1 Value
        label ""
            ret
                icmp ==
                    call kind-of value
                    value-type-symbol

defvalue expression?
    define "" (value expected-head)
        function i1 Value Value
        label ""
            cond-br
                call atom? value
                label $is-atom
                    ret (int i1 0)
                label $is-not-atom
                    ret
                        call value==
                            call at value
                            expected-head

defvalue type-key
    getelementptr
        global "" "#bang-type"
        \ 0 0

defvalue set-type
    define "" (value value-type)
        function void Value Value
        label ""
            call set-key
                value
                type-key
                value-type
            ret;

defvalue get-type
    define "" (value)
        function Value Value
        label ""
            ret
                call get-key
                    value
                    type-key

defvalue ref-set-next
    define "" (lhs rhs)
        function Value Value Value
        label ""
            ret
                call ref
                    call set-next lhs rhs

# appends ys ... to xs ...
define join (xs ys)
    function Value Value Value
    label ""
        cond-br
            icmp == xs
                null Value
            label $is-null
                ret ys
            label $is-not-null
                ret
                    call set-next xs
                        call join
                            call next xs
                            ys

################################################################################
# build and install the preprocessor hook function.

deftype MacroFunction
    function Value Value Value

define expand-macro (value env)
    MacroFunction
    label ""
        cond-br
            call list? value
            label $is-list
            label $is-atom

    label $is-list
        cond-br
            call atom? value
            label $is-empty-list
            label $is-expression

    label $is-atom
        call error-message value
            bitcast (global "" "unknown atom") rawstring
        br
            label $error
    label $is-empty-list
        call error-message value
            bitcast (global "" "expression is empty") rawstring
        br
            label $error

    label $is-expression
        defvalue head
            call at value
        cond-br
            call expression? value (quote _Value escape)
            label $is-escape
                ret
                    call at
                        call next value
            label $is-not-escape
                defvalue handler
                    bitcast
                        call handle-value
                            call get-key env
                                call string-value head
                        * MacroFunction
                cond-br
                    icmp ==
                        null (* MacroFunction)
                        handler
                    label $has-no-handler
                    label $has-handler

    label $has-handler
        ret
            call expand-macro
                call
                    phi (* MacroFunction)
                        handler $is-not-escape
                    value
                    env
                env

    label $has-no-handler
        call error-message value
            bitcast (global "" "unknown special form, macro or function") rawstring
        br
            label $error

    label $error
        ret
            null Value

defvalue global-env
    quote _Value ()

define qquote-1 (value)
    function Value Value
    label ""
        cond-br
            call atom? value
            label $is-not-list
            label $is-list
    label $is-not-list
        ret
            call ref
                call set-next
                    quote _Value quote
                    call set-next
                        value
                        null Value
    label $is-list
        cond-br
            call expression? value
                quote _Value unquote
            label $is-unquote
            label $is-not-unquote
    label $is-unquote
        ret
            call next
                call at value
    label $is-not-unquote
        cond-br
            call expression? value
                quote _Value qquote
            label $is-qquote
            label $is-not-qquote
    label $is-qquote
        ret
            call qquote-1
                call qquote-1
                    call next
                        call at value
    label $is-not-qquote
        cond-br
            call atom?
                call at value
            label $is-arbitrary-list
            label $head-is-list
    label $head-is-list
        cond-br
            call expression?
                call at value
                quote _Value unquote-splice
            label $is-unquote-splice
            label $is-arbitrary-list
    label $is-unquote-splice
        ret
            call ref-set-next
                quote _Value append
                call set-next
                    call next
                        call at
                            call at value
                    call qquote-1
                        call ref
                            call next
                                call at value
    label $is-arbitrary-list
        ret
            call ref-set-next
                quote _Value prepend
                call set-next
                    call qquote-1
                        call at value
                    call qquote-1
                        call ref
                            call next
                                call at value

define macro-qquote (value env)
    MacroFunction
    label ""
        defvalue qq
            call qquote-1
                call next
                    call at value
        call dump-value qq
        ret qq

define bang-mapper (value index env)
    bang-mapper-func
    label ""
        cond-br
            icmp == index 0
            label $is-head
                ret
                    quote _Value do-splice
            label $is-body
                ret
                    call expand-macro value global-env

# all top level expressions go through the preprocessor, which then descends
# the expression tree and translates it to bang IR.
define global-preprocessor (ir-env value)
    preprocessor-func
    label ""
        cond-br
            call expression? value (quote _Value bang)
            label $is-bang
                ret
                    call bang-map
                        call at value
                        \ bang-mapper global-env
            label $else
                ret value

# install preprocessor and continue evaluating the module
run
    define "" ()
        function void
        label ""
            call set-key global-env
                bitcast
                    global "" "qquote"
                    rawstring
                call handle
                    bitcast
                        macro-qquote
                        * opaque

            call set-preprocessor global-preprocessor
            ret;

# all top level expressions from here go through the preprocessor
# we only recognize and expand expressions that start with (bang ...)



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
        /// call dump-value
            quote _Value
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
                                    rawstring
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
run main

