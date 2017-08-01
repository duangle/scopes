

var x = 5
assert ((load x) == 5)
x = 10
assert ((load x) == 10)

# transparent pass-through of values
var y = 2
var z = 12
assert ((x + y) == z)

# declare unsized array on stack
var x @ 5 : i32
x @ 0 = 1
x @ 1 = x @ 0 + 1
x @ 2 = x @ 1 + 1
x @ 3 = x @ 2 + 1
x @ 4 = x @ 3 + 1
assert ((x @ 4) == 5)
