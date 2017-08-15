
# `var` creates a stack variable of reference type
var x = 5
assert (x == 5)
x = 10              # references support assignment operator
assert (x == 10)

var y = 2
var z = 12
assert ((x + y) == z) # references pass-through overloadable operators

# bind same reference to different name via let
let w = y
# copy by value to a new, independent reference
var z = y
y = 3
assert (y == 3)
assert (z == 2)
assert (w == y)

# loop with a mutable counter
var i = 0
let [loop]
if (i < 10)
    i = i + 1    
    loop;
assert (i == 10)

# declare unsized mutable array on stack; the size can be a variable
var y = 5
var x @ y : i32
x @ 0 = 1
x @ 1 = x @ 0 + 1
x @ 2 = x @ 1 + 1
x @ 3 = x @ 2 + 1
x @ 4 = x @ 3 + 1
assert ((x @ 4) == 5)



