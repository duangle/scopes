print
    .. "test module 2 loaded from " module-path
assert
    ==
        ..
            list 1 2 3
            list 4 5 6
        list 1 2 3 4 5 6

assert
    == bangra.test_module2 none
    \ "module loaded twice"
set-scope-symbol! bangra (quote test_module2) true

scopeof
    compute =
        fn (x y)
            x + y


