# boot script
# the bangra executable looks for a boot file at
# path/to/executable.b and, if found, executes it.

syntax-extend stage-1 (_ scope)
    table
        tupleof scope-parent-symbol scope
        tupleof
            quote symbol?
            continuation symbol? (_ x)
                == (typeof x) symbol
        tupleof
            quote list?
            continuation list? (_ x)
                == (typeof x) list
        tupleof
            quote none?
            continuation none? (_ x)
                == x none
        tupleof
            quote empty?
            continuation empty? (_ x)
                branch
                    == (typeof x) list
                    continuation ()
                        == x (list)
                    continuation () false
        tupleof
            quote key?
            continuation key? (_ x y)
                != (@ x y) none
        tupleof
            quote load
            continuation load (_ path)
                eval
                    list-load path
                    globals;
                    path
        tupleof
            quote API
            import-c
                # todo: search path & embedded resources
                .. interpreter-dir "/bangra.h"
                tupleof;
        tupleof
            quote syntax-single-macro
            continuation syntax-single-macro (_ f)
                syntax-macro
                    continuation (_ scope expr)
                        cons
                            f scope
                                @ expr 0
                            @ expr 1
        tupleof
            quote call
            syntax-macro
                continuation call (_ scope expr)
                    cons
                        @ expr 0 1
                        @ expr 1
        tupleof
            quote dump-syntax
            syntax-macro
                continuation dump-syntax (_ scope expr)
                    call
                        continuation (_ e)
                            dump
                                @ e 0 0
                            cons
                                escape
                                    @ e 0 0
                                @ e 1
                        expand scope
                            cons
                                @ expr 0 1
                                @ expr 1
        tupleof
            # a lofi version of let so we get some sugar early
            quote let
            syntax-macro
                continuation syntax-macro (_ scope expr)
                    call
                        continuation (_ param-name)
                            call
                                continuation (_ param scope-name)
                                    list
                                        list syntax-extend (list (parameter (quote _)) scope-name)
                                            list table
                                                list tupleof (list quote scope-parent-symbol) scope-name
                                                list tupleof (list quote param-name) param
                                        cons
                                            cons continuation
                                                cons (list (parameter (quote _)) param)
                                                    @ expr 1
                                            @ expr 0 2
                                parameter param-name
                                quote scope
                        @ expr 0 1 0
        tupleof
            quote ?
            syntax-macro
                continuation ? (_ scope expr)
                    cons
                        list branch
                            @ expr 0 1 0
                            list continuation (list)
                                @ expr 0 2 0
                            list continuation (list)
                                @ expr 0 3 0
                        @ expr 1
        tupleof
            quote :
            syntax-macro
                continuation : (_ scope expr)
                    cons
                        cons tupleof
                            cons
                                list quote
                                    @ expr 0 1 0
                                branch
                                    == (@ expr 0 2) (list)
                                    continuation ()
                                        list
                                            @ expr 0 1 0
                                    continuation ()
                                        @ expr 0 2
                        @ expr 1

syntax-extend stage-2 (_ scope)
    let list-join
        continuation list-join (_ a b)
            ? (empty? a) b
                cons
                    @ a 0
                    list-join
                        @ a 1
                        b
    let list-head?
        continuation list-head? (_ expr name)
            ? (list? expr)
                do
                    let head (@ expr 0)
                    ? (symbol? head)
                        == head name
                        false
                false
    table
        tupleof scope-parent-symbol scope
        : bool (integer 1 false)
        : int8 (integer 8 true)
        : int16 (integer 16 true)
        : int32 (integer 32 true)
        : int64 (integer 64 true)
        : uint8 (integer 8 false)
        : uint16 (integer 16 false)
        : uint32 (integer 32 false)
        : uint64 (integer 64 false)

        : int (integer 32 true)
        : uint (integer 32 false)

        : real16 (real 16)
        : real32 (real 32)
        : real64 (real 64)

        : half (real 16)
        : float (real 32)
        : double (real 64)

        : list-join
        : list-head?
        : list-atom?
            continuation list-atom? (_ x)
                ? (list? x)
                    empty? x
                    true
        : assert # (assert bool-expr [error-message])
            syntax-single-macro
                continuation assert (_ scope expr)
                    list ? (@ expr 1 0) true
                        list error
                            ? (empty? (@ expr 2)) "assertion failed"
                                @ expr 2 0
        : ::@
            syntax-macro
                continuation ::@ (_ scope expr)
                    cons
                        list-join
                            @ expr 0 1
                            list
                                @ expr 1 0
                        @ expr 2
        : ::*
            syntax-macro
                continuation ::* (_ scope expr)
                    list
                        list-join
                            @ expr 0 1
                            @ expr 1
        : .
            syntax-single-macro
                continuation . (_ scope expr)
                    let key
                        @ expr 2 0
                    ? (symbol? key)
                        list
                            (do @)
                            @ expr 1 0
                            list quote key
                        error "symbol expected"

        : function # (function [name] (param ...) body ...)
            syntax-macro
                continuation function (_ scope expr)
                    let decl
                        (@ expr 0 1 0)
                    let retparam
                        quote return
                    ? (symbol? decl)
                        cons
                            list let decl
                                cons continuation
                                    cons
                                        @ expr 0 1 0
                                        cons
                                            cons
                                                retparam
                                                @ expr 0 2 0
                                            @ expr 0 3
                            ? (empty? (@ expr 1))
                                list decl
                                @ expr 1
                        cons
                            cons continuation
                                cons
                                    cons
                                        retparam
                                        @ expr 0 1 0
                                    @ expr 0 2
                            @ expr 1
        : and
            syntax-single-macro
                continuation and (_ scope expr)
                    let tmp
                        parameter
                            quote tmp
                    list
                        list continuation (list (parameter (quote _)) tmp)
                            list branch tmp
                                list continuation (list)
                                    @ expr 2 0
                                list continuation (list) tmp
                        @ expr 1 0
        : or
            syntax-single-macro
                continuation or (_ scope expr)
                    let tmp
                        parameter
                            quote tmp
                    list
                        list continuation (list (parameter (quote _)) tmp)
                            list branch tmp
                                list continuation (list) tmp
                                list continuation (list)
                                    @ expr 2 0
                        @ expr 1 0
        : loop
            syntax-single-macro
                continuation loop (_ scope expr)
                    let param-repeat
                        quote repeat
                    list do
                        list let param-repeat
                            cons continuation
                                cons
                                    cons
                                        parameter (quote _)
                                        @ expr 1 0
                                    @ expr 2
                        cons param-repeat
                            @ expr 1 0
        : if
            do
                let if-rec
                    continuation if (_ scope expr)
                        let next-expr
                            @ expr 1 0
                        ? (list-head? next-expr (quote elseif))
                            do
                                let nextif
                                    if-rec scope
                                        @ expr 1
                                cons
                                    list branch
                                        @ expr 0 1 0
                                        cons continuation
                                            cons (list)
                                                @ expr 0 2
                                        list continuation (list)
                                            @ nextif 0
                                    @ nextif 1
                            ? (list-head? next-expr (quote else))
                                cons
                                    list branch
                                        @ expr 0 1 0
                                        cons continuation
                                            cons (list)
                                                @ expr 0 2
                                        cons continuation
                                            cons (list)
                                                @ expr 1 0 1
                                    @ expr 2
                                do
                                    cons
                                        list branch
                                            @ expr 0 1 0
                                            cons continuation
                                                cons (list)
                                                    @ expr 0 2
                                            list continuation (list)
                                        @ expr 1
                syntax-macro if-rec
        : syntax-infix-rules
            continuation syntax-infix-rules (_ prec order name)
                structof
                    tupleof (quote prec) prec
                    tupleof (quote order) order
                    tupleof (quote name) name
        : syntax-infix-op
            syntax-single-macro
                continuation syntax-infix-op (_ scope expr)
                    list tupleof
                        list quote
                            symbol
                                .. "#ifx:" (string (@ expr 1 0))
                        @ expr 2 0

syntax-extend stage-3 (_ scope)

    function unwrap-single (expr)
        # unwrap single item from list or prepend 'do' clause to list
        ? (empty? (@ expr 1))
            @ expr 0
            cons do expr

    function fold (it init f)
        let next
            @ it 0
        let st
            next (@ it 1)
        let out init
        loop (out st)
            if (none? st) out
            else
                repeat
                    f out (@ st 0)
                    next (@ st 1)

    function iter (s)
        let ls
            length s
        tupleof
            function (i)
                if (< i ls)
                    tupleof (@ s i) (+ i 1)
                else none
            0

    function iter-r (s)
        tupleof
            function (i)
                if (> i 0)
                    let k (- i 1)
                    tupleof (@ s k) k
                else none
            length s

    function get-scope-symbol (scope key)
        loop (scope)
            let result
                @ scope key
            if (none? result)
                let parent
                    @ scope scope-parent-symbol
                if (none? parent) none
                else
                    repeat parent
            else result

    function get-ifx-op (scope op)
        let key
            symbol
                .. "#ifx:" (string op)
        ? (symbol? op)
            get-scope-symbol scope key
            none
    function has-infix-ops (infix-table expr)
        # any expression whose second argument matches an infix operator
        # is treated as an infix expression.
        and
            not
                or
                    empty? (@ expr 1)
                    empty? (@ expr 2)
            != (get-ifx-op infix-table (@ expr 1 0)) none

    function infix-op (infix-table token prec pred)
        let op
            get-ifx-op infix-table token
        if (none? op)
            error
                .. (string token)
                    " is not an infix operator, but embedded in an infix expression"
        elseif (pred (. op prec) prec)
            op
        else none

    function rtl-infix-op (infix-table token prec pred)
        let op
            get-ifx-op infix-table token
        if (none? op)
            error
                .. (string token)
                    " is not an infix operator, but embedded in an infix expression"
        elseif
            and
                == (. op order) <
                pred (. op prec) prec
            op
        else none

    function parse-infix-expr (infix-table lhs state mprec)
        loop (lhs state)
            let la (@ state 0)
            if (empty? la)
                tupleof lhs state
            else
                let op
                    infix-op infix-table la mprec >=
                if (none? op)
                    tupleof lhs state
                else
                    let next-state (@ state 1)
                    let rhs (@ next-state 0)
                    let state (@ next-state 1)
                    let rhs-state
                        loop (rhs state)
                            let ra
                                @ state 0
                            if (empty? ra)
                                tupleof rhs state
                            else
                                let lop
                                    infix-op infix-table ra (. op prec) >
                                let nextop
                                    ? (none? lop)
                                        rtl-infix-op infix-table ra (. op prec) ==
                                        lop
                                if (none? nextop)
                                    tupleof rhs state
                                else
                                    let rhs-state
                                        parse-infix-expr
                                            infix-table
                                            rhs
                                            state
                                            . nextop prec
                                    repeat
                                        @ rhs-state 0
                                        @ rhs-state 1
                    repeat
                        list (. op name) lhs
                            @ rhs-state 0
                        @ rhs-state 1

    let bangra
        table
            : path
                list
                    "./?.b"
                    .. interpreter-dir "/?.b"
            : modules
                table;
    function make-module-path (pattern name)
        fold (iter pattern) ""
            function (out val)
                list out val
                    .. out val
                .. out
                    ? (== val "?") name val

    function find-module (name)
        assert (symbol? name)
            "module name must be symbol"
        let content
            @ (. bangra modules) name
        if (none? content)
            let namestr
                string name
            let pattern
                @ bangra
                    quote path
            loop (pattern)
                if (not (empty? pattern))
                    let module-path
                        make-module-path
                            @ pattern 0
                            namestr
                    let expr
                        list-load module-path
                    if (not (none? expr))
                        let fn
                            eval expr
                                table
                                    tupleof scope-parent-symbol
                                        globals;
                                    : module-path
                                module-path
                        let content
                            fn;
                        set-key! (. bangra modules)
                            tupleof name content
                        content
                    else
                        repeat
                            @ pattern 1
                else
                    error
                        .. "module not found: " namestr
        else
            content

    function make-expand-multi-op-ltr (op)
        # (op a b c ...) -> (op (op (op a b) c) ...)
        syntax-single-macro
            function (scope expr)
                let tail
                    @ expr 1
                loop (tail)
                    let rest
                        @ tail 2
                    if (empty? rest)
                        cons op tail
                    else
                        repeat
                            cons
                                list op
                                    @ tail 0
                                    @ tail 1 0
                                rest

    function symbol-or-parameter (x)
        let t (typeof x)
        (t == symbol) or (t == parameter)

    function xpcall (func xfunc)
        let old_handler
            get-exception-handler;
        function cleanup ()
            set-exception-handler! old_handler
        function try ()
            let finally return
            function except (exc aframe args)
                cleanup;
                xfunc exc
                finally;
            set-exception-handler! except
            flowcall func
            cleanup;
        try;

    table
        tupleof scope-parent-symbol scope
        : xpcall
        : fold
        : iter
        : bangra
        : require find-module
        : and
            make-expand-multi-op-ltr and
        : or
            make-expand-multi-op-ltr or
        : try
            syntax-macro
                function (scope expr)
                    if (not (list-head? (@ expr 1 0) (quote except)))
                        error "except block missing"
                    cons
                        list xpcall
                            cons continuation
                                cons (list)
                                    @ expr 0 1
                            cons continuation
                                cons
                                    cons (parameter (quote _))
                                        @ expr 1 0 1 0
                                    @ expr 1 0 2
                        @ expr 2

        # quasiquote support
        # (qquote expr [...])
        : qquote
            do
                function qquote-1 (x)
                    if (list-atom? x)
                        list quote x
                    elseif (list-head? x (quote unquote))
                        unwrap-single (@ x 1)
                    elseif (list-head? x (quote qquote))
                        qquote-1 (qquote-1 (@ x 1 0))
                    elseif (list-atom? (@ x 0))
                        list cons
                            qquote-1 (@ x 0)
                            qquote-1 (@ x 1)
                    elseif (list-head? (@ x 0) (quote unquote-splice))
                        list list-join
                            unwrap-single (@ x 0 1)
                            qquote-1 (@ x 1)
                    else
                        list cons
                            qquote-1 (@ x 0)
                            qquote-1 (@ x 1)
                syntax-macro
                    function (scope expr)
                        cons
                            ? (empty? (@ expr 0 2))
                                qquote-1 (@ expr 0 1 0)
                                qquote-1 (@ expr 0 1)
                            @ expr 1
        : let
            # support for multiple declarations in one let scope
            syntax-macro
                function (scope expr)
                    let args (@ expr 0 1)
                    let argtype (typeof (@ args 0))
                    if (== argtype symbol)
                        if (empty? (@ args 2))
                            # regular form with support for recursion
                            cons
                                cons let args
                                @ expr 1
                        else
                            # unpacking multiple variables, without recursion

                            # iterate until we hit the last cell,
                            # which is the body
                            function find-body (expr)
                                if (not (empty? (@ expr 1)))
                                    let out
                                        find-body (@ expr 1)
                                    tupleof
                                        cons (@ expr 0) (@ out 0)
                                        @ out 1
                                else
                                    tupleof (list)
                                        @ expr 0
                            let cells
                                find-body args
                            list
                                list
                                    cons continuation
                                        cons
                                            cons (parameter (quote _)) (@ cells 0)
                                            @ expr 1
                                    list splice
                                        @ cells 1

                    elseif (== argtype parameter)
                        # regular form, hidden parameter
                        cons
                            cons continuation
                                cons
                                    list
                                        parameter (quote _)
                                        @ args 0
                                    @ expr 1
                            @ args 1
                    else
                        # multiple variables with support for recursion,
                        # but no cross dependency

                        # prepare quotable values from declarations
                        function handle-pairs (pairs)
                            if (empty? pairs)
                                tupleof
                                    list;
                                    list;
                                    list;
                            else
                                let pair
                                    @ pairs 0
                                let name
                                    @ pair 0
                                let value
                                    @ pair 1 0
                                let param
                                    parameter name
                                let cells
                                    handle-pairs
                                        @ pairs 1
                                tupleof
                                    cons
                                        list tupleof (list quote name) param
                                        @ cells 0
                                    cons param (@ cells 1)
                                    cons value (@ cells 2)

                        let cells
                            handle-pairs args
                        let scope-name
                            quote scope
                        list
                            list syntax-extend (list (parameter (quote _)) scope-name)
                                cons table
                                    cons
                                        list tupleof (list quote scope-parent-symbol) scope-name
                                        @ cells 0
                            cons
                                cons continuation
                                    cons
                                        cons
                                            parameter (quote _)
                                            @ cells 1
                                        @ expr 1
                                @ cells 2

        tupleof scope-list-wildcard-symbol
            function (scope topexpr)
                let expr
                    @ topexpr 0
                let head
                    @ expr 0
                let headstr
                    string head
                # method call syntax
                if
                    and
                        symbol? head
                        and
                            none? (get-scope-symbol scope head)
                            == (slice headstr 0 1) "."

                    let name
                        symbol
                            slice headstr 1
                    let self-arg
                        @ expr 1 0
                    let rest
                        @ expr 2
                    let self
                        parameter
                            quote self
                    cons
                        list
                            list continuation (list (parameter (quote _)) self)
                                cons
                                    list (do @) self
                                        list quote name
                                    cons self rest
                            self-arg
                        @ topexpr 1
                # infix operator support
                elseif (has-infix-ops scope expr)
                    cons
                        @
                            parse-infix-expr scope
                                \ (@ expr 0) (@ expr 1) 0
                            0
                        @ topexpr 1
        tupleof scope-symbol-wildcard-symbol
            function (scope topexpr)
                let sym
                    @ topexpr 0
                let it
                    iter-r
                        string sym
                function finalize-head (out)
                    cons
                        symbol
                            @ out 0
                        @ out 1
                # return tokenized list if string contains a dot
                # and it's not the concat operator
                if
                    and
                        none? (get-scope-symbol scope sym)
                        fold it false
                            function (out k)
                                if (== k ".") true
                                else out
                    cons
                        finalize-head
                            fold it (list "")
                                function (out k)
                                    if (== k ".")
                                        cons ""
                                            cons
                                                quote .
                                                finalize-head out
                                    else
                                        cons
                                            .. k (@ out 0)
                                            @ out 1
                        @ topexpr 1

        syntax-infix-op : (syntax-infix-rules 70 > :)
        syntax-infix-op or (syntax-infix-rules 100 > or)
        syntax-infix-op and (syntax-infix-rules 200 > and)
        syntax-infix-op | (syntax-infix-rules 240 > |)
        syntax-infix-op ^ (syntax-infix-rules 250 > ^)
        syntax-infix-op & (syntax-infix-rules 260 > &)
        syntax-infix-op < (syntax-infix-rules 300 > <)
        syntax-infix-op > (syntax-infix-rules 300 > >)
        syntax-infix-op <= (syntax-infix-rules 300 > <=)
        syntax-infix-op >= (syntax-infix-rules 300 > >=)
        syntax-infix-op != (syntax-infix-rules 300 > !=)
        syntax-infix-op == (syntax-infix-rules 300 > ==)
        #syntax-infix-op is (syntax-infix-rules 300 > is)
        syntax-infix-op .. (syntax-infix-rules 400 < ..)
        #syntax-infix-op << (syntax-infix-rules 450 > <<)
        #syntax-infix-op >> (syntax-infix-rules 450 > >>)
        syntax-infix-op - (syntax-infix-rules 500 > -)
        syntax-infix-op + (syntax-infix-rules 500 > +)
        syntax-infix-op % (syntax-infix-rules 600 > &)
        syntax-infix-op / (syntax-infix-rules 600 > /)
        #syntax-infix-op // (syntax-infix-rules 600 > //)
        syntax-infix-op * (syntax-infix-rules 600 > *)
        #syntax-infix-op ** (syntax-infix-rules 700 < **)
        syntax-infix-op . (syntax-infix-rules 800 > .)
        syntax-infix-op @ (syntax-infix-rules 800 > @)
        #syntax-infix-op .= (syntax-infix-rules 800 > .=)
        #syntax-infix-op @= (syntax-infix-rules 800 > @=)
        #syntax-infix-op =@ (syntax-infix-rules 800 > =@)

syntax-extend stage-final (_ scope)
    set-globals! scope
    scope

none
