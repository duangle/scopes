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
    fn/cc assert-type (_ T)
        branch (type? T)
            fn/cc (_)
            fn/cc (_)
                compiler-error
                    string-join "type expected, not " (Any-repr (Any-wrap T))
    assert-type a
    assert-type b
    icmp== (bitcast a u64) (bitcast b u64)

fn/cc assert-typeof (_ a T)
    branch (type== T (typeof a))
        fn/cc (_)
        fn/cc (_)
            compiler-error
                string-join "type "
                    string-join (Any-repr (Any-wrap T))
                        string-join " expected, not "
                            Any-repr (Any-wrap (typeof a))

fn/cc string->rawstring (_ s)
    assert-typeof s string
    getelementptr s 0 1 0

fn/cc Any-typeof (_ val)
    extractvalue val 0
fn/cc Any-payload (_ val)
    extractvalue val 1

fn/cc Any-extract-list (_ val)
    inttoptr (Any-payload val) list

fn/cc Any-extract-Syntax (_ val)
    inttoptr (Any-payload val) Syntax

fn/cc Any-extract-Symbol (_ val)
    bitcast (Any-payload val) Symbol

fn/cc Any-extract-i32 (_ val)
    trunc (Any-payload val) i32

fn/cc Any-dispatch (return val)
    call
        fn/cc try0 (_ T)
            fn/cc failed (_)
                return none
            fn/cc try3 (_)
                branch (type== T i32)
                    fn/cc (_)
                        return (Any-extract-i32 val)
                    \ failed
            fn/cc try2 (_)
                branch (type== T Symbol)
                    fn/cc (_)
                        return (Any-extract-Symbol val)
                    \ try3
            fn/cc try1 (_)
                branch (type== T Syntax)
                    fn/cc (_)
                        return (Any-extract-Syntax val)
                    \ try2
            branch (type== T list)
                fn/cc (_)
                    return (Any-extract-list val)
                \ try1
        Any-typeof val

# calling polymorphic function
    Any-dispatch (Any-wrap 10)

# importing C code
#call
    fn/cc (_ lib)
        call
            fn/cc (_ sinf printf)
                printf
                    string->rawstring "test: %f\n"
                    sinf 0.5235987755982989
                #printf
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
        \ eol

# print function
fn/cc print (return ...)
    fn/cc load-printf (_)
        compiler-message "loading printf..."
        call
            fn/cc (_ lib)
                call
                    fn/cc (_ printf)
                        fn/cc (_ fmt ...)
                            printf (string->rawstring fmt) ...
                    Any-extract
                        Scope@ lib 'stb_printf
            import-c "printf.c" "
                int stb_printf(const char *fmt, ...);
                "
                \ eol

    call
        fn/cc (() printf)
            fn/cc print-element (return val)
                call
                    fn/cc (() T)
                        fn/cc fail ()
                            io-write "<value of type "
                            io-write (Any-repr (Any-wrap (typeof val)))
                            io-write ">"
                            return

                        fn/cc try-f32 ()
                            branch (type== T f32)
                                fn/cc ()
                                    printf "%g" val
                                    return
                                \ fail
                        fn/cc try-i32 ()
                            branch (type== T i32)
                                fn/cc ()
                                    printf "%i" val
                                    return
                                \ try-f32
                        branch (type== T string)
                            fn/cc ()
                                io-write val
                                return
                            \ try-i32
                    typeof val
            call
                fn/cc loop (() i)
                    branch (icmp<s i (va-countof ...))
                        fn/cc ()
                            branch (icmp>s i 0)
                                fn/cc (_)
                                    io-write " "
                                fn/cc (_)
                            print-element (unconst (va@ i ...))
                            cc/call loop none (add i 1)
                        fn/cc ()
                            io-write "\n"
                            return
                \ 0
        load-printf

print "the number is" (add 303 606) 1.0
print "one more line"

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

        fn/cc list? (_ val)
            type== (Any-typeof val) list

        fn/cc maybe-unsyntax (_ val)
            branch (type== (Any-typeof val) Syntax)
                fn/cc (_)
                    extractvalue (load (Any-extract-Syntax val)) 1
                fn/cc (_) val



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
                                                    \ true
                                        maybe-unsyntax at
                                    loop next
                                list-at-next l
                \ l

        walk-list
            fn/cc on-leaf (_ value depth)
                print-spaces depth
                #Any-dispatch value
                io-write
                    repr value
                io-write "\n"
            unconst exprs
            unconst 0

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
            \ eol

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
            \ x
        io-write "done stuff.\n"
        \ true

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
            \ n

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




