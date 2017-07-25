
fn odd?

fn even? (n)
    if (n == 0) true
    else
        odd? (n - 1)
fn odd? (n)
    if (n == 0) false
    else
        even? (n - 1)

assert (even? (unconst 12))
assert (odd? (unconst 11))
