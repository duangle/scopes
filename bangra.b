# boot script
# the bangra executable looks for a boot file at
# path/to/executable.b and, if found, executes it.
bangra

let-syntax
    call
        syntax-macro
            function (env expr)
                cons
                    @ expr 0 1
                    @ expr 1
    dump-syntax
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
    let
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
                                            (@ expr 0 1 1 0)))))))
                        (parameter param-name))) (string (@ expr 0 1 0)))
    ?
        syntax-macro
            function (env expr)
                cons
                    slist branch
                        @ expr 0 1 0
                        slist function (slist)
                            @ expr 0 1 1 0
                        slist function (slist)
                            @ expr 0 1 1 1 0
                    @ expr 1
    syntax-set-globals!
        syntax-macro
            function (env expr)
                set-globals! env
                cons (slist do)
                    @ expr 1

let-syntax
    slist-join
        function (a b)
            ? (empty? a) b
                cons
                    @ a 0
                    slist-join
                        @ a 1
                        b
    ::*
        syntax-macro
            function (env expr)
                slist
                    slist-join
                        @ expr 0 1
                        @ expr 1

do
    print
        slist-join
            slist 1 2 3
            slist 4 5 6

    call print "hi"

    let x 5
    print (+ x 1)
    print x

    print "hi"
