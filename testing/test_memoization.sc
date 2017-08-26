
fn memoized ()
    typename "bang"

fn memoized2 (x)
    typename x

assert
    (memoized) == (memoized)

assert
    (memoized2 "test") == (memoized2 "test")
