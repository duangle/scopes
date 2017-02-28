
let x =
    loop
        with
            i =
                "ignore me"
                0
            k =
                "ignore me"
                10

        if (i < k)
            repeat (i + 1) k
        else
            i

assert (x == 10)


