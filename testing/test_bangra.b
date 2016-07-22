IR

"

the typed bangra expression format is
    (: expression type)

at this point it is assumed that all expressions nested in <expression> are
typed.

a root expression that is not a typed expression is expanded / resolved before
expansion continues.






"

include "../api.b"
include "../macros.b"
include "../libc.b"

################################################################################
# build and install the preprocessor hook function.

deftype &opaque (pointer opaque)

define wrap-pointer (head ptr)
    function Value Value &opaque
    ret
        qquote
            unquote head;
                unquote
                    call new-handle ptr

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
                call get-key! env key-ir-env
            Environment

define get-symbols (env)
    function Value Value
    ret
        call get-key! env key-symbols

define get-handler (env head)
    function (pointer MacroFunction) Value Value
    ret
        bitcast
            call handle-value
                call get-key!
                    call get-symbols env
                    head
            pointer MacroFunction

declare expand-expression
    MacroFunction

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
                        call expand-expression expr env
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

define single (value)
    function Value Value
    ret
        call set-next value (null Value)

define expand-expression (value env)
    MacroFunction
    defvalue tail
        call next value
    ret
        ?
            call atom? value
            call set-next
                qquote
                    error
                        unquote
                            call single value
                        "unknown atom"
                tail
            splice
                defvalue head
                    call at value
                ?
                    call value== head (quote :)
                    call set-next
                        call next head
                        tail
                    splice
                        defvalue handler
                            call get-key!
                                call get-symbols env
                                head
                        if
                            call handle? handler;
                                call expand-expression
                                    call
                                        bitcast
                                            call handle-value handler
                                            pointer MacroFunction
                                        value
                                        env
                                    env
                            else
                                qquote
                                    error
                                        unquote
                                            call set-next value
                                                null Value
                                        "unable to resolve symbol"

global global-env
    null Value

define set-global (key value)
    function void Value Value
    call set-key!
        call get-symbols
            load global-env
        key
        value
    ret;

define set-global-syntax (head handler)
    function void Value (pointer MacroFunction)
    call set-global
        head
        call new-handle
            bitcast
                handler
                pointer opaque
    ret;

# the top level expression goes through the preprocessor, which then descends
# the expression tree and translates it to bangra IR.
define global-preprocessor (ir-env value)
    preprocessor-func
    call set-key!
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
        call dump-value
            qquote
                IR
                    #include "../libc.b"

                    define ::ret:: ()
                        function void
                        unquote result
                        ret;
                    dump-module;
                    execute ::ret::

# install bangra preprocessor
run
    store
        call new-table
        global-env
    defvalue symtable
        call new-table
    call set-key!
        load global-env
        key-symbols
        symtable

    call set-global-syntax
        quote let
        define "" (value env)
            MacroFunction
            ret
                call set-next
                    qquote
                        :
                            true
                            boolean
                    call next value

    call set-preprocessor
        &str "bangra"
        global-preprocessor

module test-bangra bangra

    :
        declare printf
            function i32 rawstring ...
        function i32 rawstring ...

    :
        call printf
            bitcast (global "" "hello world\n") rawstring
        i32


