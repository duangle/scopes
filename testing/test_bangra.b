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
            unquote-splice head;
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

define single (value)
    function Value Value
    ret
        call set-next value (null Value)

define typed? (value)
    function i1 Value
    ret
        call value== (call at value) (quote :)

define get-expr (value)
    function Value Value
    ret
        call next
            call at value

define get-type (value)
    function Value Value
    ret
        call next
            call next
                call at value

define typed (value-expr type-expr)
    function Value Value Value
    ret
        call ref
            call set-next (quote :)
                    call set-next value-expr
                        call clear-next type-expr

define function-type? (value)
    function i1 Value
    ret
        call value==
            call at value
            quote function

define pointer-type? (value)
    function i1 Value
    ret
        call value==
            call at value
            quote pointer

define element-type (value)
    function Value Value
    ret
        call next
            call at value

define return-type (value)
    function Value Value
    ret
        call next
            call at value

define param-types (value)
    function Value Value
    ret
        call next
            call next
                call at value

define replace (fromvalue tovalue)
    function Value Value Value
    ret
        call set-next tovalue
            call next fromvalue


declare expand-expression
    MacroFunction

# list is mapped chain-aware
define map (value ctx f)
    function Value Value Value (pointer MacroFunction)
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
                        call f expr ctx
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

define expand-untype-expression (value env)
    MacroFunction
    defvalue expanded-value
        call expand-expression value env
    ret
        ? (call typed? expanded-value)
            call replace value
                call get-expr expanded-value
            expanded-value

defvalue ellipsis
    quote ...

define type== (value1 value2)
    function i1 Value Value
    ret
        if
            call value== value1 ellipsis;
                true
            call value== value2 ellipsis;
                true
            or (call pointer? value1) (call pointer? value2);
                call type==
                    call at value1
                    call at value2
            call value== value1 value2;
                ?
                    and
                        icmp == value1 (null Value)
                        icmp == value2 (null Value)
                    true
                    call type==
                        call next value1
                        call next value2
            else false

define cast-untype-parameter (value env)
    MacroFunction
    defvalue src-tuple
        call next
            defvalue dest-types value
    defvalue dest-type
        call at dest-types
    # if ellipsis, keep, otherwise iterate to next type
    defvalue next-dest-type
        select
            call value== dest-type (quote ...)
            dest-type
            call next dest-type
    defvalue src-value
        call get-expr src-tuple
    defvalue src-type
        call get-type src-tuple
    defvalue casted-src-value
        ?
            call type== src-type dest-type
            src-value
            qquote
                bitcast
                    unquote src-value
                    unquote dest-type
    ret
        call set-next casted-src-value
            select
                icmp == next-dest-type (null Value)
                null Value
                call set-next!
                    call ref next-dest-type
                    call next src-tuple

define expand-expression (value env)
    MacroFunction
    defvalue tail
        call next value
    ret
        ?
            call atom? value
            splice
                if
                    call symbol? value;
                        defvalue resolved-sym
                            call get-key!
                                call get-symbols env
                                value
                        ?
                            icmp == resolved-sym (null Value)
                            qquote
                                :
                                    error
                                        unquote value
                                        "unknown symbol"
                                    error
                            call replace value resolved-sym
                    call string? value;
                        call replace value
                            qquote
                                :
                                    global ""
                                        unquote value
                                    pointer
                                        array i8
                                            unquote
                                                call new-integer
                                                    call string-size value
                    call integer? value;
                        call replace value
                            qquote
                                :
                                    unquote value
                                    i32
                    else
                        qquote
                            :
                                error
                                    unquote value
                                    "invalid atom"
                                error
            splice
                defvalue head
                    call at value
                ?
                    call typed? value
                    value
                    splice
                        # resolve first argument
                        defvalue resolved-head
                            call expand-expression
                                head
                                env
                        if
                            # macro?
                            call handle? resolved-head;
                                call expand-expression
                                    call
                                        bitcast
                                            call handle-value resolved-head
                                            pointer MacroFunction
                                        value
                                        env
                                    env
                            # function call
                            else
                                defvalue resolved-params
                                    call map
                                        call next
                                            call at value
                                        env
                                        expand-expression
                                defvalue sym-type
                                    call get-type resolved-head
                                if
                                    call function-type? sym-type;
                                        defvalue funcname
                                            call get-expr resolved-head
                                        defvalue funcparamtypes
                                            call param-types sym-type
                                        # expand params to retrieve final types
                                        defvalue params
                                            call map
                                                call next
                                                    call at value
                                                env
                                                expand-expression
                                        # casted params
                                        defvalue castedparams
                                            call map
                                                # prepend types to params so mapping
                                                # function can read them
                                                call set-next
                                                    call ref funcparamtypes
                                                    params
                                                env
                                                cast-untype-parameter
                                        call typed
                                            qquote
                                                call
                                                    unquote funcname
                                                    unquote-splice castedparams
                                            call return-type sym-type
                                    else
                                        qquote
                                            :
                                                error
                                                    unquote value
                                                    "symbol is not a function or macro"
                                                error


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
        call map
            call next
                call at value
            load global-env
            expand-untype-expression

    ret
        call dump-value
            qquote
                IR
                    define ::ret:: ()
                        function void
                        unquote-splice result
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
            defvalue expr
                call next
                    defvalue name
                        call next
                            call at value
            defvalue expr-type
                call get-type expr
            defvalue newexpr
                call replace value
                    call typed
                        qquote
                            defvalue
                                unquote name
                                unquote
                                    call get-expr expr
                        expr-type
            call set-key!
                call get-symbols env
                name
                call typed
                    name
                    expr-type
            ret newexpr

    call set-preprocessor
        &str "bangra"
        global-preprocessor

module test-bangra bangra

    let printf
        :
            declare "printf"
                function i32 rawstring ...
            function i32 rawstring ...

    printf "%s %s\n" "hello" "world"



