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

# basic branching and phi node merging
    fn/cc puts (_ s br)
        io-write s
        io-write
            branch br
                fn/cc (_) "\n"
                fn/cc (_) ""

    puts "hello world, " false
    puts "hello world" true

# tail-recursive program that avoids closures
    fn/cc puts (return s n)
        fn/cc loop (() i)
            branch (icmp== i n)
                fn/cc ()
                    return
                fn/cc ()
                    io-write s
                    io-write "\n"
                    cc/call loop none (add i 1)
        cc/call loop none 0

    puts "hello world" 10

# tail-recursive program that uses closures
    fn/cc print-loop (_ s n)
        fn/cc puts (_ s)
            io-write s
            io-write "\n"
        fn/cc loop (_ i)
            branch (icmp== i n)
                fn/cc (_)
                fn/cc (_)
                    puts s
                    loop (add i 1)
        loop
            mystify 0

    call
        fn/cc (_ txt)
            print-loop txt 5
            print-loop txt 5
        \ "hello world"


# program using closure
    fn/cc puts (return s)
        fn/cc do-stuff (_ t)
            io-write s
            io-write t
        do-stuff "\n"

    puts "hello world"

# passing template as first-order argument
    fn/cc square-brackets (_ s)
        io-write "["; io-write s; io-write "]"
    fn/cc round-brackets (_ s)
        io-write "("; io-write s; io-write ")"
    fn/cc apply-brackets (_ f s)
        f s
        io-write "\n"

    apply-brackets square-brackets "hello"
    apply-brackets square-brackets "world"
    apply-brackets round-brackets "hello"
    apply-brackets round-brackets "world"

# passing builtin as first-order argument
    call
        fn/cc (_ f)
            f "yes\n"
            f "no\n"
        \ io-write

# return function dynamically
    fn/cc square-brackets (_ s)
        io-write "["; io-write s; io-write "]"
    fn/cc round-brackets (_ s)
        io-write "("; io-write s; io-write ")"
    fn/cc bracket (_ use-square?)
        branch use-square?
            fn/cc (_) square-brackets
            fn/cc (_) round-brackets
    fn/cc apply-brackets (_ f s)
        f s
        io-write "\n"

    apply-brackets (bracket true) "hello"
    apply-brackets (bracket true) "world"
    apply-brackets (bracket false) "hello"
    apply-brackets (bracket false) "world"

# locally bound function names
    call
        fn/cc (_ f1 f2)
            f1 "huh"
            f2 "oh"
            f1 "hmm!"
            f2 "oh la la"

        fn/cc (_ s)
            io-write s
            io-write "?\n"
        fn/cc (_ s)
            io-write s
            io-write "!\n"

# branch with constant argument
    branch true
        fn/cc (_)
            io-write "true"
        fn/cc (_)
            io-write "false"

# polymorphic return type and inlined type checking
    fn/cc print-value (_ value)
        call
            fn/cc (_ value-type)
                branch (type== value-type i32)
                    fn/cc (_)
                        io-write "<number>\n"
                        \ "hello"
                    fn/cc (_)
                        branch (type== value-type string)
                            fn/cc (_)
                                io-write value
                                io-write "\n"
                                \ false
                            fn/cc (_)
                                io-write "???\n"
            typeof value
    print-value
        print-value
            print-value 3

# calling a pure c function that returns multiple arguments
    print-number 303
        print-number 1 2 3

# typify
    fn/cc f (_ s)
        io-write s

    typify f string

# church encoding
    fn/cc string->fn (_ s)
        fn/cc (_ f)
            f s

    fn/cc io-write-fn (_ f)
        f io-write

    io-write-fn
        string->fn "hello\n"

# deferring remaining expressions to bootstrap parser
syntax-apply-block
    fn/cc (_ anchor exprs env)
        fn/cc type== (_ a b)
            icmp==
                bitcast a u64
                bitcast b u64

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
        even? 5
        fn/cc (_)
            io-write "even\n"
        fn/cc (_)
            io-write "odd\n"



\ none




