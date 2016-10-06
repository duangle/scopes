# boot script
# the bangra executable looks for a boot file at
# path/to/executable.b and, if found, executes it.
bangra

# bootstrap syntax expander resolves environment parameters
# compiler expects parameters, has no symbol support

print "executed!"

syntax-scope (scope)
    structof
        tupleof "#parent" scope
        tupleof "call"
            syntax-macro
                function (env expr)
                    cons
                        @ expr 0 1
                        @ expr 1
        tupleof "let"
            syntax-macro
                function (env expr)
                    slist
                        slist
                            cons function
                                cons (slist (@ expr 0 1 0)) (@ expr 1)
                            @ expr 0 1 1 0

syntax-scope (scope)
    structof
        tupleof "#parent" scope
        tupleof "syntax-dump"
            syntax-macro
                function (env expr)
                    let e
                        expand env
                            cons
                                @ expr 0 1
                                @ expr 1
                    dump
                        @ e 0 0
                    cons
                        slist escape
                            @ e 0 0
                        @ e 1

call print "hi"

let x 5
print (+ x 1)
print x

print "hi"
