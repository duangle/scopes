
# tests for recursive and cross-referencing functions

fn rc (x n)
    if (x < n)
        (recur (x + 1) n) * 2
    else x

assert ((rc 5 10) == 320)

do
    fn even? (n)
        ? (n == 0) true
            odd? (n - 1)
    with
    fn odd? (n)
        ? (n == 0) false
            even? (n - 1)

    dump-IL even?

    assert (even? 12)
    assert (odd? 11)

