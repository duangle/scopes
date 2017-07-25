print
    .. "test module 2 loaded from " module-path
assert
    ==
        ..
            list 1 2 3
            list 4 5 6
        list 1 2 3 4 5 6

assert (not (va@ 1 (@ (unconst package) 'test_module2))) "module loaded twice"
set-scope-symbol! package 'test_module2 true

let env = (Scope)
set-scope-symbol! env 'compute
    fn (x y)
        x + y

env
