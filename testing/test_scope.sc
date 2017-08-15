
define-scope-macro isvar?
    let key = (decons args)
    let key = (key as Syntax as Symbol)
    let _ ok = (@ syntax-scope key)
    return ok syntax-scope

do
    let x = 5
    assert (isvar? x)
    assert (not (isvar? y))

assert (not (isvar? x))

do
    define X 5
    assert (isvar? X)

assert (not (isvar? X))
