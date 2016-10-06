# boot script
# the bangra executable looks for a boot file at
# path/to/executable.b and, if found, executes it.
bangra

syntax-scope (scope)
    structof
        tupleof "#parent" scope
        tupleof "call"
            syntax-macro
                function (env expr)
                    cons
                        @ expr 0 1
                        @ expr 1
        tupleof "syntax-dump"
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
                                                (@ expr 0 1 1 0)))))))
                            (parameter param-name))) (string (@ expr 0 1 0)))
        tupleof "?"
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

syntax-scope (scope)
    let join
        function (a b)
            ? (empty? a) b
                cons
                    @ a 0
                    join
                        @ a 1
                        b
    structof
        tupleof "#parent" scope
        tupleof "join" join
        tupleof "::*"
            syntax-macro
                function (env expr)
                    slist
                        join
                            @ expr 0 1
                            @ expr 1

print
    join
        slist 1 2 3
        slist 4 5 6

call print "hi"

let x 5
print (+ x 1)
print x

print "hi"
