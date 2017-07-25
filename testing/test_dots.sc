
# test if dots expand correctly in expression list
syntax-extend
    let k = (Scope)
    set-scope-symbol! k 'x true
    set-scope-symbol! syntax-scope 'k k
    syntax-scope

fn X ()
    if k.x true
    else false

assert (X)


