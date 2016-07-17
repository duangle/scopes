IR

///
    include "../api.b"
    include "../macros.b"

    ################################################################################
    # build and install the preprocessor hook function.

    deftype MacroFunction
        function Value Value Value

    defvalue key-symbols
        quote "symbols"
    defvalue key-ir-env
        quote "ir-env"

    define get-ir-env (env)
        function Environment Value
        ret
            bitcast
                call handle-value
                    call get-key env key-ir-env
                Environment

    define get-symbols (env)
        function Value Value
        ret
            call get-key env key-symbols

    define get-handler (env head)
        function (* MacroFunction) Value Value
        ret
            bitcast
                call handle-value
                    call get-key
                        call get-symbols env
                        head
                * MacroFunction

    define expand-macro (value env)
        MacroFunction
        ret
            ?
                call atom? value
                splice
                    call error-message
                        call get-ir-env env
                        value
                        bitcast (global "" "unknown atom") rawstring
                    null Value
                splice
                    defvalue head
                        call at value
                    ?
                        call expression? value (quote escape)
                        splice
                            call set-next
                                call next
                                    call at value
                                call next value
                        splice
                            defvalue handler
                                call get-handler env head
                            ?
                                icmp ==
                                    null (* MacroFunction)
                                    handler
                                splice
                                    call error-message
                                        call get-ir-env env
                                        value
                                        bitcast (global "" "unknown special form, macro or function") rawstring
                                    null Value
                                    call expand-macro
                                        call handler value env
                                        env

    defvalue global-env
        global global-env
            null Value

    define set-global-syntax (head handler)
        function void Value (* MacroFunction)
        call set-key
            call get-symbols
                load global-env
            head
            call new-handle
                bitcast
                    handler
                    * opaque
        ret;

    # expression list is expanded chain-aware
    define expand-expression-list (value env)
        function Value Value Value
        ret
            ?
                icmp == value
                    null Value
                null Value
                splice
                    defvalue expanded-first-expr
                        call expand-macro value env
                    defvalue second-expr
                        call next expanded-first-expr
                    ?
                        icmp == second-expr
                            null Value
                        expanded-first-expr
                        label $loop
                            defvalue expr
                                phi Value
                                    second-expr $first-expr
                            defvalue prev-expr
                                phi Value
                                    expanded-first-expr $first-expr
                            defvalue expanded-expr
                                call expand-macro expr env
                            call set-next! prev-expr expanded-expr
                            defvalue next-expr
                                call next expanded-expr
                            incoming expr
                                next-expr $loop
                            incoming prev-expr
                                expanded-expr $loop
                            cond-br
                                icmp == next-expr
                                    null Value
                                expanded-first-expr
                                $loop

    # all top level expressions go through the preprocessor, which then descends
    # the expression tree and translates it to bangra IR.
    define global-preprocessor (ir-env value)
        preprocessor-func
        label ""
            call set-key
                load global-env
                key-ir-env
                call new-handle
                    bitcast
                        ir-env
                        * opaque
            defvalue result
                call expand-expression-list
                    call next
                        call at value
                    load global-env

            ret
                call ref
                    call join
                        quote _Value IR
                        result

    # install bangra preprocessor
    execute
        define "" ()
            function void
            label ""
                store
                    call new-table
                    global-env
                defvalue symtable
                    call new-table
                call set-key
                    load global-env
                    key-symbols
                    symtable
                call set-global-syntax
                    quote _Value "qquote"
                    macro-qquote
                call set-preprocessor
                    bitcast (global "" "bangra") rawstring
                    global-preprocessor
                ret;

    module test-bangra bangra
        escape
            define test-func ()
                function void
                label ""
                    ret;
