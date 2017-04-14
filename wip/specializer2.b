
#### test #####

fn pow2 (x)
    i32* x x

fn pow (x n)
    if (n == 0) 1
    elseif ((n % 2) == 0) (pow2 (pow x (n / 2)))
    else (x * (pow x (n - 1)))

assert
    (pow 2 5) == 32

fn pow2-int (x : int)
    pow2 x

dump-flow pow2-int

