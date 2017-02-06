# boot script
# the bangra executable looks for a boot file at
# path/to/executable.b and, if found, executes it.
let-syntax (scope)
    table
        tupleof scope-parent-symbol scope
        tupleof
            quote symbol?
            function (x)
                == (typeof x) symbol
        tupleof
            quote list?
            function (x)
                == (typeof x) list
        tupleof
            quote none?
            function (x)
                == x none
        tupleof
            quote empty?
            function (x)
                branch
                    == (typeof x) list
                    function ()
                        == x (list)
                    function () false
        tupleof
            quote key?
            function (x y)
                != (@ x y) none
        tupleof
            quote load
            function (path)
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
            function (f)
                syntax-macro
                    function (scope expr)
                        cons
                            f scope
                                @ expr 0
                            @ expr 1
        tupleof
            quote call
            syntax-macro
                function (scope expr)
                    cons
                        @ expr 0 1
                        @ expr 1
        tupleof
            quote dump-syntax
            syntax-macro
                function (scope expr)
                    ((function (e)
                        (dump
                            (@ e 0 0))
                        (cons
                            (list escape
                                (@ e 0 0))
                            (@ e 1)))
                        (expand scope
                            (cons
                                (@ expr 0 1)
                                (@ expr 1))))
        tupleof
            quote let
            syntax-macro
                function (scope expr)
                    ((function (param-name)
                        ((function (param scope-name)
                            (list
                                (list let-syntax (list scope-name)
                                    (list table
                                        (list tupleof (list quote scope-parent-symbol) scope-name)
                                        (list tupleof (list quote param-name) param)))
                                (list
                                    (cons function
                                        (cons (list param)
                                                (@ expr 1)))
                                    (@ expr 0 2 0))))
                            (parameter param-name)
                            (quote scope))) (@ expr 0 1 0))
        tupleof
            quote ?
            syntax-macro
                function (scope expr)
                    cons
                        list branch
                            @ expr 0 1 0
                            list function (list)
                                @ expr 0 2 0
                            list function (list)
                                @ expr 0 3 0
                        @ expr 1
        tupleof
            quote :
            syntax-macro
                function (scope expr)
                    cons
                        cons tupleof
                            cons
                                list quote
                                    @ expr 0 1 0
                                branch
                                    == (@ expr 0 2) (list)
                                    function ()
                                        list
                                            @ expr 0 1 0
                                    function ()
                                        @ expr 0 2
                        @ expr 1
        tupleof
            quote syntax-set-globals!
            syntax-macro
                function (scope expr)
                    set-globals! scope
                    cons
                        none
                        @ expr 1

let-syntax (scope)
    let list-join
        function (a b)
            ? (empty? a) b
                cons
                    @ a 0
                    list-join
                        @ a 1
                        b
    let list-head?
        function (expr name)
            ? (list? expr)
                do
                    let head (@ expr 0)
                    ? (symbol? head)
                        == head name
                        false
                false
    table
        tupleof scope-parent-symbol scope
        : list-join
        : list-head?
        : list-atom?
            function (x)
                ? (list? x)
                    empty? x
                    true
        : assert # (assert bool-expr [error-message])
            syntax-single-macro
                function (scope expr)
                    list ? (@ expr 1 0) true
                        list error
                            ? (empty? (@ expr 2)) "assertion failed"
                                @ expr 2 0
        : ::@
            syntax-macro
                function (scope expr)
                    cons
                        list-join
                            @ expr 0 1
                            list
                                @ expr 1 0
                        @ expr 2
        : ::*
            syntax-macro
                function (scope expr)
                    list
                        list-join
                            @ expr 0 1
                            @ expr 1
        : .
            syntax-single-macro
                function (scope expr)
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
                function (scope expr)
                    let decl
                        (@ expr 0 1 0)
                    ? (symbol? decl)
                        cons
                            list let decl
                                cons function
                                    @ expr 0 2
                            ? (empty? (@ expr 1))
                                list decl
                                @ expr 1
                        cons
                            cons function
                                @ expr 0 1
                            @ expr 1
        : and
            syntax-single-macro
                function (scope expr)
                    let tmp
                        parameter
                            quote tmp
                    list
                        list function (list tmp)
                            list branch tmp
                                list function (list)
                                    @ expr 2 0
                                list function (list) tmp
                        @ expr 1 0
        : or
            syntax-single-macro
                function (scope expr)
                    let tmp
                        parameter
                            quote tmp
                    list
                        list function (list tmp)
                            list branch tmp
                                list function (list) tmp
                                list function (list)
                                    @ expr 2 0
                        @ expr 1 0
        : loop
            syntax-single-macro
                function (scope expr)
                    let param-repeat
                        quote repeat
                    list do
                        list let param-repeat
                            cons function
                                cons
                                    @ expr 1 0
                                    @ expr 2
                        cons param-repeat
                            @ expr 1 0
        : if
            do
                let if-rec
                    function (scope expr)
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
                                        cons function
                                            cons (list)
                                                @ expr 0 2
                                        list function (list)
                                            @ nextif 0
                                    @ nextif 1
                            ? (list-head? next-expr (quote else))
                                cons
                                    list branch
                                        @ expr 0 1 0
                                        cons function
                                            cons (list)
                                                @ expr 0 2
                                        cons function
                                            cons (list)
                                                @ expr 1 0 1
                                    @ expr 2
                                do
                                    cons
                                        list branch
                                            @ expr 0 1 0
                                            cons function
                                                cons (list)
                                                    @ expr 0 2
                                            list function (list)
                                        @ expr 1
                syntax-macro if-rec
        : syntax-infix-rules
            function (prec order name)
                structof
                    tupleof (quote prec) prec
                    tupleof (quote order) order
                    tupleof (quote name) name
        : syntax-infix-op
            syntax-single-macro
                function (scope expr)
                    list tupleof
                        list quote
                            symbol
                                .. "#ifx:" (string (@ expr 1 0))
                        @ expr 2 0

let-syntax (scope)

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

    function get-ifx-op (scope op)
        let key
            symbol
                .. "#ifx:" (string op)
        ? (symbol? op)
            loop (scope)
                if (key? scope key)
                    @ scope key
                elseif (key? scope scope-parent-symbol)
                    repeat (@ scope scope-parent-symbol)
                else none
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

    table
        tupleof scope-parent-symbol scope
        : fold
        : iter
        : bangra
        : require find-module
        : and
            make-expand-multi-op-ltr and
        : or
            make-expand-multi-op-ltr or
        : call/cc
            function (f)
                __call/cc
                    function (continuation)
                        f
                            function (return-value)
                                __return/cc continuation return-value
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
                    if (symbol? (@ args 0))
                        # regular form
                        cons
                            cons let args
                            @ expr 1
                    else
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
                            list let-syntax (list scope-name)
                                cons table
                                    cons
                                        list tupleof (list quote scope-parent-symbol) scope-name
                                        @ cells 0
                            cons
                                cons function
                                    cons
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
                            != headstr ".."
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
                            list function (list self)
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
                        != sym (quote ..)
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

        syntax-infix-op := (syntax-infix-rules 50 < let)
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

syntax-set-globals!;
none
