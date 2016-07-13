IR

include "api.b"

################################################################################
# build and install the preprocessor hook function.

deftype MacroFunction
    function Value Value Value

define expand-macro (value env)
    MacroFunction
    label ""
        cond-br
            call atom? value
            label $is-atom
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
        call error-message value
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
                    call expand-macro value
                        load global-env

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
                        bang-mapper
                        load global-env
            label $else
                ret value

# install preprocessor and continue evaluating the module
run
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

            call set-preprocessor global-preprocessor
            ret;

# all top level expressions from here go through the preprocessor
# we only recognize and expand expressions that start with (bang ...)


