
syntax-extend
    let s = (Scope)
    set-scope-symbol! s 'test
        fn ()
            true
    set-scope-symbol! syntax-scope 'S s
    syntax-scope

do
    using S filter "^test$"
    test;

do
    using S
    test;

do
    S.test;

