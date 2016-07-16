IR

include "../api.b"

################################################################################
# build and install the preprocessor hook function.

deftype MacroFunction
    function Value Value Value

define get-ir-env (env)
    function Environment Value
    label ""
        ret
            bitcast
                call handle-value
                    call get-key env
                        quote _Value "ir-env"
                Environment

define expand-macro (value env)
    MacroFunction
    label ""
        cond-br
            call atom? value
            label $is-atom
            label $is-expression

    label $is-atom
        call error-message
            call get-ir-env env
            value
            bitcast (global "" "unknown atom") rawstring
        br
            label $error
    label $is-empty-list
        call error-message
            call get-ir-env env
            value
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
                            call get-key env head
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
        call error-message
            call get-ir-env env
            value
            bitcast (global "" "unknown special form, macro or function") rawstring
        br
            label $error

    label $error
        ret
            null Value

defvalue global-env
    global global-env
        null Value

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

define expand-expression-list (value env)
    function Value Value Value
    label $entry
        cond-br
            icmp == value
                null Value
            label $empty
                ret
                    null Value
            label $loop
                defvalue expr
                    phi Value
                        value $entry
                call dump-value expr


                defvalue next-expr
                    call next expr
                incoming expr
                    next-expr $loop
                cond-br
                    icmp == next-expr
                        null Value
                    label $done
                        ret
                            null Value
                    $loop

# all top level expressions go through the preprocessor, which then descends
# the expression tree and translates it to bangra IR.
define global-preprocessor (ir-env value)
    preprocessor-func
    label ""
        call set-key
            load global-env
            quote _Value "ir-env"
            call new-handle
                bitcast
                    ir-env
                    * opaque
        defvalue result
            call expand-expression-list
                call next
                    call at value
                load global-env
        call dump-value result
        ret result

# install bangra preprocessor
execute
    define "" ()
        function void
        label ""
            store
                call new-table
                global-env
            call set-key
                load global-env
                quote _Value "qquote"
                call new-handle
                    bitcast
                        macro-qquote
                        * opaque

            call set-preprocessor
                bitcast (global "" "bangra") rawstring
                global-preprocessor
            ret;

module test-bangra bangra

    qquote test
