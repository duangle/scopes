# boot script
# the bangra executable looks for a boot file at
# path/to/executable.b and, if found, executes it.
bangra

let-syntax (scope)
    structof
        tupleof "#parent" scope
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
                                        (structof
                                            (tupleof "#parent" env)
                                            (tupleof param-name param))
                                        (slist
                                            (slist
                                                (cons function
                                                    (cons (slist param)
                                                        (@ expr 1)))
                                                (@ expr 0 2 0)))))))
                            (parameter param-name))) (string (@ expr 0 1 0)))
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
        tupleof "#symbol"
            function (env expr)
                print expr
                slist;

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
    structof
        tupleof "#parent" scope
        tupleof "slist-join" slist-join
        tupleof "::*"
            syntax-macro
                function (env expr)
                    slist
                        slist-join
                            @ expr 0 1
                            @ expr 1
        tupleof "and"
            syntax-macro
                function (env expr)
                    let tmp
                        parameter "tmp"
                    cons
                        slist
                            slist function (slist tmp)
                                slist branch tmp
                                    slist function (slist)
                                        @ expr 0 2 0
                                    slist function (slist) tmp
                            @ expr 0 1 0
                        @ expr 1
        tupleof "or"
            syntax-macro
                function (env expr)
                    let tmp
                        parameter "tmp"
                    cons
                        slist
                            slist function (slist tmp)
                                slist branch tmp
                                    slist function (slist) tmp
                                    slist function (slist)
                                        @ expr 0 2 0
                            @ expr 0 1 0
                        @ expr 1
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
                            cons
                                slist branch
                                    @ expr 0 1 0
                                    cons function
                                        cons (slist)
                                            @ expr 0 2
                                    cons function
                                        cons (slist) (slist)
                                @ expr 1
            syntax-macro if-rec

do
    print
        slist-join
            slist 1 2 3
            slist 4 5 6

    call print "hi"

    let x 5
    print (+ x 1)
    print x
    let k 1

    print
        if (== k 0)
            print "if!"
            1
        elseif (== k 1)
            print "elseif 1!"
            2
        elseif (== k 2)
            print "elseif 2!"
            3
        else
            print "else!"
            4

    print "hi"
    print "ho"
