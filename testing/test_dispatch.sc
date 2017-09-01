do
    let m =
        fn-dispatcher
            call
                type-matcher
                    x = i32; y = i32; z = i32
                fn (a b c)
                    add a (add b c)
            call
                type-matcher f32 f32 f32
                fn (a b c)
                    fadd a (fadd b c)
            call
                type-matcher string string string
                fn (a b c)
                    .. a b c

    var x = 1
    assert ((m x 2 3) == 6)
    assert ((m 1.0 2.0 3.0) == 6.0)
    assert ((m "a" "b" "c") == "abc")

do
    fn... add3
        (a : i32, b : i32, c : i32)
            add a (add b c)
        (a : f32, b : f32, c : f32)
            fadd a (fadd b c)
        (a : string, b : string, c : string)
            string-join a (string-join b c)

    var x = 1 # reference type will be implicitly converted to value type
    assert ((add3 x 2 3) == 6)
    assert ((add3 1.0 2.0 3.0) == 6.0)
    assert ((add3 "a" "b" "c") == "abc")

    #dump
        add3 true

false

