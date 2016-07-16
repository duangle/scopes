IR

include "../api.b"

################################################################################
# build and install the preprocessor hook function.

deftype MacroFunction
    function Value Value Value

defvalue key-symbols
    quote _Value "symbols"
defvalue key-ir-env
    quote _Value "ir-env"

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
    cond-br
        call atom? value
        label $is-atom
            call error-message
                call get-ir-env env
                value
                bitcast (global "" "unknown atom") rawstring
            br
                label $error
                    ret
                        null Value
        label $is-expression
            defvalue head
                call at value
            cond-br
                call expression? value (quote _Value escape)
                label $is-escape
                    ret
                        call set-next
                            call next
                                call at value
                            call next value
                label $is-not-escape
                    defvalue handler
                        call get-handler env head
                    cond-br
                        icmp ==
                            null (* MacroFunction)
                            handler
                        label $has-no-handler
                            call error-message
                                call get-ir-env env
                                value
                                bitcast (global "" "unknown special form, macro or function") rawstring
                            br $error
                        label $has-handler
                            ret
                                call expand-macro
                                    call
                                        phi (* MacroFunction)
                                            handler $is-not-escape
                                        value
                                        env
                                    env

defvalue global-env
    global global-env
        null Value

define set-global-syntax (head handler)
    function void Value (* MacroFunction)
    label ""
        call set-key
            call get-symbols
                load global-env
            head
            call new-handle
                bitcast
                    handler
                    * opaque
        ret;

define qquote-1 (value)
    function Value Value
    label ""
        cond-br
            call atom? value
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
                        ret
                            call next
                                call at value
                    label $is-not-unquote
                        cond-br
                            call expression? value
                                quote _Value qquote
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
                                    label $head-is-list
                                        cond-br
                                            call expression?
                                                call at value
                                                quote _Value unquote-splice
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
                                            $is-arbitrary-list

define macro-qquote (value env)
    MacroFunction
    label ""
        defvalue qq
            call qquote-1
                call next
                    call at value
        #call dump-value qq
        ret qq

# expression list is expanded chain-aware
define expand-expression-list (value env)
    function Value Value Value
    label $entry
        cond-br
            icmp == value
                null Value
            label $empty
                ret
                    null Value
            label $first-expr
                defvalue expanded-first-expr
                    call expand-macro value env
                defvalue second-expr
                    call next expanded-first-expr
                cond-br
                    icmp == second-expr
                        null Value
                    label $done
                        ret expanded-first-expr
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
                            $done
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
