/// print
    require "test_module"

qquote-test :=
    qquote
        print
            unquote
                k := 1
                k + 2
        unquote-splice
            k := 2
            qquote
                print
                    unquote k
                print 1
assert
    == qquote-test
        quote
            print 3
            print 2
            print 1

k := 3
T :=
    structof
        test :
            function (self a b)
                + a b

assert
    k == (.test T 1 2)

assert
    <
        slist 1 2 2
        slist 1 2 3

assert
    ==
        slist-join
            slist 1 2 3
            slist 4 5 6
        slist 1 2 3 4 5 6

call print "hi"

assert
    == "hheellll  wwrrlldd"
        fold (iter "hello world") ""
            function (out k)
                if (k == "o")
                    out
                else
                    .. out k k

assert
    == "xyz"
        slice "abcdefghijklmnopqrstuvwxyz" -3
    "slice failed"

assert
    == 3
        length "hi!"
assert
    == 0
        length ""
assert
    == 3
        length
            tupleof 1 2 3
assert
    == 1
        length
            structof
                key : 123

assert
    1 + 2 * 3 == 7

let x 5
print (+ x 1)
print x
let k 1

let V
    structof
        x : 0
        y : 1
        z :
            structof
                u : 0
                v : 1
                w :
                    structof
                        red : 0
                        green : 1
                        blue : 2
assert
    == V.z.w.blue
        V . z @ (quote w) . blue
    "accessors failed"

assert
    2 * 2 + 1 == 5
    "infix operators failed"

assert
    (true and true or true) == true
    "and/or failed"

assert
    (tupleof 1 2 3) @ 2 == 3
    "tuple indexing failed"

do
    let i 0
    let k "!"
    assert
        == "!!!!!!!!!!!"
            loop (i k)
                if (i < 10)
                    print "#" i k
                    repeat (i + 1) (k .. "!")
                else
                    k
        "loop failed"

assert
    == 2
        if (k == 0)
            1
        elseif (k == 1)
            q := 2
            q
        elseif (k == 2)
            3
        else
            4
    "if-tree failed"
print;

assert
    == 9
        ::@ 5 +
        1 + 3

print "ok"
