IR

"

the typed bangra expression format is
    (: expression type)

at this point it is assumed that all expressions nested in <expression> are
typed.

a root expression that is not a typed expression is expanded / resolved before
expansion continues.

"

include "api.b"
include "macros.b"
include "libc.b"

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
defvalue key-definition
    quote "definition"
defvalue key-env-meta
    quote meta

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

define new-env (meta-env)
    function Value Value
    defvalue obj
        call new-table
    defvalue symtable
        call new-table
    call set-key! obj key-symbols symtable
    call set-key! obj key-env-meta meta-env
    ret obj

define get-symbol (env name)
    function Value Value Value
    ret
        call get-key
            call get-symbols env
            name

define resolve-symbol (env name)
    function Value Value Value
    defvalue result
        call get-key
            call get-symbols env
            name
    ret
        ?
            icmp != result (null Value)
            result
            splice
                defvalue meta-env
                    call get-key env key-env-meta
                ?
                    icmp == meta-env (null Value)
                    null Value
                    call resolve-symbol meta-env name


define set-symbol (env name value)
    function void Value Value Value
    call set-key!
        call get-symbols env
        name
        value
    ret;

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

define error? (value)
    function i1 Value
    ret
        call value==
            call get-type value
            quote error

define typed (value-expr type-expr)
    function Value Value Value
    ret
        call ref
            call set-next (quote :)
                    call set-next value-expr
                        call clear-next type-expr

define type? (value)
    function i1 Value
    ret
        call value== value
            quote type

define typeclass? (value)
    function i1 Value
    ret
        call value==
            call at value
            quote typeclass

define typeclass-name (value)
    function Value Value
    ret
        call next
            call at value

define typeclass-table (value)
    function Value Value
    ret
        call next
            call next
                call at value

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

defvalue key-result-type
    quote "result-type"

# stores type of last element in env
define expand-untype-expression (value env)
    MacroFunction
    defvalue expanded-value
        call expand-expression value env
    defvalue result
        ? (call typed? expanded-value)
            splice
                defvalue expanded-type
                    call get-type expanded-value
                call set-key! env key-result-type expanded-type
                call replace value
                    call get-expr expanded-value
            expanded-value
    ret result

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

define extract-type-map-parameters (value env)
    MacroFunction
    defvalue input-type
        call get-type
            defvalue input-expr
                call next
                    defvalue input-names value
    defvalue input-name
        call at input-names
    call set-symbol env input-name
        call typed input-name input-type
    defvalue next-input-name
        call next input-name
    #print input-name input-type
    ret
        call set-next
            input-type
            select
                icmp == next-input-name (null Value)
                null Value
                call set-next!
                    call ref next-input-name
                    call next input-expr

define expand-call (resolved-head value env)
    function Value Value Value Value
    defvalue sym-type
        call get-type resolved-head
    ret
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
                call replace value
                    call typed
                        qquote
                            call
                                unquote funcname
                                unquote-splice castedparams
                        call return-type sym-type
            call type? sym-type;
                defvalue typedef
                    call get-expr resolved-head
                if
                    call typeclass? typedef;
                        defvalue obj
                            call typeclass-table typedef
                        defvalue type-params
                            call get-key obj key-definition
                        defvalue body
                            call next type-params
                        # expand params to retrieve final types
                        defvalue params
                            call map
                                call next
                                    call at value
                                env
                                expand-expression
                        defvalue subenv
                            call new-env env
                        defvalue paramtypes
                            call map
                                call set-next
                                    type-params
                                    params
                                subenv
                                extract-type-map-parameters
                        defvalue untyped-params
                            call map params env
                                expand-untype-expression
                        defvalue expanded-body
                            call map body subenv
                                expand-untype-expression
                        defvalue return-type
                            call get-key subenv key-result-type
                        defvalue function-type
                            qquote
                                function
                                    unquote return-type
                                    unquote-splice paramtypes
                        defvalue function-decl
                            qquote
                                define "" (unquote type-params)
                                    unquote function-type
                                    ret
                                        splice
                                            unquote-splice expanded-body
                        call replace value
                            qquote
                                :
                                    call
                                        unquote function-decl
                                        unquote-splice untyped-params
                                    unquote return-type
                    else
                        qquote
                            :
                                error "can not instantiate type"
                                    unquote value
                                error
            else
                qquote
                    :
                        error "symbol is not a function or macro"
                            unquote value
                        error

define expand-expression (value env)
    MacroFunction
    ret
        ?
            call atom? value
            splice
                if
                    call symbol? value;
                        defvalue resolved-sym
                            call resolve-symbol env value
                        ?
                            icmp == resolved-sym (null Value)
                            qquote
                                :
                                    error "unknown symbol"
                                        unquote value
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
                    call real? value;
                        call replace value
                            qquote
                                :
                                    unquote value
                                    float
                    else
                        qquote
                            :
                                error "invalid atom"
                                    unquote value
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
                            call expand-expression head env
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
                                call expand-call resolved-head value env

global global-env
    null Value
global type
    null Value

define set-global (key value)
    function void Value Value
    call set-symbol
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
        qquote
            IR
                define ::ret:: ()
                    function void
                    unquote-splice result
                    ret;
                execute ::ret::

define macro-bangra (ir-env value)
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
            call next value
            load global-env
            expand-untype-expression

    ret
        qquote
            run
                unquote-splice result

# install bangra preprocessor
run
    store
        call new-env
            null Value
        global-env

    defvalue type-type
        call new-table

    call set-global
        quote type
        qquote
            :
                unquote type-type
                unquote type-type

    call set-global
        quote int
        quote
            : i32 type
    call set-global
        quote rawstring
        quote
            : rawstring type
    call set-global
        quote ...
        quote
            : ... type

    call set-global-syntax
        quote <-
        define "" (value env)
            MacroFunction
            defvalue arg-params
                call next
                    defvalue arg-rettype
                        call next
                            call at value
            defvalue rettype
                call expand-expression arg-rettype env
            defvalue params
                call map
                    call at arg-params
                    env
                    expand-untype-expression
            ret
                call replace value
                    qquote
                        :
                            function
                                unquote
                                    call get-expr rettype
                                unquote-splice params
                            type

    call set-global-syntax
        quote function
        define "" (value env)
            MacroFunction
            defvalue params
                call next
                    call at value
            defvalue template
                call new-table
            call set-key! template
                key-definition
                params
            ret
                call replace value
                    qquote
                        :
                            typeclass template
                                unquote template
                            type

    call set-global-syntax
        quote extern-C
        define "" (value env)
            MacroFunction
            defvalue namestr
                call next
                    call at value
            defvalue type
                call expand-expression
                    call next namestr
                    env
            defvalue type-expr
                call get-expr type
            ret
                call replace value
                    qquote
                        :
                            declare
                                unquote namestr
                                unquote type-expr
                            unquote type-expr

    call set-global-syntax
        quote let
        define "" (value env)
            MacroFunction
            defvalue expr
                call expand-expression
                    call next
                        defvalue name
                            call next
                                call at value
                    env
            defvalue expr-type
                call get-type expr
            ret
                ?
                    call type? expr-type
                    splice
                        call set-symbol env name expr
                        call replace value
                            qquote
                                :
                                    splice;
                                    void
                    splice
                        call set-symbol env name
                            call typed
                                name
                                expr-type
                        call replace value
                            call typed
                                qquote
                                    defvalue
                                        unquote name
                                        unquote
                                            call get-expr expr
                                expr-type

    call set-preprocessor
        &str "bangra"
        global-preprocessor

    call set-macro env
        &str "bangra"
        macro-bangra

/// bangra

    let printf
        : printf (function i32 rawstring ...)

    let testf2
        function (arg1 arg2)
            printf "%s %s\n" arg1 arg2

    let testf
        function (arg1 arg2)
            testf2 arg1 arg2

    printf "%i\n"
        testf "hello" "world"
