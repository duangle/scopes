
define array
    type (quote array)

fn align (offset align)
    let T = (typeof offset)
    fn-types integer? T
    (offset + align - (T 1)) & (~ (align - (T 1)))

set-type-symbol! array (quote apply-type)
    fn apply-array-type (count element)
        fn-types size_t type
        let etype =
            type (symbol (.. "[" (string count) " x " (string element) "]"))
        if (none? (. etype complete))
            let alignment = (alignof element)
            let stride = (align (sizeof element) alignment)
            set-type-symbol! etype (quote size) (stride * count)
            set-type-symbol! etype (quote alignment) alignment
            set-type-symbol! etype (quote apply-type)
                fn apply-typed-array-type (args...)
                    let self =
                        alloc etype
                    let argcount = (va-countof args...)
                    assert (argcount == count)
                        .. (string (int count)) " elements expected, got "
                            string (int argcount)
                    loop-for i arg in (enumerate (va-iter args...))
                        print i arg
                        continue
                    \ self
            set-type-symbol! etype (quote complete) true
        \ etype

let a4xint =
    array (size_t 4) int
print
    a4xint 1 2 3 4

define somelist
    quote (1 2 3 4)

print somelist

fn myf (a b)
    print myf
    print recur
    + a b

print
    myf 1 2

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
