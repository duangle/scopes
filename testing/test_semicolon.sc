
do
    assert
        ==
            quote
                1 2; 3 4
            '((1 2) (3 4))

    assert
        ==
            quote
                1 2; 3
            '((1 2) 3)

    assert
        ==
            quote
                1 2; 3; 4 5
            '((1 2) (3) (4 5))

true
