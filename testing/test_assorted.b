
assert
    ==
        require
            quote test_module2
        require
            quote test_module2

print
    min Integer i8
    max 3 4 5

do
    let test-qualifier =
        Qualifier (quote test)
    print
        (test-qualifier int) <: Qualifier


#do
    let C =
        external
            quote print_number
            cfunction int
                tuple int
                \ false

    print
        C 400

do
    let sc = (Scope (globals))
    set-scope-symbol! sc (quote injected-var) 3
    let m =
        eval
            list-load
                .. interpreter-dir "/testing/test_module.b"
            \ sc
    let t = (m)
    assert
        7 == (t.compute 4)

#do
    let z =
        ..
            tableof
                x : 1
                y : 2
            tableof
                y : 3
                z : 4
                w : 5
    let q = 303
    set-key! z (: q)
    assert (z.x == 1)
    assert (z.y == 3)
    assert (z.z == 4)
    assert (z.w == 5)
    assert (z.q == 303)

let
    a = 1
    a = (a + 1)
assert (a == 2)

do
    let
        x = 0
        y = 1
        z = 2
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
                \ true
            do
                print "and#2"
                \ true
            do
                print "and#3"
                \ true
            do
                print "and#4"
                \ false
            do
                error "should never see this"
                \ false
        \ false
    "'and' for more than two arguments failed"

assert
    ==
        or
            do
                print "or#1"
                \ false
            do
                print "or#2"
                \ false
            do
                print "or#3"
                \ false
            do
                print "or#4"
                \ true
            do
                error "should never see this"
                \ true
        \ true
    "'or' for more than two arguments failed"

let qquote-test =
    qquote-syntax
        print
            unquote
                let k = 1
                datum->syntax
                    k + 2
                    active-anchor
        unquote-splice
            let k = 2
            qquote-syntax
                print
                    unquote
                        datum->syntax k
                            active-anchor
                print 1
assert
    == qquote-test
        quote
            print 3
            print 2
            print 1

let k = 3
let T =
    scopeof
        test =
            fn (self a b)
                + a b

assert
    k == (.test T 1 2)

assert
    <
        list 1 2 2
        list 1 2 3

assert
    ==
        ..
            list 1 2 3
            list 4 5 6
        list 1 2 3 4 5 6

call print "hi"

assert
    == "hheellll  wwrrlldd"
        loop-for k in "hello world"
            with
                out = ""
            continue
                if (k == "o") out
                else
                    .. out k k
        else out

do
    let k = "abcdefghijklmnopqrstuvwxyz"
    assert
        == "xyz"
            slice k -3
        "slice failed"
    assert
        == "bcd"
            slice k 1 4
        "slice failed"

assert
    == (size_t 3)
        countof "hi!"
assert
    == (size_t 0)
        countof ""
assert
    == (size_t 3)
        countof
            tupleof 1 2 3
assert
    == (size_t 1)
        countof
            structof
                key = 123

assert
    1 + 2 * 3 == 7

let x = 5
print (+ x 1)
print x
let k = 1

let V =
    structof
        x = 0
        y = 1
        z =
            structof
                u = 0
                v = 1
                w =
                    structof
                        red = 0
                        green = 1
                        blue = 2
assert
    == V.z.w.blue
        V . z @ (quote w) . blue
    "accessors failed"

assert
    2.0 ** 5.0 == 32.0

assert
    and
        4 == (list 0 (list 1 2 (list 3 4) 5) 6) @ 1 @ 2 @ 1
        4 == (@ (list 0 (list 1 2 (list 3 4) 5) 6) 1 2 1)

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

fn/cc call/cc (cont func)
    cc/call none func cont

assert
    call/cc
        fn (cont)
            print "entered function"
            cc/call none cont true
    "call/cc failed"

do
    let cont-test =
        fn/cc cont-test (topret x)
            print "entered function"
            fn subf ()
                print "entered subfunction"
                # `return x` would just exit subf, but not cont-test
                cc/call none topret x
            subf;
            error "unreachable"
    assert
        cont-test true
        "continuation failed"

    fn cont-test2 (x)
        if x
            return x
        else
            return;
        error "unreachable"
    assert
        (cont-test2 true) == true
    assert
        (cont-test2 false) == none

## hashes are not part of core language atm
    print
        hash "this string, hashed"
        hash print
        hash true
        hash false
        hash 0
        hash int8
    print
        hash 303
        hash -303
    print
        hash 0.0
        hash 1.0
        hash 1.5
        hash
            print

    assert
        ==
            hash "hello world"
            hash
                .. "hello" " " "world"

    do
        let S = "the quick brown fox jumped over the lazy dog"
        print
            ==
                hash
                    tupleof S
                hash S

    print
        hash
            tupleof
                tupleof 1 2 3
                tupleof 3 2 1
        hash
            tupleof 1 2 3 3 2 1

    print
        hash
            tupleof 3 5
        hash 0x0000000500000003

do
    let i = 0
    let k = "!"
    assert
        == "!!!!!!!!!!!"
            loop (i k)
                if (i < 10)
                    print "#" i k
                    continue (i + 1) (k .. "!")
                else k
        "loop failed"

assert
    == 2
        if (k == 0) 1
        elseif (k == 1)
            let q = 2
            \ q
        elseif (k == 2) 3
        else 4
    "if-tree failed"

assert
    == 9
        ::@ 5 +
        1 + 3

fn test-noparam (x y)
    assert
        x == none
    assert
        y == none
    \ true
test-noparam;
fn test-extraparam () true
test-extraparam 1 2 3


fn test-varargs (x y ...)
    assert
        and
            x == 1
            y == 2
    assert
        (va-countof ...) == (u64 3)
    assert
        and
            (va-arg 0 ...) == 3
            (va-arg 1 ...) == 4
            (va-arg 2 ...) == 5
    list 1 2 ...
assert
    ==
        test-varargs 1 2 3 4 5
        list 1 2 3 4 5

# splice interface removed atm
#assert
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
    let
        a = 1
        b = (a + 1)
    print a b

do
    # single assignment.
    # names are always bound for the remainder of this scope.
    let p-value =
        1 + 2

    # function declarations support recursion.
    fn q-func (x)
        ? (x <= 0) 1
            x * (q-func (x - 1))

    print
        q-func 5

    # multiple assignments in one block; later assignments can depend on
    # earlier ones. no support for recursion.
    let
        button-type-yes     = 0
        button-type-no      = 1
        button-type-cancel  = (button-type-no + 1)

    fn b-func (x)
        ? (x <= 0) 1
            x * (b-func (x - 1))

    print
        b-func 5

    # multiple assignments by unpacking a tuple or other spliceable type,
    # no recursion.
    let x y z =
        do
            print "tuple unpacking!"
            _ 1.0 1.5 2.0

    # since let is implemented using vararg function parameters,
    # simple vararg matching can also be performed
    let start center... end = "(" 1 2 3 ")"
    assert
        and
            start == "("
            end == ")"
            (va-arg 0 center...) == 1
            (va-arg 1 center...) == 2
            (va-arg 2 center...) == 3

    let x y z = 1 2 3
    print "HALLO" x y z
    assert
        ==
            list x y z
            list 1 2 3

assert (i8 == i8)
assert (i8 <= i8)
assert (not (i8 <: i8))
assert (not (i8 <: i16))
assert (not (i16 <: i8))
assert (i8 <: Integer)
assert (not (i8 <: Real))
assert ((pointer int) <: pointer)
assert ((array int (size_t 8)) <: array)
assert ((vector float (size_t 4)) <: vector)

define TEST 5
define TEST2
    TEST + 1
define TEST3
    fn (x)
        x * 2
assert
    and
        TEST == 5
        TEST2 == 6
        (TEST3 6) == 12

dump-syntax
    do
        let k = "hello world"
        print k

try
    print "enter #1"
    try
        print "enter #2"
        raise "raise #2.1"
        print "leave #2"
    except (e)
        print "exception #2:" e
        raise ("reraise #2: " .. e)
    print "leave #1"
except (e)
    print "exception #1:" e

