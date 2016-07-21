IR

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
    function (pointer MacroFunction) Value Value
    ret
        bitcast
            call handle-value
                call get-key
                    call get-symbols env
                    head
            pointer MacroFunction

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
                                null (pointer MacroFunction)
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
    function void Value (pointer MacroFunction)
    call set-key
        call get-symbols
            load global-env
        head
        call new-handle
            bitcast
                handler
                pointer opaque
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
                defvalue head-expr
                    alloca Value
                store (null Value) head-expr
                defvalue prev-expr
                    alloca Value
                store (null Value) prev-expr
                loop expr
                    value
                    icmp != expr (null Value)
                    call next expanded-expr
                    defvalue expanded-expr
                        call expand-macro expr env
                    defvalue @prev-expr
                        load prev-expr
                    ? (icmp == @prev-expr (null Value))
                        splice
                            store expanded-expr head-expr
                            false
                        splice
                            call set-next!
                                @prev-expr
                                expanded-expr
                            false
                    store expanded-expr prev-expr
                load head-expr

# all top level expressions go through the preprocessor, which then descends
# the expression tree and translates it to bangra IR.
define global-preprocessor (ir-env value)
    preprocessor-func
    call set-key
        load global-env
        key-ir-env
        call new-handle
            bitcast
                ir-env
                pointer opaque
    defvalue result
        call expand-expression-list
            call next
                call at value
            load global-env

    ret
        call ref
            call join
                quote IR
                result

# install bangra preprocessor
run
    store
        call new-table
        global-env
    defvalue symtable
        call new-table
    call set-key
        load global-env
        key-symbols
        symtable
    call set-preprocessor
        bitcast (global "" "bangra") rawstring
        global-preprocessor

module test-bangra bangra
    escape
        splice
            include "../libc.b"
            include "../macros.b"

            define test-func ()
                function void
                call printf
                    &str "hello world!\n"
                ret;

    escape
        run
            call test-func
