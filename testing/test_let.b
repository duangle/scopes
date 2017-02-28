

do
    let
        x = 1 2 3
        y = 4 5 6
    let z = 7 8 9

    assert (x == 3)
    assert (y == 6)
    assert (z == 9)

do
    let x y z =
        tupleof 1 2 3
        tupleof 4 5 6
        tupleof 7 8 9

    assert (x == 7)
    assert (y == 8)
    assert (z == 9)

