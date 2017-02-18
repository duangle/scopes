
call
    continuation (_ x)
        contcall _
            function (x y)
                print x y
                contcall none _
            x
    "hi"

function iter-list (alist)
    function (init)
        init
            function (process k)
                if (not (empty? k))
                    process
                        function (repeat)
                            repeat (slice k 1)
                        @ k 0
            alist

function range (N)
    function (init)
        init
            function (process i)
                if (i < N)
                    process
                        function (repeat)
                            repeat (i + 1)
                        i
            0

function zip (gen-a gen-b)
    function (init)
        gen-a
            function (a-nextfunc a-init-state...)
                gen-b
                    function (b-nextfunc b-init-state...)
                        init
                            function (process a-state b-state)
                                a-nextfunc
                                    function (a-cont a-value...)
                                        b-nextfunc
                                            function (b-cont b-value...)
                                                process
                                                    function (repeat)
                                                        a-cont
                                                            function (a-next-state...)
                                                                b-cont
                                                                    function (b-next-state...)
                                                                        repeat
                                                                            a-next-state...
                                                                            b-next-state...
                                                    splice a-value...
                                                    splice b-value...
                                            splice b-state
                                    splice a-state
                            a-init-state...
                            b-init-state...

function foreach (gen f)
    gen
        function (nextfunc init-state...)
            function step (cont value...)
                f (splice value...)
                cont
                    function (state...)
                        nextfunc step (splice state...)
            nextfunc step (splice init-state...)

foreach
    zip
        range 10
        zip
            iter-list (quote U X S)
            iter-list (quote V Y T)
    function (values...)
        print "#" values...
