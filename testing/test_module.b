print "test module loaded!"
assert
    ==
        ..
            list 1 2 3
            list 4 5 6
        list 1 2 3 4 5 6

table
    compute :
        function (x)
            injected-var + x

