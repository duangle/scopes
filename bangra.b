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
        loop 0

    print-loop "hello world" 5
    print-loop "hello world" 5

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


\ none





