# boot script
# the bangra executable looks for a boot file at
# path/to/executable.b and, if found, executes it.
bangra

let-syntax (scope)
    table
        tupleof "#parent" scope
        tupleof "API"
            import-c "bangra.h"
                tupleof;
        tupleof "syntax-single-macro"
            function (f)
                syntax-macro
                    function (env expr)
                        cons
                            f env
                                @ expr 0
                            @ expr 1
        tupleof "call"
            syntax-macro
                function (env expr)
                    cons
                        @ expr 0 1
                        @ expr 1
        tupleof "dump-syntax"
            syntax-macro
                function (env expr)
                    ((function (e)
                        (dump
                            (@ e 0 0))
                        (cons
                            (slist escape
                                (@ e 0 0))
                            (@ e 1)))
                        (expand env
                            (cons
                                (@ expr 0 1)
                                (@ expr 1))))
        tupleof "let"
            syntax-macro
                function (env expr)
                    ((function (param-name)
                        ((function (param)
                            (slist
                                (cons escape
                                    (expand
                                        (table
                                            (tupleof "#parent" env)
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
                function (env expr)
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
                function (env expr)
                    set-globals! env
                    cons (slist do)
                        @ expr 1

///
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
            tupleof "::*"
                syntax-macro
                    function (env expr)
                        slist
                            slist-join
                                @ expr 0 1
                                @ expr 1
            tupleof "."
                syntax-single-macro
                    function (env expr)
                        let key
                            @ expr 2 0
                        ? (symbol? key)
                            slist @
                                @ expr 1 0
                                string key
                            error "symbol expected"

            tupleof "function" # (function [name] (param ...) body ...)
                syntax-single-macro
                    function (env expr)
                        ? (symbol? (@ expr 1 0))
                            slist let
                                @ expr 1 0
                                cons function
                                    @ expr 2
                            cons function
                                @ expr 1
            tupleof "and"
                syntax-single-macro
                    function (env expr)
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
                    function (env expr)
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
                    function (env expr)
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
                    function (env expr)
                        let next-expr
                            @ expr 1 0
                        ? (slist-head? next-expr "elseif")
                            do
                                let nextif
                                    if-rec env
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
                    function (env expr)
                        slist structof
                            slist tupleof "prec" (@ expr 1 0)
                            slist tupleof "order" (string (@ expr 2 0))
                            slist tupleof "name" (@ expr 3 0)
            tupleof "syntax-infix-op"
                syntax-single-macro
                    function (env expr)
                        slist tupleof
                            (+ "#ifx:" (string (@ expr 1 0)))
                            @ expr 2 0

    let-syntax (scope)

        function fold (it init f)
            let next
                @ it 0
            let st
                next (@ it 1)
            let out init
            loop (out st)
                if (null? st) out
                else
                    repeat
                        f out (@ st 0)
                        next (@ st 1)

        function iter-string-r (s)
            tupleof
                function (i)
                    if (> i 0)
                        let k (- i 1)
                        tupleof (@ s k) k
                    else null
                length s

        function get-ifx-op (env op)
            let key
                + "#ifx:" (string op)
            ? (symbol? op)
                loop (env)
                    if (key? env key)
                        @ env key
                    elseif (key? env "#parent")
                        repeat (@ env "#parent")
                    else null
                null
        function has-infix-ops (infix-table expr)
            let word
                @ expr 1
            loop (word)
                if (empty? (@ word 1)) false
                elseif (!= (get-ifx-op infix-table (@ word 0)) null) true
                else
                    repeat
                        @ word 1

        function infix-op (infix-table token prec pred)
            let op
                get-ifx-op infix-table token
            if (null? op)
                error
                    + (string token)
                        " is not an infix operator, but embedded in an infix expression"
            elseif (pred (@ op "prec") prec)
                op
            else null

        function rtl-infix-op (infix-table token prec pred)
            let op
                get-ifx-op infix-table token
            if (null? op)
                error
                    + (string token)
                        " is not an infix operator, but embedded in an infix expression"
            elseif (and (== (@ op "order") "<") (pred (@ op "prec") prec))
                op
            else null

        function parse-infix-expr (infix-table lhs state mprec)
            loop (lhs state)
                let la (@ state 0)
                if (empty? la)
                    tupleof lhs state
                else
                    let op
                        infix-op infix-table la mprec >=
                    if (null? op)
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
                                        ? (null? lop)
                                            rtl-infix-op infix-table ra (@ op "prec") ==
                                            lop
                                    if (null? nextop)
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

        structof
            tupleof "#parent" scope
            tupleof "fold" fold
            tupleof "#slist"
                function (env topexpr)
                    let expr
                        @ topexpr 0
                    let head
                        @ expr 0
                    # method call syntax
                    if
                        and
                            symbol? head
                            == (@ (string head) (tupleof 0 1)) "."

                        let name
                            @ (string head) (tupleof 1)
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
                                        slist @ self name
                                        cons self rest
                                self-arg
                            @ topexpr 1
                    # infix operator support
                    elseif (has-infix-ops env expr)
                        cons
                            @
                                parse-infix-expr env
                                    \ (@ expr 0) (@ expr 1) 0
                                0
                            @ topexpr 1

            tupleof "#symbol"
                function (env topexpr)
                    let it
                        iter-string-r
                            string (@ topexpr 0)
                    if
                        fold it false
                            function (out k)
                                if (== k ".") true
                                else out

                        cons
                            fold it (slist (symbol ""))
                                function (out k)
                                    if (== k ".")
                                        cons (symbol "") (cons (quote .) out)
                                    else
                                        cons (symbol (+ k (@ out 0))) (@ out 1)
                            @ topexpr 1

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

    do
        let T
            structof
                tupleof "test"
                    function (self a b)
                        + a b

        print
            .test T 1 2

        print
            slist-join
                slist 1 2 3
                slist 4 5 6

        call print "hi"

        function iter (s)
            let ls
                length s
            let it
                function (i)
                    if (i < ls)
                        tupleof (@ s i) (i + 1)
                    else
                        null
            tupleof it 0

        print
            repr
                fold (iter "hello world") ""
                    function (out k)
                        print k
                        if (k == "o")
                            out
                        else
                            + out k k

        print
            repr
                @ "abcdefghijklmnopqrstuvwxyz"
                    tupleof -3

        print "lengths:"
            length "hi!"
            length ""
            length
                tupleof 1 2 3
            length
                structof
                    tupleof "key" 123

        print "kindof:"
            ==
                kindof float
                API.TypeKind.T_Integer

        print
            1 + 2 * 3 == 7

        let x 5
        print (+ x 1)
        print x
        let k 1

        let V
            structof
                tupleof "x" 0
                tupleof "y" 1
                tupleof "z"
                    structof
                        tupleof "u" 0
                        tupleof "v" 1
                        tupleof "w"
                            structof
                                tupleof "a" 0
                                tupleof "b" 1
                                tupleof "c" 2
        print "dot:"
            V . z @ "w" . c

        print
            2 * 2 + 1 == 5

        print
            true and true or true

        print
            (tupleof 1 2 3) @ 2 == 3

        do
            let i 0
            let k "!"
            print
                loop (i k)
                    if (i < 10)
                        print "#" i k
                        repeat (i + 1) (k + "!")
                    else
                        k

        print
            if (k == 0)
                print "if!"
                1
            elseif (k == 1)
                print "elseif 1!"
                2
            elseif (k == 2)
                print "elseif 2!"
                3
            else
                print "else!"
                4

        print "hi"
        print "ho"
