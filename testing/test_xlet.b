
# tests for xlet and cross-referencing functions

xlet rc =
    function (x n)
        if (x < n)
            (rc (x + 1) n) * 2
        else
            x

assert ((rc 5 10) == 320)

do
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

do
    function even? (n)
        ? (n == 0) true
            odd? (n - 1)
    with
    function odd? (n)
        ? (n == 0) false
            even? (n - 1)

    assert (even? 12)
    assert (odd? 11)
