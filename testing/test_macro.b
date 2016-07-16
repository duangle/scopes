IR

include "../api.b"
include "../libc.b"

define macro-quote (env expr)
    preprocessor-func
    ret
        call ref
            call join
                quote _Value escape
                call ref
                    call join
                        call at
                            quote _Value (quote _Value)
                        expr

define macro-@str (env expr)
    preprocessor-func
    ret
        call ref
            call join
                call join
                    quote _Value bitcast
                    call ref
                        call join
                            call at
                                quote _Value (global "")
                            expr
                quote _Value rawstring

execute
    define "" (env)
        function void Environment
        call set-macro env
            bitcast (global "" "quote") rawstring
            macro-quote
        call set-macro env
            bitcast (global "" "@str") rawstring
            macro-@str
        ret;

define qquote-1 (value)
    function Value Value
    cond-br
        call atom? value
        block $is-not-list
        block $is-list
    set-block $is-not-list
    ret
        call ref
            call set-next
                quote quote
                call set-next
                    value
                    null Value
    set-block $is-list
    cond-br
        call expression? value
            quote unquote
        block $is-unquote
        block $is-not-unquote
    set-block $is-unquote
    ret
        call next
            call at value
    set-block $is-not-unquote
    cond-br
        call expression? value
            quote qquote
        block $is-qquote
        block $is-not-qquote
    set-block $is-qquote
    ret
        call qquote-1
            call qquote-1
                call next
                    call at value
    set-block $is-not-qquote
    ret
        call ref
            call join
                call at
                    quote (call prepend)
                call join
                    call qquote-1
                        call at value
                    call qquote-1
                        call ref
                            call next
                                call at value

define macro-qquote (env expr)
    preprocessor-func
    ret
        call qquote-1 expr

define macro-run (env expr)
    preprocessor-func
    ret
        call ref
            call join
                quote execute
                call ref
                    call join
                        call at
                            quote (define "" (env))
                        call join
                            quote (function void Environment)
                            call join expr
                                quote (ret)

execute
    define "" (env)
        function void Environment
        call set-macro env
            @str "run"
            macro-run
        call set-macro env
            @str "qquote"
            macro-qquote
        ret;

defvalue macro-?
    define "" (env expr)
        preprocessor-func
        defvalue else-expr
            call next
                defvalue then-expr
                    call next
                        defvalue condition expr
        defvalue label-then (call unique-symbol (@str "then"))
        defvalue label-else (call unique-symbol (@str "else"))
        defvalue label-finally (call unique-symbol (@str "if-end"))
        defvalue label-then-br (call unique-symbol (@str "then-br"))
        defvalue label-else-br (call unique-symbol (@str "else-br"))
        defvalue value-then (call unique-symbol (@str "value"))
        defvalue value-else (call unique-symbol (@str "value"))
        ret
            qquote
                splice
                    cond-br
                        unquote
                            call set-next condition (null Value)
                        block
                            unquote label-then
                        block
                            unquote label-else
                    block
                        unquote label-finally
                    set-block
                        unquote label-then
                    defvalue
                        unquote value-then
                        unquote
                            call set-next then-expr (null Value)
                    defvalue
                        unquote label-then-br
                        this-block
                    br
                        unquote label-finally
                    set-block
                        unquote label-else
                    defvalue
                        unquote value-else
                        unquote
                            call set-next else-expr (null Value)
                    defvalue
                        unquote label-else-br
                        this-block
                    br
                        unquote label-finally
                    set-block
                        unquote label-finally
                    phi
                        typeof (unquote value-then)
                        (unquote value-then) (unquote label-then-br)
                        (unquote value-else) (unquote label-else-br)

run
    call set-macro env
        @str "?"
        macro-?

run
    call dump-value
        qquote
            stuff
                directly splicing lists:
                    unquote
                        call at
                            quote (x y z)
                    \ , and that is it
    ?
        int i1 1
        ?
            int i1 1
            call printf
                @str "choice 11!\n"
            call printf
                @str "choice 10!\n"
        ?
            int i1 1
            call printf
                @str "choice 01!\n"
            call printf
                @str "choice 00!\n"

dump-module;