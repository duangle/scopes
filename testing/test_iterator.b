
# we define a generator function that yields multiple values from a loop,
# enclosed by a special first value and a configurable tail element

# generator functions can be composed to flatten the iteration

fn list-generator (l)
    fn (yield)
        loop-for x in l
            # return one element from this list
            yield x
            # and repeat until depleted
            continue

fn dog-generator (greeting words tail)
    fn (yield)
        # yield the first value conditionally
        if greeting
            yield "hello?!"
        else
            yield "yeeees?"
        # now yield all items of a list;
        # we continue in a different generator and pass on the yield function
        (list-generator words) yield
        # finally, yield a custom tail element
        yield tail
        # any return value will be ignored
        \ true

# now iterate through the values in this function
loop-for x in (dog-generator true (list "yes" "this" "is" "dog") "what's up?")
    with
        i = 0
    # print each value
    print i "#" x
    # run until generator is out of values
    continue (i + 1)
else
    print "done." i
    assert (i == 6)

fn bleh (args... rest)
    print rest
        list args...

bleh 1 2 3

call
    fn/cc (_ x)
        cc/call _
            fn (x y)
                print x y
                cc/call none _
            \ x
    \ "hi"

let done =
    Qualifier (quote done)

print
    quote (1 2; 3 4;)

fn ilist (alist)
    fn (xf)
        fn step (xf l)
            if (not (empty? l))
                let xff = (xf (@ l 0))
                fn ()
                    step xff (slice l 1)
            else
                xf;
        fn ()
            step xf alist

do
    let T =
        scopeof
            a = 1
            b = 2
            c = 3

    # FIXME: this one is indeed broken
    #loop-for k v in T
        print ">" k v
        continue

    fn atnext (l)
        if ((countof l) != (size_t 0))
            return (@ l 0) (slice l 1)

    loop
        with
            i = 0
            j = 2

        if (i < 10)
            print i j
            continue (i + 1) (j + 2)

    # this loop prints the number of elements and returns the number
    # of elements counted.

    # loop init variables:
    let l = # the list we're going to iterate
        list "yes" "this" "is" "dog"
    # store return value of loop in `total_elements`
    let total_elements =
        loop
            with l
                i = 0 # initialize loop state from scope
            let v = (atnext l)
            if (not (none? v))
                # get current element
                let x = (v @ 0)
                do
                    # custom processing block
                    print i x
                    # repeat the loop explicitly
                    continue
                        v @ 1 # advance the iterator
                        i + 1 # increase the counter
            else
                # a custom return block
                # this one returns the counter
                \ i

    print total_elements "element(s) counted."

do
    let z = (list)
    let zipped-lists =
        loop-for x y in (zip (range 5 10 2) (range 10))
            with z

            print x y
            continue
                cons (list x y) z
        else z
    print zipped-lists
    assert
        ==
            quote ((9 2) (7 1) (5 0))
            \ zipped-lists
    fn atnext (l)
        if ((countof l) != 0)
            tupleof (@ l 0) (slice l 1)
    fn iter-list (l)
        tupleof atnext l

    loop-for i c in (enumerate "the quick brown fox")
        if (c != " ")
            continue
        else
            print "space at index" i

    # this loop prints each element of a list and returns the number
    # of elements counted, without involving any mutable variables.

    # store return value of loop in `total_elements`
    let total_elements =
        loop-for x in (list "yes" "this" "is" "dog")
            with        # (with ...) is optional.
                i = 0   # in this case, we'll use it to keep state for
                        # a custom counter.
            # print element and our custom index
            print i x
            # repeat the loop (must always be done explicitly)
            continue
                i + 1 # increase the counter
        else # list iterator exhausted before we exited the loop
            # return the counter
            \ i

    print total_elements "element(s) counted."



