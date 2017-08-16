
let x =
    do
        let loop (i k) = (unconst 0) (unconst 10)
        if (i < k)
            loop (i + 1) k
        else i
assert (x == 10)

var i = 10
while (i != 0)
    i = i - 1
    if (i == 3)
        continue;
    print i
