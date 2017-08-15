
assert 
    match '(1 2 3)
        '(1 2 3) true
        else false

and
    do-in
        let x =
            let y = 5
        true
    do-in
        let z =
            let w = y
        assert (x == 5)
        assert (z == 5)
        assert (w == 5)
        true
assert (x == 5)
assert (y == 5)
assert (z == 5)
assert (w == 5)


#assert
    match '(1 2 3)
        (list 1 $q 3) 
            assert (q == 2)
            true
        else false

let x = 3
let y = 4
assert
    match 5
        (or x y 5) true
        else false
