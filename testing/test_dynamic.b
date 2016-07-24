IR

'

the bangra language only deals with dynamic data types (Value) on its top level

every bangra function follows this IR signature::
    (function Value Upvalues Value)

where the return value is the first element of a chain of return values, the
first argument is a chain of upvalues, and the second argument is the first
element of a chain of arguments. All three arguments can be null.

a bangra module is specified to export a bangra function "::ret::" which
returns a table that maps symbols to other objects.

'

include "../api.b"
include "../macros.b"
include "../libc.b"

################################################################################
# build and install the preprocessor hook function.

deftype Upvalues Value
deftype &opaque (pointer opaque)

defvalue internal
    quote internal

defvalue header-function
    quote function

deftype Function
    function Value Upvalues Value

define print (upval param)
    Function
    call dump-value
        call ref param
    ret
        null Value

define add (upval param)
    Function
    defvalue result
        alloca i64
    store
        int i64 0
        result
    loop value
        param
        icmp != value (null Value)
        call next value
        store
            add
                load result
                call integer-value value
            result

    ret
        call new-integer
            load result

define wrap-pointer (head ptr)
    function Value Value &opaque
    ret
        qquote
            unquote head;
                unquote
                    call new-handle ptr

define wrap-function (f)
    function Value (pointer Function)
    ret
        call wrap-pointer
            header-function
            bitcast f &opaque

deftype MacroFunction
    function Value Value Value

defvalue key-symbols
    quote "symbols"
defvalue key-ir-env
    quote "ir-env"

define function? (value)
    function i1 Value
    defvalue head
        call at value
    ret
        ?
            call value== head header-function
            call handle?
                call next head
            false

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

define make-call-params (value)
    function Value Value
    ret
        ?
            icmp == value (null Value)
            quote (null Value)
            qquote
                call cons
                    unquote
                        call set-next value
                            null Value
                    unquote
                        call make-call-params
                            call next value

define expand-expression (value env)
    MacroFunction
    defvalue tail
        call next value
    ret
        call set-next
            ?
                call atom? value
                if
                    icmp != (call symbol? value) true;
                        qquote
                            quote _Value
                                unquote
                                    call set-next value (null Value)
                    else
                        qquote
                            call error-message
                                call get-ir-env env
                                value
                                &str "unknown atom"
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
                                call get-key
                                    call get-symbols env
                                    head
                            if
                                icmp == handler internal;
                                    defvalue params
                                        call make-call-params
                                            call expand-expression-list
                                                call next head
                                                env
                                    qquote
                                        call
                                            unquote
                                                call set-next head (null Value)
                                            null Value
                                            unquote params
                                else
                                    qquote
                                        error
                                            unquote value
                                            "function expected"
            tail

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

define export-global-function (name f)
    function Value Value (pointer Function)
    ret
        qquote
            defvalue
                unquote name
                quote
                    Function
                    unquote
                        call new-handle
                            bitcast f &opaque

define cons (a b)
    function Value Value Value
    cond-br
        icmp == a (null Value)
        block $is-null
        block $else
    set-block $is-null
    ret
        call set-next
            call ref a
            b
    set-block $else
    ret
        call set-next a b

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
        qquote
            IR
                #include "../libc.b"

                defvalue ref
                    declare "bangra_ref" (function Value Value)
                defvalue set-next
                    declare "bangra_set_next" (function Value Value Value)
                defvalue set-next!
                    declare "bangra_set_next_mutable" (function Value Value Value)
                unquote
                    call export-global-function (quote print) print
                unquote
                    call export-global-function (quote +) add
                defvalue cons
                    quote (function Value Value Value)
                        unquote
                            call new-handle
                                bitcast cons &opaque

                define ::ret:: ()
                    function void
                    unquote result
                    ret;
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

    call set-global (quote print) internal
    call set-global (quote +) internal

    call set-preprocessor
        &str "bangra"
        global-preprocessor

module test-bangra bangra
    print
        + 1 2 3
        \ 1 2 3
        + 1 2 3 4 5
        "test"

