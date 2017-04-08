
define isvar?
    macro
        fn isvar? (expr scope)
            datum->syntax
                not (none? (@ scope (@ expr 1)))
                syntax->anchor expr

do
    let x = 5
    assert (isvar? x)
    assert (not (isvar? y))

assert (not (isvar? x))

do
    define X 5
    assert (isvar? X)

assert (not (isvar? X))
