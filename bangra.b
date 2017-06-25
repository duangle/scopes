#
    Bangra Interpreter
    Copyright (c) 2017 Leonard Ritter

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to
    deal in the Software without restriction, including without limitation the
    rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
    sell copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.

    This is the bangra boot script. It implements the remaining standard
    functions and macros, parses the command-line and then enters the REPL.

fn/cc type? (_ T)
    icmp== (bitcast type u64) (bitcast (typeof T) u64)

fn/cc type== (_ a b)
    branch (type? a)
        fn/cc (_)
        fn/cc (_)
            compiler-error "type expected"
    branch (type? b)
        fn/cc (_)
        fn/cc (_)
            compiler-error "type expected"
    icmp== (bitcast a u64) (bitcast b u64)

fn/cc assert-typeof (_ a T)
    branch (type== T (typeof a))
        fn/cc (_)
        fn/cc (_)
            compiler-error "type mismatch"

fn/cc string->rawstring (_ s)
    assert-typeof s string
    getelementptr s 0 1 0

# importing C code
call
    fn/cc (_ lib)
        call
            fn/cc (_ sinf printf)
                printf
                    string->rawstring "test: %f\n"
                    sinf 0.5235987755982989
                #printf
                    string->rawstring "fac: %i\n"
                    fac
                        mystify 5
            purify
                Any-extract
                    Scope@ lib 'sinf
            Any-extract
                Scope@ lib 'printf

    import-c "testdata.c" "
        float sinf(float);
        int printf( const char* format, ... );
        "
        \ eol

# deferring remaining expressions to bootstrap parser
#syntax-apply-block
    fn/cc (_ anchor exprs env)
        fn/cc list-empty? (_ l)
            icmp== (ptrtoint l size_t) 0:usize
        fn/cc list-at (_ l)
            branch (list-empty? l)
                fn/cc (_) (Any-wrap none)
                fn/cc (_) (extractvalue (load l) 0)
        fn/cc list-next (_ l)
            branch (list-empty? l)
                fn/cc (_) eol
                fn/cc (_)
                    bitcast (extractvalue (load l) 1) list
        fn/cc list-at-next (_ l)
            branch (list-empty? l)
                fn/cc (_)
                    _ (Any-wrap none) eol
                fn/cc (_)
                    _
                        extractvalue (load l) 0
                        bitcast (extractvalue (load l) 1) list
        fn/cc list-countof (_ l)
            extractvalue (load l) 2

        fn/cc Any-typeof (_ val)
            extractvalue val 0
        fn/cc Any-payload (_ val)
            extractvalue val 1

        fn/cc list? (_ val)
            type== (Any-typeof val) list

        fn/cc Any-extract-list (_ val)
            inttoptr (Any-payload val) list

        fn/cc Any-extract-Syntax (_ val)
            inttoptr (Any-payload val) Syntax

        fn/cc Any-extract-Symbol (_ val)
            bitcast (Any-payload val) Symbol

        fn/cc Any-extract-i32 (_ val)
            trunc (Any-payload val) i32

        fn/cc maybe-unsyntax (_ val)
            branch (type== (Any-typeof val) Syntax)
                fn/cc (_)
                    extractvalue (load (Any-extract-Syntax val)) 1
                fn/cc (_) val

        fn/cc Any-dispatch (return val)
            call
                fn/cc try0 (() T)
                    fn/cc failed ()
                        return none
                    fn/cc try3 ()
                        branch (type== T i32)
                            fn/cc ()
                                return (Any-extract-i32 val)
                            \ failed
                    fn/cc try2 ()
                        branch (type== T Symbol)
                            fn/cc ()
                                return (Any-extract-Symbol val)
                            \ try3
                    fn/cc try1 ()
                        branch (type== T Syntax)
                            fn/cc ()
                                return (Any-extract-Syntax val)
                            \ try2
                    branch (type== T list)
                        fn/cc ()
                            return (Any-extract-list val)
                        \ try1
                Any-typeof val

        fn/cc print-spaces (_ depth)
            branch (icmp== depth 0)
                fn/cc (_)
                fn/cc (_)
                    io-write "    "
                    print-spaces (sub depth 1)

        fn/cc walk-list (_ on-leaf l depth)
            call
                fn/cc loop (_ l)
                    branch (list-empty? l)
                        fn/cc (_) true
                        fn/cc (_)
                            call
                                fn/cc (_ at next)
                                    call
                                        fn/cc (_ value)
                                            branch (list? value)
                                                fn/cc (_)
                                                    print-spaces depth
                                                    io-write ";\n"
                                                    walk-list on-leaf
                                                        Any-extract-list value
                                                        add depth 1
                                                fn/cc (_)
                                                    on-leaf value depth
                                        maybe-unsyntax at
                                    loop next
                                list-at-next l
                \ l

        walk-list
            fn/cc on-leaf (_ value depth)
                print-spaces depth
                Any-dispatch value
                io-write
                    repr value
                io-write "\n"
            mystify exprs
            mystify 0

# static assertion
    branch (type== (typeof 1) i32)
        fn/cc (_)
        fn/cc (_)
            compiler-error "static assertion failed: argument not i32"
    branch (constant? (add 1 2))
        fn/cc (_)
        fn/cc (_)
            compiler-error "static assertion failed: argument not constant"

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

    fac 5

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
                            mystify 5
                purify
                    Any-extract
                        Scope@ lib 'sinf
                Any-extract
                    Scope@ lib 'printf

        import-c "testdata.c" "
            float sinf(float);
            int printf( const char* format, ... );
            "
            \ eol

# mutual recursion
    fn/cc even? (eret ei)
        fn/cc odd? (oret oi)
            branch (icmp>s oi 0)
                fn/cc (_)
                    even? (sub oi 1)
                fn/cc (_)
                    _ false

        branch (icmp>s ei 0)
            fn/cc (_)
                odd? (sub ei 1)
            fn/cc (_)
                _ true

    branch
        even? 31
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
        cc/call loop none 0 n

    puts "hello world" 8

\ none




