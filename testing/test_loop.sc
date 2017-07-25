
let x =
    do
        let [loop] i k = (unconst 0) (unconst 10)
        if (i < k)
            loop (i + 1) k
        else i
assert (x == 10)


