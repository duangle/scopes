
define isvar?
    macro
        fn isvar? (expr scope)
            not (none? (find-scope-symbol scope (@ expr 1)))

do
    let x = 5
    assert (isvar? x)
    assert (not (isvar? y))

assert (not (isvar? x))

do
    xlet x = 5
    assert (isvar? x)
    assert (not (isvar? y))

assert (not (isvar? x))

do
    define X 5
    assert (isvar? X)

assert (not (isvar? X))
