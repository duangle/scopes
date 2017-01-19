k := 3
T :=
    structof
        tupleof "test"
            function (self a b)
                + a b

assert
    k == (.test T 1 2)

print
    slist-join
        slist 1 2 3
        slist 4 5 6

call print "hi"

function iter (s)
    let ls
        length s
    let it
        function (i)
            if (i < ls)
                tupleof (@ s i) (i + 1)
            else
                none
    tupleof it 0

print
    repr
        fold (iter "hello world") ""
            function (out k)
                print k
                if (k == "o")
                    out
                else
                    .. out k k

print
    repr
        slice "abcdefghijklmnopqrstuvwxyz" -3

print "lengths:"
    length "hi!"
    length ""
    length
        tupleof 1 2 3
    length
        structof
            tupleof "key" 123

print
    1 + 2 * 3 == 7

let x 5
print (+ x 1)
print x
let k 1

let V
    structof
        tupleof "x" 0
        tupleof "y" 1
        tupleof "z"
            structof
                tupleof "u" 0
                tupleof "v" 1
                tupleof "w"
                    structof
                        tupleof "red" 0
                        tupleof "green" 1
                        tupleof "blue" 2
print "dot:"
    V . z @ "w" . blue
    V.z.w.blue

print
    2 * 2 + 1 == 5

print "true and true or true:"
    true and true or true

print "(tupleof 1 2 3) @ 2 == 3:"
    (tupleof 1 2 3) @ 2 == 3

assert true

do
    let i 0
    let k "!"
    print
        loop (i k)
            if (i < 10)
                print "#" i k
                repeat (i + 1) (k .. "!")
            else
                k

print
    if (k == 0)
        print "if!"
        1
    elseif (k == 1)
        print "elseif 1!"
        2
    elseif (k == 2)
        print "elseif 2!"
        3
    else
        print "else!"
        4
print;
print "hi"
print "ho"
