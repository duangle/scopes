

var x = 0
assert ((load x) == 0)
print ('deref x)
x = 10
assert ((load x) == 10)

let px = ('ref x)
px = 20
assert ((load px) == 20)
#dump (typeof px)

