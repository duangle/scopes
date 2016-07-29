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

# UTILITIES
#-------------------------------------------------------------------------------

define single (value)
    function Value Value
    ret
        set-next value (null Value)

define replace (fromvalue tovalue)
    function Value Value Value
    ret
        set-next tovalue
            next fromvalue

deftype FoldFunction
    function Value (pointer opaque) Value

define fold (ctx value f)
    function Value (pointer opaque) Value (pointer FoldFunction)
    ret
        ? (icmp == value (null Value))
            null Value
            set-next
                f ctx value
                fold ctx
                    next value
                    f

# TYPE CONSTRUCTORS
#-------------------------------------------------------------------------------

global type-cache
    null Value

global type-type
    null Value

global null-type
    null Value
global error-type
    null Value

global ellipsis-type
    null Value

global void-type
    null Value
global opaque-type
    null Value

global half-type
    null Value
global float-type
    null Value
global double-type
    null Value

global bool-type
    null Value

global int8-type
    null Value
global int16-type
    null Value
global int32-type
    null Value
global int64-type
    null Value

global uint8-type
    null Value
global uint16-type
    null Value
global uint32-type
    null Value
global uint64-type
    null Value

global rawstring-type
    null Value

global function-typeclass
    null Value

# all types
defvalue KEY_REPR
    quote "repr"
defvalue KEY_IR_REPR
    quote "IR-repr"

defvalue KEY_APPLY # for types that support apply syntax
    quote "apply"

# functions
defvalue KEY_RETURN_TYPE
    quote "return-type"
defvalue KEY_PARAMETER_TYPES
    quote "parameter-types"

# templates
defvalue KEY_PARAMETER_NAMES
    quote "parameter-names"
defvalue KEY_BODY
    quote "body"

# tuples
defvalue KEY_ELEMENT_TYPES
    quote "element-types"

# arrays and vectors
defvalue KEY_ELEMENT_TYPE
    quote "element-type"
defvalue KEY_SIZE
    quote "size"

# integers
defvalue KEY_WIDTH
    quote "width"
defvalue KEY_SIGNED
    quote "signed"

define type? (value)
    function i1 Value
    ret
        table? value

defvalue type-repr
    define "" (value)
        function Value Value
        assert
            type? value
        defvalue result
            get-key value KEY_REPR
        ret
            select (icmp == result (null Value))
                value
                result

defvalue type-ir-repr
    define "" (value)
        function Value Value
        assert
            type? value
            value
        defvalue result
            get-key value KEY_IR_REPR
        ret
            select (icmp == result (null Value))
                value
                result

defvalue array-vector-type
    define "" (prefix element-type size)
        function Value Value Value i64
        defvalue cache
            load type-cache
        defvalue sizevalue
            new-integer size
        defvalue element-type-repr
            type-repr element-type
        defvalue array-repr
            ref
                set-next prefix
                    set-next element-type-repr sizevalue
        defvalue key
            format-value array-repr -1
        defvalue cached
            get-key cache key
        ret
            ?
                icmp != cached (null Value)
                cached
                splice
                    defvalue ir-array-repr
                        ref
                            set-next prefix
                                set-next
                                    type-ir-repr element-type
                                    sizevalue

                    defvalue newtype
                        table
                            KEY_ELEMENT_TYPE element-type
                            KEY_SIZE sizevalue
                            KEY_REPR array-repr
                            KEY_IR_REPR ir-array-repr

                    set-key! cache key newtype
                    newtype

defvalue array-type
    define "" (element-type size)
        function Value Value i64
        ret
            array-vector-type
                quote array
                \ element-type size

defvalue vector-type
    define "" (element-type size)
        function Value Value i64
        ret
            array-vector-type
                quote vector
                \ element-type size

defvalue tuple-type
    define "" (element-types)
        function Value Value
        defvalue cache
            load type-cache
        defvalue repr-element-types
            fold
                null (pointer opaque)
                element-types
                define "" (ctx value)
                    FoldFunction
                    ret
                        type-repr value
        defvalue tuple-repr
            ref
                set-next
                    quote tuple
                    repr-element-types
        defvalue key
            format-value tuple-repr -1
        defvalue cached
            get-key cache key
        ret
            ?
                icmp != cached (null Value)
                cached
                splice
                    defvalue ir-repr-element-types
                        fold
                            null (pointer opaque)
                            element-types
                            define "" (ctx value)
                                FoldFunction
                                ret
                                    type-ir-repr value
                    defvalue ir-tuple-repr
                        ref
                            set-next
                                quote struct
                                set-next
                                    quote ""
                                    ir-repr-element-types
                    defvalue newtype
                        table
                            KEY_ELEMENT_TYPES element-types
                            KEY_REPR tuple-repr
                            KEY_IR_REPR ir-tuple-repr
                    set-key! cache key newtype
                    newtype

defvalue template-type
    define "" (parameter-names body)
        function Value Value Value
        defvalue template-repr
            ref
                set-next
                    quote template
                    set-next parameter-names
                        ref body
        defvalue newtype
            table
                KEY_PARAMETER_NAMES parameter-names
                KEY_BODY body
                KEY_REPR template-repr
        ret newtype

defvalue function-type
    define "" (return-type parameter-types)
        function Value Value Value
        defvalue cache
            load type-cache
        defvalue repr-return-type
            type-repr return-type
        defvalue repr-parameter-types
            fold
                null (pointer opaque)
                parameter-types
                define "" (ctx value)
                    FoldFunction
                    ret
                        type-repr value
        defvalue function-repr
            ref
                set-next
                    quote <-
                    set-next
                        repr-return-type
                        ref repr-parameter-types
        defvalue key
            format-value function-repr -1
        defvalue cached
            get-key cache key
        ret
            ?
                icmp != cached (null Value)
                cached
                splice
                    defvalue ir-repr-return-type
                        type-ir-repr return-type
                    defvalue ir-repr-parameter-types
                        fold
                            null (pointer opaque)
                            parameter-types
                            define "" (ctx value)
                                FoldFunction
                                ret
                                    type-ir-repr value
                    defvalue ir-function-repr
                        ref
                            set-next
                                quote function
                                set-next ir-repr-return-type
                                    ir-repr-parameter-types
                    defvalue newtype
                        table
                            KEY_RETURN_TYPE return-type
                            KEY_PARAMETER_TYPES parameter-types
                            KEY_REPR function-repr
                            KEY_IR_REPR ir-function-repr
                    set-meta! newtype
                        load function-typeclass
                    set-key! cache key newtype
                    newtype

defvalue pointer-type
    define "" (element-type)
        function Value Value
        defvalue cache
            load type-cache
        defvalue element-type-repr
            type-repr element-type
        defvalue pointer-repr
            ref
                set-next (quote &) element-type-repr
        defvalue key
            format-value pointer-repr -1
        defvalue cached
            get-key cache key
        ret
            ?
                icmp != cached (null Value)
                cached
                splice
                    defvalue ir-pointer-repr
                        ref
                            set-next (quote pointer)
                                type-ir-repr element-type

                    defvalue newtype
                        table
                            KEY_ELEMENT_TYPE element-type
                            KEY_REPR pointer-repr
                            KEY_IR_REPR ir-pointer-repr
                    set-key! cache key newtype
                    newtype

defvalue special-type
    define "" (repr ir-repr)
        function Value Value Value
        ret
            table
                KEY_REPR repr
                KEY_IR_REPR
                    ? (icmp == ir-repr (null Value))
                        repr
                        ir-repr

defvalue int-type
    define "" (width signed)
        function Value i32 i1
        defvalue cache
            load type-cache
        defvalue width-value
            new-integer
                zext width i64
        defvalue int-repr
            ? (icmp == width 1)
                quote bool
                string-concat
                    select signed (quote int) (quote uint)
                    format-value width-value -1
        defvalue key int-repr
        defvalue cached
            get-key cache key
        ret
            ?
                icmp != cached (null Value)
                cached
                splice
                    defvalue ir-int-repr
                        string-concat
                            quote i
                            format-value width-value -1

                    defvalue newtype
                        table
                            KEY_WIDTH key
                            KEY_SIGNED
                                new-integer
                                    zext signed i64
                            KEY_REPR int-repr
                            KEY_IR_REPR ir-int-repr

                    set-key! cache key newtype
                    newtype

defvalue init-global-types
    define "" ()
        function void

        store (new-table) type-cache

        store (new-table) function-typeclass

        store
            special-type (quote type) (null Value)
            type-type
        store
            special-type (quote error) (null Value)
            error-type
        store
            special-type (quote null) (null Value)
            null-type

        store
            special-type (quote ...) (null Value)
            ellipsis-type

        store
            special-type (quote void) (null Value)
            void-type
        store
            special-type (quote opaque) (null Value)
            opaque-type
        store
            special-type (quote half) (null Value)
            half-type
        store
            special-type (quote float) (null Value)
            float-type
        store
            special-type (quote double) (null Value)
            double-type

        store (int-type 8 true) int8-type
        store (int-type 16 true) int16-type
        store (int-type 32 true) int32-type
        store (int-type 64 true) int64-type

        store (int-type 1 false) bool-type

        store (int-type 8 false) uint8-type
        store (int-type 16 false) uint16-type
        store (int-type 32 false) uint32-type
        store (int-type 64 false) uint64-type

        store (pointer-type (load int8-type)) rawstring-type


        ret;

# TYPED EXPRESSIONS
#-------------------------------------------------------------------------------

define typed? (value)
    function i1 Value
    ret
        type? (at value)

define typed-type (value)
    function Value Value
    assert
        typed? value
        value
    ret
        at value

define typed-expression (value)
    function Value Value
    assert
        typed? value
    ret
        next
            at value

define typed-abstract? (value)
    function i1 Value
    ret
        null? (typed-expression value)

define typed-error? (value)
    function i1 Value
    ret
        value==
            typed-type value
            load error-type

define typed (type-expr value-expr)
    function Value Value Value
    assert
        icmp != type-expr (null Value)
    assert
        type? type-expr
    ret
        ref
            set-next type-expr value-expr

################################################################################
# build and install the preprocessor hook function.

deftype &opaque (pointer opaque)

deftype MacroFunction
    function Value Value Value

deftype TypeMacroFunction
    # result self value env
    function Value Value Value Value

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
            handle-value
                get-key env key-ir-env
            Environment

define get-symbols (env)
    function Value Value
    ret
        get-key env key-symbols

define new-env (meta-env)
    function Value Value
    defvalue obj
        new-table;
    defvalue symtable
        new-table;
    set-key! obj key-symbols symtable
    set-key! obj key-env-meta meta-env
    ret obj

define get-symbol (env name)
    function Value Value Value
    ret
        get-key
            get-symbols env
            name

define resolve-symbol (env name)
    function Value Value Value
    defvalue result
        get-key
            get-symbols env
            name
    ret
        ?
            icmp != result (null Value)
            result
            splice
                defvalue meta-env
                    get-key env key-env-meta
                ?
                    icmp == meta-env (null Value)
                    null Value
                    resolve-symbol meta-env name


define set-symbol (env name value)
    function void Value Value Value
    set-key!
        get-symbols env
        name
        value
    ret;

declare expand-expression
    MacroFunction

# list is mapped chain-aware
define map (value ctx f)
    function Value Value Value (pointer MacroFunction)
    ret
        ? (icmp == value (null Value))
            null Value
            splice
                defvalue expanded-value
                    f value ctx
                ?
                    null? expanded-value
                    # skip
                    map
                        next value
                        \ ctx f
                    set-next
                        expanded-value
                        map
                            next expanded-value
                            \ ctx f

defvalue key-result-type
    quote "result-type"

# stores type of last element in env
define expand-untype-expression (value env)
    MacroFunction
    defvalue expanded-value
        expand-expression value env
    defvalue expanded-type
        typed-type expanded-value
    set-key! env key-result-type expanded-type
    ret
        replace value
            typed-expression expanded-value

# only returns types
define expand-type-expression (value env)
    MacroFunction
    defvalue expanded-value
        expand-expression value env
    assert
        null? (typed-expression expanded-value)
    ret
        replace value
            typed-type expanded-value

defvalue ellipsis
    quote ...

define cast-untype-parameter (value env)
    MacroFunction
    defvalue src-tuple
        next
            defvalue dest-types value
    defvalue dest-type
        at dest-types
    defvalue ellipsis?
        value== dest-type (load ellipsis-type)
    # if ellipsis, keep, otherwise iterate to next type
    defvalue next-dest-type
        select ellipsis?
            dest-type
            next dest-type
    defvalue src-value
        typed-expression src-tuple
    defvalue src-type
        typed-type src-tuple
    defvalue casted-src-value
        ?
            or?
                value== src-type dest-type
                ellipsis?
            src-value
            qquote
                bitcast
                    unquote src-value
                    unquote
                        type-ir-repr dest-type
    defvalue next-src-tuple
        next src-tuple
    ret
        set-next casted-src-value
            select
                icmp == next-src-tuple (null Value)
                null Value
                set-next!
                    ref next-dest-type
                    next-src-tuple

define extract-type-map-parameters (value env)
    MacroFunction
    defvalue input-type
        typed-type
            defvalue input-expr
                next
                    defvalue input-names value
    defvalue input-name
        at input-names
    set-symbol env input-name
        typed input-type input-name
    defvalue next-input-name
        next input-name
    #print input-name input-type
    ret
        set-next
            input-type
            select
                icmp == next-input-name (null Value)
                null Value
                set-next!
                    ref next-input-name
                    next input-expr

defvalue function-apply
    define "" (head params env)
        TypeMacroFunction
        defvalue head-type
            typed-type head
        defvalue funcname
            typed-expression head
        defvalue funcrettype
            get-key head-type KEY_RETURN_TYPE
        defvalue funcparamtypes
            get-key head-type KEY_PARAMETER_TYPES
        # casted params
        defvalue castedparams
            map
                # prepend types to params so mapping
                # function can read them
                set-next
                    ref funcparamtypes
                    params
                env
                cast-untype-parameter
        ret
            typed
                funcrettype
                qquote
                    call
                        unquote funcname
                        unquote-splice castedparams

define expand-call (resolved-head value env)
    function Value Value Value Value
    defvalue sym-type
        typed-type resolved-head
    assert
        type? sym-type
    ret
        ?
            null?
                defvalue callf
                    bitcast
                        handle-value
                            get-key sym-type KEY_APPLY
                        pointer TypeMacroFunction
            typed
                load error-type
                qquote
                    error "can not apply first expression argument"
                        unquote value
            splice
                # expand params to retrieve final types
                defvalue params
                    map
                        next
                            at value
                        env
                        expand-expression
                replace value
                    callf resolved-head params env

        /// if
            function-type? sym-type;
                defvalue funcname
                    typed-expression resolved-head
                defvalue funcparamtypes
                    param-types sym-type
                # expand params to retrieve final types
                defvalue params
                    map
                        next
                            at value
                        env
                        expand-expression
                # casted params
                defvalue castedparams
                    map
                        # prepend types to params so mapping
                        # function can read them
                        set-next
                            ref funcparamtypes
                            params
                        env
                        cast-untype-parameter
                replace value
                    typed
                        return-type sym-type
                        qquote
                            call
                                unquote funcname
                                unquote-splice castedparams
            type? sym-type;
                defvalue typedef
                    typed-expression resolved-head
                if
                    typeclass? typedef;
                        defvalue obj
                            typeclass-table typedef
                        defvalue type-params
                            get-key obj key-definition
                        defvalue body
                            next type-params
                        # expand params to retrieve final types
                        defvalue params
                            map
                                next
                                    at value
                                env
                                expand-expression
                        defvalue subenv
                            new-env env
                        defvalue paramtypes
                            map
                                set-next
                                    type-params
                                    params
                                subenv
                                extract-type-map-parameters
                        defvalue untyped-params
                            map params env
                                expand-untype-expression
                        defvalue expanded-body
                            map body subenv
                                expand-untype-expression
                        defvalue return-type
                            get-key subenv key-result-type
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
                        replace value
                            typed
                                return-type
                                qquote
                                    call
                                        unquote function-decl
                                        unquote-splice untyped-params

                    else
                        typed
                            load error-type
                            qquote
                                error "can not instantiate type"
                                    unquote value
            else
                typed
                    load error-type
                    qquote
                        error "symbol is not a function or macro"
                            unquote value

define expand-expression (value env)
    MacroFunction
    ret
        ?
            atom? value
            splice
                if
                    symbol? value;
                        defvalue resolved-sym
                            resolve-symbol env value
                        ?
                            icmp == resolved-sym (null Value)
                            typed
                                load error-type
                                qquote
                                    error "unknown symbol"
                                        unquote value
                            replace value resolved-sym
                    string? value;
                        replace value
                            typed
                                pointer-type
                                    array-type
                                        load int8-type
                                        string-size value
                                qquote
                                    global ""
                                        unquote value
                    integer? value;
                        replace value
                            typed
                                load int32-type
                                value
                    real? value;
                        replace value
                            typed
                                load float-type
                                value
                    else
                        typed
                            load error-type
                            qquote
                                error "invalid atom"
                                    unquote value
            splice
                defvalue head
                    at value
                ?
                    typed? value
                    value
                    splice
                        # resolve first argument
                        defvalue resolved-head
                            expand-expression head env
                        if
                            # macro?
                            handle? resolved-head;
                                expand-expression
                                    call
                                        bitcast
                                            handle-value resolved-head
                                            pointer MacroFunction
                                        value
                                        env
                                    env
                            # function call
                            else
                                expand-call resolved-head value env

global global-env
    null Value

define set-global (key value)
    function void Value Value
    set-symbol
        load global-env
        key
        value
    ret;

define set-global-syntax (head handler)
    function void Value (pointer MacroFunction)
    set-global
        head
        handle handler
    ret;

# the top level expression goes through the preprocessor, which then descends
# the expression tree and translates it to bangra IR.
define global-preprocessor (ir-env value)
    preprocessor-func
    set-key!
        load global-env
        key-ir-env
        new-handle
            bitcast
                ir-env
                pointer opaque

    defvalue result
        map
            next
                at value
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
                    execute ::ret::

define macro-bangra (ir-env value)
    preprocessor-func
    set-key!
        load global-env
        key-ir-env
        new-handle
            bitcast
                ir-env
                pointer opaque

    defvalue result
        map
            next value
            load global-env
            expand-untype-expression

    ret
        qquote
            run
                unquote-splice result

# INITIALIZATION
#-------------------------------------------------------------------------------

# install bangra preprocessor
run
    init-global-types;

    set-key!
        load function-typeclass
        KEY_APPLY
        handle function-apply

    store
        new-env
            null Value
        global-env

    set-global
        quote type
        typed
            load type-type
            null Value

    set-global
        quote int
        typed
            load int32-type
            null Value

    set-global
        quote rawstring
        typed
            load rawstring-type
            null Value

    set-global
        quote ...
        typed
            load ellipsis-type
            null Value

    set-global-syntax
        quote <-
        define "" (value env)
            MacroFunction
            defvalue arg-params
                next
                    defvalue arg-rettype
                        next
                            at value
            defvalue rettype
                typed-type
                    expand-expression arg-rettype env
            defvalue params
                map
                    at arg-params
                    env
                    expand-type-expression
            ret
                replace value
                    typed
                        function-type rettype params
                        null Value

    set-global-syntax
        quote function
        define "" (value env)
            MacroFunction
            defvalue body
                next
                    defvalue params
                        next
                            at value
            ret
                replace value
                    typed
                        template-type params body
                        null Value

    set-global-syntax
        quote extern-C
        define "" (value env)
            MacroFunction
            defvalue namestr
                next
                    at value
            assert
                string? namestr
            defvalue expand-expr
                expand-expression
                    next namestr
                    env
            ret
                ?
                    typed-error? expand-expr
                    expand-expr
                    splice
                        defvalue type
                            typed-type expand-expr
                        replace value
                            typed
                                type
                                qquote
                                    declare
                                        unquote namestr
                                        unquote
                                            type-ir-repr type

    set-global-syntax
        quote let
        define "" (value env)
            MacroFunction
            defvalue expr
                expand-expression
                    next
                        defvalue name
                            next
                                at value
                    env
            ret
                ?
                    typed-abstract? expr
                    splice
                        set-symbol env name expr
                        replace value expr
                    splice
                        defvalue expr-type
                            typed-type expr
                        set-symbol env name
                            typed
                                expr-type
                                name
                        replace value
                            typed
                                expr-type
                                qquote
                                    defvalue
                                        unquote name
                                        unquote
                                            typed-expression expr

    set-preprocessor
        &str "bangra"
        global-preprocessor

    set-macro env
        &str "bangra"
        macro-bangra

