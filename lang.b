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

define single (value)
    function Value Value
    ret
        set-next value (null Value)

define typed? (value)
    function i1 Value
    ret
        value== (at value) (quote :)

define get-expr (value)
    function Value Value
    ret
        next
            at value

define get-type (value)
    function Value Value
    ret
        next
            next
                at value

define error? (value)
    function i1 Value
    ret
        value==
            get-type value
            quote error

define typed (value-expr type-expr)
    function Value Value Value
    ret
        ref
            set-next (quote :)
                set-next value-expr
                    clear-next type-expr

define type? (value)
    function i1 Value
    ret
        value== value
            quote type

define typeclass? (value)
    function i1 Value
    ret
        value==
            at value
            quote typeclass

define typeclass-name (value)
    function Value Value
    ret
        next
            at value

define typeclass-table (value)
    function Value Value
    ret
        next
            next
                at value

define function-type? (value)
    function i1 Value
    ret
        value==
            at value
            quote function

define pointer-type? (value)
    function i1 Value
    ret
        value==
            at value
            quote pointer

define element-type (value)
    function Value Value
    ret
        next
            at value

define return-type (value)
    function Value Value
    ret
        next
            at value

define param-types (value)
    function Value Value
    ret
        next
            next
                at value

define replace (fromvalue tovalue)
    function Value Value Value
    ret
        set-next tovalue
            next fromvalue


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
                    next expanded-expr
                    defvalue expanded-expr
                        f expr ctx
                    defvalue @prev-expr
                        load prev-expr
                    ? (icmp == @prev-expr (null Value))
                        splice
                            store expanded-expr head-expr
                            false
                        splice
                            set-next!
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
        expand-expression value env
    defvalue result
        ? (typed? expanded-value)
            splice
                defvalue expanded-type
                    get-type expanded-value
                set-key! env key-result-type expanded-type
                replace value
                    get-expr expanded-value
            expanded-value
    ret result

defvalue ellipsis
    quote ...

define type== (value1 value2)
    function i1 Value Value
    ret
        if
            value== value1 ellipsis;
                true
            value== value2 ellipsis;
                true
            or (pointer? value1) (pointer? value2);
                type==
                    at value1
                    at value2
            value== value1 value2;
                ?
                    and
                        icmp == value1 (null Value)
                        icmp == value2 (null Value)
                    true
                    type==
                        next value1
                        next value2
            else false

define cast-untype-parameter (value env)
    MacroFunction
    defvalue src-tuple
        next
            defvalue dest-types value
    defvalue dest-type
        at dest-types
    # if ellipsis, keep, otherwise iterate to next type
    defvalue next-dest-type
        select
            value== dest-type (quote ...)
            dest-type
            next dest-type
    defvalue src-value
        get-expr src-tuple
    defvalue src-type
        get-type src-tuple
    defvalue casted-src-value
        ?
            type== src-type dest-type
            src-value
            qquote
                bitcast
                    unquote src-value
                    unquote dest-type
    ret
        set-next casted-src-value
            select
                icmp == next-dest-type (null Value)
                null Value
                set-next!
                    ref next-dest-type
                    next src-tuple

define extract-type-map-parameters (value env)
    MacroFunction
    defvalue input-type
        get-type
            defvalue input-expr
                next
                    defvalue input-names value
    defvalue input-name
        at input-names
    set-symbol env input-name
        typed input-name input-type
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

define expand-call (resolved-head value env)
    function Value Value Value Value
    defvalue sym-type
        get-type resolved-head
    ret
        if
            function-type? sym-type;
                defvalue funcname
                    get-expr resolved-head
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
                        qquote
                            call
                                unquote funcname
                                unquote-splice castedparams
                        return-type sym-type
            type? sym-type;
                defvalue typedef
                    get-expr resolved-head
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
            atom? value
            splice
                if
                    symbol? value;
                        defvalue resolved-sym
                            resolve-symbol env value
                        ?
                            icmp == resolved-sym (null Value)
                            qquote
                                :
                                    error "unknown symbol"
                                        unquote value
                                    error
                            replace value resolved-sym
                    string? value;
                        replace value
                            qquote
                                :
                                    global ""
                                        unquote value
                                    pointer
                                        array i8
                                            unquote
                                                call new-integer
                                                    call string-size value
                    integer? value;
                        replace value
                            qquote
                                :
                                    unquote value
                                    i32
                    real? value;
                        replace value
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
global type-type
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
        new-handle
            bitcast
                handler
                pointer opaque
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

# TYPE CONSTRUCTORS
#-------------------------------------------------------------------------------

global type-cache
    null Value

# all types
defvalue KEY_REPR
    quote "repr"
defvalue KEY_IR_REPR
    quote "IR-repr"

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

defvalue type-repr
    define "" (value)
        function Value Value
        defvalue result
            get-key value KEY_REPR
        ret
            select (icmp == result (null Value))
                value
                result

defvalue type-ir-repr
    define "" (value)
        function Value Value
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

global boolean-type
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

defvalue init-global-types
    define "" ()
        function void

        store (new-table) type-cache

        store (int-type 8 true) int8-type
        store (int-type 16 true) int16-type
        store (int-type 32 true) int32-type
        store (int-type 64 true) int64-type

        store (int-type 1 false) boolean-type

        store (int-type 8 false) uint8-type
        store (int-type 16 false) uint16-type
        store (int-type 32 false) uint32-type
        store (int-type 64 false) uint64-type

        store (pointer-type (load int8-type)) rawstring-type

        ret;

# INITIALIZATION
#-------------------------------------------------------------------------------

# install bangra preprocessor
run
    init-global-types;

    store
        new-env
            null Value
        global-env

    store
        new-table;
        type-type

    set-global
        quote type
        qquote
            :
                unquote
                    load type-type
                unquote
                    load type-type

    set-global
        quote int
        quote
            : i32 type
    set-global
        quote rawstring
        quote
            : rawstring type
    set-global
        quote ...
        quote
            : ... type

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
                expand-expression arg-rettype env
            defvalue params
                map
                    at arg-params
                    env
                    expand-untype-expression
            ret
                replace value
                    qquote
                        :
                            function
                                unquote
                                    get-expr rettype
                                unquote-splice params
                            type

    set-global-syntax
        quote function
        define "" (value env)
            MacroFunction
            defvalue params
                next
                    at value
            defvalue template
                new-table;
            set-key! template
                key-definition
                params
            ret
                replace value
                    qquote
                        :
                            typeclass template
                                unquote template
                            type

    set-global-syntax
        quote extern-C
        define "" (value env)
            MacroFunction
            defvalue namestr
                next
                    at value
            defvalue type
                expand-expression
                    next namestr
                    env
            defvalue type-expr
                get-expr type
            ret
                replace value
                    qquote
                        :
                            declare
                                unquote namestr
                                unquote type-expr
                            unquote type-expr

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
            defvalue expr-type
                get-type expr
            ret
                ?
                    type? expr-type
                    splice
                        set-symbol env name expr
                        replace value
                            qquote
                                :
                                    splice;
                                    void
                    splice
                        set-symbol env name
                            typed
                                name
                                expr-type
                        replace value
                            typed
                                qquote
                                    defvalue
                                        unquote name
                                        unquote
                                            get-expr expr
                                expr-type

    set-preprocessor
        &str "bangra"
        global-preprocessor

    set-macro env
        &str "bangra"
        macro-bangra

