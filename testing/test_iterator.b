
call
    continuation (_ x)
        contcall _
            function (x y)
                print x y
                contcall none _
            x
    "hi"

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
