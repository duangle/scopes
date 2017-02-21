
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

print
    quote
        loop
            1 2; 3;
            test;

let done =
    tag (quote done)

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

    loop ((i = 0) (j = 2))
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
            l (i = 0) # initialize loop state from scope

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
        for x y in (zip (range 5 10 2) (range 10)) loop (z)
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

    let l = # the list we're going to iterate
        list "yes" "this" "is" "dog"
    # store return value of loop in `total_elements`
    let total_elements =
        for x in l loop # loop (...) is optional.
            (i = 0);    # in this case, we'll use it to keep state for
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

function iter-list (alist)
    continuation (init)
        contcall
            continuation (break process k)
                if ((countof k) > 0)
                    contcall
                        continuation post-process (repeat)
                            contcall none repeat (slice k 1)
                        process
                        @ k 0
                else
                    contcall none break true
            init
            alist

function range (N)
    continuation (init)
        contcall
            continuation (break process i)
                if (i < N)
                    contcall
                        continuation (repeat)
                            contcall none repeat (i + 1)
                        process
                        i
                else
                    contcall none break true
            init
            0

function zip (gen-a gen-b)
    continuation (init)
        contcall
            continuation (a-nextfunc a-init-state)
                contcall
                    continuation (b-nextfunc b-init-state)
                        contcall
                            continuation (break process ab-state)
                                contcall
                                    break
                                    a-nextfunc
                                    continuation (a-cont a-value)
                                        contcall
                                            break
                                            b-nextfunc
                                            continuation (b-cont b-value)
                                                contcall
                                                    continuation (repeat)
                                                        contcall
                                                            continuation (_ a-next-state)
                                                                contcall
                                                                    continuation (_ b-next-state)
                                                                        contcall none repeat
                                                                            tupleof
                                                                                a-next-state
                                                                                b-next-state
                                                                    b-cont
                                                            a-cont
                                                    process
                                                    tupleof a-value b-value
                                            @ ab-state 1
                                    @ ab-state 0
                            init
                            tupleof a-init-state b-init-state
                    gen-b
            gen-a

let foreach =
    continuation foreach (break gen f)
        contcall
            continuation init-loop (nextfunc init-state)
                let step =
                    continuation step-loop (cont value)
                        f value
                        contcall
                            continuation process-element (_ state)
                                contcall break nextfunc step state
                            cont
                contcall break nextfunc step init-state
            gen

foreach
    #range 10
    #iter-list (quote A B C D E F)
    zip
        range 30
        zip
            iter-list (quote U X S)
            iter-list (quote V Y T)
    function (value)
        print "#" value
