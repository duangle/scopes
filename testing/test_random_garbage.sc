# deferring remaining expressions to bootstrap parser
#syntax-apply-block
    fn (anchor exprs env)
        walk-list
            fn on-leaf (value depth)
                print-spaces depth
                #Any-dispatch value
                io-write
                    Any-repr value
                io-write "\n"
            unconst exprs
            unconst 0

# static assertion
    branch (type== (typeof 1) i32)
        fn/cc (_)
        fn/cc (_)
            compiler-error! "static assertion failed: argument not i32"
    branch (constant? (add 1 2))
        fn/cc (_)
        fn/cc (_)
            compiler-error! "static assertion failed: argument not constant"

# naive factorial
    fn/cc fac (_ n)
        branch (icmp<=s n 1)
            fn/cc (_) 1
            fn/cc (_)
                mul n
                    call
                        fn/cc (_ n-1)
                            fac n-1
                        sub n 1

    fac
        unconst 5

# importing C code
    call
        fn/cc (_ lib)
            call
                fn/cc (_ sinf printf)
                    printf
                        string->rawstring "test: %f\n"
                        sinf 0.5235987755982989
                    printf
                        string->rawstring "fac: %i\n"
                        fac
                            unconst 5
                purify
                    Any-extract
                        Scope@ lib 'sinf
                Any-extract
                    Scope@ lib 'printf

        import-c "testdata.c" "
            float sinf(float);
            int printf( const char* format, ... );
            "
            eol

# continuation skip
    fn/cc mainf (return x)
        io-write "doin stuff...\n"
        call
            fn/cc subf (_ x)
                branch (icmp== x 0)
                    fn/cc (_) true
                    fn/cc (_)
                        io-write "terminated early!\n"
                        return false
            x
        io-write "done stuff.\n"
        true

    mainf
        unconst 0
    mainf
        unconst 1


# mutual recursion
    fn/cc even? (eret ei)
        fn/cc odd? (oret oi)
            branch (icmp>s oi 0)
                fn/cc (_)
                    even? (sub oi 1)
                fn/cc (_)
                    eret false

        branch (icmp>s ei 0)
            fn/cc (_)
                odd? (sub ei 1)
            fn/cc (_)
                _ true

    branch
        even?
            unconst 30
        fn/cc (_)
            io-write "even\n"
        fn/cc (_)
            io-write "odd\n"


# tail-recursive program that avoids closures
    fn/cc puts (return s n)
        fn/cc loop (() i n)
            branch (icmp== i n)
                fn/cc ()
                    return
                fn/cc ()
                    io-write s
                    io-write "\n"
                    cc/call loop none (add i 1) n
        cc/call loop none
            branch (constant? n)
                fn/cc (_) 0
                fn/cc (_) (unconst 0)
            n

    puts "hello world"
        unconst 8

# tail-recursive program with closures
    fn/cc puts (return s n)
        fn/cc loop (_ i n)
            branch (icmp== i n)
                fn/cc (_)
                    return
                fn/cc (_)
                    io-write s
                    io-write "\n"
                    loop (add i 1) n
        loop
            branch (constant? n)
                fn/cc (_) 0
                fn/cc (_) (unconst 0)
            \ n

    puts "hello world" (unconst 8)

# explicit instantiation
#fn test-explicit-instantiation ()
    fn test-add (x1 y1 z1 w1 x2 y2 z2 w2)
        return
            fadd x1 x2
            fadd y1 y2
            fadd z1 z2
            fadd w1 w2

    dump-label test-add
    dump-label
        typify test-add f32 f32 f32 f32 f32 f32 f32 f32
    let f =
        compile
            typify test-add f32 f32 f32 f32 f32 f32 f32 f32
            \ 'dump-disassembly 'dump-module
    dump f
    print
        f 1. 2. 3. 4. 5. 6. 7. 8.

#test-explicit-instantiation

#fn test-select-optimization ()
    fn conditional-select (opt i)
        if opt
            return
                add i 5
        else
            return
                mul i 5

    dump-label
        typify conditional-select bool i32

    compile
        typify conditional-select bool i32
        \ 'dump-module 'dump-disassembly #'skip-opts

#test-select-optimization

# return function dynamically
fn test-dynamic-function-return ()
    fn square-brackets (s)
        io-write "["; io-write s; io-write "]"
    fn round-brackets (s)
        io-write "("; io-write s; io-write ")"
    fn bracket (use-square?)
        if (unconst use-square?) square-brackets
        else round-brackets
    fn apply-brackets (f s)
        f s
        io-write "\n"

    apply-brackets (bracket true) "hello"
    apply-brackets (bracket true) "world"
    apply-brackets (bracket false) "hello"
    apply-brackets (bracket false) "world"

#test-dynamic-function-return

# polymorphic return type and inlined type checking
fn test-polymorphic-return-type ()
    fn print-value (value)
        let value-type = (typeof value)
        if (type== value-type i32)
            io-write "<number>\n"
            "hello"
        elseif (type== value-type string)
            io-write value
            io-write "\n"
            false
        else
            io-write "???\n"
    print-value
        print-value
            print-value (unconst 3)

#test-polymorphic-return-type
