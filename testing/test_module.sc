print "test module loaded!"

let env = (Scope)
set-scope-symbol! env 'compute
    fn (x)
        injected-var + x

env
