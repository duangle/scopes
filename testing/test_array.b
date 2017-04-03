
define somelist
    quote (1 2 3 4)

print somelist

let T =
    type
        quote mytype

print "tid:"
    type-index T

set-type-symbol! T (quote repr)
    fn (value)
        #print value
        return "?"

set-type-symbol! T (quote apply-type)
    fn (...)
        print "apply-type!" ...

print
    T 1 2 3

print "alloc:"
    alloc T

print
    sizeof T

print
    alignof int

print
    qualifier (symbol "hi")

#
    do
        let k =
            arrayof int 1 2 3

        print k
        assert ((@ k 0) == 1)
        assert ((@ k 1) == 2)
        assert ((@ k 2) == 3)

    do
        let k =
            arrayof (tuple int8 int)
                tupleof (int8 1) 4
                tupleof (int8 2) 5
                tupleof (int8 3) 6

        assert ((@ k 0 0) == (int8 1))
        assert ((@ k 1 0) == (int8 2))
        assert ((@ k 2 0) == (int8 3))

        assert ((@ k 0 1) == 4)
        assert ((@ k 1 1) == 5)
        assert ((@ k 2 1) == 6)

    do
        let k =
            tupleof 1 2 3 4
        let m =
            bitcast (array int 4) k
        assert ((@ k 0) == 1)
        assert ((@ k 1) == 2)
        assert ((@ k 2) == 3)
        assert ((@ k 3) == 4)
