# boot script
# the bangra executable looks for a boot file at
# path/to/executable.b and, if found, executes it.
let-syntax (scope)
    table
        tupleof "#parent" scope
        tupleof "symbol?"
            function (x)
                == (typeof x) symbol
        tupleof "slist?"
            function (x)
                == (typeof x) slist
        tupleof "none?"
            function (x)
                == x none
        tupleof "empty?"
            function (x)
                == x (slist)
        tupleof "key?"
            function (x y)
                != (@ x y) none
        tupleof "API"
            import-c
                # todo: search path & embedded resources
                .. interpreter-dir "/bangra.h"
                tupleof;
        tupleof "syntax-single-macro"
            function (f)
                syntax-macro
                    function (scope expr)
                        cons
                            f scope
                                @ expr 0
                            @ expr 1
        tupleof "call"
            syntax-macro
                function (scope expr)
                    cons
                        @ expr 0 1
                        @ expr 1
        tupleof "dump-syntax"
            syntax-macro
                function (scope expr)
                    ((function (e)
                        (dump
                            (@ e 0 0))
                        (cons
                            (slist escape
                                (@ e 0 0))
                            (@ e 1)))
                        (expand scope
                            (cons
                                (@ expr 0 1)
                                (@ expr 1))))
        tupleof "let"
            syntax-macro
                function (scope expr)
                    ((function (param-name)
                        ((function (param)
                            (slist
                                (cons escape
                                    (expand
                                        (table
                                            (tupleof "#parent" scope)
                                            (tupleof param-name param))
                                        (slist
                                            (slist
                                                (cons function
                                                    (cons (slist param)
                                                        (@ expr 1)))
                                                (@ expr 0 2 0)))))))
                            (parameter param-name))) (@ expr 0 1 0))
        tupleof "?"
            syntax-macro
                function (scope expr)
                    cons
                        slist branch
                            @ expr 0 1 0
                            slist function (slist)
                                @ expr 0 2 0
                            slist function (slist)
                                @ expr 0 3 0
                        @ expr 1
        tupleof "syntax-set-globals!"
            syntax-macro
                function (scope expr)
                    set-globals! scope
                    cons
                        none
                        @ expr 1

let-syntax (scope)
    let slist-join
        function (a b)
            ? (empty? a) b
                cons
                    @ a 0
                    slist-join
                        @ a 1
                        b
    let slist-head?
        function (expr name)
            ? (slist? expr)
                do
                    let head (@ expr 0)
                    ? (symbol? head)
                        == (string head) name
                        false
                false
    table
        tupleof "#parent" scope
        tupleof "slist-join" slist-join
        tupleof "slist-head?" slist-head?
        tupleof "slist-atom?"
            function (x)
                ? (slist? x)
                    empty? x
                    true
        tupleof "assert" # (assert bool-expr [error-message])
            syntax-single-macro
                function (scope expr)
                    slist ? (@ expr 1 0) true
                        slist error
                            ? (empty? (@ expr 2)) "assertion failed"
                                @ expr 2 0
        tupleof "::@"
            syntax-macro
                function (scope expr)
                    cons
                        slist-join
                            @ expr 0 1
                            slist
                                @ expr 1 0
                        @ expr 2
        tupleof "::*"
            syntax-macro
                function (scope expr)
                    slist
                        slist-join
                            @ expr 0 1
                            @ expr 1
        tupleof "."
            syntax-single-macro
                function (scope expr)
                    let key
                        @ expr 2 0
                    ? (symbol? key)
                        slist
                            (do @)
                            @ expr 1 0
                            string key
                        error "symbol expected"

        tupleof "function" # (function [name] (param ...) body ...)
            syntax-single-macro
                function (scope expr)
                    ? (symbol? (@ expr 1 0))
                        slist let
                            @ expr 1 0
                            cons function
                                @ expr 2
                        cons function
                            @ expr 1
        tupleof "and"
            syntax-single-macro
                function (scope expr)
                    let tmp
                        parameter "tmp"
                    slist
                        slist function (slist tmp)
                            slist branch tmp
                                slist function (slist)
                                    @ expr 2 0
                                slist function (slist) tmp
                        @ expr 1 0
        tupleof "or"
            syntax-single-macro
                function (scope expr)
                    let tmp
                        parameter "tmp"
                    slist
                        slist function (slist tmp)
                            slist branch tmp
                                slist function (slist) tmp
                                slist function (slist)
                                    @ expr 2 0
                        @ expr 1 0
        tupleof "loop"
            syntax-single-macro
                function (scope expr)
                    let param-repeat
                        quote repeat
                    slist do
                        slist let param-repeat
                            cons function
                                cons
                                    @ expr 1 0
                                    @ expr 2
                        cons param-repeat
                            @ expr 1 0
        tupleof "if"
            let if-rec
                function (scope expr)
                    let next-expr
                        @ expr 1 0
                    ? (slist-head? next-expr "elseif")
                        do
                            let nextif
                                if-rec scope
                                    @ expr 1
                            cons
                                slist branch
                                    @ expr 0 1 0
                                    cons function
                                        cons (slist)
                                            @ expr 0 2
                                    slist function (slist)
                                        @ nextif 0
                                @ nextif 1
                        ? (slist-head? next-expr "else")
                            cons
                                slist branch
                                    @ expr 0 1 0
                                    cons function
                                        cons (slist)
                                            @ expr 0 2
                                    cons function
                                        cons (slist)
                                            @ expr 1 0 1
                                @ expr 2
                            do
                                cons
                                    slist branch
                                        @ expr 0 1 0
                                        cons function
                                            cons (slist)
                                                @ expr 0 2
                                        slist function (slist)
                                    @ expr 1
            syntax-macro if-rec
        tupleof "syntax-infix-rules"
            syntax-single-macro
                function (scope expr)
                    slist structof
                        slist tupleof "prec" (@ expr 1 0)
                        slist tupleof "order" (string (@ expr 2 0))
                        slist tupleof "name" (@ expr 3 0)
        tupleof "syntax-infix-op"
            syntax-single-macro
                function (scope expr)
                    slist tupleof
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
            .. "#ifx:" (string op)
        ? (symbol? op)
            loop (scope)
                if (key? scope key)
                    @ scope key
                elseif (key? scope "#parent")
                    repeat (@ scope "#parent")
                else none
            none
    function has-infix-ops (infix-table expr)
        let word
            @ expr 1
        loop (word)
            if (empty? (@ word 1)) false
            elseif (!= (get-ifx-op infix-table (@ word 0)) none) true
            else
                repeat
                    @ word 1

    function infix-op (infix-table token prec pred)
        let op
            get-ifx-op infix-table token
        if (none? op)
            error
                .. (string token)
                    " is not an infix operator, but embedded in an infix expression"
        elseif (pred (@ op "prec") prec)
            op
        else none

    function rtl-infix-op (infix-table token prec pred)
        let op
            get-ifx-op infix-table token
        if (none? op)
            error
                .. (string token)
                    " is not an infix operator, but embedded in an infix expression"
        elseif (and (== (@ op "order") "<") (pred (@ op "prec") prec))
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
                                    infix-op infix-table ra (@ op "prec") >
                                let nextop
                                    ? (none? lop)
                                        rtl-infix-op infix-table ra (@ op "prec") ==
                                        lop
                                if (none? nextop)
                                    tupleof rhs state
                                else
                                    let rhs-state
                                        parse-infix-expr
                                            infix-table
                                            rhs
                                            state
                                            @ nextop "prec"
                                    repeat
                                        @ rhs-state 0
                                        @ rhs-state 1
                    repeat
                        slist (@ op "name") lhs
                            @ rhs-state 0
                        @ rhs-state 1

    table
        tupleof "#parent" scope
        tupleof "fold" fold
        tupleof "iter" iter
        # quasiquote support
        # (qquote expr [...])
        tupleof "qquote"
            do
                function qquote-1 (x)
                    if (slist-atom? x)
                        slist quote x
                    elseif (slist-head? x "unquote")
                        unwrap-single (@ x 1)
                    elseif (slist-head? x "qquote")
                        qquote-1 (qquote-1 (@ x 1 0))
                    elseif (slist-atom? (@ x 0))
                        slist cons
                            qquote-1 (@ x 0)
                            qquote-1 (@ x 1)
                    elseif (slist-head? (@ x 0) "unquote-splice")
                        slist slist-join
                            unwrap-single (@ x 0 1)
                            qquote-1 (@ x 1)
                    else
                        slist cons
                            qquote-1 (@ x 0)
                            qquote-1 (@ x 1)
                syntax-macro
                    function (scope expr)
                        cons
                            ? (empty? (@ expr 0 2))
                                qquote-1 (@ expr 0 1 0)
                                qquote-1 (@ expr 0 1)
                            @ expr 1

        tupleof "#slist"
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
                        slice headstr 1
                    let self-arg
                        @ expr 1 0
                    let rest
                        @ expr 2
                    let self
                        parameter "self"
                    cons
                        slist
                            slist function (slist self)
                                cons
                                    slist (do @) self name
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
        tupleof "#symbol"
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
                            fold it (slist "")
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
