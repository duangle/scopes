
do
    assert
        ==?
            quote
                1 2; 3 4
            quote ((1 2) (3 4))

    assert
        ==?
            quote
                1 2; 3
            quote ((1 2) (3))

    print
        quote
            1 2; 3; 4 5
        quote ((1 2) 3 (4 5))

\ true
