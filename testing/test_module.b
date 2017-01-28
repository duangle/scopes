print "test module loaded!"
assert
    ==
        slist-join
            slist 1 2 3
            slist 4 5 6
        slist 1 2 3 4 5 6

table
    compute :
        function (x)
            injected-var + x

