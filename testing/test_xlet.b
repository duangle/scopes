

xlet rc =
    function (x n)
        if (x < n)
            (rc (x + 1) n) * 2
        else
            x

assert ((rc 5 10) == 320)

xlet
    even? =
        function (n)
            ? (n == 0) true
                odd? (n - one)
    odd? =
        function (n)
            ? (n == 0) false
                even? (n - one)
    one = 1

assert (even? 12)
assert (odd? 11)
print (odd? 30)
print "done."
