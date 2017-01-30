print
    .. "test module 2 loaded from " module-path
assert
    ==
        slist-join
            slist 1 2 3
            slist 4 5 6
        slist 1 2 3 4 5 6

assert
    == bangra.test_module2 none
    "module loaded twice"
set-key! bangra
    : test_module2 true

table
    compute :
        function (x y)
            x + y


