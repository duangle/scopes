
# we define a generator function that yields multiple values from a loop,
# enclosed by a special first value and a configurable tail element

# generator functions can be composed to flatten the iteration

function list-generator (l)
    function (yield)
        for x in l
            # return one element from this list
            yield x
            # and repeat until depleted
            repeat;

function dog-generator (greeting words tail)
    function (yield)
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
        true

# now iterate through the values in this function
for x in (dog-generator true (list "yes" "this" "is" "dog") "what's up?")
    with
        i = 0
    # print each value
    print i "#" x
    # run until generator is out of values
    repeat (i + 1)
else
    print "done." i
    assert (i == 6)

function bleh (args... rest)
    print rest
        list args...

bleh 1 2 3

call
    continuation (_ x)
        contcall _
            function (x y)
                print x y
                contcall none _
            x
    "hi"

let done =
    tag (quote done)

print
    quote
        (1 2; 3 4;)

function ilist (alist)
    function (xf)
        function step (xf l)
            if (not (empty? l))
                let xff = (xf (@ l 0))
                function ()
                    step xff (slice l 1)
            else
                xf;
        function ()
            step xf alist

do
    let T =
        tableof
            : a 1
            : b 2
            : c 3
            tupleof (list 1 2 3) true

    for k v in T
        print ">" k v
        repeat;

    function atnext (l)
        if ((countof l) != 0)
            tupleof (@ l 0) (slice l 1)

    loop
        with
            i = 0
            j = 2

        if (i < 10)
            print i j
            repeat (i + 1) (j + 2)

    # this loop prints the number of elements and returns the number
    # of elements counted.

    # loop init variables:
    let l = # the list we're going to iterate
        tupleof "yes" "this" "is" "dog"
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
                    repeat
                        (v @ 1) # advance the iterator
                        i + 1 # increase the counter
            else
                # a custom return block
                # this one returns the counter
                i

    print total_elements "element(s) counted."

do
    let z = (list)
    let zipped-lists =
        for x y in (zip (range 5 10 2) (range 10))
            with z

            print x y
            repeat
                cons (list x y) z
        else
            z
    print zipped-lists
    assert
        ==
            quote ((9 2) (7 1) (5 0))
            zipped-lists
    function atnext (l)
        if ((countof l) != 0)
            tupleof (@ l 0) (slice l 1)
    function iter-list (l)
        tupleof atnext l

    for i c in (enumerate "the quick brown fox")
        if (c != " ")
            repeat;
        else
            print "space at index" i

    # this loop prints each element of a list and returns the number
    # of elements counted, without involving any mutable variables.

    # store return value of loop in `total_elements`
    let total_elements =
        for x in (list "yes" "this" "is" "dog")
            with        # (with ...) is optional.
                i = 0   # in this case, we'll use it to keep state for
                        # a custom counter.
            # print element and our custom index
            print i x
            # repeat the loop (must always be done explicitly)
            repeat
                i + 1 # increase the counter
        else # list iterator exhausted before we exited the loop
            # return the counter
            i

    print total_elements "element(s) counted."



