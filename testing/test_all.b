print

C :=
    external
        quote print_number
        cfunction int
            tupleof int
            false
print
    C 400

set-key! bangra
    : path
        cons
            .. interpreter-dir "/testing/?.b"
            bangra.path

assert
    ==
        require
            quote test_module2
        require
            quote test_module2

do
    m :=
        eval
            list-load
                .. interpreter-dir "/testing/test_module.b"
            table
                tupleof scope-parent-symbol
                    globals;
                injected-var : 3
    t := (m)
    assert
        7 == (t.compute 4)

do
    z :=
        table-join
            table
                x : 1
                y : 2
            table
                y : 3
                z : 4
                w : 5
    q := 303
    set-key! z (: q)
    assert (z.x == 1)
    assert (z.y == 3)
    assert (z.z == 4)
    assert (z.w == 5)
    assert (z.q == 303)

do
    let
        x 0
        y 1
        z 2
    assert
        and
            and
                x == 0
                y == 1
            z == 2
        "multideclaration failed"

assert
    ==
        and
            do
                print "and#1"
                true
            do
                print "and#2"
                true
            do
                print "and#3"
                true
            do
                print "and#4"
                false
            do
                error "should never see this"
                false
        false
    "'and' for more than two arguments failed"

assert
    ==
        or
            do
                print "or#1"
                false
            do
                print "or#2"
                false
            do
                print "or#3"
                false
            do
                print "or#4"
                true
            do
                error "should never see this"
                true
        true
    "'or' for more than two arguments failed"

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
        list 1 2 2
        list 1 2 3

assert
    ==
        list-join
            list 1 2 3
            list 4 5 6
        list 1 2 3 4 5 6

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

do
    T := API
        /// table
            x : 1
            y : 2
            z : 3
    kv :=
        next-key T none
    loop (kv)
        if (not (none? kv))
            print kv
            repeat
                next-key T (kv @ 0)

    print ">"
        cstr T.bangra_interpreter_dir
    print ">>"
        cstr T.bangra_interpreter_path
    print
        T.print_number 302

assert
    2 * 2 + 1 == 5
    "infix operators failed"

assert
    (true and true or true) == true
    "and/or failed"

assert
    (tupleof 1 2 3) @ 2 == 3
    "tuple indexing failed"

print typeof

assert
    call/cc
        function (cont)
            print "entered function"
            cont true
            error "unreachable"
    "call/cc failed"

do
    function cont-test (x)
        print "entered function"
        let topret return
        function subf ()
            print "entered subfunction"
            # `return x` would just exit subf, but not cont-test
            topret x
        subf;
        error "unreachable"
    assert
        cont-test true
        "continuation failed"

    function cont-test2 (x)
        if x
            return x
        else
            return;
        error "unreachable"
    assert
        (cont-test2 true) == true
    assert
        (cont-test2 false) == none

do
    function generator ()
        let T
            table;
        set-key! T
            : run
                function (ret)
                    let G
                        table
                            : ret
                    function yield ()
                        call/cc
                            function (cont)
                                G.ret
                                    function (ret)
                                        set-key! G
                                            : ret
                                        cont none
                    print "step 1"
                    yield;
                    print "step 2"
                    yield;
                    print "step 3"
                    yield;
        function step-gen ()
            set-key! T
                : run
                    call/cc T.run
    let g (generator)
    print "call 1:" (g)
    print "call 2:" (g)
    print "call 3:" (g)
    print "done"

#(define (process p) (toplevel-exit))

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
            let q 2
            q
        elseif (k == 2)
            3
        else
            4
    "if-tree failed"

assert
    == 9
        ::@ 5 +
        1 + 3

function test-noparam (x y)
    assert
        x == none
    assert
        y == none
    true
test-noparam;
function test-extraparam ()
    true
test-extraparam 1 2 3


function test-varargs (x y ...)
    assert
        and
            x == 1
            y == 2
    assert
        (length ...) == 3
    assert
        and
            ... @ 0 == 3
            ... @ 1 == 4
            ... @ 2 == 5
    list 1 2 (splice ...) 6
assert
    ==
        test-varargs 1 2 3 4 5
        list 1 2 3 4 5 6

assert
    ==
        list 1 2
            splice
                tupleof 3
                    splice
                        tupleof 4 5
            splice
                tupleof 6 7
            8
        list 1 2 3 4 5 6 7 8

do
    # single assignment.
    # names are always bound for the remainder of this scope.
    let p-value
        1 + 2

    # single assignment, recursive access for functions.
    let q-func
        function (x)
            ? (x <= 0)
                1
                x * (q-func (x - 1))

    # multiple assignments in one block, must not depend on each other,
    # but permits recursive access for functions.
    let
        button-type-yes     0
        button-type-no      1
        button-type-cancel  2

        b-func
            function (x)
                ? (x <= 0)
                    1
                    x * (b-func (x - 1))

    # multiple assignments by unpacking a tuple or other spliceable type,
    # no recursion.
    let x y z
        tupleof 1.0 1.5 2.0

    # since let is implemented using vararg function parameters,
    # simple vararg matching can also be performed
    let start center... end
        list "(" 1 2 3 ")"
    assert
        and
            start == "("
            end == ")"
            center... == (tupleof 1 2 3)

    let x y z
        tupleof 1 2 3
    print "HALLO" x y z
    assert
        ==
            list x y z
            list 1 2 3

print "done."
