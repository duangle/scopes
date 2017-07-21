
# test if dots expand correctly in expression list
let k = (Scope)
set-scope-symbol! k (quote x) true

fn X ()
    if k.x true
    else false

assert (X)


