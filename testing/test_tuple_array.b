
do
    let k =
        arrayof int 1 2 3

    print k
    assert ((@ k 0) == 1)
    assert ((@ k 1) == 2)
    assert ((@ k 2) == 3)

do
    let k =
        arrayof (tuple i8 int)
            tupleof (i8 1) 4
            tupleof (i8 2) 5
            tupleof (i8 3) 6

    assert ((@ k 0 0) == (i8 1))
    assert ((@ k 1 0) == (i8 2))
    assert ((@ k 2 0) == (i8 3))

    assert ((@ k 0 1) == 4)
    assert ((@ k 1 1) == 5)
    assert ((@ k 2 1) == 6)

do
    let k =
        tupleof 1 2 3 4
    let m =
        bitcast (array int (size_t 4)) k
    assert ((@ k 0) == 1)
    assert ((@ k 1) == 2)
    assert ((@ k 2) == 3)
    assert ((@ k 3) == 4)
