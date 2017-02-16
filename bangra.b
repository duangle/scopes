# boot script
# the bangra executable looks for a boot file at
# path/to/executable.b and, if found, executes it.

syntax-extend stage-0 (_ scope)
    set-key! scope
        symbol "quote"
        block-scope-macro
            continuation quote (_ expr scope)
                call
                    continuation (_ args)
                        tupleof
                            cons
                                # stop compiler expansion
                                escape
                                    # stop macro expansion
                                    escape
                                        # keep wrapped in list
                                        # if multiple arguments
                                        branch
                                            == (@ args 1) (list)
                                            continuation ()
                                                @ args 0
                                            continuation ()
                                                args
                                @ expr 1
                            scope
                    @ expr 0 1
    scope

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
            quote macro
            continuation macro (_ f)
                block-scope-macro
                    continuation (_ expr scope)
                        tupleof
                            cons
                                f (@ expr 0) scope
                                @ expr 1
                            scope
        tupleof
            quote block-macro
            continuation macro (_ f)
                block-scope-macro
                    continuation (_ expr scope)
                        tupleof
                            f expr scope
                            scope
        /// tupleof
            quote call
            block-scope-macro
                continuation call (_ expr scope)
                    tupleof
                        cons
                            @ expr 0 1
                            @ expr 1
                        scope
        tupleof
            quote dump-syntax
            block-scope-macro
                continuation dump-syntax (_ expr scope)
                    tupleof
                        call
                            continuation (_ e)
                                dump
                                    @ e 0 0
                                cons
                                    escape
                                        @ e 0 0
                                    @ e 1
                            @
                                expand
                                    cons
                                        @ expr 0 1
                                        @ expr 1
                                    scope
                                0
                        scope
        tupleof
            # a lofi version of let so we get some sugar early
            quote let
            block-scope-macro
                continuation syntax-macro (_ expr scope)
                    branch
                        == (typeof (@ expr 0 2 0)) symbol
                        continuation () none
                        continuation ()
                            error "syntax: let <var> = <expr>"
                    branch
                        == (@ expr 0 2 0) (quote =)
                        continuation () none
                        continuation ()
                            error "syntax: let <var> = <expr>"
                    tupleof
                        call
                            continuation (_ param-name)
                                call
                                    continuation (_ param)
                                        # since the scope covers the remaining
                                        # block, we can mutate it directly
                                        set-key! scope param-name param
                                        list
                                            cons
                                                cons continuation
                                                    cons (list (parameter (quote _)) param)
                                                        @ expr 1
                                                @ expr 0 3
                                    parameter param-name
                            @ expr 0 1 0
                        scope
        tupleof
            quote ?
            block-scope-macro
                continuation ? (_ expr scope)
                    tupleof
                        cons
                            list branch
                                @ expr 0 1 0
                                list continuation (list)
                                    @ expr 0 2 0
                                list continuation (list)
                                    @ expr 0 3 0
                            @ expr 1
                        scope
        tupleof
            quote :
            block-scope-macro
                continuation : (_ expr scope)
                    tupleof
                        cons
                            cons tupleof
                                cons
                                    branch
                                        ==
                                            typeof
                                                @ expr 0 1 0
                                            symbol
                                        continuation ()
                                            list quote
                                                @ expr 0 1 0
                                        continuation ()
                                            @ expr 0 1 0
                                    branch
                                        == (@ expr 0 2) (list)
                                        continuation ()
                                            list
                                                @ expr 0 1 0
                                        continuation ()
                                            @ expr 0 2
                            @ expr 1
                        scope

syntax-extend stage-2 (_ scope)
    let list-join =
        continuation list-join (_ a b)
            ? (empty? a) b
                cons
                    @ a 0
                    list-join
                        @ a 1
                        b
    let list-head? =
        continuation list-head? (_ expr name)
            ? (list? expr)
                do
                    let head = (@ expr 0)
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
            macro
                continuation assert (_ expr)
                    list ? (@ expr 1 0) true
                        list error
                            ? (empty? (@ expr 2)) "assertion failed"
                                @ expr 2 0
        : ::@
            block-macro
                continuation ::@ (_ expr)
                    cons
                        list-join
                            @ expr 0 1
                            list
                                @ expr 1 0
                        @ expr 2
        : ::*
            block-macro
                continuation ::* (_ expr)
                    list
                        list-join
                            @ expr 0 1
                            @ expr 1
        : .
            macro
                continuation . (_ expr)
                    let key =
                        @ expr 2 0
                    ? (symbol? key)
                        list
                            (do @)
                            @ expr 1 0
                            list quote key
                        error "symbol expected"

        : function # (function [name] (param ...) body ...)
            block-macro
                continuation function (_ expr)
                    let decl =
                        (@ expr 0 1 0)
                    let retparam =
                        quote return
                    ? (symbol? decl)
                        cons
                            list let decl (quote =)
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
            macro
                continuation and (_ expr)
                    let tmp =
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
            macro
                continuation or (_ expr)
                    let tmp =
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
            macro
                continuation loop (_ expr)
                    let param-repeat =
                        quote repeat
                    list do
                        list let param-repeat (quote =)
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
                let if-rec =
                    continuation if (_ expr)
                        let next-expr =
                            @ expr 1 0
                        let cond =
                            @ expr 0 1 0
                        let then-exprlist =
                            @ expr 0 2
                        let make-branch =
                            continuation (_ else-exprlist)
                                list branch
                                    cond
                                    cons continuation
                                        cons (list) then-exprlist
                                    cons continuation
                                        cons (list) else-exprlist

                        ? (list-head? next-expr (quote elseif))
                            do
                                let nextif =
                                    if-rec
                                        @ expr 1
                                cons
                                    make-branch
                                        list (@ nextif 0)
                                    @ nextif 1
                            ? (list-head? next-expr (quote else))
                                cons
                                    make-branch
                                        @ expr 1 0 1
                                    @ expr 2
                                cons
                                    make-branch
                                        list;
                                    @ expr 1
                block-macro if-rec
        : syntax-infix-rules
            continuation syntax-infix-rules (_ prec order name)
                structof
                    tupleof (quote prec) prec
                    tupleof (quote order) order
                    tupleof (quote name) name
        : syntax-infix-op
            macro
                continuation syntax-infix-op (_ expr)
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
        let next =
            @ it 0
        let st =
            next (@ it 1)
        let out = init
        loop (out st)
            if (none? st) out
            else
                repeat
                    f out (@ st 0)
                    next (@ st 1)

    function iter (s)
        let ls =
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
                    let k = (- i 1)
                    tupleof (@ s k) k
                else none
            length s

    function get-scope-symbol (scope key)
        loop (scope)
            let result =
                @ scope key
            if (none? result)
                let parent =
                    @ scope scope-parent-symbol
                if (none? parent) none
                else
                    repeat parent
            else result

    function get-ifx-op (scope op)
        let key =
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
        let op =
            get-ifx-op infix-table token
        if (none? op)
            error
                .. (string token)
                    " is not an infix operator, but embedded in an infix expression"
        elseif (pred (. op prec) prec)
            op
        else none

    function rtl-infix-op (infix-table token prec pred)
        let op =
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
            let la = (@ state 0)
            if (empty? la)
                tupleof lhs state
            else
                let op =
                    infix-op infix-table la mprec >=
                if (none? op)
                    tupleof lhs state
                else
                    let next-state = (@ state 1)
                    let rhs = (@ next-state 0)
                    let state = (@ next-state 1)
                    let rhs-state =
                        loop (rhs state)
                            let ra =
                                @ state 0
                            if (empty? ra)
                                tupleof rhs state
                            else
                                let lop =
                                    infix-op infix-table ra (. op prec) >
                                let nextop =
                                    ? (none? lop)
                                        rtl-infix-op infix-table ra (. op prec) ==
                                        lop
                                if (none? nextop)
                                    tupleof rhs state
                                else
                                    let rhs-state =
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

    let bangra =
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
        let content =
            @ (. bangra modules) name
        if (none? content)
            let namestr =
                string name
            let pattern =
                @ bangra
                    quote path
            loop (pattern)
                if (not (empty? pattern))
                    let module-path =
                        make-module-path
                            @ pattern 0
                            namestr
                    let expr =
                        list-load module-path
                    if (not (none? expr))
                        let fn =
                            eval expr
                                table
                                    tupleof scope-parent-symbol
                                        globals;
                                    : module-path
                                module-path
                        let content =
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
        macro
            function (expr)
                let tail =
                    @ expr 1
                loop (tail)
                    let rest =
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
        let t = (typeof x)
        (t == symbol) or (t == parameter)

    function xpcall (func xfunc)
        let old_handler =
            get-exception-handler;
        function cleanup ()
            set-exception-handler! old_handler
        function try ()
            let finally = return
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
            block-macro
                function (expr scope)
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
                block-macro
                    function (expr)
                        cons
                            ? (empty? (@ expr 0 2))
                                qquote-1 (@ expr 0 1 0)
                                qquote-1 (@ expr 0 1)
                            @ expr 1

        : define
            block-macro
                function (expr scope)
                    let name =
                        @ expr 0 1 0
                    let exprlist =
                        @ expr 0 2
                    let subscope =
                        parameter (quote scope)
                    cons
                        list syntax-extend (list (parameter (quote _)) subscope)
                            list let (quote name) (quote =)
                                cons do exprlist
                            list set-key! subscope (list quote name) (quote name)
                            subscope
                        @ expr 1
        : let
            function =? (x)
                and
                    == (typeof x) symbol
                    == x (quote =)

            # support for multiple declarations in one let scope
            block-macro
                function (expr scope)
                    let args = (@ expr 0 1)
                    let argtype = (typeof (@ args 0))
                    if (== argtype symbol)
                        if (=? (@ args 1 0))
                            # regular form with support for recursion
                            cons
                                cons let args
                                @ expr 1
                        else
                            # unpacking multiple variables, without recursion

                            # iterate until we hit the = symbol, after which
                            # the body follows
                            function find-body (expr)
                                if (=? (@ expr 0))
                                    tupleof (list)
                                        @ expr 1
                                elseif (empty? expr)
                                    error "syntax: let <name> ... = <expression>"
                                else
                                    let out =
                                        find-body (@ expr 1)
                                    tupleof
                                        cons (@ expr 0) (@ out 0)
                                        @ out 1
                            let cells =
                                find-body args
                            list
                                list
                                    cons continuation
                                        cons
                                            cons (parameter (quote _)) (@ cells 0)
                                            @ expr 1
                                    list splice
                                        cons do
                                            @ cells 1

                    elseif (== argtype parameter)
                        assert (=? (@ args 1))
                            "syntax: let <parameter> = <expression>"
                        # regular form, hidden parameter
                        cons
                            cons continuation
                                cons
                                    list
                                        parameter (quote _)
                                        @ args 0
                                    @ expr 1
                            @ args 2
                    else
                        # multiple variables with support for recursion,
                        # and later variables can depend on earlier ones

                        # prepare quotable values from declarations
                        function handle-pairs (pairs)
                            if (empty? pairs)
                                @ expr 1
                            else
                                let pair =
                                    @ pairs 0
                                assert (=? (@ pair 1 0))
                                    "syntax: let (<name> = <expression>) ..."
                                cons
                                    cons let pair
                                    handle-pairs
                                        @ pairs 1

                        handle-pairs args

        tupleof scope-list-wildcard-symbol
            function (topexpr scope)
                let expr =
                    @ topexpr 0
                let head =
                    @ expr 0
                let headstr =
                    string head
                # method call syntax
                if
                    and
                        symbol? head
                        and
                            none? (get-scope-symbol scope head)
                            == (slice headstr 0 1) "."

                    let name =
                        symbol
                            slice headstr 1
                    let self-arg =
                        @ expr 1 0
                    let rest =
                        @ expr 2
                    let self =
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
            function (topexpr scope)
                let sym =
                    @ topexpr 0
                let it =
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
        syntax-infix-op % (syntax-infix-rules 600 > %)
        syntax-infix-op / (syntax-infix-rules 600 > /)
        syntax-infix-op // (syntax-infix-rules 600 > //)
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

