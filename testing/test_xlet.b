
# tests for xlet and cross-referencing functions

xlet rc =
    fn (x n)
        if (x < n)
            (rc (x + 1) n) * 2
        else
            x

assert ((rc 5 10) == 320)

do
    xlet
        even? =
            fn (n)
                ? (n == 0) true
                    odd? (n - one)
        odd? =
            fn (n)
                ? (n == 0) false
                    even? (n - one)
        one = 1

    assert (even? 12)
    assert (odd? 11)

do
    fn even? (n)
        ? (n == 0) true
            odd? (n - 1)
    with
    fn odd? (n)
        ? (n == 0) false
            even? (n - 1)

    assert (even? 12)
    assert (odd? 11)

do
    xlet
        x = 1 2 3
        y = 4 5 6
    xlet z = 7 8 9

    assert (x == 3)
    assert (y == 6)
    assert (z == 9)
