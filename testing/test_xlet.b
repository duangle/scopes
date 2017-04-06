
# Some examples for looping/breaking control flow in Bangra.
  All these examples are implemented without late binding. Instead,
  the macro expansion process creates function entry points before
  expanding further.

# An anonymous function calling itself with the help of `recur`
let result =
    call
        fn (x n)
            if (x < n)
                (recur (x + 1) n) * 2
            else x
        \ 5 10

assert (result == 320)

# Two cross-dependent functions calling each other thanks to `xfn`
xfn
    even? (n)
        ? (n == 0) true
            odd? (n - 1)
    odd? (n)
        ? (n == 0) false
            even? (n - 1)

assert (even? 12)
assert (odd? 11)

# A simple loop with explicit `repeat` and `break` control functions.
  Using `break` is not strictly necessary unless you want to return
  multiple arguments.
let result =
    loop
        with
            i = 0
        if (i < 10)
            continue (i + 1)
        else
            break "done"

assert (result == "done")

# above loop reformulated as an iterator
let result =
    loop-for i in (range 10)
        continue
    else "done"

assert (result == "done")
