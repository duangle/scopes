
let ptr = (pointerof 5)

let pptr = (pointerof ptr)

#assert
    (.getaddress ptr) != (.getaddress pptr)

print
    .getaddress ptr
    .getaddress pptr

let rptr = (.getvalue pptr)
let val = (.getvalue rptr)

print val rptr
